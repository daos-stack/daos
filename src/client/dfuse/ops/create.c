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
dfuse_create_ll_cb(struct dfuse_request *request)
{
	struct dfuse_file_handle		*handle = container_of(request, struct dfuse_file_handle, creat_req);
	struct dfuse_projection_info	*fs_handle = request->fsh;
	struct dfuse_create_out		*out = crt_reply_get(request->rpc);
	struct fuse_file_info		fi = {0};
	struct fuse_entry_param		entry = {0};
	d_list_t			*rlink;
	bool				keep_ref = false;

	DFUSE_TRA_DEBUG(handle, "cci_rc %d rc %d err %d",
			request->rc, out->rc, out->err);

	DFUSE_REQUEST_RESOLVE(request, out);
	if (request->rc != 0) {
		D_GOTO(out_err, 0);
	}

	/* Create a new FI descriptor from the RPC reply */

	/* Reply to the create request with the GAH from the create call */

	entry.attr = out->stat;
	entry.generation = 1;
	entry.ino = entry.attr.st_ino;

	fi.fh = (uint64_t)handle;
	handle->common.gah = out->gah;
	handle->inode_num = entry.ino;
	handle->common.ep = request->rpc->cr_ep;

	/* Populate the inode table with the GAH from the duplicate file
	 * so that it can still be accessed after the file is closed
	 */
	handle->ie->gah = out->igah;
	handle->ie->stat = out->stat;
	DFUSE_TRA_UP(handle->ie, fs_handle, "inode");
	rlink = d_hash_rec_find_insert(&fs_handle->inode_ht,
				       &handle->ie->stat.st_ino,
				       sizeof(handle->ie->stat.st_ino),
				       &handle->ie->ie_htl);

	if (rlink == &handle->ie->ie_htl) {
		DFUSE_TRA_INFO(handle->ie, "New file %lu " GAH_PRINT_STR,
			       entry.ino, GAH_PRINT_VAL(out->gah));

		handle->ie = NULL;
		keep_ref = true;
	} else {
		/* This is an interesting, but not impossible case, although it
		 * could also represent a problem.
		 *
		 * One way of getting here would be to have another thread, with
		 * another RPC looking up the new file, and for the create RPC
		 * to create the file but the lookup RPC to observe the new file
		 * and the reply to arrive first.  Unlikely but possible.
		 *
		 * Another means of getting here would be if the filesystem was
		 * rapidly recycling inodes, and the local entry in cache was
		 * from an old generation.  This in theory should not happen
		 * as an entry in the hash table would mean the server held open
		 * the file, so even if it had been unlinked it would still
		 * exist and thus the inode was unlikely to be reused.
		 */
		DFUSE_TRA_INFO(request, "Existing file rlink %p %lu "
			       GAH_PRINT_STR, rlink, entry.ino,
			       GAH_PRINT_VAL(out->gah));
		ie_close(fs_handle, handle->ie);
	}

	DFUSE_REPLY_CREATE(request, entry, fi);
	return keep_ref;

out_err:
	DFUSE_REPLY_ERR(request, request->rc);
	dfuse_da_release(fs_handle->fh_da, handle);
	return false;
}

static const struct dfuse_request_api api = {
	.on_result = dfuse_create_ll_cb,
	.gah_offset = offsetof(struct dfuse_create_in, common.gah),
	.have_gah = true,
};

void
dfuse_cb_create(fuse_req_t req, fuse_ino_t parent, const char *name,
		mode_t mode, struct fuse_file_info *fi)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	struct dfuse_file_handle *handle = NULL;
	struct dfuse_create_in *in;
	int rc;

	/* O_LARGEFILE should always be set on 64 bit systems, and in fact is
	 * defined to 0 so IOF defines LARGEFILE to the value that O_LARGEFILE
	 * would otherwise be using and check that is set.
	 */
	if (!(fi->flags & LARGEFILE)) {
		DFUSE_TRA_INFO(req, "O_LARGEFILE required 0%o",
			       fi->flags);
		D_GOTO(out_err, rc = ENOTSUP);
	}

	/* Check for flags that do not make sense in this context.
	 */
	if (fi->flags & DFUSE_UNSUPPORTED_CREATE_FLAGS) {
		DFUSE_TRA_INFO(req, "unsupported flag requested 0%o",
			       fi->flags);
		D_GOTO(out_err, rc = ENOTSUP);
	}

	/* Check that only the flag for a regular file is specified */
	if ((mode & S_IFMT) != S_IFREG) {
		DFUSE_TRA_INFO(req, "unsupported mode requested 0%o",
			       mode);
		D_GOTO(out_err, rc = ENOTSUP);
	}

	handle = dfuse_da_acquire(fs_handle->fh_da);
	if (!handle)
		D_GOTO(out_err, rc = ENOMEM);

	DFUSE_TRA_UP(handle, fs_handle, fs_handle->fh_da->reg.name);
	DFUSE_TRA_UP(&handle->creat_req, handle, "creat_req");

	handle->common.projection = &fs_handle->proj;
	handle->creat_req.req = req;
	handle->creat_req.ir_api = &api;

	DFUSE_TRA_INFO(handle, "file '%s' flags 0%o mode 0%o", name, fi->flags,
		       mode);

	in = crt_req_get(handle->creat_req.rpc);

	handle->creat_req.ir_inode_num = parent;

	strncpy(in->common.name.name, name, NAME_MAX);
	in->mode = mode;
	in->flags = fi->flags;

	strncpy(handle->ie->name, name, NAME_MAX);
	handle->ie->parent = parent;

	LOG_FLAGS(handle, fi->flags);
	LOG_MODES(handle, mode);

	rc = dfuse_fs_send(&handle->creat_req);
	if (rc) {
		D_GOTO(out_err, rc = EIO);
	}

	dfuse_da_restock(fs_handle->fh_da);

	return;
out_err:
	DFUSE_REPLY_ERR_RAW(handle, req, rc);

	if (handle) {
		DFUSE_TRA_DOWN(&handle->creat_req);
		dfuse_da_release(fs_handle->fh_da, handle);
	}
}
