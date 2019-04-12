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
#define POOL_NAME dh_da
#define TYPE_NAME dfuse_dir_handle
#include "dfuse_ops.h"

static bool
closedir_ll_cb(struct dfuse_request *request)
{
	struct dfuse_status_out	   *out = request->out;
	struct TYPE_NAME *dh		= CONTAINER(request);

	DFUSE_REQUEST_RESOLVE(request, out);

	if (!request->req)
		D_GOTO(out, 0);

	if (request->rc == 0)
		DFUSE_REPLY_ZERO(request);
	else
		DFUSE_REPLY_ERR(request, request->rc);
out:
	dfuse_da_release(dh->open_req.fsh->dh_da, dh);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result	= closedir_ll_cb,
};

void
dfuse_releasedir_priv(fuse_req_t req, struct dfuse_dir_handle *dh)
{
	struct dfuse_projection_info *fs_handle = dh->open_req.fsh;
	int rc;

	DFUSE_REQ_INIT_REQ(dh, fs_handle, api, req, rc);
	if (rc)
		D_GOTO(err, rc);

	rc = dfuse_fs_send(&dh->close_req);
	if (rc != 0)
		D_GOTO(err, rc);
	return;
err:
	if (req) {
		dh->close_req.req  = req;
		DFUSE_REPLY_ERR(&dh->close_req, rc);
	} else {
		DFUSE_TRA_DOWN(&dh->close_req);
	}

	dfuse_da_release(fs_handle->dh_da, dh);

}

void
dfuse_cb_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct TYPE_NAME *dh = (struct TYPE_NAME *)fi->fh;

	dfuse_releasedir_priv(req, dh);
}

void
dfuse_int_releasedir(struct dfuse_dir_handle *dh)
{
	dfuse_releasedir_priv(NULL, dh);
}
