/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

#ifdef FUSE_CAP_CACHE_SYMLINKS
	SHOW_FLAG(handle, in, FUSE_CAP_CACHE_SYMLINKS);
#endif
#ifdef FUSE_CAP_NO_OPENDIR_SUPPORT
	SHOW_FLAG(handle, in, FUSE_CAP_NO_OPENDIR_SUPPORT);
#endif
#ifdef FUSE_CAP_EXPLICIT_INVAL_DATA
	SHOW_FLAG(handle, in, FUSE_CAP_EXPLICIT_INVAL_DATA);
#endif

	if (in)
		DFUSE_TRA_WARNING(handle, "Unknown flags %#x", in);
}

/* Called on filesystem init.  It has the ability to both observe configuration
 * options, but also to modify them.  As we do not use the FUSE command line
 * parsing this is where we apply tunables.
 */
static void
dfuse_fuse_init(void *arg, struct fuse_conn_info *conn)
{
	struct dfuse_projection_info *fs_handle = arg;

	DFUSE_TRA_INFO(fs_handle, "Fuse configuration");

	DFUSE_TRA_INFO(fs_handle, "Proto %d %d", conn->proto_major,
		       conn->proto_minor);

	DFUSE_TRA_INFO(fs_handle, "max read %#x", conn->max_read);
	DFUSE_TRA_INFO(fs_handle, "max write %#x", conn->max_write);
	DFUSE_TRA_INFO(fs_handle, "readahead %#x", conn->max_readahead);

	DFUSE_TRA_INFO(fs_handle, "Capability supported by kernel %#x",
		       conn->capable);

	dfuse_show_flags(fs_handle, conn->capable);

	DFUSE_TRA_INFO(fs_handle, "Capability requested %#x", conn->want);

	conn->want |= FUSE_CAP_READDIRPLUS;
	conn->want |= FUSE_CAP_READDIRPLUS_AUTO;

	conn->time_gran = 1;

	if (fs_handle->dpi_info->di_wb_cache)
		conn->want |= FUSE_CAP_WRITEBACK_CACHE;

	dfuse_show_flags(fs_handle, conn->want);

	conn->max_background = 16;
	conn->congestion_threshold = 8;

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
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dfs_ops->create)
		D_GOTO(err, rc = ENOTSUP);

	parent_inode->ie_dfs->dfs_ops->create(req, parent_inode, name, mode,
					      fi);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_mknod(fuse_req_t req, fuse_ino_t parent, const char *name,
	    mode_t mode, dev_t rdev)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %lu",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dfs_ops->mknod)
		D_GOTO(err, rc = ENOTSUP);

	parent_inode->ie_dfs->dfs_ops->mknod(req, parent_inode, name, mode);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_obj_hdl		*handle = NULL;
	struct dfuse_inode_entry	*inode = NULL;
	d_list_t			*rlink = NULL;
	int rc;

	if (fi)
		handle = (void *)fi->fh;

	if (handle) {
		inode = handle->doh_ie;
	} else {
		rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino,
					sizeof(ino));
		if (!rlink) {
			DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx",
					ino);
			D_GOTO(err, rc = ENOENT);
		}
		inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	}

	if (inode->ie_dfs->dfs_ops->getattr)
		inode->ie_dfs->dfs_ops->getattr(req, inode);
	else
		DFUSE_REPLY_ATTR(inode, req, &inode->ie_stat);

	if (rlink)
		d_hash_rec_decref(&fs_handle->dpi_iet, rlink);

	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
	      int to_set, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_obj_hdl		*handle = NULL;
	struct dfuse_inode_entry	*inode = NULL;
	d_list_t			*rlink = NULL;
	int rc;

	if (fi)
		handle = (void *)fi->fh;

	if (handle) {
		inode = handle->doh_ie;
	} else {
		rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
		if (!rlink) {
			DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx",
					ino);
			D_GOTO(out, rc = ENOENT);
		}

		inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);
	}

	if (inode->ie_dfs->dfs_ops->setattr)
		inode->ie_dfs->dfs_ops->setattr(req, inode, attr, to_set);
	else
		D_GOTO(out, rc = ENOTSUP);

	if (rlink)
		d_hash_rec_decref(&fs_handle->dpi_iet, rlink);

	return;
out:
	if (rlink)
		d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode;
	d_list_t			*rlink;
	int rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	parent_inode->ie_dfs->dfs_ops->lookup(req, parent_inode, name);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode = NULL;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dfs_ops->mknod)
		D_GOTO(decref, rc = ENOTSUP);

	parent_inode->ie_dfs->dfs_ops->mknod(req, parent_inode, name,
					     mode | S_IFDIR);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(parent_inode, req, rc);
}

static void
df_ll_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->opendir)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->opendir(req, inode, fi);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
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

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->releasedir)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->releasedir(req, inode, fi);
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);

	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
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

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx",
				parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dfs_ops->unlink)
		D_GOTO(decref, rc = ENOTSUP);

	parent_inode->ie_dfs->dfs_ops->unlink(req, parent_inode, name);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

/* Handle readdir and readdirplus slightly differently, the presence of the
 * opendir callback will mean fi->fh is set for dfs files but not containers
 * or pools to use this fact to avoid a hash table lookup on the inode.
 */
static void
df_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset,
	      struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;

	if (oh == NULL) {
		DFUSE_REPLY_ERR_RAW(fs_handle, req, ENOTSUP);
		return;
	}

	dfuse_cb_readdir(req, oh, size, offset, false);
}

static void
df_ll_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset,
		  struct fuse_file_info *fi)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_obj_hdl		*oh = (struct dfuse_obj_hdl *)fi->fh;

	if (oh == NULL) {
		DFUSE_REPLY_ERR_RAW(fs_handle, req, ENOTSUP);
		return;
	}

	dfuse_cb_readdir(req, oh, size, offset, true);
}

void
df_ll_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
	      const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", parent);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->symlink)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->symlink(req, link, inode, name);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
	       const char *value, size_t size, int flags)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	/* Don't allow setting of uid/gid extended attribute */
	if (strncmp(name, DFUSE_XATTR_PREFIX,
		    sizeof(DFUSE_XATTR_PREFIX) - 1) == 0) {
		D_GOTO(err, rc = EPERM);
	}

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->setxattr)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->setxattr(req, inode, name, value, size, flags);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->getxattr)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->getxattr(req, inode, name, size);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	/* Don't allow removing of dfuse extended attribute.  This will return
	 * regardless of it the attribute exists or not, but the alternative
	 * is a round-trip to check, so this seems like the best option here.
	 */
	if (strncmp(name, DFUSE_XATTR_PREFIX,
		    sizeof(DFUSE_XATTR_PREFIX) - 1) == 0) {
		D_GOTO(err, rc = EPERM);
	}

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->removexattr)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->removexattr(req, inode, name);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

void
df_ll_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->listxattr)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->listxattr(req, inode, size);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
	     fuse_ino_t newparent, const char *newname, unsigned int flags)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*parent_inode;
	struct dfuse_inode_entry	*newparent_inode = NULL;
	d_list_t			*rlink;
	d_list_t			*rlink2;
	int rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &parent, sizeof(parent));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", parent);
		D_GOTO(err, rc = ENOENT);
	}

	parent_inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!parent_inode->ie_dfs->dfs_ops->rename)
		D_GOTO(decref, rc = EXDEV);

	if (parent != newparent) {
		rlink2 = d_hash_rec_find(&fs_handle->dpi_iet, &newparent,
					 sizeof(newparent));
		if (!rlink2) {
			DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx",
					newparent);
			D_GOTO(decref, rc = ENOENT);
		}

		newparent_inode = container_of(rlink2, struct dfuse_inode_entry,
					       ie_htl);

		if (parent_inode->ie_dfs != newparent_inode->ie_dfs)
			D_GOTO(decref_both, rc = EXDEV);
	}

	parent_inode->ie_dfs->dfs_ops->rename(req, parent_inode, name,
					      newparent_inode, newname, flags);
	if (newparent_inode)
		d_hash_rec_decref(&fs_handle->dpi_iet, rlink2);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref_both:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink2);
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

static void
df_ll_statfs(fuse_req_t req, fuse_ino_t ino)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*inode;
	d_list_t			*rlink;
	int				rc;

	rlink = d_hash_rec_find(&fs_handle->dpi_iet, &ino, sizeof(ino));
	if (!rlink) {
		DFUSE_TRA_ERROR(fs_handle, "Failed to find inode %#lx", ino);
		D_GOTO(err, rc = ENOENT);
	}

	inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	if (!inode->ie_dfs->dfs_ops->statfs)
		D_GOTO(decref, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->statfs(req, inode);

	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
	return;
decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, rlink);
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}

/* dfuse ops that are used for accessing dfs mounts */
struct dfuse_inode_ops dfuse_dfs_ops = {
	.lookup		= dfuse_cb_lookup,
	.mknod		= dfuse_cb_mknod,
	.opendir	= dfuse_cb_opendir,
	.releasedir	= dfuse_cb_releasedir,
	.getattr	= dfuse_cb_getattr,
	.unlink		= dfuse_cb_unlink,
	.create		= dfuse_cb_create,
	.rename		= dfuse_cb_rename,
	.symlink	= dfuse_cb_symlink,
	.setxattr	= dfuse_cb_setxattr,
	.getxattr	= dfuse_cb_getxattr,
	.listxattr	= dfuse_cb_listxattr,
	.removexattr	= dfuse_cb_removexattr,
	.setattr	= dfuse_cb_setattr,
	.statfs		= dfuse_cb_statfs,
};

struct dfuse_inode_ops dfuse_cont_ops = {
	.lookup		= dfuse_cont_lookup,
	.statfs		= dfuse_cb_statfs,
};

struct dfuse_inode_ops dfuse_pool_ops = {
	.lookup		= dfuse_pool_lookup,
	.statfs		= dfuse_cb_statfs,
};

struct fuse_lowlevel_ops dfuse_ops = {
	/* Ops that support per-inode indirection */
	.getattr	= df_ll_getattr,
	.lookup		= df_ll_lookup,
	.mkdir		= df_ll_mkdir,
	.opendir	= df_ll_opendir,
	.releasedir	= df_ll_releasedir,
	.unlink		= df_ll_unlink,
	.rmdir		= df_ll_unlink,
	.readdir	= df_ll_readdir,
	.readdirplus	= df_ll_readdirplus,
	.create		= df_ll_create,
	.mknod		= df_ll_mknod,
	.rename		= df_ll_rename,
	.symlink	= df_ll_symlink,
	.setxattr	= df_ll_setxattr,
	.getxattr	= df_ll_getxattr,
	.listxattr	= df_ll_listxattr,
	.removexattr	= df_ll_removexattr,
	.setattr	= df_ll_setattr,
	.statfs		= df_ll_statfs,

	/* Ops that do not need to support per-inode indirection */
	.init		 = dfuse_fuse_init,
	.forget		 = dfuse_cb_forget,
	.forget_multi	 = dfuse_cb_forget_multi,

	/* Ops that do not support per-inode indirection
	 *
	 * Avoid the extra level of indirection here, as only dfs allows
	 * creation of files, so it should be the only place to see file
	 * operations.
	 *
	 */
	.open		= dfuse_cb_open,
	.release	= dfuse_cb_release,
	.write_buf	= dfuse_cb_write,
	.read		= dfuse_cb_read,
	.readlink	= dfuse_cb_readlink,
	.ioctl		= dfuse_cb_ioctl,
};
