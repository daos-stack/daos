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
readlink_cb(struct dfuse_request *request)
{
	struct dfuse_string_out *out = crt_reply_get(request->rpc);

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

static const struct dfuse_request_api api = {
	.on_result	= readlink_cb,
	.gah_offset	= offsetof(struct dfuse_gah_in, gah),
	.have_gah	= true,
};

void
dfuse_cb_readlink(fuse_req_t req, fuse_ino_t ino)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	struct dfuse_request		*request;
	int rc;
	int ret;

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

	/* Add a second ref as that's what the dfuse_fs_send() function
	 * expects.  In the case of failover the RPC might be completed,
	 * and a copy made the the RPC seen in statfs_cb might not be
	 * the same one as seen here.
	 */
	crt_req_addref(request->rpc);

	rc = dfuse_fs_send(request);
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
