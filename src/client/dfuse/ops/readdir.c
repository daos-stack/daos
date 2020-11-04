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

/* Maximum number of dentries to read at one time. */
#define READDIR_MAX_COUNT 1024

/* Initial number of dentries to read when doing readdirplus */
#define READDIR_PLUS_COUNT 26
/* Initial number of dentries to read */
#define READDIR_BASE_COUNT 128

/* Marker offset used to signify end-of-directory */
#define DFUSE_READDIR_EOD (1LL << 63)

/* Offset of the first file, allow two entries for . and .. */
#define OFFSET_BASE 2

struct iterate_data {
	off_t			id_base_offset;
	int			id_index;
	struct dfuse_obj_hdl	 *id_oh;
};

static int
filler_cb(dfs_t *dfs, dfs_obj_t *dir, const char name[], void *arg)
{
	struct iterate_data *idata = arg;
	struct dfuse_readdir_entry *dre = &idata->id_oh->doh_dre[idata->id_index];

	DFUSE_TRA_DEBUG(idata->id_oh, "Adding at index %d offset %ld '%s'",
			idata->id_index,
			idata->id_base_offset + idata->id_index,
			name);

	strncpy(dre->dre_name, name, NAME_MAX);
	dre->dre_offset = idata->id_base_offset + idata->id_index;
	dre->dre_next_offset = idata->id_base_offset + idata->id_index + 1;
	idata->id_index++;

	return 0;
}

static int
fetch_dir_entries(struct dfuse_obj_hdl *oh, off_t offset, int to_fetch,
		  bool *eod)
{
	struct iterate_data	idata = {};
	uint32_t		count = to_fetch;
	int			rc;

	idata.id_base_offset = offset;
	idata.id_oh = oh;

	DFUSE_TRA_DEBUG(oh, "Fetching new entries at offset %ld", offset);

	rc = dfs_iterate(oh->doh_dfs, oh->doh_obj, &oh->doh_anchor, &count,
			 (NAME_MAX + 1) * count, filler_cb, &idata);

	oh->doh_anchor_index += count;
	oh->doh_dre_index = 0;
	oh->doh_dre_last_index = count;

	DFUSE_TRA_DEBUG(oh, "Added %d entries, anchor_index %d rc %d",
			count, oh->doh_anchor_index, rc);

	if (count) {
		if (daos_anchor_is_eof(&oh->doh_anchor))
			oh->doh_dre[count - 1].dre_next_offset = DFUSE_READDIR_EOD;
	} else {
		*eod = true;
	}

	return rc;
}

static int
create_entry(struct dfuse_projection_info *fs_handle,
	     struct dfuse_inode_entry *parent,
	     struct fuse_entry_param *entry,
	     dfs_obj_t *obj,
	     char *name,
	     d_list_t **rlinkp)
{
	struct dfuse_inode_entry	*ie;
	d_list_t *rlink;
	int rc = 0;

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(out, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	ie->ie_obj = obj;
	ie->ie_stat = entry->attr;

	entry->attr_timeout = parent->ie_dfs->dfs_attr_timeout;
	entry->entry_timeout = parent->ie_dfs->dfs_attr_timeout;

	entry->generation = 1;
	entry->ino = entry->attr.st_ino;

	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;

	/* TODO:
	 * See if we need to check for UNS entry point here.  It may be that
	 * we can just return the inode here and if it gets looked up again
	 * that the UNS code will work at that point, potentially giving it
	 * a new inode number but it may be that we need to handle it here.
	 */

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	atomic_store_relaxed(&ie->ie_ref, 1);

	rlink = d_hash_rec_find_insert(&fs_handle->dpi_iet,
				       &ie->ie_stat.st_ino,
				       sizeof(ie->ie_stat.st_ino),
				       &ie->ie_htl);

	if (rlink != &ie->ie_htl) {
		struct dfuse_inode_entry *inode;

		inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		/* The lookup has resulted in an existing file, so reuse that
		 * entry, drop the inode in the lookup descriptor and do not
		 * keep a reference on the parent.
		 */

		/* Update the existing object with the new name/parent */

		DFUSE_TRA_DEBUG(inode,
				"Maybe updating parent inode %lu dfs_root %lu",
				entry->ino, ie->ie_dfs->dfs_root);

		if (ie->ie_stat.st_ino == ie->ie_dfs->dfs_root) {
			DFUSE_TRA_DEBUG(inode, "Not updating parent");
		} else {
			rc = dfs_update_parent(inode->ie_obj, ie->ie_obj,
					       ie->ie_name);
			if (rc != 0)
				DFUSE_TRA_ERROR(inode,
						"dfs_update_parent() failed %d",
						rc);
		}
		inode->ie_parent = ie->ie_parent;
		strncpy(inode->ie_name, ie->ie_name, NAME_MAX + 1);

		atomic_fetch_sub_relaxed(&ie->ie_ref, 1);
		ie->ie_parent = 0;
		ie->ie_root = 0;
		ie_close(fs_handle, ie);
		ie = inode;
	}

	*rlinkp = rlink;
out:
	return rc;
}

static inline void
dfuse_readdir_reset(struct dfuse_obj_hdl *oh)
{
	memset(&oh->doh_anchor, 0, sizeof(oh->doh_anchor));
	memset(oh->doh_dre, 0, sizeof(*oh->doh_dre) * READDIR_MAX_COUNT);
	oh->doh_dre_index = 0;
	oh->doh_dre_last_index = 0;
	oh->doh_anchor_index = 0;
}

void
dfuse_cb_readdir(fuse_req_t req, struct dfuse_obj_hdl *oh,
		 size_t size, off_t offset, bool plus)
{
	struct dfuse_projection_info *fs_handle = fuse_req_userdata(req);
	char			*reply_buff;
	off_t			buff_offset = 0;
	int			added = 0;
	int			rc = 0;
	int			large_fetch = true;

	if (offset == DFUSE_READDIR_EOD) {
		DFUSE_TRA_DEBUG(oh, "End of directory %lx", offset);
		DFUSE_REPLY_BUF(oh, req, NULL, (size_t)0);
		return;
	}

	D_ALLOC(reply_buff, size);
	if (reply_buff == NULL)
		D_GOTO(err, rc = ENOMEM);

	if (oh->doh_dre == NULL) {
		D_ALLOC_ARRAY(oh->doh_dre, READDIR_MAX_COUNT);
		if (oh->doh_dre == NULL)
			D_GOTO(err, rc = ENOMEM);
	}

	/* if starting from the beginning, reset the anchor attached to
	 * the open handle.
	 */
	if (offset == 0)
		dfuse_readdir_reset(oh);

	DFUSE_TRA_DEBUG(oh, "plus %d offset %ld idx %d idx_offset %ld",
			plus, offset, oh->doh_dre_index,
			oh->doh_dre[oh->doh_dre_index].dre_offset);

	/* If there is an offset, and either there is no current offset, or it's
	 * different then seek
	 */
	if (offset &&
	    oh->doh_dre[oh->doh_dre_index].dre_offset != offset &&
	    oh->doh_anchor_index + OFFSET_BASE != offset) {
		uint32_t num;

		/*
		 * otherwise we are starting at an earlier offset where we left
		 * off on last readdir, so restart by first enumerating that
		 * many entries. This is the telldir/seekdir use case.
		 */

		DFUSE_TRA_INFO(oh,
			       "Seeking from offset %ld(%d) to %ld (index %d)",
			       oh->doh_dre[oh->doh_dre_index].dre_offset,
			       oh->doh_anchor_index,
			       offset,
			       oh->doh_dre_index);

		dfuse_readdir_reset(oh);
		num = (uint32_t)offset - OFFSET_BASE;
		while (num) {
			rc = dfs_iterate(oh->doh_dfs, oh->doh_obj,
					 &oh->doh_anchor, &num,
					 (NAME_MAX + 1) * num,
					 NULL, NULL);
			if (rc)
				D_GOTO(err, rc);

			if (daos_anchor_is_eof(&oh->doh_anchor)) {
				dfuse_readdir_reset(oh);
				D_GOTO(reply, 0);
			}

			oh->doh_anchor_index += num;

			num = offset - OFFSET_BASE - oh->doh_anchor_index;
		}
		large_fetch = false;
	}

	if (offset == 0)
		offset = OFFSET_BASE;

	if (offset < 1024)
		large_fetch = false;

	do {
		int i;
		bool fetched = false;

		if (oh->doh_dre_last_index == 0) {
			uint32_t to_fetch;
			bool eod = false;

			D_ASSERT(offset != oh->doh_dre[oh->doh_dre_index].dre_offset);

			if (large_fetch)
				to_fetch = READDIR_MAX_COUNT;
			else if (plus)
				to_fetch = READDIR_PLUS_COUNT - added;
			else
				to_fetch = READDIR_BASE_COUNT - added;

			rc = fetch_dir_entries(oh, offset, to_fetch, &eod);
			if (rc != 0)
				D_GOTO(err, 0);

			if (eod)
				D_GOTO(reply, 0);

			fetched = true;
		} else {
			D_ASSERT(offset == oh->doh_dre[oh->doh_dre_index].dre_offset);
		}

		DFUSE_TRA_DEBUG(oh, "processing offset %ld", offset);

		/* Populate dir */
		for (i = oh->doh_dre_index; i < oh->doh_dre_last_index ; i++) {
			struct dfuse_readdir_entry	*dre = &oh->doh_dre[i];
			struct stat			stbuf = {0};
			dfs_obj_t			*obj;
			size_t				written;

			D_ASSERT(dre->dre_offset != 0);

			oh->doh_dre_index += 1;

			DFUSE_TRA_DEBUG(oh, "Checking offset %ld next %ld '%s'",
					dre->dre_offset,
					dre->dre_next_offset,
					dre->dre_name);

			rc = dfs_lookup_rel(oh->doh_dfs, oh->doh_obj,
					    dre->dre_name, O_RDONLY, &obj,
					    &stbuf.st_mode,
					    plus ? &stbuf : NULL);
			if (rc == ENOENT) {
				DFUSE_TRA_DEBUG(oh, "File does not exist");
				continue;
			} else if (rc != 0) {
				DFUSE_TRA_DEBUG(oh, "Problem finding file %d",
						rc);
				D_GOTO(reply, 0);
			}

			rc = dfuse_lookup_inode_from_obj(fs_handle,
							 oh->doh_ie->ie_dfs,
							 obj,
							 &stbuf.st_ino);
			if (rc) {
				DFUSE_TRA_DEBUG(oh, "Problem looking up file");
				dfs_release(obj);
				D_GOTO(reply, 0);
			}

			if (plus) {
				struct fuse_entry_param	entry = {0};
				d_list_t *rlink;

				entry.attr = stbuf;

				rc = create_entry(fs_handle,
						  oh->doh_ie,
						  &entry,
						  obj,
						  dre->dre_name,
						  &rlink);
				if (rc != 0)
					D_GOTO(reply, rc);

				written = fuse_add_direntry_plus(req,
								 &reply_buff[buff_offset],
								 size - buff_offset,
								 dre->dre_name,
								 &entry,
								 dre->dre_next_offset);
				if (written > size - buff_offset) {
					d_hash_rec_decref(&fs_handle->dpi_iet,
							  rlink);
				}

			} else {
				dfs_release(obj);

				written = fuse_add_direntry(req,
							    &reply_buff[buff_offset],
							    size - buff_offset,
							    dre->dre_name,
							    &stbuf,
							    dre->dre_next_offset);
			}
			if (written > size - buff_offset) {
				DFUSE_TRA_DEBUG(oh, "Buffer is full");
				oh->doh_dre_index -= 1;
				D_GOTO(reply, 0);
			}

			/* This entry has been added to the buffer so mark it as
			 * empty
			 */
			dre->dre_offset = 0;
			buff_offset += written;
			added++;
			offset++;

			if (dre->dre_next_offset == DFUSE_READDIR_EOD) {
				DFUSE_TRA_DEBUG(oh, "Reached end of directory");
				dfuse_readdir_reset(oh);
				D_GOTO(reply, 0);
			}
		}
		if (oh->doh_dre_index == oh->doh_dre_last_index) {
			oh->doh_dre_index = 0;
			oh->doh_dre_last_index = 0;
		}
		if (fetched && !large_fetch)
			break;
	} while (true);

reply:

	if (rc)
		DFUSE_TRA_WARNING(oh, "Replying %d %d", added, rc);
	else
		DFUSE_TRA_DEBUG(oh, "Replying %d %d", added, rc);

	if (added == 0 && rc != 0)
		D_GOTO(err, 0);

	DFUSE_REPLY_BUF(oh, req, reply_buff, buff_offset);
	D_FREE(reply_buff);

	return;

err:
	dfuse_readdir_reset(oh);
	DFUSE_REPLY_ERR_RAW(oh, req, rc);
	D_FREE(reply_buff);
}
