/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#define LOOP_COUNT 128

struct iterate_data {
	fuse_req_t			req;
	struct dfuse_inode_entry	*inode;
	struct dfuse_obj_hdl		*oh;
	size_t				size;
	size_t				fuse_size;
	size_t				b_off;
	uint8_t				stop;
};

int
filler_cb(dfs_t *dfs, dfs_obj_t *dir, const char name[], void *_udata)
{
	struct iterate_data	*udata = (struct iterate_data *)_udata;
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(udata->req);
	struct dfuse_obj_hdl	*oh = udata->oh;
	dfs_obj_t		*obj;
	daos_obj_id_t		oid;
	struct stat		stbuf = {0};
	int			ns = 0;
	int			rc;

	/*
	 * MSC - from fuse fuse_add_direntry: "From the 'stbuf' argument the
	 * st_ino field and bits 12-15 of the st_mode field are used. The other
	 * fields are ignored." So we only need to lookup the entry for the
	 * mode.
	 */

	rc = dfs_lookup_rel(dfs, dir, name, O_RDONLY, &obj, &stbuf.st_mode,
			    NULL);
	if (rc)
		return rc;

	rc = dfs_obj2id(obj, &oid);
	if (rc)
		D_GOTO(out, rc);

	rc = dfuse_lookup_inode(fs_handle, udata->inode->ie_dfs, &oid,
				&stbuf.st_ino);
	if (rc)
		D_GOTO(out, rc);

	/*
	 * If we are still within the fuse size limit (less than 4k - we have
	 * not gone beyond 4k and cur_off is still 0).
	 */
	if (oh->doh_cur_off == 0) {
		/** try to add the entry within the 4k size limit. */
		ns = fuse_add_direntry(udata->req, oh->doh_buf + udata->b_off,
				       udata->fuse_size - udata->b_off, name,
				       &stbuf, oh->doh_fuse_off + 1);

		/** if entry fits, increment the stream and fuse buf offset. */
		if (ns <= udata->fuse_size - udata->b_off) {
			udata->b_off += ns;
			oh->doh_fuse_off++;
			D_GOTO(out, rc = 0);
		}

		/*
		 * If entry does not fit within the 4k fuse imposed size, we now
		 * add the entry, but within the larger size limitation of the
		 * OH buffer (16k). But we also need to save the state of the
		 * current offset since this will not be returned in the current
		 * readdir call but will be consumed in subsequent calls.
		 */
		oh->doh_start_off[oh->doh_idx] = udata->b_off;
		oh->doh_cur_off = udata->b_off;
		oh->doh_dir_off[oh->doh_idx] = oh->doh_fuse_off;

		ns = fuse_add_direntry(udata->req, oh->doh_buf + udata->b_off,
				       udata->size - udata->b_off, name, &stbuf,
				       oh->doh_dir_off[oh->doh_idx] + 1);

		/** Entry should fit now */
		D_ASSERT(ns <= udata->size - udata->b_off);
		oh->doh_cur_off += ns;
		oh->doh_dir_off[oh->doh_idx]++;

		/** no need to issue further dfs_iterate() calls. */
		udata->stop = 1;
		D_GOTO(out, rc = 0);
	}

insert:
	/*
	 * At this point, we are already adding to the buffer within the large
	 * size limitation where it will be consumed in future readdir calls.
	 */
	ns = fuse_add_direntry(udata->req, oh->doh_buf + oh->doh_cur_off,
			       udata->size - oh->doh_cur_off, name, &stbuf,
			       oh->doh_dir_off[oh->doh_idx] + 1);
	/*
	 * In the case where the OH handle does not fit, we still need to add
	 * the entry since DFS already enumerated it. So, realloc to fit the
	 * entries that were already enumerated and insert again.
	 */
	if (ns > udata->size - oh->doh_cur_off) {
		udata->size = udata->size * 2;
		oh->doh_buf = realloc(oh->doh_buf, udata->size);
		if (oh->doh_buf == NULL)
			D_GOTO(out, rc = -ENOMEM);
		goto insert;
	}

	/** update the end offset in the OH buffer */
	oh->doh_cur_off += ns;

	/*
	 * Since fuse can process a max of 4k size of entries, it's mostly the
	 * case that the offset where the last entry that can fit in a 4k buf
	 * size is not aligned at the 4k bnoundary. So we need to keep track of
	 * offsets before the last entry that exceeds 4k in the buffer size for
	 * further calls to readdir to consume.
	 */
	if (oh->doh_cur_off - oh->doh_start_off[oh->doh_idx] >
	    udata->fuse_size) {
		oh->doh_idx++;
		oh->doh_dir_off[oh->doh_idx] = oh->doh_dir_off[oh->doh_idx - 1];
		oh->doh_start_off[oh->doh_idx] = oh->doh_cur_off - ns;
	}
	oh->doh_dir_off[oh->doh_idx]++;

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
	size_t			buf_size;
	struct iterate_data	udata;
	int			rc;

	if (offset < 0)
		D_GOTO(err, rc = EINVAL);

	D_ASSERT(oh);

	/*
	 * the DFS size should be less than what we want in fuse to account for
	 * the fuse metadata for each entry, so just use 1/2 for now.
	 */
	buf_size = size * READDIR_BLOCKS / 2;

	if (offset == 0) {
		/*
		 * if starting from the begnning, reset the anchor attached to
		 * the open handle.
		 */
		memset(&oh->doh_anchor, 0, sizeof(oh->doh_anchor));

		/** also reset dir stream and buffer offsets */
		oh->doh_fuse_off = 0;
		oh->doh_cur_off = 0;
		oh->doh_idx = 0;
	} else if (offset != oh->doh_fuse_off) {
		uint32_t num, keys;

		/*
		 * otherwise we are starting at an earlier offset where we left
		 * off on last readdir, so restart by first enumerating that
		 * many entries. This is the telldir/seekdir use case.
		 */

		memset(&oh->doh_anchor, 0, sizeof(oh->doh_anchor));
		num = (uint32_t)offset;
		keys = 0;
		while (num) {
			rc = dfs_iterate(oh->doh_dfs, oh->doh_obj,
					 &oh->doh_anchor, &num, buf_size,
					 NULL, NULL);
			if (rc)
				D_GOTO(err, rc);

			if (daos_anchor_is_eof(&oh->doh_anchor))
				return;

			keys += num;
			num = offset - keys;
		}
		/** set the dir stream to 'offset' elements enumerated */
		oh->doh_fuse_off = offset;

		/** discard everything in the OH buffers we have cached. */
		oh->doh_cur_off = 0;
		oh->doh_idx = 0;
	}

	/*
	 * On subsequent calls to readdir, if there was anything to consume on
	 * the buffer attached to the dir handle from the previous call, either
	 * consume a 4k block or whatever remains.
	 */
	if (offset && oh->doh_cur_off) {
		/*
		 * if remaining does not fit in the fuse buf, return a 4k (or
		 * less block) and advance the idx tracking number of blocks
		 * consumed.
		 */
		if (size < oh->doh_cur_off - oh->doh_start_off[oh->doh_idx]) {
			fuse_reply_buf(req, oh->doh_buf +
				       oh->doh_start_off[oh->doh_idx],
				       oh->doh_start_off[oh->doh_idx + 1] -
				       oh->doh_start_off[oh->doh_idx]);
			oh->doh_fuse_off = oh->doh_dir_off[oh->doh_idx];
			oh->doh_idx++;
			return;
		}

		/** otherwise return everything left since it should fit. */
		fuse_reply_buf(req,
			       oh->doh_buf + oh->doh_start_off[oh->doh_idx],
			       oh->doh_cur_off -
			       oh->doh_start_off[oh->doh_idx]);

		oh->doh_fuse_off = oh->doh_dir_off[oh->doh_idx];

		/** reset buffer offset counters to reuse the OH buffer. */
		oh->doh_cur_off = 0;
		oh->doh_idx = 0;
		return;
	}

	/** Allocate readdir buffer on OH if it has not been allocated before */
	if (oh->doh_buf == NULL) {
		/** buffer will be freed when this oh is closed */
		D_ALLOC(oh->doh_buf, buf_size);
		if (!oh->doh_buf)
			D_GOTO(err, rc = ENOMEM);
	}

	udata.req = req;
	udata.size = buf_size;
	udata.fuse_size = size;
	udata.b_off = 0;
	udata.inode = inode;
	udata.oh = oh;
	udata.stop = 0;

	while (!daos_anchor_is_eof(&oh->doh_anchor)) {
		/** should not be here if we exceeded the fuse 4k buf size */
		D_ASSERT(oh->doh_cur_off == 0);

		rc = dfs_iterate(oh->doh_dfs, oh->doh_obj, &oh->doh_anchor, &nr,
				 buf_size - udata.b_off, filler_cb, &udata);

		/** if entry does not fit in buffer, just return */
		if (rc == E2BIG)
			break;
		/** otherwise a different error occurred */
		if (rc)
			D_GOTO(err, rc);

		/** if the fuse buffer is full, break enumeration */
		if (udata.stop)
			break;
	}

	oh->doh_idx = 0;
	fuse_reply_buf(req, oh->doh_buf, udata.b_off);
	return;

err:
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
}
