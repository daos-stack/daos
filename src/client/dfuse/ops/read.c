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

#include "dfuse_common.h"
#include "dfuse.h"

static bool
read_bulk_cb(struct dfuse_request *request)
{
	struct dfuse_rb *rb = container_of(request, struct dfuse_rb, rb_req);
	struct dfuse_readx_out *out = request->out;
	int rc = 0;
	size_t bytes_read = 0;
	void *buff = NULL;

	if (out->err) {
		DFUSE_TRA_ERROR(rb, "Error from target %d", out->err);
		rb->failure = true;
		D_GOTO(out, request->rc = EIO);
	}

	DFUSE_REQUEST_RESOLVE(request, out);
	if (request->rc)
		D_GOTO(out, 0);

	if (out->iov_len > 0) {
		if (out->data.iov_len != out->iov_len)
			D_GOTO(out, request->rc = EIO);
		buff = out->data.iov_buf;
		bytes_read = out->data.iov_len;
	} else if (out->bulk_len > 0) {
		bytes_read = out->bulk_len;
	}

out:
	if (request->rc) {
		DFUSE_REPLY_ERR(request, request->rc);
	} else {

		/* It's not clear without benchmarking which approach is better
		 * here, fuse_reply_buf() is a small wrapper around writev()
		 * which is a much shorter code-path however fuse_reply_data()
		 * attempts to use splice which may well be faster.
		 *
		 * For now it's easy to pick between them, and both of them are
		 * passing valgrind tests.
		 */
		if (request->fsh->flags & DFUSE_FUSE_READ_BUF) {
			rc = fuse_reply_buf(request->req, buff, bytes_read);
			if (rc != 0)
				DFUSE_TRA_ERROR(rb,
						"fuse_reply_buf returned %d:%s",
						rc, strerror(-rc));

		} else {
			rb->fbuf.buf[0].size = bytes_read;
			rb->fbuf.buf[0].mem = buff;
			rc = fuse_reply_data(request->req, &rb->fbuf, 0);
			if (rc != 0)
				DFUSE_TRA_ERROR(rb,
						"fuse_reply_data returned %d:%s",
						rc, strerror(-rc));
		}
	}
	dfuse_da_release(rb->pt, rb);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result	= read_bulk_cb,
};

void
dfuse_cb_read(fuse_req_t req, fuse_ino_t ino, size_t len, off_t position,
	      struct fuse_file_info *fi)
{
	struct dfuse_file_handle *handle = (void *)fi->fh;
	struct dfuse_projection_info *fs_handle = handle->open_req.fsh;
	struct dfuse_da_type *pt;
	struct dfuse_rb *rb = NULL;
	int rc;

	DFUSE_TRA_INFO(handle, "%#zx-%#zx " GAH_PRINT_STR, position,
		       position + len - 1, GAH_PRINT_VAL(handle->common.gah));

	if (len <= 4096)
		pt = fs_handle->rb_da_page;
	else
		pt = fs_handle->rb_da_large;

	rb = dfuse_da_acquire(pt);
	if (!rb)
		D_GOTO(out_err, rc = ENOMEM);

	DFUSE_TRA_UP(rb, handle, "readbuf");

	rb->rb_req.req = req;
	rb->rb_req.ir_api = &api;
	rb->rb_req.ir_file = handle;
	rb->pt = pt;

	rc = dfuse_fs_send(&rb->rb_req);
	if (rc != 0) {
		DFUSE_REPLY_ERR(&rb->rb_req, rc);
		dfuse_da_release(pt, rb);
	}

	dfuse_da_restock(pt);
	return;

out_err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
}
