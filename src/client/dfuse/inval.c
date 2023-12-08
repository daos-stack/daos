/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pthread.h>

#include <math.h>

#include "dfuse_common.h"
#include "dfuse.h"

/* Evict inodes based on timeout
 *
 * Maintain a number of lists for inode timeouts, for each timeout value keep a list of inodes
 * that are using that value, when a inode is refreshed by the kernel then move the inode to the end
 * of the list.
 *
 * Separately have a thread which periodically will walk each list starting at the front and
 * invalidate any entry where the timeout has expired.
 *
 * In this way the lists are never traversed, on access a entry is removed from where it is and
 * appended to the end, and the timeout starts at the front of the list and traverses only as far
 * as it needs to until the front entry is to be kept.
 *
 * Locking: The ival_lock is contended, it is accessed from:
 *  ie_close() which is called from forget and some failure paths in readdir()
 *  lookup() to move entries to the end of this list.
 *  de_run() to pull items from the front of the list.
 *
 * Wakeup: The thread is woken up when:
 *  dfuse is exiting.
 *  a new timeout value is added.
 *  something is added to an empty list.
 * A thread chooses how long to sleep based on what's on the list.
 */

/* Represents one timeout value (time).  Maintains a ordered list of dentries that are using
 * this timeout
 */
struct dfuse_time_entry {
	d_list_t inode_list;
	double   time;
	d_list_t dte_list;
};

struct inode_core {
	char       name[NAME_MAX + 1];
	fuse_ino_t parent;
};

struct dfuse_ival {
	d_list_t             time_entry_list;
	struct fuse_session *session;
};

#define EVICT_COUNT 8

static pthread_mutex_t   ival_lock = PTHREAD_MUTEX_INITIALIZER;
static bool              ival_stop;
static pthread_t         ival_thread;
static sem_t             ival_sem;
static struct dfuse_ival ival_data;

/* Eviction loop, run periodically in it's own thread
 * TODO: Use array rather than list for buckets.
 * TODO: Set evicton timeout, max(time * 1.1, 10)?
 *
 * Future work:
 * Better handling of eviction timeouts, "max(time * 1.1, 10)" or similar might be nice.
 * Have containers reference count the timeout buckets.
 * Use a list rather than arrays for timeout buckets.
 * Duplicate entire functionality for data cache?
 * Rather than invalidate data perhaps re-fresh it?
 *
 * Returns true if there is more work to do.  If false then *sleep_time is set in seconds.
 */
static bool
ival_loop(int *sleep_time)
{
	struct dfuse_time_entry *dte;
	struct inode_core        ic[EVICT_COUNT] = {};
	int                      idx             = 0;
	double                   sleep           = (60 * 1) - 1;

	D_MUTEX_LOCK(&ival_lock);

	/* Walk the list, oldest first */
	d_list_for_each_entry(dte, &ival_data.time_entry_list, dte_list) {
		struct dfuse_inode_entry *inode, *inodep;

		DFUSE_TRA_DEBUG(dte, "Iterating for timeout %lf", dte->time);

		d_list_for_each_entry_safe(inode, inodep, &dte->inode_list, ie_evict_entry) {
			double timeout;

			if (dfuse_dentry_get_valid(inode, dte->time, &timeout)) {
				DFUSE_TRA_DEBUG(inode, "Keeping left %lf " DF_DE, timeout,
						DP_DE(inode->ie_name));
				if (timeout < sleep)
					sleep = timeout;
				break;
			}

			if (atomic_load_relaxed(&inode->ie_open_count) != 0) {
				DFUSE_TRA_DEBUG(inode, "File is open " DF_DE,
						DP_DE(inode->ie_name));
				continue;
			}

			/* Log the mode here, but possibly just evict dirs anyway */
			ic[idx].parent = inode->ie_parent;
			strncpy(ic[idx].name, inode->ie_name, NAME_MAX + 1);
			ic[idx].name[NAME_MAX] = '\0';

			d_list_del_init(&inode->ie_evict_entry);

			idx++;

			if (idx == EVICT_COUNT)
				goto out;
		}
	}
out:
	*sleep_time = (int)round(sleep + 0.5);

	DFUSE_TRA_DEBUG(&ival_data, "Unlocking, allowing to sleep for %d seconds", *sleep_time);
	D_MUTEX_UNLOCK(&ival_lock);

	if (idx == 0)
		return false;

	for (int i = 0; i < idx; i++) {
		int rc;

		DFUSE_TRA_DEBUG(&ival_data, "Evicting entry %#lx " DF_DE, ic[i].parent,
				DP_DE(ic[i].name));

		rc = fuse_lowlevel_notify_inval_entry(ival_data.session, ic[i].parent, ic[i].name,
						      strnlen(ic[i].name, NAME_MAX));
		if (rc && rc != -ENOENT)
			DHS_ERROR(&ival_data, -rc, "notify_inval_entry() failed");
	}

	return true;
}

/* Main loop for eviction thread.  Spins until ready for exit waking after one second and iterates
 * over all newly expired dentries.
 */
static void *
ival_thread_fn(void *arg)
{
	int sleep_time = 1;

	while (1) {
		struct timespec ts = {};
		int             rc;

		if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
			D_ERROR("Unable to set time");
		ts.tv_sec += sleep_time;

		rc = sem_timedwait(&ival_sem, &ts);
		if (rc == 0) {
			if (ival_stop)
				return NULL;
		} else {
			rc = errno;

			if (errno != ETIMEDOUT)
				DS_ERROR(rc, "sem_wait");
		}

		while (ival_loop(&sleep_time))
			;
		if (sleep_time < 2)
			sleep_time = 2;
		DFUSE_TRA_DEBUG(&ival_data, "Sleeping %d", sleep_time);
	}
	return NULL;
}

/* Allocate and insert a new time value entry */
static int
ival_bucket_add(d_list_t *list, double timeout)
{
	struct dfuse_time_entry *dte;

	D_ALLOC_PTR(dte);
	if (dte == NULL)
		return ENOMEM;

	DFUSE_TRA_UP(dte, &ival_data, "time bucket");

	dte->time = timeout;
	D_INIT_LIST_HEAD(&dte->inode_list);

	d_list_add_tail(&dte->dte_list, list);
	return 0;
}

/* Sets up the initial data structures, after this ival_add_cont_buckets() may be called before
 * ival_thread_start().
 */
int
ival_init(struct dfuse_info *dfuse_info)
{
	int rc;

	DFUSE_TRA_UP(&ival_data, dfuse_info, "invalidator");

	D_INIT_LIST_HEAD(&ival_data.time_entry_list);

	rc = sem_init(&ival_sem, 0, 0);
	if (rc != 0) {
		rc = errno;
		goto out;
	}

	rc = ival_bucket_add(&ival_data.time_entry_list, 0);
out:
	return rc;
}

/* Start the thread.  Not called until after fuse is mounted */
int
ival_thread_start(struct dfuse_info *dfuse_info)
{
	int rc;

	ival_data.session = dfuse_info->di_session;

	rc = pthread_create(&ival_thread, NULL, ival_thread_fn, NULL);
	if (rc != 0)
		goto out;
	pthread_setname_np(ival_thread, "invalidator");

out:
	return rc;
}

/* Stop thread, remove all inodes from the invalidation queues and teardown all data structures */
void
ival_thread_stop()
{
	struct dfuse_time_entry *dte, *dtep;

	ival_stop = true;
	/* Stop and drain evict queues */
	sem_post(&ival_sem);

	pthread_join(ival_thread, NULL);

	/* Walk the list, oldest first */
	d_list_for_each_entry_safe(dte, dtep, &ival_data.time_entry_list, dte_list) {
		struct dfuse_inode_entry *inode, *inodep;

		d_list_for_each_entry_safe(inode, inodep, &dte->inode_list, ie_evict_entry)
			d_list_del_init(&inode->ie_evict_entry);

		d_list_del(&dte->dte_list);
		D_FREE(dte);
	}
	DFUSE_TRA_DOWN(&ival_data);
}

/* Update the invalidation time for an inode */
int
ival_update_inode(struct dfuse_inode_entry *inode, double timeout)
{
	struct dfuse_time_entry *dte;
	struct timespec          now;
	bool                     wake = false;

	if (S_ISDIR(inode->ie_stat.st_mode))
		timeout += 5;
	else
		timeout += 2;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

	D_MUTEX_LOCK(&ival_lock);
	inode->ie_dentry_last_update = now;

	/* Walk each timeout value
	 * These go longest to shortest so walk the list until one is found where the value is
	 * lower than we're looking for.
	 */
	d_list_for_each_entry(dte, &ival_data.time_entry_list, dte_list) {
		if (dte->time > timeout)
			continue;

		if (d_list_empty(&dte->inode_list))
			wake = true;

		DFUSE_TRA_DEBUG(inode, "timeout %lf wake " DF_BOOL " %#lx " DF_DE, timeout,
				DP_BOOL(wake), inode->ie_parent, DP_DE(inode->ie_name));

		d_list_move_tail(&inode->ie_evict_entry, &dte->inode_list);
		break;
	}

	D_MUTEX_UNLOCK(&ival_lock);

	if (wake)
		sem_post(&ival_sem);

	return 0;
}

/* Ensure there's a timeout list for the given value.
 * Check if one exists already, and if it does not the insert it into the right location.
 *
 * Returns a system error code.
 */
static int
ival_bucket_add_value(double timeout)
{
	struct dfuse_time_entry *dte;
	double                   lower = -1;
	int                      rc    = 0;
	bool                     wake  = false;

	DFUSE_TRA_INFO(&ival_data, "Setting up timeout queue for %lf", timeout);

	D_MUTEX_LOCK(&ival_lock);

	/* Walk smallest to largest */
	d_list_for_each_entry_reverse(dte, &ival_data.time_entry_list, dte_list) {
		if (dte->time == timeout)
			D_GOTO(out, rc = -DER_SUCCESS);
		if (dte->time < timeout)
			lower = dte->time;
		if (dte->time > timeout)
			break;
	}

	wake = true;
	if (lower == -1) {
		rc = ival_bucket_add(&ival_data.time_entry_list, timeout);
		goto out;
	}

	d_list_for_each_entry_reverse(dte, &ival_data.time_entry_list, dte_list) {
		if (dte->time < lower)
			continue;

		rc = ival_bucket_add(&dte->dte_list, timeout);
		break;
	}

out:
	D_MUTEX_UNLOCK(&ival_lock);

	/* Now wake the evict thread to re-scan the new list */
	if (wake && ival_thread)
		sem_post(&ival_sem);

	return rc;
}

/* Ensure the correct buckets exist for a attached container */
int
ival_add_cont_buckets(struct dfuse_cont *dfc)
{
	int rc, rc2;

	rc  = ival_bucket_add_value(dfc->dfc_dentry_timeout + 2);
	rc2 = ival_bucket_add_value(dfc->dfc_dentry_dir_timeout + 5);

	return rc ? rc : rc2;
}

void
ival_drop_inode(struct dfuse_inode_entry *ie)
{
	D_MUTEX_LOCK(&ival_lock);
	if (!d_list_empty(&ie->ie_evict_entry))
		d_list_del(&ie->ie_evict_entry);
	D_MUTEX_UNLOCK(&ival_lock);
}
