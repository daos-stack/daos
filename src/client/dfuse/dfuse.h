/**
 * (C) Copyright 2016-2021 Intel Corporation.
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

#include "daos.h"
#include "daos_fs.h"

#include "dfs_internal.h"

#include "dfuse_common.h"

struct dfuse_info {
	struct fuse_session		*di_session;
	char				*di_group;
	char				*di_mountpoint;
	uint32_t			di_thread_count;
	bool				di_threaded;
	bool				di_foreground;
	bool				di_caching;
	bool				di_wb_cache;
};

struct dfuse_projection_info {
	struct dfuse_info		*dpi_info;
	/** Hash table of open inodes, this matches kernel ref counts */
	struct d_hash_table		dpi_iet;
	/** Hash table of open pools */
	struct d_hash_table		dpi_pool_table;
	/** Next available inode number */
	ATOMIC uint64_t			dpi_ino_next;
	/* Event queue for async events */
	daos_handle_t			dpi_eq;
	/** Semaphore to signal event waiting for async thread */
	sem_t				dpi_sem;
	pthread_t			dpi_thread;
	bool				dpi_shutdown;
};

/* Launch fuse, and do not return until complete */
bool
dfuse_launch_fuse(struct dfuse_projection_info *fs_handle,
		  struct fuse_lowlevel_ops *flo,
		  struct fuse_args *args);

struct dfuse_inode_entry;

struct dfuse_readdir_entry {
	/* Name of this directory entry */
	char	dre_name[NAME_MAX + 1];

	/* Offset of this directory entry */
	off_t	dre_offset;

	/* Offset of the next directory entry
	 * A value of DFUSE_READDIR_EOD means end
	 * of directory.
	 */
	off_t	dre_next_offset;
};

/** what is returned as the handle for fuse fuse_file_info on
 * create/open/opendir
 */
struct dfuse_obj_hdl {
	/** pointer to dfs_t */
	dfs_t				*doh_dfs;
	/** the DFS object handle */
	dfs_obj_t			*doh_obj;
	/** the inode entry for the file */
	struct dfuse_inode_entry	*doh_ie;

	/** True if caching is enabled for this file. */
	bool				doh_caching;

	/* Below here is only used for directories */
	/** an anchor to track listing in readdir */
	daos_anchor_t			doh_anchor;

	/** Array of entries returned by dfs but not reported to kernel */
	struct dfuse_readdir_entry	*doh_dre;
	/** Current index into doh_dre array */
	uint32_t			doh_dre_index;
	/** Last index containing valid data */
	uint32_t			doh_dre_last_index;
	/** Next value from anchor */
	uint32_t			doh_anchor_index;
};

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
	fuse_req_t	de_req;
	daos_event_t	de_ev;
	void		(*de_complete_cb)(struct dfuse_event *ev);
	size_t		de_len;
	d_iov_t		de_iov;
	d_sg_list_t	de_sgl;
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
	uuid_t			dfp_pool;
	/** Pool handle */
	daos_handle_t		dfp_poh;
	/** Hash table entry in dpi_pool_table */
	d_list_t		dfp_entry;
	/** Hash table reference count */
	ATOMIC uint		dfp_ref;

	/** Hash table of open containers in pool */
	struct d_hash_table	dfp_cont_table;
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
	ATOMIC uint		dfs_ref;

	/** Inode number of the root of this container */
	ino_t			dfs_ino;

	/** Caching information */
	double			dfc_attr_timeout;
	double			dfc_dentry_timeout;
	double			dfc_dentry_dir_timeout;
	double			dfc_ndentry_timeout;
	bool			dfc_data_caching;
	bool			dfc_direct_io_disable;
	pthread_mutex_t		dfs_read_mutex;
};

void
dfuse_set_default_cont_cache_values(struct dfuse_cont *dfc);

int
dfuse_cont_open_by_label(struct dfuse_projection_info *fs_handle,
			 struct dfuse_pool *dfp,
			 const char *label,
			 struct dfuse_cont **_dfs);

int
dfuse_cont_open(struct dfuse_projection_info *fs_handle,
		struct dfuse_pool *dfp, uuid_t *cont,
		struct dfuse_cont **_dfs);

int
dfuse_pool_connect_by_label(struct dfuse_projection_info *fs_handle,
			const char *label,
			struct dfuse_pool **_dfp);

int
dfuse_pool_connect(struct dfuse_projection_info *fs_handle, uuid_t *pool,
		struct dfuse_pool **_dfp);

/* Xattr namespace used by dfuse.
 *
 * Extended attributes with this prefix can only be set by dfuse itself
 * or directly though dfs/daos but not through dfuse.
 */
#define DFUSE_XATTR_PREFIX "user.dfuse"

/* dfuse_core.c */

/* Setup internal structures */
int
dfuse_fs_init(struct dfuse_info *dfuse_info,
	      struct dfuse_projection_info **fsh);

/* Start a dfuse projection */
int
dfuse_start(struct dfuse_projection_info *fs_handle, struct dfuse_cont *dfs);

/* Drain and free resources used by a projection */
int
dfuse_fs_fini(struct dfuse_projection_info *fs_handle);

/* dfuse_thread.c */

extern int
dfuse_loop(struct dfuse_info *dfuse_info);

struct fuse_lowlevel_ops *dfuse_get_fuse_ops();

/* Helper macros for open() and creat() to log file access modes */
#define LOG_MODE(HANDLE, FLAGS, MODE) do {			\
		if ((FLAGS) & (MODE))				\
			DFUSE_TRA_DEBUG(HANDLE, #MODE);	\
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

#define DFUSE_REPLY_ERR_RAW(desc, req, status)				\
	do {								\
		int __err = status;					\
		int __rc;						\
		if (__err == 0) {					\
			DFUSE_TRA_ERROR(desc,				\
					"Invalid call to fuse_reply_err: 0"); \
			__err = EIO;					\
		}							\
		if (__err == EIO || __err == EINVAL) \
			DFUSE_TRA_WARNING(desc, "Returning %d '%s'",	\
					  __err, strerror(__err));	\
		else							\
			DFUSE_TRA_DEBUG(desc, "Returning %d '%s'",	\
					__err, strerror(__err));	\
		__rc = fuse_reply_err(req, __err);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_ZERO(desc, req)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(desc, "Returning 0");			\
		__rc = fuse_reply_err(req, 0);				\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_ATTR(ie, req, attr)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(ie,					\
				"Returning attr inode %#lx mode %#o",	\
				(attr)->st_ino,				\
				(attr)->st_mode);			\
		__rc = fuse_reply_attr(req, attr,			\
				(ie)->ie_dfs->dfc_attr_timeout);	\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(ie,				\
					"fuse_reply_attr returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_READLINK(ie, req, path)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(ie, "Returning target '%s'", path);	\
		__rc = fuse_reply_readlink(req, path);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(ie,				\
					"fuse_reply_readlink returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_BUF(desc, req, buf, size)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(desc, "Returning buffer(%p %#zx)",	\
				buf, size);				\
		__rc = fuse_reply_buf(req, buf, size);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_buf returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_WRITE(desc, req, bytes)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(desc, "Returning write(%#zx)", bytes); \
		__rc = fuse_reply_write(req, bytes);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_write returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_OPEN(oh, req, _fi)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(oh, "Returning open");			\
		if ((oh)->doh_ie->ie_dfs->dfc_data_caching) {		\
			(_fi)->keep_cache = 1;				\
		}							\
		__rc = fuse_reply_open(req, _fi);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(oh,				\
					"fuse_reply_open returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_CREATE(desc, req, entry, fi)			\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(desc, "Returning create");		\
		__rc = fuse_reply_create(req, &entry, fi);		\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_create returned %d:%s",\
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_ENTRY(desc, req, entry)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(desc,					\
				"Returning entry inode %#lx mode %#o",	\
				(entry).attr.st_ino,			\
				(entry).attr.st_mode);			\
		__rc = fuse_reply_entry(req, &entry);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_entry returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_STATFS(desc, req, stat)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(desc, "Returning statfs");		\
		__rc = fuse_reply_statfs(req, stat);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_statfs returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_IOCTL_SIZE(desc, req, arg, size)			\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(desc, "Returning ioctl");		\
		__rc = fuse_reply_ioctl(req, 0, arg, size);		\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(desc,				\
					"fuse_reply_ioctl returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_IOCTL(desc, req, arg)			\
	DFUSE_REPLY_IOCTL_SIZE(desc, req, &(arg), sizeof(arg))

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
	struct stat		ie_stat;

	dfs_obj_t		*ie_obj;

	/** DAOS object ID of the dfs object.  Used for uniquely identifying
	 * files
	 */
	daos_obj_id_t		ie_oid;

	/** The name of the entry, relative to the parent.
	 * This would have been valid when the inode was first observed
	 * however may be incorrect at any point after that.  It may not
	 * even match the local kernels view of the projection as it is
	 * not updated on local rename requests.
	 */
	char			ie_name[NAME_MAX + 1];

	/** The parent inode of this entry.
	 *
	 * As with name this will be correct when created however may
	 * be incorrect at any point after that.  The inode does not hold
	 * a reference on the parent so the inode may not be valid.
	 */
	fuse_ino_t		ie_parent;

	struct dfuse_cont	*ie_dfs;

	/** Hash table of inodes
	 * All valid inodes are kept in a hash table, using the hash table
	 * locking.
	 */
	d_list_t		ie_htl;

	/** Reference counting for the inode.
	 * Used by the hash table callbacks
	 */
	ATOMIC uint		ie_ref;

	/** written region for truncated files (i.e. ie_truncated set) */
	size_t			ie_start_off;
	size_t			ie_end_off;

	/** file was truncated from 0 to a certain size */
	bool			ie_truncated;

	bool			ie_root;

	/** File has been unlinked from daos */
	bool			ie_unlinked;
};

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

extern char *duns_xattr_name;

int
check_for_uns_ep(struct dfuse_projection_info *fs_handle,
		 struct dfuse_inode_entry *ie, char *attr, daos_size_t len);

void
dfuse_ie_close(struct dfuse_projection_info *, struct dfuse_inode_entry *);

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
dfuse_reply_entry(struct dfuse_projection_info *fs_handle,
		  struct dfuse_inode_entry *inode,
		  struct fuse_file_info *fi_out,
		  bool is_new,
		  fuse_req_t req);

/* Mark object as removed and invalidate any kernel data for it */
void
dfuse_oid_unlinked(struct dfuse_projection_info *fs_handle, fuse_req_t req, daos_obj_id_t *oid,
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
