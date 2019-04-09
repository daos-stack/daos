/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <gurt/list.h>
#include <gurt/hash.h>

#include "dfuse_gah.h"
#include "dfuse_fs.h"
#include "dfuse_da.h"

#include "dfuse_common.h"
#include "dfuse.h"

struct fs_info {
	char			*fsi_mnt;
	struct fuse		*fsi_fuse;
	struct fuse_session	*fsi_session;
	pthread_t		fsi_thread;
	pthread_mutex_t		fsi_lock;
	struct dfuse_projection_info *fsi_handle;
	bool			fsi_running;
	bool			fsi_mt;
};

struct dfuse_info {
	struct dfuse_state	*dfuse_state;
	struct fs_info		ci_fsinfo;
};

bool
dfuse_register_fuse(struct dfuse_info *dfuse_info,
		   struct fuse_lowlevel_ops *flo,
		   struct fuse_args *args,
		   const char *mnt,
		   bool threaded,
		   void *private_data,
		   struct fuse_session **sessionp);

struct dfuse_state *
dfuse_plugin_init();

void
dfuse_reg(struct dfuse_state *dfuse_state, struct dfuse_info *dfuse_info);

void
dfuse_post_start(struct dfuse_state *dfuse_state);

void
dfuse_finish(struct dfuse_state *dfuse_state);

void
dfuse_flush_fuse(struct dfuse_projection_info *fs_handle);

int
dfuse_deregister_fuse(struct dfuse_projection_info *fs_handle);

/**
 * Global state for DFUSE client.
 *
 */
struct dfuse_state {
	struct dfuse_info		*dfuse_info;
	/** CNSS Prefix.  Parent directory of projections */
	char				*dfuse_prefix;
	/** ctrl_fs inoss directory handle */
	struct ctrl_dir			*ionss_dir;
	/** ctrl_fs projections directory handle */
	struct ctrl_dir			*projections_dir;
	/** Group information */
	struct dfuse_service_group	grp;
};

struct dfuse_projection_info {
	struct dfuse_projection		proj;
	struct dfuse_state		*dfuse_state;
	d_list_t			link;
	struct fuse_session		*session;
	/** Feature Flags */
	uint64_t			flags;
	int				fs_id;
	struct dfuse_da			da;
	struct dfuse_da_type		*dh_da;
	struct dfuse_da_type		*fgh_da;
	struct dfuse_da_type		*fsh_da;
	struct dfuse_da_type		*close_da;
	struct dfuse_da_type		*lookup_da;
	struct dfuse_da_type		*mkdir_da;
	struct dfuse_da_type		*symlink_da;
	struct dfuse_da_type		*fh_da;
	struct dfuse_da_type		*rb_da_page;
	struct dfuse_da_type		*rb_da_large;
	struct dfuse_da_type		*write_da;
	uint32_t			max_read;
	uint32_t			max_iov_read;
	/** Hash table of open inodes */
	struct d_hash_table		inode_ht;
};

struct fuse_lowlevel_ops *dfuse_get_fuse_ops(uint64_t);

/* Helper macros for open() and creat() to log file access modes */
#define LOG_MODE(HANDLE, FLAGS, MODE) do {			\
		if ((FLAGS) & (MODE))				\
			DFUSE_TRA_DEBUG(HANDLE, #MODE);	\
		FLAGS &= ~MODE;					\
	} while (0)

/**
 * Dump the file open mode to the logile.
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

#define DFUSE_REPLY_ERR_RAW(handle, req, status)			\
	do {								\
		int __err = status;					\
		int __rc;						\
		if (__err <= 0) {					\
			DFUSE_TRA_ERROR(handle,				\
					"Invalid call to fuse_reply_err: %d", \
					__err);				\
			__err = EIO;					\
		}							\
		if (__err == ENOTSUP || __err == EIO)			\
			DFUSE_TRA_WARNING(handle, "Returning %d '%s'", \
					  __err, strerror(__err));	\
		else							\
			DFUSE_TRA_DEBUG(handle, "Returning %d '%s'",	\
					__err, strerror(__err));	\
		__rc = fuse_reply_err(req, __err);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(handle,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_FUSE_REPLY_ERR(req, status)		\
	do {						\
		DFUSE_REPLY_ERR_RAW(req, req, status);	\
		DFUSE_TRA_DOWN(req);			\
	} while (0)

#define DFUSE_REPLY_ERR(dfuse_req, status)				\
	do {								\
		DFUSE_REPLY_ERR_RAW(dfuse_req, (dfuse_req)->req, status); \
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_FUSE_REPLY_ZERO(req)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(req, "Returning 0");			\
		__rc = fuse_reply_err(req, 0);				\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(req,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(req);					\
	} while (0)

#define DFUSE_REPLY_ZERO(dfuse_req)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(dfuse_req, "Returning 0");		\
		__rc = fuse_reply_err((dfuse_req)->req, 0);		\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(dfuse_req,			\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_REPLY_ATTR(dfuse_req, attr)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(dfuse_req, "Returning attr");		\
		__rc = fuse_reply_attr((dfuse_req)->req, attr, 0);	\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(dfuse_req,			\
					"fuse_reply_attr returned %d:%s", \
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_REPLY_READLINK(dfuse_req, path)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(dfuse_req, "Returning path '%s'", path); \
		__rc = fuse_reply_readlink((dfuse_req)->req, path);	\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(dfuse_req,			\
					"fuse_reply_readlink returned %d:%s", \
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_REPLY_WRITE(handle, req, bytes)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(handle, "Returning write(%zi)", bytes); \
		__rc = fuse_reply_write(req, bytes);			\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(handle,				\
					"fuse_reply_attr returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define DFUSE_REPLY_OPEN(dfuse_req, fi)					\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(dfuse_req, "Returning open");		\
		__rc = fuse_reply_open((dfuse_req)->req, &fi);		\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(dfuse_req,			\
					"fuse_reply_open returned %d:%s", \
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_REPLY_CREATE(dfuse_req, entry, fi)			\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(dfuse_req, "Returning create");	\
		__rc = fuse_reply_create((dfuse_req)->req, &entry, &fi); \
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(dfuse_req,			\
					"fuse_reply_create returned %d:%s",\
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_REPLY_ENTRY(dfuse_req, entry)				\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(dfuse_req, "Returning entry");	\
		__rc = fuse_reply_entry((dfuse_req)->req, &entry);	\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(dfuse_req,			\
					"fuse_reply_entry returned %d:%s", \
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_FUSE_REPLY_STATFS(dfuse_req, stat)			\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(dfuse_req, "Returning statfs");	\
		__rc = fuse_reply_statfs((dfuse_req)->req, stat);	\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(dfuse_req,			\
					"fuse_reply_statfs returned %d:%s", \
					__rc, strerror(-__rc));		\
		DFUSE_TRA_DOWN(dfuse_req);				\
	} while (0)

#define DFUSE_REPLY_IOCTL(handle, req, gah_info)			\
	do {								\
		int __rc;						\
		DFUSE_TRA_DEBUG(handle, "Returning ioctl");		\
		__rc = fuse_reply_ioctl(req, 0, &(gah_info),		\
					sizeof(gah_info));		\
		if (__rc != 0)						\
			DFUSE_TRA_ERROR(handle,				\
					"fuse_reply_ioctl returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

struct dfuse_request;

/**
 * DFUSE Request API.
 *
 * Set of callbacks invoked during the lifetime of a request.
 */
struct dfuse_request_api {
	/** Called once, per request with the result
	 *
	 * Should return true if ir_ht is set to RHS_INODE_NUM, and
	 * an open reference should be kept on the inode after on_result
	 * returns.
	 */
	bool	(*on_result)(struct dfuse_request *req);
};

enum dfuse_request_state {
	RS_INIT = 1,
	RS_RESET,
	RS_LIVE
};

/** The type of any handle stored in the request.
 *
 * If set to other than RHS_NONE then the GAH from the appropriate
 * pointer type will be used, rather than the PSR.
 */
enum dfuse_request_htype {
	RHS_NONE,
	RHS_ROOT,
	RHS_INODE,
	RHS_FILE,
	RHS_DIR,
	RHS_INODE_NUM,
};

/**
 * DFUSE Request descriptor.
 *
 */
struct dfuse_request {
	/** Pointer to projection for this request. */
	struct dfuse_projection_info	*fsh;
	/** Fuse request for this DFUSE request, may be 0 */
	fuse_req_t			req;
	/** Callbacks to use for this request */
	const struct dfuse_request_api	*ir_api;

	/* Mock entry, to avoid having to call crt_req_get() in code
	 * which doesn't have a RPC pointer.
	 */
	void *out;
	/** Error status of this request.
	 *
	 * This is a libc error number and is set before a call to
	 *  on_result
	 */
	int				rc;
	/** Request state.
	 *
	 * Used to ensure REQUEST_INIT()/REQUEST_RESET() have been invoked
	 * correctly.
	 */
	enum dfuse_request_state		ir_rs;

	/** Request handle type */
	enum dfuse_request_htype		ir_ht;

	union {
		/** Optional pointer to handle.
		 * Which one of these to use is set by the ir_ht value
		 */
		struct dfuse_inode_entry	*ir_inode;
		struct dfuse_file_handle	*ir_file;
		struct dfuse_dir_handle	*ir_dir;
		fuse_ino_t		ir_inode_num;
	};
	/** List of requests.
	 *
	 * Used during failover to keep a list of requests that need to be
	 * actioned once failover is complete.
	 */
	d_list_t			ir_list;
};

/** Initialise a request.  To be called once per request */
#define DFUSE_REQUEST_INIT(REQUEST, FSH)		\
	do {						\
		(REQUEST)->fsh = FSH;			\
		(REQUEST)->ir_rs = RS_INIT;		\
		D_INIT_LIST_HEAD(&(REQUEST)->ir_list);	\
	} while (0)

/** Reset a request for re-use.  To be called before each use */
#define DFUSE_REQUEST_RESET(REQUEST)				\
	do {							\
		D_ASSERT((REQUEST)->ir_rs == RS_INIT ||		\
			(REQUEST)->ir_rs == RS_RESET ||		\
			(REQUEST)->ir_rs == RS_LIVE);		\
		(REQUEST)->ir_rs = RS_RESET;			\
		(REQUEST)->ir_ht = RHS_NONE;			\
		(REQUEST)->ir_inode = NULL;			\
		(REQUEST)->rc = 0;				\
	} while (0)

/**
 * Resolve request status.
 *
 * Correctly resolve the return codes and errors from the RPC response.
 * If the error code was already non-zero, it means an error occurred on
 * the client; do nothing. A non-zero error code in the RPC response
 * denotes a server error, in which case, set the status error code to EIO.
 */
#define DFUSE_REQUEST_RESOLVE(REQUEST, OUT)				\
	do {								\
		if (((OUT) != NULL) && (!(REQUEST)->rc)) {		\
			(REQUEST)->rc = (OUT)->rc;			\
			if ((OUT)->err)	{				\
				if ((OUT)->rc == -DER_NOMEM)		\
					(REQUEST)->rc = ENOMEM;		\
				else					\
					(REQUEST)->rc = EIO;		\
				DFUSE_TRA_INFO((REQUEST),		\
					"Returning '%s' from -%s",	\
					strerror((REQUEST)->rc),	\
					d_errstr((OUT)->err));		\
			}						\
		}							\
	} while (0)

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
	struct stat	stat;

	/** The name of the entry, relative to the parent.
	 * This would have been valid when the inode was first observed
	 * however may be incorrect at any point after that.  It may not
	 * even match the local kernels view of the projection as it is
	 * not updated on local rename requests.
	 */
	char		name[256];
	/** The parent inode of this entry.
	 *
	 * As with name this will be correct when created however may
	 * be incorrect at any point after that.  The inode does not hold
	 * a reference on the parent so the inode may not be valid.
	 */
	fuse_ino_t	parent;

	/** Hash table of inodes
	 * All valid inodes are kept in a hash table, using the hash table
	 * locking.
	 */
	d_list_t	ie_htl;

	/** Reference counting for the inode.
	 * Used by the hash table callbacks
	 */
	ATOMIC uint	ie_ref;
};

/**
 * Directory handle.
 *
 * Describes a open directory, may be used for readdir() calls.
 */
struct dfuse_dir_handle {
	/** Request for opening the directory */
	struct dfuse_request		open_req;
	/** Request for closing the directory */
	struct dfuse_request		close_req;
	int				reply_count;
	void				*replies_base;
	/** Set to True if the current batch of replies is the final one */
	int				last_replies;
	/** The inode number of the directory */
	ino_t				inode_num;

	d_list_t dh_free_list;
};

/**
 * Open file handle.
 *
 * Describes a file open for reading/writing.
 */
struct dfuse_file_handle {
	/** Common information for file handle, contains GAH and EP
	 * information.  This is shared between CNSS and IL code to allow
	 * use of some common code.
	 */
	struct dfuse_file_common		common;

	/** Open request, with precreated RPC */
	struct dfuse_request		open_req;
	/** Create request, with precreated RPC */
	struct dfuse_request		creat_req;
	/* Release request, with precreated RPC */
	struct dfuse_request		release_req;

	d_list_t fh_free_list;

	/** The inode number of the file */
	ino_t				inode_num;
	/** A pre-allocated inode entry.  This is created as the struct is
	 * allocated and then used on a successful create() call.  Once
	 * the file handle is in use then this field will be NULL.
	 */
	struct dfuse_inode_entry		*ie;
};

/** Read buffer descriptor */
struct dfuse_rb {
	struct dfuse_request		rb_req;
	struct fuse_bufvec		fbuf;
	struct dfuse_da_type		*pt;
	size_t				buf_size;
	bool				failure;
};

/** Write buffer descriptor */
struct dfuse_wb {
	struct dfuse_request		wb_req;
	bool				failure;
};

/** Common request type.
 *
 * Used for getattr, setattr and close only.
 *
 */
struct common_req {
	d_list_t			list;
	struct dfuse_request		request;
};

/** Callback structure for inode migrate RPC.
 *
 * Used so migrate callback function has access to the filesystem handle.
 */
struct dfuse_inode_migrate {
	struct dfuse_inode_entry *im_ie;
	struct dfuse_projection_info *im_fsh;
};

/** Entry request type.
 *
 * Request for all RPC types that can return a new inode.
 */
struct entry_req {
	struct dfuse_inode_entry	*ie;
	struct dfuse_request		request;
	d_list_t			list;
	struct dfuse_da_type		*da;
	char				*dest;
};

/* inode.c */

int
find_inode(struct dfuse_request *);

void
ie_close(struct dfuse_projection_info *, struct dfuse_inode_entry *);

int
dfuse_fs_send(struct dfuse_request *request);

int
dfuse_simple_resend(struct dfuse_request *request);

bool
dfuse_gen_cb(struct dfuse_request *);

void
dfuse_cb_lookup(fuse_req_t, fuse_ino_t, const char *);

void
dfuse_cb_forget(fuse_req_t, fuse_ino_t, uint64_t);

void
dfuse_cb_forget_multi(fuse_req_t, size_t, struct fuse_forget_data *);

void
dfuse_cb_getattr(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void
dfuse_cb_statfs(fuse_req_t, fuse_ino_t);

void
dfuse_cb_readlink(fuse_req_t, fuse_ino_t);

void
dfuse_cb_mkdir(fuse_req_t, fuse_ino_t, const char *, mode_t);

void
dfuse_cb_open(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void
dfuse_cb_create(fuse_req_t, fuse_ino_t, const char *, mode_t,
		struct fuse_file_info *);

void
dfuse_cb_read(fuse_req_t, fuse_ino_t, size_t, off_t,
	      struct fuse_file_info *);

void
dfuse_cb_release(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void
dfuse_int_release(struct dfuse_file_handle *);

void
dfuse_cb_unlink(fuse_req_t, fuse_ino_t, const char *);

void
dfuse_cb_rmdir(fuse_req_t, fuse_ino_t, const char *);

void
dfuse_cb_opendir(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void
dfuse_cb_rename(fuse_req_t, fuse_ino_t, const char *, fuse_ino_t,
		const char *, unsigned int);

void
dfuse_cb_releasedir(fuse_req_t, fuse_ino_t, struct fuse_file_info *);

void
dfuse_int_releasedir(struct dfuse_dir_handle *);

void
dfuse_cb_write(fuse_req_t, fuse_ino_t, const char *, size_t, off_t,
	       struct fuse_file_info *);

void
dfuse_cb_write_buf(fuse_req_t, fuse_ino_t, struct fuse_bufvec *, off_t,
		   struct fuse_file_info *);

void
dfuse_cb_ioctl(fuse_req_t, fuse_ino_t, int, void *, struct fuse_file_info *,
	       unsigned int, const void *, size_t, size_t);

void
dfuse_cb_setattr(fuse_req_t, fuse_ino_t, struct stat *, int,
		 struct fuse_file_info *);

void
dfuse_cb_symlink(fuse_req_t, const char *, fuse_ino_t, const char *);

void
dfuse_cb_fsync(fuse_req_t, fuse_ino_t, int, struct fuse_file_info *);

bool
dfuse_entry_cb(struct dfuse_request *);

#endif /* __DFUSE_H__ */
