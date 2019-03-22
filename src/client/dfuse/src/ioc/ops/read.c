/* Copyright (C) 2016-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "iof_common.h"
#include "ioc.h"
#include "log.h"
#include "ios_gah.h"

static bool
read_bulk_cb(struct ioc_request *request)
{
	struct iof_rb *rb = container_of(request, struct iof_rb, rb_req);
	struct iof_readx_out *out = crt_reply_get(request->rpc);
	int rc = 0;
	size_t bytes_read = 0;
	void *buff = NULL;

	if (out->err) {
		IOF_TRACE_ERROR(rb, "Error from target %d", out->err);
		rb->failure = true;
		if (out->err == -DER_NONEXIST)
			H_GAH_SET_INVALID(request->ir_file);
		D_GOTO(out, request->rc = EIO);
	}

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc)
		D_GOTO(out, 0);

	if (out->iov_len > 0) {
		if (out->data.iov_len != out->iov_len)
			D_GOTO(out, request->rc = EIO);
		buff = out->data.iov_buf;
		bytes_read = out->data.iov_len;
	} else if (out->bulk_len > 0) {
		bytes_read = out->bulk_len;
		buff = rb->lb.buf;
	}

out:
	if (request->rc) {
		IOC_REPLY_ERR(request, request->rc);
	} else {
		STAT_ADD_COUNT(request->fsh->stats, read_bytes, bytes_read);

		/* It's not clear without benchmarking which approach is better
		 * here, fuse_reply_buf() is a small wrapper around writev()
		 * which is a much shorter code-path however fuse_reply_data()
		 * attempts to use splice which may well be faster.
		 *
		 * For now it's easy to pick between them, and both of them are
		 * passing valgrind tests.
		 */
		if (request->fsh->flags & IOF_FUSE_READ_BUF) {
			rc = fuse_reply_buf(request->req, buff, bytes_read);
			if (rc != 0)
				IOF_TRACE_ERROR(rb,
						"fuse_reply_buf returned %d:%s",
						rc, strerror(-rc));

		} else {
			rb->fbuf.buf[0].size = bytes_read;
			rb->fbuf.buf[0].mem = buff;
			rc = fuse_reply_data(request->req, &rb->fbuf, 0);
			if (rc != 0)
				IOF_TRACE_ERROR(rb,
						"fuse_reply_data returned %d:%s",
						rc, strerror(-rc));
		}
	}
	iof_pool_release(rb->pt, rb);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= read_bulk_cb,
	.gah_offset	= offsetof(struct iof_readx_in, gah),
	.have_gah	= true,
};

void ioc_ll_read(fuse_req_t req, fuse_ino_t ino, size_t len,
		 off_t position, struct fuse_file_info *fi)
{
	struct iof_file_handle *handle = (void *)fi->fh;
	struct iof_projection_info *fs_handle = handle->open_req.fsh;
	struct iof_readx_in *in;
	struct iof_pool_type *pt;
	struct iof_rb *rb = NULL;
	int rc;

	STAT_ADD(fs_handle->stats, read);

	IOF_TRACE_INFO(handle, "%#zx-%#zx " GAH_PRINT_STR, position,
		       position + len - 1, GAH_PRINT_VAL(handle->common.gah));

	if (len <= 4096)
		pt = fs_handle->rb_pool_page;
	else
		pt = fs_handle->rb_pool_large;

	rb = iof_pool_acquire(pt);
	if (!rb)
		D_GOTO(out_err, rc = ENOMEM);

	IOF_TRACE_UP(rb, handle, "readbuf");

	rb->rb_req.req = req;
	rb->rb_req.ir_api = &api;
	rb->rb_req.ir_file = handle;
	rb->pt = pt;

	in = crt_req_get(rb->rb_req.rpc);

	in->xtvec.xt_off = position;
	in->xtvec.xt_len = len;
	in->data_bulk = rb->lb.handle;
	IOF_TRACE_LINK(rb->rb_req.rpc, rb, "read_bulk_rpc");

	rc = iof_fs_send(&rb->rb_req);
	if (rc != 0) {
		IOC_REPLY_ERR(&rb->rb_req, rc);
		iof_pool_release(pt, rb);
	}

	iof_pool_restock(pt);
	return;

out_err:
	IOC_REPLY_ERR_RAW(fs_handle, req, rc);
}
