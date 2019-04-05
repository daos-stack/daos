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
dfuse_release_cb(struct dfuse_request *request)
{
	struct dfuse_status_out *out = request->out;

	DFUSE_REQUEST_RESOLVE(request, out);
	if (request->rc) {
		DFUSE_REPLY_ERR(request, request->rc);
	} else {
		DFUSE_REPLY_ZERO(request);
	}

	dfuse_da_release(request->fsh->fh_da, request->ir_file);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result	= dfuse_release_cb,
};

static void
dfuse_release_priv(struct dfuse_file_handle *handle)
{
	struct dfuse_projection_info *fs_handle = handle->release_req.fsh;
	int rc;

	DFUSE_TRA_UP(&handle->release_req, handle, "release_req");

	DFUSE_TRA_INFO(&handle->release_req,
		       GAH_PRINT_STR, GAH_PRINT_VAL(handle->common.gah));

	handle->release_req.ir_api = &api;

	rc = dfuse_fs_send(&handle->release_req);
	if (rc) {
		D_GOTO(out_err, rc = EIO);
	}

	return;

out_err:
	if (handle->release_req.req) {
		DFUSE_REPLY_ERR(&handle->release_req, rc);
	} else {
		DFUSE_TRA_DOWN(&handle->release_req);
	}
	dfuse_da_release(fs_handle->fh_da, handle);
}

void
dfuse_cb_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_file_handle *handle = (struct dfuse_file_handle *)fi->fh;

	handle->release_req.req = req;
	dfuse_release_priv(handle);
}

void
dfuse_int_release(struct dfuse_file_handle *handle)
{

	dfuse_release_priv(handle);
}
