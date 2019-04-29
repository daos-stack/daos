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

/* TODO: This will silently ignore some files if there are more than NUM_DIRENTS
 * entries in a directory
 */
#define NUM_DIRENTS 10

void
dfuse_cb_readdir(fuse_req_t req, struct dfuse_inode_entry *inode,
		 size_t size, off_t offset)
{
	size_t		b_offset = 0;
	daos_anchor_t	anchor = {};
	uint32_t	nr = NUM_DIRENTS;
	struct dirent	dirents[NUM_DIRENTS];
	struct stat	stbuf;
	int		i;
	void		*buf;
	int		ns;
	int		rc;

	DFUSE_TRA_DEBUG(inode, "Offset %zi",
			offset);

	D_ALLOC(buf, size);
	if (!buf) {
		D_GOTO(err, rc = ENOMEM);
	}

	rc = dfs_readdir(inode->ie_dfs->dffs_dfs, inode->obj, &anchor,
			 &nr, dirents);
	if (rc != -DER_SUCCESS) {
		D_GOTO(err, 0);
	}

	for (i = offset; i < nr; i++) {
		daos_obj_id_t	oid;
		mode_t		mode;
		dfs_obj_t	*obj;

		DFUSE_TRA_DEBUG(inode, "Filename '%s'",
				dirents[i].d_name);

		/* As this code needs to know the stat struct, including the
		 * inode number we need to do a lookup, then a stat from the
		 * object, rather than a stat on the path.
		 */
		rc = dfs_lookup_rel(inode->ie_dfs->dffs_dfs, inode->obj,
				    dirents[i].d_name, O_RDONLY, &obj, &mode);

		rc = dfs_ostat(inode->ie_dfs->dffs_dfs, obj, &stbuf);
		if (rc != -DER_SUCCESS) {
			D_GOTO(err, 0);
		}

		rc = dfs_obj2id(obj, &oid);
		if (rc != -DER_SUCCESS) {
			DFUSE_TRA_ERROR(inode, "no oid");
			dfs_release(obj);
			D_GOTO(err, rc = EIO);
		}

		stbuf.st_ino = (ino_t)oid.hi;

		dfs_release(obj);

		ns = fuse_add_direntry(req, buf + b_offset, size - b_offset,
				       dirents[i].d_name, &stbuf, i + 1);
		DFUSE_TRA_DEBUG(inode, "ns is %d",
				ns);
		if (ns > size - b_offset) {
			D_GOTO(out, 0);
		}
		b_offset += ns;
	}

out:
	DFUSE_TRA_DEBUG(req, "Returning %zi bytes", b_offset);

	rc = fuse_reply_buf(req, buf, b_offset);
	if (rc != 0) {
		DFUSE_TRA_ERROR(req, "fuse_reply_error returned %d", rc);
	}

	D_FREE(buf);
	return;

err:
	DFUSE_FUSE_REPLY_ERR(req, rc);

	D_FREE(buf);
}
