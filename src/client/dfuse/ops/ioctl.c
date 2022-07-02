/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#include <sys/ioctl.h>

#include "dfuse_ioctl.h"

#define MAX_IOCTL_SIZE ((1024 * 16) - 1)

static void
handle_user_ioctl(struct dfuse_obj_hdl *oh, fuse_req_t req)
{
	struct dfuse_user_reply dur;

	dur.uid = getuid();
	dur.gid = getgid();

	DFUSE_REPLY_IOCTL(oh, req, dur);
}

static void
handle_il_ioctl(struct dfuse_obj_hdl *oh, fuse_req_t req)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	struct dfuse_il_reply         il_reply  = {0};
	int                           rc;
	uint32_t                      old_calls;

	rc = dfs_obj2id(oh->doh_ie->ie_obj, &il_reply.fir_oid);
	if (rc)
		D_GOTO(err, rc);

	il_reply.fir_version = DFUSE_IOCTL_VERSION;

	uuid_copy(il_reply.fir_pool, oh->doh_ie->ie_dfs->dfs_dfp->dfp_pool);
	uuid_copy(il_reply.fir_cont, oh->doh_ie->ie_dfs->dfs_cont);

	if (oh->doh_ie->ie_dfs->dfc_attr_timeout > 0)
		il_reply.fir_flags |= DFUSE_IOCTL_FLAGS_MCACHE;

	if (oh->doh_writeable) {
		rc = fuse_lowlevel_notify_inval_inode(fs_handle->dpi_info->di_session,
						      oh->doh_ie->ie_stat.st_ino, 0, 0);

		if (rc == 0) {
			DFUSE_TRA_DEBUG(oh, "inval inode %#lx rc is %d", oh->doh_ie->ie_stat.st_ino,
					rc);
		} else {
			DFUSE_TRA_ERROR(oh, "inval inode %#lx rc is %d", oh->doh_ie->ie_stat.st_ino,
					rc);
		}

		/* Mark this file handle as using the IL or similar, and if this is new then mark
		 * the inode as well
		 */
		old_calls = atomic_fetch_add_relaxed(&oh->doh_il_calls, 1);
		if (old_calls == 0)
			atomic_fetch_add_relaxed(&oh->doh_ie->ie_il_count, 1);
	}

	DFUSE_REPLY_IOCTL(oh, req, il_reply);
	return;
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

static void
handle_size_ioctl(struct dfuse_obj_hdl *oh, fuse_req_t req)
{
	struct dfuse_hs_reply	hs_reply = {0};
	d_iov_t			iov = {};
	int			rc;

	hs_reply.fsr_version = DFUSE_IOCTL_VERSION;

	rc = daos_pool_local2global(oh->doh_ie->ie_dfs->dfs_dfp->dfp_poh, &iov);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));

	hs_reply.fsr_pool_size = iov.iov_buf_len;

	rc = daos_cont_local2global(oh->doh_ie->ie_dfs->dfs_coh, &iov);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));

	hs_reply.fsr_cont_size = iov.iov_buf_len;
	if (hs_reply.fsr_cont_size > MAX_IOCTL_SIZE)
		D_GOTO(err, rc = EOVERFLOW);

	rc = dfs_local2global(oh->doh_ie->ie_dfs->dfs_ns, &iov);
	if (rc)
		D_GOTO(err, rc);

	hs_reply.fsr_dfs_size = iov.iov_buf_len;
	if (hs_reply.fsr_dfs_size > MAX_IOCTL_SIZE)
		D_GOTO(err, rc = EOVERFLOW);

	DFUSE_REPLY_IOCTL(oh, req, hs_reply);
	return;
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

static void
handle_poh_ioctl(struct dfuse_obj_hdl *oh, size_t size, fuse_req_t req)
{
	d_iov_t iov = {};
	int rc;

	iov.iov_buf_len = size;

	D_ALLOC(iov.iov_buf, size);
	if (iov.iov_buf == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = daos_pool_local2global(oh->doh_ie->ie_dfs->dfs_dfp->dfp_poh, &iov);
	if (rc)
		D_GOTO(free, rc = daos_der2errno(rc));

	if (iov.iov_len != iov.iov_buf_len)
		D_GOTO(free, rc = EAGAIN);

	DFUSE_REPLY_IOCTL_SIZE(oh, req, iov.iov_buf, iov.iov_len);
	D_FREE(iov.iov_buf);
	return;
free:
	D_FREE(iov.iov_buf);
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

#define POH_FILE_TEMPLATE "/tmp/dfuse_ilh_XXXXXX"

static void
handle_pfile_ioctl(struct dfuse_obj_hdl *oh, size_t size, fuse_req_t req)
{
	d_iov_t	iov = {};
	ssize_t	fsize;
	int	rc;
	int	fd;
	char	*fname = NULL;

	D_STRNDUP(fname, POH_FILE_TEMPLATE, sizeof(POH_FILE_TEMPLATE));
	if (fname == NULL)
		D_GOTO(err, rc = ENOMEM);

	/* Firstly sample the size */
	rc = daos_pool_local2global(oh->doh_ie->ie_dfs->dfs_dfp->dfp_poh, &iov);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));

	D_ALLOC(iov.iov_buf, iov.iov_buf_len);
	if (iov.iov_buf == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = daos_pool_local2global(oh->doh_ie->ie_dfs->dfs_dfp->dfp_poh, &iov);
	if (rc)
		D_GOTO(free, rc = daos_der2errno(rc));

	errno = 0;
	fd = mkstemp(fname);
	if (fd == -1)
		D_GOTO(free, rc = errno);

	fsize = write(fd, iov.iov_buf, iov.iov_len);
	close(fd);
	if (fsize != iov.iov_len)
		D_GOTO(free, rc = EIO);

	DFUSE_REPLY_IOCTL_SIZE(oh, req, fname, sizeof(POH_FILE_TEMPLATE));
	D_FREE(iov.iov_buf);
	D_FREE(fname);

	return;
free:
	D_FREE(iov.iov_buf);
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(fname);
}

static void
handle_coh_ioctl(struct dfuse_obj_hdl *oh, size_t size, fuse_req_t req)
{
	d_iov_t iov = {};
	int rc;

	iov.iov_buf_len = size;

	D_ALLOC(iov.iov_buf, size);
	if (iov.iov_buf == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = daos_cont_local2global(oh->doh_ie->ie_dfs->dfs_coh, &iov);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));

	if (iov.iov_len != iov.iov_buf_len)
		D_GOTO(free, rc = EAGAIN);

	DFUSE_REPLY_IOCTL_SIZE(oh, req, iov.iov_buf, iov.iov_len);
	D_FREE(iov.iov_buf);
	return;
free:
	D_FREE(iov.iov_buf);
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

static void
handle_dsize_ioctl(struct dfuse_obj_hdl *oh, fuse_req_t req)
{
	struct dfuse_hsd_reply	hsd_reply = {0};
	d_iov_t			iov = {};
	int			rc;

	/* Handle directory */
	hsd_reply.fsr_version = DFUSE_IOCTL_VERSION;

	rc = dfs_obj_local2global(oh->doh_ie->ie_dfs->dfs_ns, oh->doh_obj, &iov);
	if (rc)
		D_GOTO(err, rc);

	hsd_reply.fsr_dobj_size = iov.iov_buf_len;
	if (hsd_reply.fsr_dobj_size > MAX_IOCTL_SIZE)
		D_GOTO(err, rc = EOVERFLOW);

	DFUSE_REPLY_IOCTL(oh, req, hsd_reply);
	return;
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

static void
handle_doh_ioctl(struct dfuse_obj_hdl *oh, size_t size, fuse_req_t req)
{
	d_iov_t iov = {};
	int rc;

	iov.iov_buf_len = size;

	D_ALLOC(iov.iov_buf, size);
	if (iov.iov_buf == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_local2global(oh->doh_ie->ie_dfs->dfs_ns, &iov);
	if (rc)
		D_GOTO(err, rc);

	if (iov.iov_len != iov.iov_buf_len)
		D_GOTO(free, rc = EAGAIN);

	DFUSE_REPLY_IOCTL_SIZE(oh, req, iov.iov_buf, iov.iov_len);
	D_FREE(iov.iov_buf);
	return;
free:
	D_FREE(iov.iov_buf);
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

static void
handle_dooh_ioctl(struct dfuse_obj_hdl *oh, size_t size, fuse_req_t req)
{
	d_iov_t iov = {};
	int rc;

	iov.iov_buf_len = size;

	D_ALLOC(iov.iov_buf, size);
	if (iov.iov_buf == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_obj_local2global(oh->doh_ie->ie_dfs->dfs_ns, oh->doh_obj, &iov);
	if (rc)
		D_GOTO(err, rc);

	if (iov.iov_len != iov.iov_buf_len)
		D_GOTO(free, rc = EAGAIN);

	DFUSE_REPLY_IOCTL_SIZE(oh, req, iov.iov_buf, iov.iov_len);
	D_FREE(iov.iov_buf);
	return;
free:
	D_FREE(iov.iov_buf);
err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}

#ifdef FUSE_IOCTL_USE_INT
void dfuse_cb_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void *arg,
		    struct fuse_file_info *fi, unsigned int flags,
		    const void *in_buf, size_t in_bufsz, size_t out_bufsz)
#else
void dfuse_cb_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void *arg,
		    struct fuse_file_info *fi, unsigned int flags,
		    const void *in_buf, size_t in_bufsz, size_t out_bufsz)
#endif
{
	struct dfuse_obj_hdl	*oh = (struct dfuse_obj_hdl *)fi->fh;
	int			rc;
	const struct fuse_ctx	*fc;
	uid_t			uid;
	gid_t			gid;

	if (cmd == TCGETS) {
		DFUSE_TRA_DEBUG(oh, "Ignoring TCGETS ioctl");
		D_GOTO(out_err, rc = ENOTTY);
	}

	if (cmd == TIOCGPGRP) {
		DFUSE_TRA_DEBUG(oh, "Ignoring TIOCGPGRP ioctl");
		D_GOTO(out_err, rc = ENOTTY);
	}

	/* Check the IOCTl type is correct */
	if (_IOC_TYPE(cmd) != DFUSE_IOCTL_TYPE) {
		DFUSE_TRA_INFO(oh, "Real ioctl support is not implemented cmd=%#x", cmd);
		D_GOTO(out_err, rc = ENOTSUP);
	}

	DFUSE_TRA_DEBUG(oh, "ioctl cmd=%#x", cmd);

	if (cmd == DFUSE_IOCTL_IL) {
		if (out_bufsz < sizeof(struct dfuse_il_reply))
			D_GOTO(out_err, rc = EIO);

		handle_il_ioctl(oh, req);
		return;
	}

	if (cmd == DFUSE_IOCTL_DFUSE_USER) {
		if (out_bufsz < sizeof(struct dfuse_user_reply))
			D_GOTO(out_err, rc = EIO);
		handle_user_ioctl(oh, req);
		return;
	}

	/* The dfs handles are OK to pass across security domains because you
	 * need the correct container handle to be able to use them.
	 */
	if (cmd == DFUSE_IOCTL_IL_DSIZE) {
		if (S_ISDIR(oh->doh_ie->ie_stat.st_mode))
			D_GOTO(out_err, rc = EISDIR);

		if (out_bufsz < sizeof(struct dfuse_hsd_reply))
			D_GOTO(out_err, rc = EIO);
		handle_dsize_ioctl(oh, req);

		return;

	} else if (_IOC_NR(cmd) == DFUSE_IOCTL_REPLY_DOH) {
		size_t size = _IOC_SIZE(cmd);

		handle_doh_ioctl(oh, size, req);

		return;
	} else if (_IOC_NR(cmd) == DFUSE_IOCTL_REPLY_DOOH) {
		size_t size = _IOC_SIZE(cmd);

		handle_dooh_ioctl(oh, size, req);
		return;
	}

	fc = fuse_req_ctx(req);
	uid = getuid();
	gid = getgid();

	if (fc->uid != uid || fc->gid != gid)
		D_GOTO(out_err, rc = EPERM);

	DFUSE_TRA_DEBUG(oh, "trusted pid %d", fc->pid);

	if (cmd == DFUSE_IOCTL_IL_SIZE) {
		if (out_bufsz < sizeof(struct dfuse_hs_reply))
			D_GOTO(out_err, rc = EIO);
		handle_size_ioctl(oh, req);
	} else if (_IOC_NR(cmd) == DFUSE_IOCTL_REPLY_POH) {
		size_t size = _IOC_SIZE(cmd);

		handle_poh_ioctl(oh, size, req);
	} else if (_IOC_NR(cmd) == DFUSE_IOCTL_REPLY_COH) {
		size_t size = _IOC_SIZE(cmd);

		handle_coh_ioctl(oh, size, req);
	} else if (_IOC_NR(cmd) == DFUSE_IOCTL_REPLY_PFILE) {
		size_t size = _IOC_SIZE(cmd);

		handle_pfile_ioctl(oh, size, req);
	} else {
		DFUSE_TRA_WARNING(oh, "Unknown IOCTL type %#x", cmd);
		D_GOTO(out_err, rc = EIO);
	}

	return;

out_err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}
