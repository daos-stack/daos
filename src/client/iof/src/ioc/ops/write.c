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
write_cb(struct ioc_request *request)
{
	struct iof_wb		*wb = container_of(request, struct iof_wb, wb_req);
	struct iof_writex_out	*out = crt_reply_get(request->rpc);
	struct iof_writex_in	*in = crt_req_get(request->rpc);

	if (out->err) {
		/* Convert the error types, out->err is a IOF error code
		 * so translate it to a errno we can pass back to FUSE.
		 */
		IOF_TRACE_ERROR(wb, "Error from target %d", out->err);

		if (in->data_bulk)
			wb->failure = true;
		if (out->err == -DER_NONEXIST)
			H_GAH_SET_INVALID(wb->wb_req.ir_file);

		D_GOTO(err, request->rc = EIO);
	}

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc)
		D_GOTO(err, 0);

	IOC_REPLY_WRITE(wb, request->req, out->len);

	STAT_ADD_COUNT(request->fsh->stats, write_bytes, out->len);

	iof_pool_release(request->fsh->write_pool, wb);

	return false;

err:
	IOC_REPLY_ERR(request, request->rc);

	iof_pool_release(request->fsh->write_pool, wb);
	return false;
}

static const struct ioc_request_api api = {
	.on_result = write_cb,
	.gah_offset = offsetof(struct iof_writex_in, gah),
	.have_gah = true,
};

static void
ioc_writex(size_t len, off_t position, struct iof_wb *wb)
{
	struct iof_writex_in *in = crt_req_get(wb->wb_req.rpc);
	int rc;

	IOF_TRACE_LINK(wb->wb_req.rpc, wb, "writex_rpc");

	in->xtvec.xt_len = len;
	if (len <= wb->wb_req.fsh->proj.max_iov_write) {
		d_iov_set(&in->data, wb->lb.buf, len);
	} else {
		in->bulk_len = len;
		in->data_bulk = wb->lb.handle;
	}

	in->xtvec.xt_off = position;
	wb->wb_req.ir_api = &api;

	rc = iof_fs_send(&wb->wb_req);
	if (rc) {
		D_GOTO(err, rc = EIO);
	}

	return;

err:
	IOC_REPLY_ERR_RAW(wb, wb->wb_req.req, rc);
	iof_pool_release(wb->wb_req.fsh->write_pool, wb);
}

void ioc_ll_write(fuse_req_t req, fuse_ino_t ino, const char *buff, size_t len,
		  off_t position, struct fuse_file_info *fi)
{
	struct iof_file_handle *handle = (struct iof_file_handle *)fi->fh;
	struct iof_wb *wb = NULL;
	int rc;

	STAT_ADD(handle->open_req.fsh->stats, write);

	wb = iof_pool_acquire(handle->open_req.fsh->write_pool);
	if (!wb)
		D_GOTO(err, rc = ENOMEM);

	IOF_TRACE_UP(wb, handle, "writebuf");

	IOF_TRACE_INFO(wb, "%#zx-%#zx " GAH_PRINT_STR, position,
		       position + len - 1, GAH_PRINT_VAL(handle->common.gah));

	wb->wb_req.req = req;
	wb->wb_req.ir_file = handle;

	memcpy(wb->lb.buf, buff, len);

	ioc_writex(len, position, wb);

	return;
err:
	IOC_REPLY_ERR_RAW(handle, req, rc);
}

/*
 * write_buf() callback for fuse.  Essentially the same as ioc_ll_write()
 * however with two advantages, it allows us to check parameters before
 * doing any allocation/memcpy() and it uses fuse_buf_copy() to put the data
 * directly into our data buffer avoiding an additional memcpy().
 */
void ioc_ll_write_buf(fuse_req_t req, fuse_ino_t ino, struct fuse_bufvec *bufv,
		      off_t position, struct fuse_file_info *fi)
{
	struct iof_file_handle *handle = (struct iof_file_handle *)fi->fh;
	struct iof_wb *wb = NULL;
	size_t len = bufv->buf[0].size;
	struct fuse_bufvec dst = { .count = 1 };
	int rc;

	STAT_ADD(handle->open_req.fsh->stats, write);

	/* Check for buffer count being 1.  According to the documentation this
	 * will always be the case, and if it isn't then our code will be using
	 * the wrong value for len
	 */
	if (bufv->count != 1)
		D_GOTO(err, rc = EIO);

	IOF_TRACE_INFO(handle, "Count %zi [0].flags %#x",
		       bufv->count, bufv->buf[0].flags);

	wb = iof_pool_acquire(handle->open_req.fsh->write_pool);
	if (!wb)
		D_GOTO(err, rc = ENOMEM);
	IOF_TRACE_UP(wb, handle, "writebuf");

	IOF_TRACE_INFO(wb, "%#zx-%#zx " GAH_PRINT_STR, position,
		       position + len - 1, GAH_PRINT_VAL(handle->common.gah));

	wb->wb_req.req = req;
	wb->wb_req.ir_file = handle;

	dst.buf[0].size = len;
	dst.buf[0].mem = wb->lb.buf;
	rc = fuse_buf_copy(&dst, bufv, 0);
	if (rc != len)
		D_GOTO(err, rc = EIO);

	ioc_writex(len, position, wb);

	return;
err:
	IOC_REPLY_ERR_RAW(handle, req, rc);
	if (wb)
		iof_pool_release(handle->open_req.fsh->write_pool, wb);
}
