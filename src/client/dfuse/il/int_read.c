/**
 * (C) Copyright 2017-2019 Intel Corporation.
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

#define D_LOGFAC DD_FAC(il)
#include "dfuse_common.h"
#include "dfuse_gah.h"
#include "intercept.h"

struct read_bulk_cb_r {
	struct dfuse_readx_out *out;
	struct dfuse_file_common *f_info;
	crt_rpc_t *rpc;
	struct dfuse_tracker tracker;
	int err;
	int rc;
};

static void
read_bulk_cb(const struct crt_cb_info *cb_info)
{
	struct read_bulk_cb_r *reply = cb_info->cci_arg;
	struct dfuse_readx_out *out = crt_reply_get(cb_info->cci_rpc);

	if (cb_info->cci_rc != 0) {
		/*
		 * Error handling.  On timeout return EAGAIN, all other errors
		 * return EIO.
		 *
		 * TODO: Handle target eviction here
		 */
		DFUSE_LOG_INFO("Bad RPC reply %d", cb_info->cci_rc);
		if (cb_info->cci_rc == -DER_TIMEDOUT)
			reply->err = EAGAIN;
		else
			reply->err = EIO;
		dfuse_tracker_signal(&reply->tracker);
		return;
	}

	if (out->err) {
		DFUSE_LOG_ERROR("Error from target %d", out->err);

		if (out->err == -DER_NOMEM)
			reply->err = ENOMEM;
		else
			reply->err = EIO;
		dfuse_tracker_signal(&reply->tracker);
		return;
	}

	if (out->rc) {
		reply->rc = out->rc;
		dfuse_tracker_signal(&reply->tracker);
		return;
	}

	crt_req_addref(cb_info->cci_rpc);

	reply->out = out;
	reply->rpc = cb_info->cci_rpc;

	dfuse_tracker_signal(&reply->tracker);
}

static ssize_t
read_bulk(char *buff, size_t len, off_t position,
	  struct dfuse_file_common *f_info, int *errcode)
{
	struct dfuse_projection		*fs_handle;
	struct dfuse_service_group	*grp;
	struct dfuse_readx_in		*in;
	struct dfuse_readx_out		*out;
	struct read_bulk_cb_r		reply = {0};
	crt_rpc_t			*rpc = NULL;
	crt_bulk_t			bulk;
	ssize_t				read_len = 0;
	d_sg_list_t			sgl = {0};
	d_iov_t				iov = {0};
	int				rc;

	fs_handle = f_info->projection;
	grp = fs_handle->grp;

	rc = crt_req_create(fs_handle->crt_ctx, &grp->psr_ep,
			    CRT_PROTO_OPC(fs_handle->io_proto->cpf_base,
					  fs_handle->io_proto->cpf_ver,
					  0),
			    &rpc);
	if (rc || !rpc) {
		DFUSE_LOG_ERROR("Could not create request, rc = %d", rc);
		*errcode = EIO;
		return -1;
	}

	in = crt_req_get(rpc);
	in->gah = f_info->gah;
	in->xtvec.xt_off = position;
	in->xtvec.xt_len = len;

	iov.iov_len = len;
	iov.iov_buf_len = len;
	iov.iov_buf = (void *)buff;
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;

	rc = crt_bulk_create(fs_handle->crt_ctx, &sgl, CRT_BULK_RW,
			     &in->data_bulk);
	if (rc) {
		DFUSE_LOG_ERROR("Failed to make local bulk handle %d", rc);
		*errcode = EIO;
		return -1;
	}

	dfuse_tracker_init(&reply.tracker, 1);
	bulk = in->data_bulk;

	reply.f_info = f_info;

	rc = crt_req_send(rpc, read_bulk_cb, &reply);
	if (rc) {
		DFUSE_LOG_ERROR("Could not send rpc, rc = %d", rc);
		*errcode = EIO;
		return -1;
	}
	dfuse_fs_wait(fs_handle, &reply.tracker);

	if (reply.err) {
		*errcode = reply.err;
		return -1;
	}

	if (reply.rc != 0) {
		*errcode = reply.rc;
		return -1;
	}

	out = reply.out;
	if (out->iov_len > 0) {
		if (out->data.iov_len != out->iov_len) {
			/* TODO: This is a resource leak */
			DFUSE_LOG_ERROR("Missing IOV %d", out->iov_len);
			*errcode = EIO;
			return -1;
		}
		read_len = out->data.iov_len;
		DFUSE_LOG_INFO("Received %#zx via immediate", read_len);
		memcpy(buff + out->bulk_len, out->data.iov_buf, read_len);
	}
	if (out->bulk_len > 0) {
		DFUSE_LOG_INFO("Received %#zx via bulk", out->bulk_len);
		read_len += out->bulk_len;
	}

	crt_req_decref(reply.rpc);

	rc = crt_bulk_free(bulk);
	if (rc) {
		*errcode = EIO;
		return -1;
	}

	DFUSE_LOG_INFO("Read complete %#zx", read_len);

	return read_len;
}

ssize_t ioil_do_pread(char *buff, size_t len, off_t position,
		      struct dfuse_file_common *f_info, int *errcode)
{
	DFUSE_LOG_INFO("%#zx-%#zx " GAH_PRINT_STR, position, position + len - 1,
		       GAH_PRINT_VAL(f_info->gah));

	return read_bulk(buff, len, position, f_info, errcode);
}

ssize_t
ioil_do_preadv(const struct iovec *iov, int count, off_t position,
	       struct dfuse_file_common *f_info, int *errcode)
{
	ssize_t bytes_read;
	ssize_t total_read = 0;
	int i;

	for (i = 0; i < count; i++) {
		bytes_read = read_bulk(iov[i].iov_base, iov[i].iov_len,
				       position, f_info, errcode);

		if (bytes_read == -1)
			return (ssize_t)-1;

		if (bytes_read == 0)
			return total_read;

		position += bytes_read;
		total_read += bytes_read;
	}

	return total_read;
}
