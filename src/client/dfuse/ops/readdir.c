/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#include "daos_uns.h"

/* Initial number of dentries to read when doing readdirplus */
#define READDIR_PLUS_COUNT 26
/* Initial number of dentries to read */
#define READDIR_BASE_COUNT 128

/* Marker offset used to signify end-of-directory */
#define READDIR_EOD        (1LL << 63)

/* Offset of the first file, allow two entries for . and .. */
#define OFFSET_BASE        2

struct iterate_data {
	off_t                     id_base_offset;
	int                       id_index;
	struct dfuse_readdir_hdl *id_hdl;
};

/* Mark a directory change so that any cache can be evicted.  The kernel pagecache is already
 * wiped on unlink if the directory isn't open, if it is then already open handles will return
 * the unlinked file, and a inval() call here does not change that.
 */
void
dfuse_cache_evict_dir(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *ie)
{
	uint32_t open_count = atomic_load_relaxed(&ie->ie_open_count);

	if (open_count != 0)
		DFUSE_TRA_DEBUG(ie, "Directory change whilst open");

	D_SPIN_LOCK(&dfuse_info->di_lock);
	if (ie->ie_rd_hdl) {
		DFUSE_TRA_DEBUG(ie, "Setting shared readdir handle as invalid");
		ie->ie_rd_hdl->drh_valid = false;
	}
	D_SPIN_UNLOCK(&dfuse_info->di_lock);

	dfuse_cache_evict(ie);
}

static int
filler_cb(dfs_t *dfs, dfs_obj_t *dir, const char name[], void *arg)
{
	struct iterate_data        *idata = arg;
	struct dfuse_readdir_entry *dre;

	dre = &idata->id_hdl->drh_dre[idata->id_index];

	DFUSE_TRA_DEBUG(idata->id_hdl, "Adding at index %d offset %#lx " DF_DE, idata->id_index,
			idata->id_base_offset + idata->id_index, DP_DE(name));

	strncpy(dre->dre_name, name, NAME_MAX);
	dre->dre_offset      = idata->id_base_offset + idata->id_index;
	dre->dre_next_offset = dre->dre_offset + 1;
	idata->id_index++;

	return 0;
}

static int
fetch_dir_entries(struct dfuse_obj_hdl *oh, off_t offset, int to_fetch, bool *eod)
{
	struct iterate_data       idata = {};
	uint32_t                  count = to_fetch;
	int                       rc;
	struct dfuse_readdir_hdl *hdl = oh->doh_rd;

	idata.id_base_offset = offset;
	idata.id_hdl         = hdl;

	DFUSE_TRA_DEBUG(hdl, "Fetching new entries at offset %#lx", offset);

	D_ASSERT(oh->doh_rd);

	rc = dfs_iterate(oh->doh_dfs, oh->doh_ie->ie_obj, &hdl->drh_anchor, &count,
			 (NAME_MAX + 1) * count, filler_cb, &idata);

	if (rc) {
		DFUSE_TRA_ERROR(oh, "dfs_iterate() returned: %d (%s)", rc, strerror(rc));
		return rc;
	}

	hdl->drh_anchor_index += count;
	hdl->drh_dre_index      = 0;
	hdl->drh_dre_last_index = count;

	DFUSE_TRA_DEBUG(hdl, "Added %d entries, anchor_index %d rc %d", count,
			hdl->drh_anchor_index, rc);

	if (count) {
		if (daos_anchor_is_eof(&hdl->drh_anchor))
			hdl->drh_dre[count - 1].dre_next_offset = READDIR_EOD;
	} else {
		*eod = true;
	}

	return rc;
}

/* Create a readdir handle */
static struct dfuse_readdir_hdl *
_handle_init(struct dfuse_cont *dfc)
{
	struct dfuse_readdir_hdl *hdl;

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&hdl->drh_cache_list);
	atomic_init(&hdl->drh_ref, 1);
	hdl->drh_valid = true;
	return hdl;
}

/* Drop a ref on a readdir handle and release if required. Handle will no longer be usable */
void
dfuse_dre_drop(struct dfuse_info *dfuse_info, struct dfuse_obj_hdl *oh)
{
	struct dfuse_readdir_hdl *hdl;
	struct dfuse_readdir_c   *drc, *next;
	uint32_t                  oldref;
	off_t                     expected_offset = 2;

	DFUSE_TRA_DEBUG(oh, "Dropping ref on %p", oh->doh_rd);

	if (!oh->doh_rd)
		return;

	hdl = oh->doh_rd;

	oh->doh_rd       = NULL;
	oh->doh_rd_nextc = NULL;

	/* Lock is to protect oh->doh_ie->ie_rd_hdl between readdir/closedir calls */
	D_SPIN_LOCK(&dfuse_info->di_lock);

	oldref = atomic_fetch_sub_relaxed(&hdl->drh_ref, 1);
	if (oldref != 1) {
		DFUSE_TRA_DEBUG(hdl, "Ref was %d", oldref);
		D_GOTO(unlock, 0);
	}

	DFUSE_TRA_DEBUG(hdl, "Ref was 1, freeing");

	/* Check for common */
	if (hdl == oh->doh_ie->ie_rd_hdl)
		oh->doh_ie->ie_rd_hdl = NULL;

	d_list_for_each_entry_safe(drc, next, &hdl->drh_cache_list, drc_list) {
		D_ASSERT(drc->drc_offset == expected_offset);
		D_ASSERT(drc->drc_next_offset == expected_offset + 1 ||
			 drc->drc_next_offset == READDIR_EOD);
		expected_offset = drc->drc_next_offset;
		if (drc->drc_rlink)
			d_hash_rec_decref(&dfuse_info->dpi_iet, drc->drc_rlink);
		D_FREE(drc);
	}
	D_FREE(hdl);
unlock:
	D_SPIN_UNLOCK(&dfuse_info->di_lock);
}

static int
create_entry(struct dfuse_info *dfuse_info, struct dfuse_inode_entry *parent, struct stat *stbuf,
	     dfs_obj_t *obj, char *name, char *attr, daos_size_t attr_len, d_list_t **rlinkp)
{
	struct dfuse_inode_entry *ie;
	d_list_t                 *rlink;
	int                       rc = 0;

	D_ALLOC_PTR(ie);
	if (!ie) {
		dfs_release(obj);
		D_GOTO(out, rc = ENOMEM);
	}

	DFUSE_TRA_UP(ie, parent, "inode");

	dfuse_ie_init(dfuse_info, ie);
	ie->ie_obj  = obj;
	ie->ie_stat = *stbuf;

	dfs_obj2id(ie->ie_obj, &ie->ie_oid);

	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs    = parent->ie_dfs;

	if (S_ISDIR(ie->ie_stat.st_mode) && attr_len) {
		/* Check for UNS entry point, this will allocate a new inode number if successful */
		rc = check_for_uns_ep(dfuse_info, ie, attr, attr_len);
		if (rc != 0) {
			DFUSE_TRA_WARNING(ie, "check_for_uns_ep() returned %d, ignoring", rc);
			rc = 0;
		}
		ie->ie_root = true;
	}

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';

	DFUSE_TRA_DEBUG(ie, "Inserting inode %#lx mode 0%o", stbuf->st_ino, ie->ie_stat.st_mode);

	rlink = d_hash_rec_find_insert(&dfuse_info->dpi_iet, &ie->ie_stat.st_ino,
				       sizeof(ie->ie_stat.st_ino), &ie->ie_htl);

	if (rlink != &ie->ie_htl) {
		struct dfuse_inode_entry *inode;

		inode = container_of(rlink, struct dfuse_inode_entry, ie_htl);

		/* The lookup has resulted in an existing file, so reuse that
		 * entry, drop the inode in the lookup descriptor and do not
		 * keep a reference on the parent.
		 */

		/* Update the existing object with the new name/parent */

		DFUSE_TRA_DEBUG(inode, "Maybe updating parent inode %#lx dfs_ino %#lx",
				stbuf->st_ino, ie->ie_dfs->dfs_ino);

		/** update the chunk size and oclass of inode entry */
		dfs_obj_copy_attr(inode->ie_obj, ie->ie_obj);

		if (ie->ie_stat.st_ino == ie->ie_dfs->dfs_ino) {
			DFUSE_TRA_DEBUG(inode, "Not updating parent");
		} else {
			rc = dfs_update_parent(inode->ie_obj, ie->ie_obj, ie->ie_name);
			if (rc != 0)
				DFUSE_TRA_DEBUG(inode, "dfs_update_parent() failed %d", rc);
		}
		inode->ie_parent = ie->ie_parent;
		strncpy(inode->ie_name, ie->ie_name, NAME_MAX + 1);

		atomic_fetch_sub_relaxed(&ie->ie_ref, 1);
		dfuse_ie_close(dfuse_info, ie);
		ie = inode;
	}

	*rlinkp = rlink;
	if (rc != 0)
		dfuse_ie_close(dfuse_info, ie);
out:
	return rc;
}

static void
set_entry_params(struct fuse_entry_param *entry, struct dfuse_inode_entry *ie)
{
	entry->generation = 1;
	entry->ino        = entry->attr.st_ino;

	if (S_ISDIR(ie->ie_stat.st_mode))
		entry->entry_timeout = ie->ie_dfs->dfc_dentry_dir_timeout;
	else
		entry->entry_timeout = ie->ie_dfs->dfc_dentry_timeout;

	if ((atomic_load_relaxed(&ie->ie_il_count)) != 0)
		return;
	entry->attr_timeout = ie->ie_dfs->dfc_attr_timeout;
}

static inline void
dfuse_readdir_reset(struct dfuse_readdir_hdl *hdl)
{
	memset(&hdl->drh_anchor, 0, sizeof(hdl->drh_anchor));
	memset(hdl->drh_dre, 0, sizeof(*hdl->drh_dre) * READDIR_MAX_COUNT);
	hdl->drh_dre_index      = 0;
	hdl->drh_dre_last_index = 0;
	hdl->drh_anchor_index   = 0;
}

#define FADP fuse_add_direntry_plus
#define FAD  fuse_add_direntry

/* Fetch a readdir handle for this operation, this might be shared with other directory
 * handles for the same inode.  Only one readdir will happen concurrently for each inode
 * however readdir does get called concurrently with releasedir for the same inode so
 * protect this section with a spinlock.
 */
static int
ensure_rd_handle(struct dfuse_info *dfuse_info, struct dfuse_obj_hdl *oh)
{
	if (oh->doh_rd != NULL)
		return 0;

	D_SPIN_LOCK(&dfuse_info->di_lock);

	if (oh->doh_ie->ie_rd_hdl && oh->doh_ie->ie_rd_hdl->drh_valid) {
		oh->doh_rd = oh->doh_ie->ie_rd_hdl;
		atomic_fetch_add_relaxed(&oh->doh_rd->drh_ref, 1);
		DFUSE_TRA_DEBUG(oh, "Sharing readdir handle %p with existing readers", oh->doh_rd);
	} else {
		oh->doh_rd = _handle_init(oh->doh_ie->ie_dfs);
		if (oh->doh_rd == NULL) {
			D_SPIN_UNLOCK(&dfuse_info->di_lock);
			return ENOMEM;
		}

		DFUSE_TRA_UP(oh->doh_rd, oh, "readdir");

		if (oh->doh_ie->ie_rd_hdl == NULL && oh->doh_ie->ie_dfs->dfc_dentry_timeout > 0) {
			oh->doh_rd->drh_caching = true;
			oh->doh_ie->ie_rd_hdl   = oh->doh_rd;
		}
	}
	D_SPIN_UNLOCK(&dfuse_info->di_lock);
	return 0;
}

#define FADP fuse_add_direntry_plus
#define FAD  fuse_add_direntry

int
dfuse_do_readdir(struct dfuse_info *dfuse_info, fuse_req_t req, struct dfuse_obj_hdl *oh,
		 char *reply_buff, size_t *out_size, off_t offset, bool plus)
{
	off_t                     buff_offset = 0;
	int                       added       = 0;
	int                       rc          = 0;
	bool                      large_fetch = true;
	bool                      to_seek     = false;
	struct dfuse_readdir_hdl *hdl;
	size_t                    size = *out_size;

	rc = ensure_rd_handle(dfuse_info, oh);
	if (rc != 0)
		return rc;

	hdl = oh->doh_rd;

	/* Keep track of if this call is part of a series of calls, one start-to-end directory reads
	 * will populate the kernel cache.  This lets us estimate when the kernel cache was
	 * populated so that opendir can pass "keep_cache" based on timeout values.
	 */
	if (offset == 0) {
		if (oh->doh_kreaddir_started) {
			oh->doh_kreaddir_invalid = true;
		}
		oh->doh_kreaddir_started = true;
	}

	DFUSE_TRA_DEBUG(oh, "plus %d offset %#lx idx %d idx_offset %#lx", plus, offset,
			hdl->drh_dre_index, hdl->drh_dre[hdl->drh_dre_index].dre_offset);

	DFUSE_TRA_DEBUG(oh, "Offsets requested %#lx directory %#lx", offset, oh->doh_rd_offset);

	/* If the offset is unexpected for this directory handle then seek, first ensuring the
	 * readdir handle is unique.
	 */
	if (oh->doh_rd_offset != offset) {
		to_seek = true;
	} else if (!d_list_empty(&hdl->drh_cache_list)) {
		/* If there is no seekdir but there is valid cache data then use the cache.
		 *
		 * Directory handles may not have up-to-date values for doh_rd_nextc in some cases
		 * so perform a seek here if necessairy.
		 */
		struct dfuse_readdir_c *drc;
		size_t                  written     = 0;
		off_t                   next_offset = 0;
		void                   *nextp;

		DFUSE_TRA_DEBUG(oh, "hdl_next %p list start %p list end %p list addr %p",
				oh->doh_rd_nextc, hdl->drh_cache_list.next,
				hdl->drh_cache_list.prev, &hdl->drh_cache_list);

		if (oh->doh_rd_nextc) {
			drc = oh->doh_rd_nextc;

			if (drc == (void *)&hdl->drh_cache_list) {
				DFUSE_TRA_DEBUG(oh, "Existing location is end-of-stream");
			} else {
				DFUSE_TRA_DEBUG(oh,
						"Resuming at existing location on list %#lx %#lx",
						drc->drc_offset, offset);
			}
		} else {
			drc = container_of(hdl->drh_cache_list.next, struct dfuse_readdir_c,
					   drc_list);

			DFUSE_TRA_DEBUG(oh, "Starting on list %#lx %#lx", drc->drc_offset, offset);
		}
		if (offset != 0) {
			/* Whilst there is more list then move forward in the list until the
			 * offsets match.
			 */

			nextp = &drc->drc_list.next;

			while ((nextp != (void *)&hdl->drh_cache_list) &&
			       (drc->drc_offset != offset)) {
				DFUSE_TRA_DEBUG(oh, "Moving along list looking for %#lx at %#lx",
						offset, drc->drc_offset);

				nextp = drc->drc_list.next;
				drc   = container_of(nextp, struct dfuse_readdir_c, drc_list);
			}
		}

		nextp = &drc->drc_list.next;
		while (nextp != (void *)&hdl->drh_cache_list) {
			drc = container_of(nextp, struct dfuse_readdir_c, drc_list);

			DFUSE_TRA_DEBUG(oh, "%p adding offset %#lx next %#lx " DF_DE, drc,
					drc->drc_offset, drc->drc_next_offset,
					DP_DE(drc->drc_name));

			if (plus) {
				struct fuse_entry_param   entry = {0};
				struct dfuse_inode_entry *ie;

				if (drc->drc_rlink) {
					entry.attr = drc->drc_stbuf;

					d_hash_rec_addref(&dfuse_info->dpi_iet, drc->drc_rlink);
					ie = container_of(drc->drc_rlink, struct dfuse_inode_entry,
							  ie_htl);
				} else {
					char          out[DUNS_MAX_XATTR_LEN];
					char         *outp     = &out[0];
					daos_size_t   attr_len = DUNS_MAX_XATTR_LEN;
					struct stat   stbuf    = {0};
					dfs_obj_t    *obj      = NULL;
					d_list_t     *rlink;
					daos_obj_id_t oid;

					/* Handle the case where the cache was populated by
					 * a readdir call but is being read by a readdirplus
					 * call so the extra data needs to be loaded by the
					 * second reader, not the first.
					 */

					rc = dfs_lookupx(
					    oh->doh_dfs, oh->doh_ie->ie_obj, drc->drc_name,
					    O_RDWR | O_NOFOLLOW, &obj, &stbuf.st_mode, &stbuf, 1,
					    &duns_xattr_name, (void **)&outp, &attr_len);

					if (rc != 0) {
						DFUSE_TRA_DEBUG(oh, "Problem finding file %d", rc);
						D_GOTO(reply, 0);
					}
					/* Check oid is the same! */

					dfs_obj2id(obj, &oid);

					dfuse_compute_inode(oh->doh_ie->ie_dfs, &oid,
							    &stbuf.st_ino);

					rc = create_entry(dfuse_info, oh->doh_ie, &stbuf, obj,
							  drc->drc_name, out, attr_len, &rlink);
					if (rc != 0) {
						D_GOTO(reply, rc);
					}

					ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

					if (ie->ie_root) {
						entry.attr = ie->ie_stat;
					} else {
						entry.attr = stbuf;
					}
					drc->drc_stbuf = entry.attr;
					d_hash_rec_addref(&dfuse_info->dpi_iet, rlink);
					drc->drc_rlink = rlink;
				}

				set_entry_params(&entry, ie);

				written = FADP(req, &reply_buff[buff_offset], size - buff_offset,
					       drc->drc_name, &entry, drc->drc_next_offset);
				if (written > size - buff_offset)
					d_hash_rec_decref(&dfuse_info->dpi_iet, drc->drc_rlink);

			} else {
				written = FAD(req, &reply_buff[buff_offset], size - buff_offset,
					      drc->drc_name, &drc->drc_stbuf, drc->drc_next_offset);
			}

			if (written > size - buff_offset) {
				DFUSE_TRA_DEBUG(oh, "Buffer is full");
				oh->doh_rd_nextc  = drc;
				oh->doh_rd_offset = next_offset;
				D_GOTO(reply, rc = 0);
			}
			next_offset = drc->drc_next_offset;
			added += 1;
			buff_offset += written;

			nextp = drc->drc_list.next;
		}

		DFUSE_TRA_DEBUG(oh, "Ran out of cache entries, added %d", added);

		if (added) {
			/* This reader has got to the end of the cache list so update nextc
			 * with the last replied entry, that is the current tail of the list.
			 */
			oh->doh_rd_nextc = container_of(hdl->drh_cache_list.prev,
							struct dfuse_readdir_c, drc_list);
			D_GOTO(reply, oh->doh_rd_offset = next_offset);
		}
	}

	if (!to_seek) {
		if (hdl->drh_dre_last_index == 0) {
			if (offset != hdl->drh_dre[hdl->drh_dre_index].dre_offset &&
			    hdl->drh_dre[hdl->drh_dre_index].dre_offset != 0)
				to_seek = true;
		} else {
			if (offset != hdl->drh_dre[hdl->drh_dre_index].dre_offset)
				to_seek = true;
		}
		if (to_seek)
			DFUSE_TRA_DEBUG(oh, "seeking, %#lx %d %#lx", offset,
					hdl->drh_dre_last_index,
					hdl->drh_dre[hdl->drh_dre_index].dre_offset);
	}

	if (to_seek) {
		uint32_t num;

		DFUSE_TRA_DEBUG(oh, "Seeking from offset %#lx to %#lx", oh->doh_rd_offset, offset);

		oh->doh_kreaddir_invalid = true;

		/* Drop if shared */
		if (oh->doh_rd->drh_caching) {
			DFUSE_TRA_DEBUG(oh, "Switching to private handle");
			dfuse_dre_drop(dfuse_info, oh);
			oh->doh_rd = _handle_init(oh->doh_ie->ie_dfs);
			hdl        = oh->doh_rd;
			if (oh->doh_rd == NULL)
				D_GOTO(out_reset, rc = ENOMEM);
			DFUSE_TRA_UP(oh->doh_rd, oh, "readdir");
		} else {
			dfuse_readdir_reset(hdl);
		}

		DFUSE_TRA_DEBUG(oh, "Seeking from offset %#lx(%d) to %#lx (index %d)",
				hdl->drh_dre[hdl->drh_dre_index].dre_offset, hdl->drh_anchor_index,
				offset, hdl->drh_dre_index);

		if (offset != 0) {
			num = (uint32_t)offset - OFFSET_BASE;
			while (num) {
				rc = dfs_iterate(oh->doh_dfs, oh->doh_ie->ie_obj, &hdl->drh_anchor,
						 &num, (NAME_MAX + 1) * num, NULL, NULL);
				if (rc)
					D_GOTO(out_reset, rc);

				if (daos_anchor_is_eof(&hdl->drh_anchor)) {
					dfuse_readdir_reset(hdl);
					oh->doh_rd_offset = 0;
					D_GOTO(reply, rc = 0);
				}

				hdl->drh_anchor_index += num;

				num = offset - OFFSET_BASE - hdl->drh_anchor_index;
			}
		}
		large_fetch = false;
	}

	if (offset == 0)
		offset = OFFSET_BASE;

	if (offset < 1024)
		large_fetch = false;

	do {
		int  i;
		bool fetched = false;

		if (hdl->drh_dre_last_index == 0) {
			uint32_t to_fetch;
			bool     eod = false;

			D_ASSERT(offset != hdl->drh_dre[hdl->drh_dre_index].dre_offset);

			if (large_fetch)
				to_fetch = READDIR_MAX_COUNT;
			else if (plus)
				to_fetch = READDIR_PLUS_COUNT - added;
			else
				to_fetch = READDIR_BASE_COUNT - added;

			rc = fetch_dir_entries(oh, offset, to_fetch, &eod);
			if (rc != 0)
				D_GOTO(reply, rc);

			if (eod)
				D_GOTO(reply, rc = 0);

			fetched = true;
		} else {
			D_ASSERT(offset == hdl->drh_dre[hdl->drh_dre_index].dre_offset);
		}

		DFUSE_TRA_DEBUG(oh, "processing offset %#lx", offset);

		/* Populate dir */
		for (i = hdl->drh_dre_index; i < hdl->drh_dre_last_index; i++) {
			struct dfuse_readdir_entry *dre   = &hdl->drh_dre[i];
			struct stat                 stbuf = {0};
			daos_obj_id_t               oid;
			dfs_obj_t                  *obj;
			size_t                      written;
			char                        out[DUNS_MAX_XATTR_LEN];
			char                       *outp     = &out[0];
			daos_size_t                 attr_len = DUNS_MAX_XATTR_LEN;
			struct dfuse_readdir_c     *drc      = NULL;

			if (hdl->drh_caching) {
				D_ALLOC_PTR(drc);
				if (drc == NULL) {
					D_GOTO(reply, rc = ENOMEM);
				}
				strncpy(drc->drc_name, dre->dre_name, NAME_MAX);
				drc->drc_offset      = offset;
				drc->drc_next_offset = dre->dre_next_offset;
			}

			D_ASSERT(dre->dre_offset != 0);

			hdl->drh_dre_index += 1;

			DFUSE_TRA_DEBUG(hdl, "Checking offset %#lx next %#lx " DF_DE,
					dre->dre_offset, dre->dre_next_offset,
					DP_DE(dre->dre_name));

			if (plus)
				rc = dfs_lookupx(oh->doh_dfs, oh->doh_ie->ie_obj, dre->dre_name,
						 O_RDWR | O_NOFOLLOW, &obj, &stbuf.st_mode, &stbuf,
						 1, &duns_xattr_name, (void **)&outp, &attr_len);
			else
				rc = dfs_lookup_rel(oh->doh_dfs, oh->doh_ie->ie_obj, dre->dre_name,
						    O_RDONLY | O_NOFOLLOW, &obj, &stbuf.st_mode,
						    NULL);
			if (rc == ENOENT) {
				DFUSE_TRA_DEBUG(oh, "File does not exist");
				D_FREE(drc);
				continue;
			} else if (rc != 0) {
				DFUSE_TRA_DEBUG(oh, "Problem finding file %d", rc);
				D_FREE(drc);
				D_GOTO(reply, rc);
			}

			dfs_obj2id(obj, &oid);

			dfuse_compute_inode(oh->doh_ie->ie_dfs, &oid, &stbuf.st_ino);

			if (plus) {
				struct fuse_entry_param   entry = {0};
				struct dfuse_inode_entry *ie;
				d_list_t                 *rlink;

				rc = create_entry(dfuse_info, oh->doh_ie, &stbuf, obj,
						  dre->dre_name, out, attr_len, &rlink);
				if (rc != 0) {
					dfs_release(obj);
					D_FREE(drc);
					D_GOTO(reply, rc);
				}

				ie = container_of(rlink, struct dfuse_inode_entry, ie_htl);

				if (ie->ie_root) {
					entry.attr = ie->ie_stat;
				} else {
					entry.attr = stbuf;
				}

				/* If saving this in the cache then take an extra ref for the
				 * entry on the list, as well as saving rlink
				 */
				if (drc) {
					drc->drc_stbuf = entry.attr;
					d_hash_rec_addref(&dfuse_info->dpi_iet, rlink);
					drc->drc_rlink = rlink;
				}

				set_entry_params(&entry, ie);

				written = FADP(req, &reply_buff[buff_offset], size - buff_offset,
					       dre->dre_name, &entry, dre->dre_next_offset);
				if (written > size - buff_offset) {
					d_hash_rec_decref(&dfuse_info->dpi_iet, rlink);
					if (drc)
						d_hash_rec_decref(&dfuse_info->dpi_iet, rlink);
				}
			} else {
				dfs_release(obj);

				written = FAD(req, &reply_buff[buff_offset], size - buff_offset,
					      dre->dre_name, &stbuf, dre->dre_next_offset);

				if (drc) {
					drc->drc_stbuf.st_mode = stbuf.st_mode;
					drc->drc_stbuf.st_ino  = stbuf.st_ino;
				}
			}
			if (written > size - buff_offset) {
				DFUSE_TRA_DEBUG(oh, "Buffer is full, rolling back");
				hdl->drh_dre_index -= 1;
				D_FREE(drc);
				D_GOTO(reply, rc = 0);
			}

			if (drc) {
				oh->doh_rd_nextc = drc;
				DFUSE_TRA_DEBUG(hdl, "Appending offset %#lx to list, next %#lx",
						drc->drc_offset, drc->drc_next_offset);
				d_list_add_tail(&drc->drc_list, &hdl->drh_cache_list);
			}

			/* This entry has been added to the buffer so mark it as empty */
			dre->dre_offset = 0;
			buff_offset += written;
			added++;
			offset++;
			oh->doh_rd_offset = dre->dre_next_offset;

			if (dre->dre_next_offset == READDIR_EOD) {
				DFUSE_TRA_DEBUG(oh, "Reached end of directory");
				oh->doh_rd_offset = READDIR_EOD;
				D_GOTO(reply, rc = 0);
			}
		}
		if (hdl->drh_dre_index == hdl->drh_dre_last_index) {
			hdl->drh_dre_index      = 0;
			hdl->drh_dre_last_index = 0;
		}
		if (fetched && !large_fetch)
			break;
	} while (true);

reply:

	/* Reply with some data.  It can happen that there's valid data in the buffer already
	 * and then we hit an error, if that happens then 'added' and 'rc' will both be non-zero,
	 * for that case we want to return the data that's already in the buffer and drop the error.
	 * Any subsequent call would cause the next entry to be looked up and a persistent error
	 * would lead to a non-zero value of 'rc' but a 0 for added.
	 *
	 * oh->doh_rd_offset is assumed to be set correctly at this point and should always be
	 * updated when added is changed.
	 *
	 * added    rc
	 * 0        non-zero       Return error.
	 * 0        0              Return 0.  Can happen on empty directory.
	 * non-zero non-zero       Reply with buffer.
	 * non-zero 0              Reply with buffer.
	 */

	if (added)
		DFUSE_TRA_DEBUG(oh, "Replying with %d entries offset %#lx ", added,
				oh->doh_rd_offset);

	if (added == 0 && rc != 0)
		D_GOTO(out_reset, rc);

	*out_size = buff_offset;
	return 0;

out_reset:
	if (hdl)
		dfuse_readdir_reset(hdl);
	D_ASSERT(rc != 0);
	return rc;
}

void
dfuse_cb_readdir(fuse_req_t req, struct dfuse_obj_hdl *oh, size_t size, off_t offset, bool plus)
{
	struct dfuse_info *dfuse_info = fuse_req_userdata(req);
	char              *reply_buff = NULL;
	int                rc         = EIO;

	D_ASSERTF(atomic_fetch_add_relaxed(&oh->doh_readdir_number, 1) == 0,
		  "Multiple readdir per handle");

	D_ASSERTF(atomic_fetch_add_relaxed(&oh->doh_ie->ie_readdir_number, 1) == 0,
		  "Multiple readdir per inode");

	/* Handle the EOD case, the kernel will keep reading until it receives zero replies so
	 * reply early in this case.
	 */
	if (offset == READDIR_EOD) {
		oh->doh_kreaddir_finished = true;
		DFUSE_TRA_DEBUG(oh, "End of directory %#lx", offset);

		size = 0;
		D_GOTO(out, rc = 0);
	}

	if ((offset > 0 && offset < OFFSET_BASE) || offset < 0)
		D_GOTO(out, rc = EINVAL);

	/* Alignment is important for the buffer, the packing function will align up so a badly
	 * allocated buffer will need to be padded at the start, to avoid that then align here.
	 * TODO: This could be part of the readdir handle to avoid frequent allocations for a page
	 * which is passed between kernel and userspace.
	 */
	D_ALIGNED_ALLOC(reply_buff, size, size);
	if (reply_buff == NULL)
		D_GOTO(out, rc = ENOMEM);

	rc = dfuse_do_readdir(dfuse_info, req, oh, reply_buff, &size, offset, plus);

out:
	atomic_fetch_sub_relaxed(&oh->doh_readdir_number, 1);
	atomic_fetch_sub_relaxed(&oh->doh_ie->ie_readdir_number, 1);

	if (rc)
		DFUSE_REPLY_ERR_RAW(oh, req, rc);
	else
		DFUSE_REPLY_BUF(oh, req, reply_buff, size);

	D_FREE(reply_buff);
}
