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

#define REQ_NAME close_req
#define POOL_NAME dh_pool
#define TYPE_NAME iof_dir_handle
#include "ioc_ops.h"

#define STAT_KEY closedir

static bool
closedir_ll_cb(struct ioc_request *request)
{
	struct iof_status_out *out	= crt_reply_get(request->rpc);
	struct TYPE_NAME *dh		= CONTAINER(request);
	struct iof_projection_info *fs_handle = dh->open_req.fsh;

	IOC_REQUEST_RESOLVE(request, out);

	if (!request->req)
		D_GOTO(out, 0);

	if (request->rc == 0)
		IOC_REPLY_ZERO(request);
	else
		IOC_REPLY_ERR(request, request->rc);
out:

	D_MUTEX_LOCK(&fs_handle->od_lock);
	d_list_del_init(&dh->dh_od_list);
	D_MUTEX_UNLOCK(&fs_handle->od_lock);

	iof_pool_release(dh->open_req.fsh->dh_pool, dh);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= closedir_ll_cb,
	.gah_offset	= offsetof(struct iof_gah_in, gah),
	.have_gah	= true,
};

void ioc_releasedir_priv(fuse_req_t req, struct iof_dir_handle *dh)
{
	struct iof_projection_info *fs_handle = dh->open_req.fsh;
	int rc;

	IOC_REQ_INIT_REQ(dh, fs_handle, api, req, rc);
	if (rc)
		D_GOTO(err, rc);

	rc = iof_fs_send(&dh->close_req);
	if (rc != 0)
		D_GOTO(err, rc);
	return;
err:
	if (req) {
		dh->close_req.req  = req;
		IOC_REPLY_ERR(&dh->close_req, rc);
	} else {
		IOF_TRACE_DOWN(&dh->close_req);
	}

	D_MUTEX_LOCK(&fs_handle->od_lock);
	d_list_del_init(&dh->dh_od_list);
	D_MUTEX_UNLOCK(&fs_handle->od_lock);

	iof_pool_release(fs_handle->dh_pool, dh);

}

void ioc_ll_releasedir(fuse_req_t req, fuse_ino_t ino,
		       struct fuse_file_info *fi)
{
	struct TYPE_NAME *dh = (struct TYPE_NAME *)fi->fh;

	ioc_releasedir_priv(req, dh);
}

void ioc_int_releasedir(struct iof_dir_handle *dh)
{
	ioc_releasedir_priv(NULL, dh);
}
