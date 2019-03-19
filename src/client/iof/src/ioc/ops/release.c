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
ioc_release_cb(struct ioc_request *request)
{
	struct iof_status_out *out = crt_reply_get(request->rpc);

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc) {
		IOC_REPLY_ERR(request, request->rc);
	} else {
		IOC_REPLY_ZERO(request);
	}

	iof_pool_release(request->fsh->fh_pool, request->ir_file);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= ioc_release_cb,
	.gah_offset	= offsetof(struct iof_gah_in, gah),
	.have_gah	= true,
};

static void
ioc_release_priv(struct iof_file_handle *handle)
{
	struct iof_projection_info *fs_handle = handle->release_req.fsh;
	int rc;

	STAT_ADD(fs_handle->stats, release);

	D_MUTEX_LOCK(&fs_handle->of_lock);
	d_list_del(&handle->fh_of_list);
	d_list_del(&handle->fh_ino_list);
	D_MUTEX_UNLOCK(&fs_handle->of_lock);

	IOF_TRACE_UP(&handle->release_req, handle, "release_req");

	IOF_TRACE_INFO(&handle->release_req,
		       GAH_PRINT_STR, GAH_PRINT_VAL(handle->common.gah));

	handle->release_req.ir_api = &api;

	rc = iof_fs_send(&handle->release_req);
	if (rc) {
		D_GOTO(out_err, rc = EIO);
	}

	return;

out_err:
	if (handle->release_req.req) {
		IOC_REPLY_ERR(&handle->release_req, rc);
	} else {
		IOF_TRACE_DOWN(&handle->release_req);
	}
	iof_pool_release(fs_handle->fh_pool, handle);
}

void ioc_ll_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct iof_file_handle *handle = (struct iof_file_handle *)fi->fh;

	handle->release_req.req = req;
	ioc_release_priv(handle);
}

void ioc_int_release(struct iof_file_handle *handle)
{

	ioc_release_priv(handle);
}
