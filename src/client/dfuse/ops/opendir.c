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

#define REQ_NAME open_req
#define POOL_NAME dh_pool
#define TYPE_NAME iof_dir_handle
#include "dfuse_ops.h"

#define STAT_KEY opendir

static bool
opendir_ll_cb(struct ioc_request *request)
{
	struct TYPE_NAME	*dh = CONTAINER(request);
	struct iof_opendir_out	*out = crt_reply_get(request->rpc);
	struct fuse_file_info	fi = {0};

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc == 0) {
		dh->gah = out->gah;
		dh->ep = dh->open_req.fsh->proj.grp->psr_ep;
		fi.fh = (uint64_t)dh;
		IOC_REPLY_OPEN(request, fi);
	} else {
		IOC_REPLY_ERR(request, request->rc);
		iof_pool_release(dh->open_req.fsh->dh_pool, dh);
	}
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= opendir_ll_cb,
	.gah_offset	= offsetof(struct iof_gah_in, gah),
	.have_gah	= true,
};

void
dfuse_cb_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct iof_projection_info	*fs_handle = fuse_req_userdata(req);
	struct TYPE_NAME		*dh = NULL;
	int rc;

	IOF_TRACE_INFO(fs_handle, "ino %lu", ino);
	IOC_REQ_INIT_REQ(dh, fs_handle, api, req, rc);
	if (rc)
		D_GOTO(err, rc);

	dh->open_req.ir_inode_num = ino;

	dh->inode_num = ino;

	rc = iof_fs_send(&dh->open_req);
	if (rc != 0)
		D_GOTO(err, 0);
	return;
err:
	if (dh)
		iof_pool_release(fs_handle->dh_pool, dh);

	IOC_REPLY_ERR_RAW(fs_handle, req, rc);
}
