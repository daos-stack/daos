/**
 * (C) Copyright 2019 Intel Corporation.
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

#define LOOP_COUNT 10

struct iterate_data {
	fuse_req_t			req;
	struct dfuse_inode_entry	*inode;
	struct dfuse_obj_hdl		*oh;
	void				*buf;
	size_t				size;
	size_t				b_offset;
};

int
filler_cb(dfs_t *dfs, dfs_obj_t *dir, const char name[], void *_udata)
{
	struct iterate_data	*udata = (struct iterate_data *)_udata;
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(udata->req);
	dfs_obj_t		*obj;
	daos_obj_id_t		oid;
	struct stat		stbuf = {0};
	int			ns;
	int			rc;

	/*
	 * MSC - from fuse fuse_add_direntry: "From the 'stbuf' argument the
	 * st_ino field and bits 12-15 of the st_mode field are used. The other
	 * fields are ignored." So we only need to lookup the entry for the
	 * mode.
	 */
	DFUSE_TRA_DEBUG(udata->inode, "Adding entry name '%s'", name);
	rc = dfs_lookup_rel(dfs, dir, name, O_RDONLY, &obj, &stbuf.st_mode);
	if (rc)
		return rc;

	rc = dfs_obj2id(obj, &oid);
	if (rc)
		D_GOTO(out, rc);

	rc = dfuse_lookup_inode(fs_handle, udata->inode->ie_dfs, &oid,
				&stbuf.st_ino);
	if (rc)
		D_GOTO(out, rc);

	ns = fuse_add_direntry(udata->req, udata->buf + udata->b_offset,
			       udata->size - udata->b_offset, name, &stbuf,
			       (off_t)(&udata->oh->doh_anchor));

	DFUSE_TRA_DEBUG(udata->inode, "add direntry: size = %ld, return %d\n",
			udata->size - udata->b_offset, ns);

	/*
	 * This should be true since we accounted for the fuse_dirent size when
	 * we started dfs_iterate().
	 */
	D_ASSERT(ns <= udata->size - udata->b_offset);
	udata->b_offset += ns;

out:
	dfs_release(obj);
	/* we return the negative errno back to DFS */
	return rc;
}

void
dfuse_cb_readdir(fuse_req_t req, struct dfuse_inode_entry *inode,
		 size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct dfuse_obj_hdl	*oh = (struct dfuse_obj_hdl *)fi->fh;
	uint32_t		nr = LOOP_COUNT;
	void			*buf = NULL;
	size_t			buf_size, loop_size;
	struct iterate_data	*udata = NULL;
	int			i = 1;
	int			rc;

	DFUSE_TRA_DEBUG(inode, "Offset %zi", offset);

	if (offset < 0)
		D_GOTO(err, rc = EINVAL);

	D_ASSERT(oh);

	if (offset == 0) {
		memset(&oh->doh_anchor, 0, sizeof(oh->doh_anchor));
	} else {
		if (offset != (off_t)&oh->doh_anchor)
			D_GOTO(err, rc = EIO);
	}

	D_ALLOC(buf, size);
	if (!buf)
		D_GOTO(err, rc = ENOMEM);

	D_ALLOC_PTR(udata);
	if (!udata)
		D_GOTO(err, rc = ENOMEM);

	udata->req = req;
	udata->buf = buf;
	udata->size = size;
	udata->b_offset = 0;
	udata->inode = inode;
	udata->oh = oh;

	/** account for the size to hold the fuse dirent + padding */
	loop_size = LOOP_COUNT * sizeof(uint64_t) * 4;

	while (!daos_anchor_is_eof(&oh->doh_anchor)) {
		/** while the size still fits, continue enumerating */
		if (size <= loop_size * i)
			D_GOTO(out, 0);

		buf_size = size - (loop_size * i);
		rc = dfs_iterate(oh->doh_dfs, oh->doh_obj, &oh->doh_anchor,
				 &nr, buf_size, filler_cb, udata);
		/** if entry does not fit in buffer, just return */
		if (rc == -E2BIG)
			D_GOTO(out, 0);
		/** otherwise a different error occured */
		if (rc)
			D_GOTO(err, 0);
		i++;
	}

out:
	DFUSE_TRA_DEBUG(req, "Returning %zi bytes", udata->b_offset);
	fuse_reply_buf(req, buf, udata->b_offset);
	D_FREE(buf);
	return;
err:
	DFUSE_FUSE_REPLY_ERR(req, rc);
	D_FREE(buf);
}
