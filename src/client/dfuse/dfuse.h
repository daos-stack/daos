/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DFUSE_H__
#define __DFUSE_H__

#include <semaphore.h>

#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <gurt/list.h>
#include <gurt/hash.h>
#include <gurt/atomic.h>
#include <gurt/slab.h>

#include "daos.h"
#include "daos_fs.h"

#include "dfs_internal.h"

#include "dfuse_common.h"

struct dfuse_info {
	struct fuse_session *di_session;
	char                *di_group;
	char                *di_mountpoint;
	uint32_t             di_thread_count;
	uint32_t             di_eq_count;
	bool                 di_threaded;
	bool                 di_foreground;
	bool                 di_caching;
	bool                 di_multi_user;
	bool                 di_wb_cache;

	/* Per process spinlock
	 * This is used to lock readdir against closedir where they share a readdir handle,
	 * so this could be per inode however that's lots of additional memory and the locking
	 * is only needed for minimal list management so isn't locked often or for long.
	 */
	pthread_spinlock_t   di_lock;

	/* RW lock used for force filesystem query ioctl to block for pending forget calls. */
	pthread_rwlock_t     di_forget_lock;

	/** Hash table of open inodes, this matches kernel ref counts */
	struct d_hash_table  dpi_iet;
	/** Hash table of open pools */
	struct d_hash_table  di_pool_table;
	/** Next available inode number */
	ATOMIC uint64_t      di_ino_next;
	bool                 di_shutdown;

	struct d_slab        di_slab;

	/* Array of dfuse_eq */
	struct dfuse_eq     *di_eqt;
	ATOMIC uint64_t      di_eqt_idx;

	ATOMIC uint64_t      di_inode_count;
	ATOMIC uint64_t      di_fh_count;
	ATOMIC uint64_t      di_pool_count;
	ATOMIC uint64_t      di_container_count;
};

/* legacy, allow the old name for easier migration */
#define dfuse_projection_info dfuse_info

struct dfuse_eq {
	struct dfuse_info  *de_handle;

	/* Event queue for async events */
	daos_handle_t       de_eq;
	/* Semaphore to signal event waiting for async thread */
	sem_t               de_sem;

	pthread_t           de_thread;

	struct d_slab_type *de_read_slab;
	struct d_slab_type *de_write_slab;
};

/* Maximum size dfuse expects for read requests, this is not a limit but rather what is expected */
#define DFUSE_MAX_READ (1024 * 1024)

/* Launch fuse, and do not return until complete */
int
dfuse_launch_fuse(struct dfuse_info *dfuse_info, struct fuse_args *args);

struct dfuse_inode_entry;

/** what is returned as the handle for fuse fuse_file_info on create/open/opendir */
struct dfuse_obj_hdl {
	/** pointer to dfs_t */
	dfs_t                    *doh_dfs;
	/** the DFS object handle.  Not created for directories. */
	dfs_obj_t                *doh_obj;
	/** the inode entry for the file */
	struct dfuse_inode_entry *doh_ie;

	/** readdir handle. */
	struct dfuse_readdir_hdl *doh_rd;

	ATOMIC uint32_t           doh_il_calls;

	/** Number of active readdir operations */
	ATOMIC uint32_t           doh_readdir_number;

	ATOMIC uint64_t           doh_write_count;

	/* Next offset we expect from readdir */
	off_t                     doh_rd_offset;

	/* Pointer to the last returned drc entry */
	struct dfuse_readdir_c   *doh_rd_nextc;

	/* Linear read function, if a file is read from start to end then this normally requires
	 * a final read request at the end of the file that returns zero bytes.  Detect this case
	 * and when the final read is detected then just return without a round trip.
	 * Store a flag for this being enabled (starts as true, but many I/O patterns will set it
	 * to false), the expected position of the next read and a boolean for if EOF has been
	 * detected.
	 */
	off_t                     doh_linear_read_pos;
	bool                      doh_linear_read;
	bool                      doh_linear_read_eof;

	/** True if caching is enabled for this file. */
	bool                      doh_caching;

	/* True if the file handle is writeable - used for cache invalidation */
	bool                      doh_writeable;

	/* Track possible kernel cache of readdir on this directory */
	/* Set to true if there is any reason the kernel will not use this directory handle as the
	 * basis for a readdir cache.  Includes if seekdir or rewind are used.
	 */
	bool                      doh_kreaddir_invalid;
	/* Set to true if readdir calls are made on this handle */
	bool                      doh_kreaddir_started;
	/* Set to true if readdir calls reach EOF made on this handle */
	bool                      doh_kreaddir_finished;

	bool                      doh_evict_on_close;
};

/* Readdir support.
 *
 * Readdir is by far the most complicated component of dfuse as a result of the kernel interfaces,
 * the dfs interface and the lack of kernel caching for concurrent operations.
 *
 * The kernel interface to readdir is to make a callback into dfuse to request a number of dentries
 * which are then populated in a buffer which is returned to the kernel, each entry in the buffer
 * contains the name of the entry, the type and some other metadata.  The kernel requests a buffer
 * size and the dfuse can reply with less if it chooses - 0 is taken as end-of-directory.  The
 * length of the filenames affects the number of entries that will fit in the buffer.
 *
 * The dfs interface to reading entries is to call dfs_iterate() which then calls a dfuse callback
 * with the name of each entry, after the iterate completes dfuse then has to perform a lookup to
 * get any metadata for the entry.  DFS takes in a buffer size and max count which can be much
 * larger than the 4k buffer the kernel uses, for this dfuse will fetch up to 1024 entries at a
 *  time for larger directories.
 *
 * The kernel uses "auto readdir plus" to switch between two types of readdir, the plus calls
 * return full stat information for each file including size (which is expensive to read) and
 * it takes a reference for each entry returned so requires a hash table reference for each entry.
 * The non-plus call just takes the name and mode for each entry so can do a lighter weight
 * dfs lookup and does not need to do any per dentry hash table operations.
 * For any directory the first call will be a plus type, subsequent entries will depend on if the
 * application is doing stat calls on the dentries, "/bin/ls -l" will result in readdir plus being
 * used throughout, "/bin/ls" will result in only the first call being plus.
 *
 * In all cases the kernel holds an inode lock on readdir however this does not extend to closedir
 * so list management is needed to protect shared readddir handles and the readdir handle pointer
 * in the inode against concurrent readdir and closedir calls for different directory handles on the
 * same inode.
 *
 * DFuse has inode handles (struct dfuse_inode_entry), open directory handles
 * (struct dfuse_obj_hdl) as well as readdir handles (struct dfuse_readdir_hdl) which may be shared
 * across directory handles and may be linked from the inode handle.
 *
 * Readdir operations primarily use readdir handles however these can be shared across directory
 * handles so some data is kept in the directory handle.  The iterator and cache are both in
 * the readdir handle but the expected offset and location in the cache are in the directory handle.
 *
 * On the first readdir call (not opendir) for a inode then a readdir handle is created to be used
 * by the directory handle and potentially shared with future directory handles so the inode handle
 * will keep a pointer to the readdir handle.  Subsequent per-directory first readdir calls will
 * choose whether to share the readdir handle or create their own - there is only one shared readdir
 * handle per inode at any one time but there may be many non-shared ones which map to a specific
 * directory handle.  Any directory handle where a seekdir is detected (the offset from one readdir
 * call does not match the next_offset) from the previous call will switch to using a private
 * readdir handle if it's not already.  In this way shared readdir handles never seek.
 *
 * Entries that have been read by dfs_iterate but not passed to the kernel or put in the cache
 * are kept in the drh_dre entries in the readdir handle, as calls progress through the directory
 * then these are processed and added to the reply buffer and put into the cache.  When out of
 * entries in the array a new dfs_iterate call is made to repopulate the array.
 *
 * The cache is kept as a list in dfh_cache_list on the readdir handle which is a standard d_list_t
 * however the directory handle also save a pointer to the appropriate entry for that caller.  When
 * the front of the list is reached then new entries are consumed from the dre entry array and
 * moved to the cache list.
 *
 * To handle cases where readdir handles are shared cache entries may or may not have a rlink
 * pointer for the inode handle for that entry, for the plus case this is needed and a reference
 * is taken each time the entry is used, for the non-plus this isn't needed so a cheaper
 * dfs_lookup() call is made, the rlink pointer will be null and only the mode entries in the
 * stat entry will be valid.
 *
 * The kernel will also cache readdir entries, dfuse will track when this is populated (using
 * heuristics rather than positive confirmation) and will use cache settings and timeouts to tell
 * the kernel to either use or populate the cache.
 *
 **/

/* Readdir entry as saved by the iterator.  These are forward-looking from the current position */
struct dfuse_readdir_entry {
	/* Name of this directory entry */
	char  dre_name[NAME_MAX + 1];

	/* Offset of this directory entry */
	off_t dre_offset;

	/* Offset of the next directory entry  A value of DFUSE_READDIR_EOD means end of directory.
	 * This could in theory be a boolean.
	 */
	off_t dre_next_offset;
};

/* Readdir entry as saved by the cache.  These are backwards looking from the current position
 * and will be used by other open handles on the same inode doing subsequent readdir calls.
 */
struct dfuse_readdir_c {
	d_list_t    drc_list;
	struct stat drc_stbuf;
	d_list_t   *drc_rlink;
	off_t       drc_offset;
	off_t       drc_next_offset;
	char        drc_name[NAME_MAX + 1];
};

/* Maximum number of dentries to read at one time. */
#define READDIR_MAX_COUNT 1024

/* Readdir handle.  Pointed to by any open directory handle after the first readdir call */
struct dfuse_readdir_hdl {
	/** an anchor to track listing in readdir */
	daos_anchor_t              drh_anchor;

	/** Array of entries returned by dfs but not reported to kernel */
	struct dfuse_readdir_entry drh_dre[READDIR_MAX_COUNT];
	/** Current index into doh_dre array */
	uint32_t                   drh_dre_index;
	/** Last index containing valid data */
	uint32_t                   drh_dre_last_index;
	/** Next value from anchor */
	uint32_t                   drh_anchor_index;

	/** List of directory entries read so far, list of dfuse_readdir_c */
	d_list_t                   drh_cache_list;

	/* Count of how many directory handles are using this handle */
	ATOMIC uint32_t            drh_ref;

	/* Set to true if this handle is caching and potentially shared.  Immutable. */
	bool                       drh_caching;

	/* Starts at true and set to false if a directory is modified when open.  Prevents new
	 * readers from sharing the handle
	 */
	bool                       drh_valid;
};

/* Drop a readdir handle from a open directory handle.
 *
 * For non-caching handles this means free it however in the case of caching it will drop
 * a reference only.
 */
void
dfuse_dre_drop(struct dfuse_projection_info *fs_handle, struct dfuse_obj_hdl *oh);

/*
 * Set required initial state in dfuse_obj_hdl.
 */
void
dfuse_open_handle_init(struct dfuse_info *dfuse_info, struct dfuse_obj_hdl *oh,
		       struct dfuse_inode_entry *ie);

struct dfuse_inode_ops {
	void (*create)(fuse_req_t req, struct dfuse_inode_entry *parent,
		       const char *name, mode_t mode,
		       struct fuse_file_info *fi);
	void (*getattr)(fuse_req_t req, struct dfuse_inode_entry *inode);
	void (*setattr)(fuse_req_t req, struct dfuse_inode_entry *inode,
			struct stat *attr, int to_set);
	void (*lookup)(fuse_req_t req, struct dfuse_inode_entry *parent,
		       const char *name);
	void (*mknod)(fuse_req_t req, struct dfuse_inode_entry *parent,
		      const char *name, mode_t mode);
	void (*opendir)(fuse_req_t req, struct dfuse_inode_entry *inode,
			struct fuse_file_info *fi);
	void (*releasedir)(fuse_req_t req, struct dfuse_inode_entry *inode,
			   struct fuse_file_info *fi);
	void (*rename)(fuse_req_t req, struct dfuse_inode_entry *parent_inode,
		       const char *name,
		       struct dfuse_inode_entry *newparent_inode,
		       const char *newname, unsigned int flags);
	void (*symlink)(fuse_req_t req, const char *link,
			struct dfuse_inode_entry *parent, const char *name);
	void (*unlink)(fuse_req_t req, struct dfuse_inode_entry *parent,
		       const char *name);
	void (*setxattr)(fuse_req_t req, struct dfuse_inode_entry *inode,
			 const char *name, const char *value, size_t size,
			 int flags);
	void (*getxattr)(fuse_req_t req, struct dfuse_inode_entry *inode,
			 const char *name, size_t size);
	void (*listxattr)(fuse_req_t req, struct dfuse_inode_entry *inode,
			  size_t size);
	void (*removexattr)(fuse_req_t req, struct dfuse_inode_entry *inode,
			    const char *name);
	void (*statfs)(fuse_req_t req, struct dfuse_inode_entry *inode);
};

struct dfuse_event {
	fuse_req_t                    de_req; /**< The fuse request handle */
	daos_event_t                  de_ev;
	size_t                        de_len; /**< The size returned by daos */
	d_iov_t                       de_iov;
	d_sg_list_t                   de_sgl;
	d_list_t                      de_list;
	struct dfuse_eq              *de_eqt;
	struct dfuse_obj_hdl         *de_oh;
	off_t                         de_req_position; /**< The file position requested by fuse */
	size_t                        de_req_len;
	void (*de_complete_cb)(struct dfuse_event *ev);
};

extern struct dfuse_inode_ops dfuse_dfs_ops;
extern struct dfuse_inode_ops dfuse_cont_ops;
extern struct dfuse_inode_ops dfuse_pool_ops;

/** Pool information
 *
 * This represents a pool that DFUSE is accessing.  All pools contain
 * a hash table of open containers.
 *
 * uuid may be NULL for root inode where there is no pool.
 *
 */
struct dfuse_pool {
	/** UUID of the pool */
	uuid_t              dfp_pool;
	/** Pool handle */
	daos_handle_t       dfp_poh;
	/** Hash table entry in dpi_pool_table */
	d_list_t            dfp_entry;
	/** Hash table reference count */
	ATOMIC uint32_t     dfp_ref;

	/** Hash table of open containers in pool */
	struct d_hash_table dfp_cont_table;
};

/** Container information
 *
 * This represents a container that DFUSE is accessing.  All containers
 * will have a valid dfs_handle.
 *
 * Note this struct used to be dfuse_dfs, hence the dfs_prefix for it's
 * members.
 *
 * uuid may be NULL for pool inodes.
 */
struct dfuse_cont {
	/** Fuse handlers to use for this container */
	struct dfuse_inode_ops	*dfs_ops;

	/** Pointer to parent pool, where a reference is held */
	struct dfuse_pool	*dfs_dfp;

	/** dfs mount handle */
	dfs_t			*dfs_ns;

	/** UUID of the container */
	uuid_t			dfs_cont;

	/** Container handle */
	daos_handle_t		dfs_coh;

	/** Hash table entry entry in dfp_cont_table */
	d_list_t		dfs_entry;
	/** Hash table reference count */
	ATOMIC uint32_t          dfs_ref;

	/** Inode number of the root of this container */
	ino_t			dfs_ino;

	/** Caching information */
	double			dfc_attr_timeout;
	double			dfc_dentry_timeout;
	double			dfc_dentry_dir_timeout;
	double			dfc_ndentry_timeout;
	double			dfc_data_timeout;
	bool			dfc_direct_io_disable;
};

void
dfuse_set_default_cont_cache_values(struct dfuse_cont *dfc);

int
dfuse_cont_open_by_label(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp, const char *label,
			 struct dfuse_cont **_dfs);

int
dfuse_cont_open(struct dfuse_info *dfuse_info, struct dfuse_pool *dfp, uuid_t *cont,
		struct dfuse_cont **_dfs);

/* Connect to a pool via either a label or uuid.
 *
 * Create a datastructure and connect to a pool by label or uuid or neither if required.  After
 * making the connection then add it to the hash table and disconnect if an existing connection
 * is identified.
 *
 * Returns a system error code.
 */
int
dfuse_pool_connect(struct dfuse_info *dfuse_info, const char *label, struct dfuse_pool **_dfp);

/* Return a connection for a pool uuid.
 *
 * Queries the hash table for an existing connection and makes a call to dfuse_pool_connect() as
 * necessary.  This function is fast for cases where existing connections exist but only accepts
 * uuids not labels.
 *
 * Returns a system error code.
 */
int
dfuse_pool_get_handle(struct dfuse_info *dfuse_info, uuid_t pool, struct dfuse_pool **_dfp);

/* Xattr namespace used by dfuse.
 *
 * Extended attributes with this prefix can only be set by dfuse itself
 * or directly though dfs/daos but not through dfuse.
 */
#define DFUSE_XATTR_PREFIX "user.dfuse"

/* dfuse_core.c */

/* Setup internal structures */
int
dfuse_fs_init(struct dfuse_info *dfuse_info);

/* Start a dfuse projection */
int
dfuse_fs_start(struct dfuse_info *dfuse_info, struct dfuse_cont *dfs);

int
dfuse_fs_stop(struct dfuse_info *dfuse_info);

/* Drain and free resources used by a projection */
int
dfuse_fs_fini(struct dfuse_info *dfuse_info);

/* dfuse_thread.c */

extern int
dfuse_loop(struct dfuse_info *dfuse_info);

extern
struct fuse_lowlevel_ops dfuse_ops;

/* Helper macros for open() and creat() to log file access modes */
#define LOG_MODE(HANDLE, FLAGS, MODE) do {			\
		if ((FLAGS) & (MODE))				\
			DFUSE_TRA_DEBUG(HANDLE, #MODE);		\
		FLAGS &= ~MODE;					\
	} while (0)

/**
 * Dump the file open mode to the logfile.
 *
 * On a 64 bit system O_LARGEFILE is assumed so always set but defined to zero
 * so set LARGEFILE here for debugging
 */
#define LARGEFILE 0100000
#define FMODE_EXEC 0x20
#define LOG_FLAGS(HANDLE, INPUT) do {					\
		int _flag = (INPUT);					\
		LOG_MODE((HANDLE), _flag, O_APPEND);			\
		LOG_MODE((HANDLE), _flag, O_RDONLY);			\
		LOG_MODE((HANDLE), _flag, O_WRONLY);			\
		LOG_MODE((HANDLE), _flag, O_RDWR);			\
		LOG_MODE((HANDLE), _flag, O_ASYNC);			\
		LOG_MODE((HANDLE), _flag, O_CLOEXEC);			\
		LOG_MODE((HANDLE), _flag, O_CREAT);			\
		LOG_MODE((HANDLE), _flag, O_DIRECT);			\
		LOG_MODE((HANDLE), _flag, O_DIRECTORY);			\
		LOG_MODE((HANDLE), _flag, O_DSYNC);			\
		LOG_MODE((HANDLE), _flag, O_EXCL);			\
		LOG_MODE((HANDLE), _flag, O_LARGEFILE);			\
		LOG_MODE((HANDLE), _flag, LARGEFILE);			\
		LOG_MODE((HANDLE), _flag, O_NOATIME);			\
		LOG_MODE((HANDLE), _flag, O_NOCTTY);			\
		LOG_MODE((HANDLE), _flag, O_NONBLOCK);			\
		LOG_MODE((HANDLE), _flag, O_PATH);			\
		LOG_MODE((HANDLE), _flag, O_SYNC);			\
		LOG_MODE((HANDLE), _flag, O_TRUNC);			\
		LOG_MODE((HANDLE), _flag, O_NOFOLLOW);			\
		LOG_MODE((HANDLE), _flag, FMODE_EXEC);			\
		if (_flag)						\
			DFUSE_TRA_ERROR(HANDLE, "Flags %#o", _flag);	\
	} while (0)

/** Dump the file mode to the logfile. */
#define LOG_MODES(HANDLE, INPUT) do {					\
		int _flag = (INPUT) & S_IFMT;				\
		LOG_MODE((HANDLE), _flag, S_IFREG);			\
		LOG_MODE((HANDLE), _flag, S_IFDIR);			\
		LOG_MODE((HANDLE), _flag, S_IFIFO);			\
		LOG_MODE((HANDLE), _flag, S_ISUID);			\
		LOG_MODE((HANDLE), _flag, S_ISGID);			\
		LOG_MODE((HANDLE), _flag, S_ISVTX);			\
		if (_flag)						\
			DFUSE_TRA_ERROR(HANDLE, "Mode 0%o", _flag);	\
	} while (0)

#define DFUSE_UNSUPPORTED_CREATE_FLAGS (O_ASYNC | O_CLOEXEC | O_DIRECTORY | \
					O_NOCTTY | O_PATH)

#define DFUSE_UNSUPPORTED_OPEN_FLAGS (DFUSE_UNSUPPORTED_CREATE_FLAGS | \
					O_CREAT | O_EXCL)

#define DFUSE_REPLY_ERR_RAW(desc, req, status)                                                     \
	do {                                                                                       \
		int __err = status;                                                                \
		int __rc;                                                                          \
		if (__err == 0) {                                                                  \
			DFUSE_TRA_ERROR(desc, "Invalid call to fuse_reply_err: 0");                \
			__err = EIO;                                                               \
		}                                                                                  \
		if (__err == EIO || __err == EINVAL)                                               \
			DFUSE_TRA_WARNING(desc, "Returning: %d (%s)", __err, strerror(__err));     \
		else                                                                               \
			DFUSE_TRA_DEBUG(desc, "Returning: %d (%s)", __err, strerror(__err));       \
		__rc = fuse_reply_err(req, __err);                                                 \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_err() returned: %d (%s)", __rc,          \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_ZERO(desc, req)                                                                \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(desc, "Returning 0");                                              \
		__rc = fuse_reply_err(req, 0);                                                     \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_err() returned: %d: (%s)", __rc,         \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_ATTR(ie, req, attr)                                                            \
	do {                                                                                       \
		int    __rc;                                                                       \
		double timeout = 0;                                                                \
		if (atomic_load_relaxed(&(ie)->ie_open_count) == 0) {                              \
			timeout = (ie)->ie_dfs->dfc_attr_timeout;                                  \
			dfuse_mcache_set_time(ie);                                                 \
		}                                                                                  \
		DFUSE_TRA_DEBUG(ie, "Returning attr inode %#lx mode %#o size %zi timeout %lf",     \
				(attr)->st_ino, (attr)->st_mode, (attr)->st_size, timeout);        \
		__rc = fuse_reply_attr(req, attr, timeout);                                        \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(ie, "fuse_reply_attr() returned: %d (%s)", __rc,           \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_ATTR_FORCE(ie, req, timeout)                                                   \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(ie, "Returning attr inode %#lx mode %#o size %zi timeout %lf",     \
				(ie)->ie_stat.st_ino, (ie)->ie_stat.st_mode,                       \
				(ie)->ie_stat.st_size, timeout);                                   \
		__rc = fuse_reply_attr(req, &ie->ie_stat, timeout);                                \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(ie, "fuse_reply_attr() returned: %d (%s)", __rc,           \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_READLINK(ie, req, path)                                                        \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(ie, "Returning target '%s'", path);                                \
		__rc = fuse_reply_readlink(req, path);                                             \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(ie, "fuse_reply_readlink() returned: %d (%s)", __rc,       \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_BUF(desc, req, buf, size)                                                      \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(desc, "Returning buffer(%p %#zx)", buf, size);                     \
		__rc = fuse_reply_buf(req, buf, size);                                             \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_buf() returned: %d (%s)", __rc,          \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_BUFQ(desc, req, buf, size)                                                     \
	do {                                                                                       \
		int __rc;                                                                          \
		__rc = fuse_reply_buf(req, buf, size);                                             \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_buf() returned: %d (%s)", __rc,          \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_WRITE(desc, req, bytes)                                                        \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(desc, "Returning write(%#zx)", bytes);                             \
		__rc = fuse_reply_write(req, bytes);                                               \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_write() returned: %d (%s)", __rc,        \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_OPEN(oh, req, _fi)                                                             \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(oh, "Returning open, keep_cache %d", (_fi)->keep_cache);           \
		__rc = fuse_reply_open(req, _fi);                                                  \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(oh, "fuse_reply_open() returned: %d (%s)", __rc,           \
					strerror(-__rc));                                          \
	} while (0)

#if HAVE_CACHE_READDIR

#define DFUSE_REPLY_OPEN_DIR(oh, req, _fi)                                                         \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(oh, "Returning open directory, use_cache %d keep_cache %d",        \
				(_fi)->cache_readdir, (_fi)->keep_cache);                          \
		__rc = fuse_reply_open(req, _fi);                                                  \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(oh, "fuse_reply_open() returned: %d (%s)", __rc,           \
					strerror(-__rc));                                          \
	} while (0)

#else

#define DFUSE_REPLY_OPEN_DIR(oh, req, _fi)                                                         \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(oh, "Returning open directory");                                   \
		__rc = fuse_reply_open(req, _fi);                                                  \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(oh, "fuse_reply_open returned: %d (%s)", __rc,             \
					strerror(-__rc));                                          \
	} while (0)

#endif

#define DFUSE_REPLY_CREATE(desc, req, entry, fi)                                                   \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(desc, "Returning create");                                         \
		__rc = fuse_reply_create(req, &entry, fi);                                         \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_create() returned: %d (%s)", __rc,       \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_ENTRY(inode, req, entry)                                                       \
	do {                                                                                       \
		int __rc;                                                                          \
		if ((entry).attr_timeout > 0) {                                                    \
			(inode)->ie_stat = (entry).attr;                                           \
			dfuse_mcache_set_time(inode);                                              \
		}                                                                                  \
		DFUSE_TRA_DEBUG(inode, "Returning entry inode %#lx mode %#o size %zi timeout %lf", \
				(entry).attr.st_ino, (entry).attr.st_mode, (entry).attr.st_size,   \
				(entry).attr_timeout);                                             \
		__rc = fuse_reply_entry(req, &entry);                                              \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(inode, "fuse_reply_entry() returned: %d (%s)", __rc,       \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_STATFS(desc, req, stat)                                                        \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(desc, "Returning statfs");                                         \
		__rc = fuse_reply_statfs(req, stat);                                               \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_statfs() returned: %d (%s)", __rc,       \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_IOCTL_SIZE(desc, req, arg, size)                                               \
	do {                                                                                       \
		int __rc;                                                                          \
		DFUSE_TRA_DEBUG(desc, "Returning ioctl");                                          \
		__rc = fuse_reply_ioctl(req, 0, arg, size);                                        \
		if (__rc != 0)                                                                     \
			DFUSE_TRA_ERROR(desc, "fuse_reply_ioctl() returned: %d (%s)", __rc,        \
					strerror(-__rc));                                          \
	} while (0)

#define DFUSE_REPLY_IOCTL(desc, req, arg) DFUSE_REPLY_IOCTL_SIZE(desc, req, &(arg), sizeof(arg))

/**
 * Inode handle.
 *
 * Describes any entry in the projection that the kernel knows about, may
 * be a directory, file, symbolic link or anything else.
 */

struct dfuse_inode_entry {
	/** stat structure for this inode.
	 * This will be valid, but out-of-date at any given moment in time,
	 * mainly used for the inode number and type.
	 */
	struct stat               ie_stat;

	dfs_obj_t                *ie_obj;

	/** DAOS object ID of the dfs object.  Used for uniquely identifying files */

	daos_obj_id_t             ie_oid;

	/** The name of the entry, relative to the parent.
	 * This would have been valid when the inode was first observed
	 * however may be incorrect at any point after that.  It may not
	 * even match the local kernels view of the projection as it is
	 * not updated on local rename requests.
	 */
	char                      ie_name[NAME_MAX + 1];

	/** The parent inode of this entry.
	 *
	 * As with name this will be correct when created however may
	 * be incorrect at any point after that.  The inode does not hold
	 * a reference on the parent so the inode may not be valid.
	 */
	fuse_ino_t                ie_parent;

	struct dfuse_cont        *ie_dfs;

	/** Hash table of inodes
	 * All valid inodes are kept in a hash table, using the hash table locking.
	 */
	d_list_t                  ie_htl;

	/* Time of last kernel cache metadata update */
	struct timespec           ie_mcache_last_update;

	/* Time of last kernel cache data update, also used for kernel readdir caching. */
	struct timespec           ie_dcache_last_update;

	/** written region for truncated files (i.e. ie_truncated set) */
	size_t                    ie_start_off;
	size_t                    ie_end_off;

	/** Reference counting for the inode Used by the hash table callbacks */
	ATOMIC uint32_t           ie_ref;

	/* Number of open file descriptors for this inode */
	ATOMIC uint32_t           ie_open_count;

	ATOMIC uint32_t           ie_open_write_count;

	/* Number of file open file descriptors using IL */
	ATOMIC uint32_t           ie_il_count;

	/* Readdir handle, if present.  May be shared */
	struct dfuse_readdir_hdl *ie_rd_hdl;

	/** Number of active readdir operations */
	ATOMIC uint32_t           ie_readdir_number;

	/** file was truncated from 0 to a certain size */
	bool                      ie_truncated;

	/** file is the root of a container */
	bool                      ie_root;

	/** File has been unlinked from daos */
	bool                      ie_unlinked;
};

static inline struct dfuse_inode_entry *
dfuse_inode_lookup(struct dfuse_info *dfuse_info, fuse_ino_t ino)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(&dfuse_info->dpi_iet, &ino, sizeof(ino));
	if (!rlink)
		return NULL;

	return container_of(rlink, struct dfuse_inode_entry, ie_htl);
}

static inline void
dfuse_inode_decref(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie)
{
	d_hash_rec_decref(&dfuse_info->dpi_iet, &ie->ie_htl);
}

extern char *duns_xattr_name;

/* Generate the inode to use for this dfs object.  This is generating a single
 * 64 bit number from three 64 bit numbers so will not be perfect but does
 * avoid most conflicts.
 *
 * Take the sequence parts of both the hi and lo object id and put them in
 * different parts of the inode, then or in the inode number of the root
 * of this dfs object, to avoid conflicts across containers.
 */
static inline void
dfuse_compute_inode(struct dfuse_cont *dfs,
		    daos_obj_id_t *oid,
		    ino_t *_ino)
{
	uint64_t hi;

	hi = (oid->hi & (-1ULL >> 32)) | (dfs->dfs_ino << 48);

	*_ino = hi ^ (oid->lo << 32);
};

/* Mark the cache for a directory invalid.  Called when directory contents change on create,
 * unlink or rename
 */
void
dfuse_cache_evict_dir(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie);

/* Metadata caching functions. */

/* Mark the cache as up-to-date from now */
void
dfuse_mcache_set_time(struct dfuse_inode_entry *ie);

/* Set the cache as invalid */
void
dfuse_mcache_evict(struct dfuse_inode_entry *ie);

/* Check the cache setting against a given timeout, and return time left */
bool
dfuse_mcache_get_valid(struct dfuse_inode_entry *ie, double max_age, double *timeout);

/* Data caching functions */

/* Mark the cache as up-to-date from now */
void
dfuse_dcache_set_time(struct dfuse_inode_entry *ie);

/* Set the cache as invalid */
void
dfuse_dcache_evict(struct dfuse_inode_entry *ie);

/* Set both caches invalid */
void
dfuse_cache_evict(struct dfuse_inode_entry *ie);

/* Check the cache setting against a given timeout */
bool
dfuse_dcache_get_valid(struct dfuse_inode_entry *ie, double max_age);

int
check_for_uns_ep(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie, char *attr,
		 daos_size_t len);

void
dfuse_ie_init(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie);

#define dfuse_ie_free(_di, _ie)                                                                    \
	do {                                                                                       \
		atomic_fetch_sub_relaxed(&(_di)->di_inode_count, 1);                               \
		D_FREE(_ie);                                                                       \
	} while (0)

#define dfuse_oh_free(_di, _oh)                                                                    \
	do {                                                                                       \
		atomic_fetch_sub_relaxed(&(_di)->di_fh_count, 1);                                  \
		D_FREE(_oh);                                                                       \
	} while (0)

void
dfuse_ie_close(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie);

/* ops/...c */

void
dfuse_cb_lookup(fuse_req_t, struct dfuse_inode_entry *, const char *);

void
dfuse_cb_forget(fuse_req_t, fuse_ino_t, uint64_t);

void
dfuse_cb_forget_multi(fuse_req_t, size_t, struct fuse_forget_data *);

void
dfuse_cb_getattr(fuse_req_t, struct dfuse_inode_entry *);

void
dfuse_cb_readlink(fuse_req_t, fuse_ino_t);

void
dfuse_cb_mknod(fuse_req_t, struct dfuse_inode_entry *,
	       const char *, mode_t);

void
dfuse_cb_opendir(fuse_req_t, struct dfuse_inode_entry *,
		 struct fuse_file_info *fi);

void
dfuse_cb_releasedir(fuse_req_t, struct dfuse_inode_entry *,
		    struct fuse_file_info *fi);

void
dfuse_cb_create(fuse_req_t, struct dfuse_inode_entry *,
		const char *, mode_t, struct fuse_file_info *);

void
dfuse_cb_open(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void
dfuse_cb_release(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void
dfuse_cb_read(fuse_req_t, fuse_ino_t, size_t, off_t,
	      struct fuse_file_info *);

void
dfuse_cb_unlink(fuse_req_t, struct dfuse_inode_entry *,
		const char *);

void
dfuse_cb_readdir(fuse_req_t, struct dfuse_obj_hdl *, size_t, off_t, bool);

void
dfuse_cb_rename(fuse_req_t, struct dfuse_inode_entry *, const char *,
		struct dfuse_inode_entry *, const char *, unsigned int);

void
dfuse_cb_write(fuse_req_t, fuse_ino_t, struct fuse_bufvec *, off_t,
	       struct fuse_file_info *);

void
dfuse_cb_symlink(fuse_req_t, const char *, struct dfuse_inode_entry *,
		 const char *);

void
dfuse_cb_setxattr(fuse_req_t, struct dfuse_inode_entry *, const char *,
		  const char *, size_t, int);

void
dfuse_cb_getxattr(fuse_req_t, struct dfuse_inode_entry *,
		  const char *, size_t);

void
dfuse_cb_listxattr(fuse_req_t, struct dfuse_inode_entry *, size_t);

void
dfuse_cb_removexattr(fuse_req_t, struct dfuse_inode_entry *, const char *);

void
dfuse_cb_setattr(fuse_req_t, struct dfuse_inode_entry *, struct stat *, int);

void
dfuse_cb_statfs(fuse_req_t, struct dfuse_inode_entry *);

#ifdef FUSE_IOCTL_USE_INT
void dfuse_cb_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void *arg,
		    struct fuse_file_info *fi, unsigned int flags,
		    const void *in_buf, size_t in_bufsz, size_t out_bufsz);
#else
void dfuse_cb_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void *arg,
		    struct fuse_file_info *fi, unsigned int flags,
		    const void *in_buf, size_t in_bufsz, size_t out_bufsz);
#endif

/* Return inode information to fuse
 *
 * Adds inode to the hash table and calls fuse_reply_entry()
 */
void
dfuse_reply_entry(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *inode,
		  struct fuse_file_info *fi_out, bool is_new, fuse_req_t req);

int
_dfuse_mode_update(fuse_req_t req, struct dfuse_inode_entry *parent, mode_t *_mode);

/* Mark object as removed and invalidate any kernel data for it */
void
dfuse_oid_unlinked(struct dfuse_info *dfuse_info, fuse_req_t req, daos_obj_id_t *oid,
		   struct dfuse_inode_entry *parent, const char *name);

/* dfuse_cont.c */
void
dfuse_cont_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name);

void
dfuse_cont_mknod(fuse_req_t req, struct dfuse_inode_entry *parent,
		 const char *name, mode_t mode);

/* dfuse_pool.c */
void
dfuse_pool_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name);

#endif /* __DFUSE_H__ */
