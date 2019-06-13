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

#include <fuse3/fuse_lowlevel.h>

#include "dfuse_common.h"
#include "dfuse.h"

#define SHOW_FLAG(HANDLE, FLAGS, FLAG)					\
	do {								\
		DFUSE_TRA_INFO(HANDLE, "Flag " #FLAG " %s",		\
			(FLAGS & FLAG) ? "enabled" : "disabled");	\
		FLAGS &= ~FLAG;						\
	} while (0)

static void
dfuse_show_flags(void *handle, unsigned int in)
{
	SHOW_FLAG(handle, in, FUSE_CAP_ASYNC_READ);
	SHOW_FLAG(handle, in, FUSE_CAP_POSIX_LOCKS);
	SHOW_FLAG(handle, in, FUSE_CAP_ATOMIC_O_TRUNC);
	SHOW_FLAG(handle, in, FUSE_CAP_EXPORT_SUPPORT);
	SHOW_FLAG(handle, in, FUSE_CAP_DONT_MASK);
	SHOW_FLAG(handle, in, FUSE_CAP_SPLICE_WRITE);
	SHOW_FLAG(handle, in, FUSE_CAP_SPLICE_MOVE);
	SHOW_FLAG(handle, in, FUSE_CAP_SPLICE_READ);
	SHOW_FLAG(handle, in, FUSE_CAP_FLOCK_LOCKS);
	SHOW_FLAG(handle, in, FUSE_CAP_IOCTL_DIR);
	SHOW_FLAG(handle, in, FUSE_CAP_AUTO_INVAL_DATA);
	SHOW_FLAG(handle, in, FUSE_CAP_READDIRPLUS);
	SHOW_FLAG(handle, in, FUSE_CAP_READDIRPLUS_AUTO);
	SHOW_FLAG(handle, in, FUSE_CAP_ASYNC_DIO);
	SHOW_FLAG(handle, in, FUSE_CAP_WRITEBACK_CACHE);
	SHOW_FLAG(handle, in, FUSE_CAP_NO_OPEN_SUPPORT);
	SHOW_FLAG(handle, in, FUSE_CAP_PARALLEL_DIROPS);
	SHOW_FLAG(handle, in, FUSE_CAP_POSIX_ACL);
	SHOW_FLAG(handle, in, FUSE_CAP_HANDLE_KILLPRIV);

	if (in)
		DFUSE_TRA_ERROR(handle, "Unknown flags %#x", in);
}

/* Called on filesystem init.  It has the ability to both observe configuration
 * options, but also to modify them.  As we do not use the FUSE command line
 * parsing this is where we apply tunables.
 */
static void
dfuse_fuse_init(void *arg, struct fuse_conn_info *conn)
{
	struct dfuse_projection_info *fs_handle = arg;

	DFUSE_TRA_INFO(fs_handle,
		       "Fuse configuration for projection srv:%d cli:%d",
		       fs_handle->fs_id, fs_handle->proj.cli_fs_id);

	DFUSE_TRA_INFO(fs_handle, "Proto %d %d",
		       conn->proto_major, conn->proto_minor);

	/* This value has to be set here to the same value passed to
	 * register_fuse().  Fuse always sets this value to zero so
	 * set it before reporting the value.
	 */
	conn->max_read = fs_handle->max_read;
	conn->max_write = fs_handle->proj.max_write;

	DFUSE_TRA_INFO(fs_handle, "max read %#x", conn->max_read);
	DFUSE_TRA_INFO(fs_handle, "max write %#x", conn->max_write);
	DFUSE_TRA_INFO(fs_handle, "readahead %#x", conn->max_readahead);

	DFUSE_TRA_INFO(fs_handle, "Capability supported %#x", conn->capable);

	dfuse_show_flags(fs_handle, conn->capable);

	/* This does not work as ioctl.c assumes fi->fh is a file handle */
	conn->want &= ~FUSE_CAP_IOCTL_DIR;

	DFUSE_TRA_INFO(fs_handle, "Capability requested %#x", conn->want);

	dfuse_show_flags(fs_handle, conn->want);

	DFUSE_TRA_INFO(fs_handle, "max_background %d", conn->max_background);
	DFUSE_TRA_INFO(fs_handle,
		       "congestion_threshold %d", conn->congestion_threshold);
}

void
df_ll_create(fuse_req_t req, fuse_ino_t parent, const char *name,
	     mode_t mode, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode;
	d_list_t			*rlink;
	bool				keep_ref;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dffs_ops->create) {
		D_GOTO(err, rc = ENOTSUP);
	}

	keep_ref = parent_inode->ie_dfs->dffs_ops->create(req, parent_inode,
							  name, mode, fi);
	if (!keep_ref) {
		d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	}
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_file_handle	*handle = NULL;
	struct dfuse_inode_entry	*inode = NULL;
	d_list_t			*rlink;
	int rc;

	if (fi)
		handle = (void *)fi->fh;

	if (handle) {
		D_GOTO(err, rc = ENOTSUP);
	} else {
		rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &ino,
					sizeof(ino));
		if (!rlink) {
			DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
					ino);
			D_GOTO(err, rc = ENOENT);
		}
		inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		if (!inode->ie_dfs->dffs_ops->getattr) {
			D_GOTO(decref, rc = ENOTSUP);
		}

		inode->ie_dfs->dffs_ops->getattr(req, inode);
		d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	}
	return;
decref:
	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode;
	d_list_t			*rlink;
	bool				keep_ref;
	int rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	keep_ref = parent_inode->ie_dfs->dffs_ops->lookup(req,
							  parent_inode, name);

	if (!keep_ref) {
		d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	}
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode;
	d_list_t			*rlink;
	bool				keep_ref;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dffs_ops->mkdir) {
		D_GOTO(decref, rc = ENOTSUP);
	}

	keep_ref = parent_inode->ie_dfs->dffs_ops->mkdir(req, parent_inode,
							 name, mode);

	if (!keep_ref) {
		d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	}
	return;
decref:
	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dffs_ops->opendir)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dffs_ops->opendir(req, inode, fi);

	return;
decref:
	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dffs_ops->releasedir)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dffs_ops->releasedir(req, inode, fi);

	return;
decref:
	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

/* Fuse wrapper for unlink, and rmdir */
static void
df_ll_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dffs_ops->unlink) {
		D_GOTO(decref, rc = ENOTSUP);
	}
	parent_inode->ie_dfs->dffs_ops->unlink(req, parent_inode, name);

	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

/*
 * Implement readdir without a opendir/closedir pair.  This works perfectly
 * well, but adding (open|close)dir would allow us to cache the inode_entry
 * between calls which would help performance, and may be necessary later on
 * to support directories which require multiple calls to readdir() to return
 * all entries.
 */
static void
df_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset,
	      struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dfpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dffs_ops->readdir) {
		D_GOTO(decref, rc = ENOTSUP);
	}
	inode->ie_dfs->dffs_ops->readdir(req, inode, size, offset, fi);

	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dfpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
dfuse_fuse_destroy(void *userdata)
{
	DFUSE_TRA_INFO(userdata, "destroy callback");
	DFUSE_TRA_DOWN(userdata);
	D_FREE(userdata);
}

/* dfuse ops that are used for accessing dfs mounts */
struct dfuse_inode_ops dfuse_dfs_ops = {
	.lookup		= dfuse_cb_lookup,
	.mkdir		= dfuse_cb_mkdir,
	.opendir	= dfuse_cb_opendir,
	.releasedir	= dfuse_cb_releasedir,
	.getattr	= dfuse_cb_getattr,
	.unlink		= dfuse_cb_unlink,
	.readdir	= dfuse_cb_readdir,
	.create		= dfuse_cb_create,
};

struct dfuse_inode_ops dfuse_cont_ops = {
	.lookup		= dfuse_cont_lookup,
	.mkdir		= dfuse_cont_mkdir,
};

struct dfuse_inode_ops dfuse_pool_ops = {
	.lookup		= dfuse_pool_lookup,
};

/* Return the ops that should be passed to fuse */
struct fuse_lowlevel_ops
*dfuse_get_fuse_ops()
{
	struct fuse_lowlevel_ops *fuse_ops;

	D_ALLOC_PTR(fuse_ops);
	if (!fuse_ops)
		return NULL;

	/* Ops that support per-inode indirection */
	fuse_ops->getattr	= df_ll_getattr;
	fuse_ops->lookup	= df_ll_lookup;
	fuse_ops->mkdir		= df_ll_mkdir;
	fuse_ops->opendir	= df_ll_opendir;
	fuse_ops->releasedir	= df_ll_releasedir;
	fuse_ops->unlink	= df_ll_unlink;
	fuse_ops->rmdir		= df_ll_unlink;
	fuse_ops->readdir	= df_ll_readdir;
	fuse_ops->create	= df_ll_create;

	/* Ops that do not need to support per-inode indirection */
	fuse_ops->init = dfuse_fuse_init;
	fuse_ops->forget = dfuse_cb_forget;
	fuse_ops->forget_multi = dfuse_cb_forget_multi;
	fuse_ops->destroy = dfuse_fuse_destroy;

	/* Ops that do not support per-inode indirection
	 *
	 * Avoid the extra level of indirection here, as only dfs allows
	 * creation of files, so it should be the only place to see file
	 * operations.
	 *
	 * TODO: Implement open/release callbacks, to allow setting of fi.fh
	 * in open, which would avoid a hashtable lookup per read/write.
	 *
	 * TODO: read_buf and write_buf support.
	 */
	fuse_ops->open		= dfuse_cb_open;
	fuse_ops->release	= dfuse_cb_release;
	fuse_ops->write		= dfuse_cb_write;
	fuse_ops->read		= dfuse_cb_read;

	return fuse_ops;
}
