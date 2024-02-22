/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <fuse3/fuse_lowlevel.h>

#include "dfuse_common.h"
#include "dfuse.h"

#define SHOW_FLAG(HANDLE, CAP, WANT, FLAG)                                                         \
	do {                                                                                       \
		DFUSE_TRA_INFO(HANDLE, "%s %s " #FLAG "",                                          \
			       (CAP & FLAG) ? "available" : "         ",                           \
			       (WANT & FLAG) ? "enabled" : "       ");                             \
		CAP &= ~FLAG;                                                                      \
		WANT &= ~FLAG;                                                                     \
	} while (0)

static void
dfuse_show_flags(void *handle, unsigned int cap, unsigned int want)
{
	DFUSE_TRA_INFO(handle, "Capability supported by kernel %#x", cap);

	DFUSE_TRA_INFO(handle, "Capability requested %#x", want);

	SHOW_FLAG(handle, cap, want, FUSE_CAP_ASYNC_READ);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_POSIX_LOCKS);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_ATOMIC_O_TRUNC);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_EXPORT_SUPPORT);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_DONT_MASK);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_SPLICE_WRITE);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_SPLICE_MOVE);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_SPLICE_READ);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_FLOCK_LOCKS);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_IOCTL_DIR);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_AUTO_INVAL_DATA);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_READDIRPLUS);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_READDIRPLUS_AUTO);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_ASYNC_DIO);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_WRITEBACK_CACHE);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_NO_OPEN_SUPPORT);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_PARALLEL_DIROPS);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_POSIX_ACL);
	SHOW_FLAG(handle, cap, want, FUSE_CAP_HANDLE_KILLPRIV);

#ifdef FUSE_CAP_CACHE_SYMLINKS
	SHOW_FLAG(handle, cap, want, FUSE_CAP_CACHE_SYMLINKS);
#endif
#ifdef FUSE_CAP_NO_OPENDIR_SUPPORT
	SHOW_FLAG(handle, cap, want, FUSE_CAP_NO_OPENDIR_SUPPORT);
#endif
#ifdef FUSE_CAP_EXPLICIT_INVAL_DATA
	SHOW_FLAG(handle, cap, want, FUSE_CAP_EXPLICIT_INVAL_DATA);
#endif

	if (cap)
		DFUSE_TRA_WARNING(handle, "Unknown capability flags %#x", cap);

	if (want)
		DFUSE_TRA_WARNING(handle, "Unknown requested flags %#x", want);
}

/* Called on filesystem init.  It has the ability to both observe configuration
 * options, but also to modify them.  As we do not use the FUSE command line
 * parsing this is where we apply tunables.
 */
static void
dfuse_fuse_init(void *arg, struct fuse_conn_info *conn)
{
	struct dfuse_info *dfuse_info = arg;

	DFUSE_TRA_INFO(dfuse_info, "Fuse configuration");

	DFUSE_TRA_INFO(dfuse_info, "Proto %d %d", conn->proto_major, conn->proto_minor);

	/* These are requests dfuse makes to the kernel, but are then capped by the kernel itself,
	 * for max_read zero means "as large as possible" which is what we want, but then dfuse
	 * does not know how large to pre-allocate any buffers.
	 */
	DFUSE_TRA_INFO(dfuse_info, "max read %#x", conn->max_read);
	DFUSE_TRA_INFO(dfuse_info, "max write %#x", conn->max_write);
	DFUSE_TRA_INFO(dfuse_info, "readahead %#x", conn->max_readahead);

	if (conn->capable & FUSE_CAP_PARALLEL_DIROPS)
		conn->want |= FUSE_CAP_PARALLEL_DIROPS;

	DFUSE_TRA_INFO(dfuse_info, "kernel readdir cache support compiled in");

	conn->want |= FUSE_CAP_READDIRPLUS;
	conn->want |= FUSE_CAP_READDIRPLUS_AUTO;

	conn->time_gran = 1;

	if (dfuse_info->di_wb_cache)
		conn->want |= FUSE_CAP_WRITEBACK_CACHE;

#ifdef FUSE_CAP_CACHE_SYMLINKS
	conn->want |= FUSE_CAP_CACHE_SYMLINKS;
#endif
	dfuse_show_flags(dfuse_info, conn->capable, conn->want);

	conn->max_background       = 16;
	conn->congestion_threshold = 8;

	DFUSE_TRA_INFO(dfuse_info, "max_background %d", conn->max_background);
	DFUSE_TRA_INFO(dfuse_info, "congestion_threshold %d", conn->congestion_threshold);
}

void
df_ll_create(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode,
	     struct fuse_file_info *fi)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *parent_inode;
	int                       rc;

	parent_inode = dfuse_inode_lookup_nf(dfuse_info, parent);

	if (!parent_inode->ie_dfs->dfs_ops->create)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(parent_inode, DS_CREATE);

	parent_inode->ie_dfs->dfs_ops->create(req, parent_inode, name, mode, fi);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

void
df_ll_mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *parent_inode;
	int                       rc;

	parent_inode = dfuse_inode_lookup_nf(dfuse_info, parent);

	if (!parent_inode->ie_dfs->dfs_ops->mknod)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(parent_inode, DS_MKNOD);

	parent_inode->ie_dfs->dfs_ops->mknod(req, parent_inode, name, mode);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

void
df_ll_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_obj_hdl     *handle     = NULL;
	struct dfuse_inode_entry *inode;

	if (fi)
		handle = (void *)fi->fh;

	if (handle) {
		inode                   = handle->doh_ie;
		handle->doh_linear_read = false;
		DFUSE_IE_STAT_ADD(inode, DS_FGETATTR);
	} else {
		inode = dfuse_inode_lookup_nf(dfuse_info, ino);
		DFUSE_IE_STAT_ADD(inode, DS_GETATTR);
	}

	if (inode->ie_dfs->dfc_attr_timeout &&
	    (atomic_load_relaxed(&inode->ie_open_write_count) == 0) &&
	    (atomic_load_relaxed(&inode->ie_il_count) == 0)) {
		double timeout;

		if (dfuse_mcache_get_valid(inode, inode->ie_dfs->dfc_attr_timeout, &timeout)) {
			DFUSE_REPLY_ATTR_FORCE(inode, req, timeout);
			return;
		}
	}

	if (inode->ie_dfs->dfs_ops->getattr)
		inode->ie_dfs->dfs_ops->getattr(req, inode);
	else
		DFUSE_REPLY_ATTR(inode, req, &inode->ie_stat);
}

void
df_ll_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set,
	      struct fuse_file_info *fi)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_obj_hdl     *handle     = NULL;
	struct dfuse_inode_entry *inode;
	int                       rc;

	if (fi)
		handle = (void *)fi->fh;

	if (handle) {
		inode                   = handle->doh_ie;
		handle->doh_linear_read = false;
		DFUSE_IE_STAT_ADD(inode, DS_FSETATTR);

	} else {
		inode = dfuse_inode_lookup_nf(dfuse_info, ino);
		DFUSE_IE_STAT_ADD(inode, DS_SETATTR);
	}

	if (inode->ie_dfs->dfs_ops->setattr)
		inode->ie_dfs->dfs_ops->setattr(req, inode, attr, to_set);
	else
		D_GOTO(err, rc = ENOTSUP);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

static void
df_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *parent_inode;

	parent_inode = dfuse_inode_lookup_nf(dfuse_info, parent);

	DFUSE_IE_STAT_ADD(parent_inode, DS_LOOKUP);

	parent_inode->ie_dfs->dfs_ops->lookup(req, parent_inode, name);
}

static void
df_ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode)
{
	struct dfuse_info        *dfuse_info   = fuse_req_userdata(req);
	struct dfuse_inode_entry *parent_inode = NULL;
	int                       rc;

	parent_inode = dfuse_inode_lookup_nf(dfuse_info, parent);

	if (!parent_inode->ie_dfs->dfs_ops->mknod)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(parent_inode, DS_MKDIR);

	parent_inode->ie_dfs->dfs_ops->mknod(req, parent_inode, name, mode | S_IFDIR);

	return;
err:
	DFUSE_REPLY_ERR_RAW(parent_inode, req, rc);
}

static void
df_ll_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	int                       rc;

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	if (!inode->ie_dfs->dfs_ops->opendir)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(inode, DS_OPENDIR);

	inode->ie_dfs->dfs_ops->opendir(req, inode, fi);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

static void
df_ll_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	int                       rc;

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	if (!inode->ie_dfs->dfs_ops->releasedir)
		D_GOTO(err, rc = ENOTSUP);

	inode->ie_dfs->dfs_ops->releasedir(req, inode, fi);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

/* Fuse wrapper for unlink, and rmdir */
static void
df_ll_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *parent_inode;
	int                       rc;

	parent_inode = dfuse_inode_lookup_nf(dfuse_info, parent);

	if (!parent_inode->ie_dfs->dfs_ops->unlink)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(parent_inode, DS_UNLINK);

	parent_inode->ie_dfs->dfs_ops->unlink(req, parent_inode, name);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

/* Handle readdir and readdirplus slightly differently, the presence of the
 * opendir callback will mean fi->fh is set for dfs files but not containers
 * or pools to use this fact to avoid a hash table lookup on the inode.
 */
static void
df_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh = (struct dfuse_obj_hdl *)fi->fh;

	if (oh == NULL) {
		struct dfuse_info *dfuse_info = fuse_req_userdata(req);

		DFUSE_REPLY_ERR_RAW(dfuse_info, req, ENOTSUP);
		return;
	}

	DFUSE_IE_STAT_ADD(oh->doh_ie, DS_READDIR);

	dfuse_cb_readdir(req, oh, size, offset, false);
}

static void
df_ll_readdirplus(fuse_req_t req, fuse_ino_t ino, size_t size, off_t offset,
		  struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl *oh = (struct dfuse_obj_hdl *)fi->fh;

	if (oh == NULL) {
		struct dfuse_info *dfuse_info = fuse_req_userdata(req);

		DFUSE_REPLY_ERR_RAW(dfuse_info, req, ENOTSUP);
		return;
	}

	DFUSE_IE_STAT_ADD(oh->doh_ie, DS_READDIR);

	dfuse_cb_readdir(req, oh, size, offset, true);
}

void
df_ll_symlink(fuse_req_t req, const char *link, fuse_ino_t parent, const char *name)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *parent_inode;
	int                       rc;

	parent_inode = dfuse_inode_lookup_nf(dfuse_info, parent);

	if (!parent_inode->ie_dfs->dfs_ops->symlink)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(parent_inode, DS_SYMLINK);

	parent_inode->ie_dfs->dfs_ops->symlink(req, link, parent_inode, name);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

/* Do not allow security xattrs to be set or read, see DAOS-14639 */
#define XATTR_SEC   "security."
/* Do not allow either system.posix_acl_default or system.posix_acl_access */
#define XATTR_P_ACL "system.posix_acl"

void
df_ll_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name, const char *value, size_t size,
	       int flags)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	int                       rc;

	/* Don't allow setting of uid/gid extended attribute */
	if (strncmp(name, DFUSE_XATTR_PREFIX, sizeof(DFUSE_XATTR_PREFIX) - 1) == 0) {
		D_GOTO(err, rc = EPERM);
	}

	if (strncmp(name, XATTR_SEC, sizeof(XATTR_SEC) - 1) == 0)
		D_GOTO(err, rc = ENOTSUP);

	if (strncmp(name, XATTR_P_ACL, sizeof(XATTR_P_ACL) - 1) == 0)
		D_GOTO(err, rc = ENOTSUP);

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	if (!inode->ie_dfs->dfs_ops->setxattr)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(inode, DS_SETXATTR);

	inode->ie_dfs->dfs_ops->setxattr(req, inode, name, value, size, flags);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

void
df_ll_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	int                       rc;

	if (strncmp(name, XATTR_SEC, sizeof(XATTR_SEC) - 1) == 0)
		D_GOTO(err, rc = ENODATA);

	if (strncmp(name, XATTR_P_ACL, sizeof(XATTR_P_ACL) - 1) == 0)
		D_GOTO(err, rc = ENODATA);

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	if (!inode->ie_dfs->dfs_ops->getxattr)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(inode, DS_GETXATTR);

	inode->ie_dfs->dfs_ops->getxattr(req, inode, name, size);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

void
df_ll_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	int                       rc;

	/* Don't allow removing of dfuse extended attribute.  This will return regardless of it the
	 * attribute exists or not, but the alternative is a round-trip to check, so this seems like
	 * the best option here.
	 */
	if (strncmp(name, DFUSE_XATTR_PREFIX, sizeof(DFUSE_XATTR_PREFIX) - 1) == 0) {
		D_GOTO(err, rc = EPERM);
	}

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	if (!inode->ie_dfs->dfs_ops->removexattr)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(inode, DS_RMXATTR);

	inode->ie_dfs->dfs_ops->removexattr(req, inode, name);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

void
df_ll_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	int                       rc;

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	if (!inode->ie_dfs->dfs_ops->listxattr)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(inode, DS_LISTXATTR);

	inode->ie_dfs->dfs_ops->listxattr(req, inode, size);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

static void
df_ll_rename(fuse_req_t req, fuse_ino_t parent, const char *name, fuse_ino_t newparent,
	     const char *newname, unsigned int flags)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *parent_inode;
	struct dfuse_inode_entry *newparent_inode = NULL;
	int                       rc;

	parent_inode = dfuse_inode_lookup_nf(dfuse_info, parent);

	DFUSE_IE_STAT_ADD(parent_inode, DS_RENAME);

	if (!parent_inode->ie_dfs->dfs_ops->rename)
		D_GOTO(err, rc = EXDEV);

	if (parent != newparent) {
		newparent_inode = dfuse_inode_lookup_nf(dfuse_info, newparent);

		if (parent_inode->ie_dfs != newparent_inode->ie_dfs)
			D_GOTO(err, rc = EXDEV);
	}

	parent_inode->ie_dfs->dfs_ops->rename(req, parent_inode, name, newparent_inode, newname,
					      flags);
	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

static void
df_ll_statfs(fuse_req_t req, fuse_ino_t ino)
{
	struct dfuse_info        *dfuse_info = fuse_req_userdata(req);
	struct dfuse_inode_entry *inode;
	int                       rc;

	inode = dfuse_inode_lookup_nf(dfuse_info, ino);

	if (!inode->ie_dfs->dfs_ops->statfs)
		D_GOTO(err, rc = ENOTSUP);

	DFUSE_IE_STAT_ADD(inode, DS_STATFS);

	inode->ie_dfs->dfs_ops->statfs(req, inode);

	return;
err:
	DFUSE_REPLY_ERR_RAW(dfuse_info, req, rc);
}

/* dfuse ops that are used for accessing dfs mounts */
struct dfuse_inode_ops dfuse_dfs_ops = {
    .lookup      = dfuse_cb_lookup,
    .mknod       = dfuse_cb_mknod,
    .opendir     = dfuse_cb_opendir,
    .releasedir  = dfuse_cb_releasedir,
    .getattr     = dfuse_cb_getattr,
    .unlink      = dfuse_cb_unlink,
    .create      = dfuse_cb_create,
    .rename      = dfuse_cb_rename,
    .symlink     = dfuse_cb_symlink,
    .setxattr    = dfuse_cb_setxattr,
    .getxattr    = dfuse_cb_getxattr,
    .listxattr   = dfuse_cb_listxattr,
    .removexattr = dfuse_cb_removexattr,
    .setattr     = dfuse_cb_setattr,
    .statfs      = dfuse_cb_statfs,
};

struct dfuse_inode_ops dfuse_cont_ops = {
    .lookup = dfuse_cont_lookup,
    .statfs = dfuse_cb_statfs,
};

struct dfuse_inode_ops dfuse_pool_ops = {
    .lookup = dfuse_pool_lookup,
    .statfs = dfuse_cb_statfs,
};

struct fuse_lowlevel_ops dfuse_ops = {
    /* Ops that support per-inode indirection */
    .getattr     = df_ll_getattr,
    .lookup      = df_ll_lookup,
    .mkdir       = df_ll_mkdir,
    .opendir     = df_ll_opendir,
    .releasedir  = df_ll_releasedir,
    .unlink      = df_ll_unlink,
    .rmdir       = df_ll_unlink,
    .readdir     = df_ll_readdir,
    .readdirplus = df_ll_readdirplus,
    .create      = df_ll_create,
    .mknod       = df_ll_mknod,
    .rename      = df_ll_rename,
    .symlink     = df_ll_symlink,
    .setxattr    = df_ll_setxattr,
    .getxattr    = df_ll_getxattr,
    .listxattr   = df_ll_listxattr,
    .removexattr = df_ll_removexattr,
    .setattr     = df_ll_setattr,
    .statfs      = df_ll_statfs,

    /* Ops that do not need to support per-inode indirection */
    .init         = dfuse_fuse_init,
    .forget       = dfuse_cb_forget,
    .forget_multi = dfuse_cb_forget_multi,

    /* Ops that do not support per-inode indirection
     *
     * Avoid the extra level of indirection here, as only dfs allows
     * creation of files, so it should be the only place to see file
     * operations.
     *
     */
    .open      = dfuse_cb_open,
    .release   = dfuse_cb_release,
    .write_buf = dfuse_cb_write,
    .read      = dfuse_cb_read,
    .readlink  = dfuse_cb_readlink,
    .ioctl     = dfuse_cb_ioctl,
};
