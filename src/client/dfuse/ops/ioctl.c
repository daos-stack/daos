/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#include <sys/ioctl.h>

#include "dfuse_ioctl.h"

#define MAX_IOCTL_SIZE ((1024 * 16) - 1)

static void
handle_il_ioctl(struct dfuse_obj_hdl *oh, fuse_req_t req)
{
	struct dfuse_il_reply	il_reply = {0};
	int			rc;

	DFUSE_TRA_INFO(oh, "Requested");

	rc = dfs_obj2id(oh->doh_ie->ie_obj, &il_reply.fir_oid);
	if (rc)
		D_GOTO(err, rc);

	il_reply.fir_version = DFUSE_IOCTL_VERSION;

	uuid_copy(il_reply.fir_pool, oh->doh_ie->ie_dfs->dfs_dfp->dfp_pool);
	uuid_copy(il_reply.fir_cont, oh->doh_ie->ie_dfs->dfs_cont);

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

	DFUSE_TRA_INFO(oh, "Requested");

	hs_reply.fsr_version = DFUSE_IOCTL_VERSION;

	rc = daos_pool_local2global(oh->doh_ie->ie_dfs->dfs_dfp->dfp_poh, &iov);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));

	hs_reply.fsr_pool_size = iov.iov_buf_len;
	if (hs_reply.fsr_pool_size > MAX_IOCTL_SIZE)
		D_GOTO(err, rc = EOVERFLOW);

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

	rc = daos_pool_local2global(oh->doh_ie->ie_dfs->dfs_dfp->dfp_poh,
				    &iov);
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

	DFUSE_TRA_INFO(oh, "Requested");

	hsd_reply.fsr_version = DFUSE_IOCTL_VERSION;

	rc = dfs_obj_local2global(oh->doh_ie->ie_dfs->dfs_ns, oh->doh_obj,
				  &iov);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));

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
handle_dooh_ioctl(struct dfuse_obj_hdl *oh, size_t size, fuse_req_t req)
{
	d_iov_t iov = {};
	int rc;

	iov.iov_buf_len = size;

	D_ALLOC(iov.iov_buf, size);
	if (iov.iov_buf == NULL)
		D_GOTO(err, rc = ENOMEM);

	rc = dfs_obj_local2global(oh->doh_ie->ie_dfs->dfs_ns, oh->doh_obj,
				  &iov);
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

	DFUSE_TRA_INFO(oh, "ioctl cmd=%#x", cmd);

	if (cmd == TCGETS) {
		DFUSE_TRA_DEBUG(oh, "Ignoring TCGETS ioctl");
		D_GOTO(out_err, rc = ENOTTY);
	}

	/* Check the IOCTl type is correct */
	if (_IOC_TYPE(cmd) != DFUSE_IOCTL_TYPE) {
		DFUSE_TRA_INFO(oh, "Real ioctl support is not implemented");
		D_GOTO(out_err, rc = ENOTSUP);
	}

	if (cmd == DFUSE_IOCTL_IL) {
		if (out_bufsz < sizeof(struct dfuse_il_reply))
			D_GOTO(out_err, rc = EIO);

		handle_il_ioctl(oh, req);
		return;
	}

	/* The dfs handles are OK to pass across security domains because you
	 * need the correct container handle to be able to use them.
	 */
	if (cmd == DFUSE_IOCTL_IL_DSIZE) {
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

	DFUSE_TRA_INFO(oh, "trusted pid %d", fc->pid);

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

	} else {
		DFUSE_TRA_WARNING(oh, "Unknown IOCTL type %#x", cmd);
		D_GOTO(out_err, rc = EIO);
	}

	return;

out_err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}
