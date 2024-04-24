/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pthread.h>

#include <math.h>

#include "dfuse_common.h"
#include "dfuse.h"

/* Evict inodes based on timeout.
 *
 * The goal here is to have dfuse release resources over time, rather than the default which would
 * be the kernel keeps a reference on everything until there's memory pressure (effectively
 * forever) then instruct the kernel to forget things which have expired.
 *
 * This reduces both kernel memory and dfuse memory, keeps the hash table much smaller and allows
 * dfuse to close containers and disconnect from pools, meaning that at idle dfuse resource
 * consumption tends to zero.
 *
 * For kernel based filesystems there is a benefit to keeping all this data in memory as it can
 * simply be re-validated before use however with fuse + DAOS then re-validate is the same cost
 * as lookup so there is no benefit in keeping this data around.
 *
 * Maintain a number of lists for inode timeouts, for each timeout value keep a list of inodes
 * that are using that value, when a inode is refreshed by the kernel then move the inode to the end
 * of the correct list.
 *
 * Separately have a thread which periodically will walk each list starting at the front and
 * invalidate any entries where the timeout has expired.
 *
 * In this way the lists are never traversed, on access a entry is removed from where it is and
 * appended to the end, and the timeout starts at the front of the list and traverses only as far
 * as it needs to until the front entry is to be kept.
 *
 *
 * As lookups will not be repeated by the kernel until after timeout has expired allow some leeway
 * before eviction to allow re-validation of in-use datasets without triggering entire tree
 * invalidations through the kernel.  Directories get five seconds, anything else two.  Ideally
 * directories would be invalidated first as this would result in less dfuse->kernel calls as once
 * the kernel invalidates directories then it invalidates the whole tree below that, however there
 * are also use-cases where there are significiant numbers of files per directory where the
 * directory is in active use but individual files are not.
 *
 * Future work might be to speculatively perform lookups close to the end of the timeout period,
 * then if a entry was in frequent use it's lookup could be performed from memory, effectively
 * moving the re-validation cost off the critical path.  This code currently only handles dentries
 * but could also separately track attributes (inodes) and file contents as well.
 *
 * Additional changes to consider in the future could include:
 *  Better handing of eviction timeouts, "max(time * 1.1, 10)" would be better than a flat +x/+5
 *  Use arrays rather than lists for the buckets for faster iteration.
 *
 * Locking: The ival_lock is contended, it is accessed several places, however none do any more
 * than list management.  As inodes might be removed from one list and re-inserted into another
 * there is a pre subsystem lock rather than per list locks.
 *  ie_close() which is called from forget and some failure paths in readdir()
 *  lookup() to move entries to the end of this list.
 *  de_run() to pull items from the front of the list.
 *
 * Wakeup: The invalidation thread is woken up when:
 *  dfuse is exiting.
 *  something is added to an empty list.
 *  after a timeout.
 * Timeouts are chosen based on the entries still on any list, dfuse will sleep as long as it can
 * but at least 2 seconds and at most 60.
 * As this relates to releasing resources there is no additional benefit in finer grained time
 * control than this.
 */

/* Grace period before invalidating directories or non-directories.  Needs to be long enough so that
 * entries in the working set are not invalidated but short enough to be meaningful.
 */
#define INVAL_DIRECTORY_GRACE (60 * 30)
#define INVAL_FILE_GRACE      2

/* Represents one timeout value (time).  Maintains a ordered list of dentries that are using
 * this timeout.
 */
struct dfuse_time_entry {
	d_list_t inode_list;
	double   time;
	d_list_t dte_list;
	int      ref;
};

/* Core data structure, maintains a list of struct dfuse_time_entry lists */
struct dfuse_ival {
	d_list_t             time_entry_list;
	struct fuse_session *session;
	bool                 session_dead;
};

/* The core data from struct dfuse_inode_entry.  No additional inode references are held on inodes
 * because of there place on invalidate lists, rather inodes are removed from any list on close.
 * Therefore once a decision is made to evict an inode then a copy of the data is needed as once
 * the ival_lock is dropped the inode could be freed.  This is not a problem if this happens as the
 * kernel will simply return ENOENT.
 */
struct inode_core {
	char       name[NAME_MAX + 1];
	fuse_ino_t parent;
};

/* Number of dentries to invalidate per iteration. This value affects how long the lock is held,
 * after the invalidations happen then another iteration will start immediately.  Invalidation of
 * directories however trigger many forget calls so we want to make use of this where possible so
 * keep this batch size small.
 */
#define EVICT_COUNT 8

static pthread_mutex_t   ival_lock = PTHREAD_MUTEX_INITIALIZER;
static bool              ival_stop;
static pthread_t         ival_thread;
static sem_t             ival_sem;
static struct dfuse_ival ival_data;

/* Eviction loop, run periodically in it's own thread
 *
 * Returns true if there is more work to do.  If false then *sleep_time is set in seconds.
 */
static bool
ival_loop(int *sleep_time)
{
	struct dfuse_time_entry *dte, *dtep;
	struct inode_core        ic[EVICT_COUNT] = {};
	int                      idx             = 0;
	double                   sleep           = (60 * 1) - 1;

	D_MUTEX_LOCK(&ival_lock);

	/* Walk the list, oldest first */
	d_list_for_each_entry_safe(dte, dtep, &ival_data.time_entry_list, dte_list) {
		struct dfuse_inode_entry *inode, *inodep;

		DFUSE_TRA_DEBUG(dte, "Iterating for timeout %.1lf ref %d", dte->time, dte->ref);

		if (dte->ref == 0 && d_list_empty(&dte->inode_list)) {
			d_list_del(&dte->dte_list);
			D_FREE(dte);
			continue;
		}

		d_list_for_each_entry_safe(inode, inodep, &dte->inode_list, ie_evict_entry) {
			double timeout;

			if (dfuse_dentry_get_valid(inode, dte->time, &timeout)) {
				DFUSE_TRA_DEBUG(inode, "Keeping left %.1lf " DF_DE, timeout,
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

	if (idx == 0 || ival_data.session_dead)
		return false;

	for (int i = 0; i < idx; i++) {
		int rc;

		DFUSE_TRA_DEBUG(&ival_data, "Evicting entry %#lx " DF_DE, ic[i].parent,
				DP_DE(ic[i].name));

		rc = fuse_lowlevel_notify_inval_entry(ival_data.session, ic[i].parent, ic[i].name,
						      strnlen(ic[i].name, NAME_MAX));
		if (rc && rc != -ENOENT && rc != -EBADF)
			DHS_ERROR(&ival_data, -rc, "notify_inval_entry() failed");
		if (rc == -EBADF)
			ival_data.session_dead = true;
	}

	return (idx == EVICT_COUNT);
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
	dte->ref  = 1;
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
	if (rc != 0)
		D_GOTO(out, rc = errno);

	rc = ival_bucket_add(&ival_data.time_entry_list, 0);
	if (rc)
		goto sem;

out:
	return rc;
sem:
	sem_destroy(&ival_sem);
	DFUSE_TRA_DOWN(&ival_data);
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
	pthread_setname_np(ival_thread, "dfuseinval");

out:
	return rc;
}

/* Stop thread, remove all inodes from the invalidation queues and teardown all data structures
 * May be called without thread_start() having been called.
 */
void
ival_thread_stop()
{
	ival_stop = true;
	/* Stop and drain evict queues */
	sem_post(&ival_sem);

	if (ival_thread)
		pthread_join(ival_thread, NULL);
	ival_thread = 0;
}

void
ival_fini()
{
	struct dfuse_time_entry *dte, *dtep;

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
		timeout += INVAL_DIRECTORY_GRACE;
	else
		timeout += INVAL_FILE_GRACE;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

	D_MUTEX_LOCK(&ival_lock);
	inode->ie_dentry_last_update = now;

	/* Walk each timeout value
	 * These go longest to shortest so walk the list until one is found where the value is
	 * lower than we're looking for.
	 */
	d_list_for_each_entry(dte, &ival_data.time_entry_list, dte_list) {
		/* If the entry is draining then do not add any new entries to it */
		if (dte->ref == 0)
			continue;

		if (dte->time > timeout)
			continue;

		if (d_list_empty(&dte->inode_list))
			wake = true;

		DFUSE_TRA_DEBUG(inode, "timeout %.1lf wake:" DF_BOOL " %#lx " DF_DE, timeout,
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
	int                      rc    = -DER_SUCCESS;

	DFUSE_TRA_DEBUG(&ival_data, "Setting up timeout queue for %.1lf", timeout);

	/* Walk smallest to largest */
	d_list_for_each_entry_reverse(dte, &ival_data.time_entry_list, dte_list) {
		if (dte->time == timeout) {
			dte->ref += 1;
			goto out;
		}
		if (dte->time < timeout)
			lower = dte->time;
		if (dte->time > timeout)
			break;
	}

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
	return rc;
}

static void
ival_bucket_dec_value(double timeout)
{
	struct dfuse_time_entry *dte;

	DFUSE_TRA_DEBUG(&ival_data, "Dropping ref for %.1lf", timeout);

	d_list_for_each_entry(dte, &ival_data.time_entry_list, dte_list) {
		if (dte->time == timeout) {
			dte->ref--;
			DFUSE_TRA_DEBUG(&ival_data, "Dropped ref on %.1lf to %d", timeout,
					dte->ref);
			return;
		}
	}

	DFUSE_TRA_ERROR(&ival_data, "Unable to find ref for %.1lf", timeout);
}

/* Ensure the correct buckets exist for a attached container.  Pools have a zero dentry timeout
 * so skip zero values
 */
int
ival_add_cont_buckets(struct dfuse_cont *dfc)
{
	int rc;

	D_MUTEX_LOCK(&ival_lock);

	rc = ival_bucket_add_value(dfc->dfc_dentry_dir_timeout + INVAL_DIRECTORY_GRACE);
	if (rc != 0)
		goto out;
	if (dfc->dfc_dentry_timeout != 0) {
		rc = ival_bucket_add_value(dfc->dfc_dentry_timeout + INVAL_FILE_GRACE);
		if (rc != 0)
			ival_bucket_dec_value(dfc->dfc_dentry_dir_timeout + INVAL_DIRECTORY_GRACE);
	}

out:
	D_MUTEX_UNLOCK(&ival_lock);

	return rc;
}

void
ival_dec_cont_buckets(struct dfuse_cont *dfc)
{
	D_MUTEX_LOCK(&ival_lock);
	if (dfc->dfc_dentry_timeout != 0)
		ival_bucket_dec_value(dfc->dfc_dentry_timeout + INVAL_FILE_GRACE);
	ival_bucket_dec_value(dfc->dfc_dentry_dir_timeout + INVAL_DIRECTORY_GRACE);
	D_MUTEX_UNLOCK(&ival_lock);
}

/* Called from ie_close() to remove inode from any possible list */
void
ival_drop_inode(struct dfuse_inode_entry *ie)
{
	D_MUTEX_LOCK(&ival_lock);
	if (!d_list_empty(&ie->ie_evict_entry))
		d_list_del(&ie->ie_evict_entry);
	D_MUTEX_UNLOCK(&ival_lock);
}
