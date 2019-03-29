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

struct write_cb_r {
	struct iof_file_common *f_info;
	ssize_t len;
	struct iof_tracker tracker;
	int err;
	int rc;
};

static void
write_cb(const struct crt_cb_info *cb_info)
{
	struct write_cb_r *reply = cb_info->cci_arg;
	struct iof_writex_out *out = crt_reply_get(cb_info->cci_rpc);

	if (cb_info->cci_rc != 0) {
		/*
		 * Error handling.  On timeout return EAGAIN, all other errors
		 * return EIO.
		 *
		 * TODO: Handle target eviction here
		 */
		IOF_LOG_INFO("Bad RPC reply %d", cb_info->cci_rc);
		if (cb_info->cci_rc == -DER_TIMEDOUT)
			reply->err = EAGAIN;
		else
			reply->err = EIO;
		iof_tracker_signal(&reply->tracker);
		return;
	}

	if (out->err) {
		IOF_LOG_ERROR("Error from target %d", out->err);

		reply->err = EIO;

		if (out->err == -DER_NOMEM)
			reply->err = ENOMEM;

		iof_tracker_signal(&reply->tracker);
		return;
	}

	reply->len = out->len;
	reply->rc = out->rc;
	iof_tracker_signal(&reply->tracker);
}

ssize_t
ioil_do_writex(const char *buff, size_t len, off_t position,
	       struct iof_file_common *f_info, int *errcode)
{
	struct iof_projection *fs_handle;
	struct iof_service_group *grp;
	struct iof_writex_in *in;
	struct write_cb_r reply = {0};
	crt_rpc_t *rpc = NULL;
	crt_bulk_t bulk;
	d_sg_list_t sgl = {0};
	d_iov_t iov = {0};
	uint64_t imm_len;
	uint64_t imm_offset = 0;
	int rc;

	IOF_LOG_INFO("%#zx-%#zx " GAH_PRINT_STR, position,
		     position + len - 1, GAH_PRINT_VAL(f_info->gah));

	fs_handle = f_info->projection;
	grp = fs_handle->grp;

	rc = crt_req_create(fs_handle->crt_ctx, &grp->psr_ep,
			    CRT_PROTO_OPC(fs_handle->io_proto->cpf_base,
					  fs_handle->io_proto->cpf_ver,
					  1),
			    &rpc);
	if (rc || !rpc) {
		IOF_LOG_ERROR("Could not create request, rc = %d",
			      rc);
		*errcode = EIO;
		return -1;
	}

	in = crt_req_get(rpc);
	in->gah = f_info->gah;

	in->xtvec.xt_len = len;
	imm_len = len % fs_handle->max_write;
	if (imm_len <= fs_handle->max_iov_write) {
		imm_offset = len - imm_len;
		d_iov_set(&in->data, (void *)buff + imm_offset, imm_len);
	} else {
		imm_len = 0;
		imm_offset = in->xtvec.xt_len;
	}

	if (imm_offset != 0) {
		in->bulk_len = iov.iov_len = imm_offset;
		iov.iov_buf_len = imm_offset;
		iov.iov_buf = (void *)buff;
		sgl.sg_iovs = &iov;
		sgl.sg_nr = 1;

		rc = crt_bulk_create(fs_handle->crt_ctx, &sgl, CRT_BULK_RO,
				     &in->data_bulk);
		if (rc) {
			IOF_LOG_ERROR("Failed to make local bulk handle %d",
				      rc);
			*errcode = EIO;
			return -1;
		}
	}

	iof_tracker_init(&reply.tracker, 1);
	in->xtvec.xt_off = position;

	bulk = in->data_bulk;

	reply.f_info = f_info;

	rc = crt_req_send(rpc, write_cb, &reply);
	if (rc) {
		IOF_LOG_ERROR("Could not send rpc, rc = %d", rc);
		*errcode = EIO;
		return -1;
	}
	iof_fs_wait(fs_handle, &reply.tracker);

	if (bulk) {
		rc = crt_bulk_free(bulk);
		if (rc) {
			*errcode = EIO;
			return -1;
		}
	}

	if (reply.err) {
		*errcode = reply.err;
		return -1;
	}

	if (reply.rc != 0) {
		*errcode = reply.rc;
		return -1;
	}

	return reply.len;
}

ssize_t
ioil_do_pwritev(const struct iovec *iov, int count, off_t position,
		struct iof_file_common *f_info, int *errcode)
{
	ssize_t bytes_written;
	ssize_t total_write = 0;
	int i;

	for (i = 0; i < count; i++) {
		bytes_written = ioil_do_writex(iov[i].iov_base, iov[i].iov_len,
					       position, f_info, errcode);

		if (bytes_written == -1)
			return (ssize_t)-1;

		if (bytes_written == 0)
			return total_write;

		position += bytes_written;
		total_write += bytes_written;
	}

	return total_write;
}
