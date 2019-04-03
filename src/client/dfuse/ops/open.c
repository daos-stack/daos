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
dfuse_open_ll_cb(struct dfuse_request *request)
{
	struct dfuse_file_handle	*handle = container_of(request, struct dfuse_file_handle, open_req);
	struct dfuse_open_out	*out = crt_reply_get(request->rpc);
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

	IOC_REPLY_OPEN(&handle->open_req, fi);

	return false;

out_err:
	IOC_REPLY_ERR(request, request->rc);
	dfuse_pool_release(request->fsh->fh_pool, handle);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result	= dfuse_open_ll_cb,
	.gah_offset	= offsetof(struct dfuse_open_in, gah),
	.have_gah	= true,
};

void
dfuse_cb_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	struct dfuse_file_handle *handle = NULL;
	struct dfuse_open_in *in;
	int rc;

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

	handle = dfuse_pool_acquire(fs_handle->fh_pool);
	if (!handle) {
		D_GOTO(out_err, rc = ENOMEM);
	}
	IOF_TRACE_UP(handle, fs_handle, fs_handle->fh_pool->reg.name);
	IOF_TRACE_UP(&handle->open_req, handle, "open_req");

	handle->common.projection = &fs_handle->proj;
	handle->open_req.req = req;
	handle->open_req.ir_api = &api;
	handle->inode_num = ino;

	in = crt_req_get(handle->open_req.rpc);

	handle->open_req.ir_inode_num = ino;

	in->flags = fi->flags;
	IOF_TRACE_INFO(handle, "flags 0%o", fi->flags);

	LOG_FLAGS(handle, fi->flags);

	rc = dfuse_fs_send(&handle->open_req);
	if (rc) {
		D_GOTO(out_err, rc = EIO);
	}

	dfuse_pool_restock(fs_handle->fh_pool);

	return;
out_err:
	IOC_REPLY_ERR_RAW(handle, req, rc);

	if (handle)
		dfuse_pool_release(fs_handle->fh_pool, handle);
}
