/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pthread.h>

#include <math.h>

#include "dfuse_common.h"
/* Allow this file to call the raw fuse_lowlevel_notify_* symbols. */
#define DFUSE_NOTIFY_RAW_OK
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
#define INVAL_DIRECTORY_GRACE (60 * 60 * 24 * 365 * 20) /* 20 years to avoid getcwd failures */
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
	ATOMIC bool          session_dead;
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

	if (idx == 0 || atomic_load_relaxed(&ival_data.session_dead))
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
			atomic_store_relaxed(&ival_data.session_dead, true);
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

/* Advisory reverse notifications.
 *
 * The fuse_lowlevel_notify_*() calls block in the kernel on inode locks that in-flight requests
 * hold while waiting on dfuse, so request handlers must never issue one directly; they enqueue
 * here and a dedicated thread delivers.  Nothing waits on delivery: a stalled or dropped entry
 * leaves a dentry cached until its normal timeout, the same as a failed kernel call.  Duplicate
 * queued entries coalesce; an entry leaves the coalesce index when popped, before delivery, so
 * an enqueue racing delivery is never absorbed into a stale notification.
 */
enum notify_kind {
	NOTIFY_INVAL_ENTRY,
	NOTIFY_DELETE,
	NOTIFY_INVAL_INODE,
};

/* Queued by copy; the source inode may be released once enqueued */
struct notify_entry {
	d_list_t         ne_list;
	d_list_t         ne_hlist; /* coalesce bucket, valid only while queued */
	enum notify_kind ne_kind;
	fuse_ino_t       ne_parent;
	fuse_ino_t       ne_ino;
	char             ne_name[NAME_MAX + 1];
	size_t           ne_namelen;
};

/* Drop rather than grow without bound; invalidation is advisory */
#define NOTIFY_QUEUE_MAX        16384

/* Depth at which delivery is judged stalled rather than merely busy */
#define NOTIFY_CONGEST_HIGH     (NOTIFY_QUEUE_MAX / 2)

#define NOTIFY_COALESCE_BUCKETS 4096

static pthread_mutex_t     notify_lock = PTHREAD_MUTEX_INITIALIZER;
static d_list_t            notify_queue;
static d_list_t            notify_coalesce[NOTIFY_COALESCE_BUCKETS];
static sem_t               notify_sem;
static bool                notify_stop;
static unsigned int        notify_queued;
static pthread_t           notify_thread;
static struct d_slab_type *notify_slab;

/* Count of enqueues seen at/above NOTIFY_CONGEST_HIGH; gates the congestion warning rate.
 * Under notify_lock.
 */
static uint64_t            notify_congest_total;

/* Trace identity; carries dfuse_info for the teardown paths, which take no arguments */
static struct dfuse_notify {
	struct dfuse_info *dn_info;
} notify_data;

/* FNV-1a over the coalesce key; 64-bit inos are folded as two 32-bit halves */
static uint32_t
notify_hash(enum notify_kind kind, fuse_ino_t parent, fuse_ino_t ino, const char *name,
	    size_t namelen)
{
	uint32_t h = 2166136261u;
	size_t   i;

	h = (h ^ (uint32_t)kind) * 16777619u;
	h = (h ^ (uint32_t)parent) * 16777619u;
	h = (h ^ (uint32_t)(parent >> 32)) * 16777619u;
	h = (h ^ (uint32_t)ino) * 16777619u;
	h = (h ^ (uint32_t)(ino >> 32)) * 16777619u;
	for (i = 0; i < namelen; i++)
		h = (h ^ (unsigned char)name[i]) * 16777619u;

	return h % NOTIFY_COALESCE_BUCKETS;
}

/* Must be called with notify_lock held. */
static struct notify_entry *
notify_coalesce_find(enum notify_kind kind, fuse_ino_t parent, fuse_ino_t ino, const char *name,
		     size_t namelen, uint32_t bucket)
{
	struct notify_entry *ne;

	d_list_for_each_entry(ne, &notify_coalesce[bucket], ne_hlist) {
		if (ne->ne_kind != kind || ne->ne_parent != parent || ne->ne_ino != ino)
			continue;
		if (ne->ne_namelen != namelen)
			continue;
		if (namelen != 0 && memcmp(ne->ne_name, name, namelen) != 0)
			continue;
		return ne;
	}

	return NULL;
}

/* Must be called with notify_lock held. */
static struct notify_entry *
notify_dequeue_locked(void)
{
	struct notify_entry *ne;

	if (d_list_empty(&notify_queue))
		return NULL;

	ne = d_list_entry(notify_queue.next, struct notify_entry, ne_list);
	d_list_del(&ne->ne_list);
	d_list_del(&ne->ne_hlist);
	notify_queued--;

	return ne;
}

/* Discard everything queued, at stop or after EBADF.  Must be called with notify_lock held. */
static void
notify_drain_locked(void)
{
	struct notify_entry *ne, *nep;
	unsigned int         count = 0;

	d_list_for_each_entry_safe(ne, nep, &notify_queue, ne_list) {
		d_list_del(&ne->ne_list);
		d_list_del(&ne->ne_hlist);
		d_slab_release(notify_slab, ne);
		count++;
	}
	notify_queued = 0;

	if (count == 0)
		return;

	atomic_fetch_add_relaxed(&notify_data.dn_info->di_notify_dropped, count);
	DFUSE_TRA_DEBUG(&notify_data, "Drained %u queued notifications", count);
}

/* Rate-limited: a mass drop must not flood the log */
static void
notify_drop(struct dfuse_info *dfuse_info, enum notify_kind kind, fuse_ino_t parent)
{
	uint64_t total;

	total = atomic_fetch_add_relaxed(&dfuse_info->di_notify_dropped, 1) + 1;

	if (total == 1 || (total % 1000) == 0)
		DFUSE_TRA_WARNING(&notify_data,
				  "Dropped advisory notify kind %d parent %#lx, %lu total", kind,
				  parent, total);
}

/* Rate-limited: sustained congestion must not flood the log.  Called with notify_lock held. */
static void
notify_congest_warn(unsigned int depth)
{
	notify_congest_total++;

	if (notify_congest_total == 1 || (notify_congest_total % 4096) == 0)
		DFUSE_TRA_WARNING(&notify_data, "Notify queue congested: depth %u", depth);
}

/* Blocking here is expected; only the notify thread runs this */
static void
notify_run(struct notify_entry *ne)
{
	int rc = 0;

	switch (ne->ne_kind) {
	case NOTIFY_INVAL_ENTRY:
		rc = fuse_lowlevel_notify_inval_entry(ival_data.session, ne->ne_parent, ne->ne_name,
						      ne->ne_namelen);
		break;
	case NOTIFY_DELETE:
		rc = fuse_lowlevel_notify_delete(ival_data.session, ne->ne_parent, ne->ne_ino,
						 ne->ne_name, ne->ne_namelen);
		break;
	case NOTIFY_INVAL_INODE:
		rc = fuse_lowlevel_notify_inval_inode(ival_data.session, ne->ne_ino, 0, 0);
		break;
	}

	/* Session is gone; latch (shared with ival_loop) and let the loop drain */
	if (rc == -EBADF) {
		atomic_store_relaxed(&ival_data.session_dead, true);
		atomic_fetch_add_relaxed(&notify_data.dn_info->di_notify_dropped, 1);
		return;
	}

	/* ENOENT/ENOTDIR just mean nothing was cached; any other error is unexpected and does
	 * not count as delivered.
	 */
	if (rc != 0 && rc != -ENOENT && rc != -ENOTDIR) {
		DHS_ERROR(&notify_data, -rc, "notify() failed, kind %d parent %#lx", ne->ne_kind,
			  ne->ne_parent);
		return;
	}

	atomic_fetch_add_relaxed(&notify_data.dn_info->di_notify_delivered, 1);
}

static void *
notify_thread_fn(void *arg)
{
	while (1) {
		struct notify_entry *ne = NULL;
		bool                 stop;
		bool                 dead;

		if (sem_wait(&notify_sem) != 0) {
			D_ASSERTF(errno == EINTR, "sem_wait: %d (%s)\n", errno, strerror(errno));
			continue;
		}

		D_MUTEX_LOCK(&notify_lock);
		stop = notify_stop;
		dead = atomic_load_relaxed(&ival_data.session_dead);
		if (stop || dead)
			notify_drain_locked();
		else
			ne = notify_dequeue_locked();
		D_MUTEX_UNLOCK(&notify_lock);

		if (stop)
			return NULL;

		if (ne == NULL)
			continue;

		notify_run(ne);
		d_slab_release(notify_slab, ne);
	}
	return NULL;
}

/* Best effort; failures result in dropped notifications. */
static void
notify_enqueue(struct dfuse_info *dfuse_info, enum notify_kind kind, fuse_ino_t parent,
	       fuse_ino_t ino, const char *name)
{
	struct notify_entry *ne;
	size_t               namelen = name ? strnlen(name, NAME_MAX) : 0;
	uint32_t             bucket  = notify_hash(kind, parent, ino, name, namelen);

	D_MUTEX_LOCK(&notify_lock);

	if (notify_stop) {
		D_MUTEX_UNLOCK(&notify_lock);
		notify_drop(dfuse_info, kind, parent);
		return;
	}

	if (notify_coalesce_find(kind, parent, ino, name, namelen, bucket) != NULL) {
		D_MUTEX_UNLOCK(&notify_lock);
		atomic_fetch_add_relaxed(&dfuse_info->di_notify_coalesced, 1);
		DFUSE_TRA_DEBUG(&notify_data, "Coalesced kind %d parent %#lx " DF_DE, kind, parent,
				DP_DE(name ? name : ""));
		return;
	}

	if (notify_queued >= NOTIFY_QUEUE_MAX) {
		D_MUTEX_UNLOCK(&notify_lock);
		notify_drop(dfuse_info, kind, parent);
		return;
	}

	ne = d_slab_acquire(notify_slab);
	if (ne == NULL) {
		D_MUTEX_UNLOCK(&notify_lock);
		notify_drop(dfuse_info, kind, parent);
		return;
	}

	ne->ne_kind    = kind;
	ne->ne_parent  = parent;
	ne->ne_ino     = ino;
	ne->ne_namelen = namelen;
	if (namelen > 0)
		memcpy(ne->ne_name, name, namelen);
	ne->ne_name[namelen] = '\0';

	d_list_add_tail(&ne->ne_hlist, &notify_coalesce[bucket]);
	d_list_add_tail(&ne->ne_list, &notify_queue);
	notify_queued++;

	if (notify_queued >= NOTIFY_CONGEST_HIGH)
		notify_congest_warn(notify_queued);

	D_MUTEX_UNLOCK(&notify_lock);

	atomic_fetch_add_relaxed(&dfuse_info->di_notify_enqueued, 1);
	DFUSE_TRA_DEBUG(&notify_data, "Enqueued kind %d parent %#lx " DF_DE, kind, parent,
			DP_DE(name ? name : ""));

	sem_post(&notify_sem);
}

void
dfuse_notify_inval_entry(struct dfuse_info *dfuse_info, fuse_ino_t parent, const char *name)
{
	notify_enqueue(dfuse_info, NOTIFY_INVAL_ENTRY, parent, 0, name);
}

void
dfuse_notify_delete(struct dfuse_info *dfuse_info, fuse_ino_t parent, fuse_ino_t ino,
		    const char *name)
{
	notify_enqueue(dfuse_info, NOTIFY_DELETE, parent, ino, name);
}

void
dfuse_notify_inval_inode(struct dfuse_info *dfuse_info, fuse_ino_t ino)
{
	notify_enqueue(dfuse_info, NOTIFY_INVAL_INODE, 0, ino, NULL);
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
	int i;

	DFUSE_TRA_UP(&ival_data, dfuse_info, "invalidator");
	DFUSE_TRA_UP(&notify_data, dfuse_info, "notify");

	D_INIT_LIST_HEAD(&ival_data.time_entry_list);

	notify_data.dn_info = dfuse_info;
	D_INIT_LIST_HEAD(&notify_queue);
	for (i = 0; i < NOTIFY_COALESCE_BUCKETS; i++)
		D_INIT_LIST_HEAD(&notify_coalesce[i]);

	rc = sem_init(&ival_sem, 0, 0);
	if (rc != 0)
		D_GOTO(out, rc = errno);

	rc = sem_init(&notify_sem, 0, 0);
	if (rc != 0) {
		rc = errno;
		goto ival_sem;
	}

	rc = ival_bucket_add(&ival_data.time_entry_list, 0);
	if (rc)
		goto notify_sem;

out:
	return rc;
notify_sem:
	sem_destroy(&notify_sem);
ival_sem:
	sem_destroy(&ival_sem);
	DFUSE_TRA_DOWN(&notify_data);
	DFUSE_TRA_DOWN(&ival_data);
	return rc;
}

/* Register the notify entry slab type.  Split out of ival_thread_start() so tests can prepare
 * the queue for notify_enqueue()/notify_dequeue_locked() without starting the delivery thread.
 */
static int
notify_init(struct dfuse_info *dfuse_info)
{
	struct d_slab_reg notify_slab_reg = {POOL_TYPE_INIT(notify_entry, ne_list)};

	return d_slab_register(&dfuse_info->di_slab, &notify_slab_reg, dfuse_info, &notify_slab);
}

/* Start the threads.  Not called until after fuse is mounted */
int
ival_thread_start(struct dfuse_info *dfuse_info)
{
	int rc;

	ival_data.session = dfuse_info->di_session;

	rc = notify_init(dfuse_info);
	if (rc != -DER_SUCCESS)
		return daos_der2errno(rc);

	rc = pthread_create(&notify_thread, NULL, notify_thread_fn, NULL);
	if (rc != 0)
		return rc;
	pthread_setname_np(notify_thread, "dfuse notify");

	rc = pthread_create(&ival_thread, NULL, ival_thread_fn, NULL);
	if (rc != 0) {
		D_MUTEX_LOCK(&notify_lock);
		notify_stop = true;
		D_MUTEX_UNLOCK(&notify_lock);
		sem_post(&notify_sem);
		pthread_join(notify_thread, NULL);
		notify_thread = 0;
		return rc;
	}
	pthread_setname_np(ival_thread, "dfuse inval");

	return 0;
}

/* Stop threads, remove all inodes from the invalidation queues and teardown all data structures.
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

	/* Latch stop before waking the thread: anything still landing behind an in-flight
	 * request during unmount is dropped rather than queued behind a thread about to exit.
	 */
	D_MUTEX_LOCK(&notify_lock);
	notify_stop = true;
	D_MUTEX_UNLOCK(&notify_lock);
	sem_post(&notify_sem);

	if (notify_thread)
		pthread_join(notify_thread, NULL);
	notify_thread = 0;
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

	sem_destroy(&notify_sem);
	sem_destroy(&ival_sem);
	DFUSE_TRA_DOWN(&notify_data);
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
