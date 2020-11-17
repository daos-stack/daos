/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

#include "dfuse_common.h"

struct dfuse_info {
	struct fuse_session		*di_session;
	struct dfuse_projection_info	*di_handle;
	char				*di_pool;
	char				*di_cont;
	char				*di_group;
	char				*di_mountpoint;
	d_rank_list_t			*di_svcl;
	bool				di_threaded;
	bool				di_foreground;
	bool				di_direct_io;
	bool				di_caching;
	/* List head of dfuse_pool entries */
	d_list_t			di_dfp_list;
	pthread_mutex_t			di_lock;
};

/* Launch fuse, and do not return until complete */
bool
dfuse_launch_fuse(struct dfuse_info *dfuse_info,
		  struct fuse_lowlevel_ops *flo,
		  struct fuse_args *args,
		  struct dfuse_projection_info *dfi_handle);

struct dfuse_projection_info {
	struct dfuse_info		*dpi_info;
	uint32_t			dpi_max_read;
	uint32_t			dpi_max_write;
	/** Hash table of open inodes, this matches kernel ref counts */
	struct d_hash_table		dpi_iet;
	/** Hash table of all known/seen inodes */
	struct d_hash_table		dpi_irt;
	/** Next available inode number */
	ATOMIC uint64_t			dpi_ino_next;
	/* Event queue for async events */
	daos_handle_t			dpi_eq;
	/** Semaphore to signal event waiting for async thread */
	sem_t				dpi_sem;
	pthread_t			dpi_thread;
	bool				dpi_shutdown;
};

/*
 * Max number of 4k (fuse buffer size for readdir) blocks that need offset
 * tracking in the readdir implementation. Since in readdir implementation we
 * specify a larger buffer size (16k) to fetch the dir entries, the buffer we
 * track those entries on the OH needs to know where fuse_add_direntry() exceeds
 * the 4k size of a block that we return to readdir. In the next call to
 * readdir, we need to resume from that last offset before we exceeded that 4k
 * size. We define this max number of blocks to 8 (not 4 - 16k/4k) to account
 * for the possibility that we need to re-alloc that buffer on OH since
 * fuse_add_direntry() adds more metadata (the fuse direntry attributes) in
 * addition to the entry name, which could exceed 16K in some cases. We just
 * double the buffer size inthis case to 32k, and so we need a max of 8 offsets
 * to track in this case.
 */
#define READDIR_BLOCKS 8

struct dfuse_inode_entry;

/** what is returned as the handle for fuse fuse_file_info on create/open */
struct dfuse_obj_hdl {
	/** pointer to dfs_t */
	dfs_t		*doh_dfs;
	/** the DFS object handle */
	dfs_obj_t	*doh_obj;
	/** the inode entry for the file */
	struct dfuse_inode_entry *doh_ie;
	/** an anchor to track listing in readdir */
	daos_anchor_t	doh_anchor;
	/** current offset in dir stream (what is returned to fuse) */
	off_t		doh_fuse_off;
	/** current offset in dir stream (includes cached entries) */
	off_t		doh_dir_off[READDIR_BLOCKS];
	/** Buffer with all entries listed from DFS with the fuse dirents */
	void		*doh_buf;
	/** offset to start from of doh_buffer */
	off_t		doh_start_off[READDIR_BLOCKS];
	/** ending offset in doh_buf */
	off_t		doh_cur_off;
	/** current idx to process in doh_start_off */
	uint32_t	doh_idx;
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
	void (*mkdir)(fuse_req_t req, struct dfuse_inode_entry *parent,
		      const char *name, mode_t mode);
	void (*opendir)(fuse_req_t req, struct dfuse_inode_entry *inode,
			struct fuse_file_info *fi);
	void (*releasedir)(fuse_req_t req, struct dfuse_inode_entry *inode,
			   struct fuse_file_info *fi);
	void (*readdir)(fuse_req_t req, struct dfuse_inode_entry *inode,
			size_t size, off_t offset, struct fuse_file_info *fi);
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

struct dfuse_pool {
	daos_pool_info_t	dfp_pool_info;
	uuid_t			dfp_pool;
	daos_handle_t		dfp_poh;
	/* List of dfuse_pool entries in the process */
	d_list_t		dfp_list;
	/* List head of dfuse_dfs entries using this pool */
	d_list_t		dfp_dfs_list;
};

struct dfuse_dfs {
	struct dfuse_inode_ops	*dfs_ops;
	struct dfuse_pool	*dfs_dfp;
	dfs_t			*dfs_ns;
	uuid_t			dfs_cont;
	daos_handle_t		dfs_coh;
	daos_cont_info_t	dfs_co_info;
	ino_t			dfs_root;
	double			dfs_attr_timeout;
	/* List of dfuse_dfs entries in the dfuse_pool */
	d_list_t		dfs_list;
	pthread_mutex_t		dfs_read_mutex;
};

/*
 * struct dfuse_info contains list of dfuse_pool
 *  One of these per process.
 * struct dfuse_pool contains list of dfuse_dfs
 *  may or may not have a pool
 *  has one or more dfs.
 * struct dfuse_dfs has callbacks.
 *  may or may not have a container
 *  has one or more inodes
 *
 * struct dfuse_inode_entry is an inode.
 *  links to dfuse_dfs
 *
 * Every inode needs a dfs.
 *
 * In normal use inodes get evicted but every inode holds a ref on it's parent
 * so there's no need for inodes to hold a ref on their dfs, just the root.
 * During shutdown inodes can be processed in any order, which means we can't
 * just free the DFS when we release the root inode (unless we don't reference
 * the dfs in ie_close())
 *
 */

/* dfuse_core.c */

/* Init a dfs struct and copy essential data */
void
dfuse_dfs_init(struct dfuse_dfs *dfs, struct dfuse_dfs *parent);

/* Start a dfuse projection */
int
dfuse_start(struct dfuse_info *dfuse_info, struct dfuse_dfs *dfs);

/* Drain and free resources used by a projection */
int
dfuse_destroy_fuse(struct dfuse_projection_info *fs_handle);

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
		if (_flag)						\
			DFUSE_TRA_ERROR(HANDLE, "Flags 0%o", _flag);	\
	} while (0)

/** Dump the file mode to the logfile. */
#define LOG_MODES(HANDLE, INPUT) do {					\
		int _flag = (INPUT) & S_IFMT;				\
		LOG_MODE((HANDLE), _flag, S_IFREG);			\
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
		if (__err == ENOTSUP || __err == EIO || __err == EINVAL) \
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
		DFUSE_TRA_DEBUG(ie, "Returning attr mode %#o dir:%d",	\
				(attr)->st_mode,			\
				S_ISDIR(((attr)->st_mode)));		\
		__rc = fuse_reply_attr(req, attr,			\
				(ie)->ie_dfs->dfs_attr_timeout);	\
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

#if HAVE_CACHE_READDIR

#define DFUSE_REPLY_OPEN(oh, req, _fi)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(oh, "Returning open");		\
		if ((oh)->doh_ie->ie_dfs->dfs_attr_timeout > 0) {	\
			(_fi)->keep_cache = 1;				\
			(_fi)->cache_readdir = 1;			\
		}							\
		__rc = fuse_reply_open(req, _fi);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(oh,				\
					"fuse_reply_open returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#else

#define DFUSE_REPLY_OPEN(oh, req, _fi)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(oh, "Returning open");		\
		if ((oh)->doh_ie->ie_dfs->dfs_attr_timeout > 0) {	\
			(_fi)->keep_cache = 1;				\
		}							\
		__rc = fuse_reply_open(req, _fi);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(oh,				\
					"fuse_reply_open returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#endif

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
				"Returning entry inode %li mode %#o dir:%d", \
				(entry).attr.st_ino,			\
				(entry).attr.st_mode,			\
				S_ISDIR((entry).attr.st_mode));		\
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

	struct dfuse_dfs	*ie_dfs;

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

	/** Set to true if this is the root of the container */
	bool			ie_root;
};

/**
 * Inode record.
 *
 * Describes all inodes observed by the system since start, including all inodes
 * known by the kernel, and all inodes that have been in the past.
 *
 * This is needed to be able to generate 64 bit inode numbers from 128 bit DAOS
 * objects, to support multiple containers/pools within a filesystem and to
 * provide consistent inode numbering for the same file over time, even if the
 * kernel cache is dropped, for example because of memory pressure.
 */
struct dfuse_inode_record_id {
	struct dfuse_dfs	*irid_dfs;
	daos_obj_id_t		irid_oid;
};

struct dfuse_inode_record {
	struct dfuse_inode_record_id	ir_id;
	d_list_t			ir_htl;
	ino_t				ir_ino;
};

/* dfuse_inode.c */

int
dfuse_lookup_inode(struct dfuse_projection_info *fs_handle,
		   struct dfuse_dfs *dfs,
		   daos_obj_id_t *oid,
		   ino_t *_ino);

int
dfuse_check_for_inode(struct dfuse_projection_info *fs_handle,
		      struct dfuse_dfs *dfs,
		      struct dfuse_inode_entry **_entry);

void
ie_close(struct dfuse_projection_info *, struct dfuse_inode_entry *);

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
dfuse_cb_mkdir(fuse_req_t, struct dfuse_inode_entry *,
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
dfuse_cb_readdir(fuse_req_t, struct dfuse_inode_entry *, size_t, off_t,
		 struct fuse_file_info *);

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

void dfuse_cb_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void *arg,
		    struct fuse_file_info *fi, unsigned int flags,
		    const void *in_buf, size_t in_bufsz, size_t out_bufsz);

/* Return inode information to fuse
 *
 * Adds inode to the hash table and calls fuse_reply_entry()
 */
void
dfuse_reply_entry(struct dfuse_projection_info *fs_handle,
		  struct dfuse_inode_entry *inode,
		  struct fuse_file_info *fi_out,
		  fuse_req_t req);

/* dfuse_cont.c */
void
dfuse_cont_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name);

void
dfuse_cont_mkdir(fuse_req_t req, struct dfuse_inode_entry *parent,
		 const char *name, mode_t mode);

/* dfuse_pool.c */
void
dfuse_pool_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		  const char *name);

#endif /* __DFUSE_H__ */
