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
ioc_open_ll_cb(struct ioc_request *request)
{
	struct iof_file_handle	*handle = container_of(request, struct iof_file_handle, open_req);
	struct iof_open_out	*out = crt_reply_get(request->rpc);
	struct fuse_file_info	fi = {0};

	IOF_TRACE_DEBUG(handle, "cci_rc %d rc %d err %d",
			request->rc, out->rc, out->err);

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc != 0) {
		D_GOTO(out_err, 0);
	}

	/* Create a new FI descriptor and use it to point to
	 * our local handle
	 */

	fi.fh = (uint64_t)handle;
	handle->common.gah = out->gah;
	handle->common.ep = request->rpc->cr_ep;
	H_GAH_SET_VALID(handle);
	D_MUTEX_LOCK(&request->fsh->of_lock);
	d_list_add_tail(&handle->fh_of_list, &request->fsh->openfile_list);
	D_MUTEX_UNLOCK(&request->fsh->of_lock);

	IOC_REPLY_OPEN(&handle->open_req, fi);

	return false;

out_err:
	IOC_REPLY_ERR(request, request->rc);
	iof_pool_release(request->fsh->fh_pool, handle);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= ioc_open_ll_cb,
	.gah_offset	= offsetof(struct iof_open_in, gah),
	.have_gah	= true,
};

void ioc_ll_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct iof_projection_info *fs_handle = fuse_req_userdata(req);
	struct iof_file_handle *handle = NULL;
	struct iof_open_in *in;
	int rc;

	STAT_ADD(fs_handle->stats, open);

	/* O_LARGEFILE should always be set on 64 bit systems, and in fact is
	 * defined to 0 so IOF defines LARGEFILE to the value that O_LARGEFILE
	 * would otherwise be using and check that is set.
	 */
	if (!(fi->flags & LARGEFILE)) {
		IOF_TRACE_INFO(fs_handle, "O_LARGEFILE required 0%o",
			       fi->flags);
		D_GOTO(out_err, rc = ENOTSUP);
	}

	/* Check for flags that do not make sense in this context.
	 */
	if (fi->flags & IOF_UNSUPPORTED_OPEN_FLAGS) {
		IOF_TRACE_INFO(fs_handle, "unsupported flag requested 0%o",
			       fi->flags);
		D_GOTO(out_err, rc = ENOTSUP);
	}

	if (fi->flags & O_WRONLY || fi->flags & O_RDWR) {
		if (!IOF_IS_WRITEABLE(fs_handle->flags)) {
			IOF_TRACE_INFO(fs_handle,
				       "Attempt to modify Read-Only File System");
			D_GOTO(out_err, rc = EROFS);
		}
	}

	handle = iof_pool_acquire(fs_handle->fh_pool);
	if (!handle) {
		D_GOTO(out_err, rc = ENOMEM);
	}
	IOF_TRACE_UP(handle, fs_handle, fs_handle->fh_pool->reg.name);
	IOF_TRACE_UP(&handle->open_req, handle, "open_req");
	IOF_TRACE_LINK(handle->open_req.rpc, &handle->open_req, "open_file_rpc");

	handle->common.projection = &fs_handle->proj;
	handle->open_req.req = req;
	handle->open_req.ir_api = &api;
	handle->inode_num = ino;

	in = crt_req_get(handle->open_req.rpc);

	handle->open_req.ir_inode_num = ino;

	in->flags = fi->flags;
	IOF_TRACE_INFO(handle, "flags 0%o", fi->flags);

	LOG_FLAGS(handle, fi->flags);

	rc = iof_fs_send(&handle->open_req);
	if (rc) {
		D_GOTO(out_err, rc = EIO);
	}

	iof_pool_restock(fs_handle->fh_pool);

	return;
out_err:
	IOC_REPLY_ERR_RAW(handle, req, rc);

	if (handle)
		iof_pool_release(fs_handle->fh_pool, handle);
}
