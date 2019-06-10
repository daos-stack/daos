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

/* TODO: This implementation is not complete, in particular it does not
 * correctly handle calls where offset != 0, which potentially generates
 * incorrect results if the number of files is greater than can be observed in
 * one call (approx 25).
 *
 * If the filesystem is only modified from one client then the results should
 * however be correct.
 */

#define LOOP_COUNT 10

void
dfuse_cb_readdir(fuse_req_t req, struct dfuse_inode_entry *inode,
		 size_t size, off_t offset)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	size_t				b_offset = 0;
	daos_anchor_t			anchor = {0};
	uint32_t			nr = LOOP_COUNT;
	struct dirent			dirents[LOOP_COUNT];
	int				next_offset = 0;
	int				i;
	void				*buf = NULL;
	int				ns;
	int				rc;

	DFUSE_TRA_DEBUG(inode, "Offset %zi",
			offset);

	/* TODO:
	 * To do this properly we need a way to convert from a offset into
	 * a daos_anchor_t, in all cases.
	 *
	 * For now simply consume the first "offset" entries in a directory and
	 * start iterating from there.  This will be correct only if the
	 * directory contents aren't modified between calls.
	 */
	if (offset != 0) {
		uint32_t count = nr;

		DFUSE_TRA_ERROR(inode,
				"Unable to correctly handle non-zero offsets");

		while (offset > 0) {

			if (offset < count) {
				count = offset;
			}

			rc = dfs_readdir(inode->ie_dfs->dffs_dfs, inode->ie_obj,
					 &anchor, &count, dirents);
			if (rc) {
				D_GOTO(err, rc = -rc);
			}
			offset -= count;
			next_offset += count;
		}
	}

	D_ALLOC(buf, size);
	if (!buf) {
		D_GOTO(err, rc = ENOMEM);
	}

	while (!daos_anchor_is_eof(&anchor)) {

		rc = dfs_readdir(inode->ie_dfs->dffs_dfs, inode->ie_obj,
				 &anchor, &nr, dirents);
		if (rc) {
			D_GOTO(err_or_buf, rc = -rc);
		}

		for (i = 0; i < nr; i++) {
			struct dfuse_inode_entry *ie = NULL;
			d_list_t		*rlink;
			struct fuse_entry_param entry = {};
			daos_obj_id_t	oid;
			mode_t		mode;

			DFUSE_TRA_DEBUG(inode, "Filename '%s'",
					dirents[i].d_name);

			/* Make an initial call to add_direntry() to query the
			 * size required.  This allows us to exit at this point
			 * if there is no buffer space, before opening the
			 * object, and allocating an inode for it.  It also
			 * avoids an error path later on, where there is already
			 * a reference taken on the inode entry.
			 *
			 * fuse_add_direntry_plus() accepts NULL values for buf
			 * to allow exactly this, assume that NULL/0 is also
			 * accepted for other values than name as well based on
			 * a reading of the source code.
			 */
			ns = fuse_add_direntry_plus(req,
						    NULL,
						    0,
						    dirents[i].d_name,
						    NULL,
						    0);
			if (ns > size - b_offset) {
				D_GOTO(out, 0);
			}

			D_ALLOC_PTR(ie);
			if (!ie) {
				D_GOTO(err_or_buf, rc = ENOMEM);
			}

			ie->ie_parent = inode->ie_stat.st_ino;
			ie->ie_dfs = inode->ie_dfs;

			strncpy(ie->ie_name, dirents[i].d_name, NAME_MAX);
			ie->ie_name[NAME_MAX] = '\0';
			atomic_fetch_add(&ie->ie_ref, 1);

			/* As this code needs to know the stat struct, including
			 * the inode number we need to do a lookup, then a stat
			 * from the object, rather than a stat on the path.
			 */
			rc = dfs_lookup_rel(inode->ie_dfs->dffs_dfs,
					    inode->ie_obj, dirents[i].d_name,
					    O_RDONLY, &ie->ie_obj, &mode);
			if (rc) {
				D_FREE(ie);
				D_GOTO(err_or_buf, rc = -rc);
			}

			rc = dfs_ostat(inode->ie_dfs->dffs_dfs, ie->ie_obj,
				       &ie->ie_stat);
			if (rc) {
				dfs_release(ie->ie_obj);
				D_FREE(ie);
				D_GOTO(err_or_buf, rc = -rc);
			}

			rc = dfs_obj2id(ie->ie_obj, &oid);
			if (rc) {
				DFUSE_TRA_ERROR(inode, "no oid");
				dfs_release(ie->ie_obj);
				D_FREE(ie);
				D_GOTO(err_or_buf, rc = -rc);
			}

			rc = dfuse_lookup_inode(fs_handle,
						inode->ie_dfs,
						&oid,
						&ie->ie_stat.st_ino);
			if (rc) {
				DFUSE_TRA_ERROR(inode, "no ino");
				dfs_release(ie->ie_obj);
				D_FREE(ie);
				D_GOTO(err_or_buf, rc);
			}

			entry.attr = ie->ie_stat;
			entry.generation = 1;
			entry.ino = entry.attr.st_ino;

			/* Add the new entry to the inode table, or take an
			 * additional reference if it's already present.
			 *
			 * It's not clear if the new dentry is supposed to take
			 * a reference on the parent or not, so for now this
			 * code does not.
			 *
			 * TODO: Verify the parent inode count is correct after
			 * this.
			 */
			rlink = d_hash_rec_find_insert(&fs_handle->dfpi_iet,
						       &ie->ie_stat.st_ino,
						       sizeof(ie->ie_stat.st_ino),
						       &ie->ie_htl);

			if (rlink != &ie->ie_htl) {
				/* The lookup has resulted in an existing file,
				 * so reuse that entry, drop the inode in the
				 * lookup descriptor and do not keep a reference
				 * on the parent.
				 */
				atomic_fetch_sub(&ie->ie_ref, 1);
				ie->ie_parent = 0;

				ie_close(fs_handle, ie);
			}

			/* This code does not use rlink at this point, and ie
			 * may no longer be valid so do not access that either,
			 * however the information required is already copied
			 * into entry so simply use that.
			 */
			ie = NULL;

			ns = fuse_add_direntry_plus(req,
						    buf + b_offset,
						    size - b_offset,
						    dirents[i].d_name,
						    &entry,
						    ++next_offset);
			DFUSE_TRA_DEBUG(inode, "ns is %d",
					ns);
			/* Assert here rather than handle this case, see comment
			 * on previous call for fuse_add_direntry_plus() to
			 * see why this cannot happen
			 */
			D_ASSERTF(ns <= size - b_offset, "Buffer size error");
			b_offset += ns;
		}
	}

out:
	DFUSE_TRA_DEBUG(req, "Returning %zi bytes", b_offset);

	rc = fuse_reply_buf(req, buf, b_offset);
	if (rc != 0) {
		DFUSE_TRA_ERROR(req, "fuse_reply_buf() failed: (%d)", rc);
	}

	D_FREE(buf);
	return;

err_or_buf:
	/* Handle error cases where the buffer may be partially filled, in this
	 * case the contents of the buffer may be discarded but there is already
	 * a reference taken in the inode entry hash table for the contents, so
	 * return the buffer is there are entries, or return the error code if
	 * the buffer is empty.
	 */
	if (b_offset != 0) {
		D_GOTO(out, 0);
	}

err:
	DFUSE_FUSE_REPLY_ERR(req, rc);

	D_FREE(buf);
}
