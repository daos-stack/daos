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

#define REQ_NAME close_req
#define POOL_NAME dh_pool
#define TYPE_NAME iof_dir_handle
#include "dfuse_ops.h"

#define STAT_KEY closedir

static bool
closedir_ll_cb(struct ioc_request *request)
{
	struct iof_status_out *out	= crt_reply_get(request->rpc);
	struct TYPE_NAME *dh		= CONTAINER(request);

	IOC_REQUEST_RESOLVE(request, out);

	if (!request->req)
		D_GOTO(out, 0);

	if (request->rc == 0)
		IOC_REPLY_ZERO(request);
	else
		IOC_REPLY_ERR(request, request->rc);
out:
	iof_pool_release(dh->open_req.fsh->dh_pool, dh);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= closedir_ll_cb,
	.gah_offset	= offsetof(struct iof_gah_in, gah),
	.have_gah	= true,
};

void
ioc_releasedir_priv(fuse_req_t req, struct iof_dir_handle *dh)
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

	iof_pool_release(fs_handle->dh_pool, dh);

}

void
dfuse_cb_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct TYPE_NAME *dh = (struct TYPE_NAME *)fi->fh;

	ioc_releasedir_priv(req, dh);
}

void
ioc_int_releasedir(struct iof_dir_handle *dh)
{
	ioc_releasedir_priv(NULL, dh);
}
