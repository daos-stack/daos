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

#include <fuse3/fuse.h>
#include "dfuse_common.h"
#include "dfuse.h"

#define REQ_NAME request
#define POOL_NAME lookup_pool
#define TYPE_NAME entry_req
#include "dfuse_ops.h"

bool
iof_entry_cb(struct ioc_request *request)
{
	struct entry_req		*desc = container_of(request, struct entry_req, request);
	struct iof_projection_info	*fs_handle = desc->request.fsh;
	struct iof_entry_out		*out = crt_reply_get(request->rpc);
	struct fuse_entry_param		entry = {0};
	d_list_t			*rlink;
	bool				keep_ref = false;

	IOC_REQUEST_RESOLVE(request, out);
	if (request->rc)
		D_GOTO(out, 0);

	entry.attr = out->stat;
	entry.generation = 1;
	entry.ino = entry.attr.st_ino;

	desc->ie->gah = out->gah;
	desc->ie->stat = out->stat;
	IOF_TRACE_UP(desc->ie, fs_handle, "inode");
	rlink = d_hash_rec_find_insert(&fs_handle->inode_ht,
				       &desc->ie->stat.st_ino,
				       sizeof(desc->ie->stat.st_ino),
				       &desc->ie->ie_htl);

	if (rlink == &desc->ie->ie_htl) {
		IOF_TRACE_INFO(desc->ie, "New file %lu " GAH_PRINT_STR,
			       entry.ino, GAH_PRINT_VAL(out->gah));
		desc->ie = NULL;
		keep_ref = true;
	} else {
		/* The lookup has resulted in an existing file, so reuse that
		 * entry, drop the inode in the lookup descriptor and do not
		 * keep a reference on the parent.
		 * Note that this function will be called with a reference on
		 * the parent anyway, so keep that one, but drop one in the call
		 * to ie_close().
		 */
		IOF_TRACE_INFO(container_of(rlink, struct ioc_inode_entry, ie_htl),
			       "Existing file %lu " GAH_PRINT_STR,
			       entry.ino, GAH_PRINT_VAL(out->gah));
		atomic_fetch_sub(&desc->ie->ie_ref, 1);
		keep_ref = true;
		ie_close(fs_handle, desc->ie);
	}

	IOC_REPLY_ENTRY(request, entry);
	iof_pool_release(desc->pool, desc);
	return keep_ref;
out:
	IOC_REPLY_ERR(request, request->rc);
	iof_pool_release(desc->pool, desc);
	return false;
}

static const struct ioc_request_api api = {
	.on_result	= iof_entry_cb,
	.gah_offset	= offsetof(struct iof_gah_string_in, gah),
	.have_gah	= true,
};

#define STAT_KEY lookup

void
dfuse_cb_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct iof_projection_info	*fs_handle = fuse_req_userdata(req);
	struct TYPE_NAME		*desc = NULL;
	struct iof_gah_string_in	*in;
	int rc;

	IOF_TRACE_INFO(fs_handle, "Parent:%lu '%s'", parent, name);
	IOC_REQ_INIT_REQ(desc, fs_handle, api, req, rc);
	if (rc)
		D_GOTO(err, rc);

	IOF_TRACE_INFO(desc, "ie %p", &desc->ie);

	desc->request.ir_inode_num = parent;

	in = crt_req_get(desc->request.rpc);
	strncpy(in->name.name, name, NAME_MAX);
	strncpy(desc->ie->name, name, NAME_MAX);
	desc->ie->parent = parent;
	desc->pool = fs_handle->lookup_pool;

	rc = iof_fs_send(&desc->request);
	if (rc != 0)
		D_GOTO(err, 0);
	return;
err:
	if (desc)
		iof_pool_release(fs_handle->lookup_pool, desc);
	IOC_REPLY_ERR_RAW(fs_handle, req, rc);
}
