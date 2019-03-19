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

static const struct ioc_request_api api = {
	.on_result	= ioc_gen_cb,
	.have_gah	= true,
	.gah_offset	= offsetof(struct iof_gah_in, gah),
};

void
ioc_ll_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
	     struct fuse_file_info *fi)
{
	struct iof_file_handle		*handle = (struct iof_file_handle *)fi->fh;
	struct iof_projection_info	*fs_handle = handle->open_req.fsh;
	struct ioc_request		*request;
	crt_opcode_t opcode;
	int rc;
	int ret;

	STAT_ADD(fs_handle->stats, fsync);

	if (!IOF_IS_WRITEABLE(fs_handle->flags))
		D_GOTO(out_no_request, ret = EROFS);

	IOF_TRACE_INFO(handle);

	D_ALLOC_PTR(request);
	if (!request) {
		D_GOTO(out_no_request, ret = ENOMEM);
	}

	IOC_REQUEST_INIT(request, fs_handle);
	IOC_REQUEST_RESET(request);

	IOF_TRACE_UP(request, fs_handle, "fsync");
	IOF_TRACE_INFO(request, "fsync %lu", ino);

	request->req = req;
	request->ir_api = &api;
	request->ir_ht = RHS_FILE;
	request->ir_file = handle;

	if (datasync)
		opcode = FS_TO_OP(fs_handle, fdatasync);
	else
		opcode = FS_TO_OP(fs_handle, fsync);

	rc = crt_req_create(fs_handle->proj.crt_ctx, NULL, opcode,
			    &request->rpc);
	if (rc || !request->rpc) {
		IOF_TRACE_ERROR(request,
				"Could not create request, rc = %d",
				rc);
		D_GOTO(out_err, ret = EIO);
	}
	crt_req_addref(request->rpc);

	rc = iof_fs_send(request);
	if (rc != 0) {
		D_GOTO(out_decref, ret = EIO);
	}

	return;

out_no_request:
	IOC_REPLY_ERR_RAW(fs_handle, req, ret);
	return;

out_decref:
	crt_req_decref(request->rpc);

out_err:
	IOC_REPLY_ERR(request, ret);
	D_FREE(request);
}
