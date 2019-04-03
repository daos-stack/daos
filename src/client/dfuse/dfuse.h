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

/**
 * \file
 *
 * CNSS/IOF client headers.
 */

#ifndef __IOF_H__
#define __IOF_H__

#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <gurt/list.h>
#include <gurt/hash.h>

#include "dfuse_cnss.h"
#include "dfuse_gah.h"
#include "dfuse_fs.h"
#include "dfuse_bulk.h"
#include "dfuse_pool.h"

/**
 * A common structure for holding a cart context and thread details.
 *
 * This is included in both dfuse_state for global values, and once per
 * projection for projection specific entries.
 */
struct dfuse_ctx {
	/** cart context */
	crt_context_t			crt_ctx;
	/** pthread identifier */
	pthread_t			thread;
	/** Tracker to detect thread start */
	struct dfuse_tracker		thread_start_tracker;
	/** Tracker to signal thread stop */
	struct dfuse_tracker		thread_stop_tracker;
	/** Poll interval to pass to crt_progress */
	uint32_t			poll_interval;
	/** Callback function to pass to crt_progress() */
	crt_progress_cond_cb_t		callback_fn;
};

/**
 * IOF Group struct.
 */

struct dfuse_group_info {
	/** Service group pointer */
	struct dfuse_service_group	grp;
};

/**
 * Global state for IOF client.
 *
 */
struct dfuse_state {
	struct cnss_info *cnss_info;
	/** CaRT RPC protocol used for metadata */
	struct crt_proto_format		*proto;
	/** CaRT RPC protocol used for I/O */
	struct crt_proto_format		*io_proto;
	/** dfuse_ctx for state */
	struct dfuse_ctx			dfuse_ctx;
	/** CNSS Prefix.  Parent directory of projections */
	char				*cnss_prefix;
	/** ctrl_fs inoss directory handle */
	struct ctrl_dir			*ionss_dir;
	/** ctrl_fs projections directory handle */
	struct ctrl_dir			*projections_dir;
	/** Group information */
	struct dfuse_group_info		group;
};

struct dfuse_projection_info {
	struct dfuse_projection		proj;
	struct dfuse_ctx			*ctx_array;
	int ctx_num;
	struct dfuse_state		*dfuse_state;
	struct ios_gah			gah;
	d_list_t			link;
	struct fuse_session		*session;
	/** The basename of the mount point */
	struct ios_name			mnt_dir;

	/** The name of the ctrlfs directory */
	struct ios_name			ctrl_dir;
	/** Feature Flags */
	uint64_t			flags;
	int				fs_id;
	struct dfuse_pool			pool;
	struct dfuse_pool_type		*dh_pool;
	struct dfuse_pool_type		*fgh_pool;
	struct dfuse_pool_type		*fsh_pool;
	struct dfuse_pool_type		*close_pool;
	struct dfuse_pool_type		*lookup_pool;
	struct dfuse_pool_type		*mkdir_pool;
	struct dfuse_pool_type		*symlink_pool;
	struct dfuse_pool_type		*fh_pool;
	struct dfuse_pool_type		*rb_pool_page;
	struct dfuse_pool_type		*rb_pool_large;
	struct dfuse_pool_type		*write_pool;
	uint32_t			max_read;
	uint32_t			max_iov_read;
	uint32_t			readdir_size;
	/** Hash table of open inodes */
	struct d_hash_table		inode_ht;

	/** Held for any access/modification to a gah on any inode/file/dir */
	pthread_mutex_t			gah_lock;

};

/*
 * Returns the correct RPC Type ID from the protocol registry.
 */
#define FS_TO_OP(HANDLE, FN) (CRT_PROTO_OPC((HANDLE)->dfuse_state->proto->cpf_base, \
					    (HANDLE)->dfuse_state->proto->cpf_ver, \
					    DEF_RPC_TYPE(FN)))

#define FS_TO_IOOP(HANDLE, IDX) (CRT_PROTO_OPC((HANDLE)->proj.io_proto->cpf_base, \
					       (HANDLE)->proj.io_proto->cpf_ver, \
					       IDX))

struct fuse_lowlevel_ops *dfuse_get_fuse_ops(uint64_t);

/* Helper macros for open() and creat() to log file access modes */
#define LOG_MODE(HANDLE, FLAGS, MODE) do {			\
		if ((FLAGS) & (MODE))				\
			IOF_TRACE_DEBUG(HANDLE, #MODE);		\
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
			IOF_TRACE_ERROR(HANDLE, "Flags 0%o", _flag);	\
	} while (0)

/** Dump the file mode to the logfile. */
#define LOG_MODES(HANDLE, INPUT) do {					\
		int _flag = (INPUT) & S_IFMT;				\
		LOG_MODE((HANDLE), _flag, S_IFREG);			\
		LOG_MODE((HANDLE), _flag, S_ISUID);			\
		LOG_MODE((HANDLE), _flag, S_ISGID);			\
		LOG_MODE((HANDLE), _flag, S_ISVTX);			\
		if (_flag)						\
			IOF_TRACE_ERROR(HANDLE, "Mode 0%o", _flag);	\
	} while (0)

#define IOF_UNSUPPORTED_CREATE_FLAGS (O_ASYNC | O_CLOEXEC | O_DIRECTORY | \
					O_NOCTTY | O_PATH)

#define IOF_UNSUPPORTED_OPEN_FLAGS (IOF_UNSUPPORTED_CREATE_FLAGS | O_CREAT | \
					O_EXCL)

#define IOC_REPLY_ERR_RAW(handle, req, status)				\
	do {								\
		int __err = status;					\
		int __rc;						\
		if (__err <= 0) {					\
			IOF_TRACE_ERROR(handle,				\
					"Invalid call to fuse_reply_err: %d", \
					__err);				\
			__err = EIO;					\
		}							\
		if (__err == ENOTSUP || __err == EIO)			\
			IOF_TRACE_WARNING(handle, "Returning %d '%s'",	\
					  __err, strerror(__err));	\
		else							\
			IOF_TRACE_DEBUG(handle, "Returning %d '%s'",	\
					__err, strerror(__err));	\
		__rc = fuse_reply_err(req, __err);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(handle,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define IOF_FUSE_REPLY_ERR(req, status)			\
	do {						\
		IOC_REPLY_ERR_RAW(req, req, status);	\
		IOF_TRACE_DOWN(req);			\
	} while (0)

#define IOC_REPLY_ERR(dfuse_req, status)				\
	do {								\
		IOC_REPLY_ERR_RAW(dfuse_req, (dfuse_req)->req, status);	\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOF_FUSE_REPLY_ZERO(req)					\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(req, "Returning 0");			\
		__rc = fuse_reply_err(req, 0);				\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(req,				\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(req);					\
	} while (0)

#define IOC_REPLY_ZERO(dfuse_req)					\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(dfuse_req, "Returning 0");		\
		__rc = fuse_reply_err((dfuse_req)->req, 0);		\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(dfuse_req,			\
					"fuse_reply_err returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOC_REPLY_ATTR(dfuse_req, attr)					\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(dfuse_req, "Returning attr");		\
		__rc = fuse_reply_attr((dfuse_req)->req, attr, 0);	\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(dfuse_req,			\
					"fuse_reply_attr returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOC_REPLY_READLINK(dfuse_req, path)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(dfuse_req, "Returning path '%s'", path); \
		__rc = fuse_reply_readlink((dfuse_req)->req, path);	\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(dfuse_req,			\
					"fuse_reply_readlink returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOC_REPLY_WRITE(handle, req, bytes)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(handle, "Returning write(%zi)", bytes);	\
		__rc = fuse_reply_write(req, bytes);			\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(handle,				\
					"fuse_reply_attr returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

#define IOC_REPLY_OPEN(dfuse_req, fi)					\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(dfuse_req, "Returning open");		\
		__rc = fuse_reply_open((dfuse_req)->req, &fi);		\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(dfuse_req,			\
					"fuse_reply_open returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOC_REPLY_CREATE(dfuse_req, entry, fi)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(dfuse_req, "Returning create");		\
		__rc = fuse_reply_create((dfuse_req)->req, &entry, &fi); \
		if (__rc != 0)						\
			IOF_TRACE_ERROR(dfuse_req,			\
					"fuse_reply_create returned %d:%s",\
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOC_REPLY_ENTRY(dfuse_req, entry)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(dfuse_req, "Returning entry");		\
		__rc = fuse_reply_entry((dfuse_req)->req, &entry);	\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(dfuse_req,			\
					"fuse_reply_entry returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOF_FUSE_REPLY_STATFS(dfuse_req, stat)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(dfuse_req, "Returning statfs");		\
		__rc = fuse_reply_statfs((dfuse_req)->req, stat);	\
		if (__rc != 0)						\
			IOF_TRACE_ERROR(dfuse_req,			\
					"fuse_reply_statfs returned %d:%s", \
					__rc, strerror(-__rc));		\
		IOF_TRACE_DOWN(dfuse_req);				\
	} while (0)

#define IOC_REPLY_IOCTL(handle, req, gah_info)				\
	do {								\
		int __rc;						\
		IOF_TRACE_DEBUG(handle, "Returning ioctl");		\
		__rc = fuse_reply_ioctl(req, 0, &(gah_info), sizeof(gah_info)); \
		if (__rc != 0)						\
			IOF_TRACE_ERROR(handle,				\
					"fuse_reply_ioctl returned %d:%s", \
					__rc, strerror(-__rc));		\
	} while (0)

struct dfuse_request;

/**
 * IOF Request API.
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
	/** Offset of GAH in RPC input buffer */
	off_t	gah_offset;
	/** Set to true if gah_offset is set */
	bool	have_gah;
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
 * IOF Request descriptor.
 *
 */
struct dfuse_request {
	/** Pointer to projection for this request. */
	struct dfuse_projection_info	*fsh;
	/** Pointer to the RPC for this request. */
	crt_rpc_t			*rpc;
	/** Fuse request for this IOF request, may be 0 */
	fuse_req_t			req;
	/** Callbacks to use for this request */
	const struct dfuse_request_api	*ir_api;
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
#define IOC_REQUEST_INIT(REQUEST, FSH)			\
	do {						\
		(REQUEST)->fsh = FSH;			\
		(REQUEST)->rpc = NULL;			\
		(REQUEST)->ir_rs = RS_INIT;		\
		D_INIT_LIST_HEAD(&(REQUEST)->ir_list);	\
	} while (0)

/** Reset a request for re-use.  To be called before each use */
#define IOC_REQUEST_RESET(REQUEST)					\
	do {								\
		D_ASSERT((REQUEST)->ir_rs == RS_INIT ||			\
			(REQUEST)->ir_rs == RS_RESET ||			\
			(REQUEST)->ir_rs == RS_LIVE);			\
		(REQUEST)->ir_rs = RS_RESET;				\
		(REQUEST)->ir_ht = RHS_NONE;				\
		(REQUEST)->ir_inode = NULL;				\
		(REQUEST)->rc = 0;					\
	} while (0)

/**
 * Resolve request status.
 *
 * Correctly resolve the return codes and errors from the RPC response.
 * If the error code was already non-zero, it means an error occurred on
 * the client; do nothing. A non-zero error code in the RPC response
 * denotes a server error, in which case, set the status error code to EIO.
 */
#define IOC_REQUEST_RESOLVE(REQUEST, OUT)				\
	do {								\
		if (((OUT) != NULL) && (!(REQUEST)->rc)) {		\
			(REQUEST)->rc = (OUT)->rc;			\
			if ((OUT)->err)	{				\
				if ((OUT)->rc == -DER_NOMEM)		\
					(REQUEST)->rc = ENOMEM;		\
				else					\
					(REQUEST)->rc = EIO;		\
				IOF_TRACE_INFO((REQUEST),		\
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
	/** The GAH for this inode */
	struct ios_gah	gah;
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
	/** The GAH to use when accessing the directory */
	struct ios_gah			gah;
	/** Request for opening the directory */
	struct dfuse_request		open_req;
	/** Request for closing the directory */
	struct dfuse_request		close_req;
	/** Any RPC reference held across readdir() calls */
	crt_rpc_t			*rpc;
	/** Pointer to any retreived data from readdir() RPCs */
	struct dfuse_readdir_reply	*replies;
	int				reply_count;
	void				*replies_base;
	/** Set to True if the current batch of replies is the final one */
	int				last_replies;
	/** The inode number of the directory */
	ino_t				inode_num;
	/** Endpoint for this directory handle */
	crt_endpoint_t			ep;

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
	struct dfuse_local_bulk		lb;
	struct dfuse_pool_type		*pt;
	size_t				buf_size;
	bool				failure;
};

/** Write buffer descriptor */
struct dfuse_wb {
	struct dfuse_request		wb_req;
	struct dfuse_local_bulk		lb;
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
	crt_opcode_t			opcode;
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
	struct dfuse_inode_entry		*ie;
	struct dfuse_request		request;
	d_list_t			list;
	crt_opcode_t			opcode;
	struct dfuse_pool_type		*pool;
	char				*dest;
};

/* inode.c */

/* Convert from a inode to a GAH using the hash table */
int
find_gah(struct dfuse_projection_info *, fuse_ino_t, struct ios_gah *);

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
dfuse_cb_readdir(fuse_req_t, fuse_ino_t, size_t, off_t,
		 struct fuse_file_info *);

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

#endif
