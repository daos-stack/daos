/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pthread.h>

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
 * Locking: The dte_lock is contended, it is accessed from:
 *  ie_close() which is called from forget and some failure paths in readdir()
 *  lookup() to move entries to the end of this list.
 *  de_run() to pull items from the front of the list.
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

#define EVICT_COUNT 8

/* Eviction loop, run periodically in it's own thread
 * TODO: Have this function return a time-to-sleep rather than waking up every second.
 * TODO: Have de_add_value() wakeup this thread.
 * TODO: Use array rather than list for buckets.
 * TODO: Set evicton timeout, max(time * 1.1, 10)?
 * TODO: Inval files as well as dirs if dentry time is different?
 * TODO: More lee-way for files.
 */
static bool
dfuse_de_run(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *parent)
{
	struct dfuse_time_entry *dte;
	struct inode_core        ic[EVICT_COUNT] = {};
	int                      idx             = 0;

	D_MUTEX_LOCK(&dfuse_info->di_dte_lock);

	/* Walk the list, oldest first */
	d_list_for_each_entry(dte, &dfuse_info->di_dtes, dte_list) {
		struct dfuse_inode_entry *inode, *inodep;

		DFUSE_TRA_DEBUG(dte, "Iterating for timeout %lf", dte->time);

		d_list_for_each_entry_safe(inode, inodep, &dte->inode_list, ie_evict_entry) {
			double timeout;

			if (dfuse_dentry_get_valid(inode, dte->time, &timeout)) {
				DFUSE_TRA_DEBUG(inode, "Keeping left %lf " DF_DE, timeout,
						DP_DE(inode->ie_name));
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
	DFUSE_TRA_DEBUG(dfuse_info, "Unlocking");
	D_MUTEX_UNLOCK(&dfuse_info->di_dte_lock);

	if (idx == 0)
		return false;

	for (int i = 0; i < idx; i++) {
		int rc;

		DFUSE_TRA_DEBUG(dfuse_info, "Evicting entry %#lx " DF_DE, ic[i].parent,
				DP_DE(ic[i].name));

		rc = fuse_lowlevel_notify_inval_entry(dfuse_info->di_session, ic[i].parent,
						      ic[i].name, strnlen(ic[i].name, NAME_MAX));
		if (rc && rc != -ENOENT)
			DHS_ERROR(dfuse_info, -rc, "notify_delete() failed");
	}

	return true;
}

/* Remove all inodes from the evict queues. */
void
dfuse_de_stop(struct dfuse_info *dfuse_info)
{
	struct dfuse_time_entry *dte, *dtep;

	/* Walk the list, oldest first */
	d_list_for_each_entry_safe(dte, dtep, &dfuse_info->di_dtes, dte_list) {
		struct dfuse_inode_entry *inode, *inodep;

		d_list_for_each_entry_safe(inode, inodep, &dte->inode_list, ie_evict_entry)
			d_list_del_init(&inode->ie_evict_entry);

		d_list_del(&dte->dte_list);
		D_FREE(dte);
	}
}

/* Main loop for eviction thread.  Spins until ready for exit waking after one second and iterates
 * over all newly expired dentries.
 */
void *
dfuse_evict_thread(void *arg)
{
	struct dfuse_info *dfuse_info = arg;

	while (1) {
		struct timespec ts = {};
		int             rc;

		if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
			D_ERROR("Unable to set time");
		ts.tv_sec += 1;

		rc = sem_timedwait(&dfuse_info->di_dte_sem, &ts);
		if (rc == 0) {
			return NULL;
		} else {
			rc = errno;

			if (errno != ETIMEDOUT)
				DS_ERROR(rc, "sem_wait");
		}

		while (dfuse_de_run(dfuse_info, 0))
			;
		DFUSE_TRA_INFO(dfuse_info, "Sleeping");
	}
	return NULL;
}

int
dfuse_update_inode_time(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *inode,
			double timeout)
{
	struct dfuse_time_entry *dte;
	struct timespec          now;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

	D_MUTEX_LOCK(&dfuse_info->di_dte_lock);
	inode->ie_dentry_last_update = now;

	/* Walk each timeout value
	 * These go longest to shortest so walk the list until one is found where the value is
	 * lower than we're looking for.
	 */
	d_list_for_each_entry(dte, &dfuse_info->di_dtes, dte_list) {
		if (dte->time > timeout)
			continue;

		DFUSE_TRA_INFO(inode, "Putting at tail %#lx " DF_DE " timeout %lf",
			       inode->ie_parent, DP_DE(inode->ie_name), timeout);

		d_list_move_tail(&inode->ie_evict_entry, &dte->inode_list);
		break;
	}

	D_MUTEX_UNLOCK(&dfuse_info->di_dte_lock);

	return 0;
}

static int
dfuse_de_add(struct dfuse_info *dfuse_info, d_list_t *list, double timeout)
{
	struct dfuse_time_entry *dte;

	D_ALLOC_PTR(dte);
	if (dte == NULL)
		return -DER_NOMEM;

	DFUSE_TRA_UP(dte, dfuse_info, "time bucket");

	dte->time = timeout;
	D_INIT_LIST_HEAD(&dte->inode_list);

	d_list_add_tail(&dte->dte_list, list);
	return -DER_SUCCESS;
}

/* Ensure there's a timeout list for the given value.
 * Check if one exists already, and if it does not the insert it into the right location.
 */
int
dfuse_de_add_value(struct dfuse_info *dfuse_info, double timeout)
{
	struct dfuse_time_entry *dte;
	double                   lower = -1;
	int                      rc    = -DER_SUCCESS;

	DFUSE_TRA_INFO(dfuse_info, "Setting up timeout queue for %lf", timeout);

	D_MUTEX_LOCK(&dfuse_info->di_dte_lock);

	/* Walk smallest to largest */
	d_list_for_each_entry_reverse(dte, &dfuse_info->di_dtes, dte_list)
	{
		if (dte->time == timeout)
			D_GOTO(out, rc = -DER_SUCCESS);
		if (dte->time < timeout)
			lower = dte->time;
		if (dte->time > timeout)
			break;
	}

	if (lower == -1) {
		rc = dfuse_de_add(dfuse_info, &dfuse_info->di_dtes, timeout);
		goto out;
	}

	d_list_for_each_entry_reverse(dte, &dfuse_info->di_dtes, dte_list)
	{
		if (dte->time < lower)
			continue;

		rc = dfuse_de_add(dfuse_info, &dte->dte_list, timeout);
		break;
	}

out:
	D_MUTEX_UNLOCK(&dfuse_info->di_dte_lock);

	return rc;
}
