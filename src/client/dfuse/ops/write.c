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
write_cb(struct dfuse_request *request)
{
	struct dfuse_wb		*wb = container_of(request, struct dfuse_wb, wb_req);
	struct dfuse_writex_out	*out = crt_reply_get(request->rpc);
	struct dfuse_writex_in	*in = crt_req_get(request->rpc);

	if (out->err) {
		/* Convert the error types, out->err is a CaRT error code
		 * so translate it to a errno we can pass back to FUSE.
		 */
		DFUSE_TRA_ERROR(wb, "Error from target %d", out->err);

		if (in->data_bulk)
			wb->failure = true;

		D_GOTO(err, request->rc = EIO);
	}

	DFUSE_REQUEST_RESOLVE(request, out);
	if (request->rc)
		D_GOTO(err, 0);

	DFUSE_REPLY_WRITE(wb, request->req, out->len);

	dfuse_da_release(request->fsh->write_da, wb);

	return false;

err:
	DFUSE_REPLY_ERR(request, request->rc);

	dfuse_da_release(request->fsh->write_da, wb);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result = write_cb,
	.gah_offset = offsetof(struct dfuse_writex_in, gah),
	.have_gah = true,
};

static void
dfuse_writex(size_t len, off_t position, struct dfuse_wb *wb)
{
	struct dfuse_writex_in *in = crt_req_get(wb->wb_req.rpc);
	int rc;

	in->xtvec.xt_len = len;
	if (len <= wb->wb_req.fsh->proj.max_iov_write) {
		d_iov_set(&in->data, wb->lb.buf, len);
	} else {
		in->bulk_len = len;
		in->data_bulk = wb->lb.handle;
	}

	in->xtvec.xt_off = position;
	wb->wb_req.ir_api = &api;

	rc = dfuse_fs_send(&wb->wb_req);
	if (rc) {
		D_GOTO(err, rc = EIO);
	}

	return;

err:
	DFUSE_REPLY_ERR_RAW(wb, wb->wb_req.req, rc);
	dfuse_da_release(wb->wb_req.fsh->write_da, wb);
}

void
dfuse_cb_write(fuse_req_t req, fuse_ino_t ino, const char *buff, size_t len,
	       off_t position, struct fuse_file_info *fi)
{
	struct dfuse_file_handle *handle = (struct dfuse_file_handle *)fi->fh;
	struct dfuse_wb *wb;
	int rc;

	wb = dfuse_da_acquire(handle->open_req.fsh->write_da);
	if (!wb)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(wb, handle, "writebuf");

	DFUSE_TRA_INFO(wb, "%#zx-%#zx " GAH_PRINT_STR, position,
		       position + len - 1, GAH_PRINT_VAL(handle->common.gah));

	wb->wb_req.req = req;
	wb->wb_req.ir_file = handle;

	memcpy(wb->lb.buf, buff, len);

	dfuse_writex(len, position, wb);

	return;
err:
	DFUSE_REPLY_ERR_RAW(handle, req, rc);
}

/*
 * write_buf() callback for fuse.  Essentially the same as dfuse_cb_write()
 * however with two advantages, it allows us to check parameters before
 * doing any allocation/memcpy() and it uses fuse_buf_copy() to put the data
 * directly into our data buffer avoiding an additional memcpy().
 */
void
dfuse_cb_write_buf(fuse_req_t req, fuse_ino_t ino, struct fuse_bufvec *bufv,
		   off_t position, struct fuse_file_info *fi)
{
	struct dfuse_file_handle *handle = (struct dfuse_file_handle *)fi->fh;
	struct dfuse_wb *wb = NULL;
	size_t len = bufv->buf[0].size;
	struct fuse_bufvec dst = { .count = 1 };
	int rc;

	/* Check for buffer count being 1.  According to the documentation this
	 * will always be the case, and if it isn't then our code will be using
	 * the wrong value for len
	 */
	if (bufv->count != 1)
		D_GOTO(err, rc = EIO);

	DFUSE_TRA_INFO(handle, "Count %zi [0].flags %#x",
		       bufv->count, bufv->buf[0].flags);

	wb = dfuse_da_acquire(handle->open_req.fsh->write_da);
	if (!wb)
		D_GOTO(err, rc = ENOMEM);
	DFUSE_TRA_UP(wb, handle, "writebuf");

	DFUSE_TRA_INFO(wb, "%#zx-%#zx " GAH_PRINT_STR, position,
		       position + len - 1, GAH_PRINT_VAL(handle->common.gah));

	wb->wb_req.req = req;
	wb->wb_req.ir_file = handle;

	dst.buf[0].size = len;
	dst.buf[0].mem = wb->lb.buf;
	rc = fuse_buf_copy(&dst, bufv, 0);
	if (rc != len)
		D_GOTO(err, rc = EIO);

	dfuse_writex(len, position, wb);

	return;
err:
	DFUSE_REPLY_ERR_RAW(handle, req, rc);
	if (wb)
		dfuse_da_release(handle->open_req.fsh->write_da, wb);
}
