/* Copyright (C) 2017-2018 Intel Corporation
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

#include <fuse3/fuse.h>
#include "iof_common.h"
#include "ioc.h"
#include "log.h"

#define REQ_NAME request
#define POOL_NAME lookup_pool
#define TYPE_NAME entry_req
#include "ioc_ops.h"

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
	D_INIT_LIST_HEAD(&desc->ie->ie_fh_list);
	D_INIT_LIST_HEAD(&desc->ie->ie_ie_children);
	D_INIT_LIST_HEAD(&desc->ie->ie_ie_list);
	H_GAH_SET_VALID(desc->ie);
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
ioc_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
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
