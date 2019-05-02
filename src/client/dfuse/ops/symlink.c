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

#define REQ_NAME request
#define POOL_NAME symlink_da
#define TYPE_NAME entry_req
#include "dfuse_ops.h"

static const struct dfuse_request_api api;

void
dfuse_cb_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
		 const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct entry_req		*desc = NULL;
	int rc;

	DFUSE_TRA_INFO(fs_handle, "Parent:%lu '%s'", parent, name);
	DFUSE_REQ_INIT_REQ(desc, fs_handle, api, req, rc);
	if (rc)
		D_GOTO(err, rc);

	D_STRNDUP(desc->dest, link, 4096);
	if (!desc->dest)
		D_GOTO(err, rc = ENOMEM);

	desc->da = fs_handle->symlink_da;
	strncpy(desc->ie->name, name, NAME_MAX);
	desc->ie->parent = parent;

	desc->request.ir_inode_num = parent;

	rc = dfuse_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);
	return;
err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	if (desc) {
		DFUSE_TRA_DOWN(&desc->request);
		dfuse_da_release(fs_handle->symlink_da, desc);
	}
}
