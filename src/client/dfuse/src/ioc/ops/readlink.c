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

static bool
readlink_cb(struct ioc_request *request)
{
	struct iof_string_out *out = crt_reply_get(request->rpc);

	/* Drop the two refs that this code has taken, one from the
	 * req_create() call and a second from addref
	 */
	crt_req_decref(request->rpc);
	crt_req_decref(request->rpc);
	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc) {
		D_GOTO(out_err, 0);
	}

	IOC_REPLY_READLINK(request, out->path);

	D_FREE(request);
	return false;

out_err:
	IOC_REPLY_ERR(request, request->rc);
	D_FREE(request);
	return false;

}

static const struct ioc_request_api api = {
	.on_result	= readlink_cb,
	.gah_offset	= offsetof(struct iof_gah_in, gah),
	.have_gah	= true,
};

void
ioc_ll_readlink(fuse_req_t req, fuse_ino_t ino)
{
	struct iof_projection_info *fs_handle = fuse_req_userdata(req);
	struct ioc_request		*request;
	int rc;
	int ret;

	STAT_ADD(fs_handle->stats, readlink);

	D_ALLOC_PTR(request);
	if (!request) {
		D_GOTO(out_no_request, ret = ENOMEM);
	}

	IOC_REQUEST_INIT(request, fs_handle);
	IOC_REQUEST_RESET(request);

	IOF_TRACE_UP(request, fs_handle, "readlink");
	IOF_TRACE_INFO(request, "statfs %lu", ino);

	request->req = req;
	request->ir_api = &api;
	request->ir_ht = RHS_INODE_NUM;
	request->ir_inode_num = ino;

	rc = crt_req_create(fs_handle->proj.crt_ctx, NULL,
			    FS_TO_OP(fs_handle, readlink), &request->rpc);
	if (rc || !request->rpc) {
		IOF_TRACE_ERROR(request,
				"Could not create request, rc = %d",
				rc);
		D_GOTO(out_err, ret = EIO);
	}

	/* Add a second ref as that's what the iof_fs_send() function
	 * expects.  In the case of failover the RPC might be completed,
	 * and a copy made the the RPC seen in statfs_cb might not be
	 * the same one as seen here.
	 */
	crt_req_addref(request->rpc);

	rc = iof_fs_send(request);
	if (rc != 0) {
		D_GOTO(out_err, ret = EIO);
	}

	return;

out_no_request:
	IOC_REPLY_ERR_RAW(fs_handle, req, ret);
	return;

out_err:
	IOC_REPLY_ERR(request, ret);
	D_FREE(request);
}
