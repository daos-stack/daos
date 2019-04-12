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

static const struct dfuse_request_api api = {
	.on_result	= dfuse_gen_cb,
};

void
dfuse_cb_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
	       struct fuse_file_info *fi)
{
	struct dfuse_file_handle	*handle = (struct dfuse_file_handle *)fi->fh;
	struct dfuse_projection_info	*fs_handle = handle->open_req.fsh;
	struct dfuse_request		*request;
	int rc;
	int ret;

	DFUSE_TRA_INFO(handle);

	D_ALLOC_PTR(request);
	if (!request) {
		D_GOTO(out_no_request, ret = ENOMEM);
	}

	DFUSE_REQUEST_INIT(request, fs_handle);
	DFUSE_REQUEST_RESET(request);

	request->req = req;
	request->ir_api = &api;
	request->ir_ht = RHS_FILE;
	request->ir_file = handle;

	rc = dfuse_fs_send(request);
	if (rc != 0) {
		D_GOTO(out_err, ret = EIO);
	}

	return;

out_no_request:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, ret);
	return;

out_err:
	DFUSE_REPLY_ERR(request, ret);
	D_FREE(request);
}
