/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "dfuse_common.h"
#include "dfuse.h"

#include "daos_uns.h"

void
dfuse_reply_entry(struct dfuse_projection_info *fs_handle,
		  struct dfuse_inode_entry *ie,
		  struct fuse_file_info *fi_out,
		  bool is_new,
		  fuse_req_t req)
{
	struct fuse_entry_param	entry = {0};
	d_list_t		*rlink;
	int			rc;

	D_ASSERT(ie->ie_parent);
	D_ASSERT(ie->ie_dfs);

	/* Set the caching attributes of this entry, but do not allow
	 * any caching on fifos.
	 */

	if (S_ISFIFO(ie->ie_stat.st_mode)) {
		if (!is_new) {
			ie->ie_stat.st_mode &= ~S_IFIFO;
			ie->ie_stat.st_mode |= S_IFDIR;
		}
	} else {
		entry.attr_timeout = ie->ie_dfs->dfs_attr_timeout;
		entry.entry_timeout = ie->ie_dfs->dfs_attr_timeout;
	}

	if (ie->ie_stat.st_ino == 0) {
		rc = dfs_obj2id(ie->ie_obj, &ie->ie_oid);
		if (rc)
			D_GOTO(out_decref, rc);

		dfuse_compute_inode(ie->ie_dfs, &ie->ie_oid,
				    &ie->ie_stat.st_ino);
	}

	entry.attr = ie->ie_stat;
	entry.generation = 1;
	entry.ino = entry.attr.st_ino;
	DFUSE_TRA_DEBUG(ie, "Inserting inode %#lx", entry.ino);

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

		DFUSE_TRA_DEBUG(ie, "inode dfs %p %ld hi %#lx lo %#lx",
				inode->ie_dfs,
				inode->ie_dfs->dfs_ino,
				inode->ie_oid.hi,
				inode->ie_oid.lo);

		DFUSE_TRA_DEBUG(ie, "inode dfs %p %ld hi %#lx lo %#lx",
				ie->ie_dfs,
				ie->ie_dfs->dfs_ino,
				ie->ie_oid.hi,
				ie->ie_oid.lo);

		/* Check for conflicts, in either the dfs or oid space.  This
		 * can happen because of the fact we squash larger identifiers
		 * into a shorter 64 bit space, but if the bitshifting is right
		 * it shouldn't happen until there are a large number of active
		 * files. DAOS-4928 has more details.
		 */
		if (ie->ie_dfs != inode->ie_dfs) {
			DFUSE_TRA_ERROR(inode, "Duplicate inode found (dfs)");
			D_GOTO(out_err, rc = EIO);
		}

		/* Check the OID */
		if (ie->ie_oid.lo != inode->ie_oid.lo ||
		    ie->ie_oid.hi != inode->ie_oid.hi) {
			DFUSE_TRA_ERROR(inode, "Duplicate inode found (oid)");
			D_GOTO(out_err, rc = EIO);
		}

		DFUSE_TRA_DEBUG(inode,
				"Maybe updating parent inode %#lx dfs_ino %#lx",
				entry.ino, ie->ie_dfs->dfs_ino);

		if (ie->ie_stat.st_ino == ie->ie_dfs->dfs_ino) {
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

	if (fi_out)
		DFUSE_REPLY_CREATE(ie, req, entry, fi_out);
	else
		DFUSE_REPLY_ENTRY(ie, req, entry);
	return;
out_decref:
	d_hash_rec_decref(&fs_handle->dpi_iet, &ie->ie_htl);
out_err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	dfs_release(ie->ie_obj);
}

/* Check for and set a unified namespace entry point.
 *
 * This function will check for and configure a inode as
 * a new entry point of possible, and modify the inode
 * as required.
 *
 * On failure it will return error.
 *
 */
static int
check_for_uns_ep(struct dfuse_projection_info *fs_handle,
		 struct dfuse_inode_entry *ie)
{
	int			rc;
	char			str[DUNS_MAX_XATTR_LEN];
	daos_size_t		str_len = DUNS_MAX_XATTR_LEN;
	struct duns_attr_t	dattr = {};
	struct dfuse_dfs	*dfs = NULL;
	struct dfuse_dfs	*dfsi;
	struct dfuse_pool	*dfp = NULL;
	struct dfuse_pool	*dfpi;
	int			new_pool = false;
	int			new_cont = false;
	int ret;

	rc = dfs_getxattr(ie->ie_dfs->dfs_ns, ie->ie_obj, DUNS_XATTR_NAME,
			  &str, &str_len);

	if (rc == ENODATA)
		return 0;
	if (rc)
		return rc;

	rc = duns_parse_attr(&str[0], str_len, &dattr);
	if (rc)
		return rc;

	if (dattr.da_type != DAOS_PROP_CO_LAYOUT_POSIX)
		return ENOTSUP;

	D_MUTEX_LOCK(&fs_handle->dpi_info->di_lock);

	/* Search the currently connect dfp list, if one matches then use that,
	 * otherwise allocate a new one.
	 */
	d_list_for_each_entry(dfpi, &fs_handle->dpi_info->di_dfp_list,
			      dfp_list) {
		DFUSE_TRA_DEBUG(ie, "Checking dfp %p", dfpi);

		if (uuid_compare(dattr.da_puuid, dfpi->dfp_pool) != 0)
			continue;

		DFUSE_TRA_DEBUG(ie, "Reusing dfp %p", dfpi);
		dfp = dfpi;
		break;
	}

	if (!dfp) {
		D_ALLOC_PTR(dfp);
		if (dfp == NULL)
			D_GOTO(out_err, ret = ENOMEM);

		DFUSE_TRA_UP(dfp, ie->ie_dfs->dfs_dfp, "dfp");
		D_INIT_LIST_HEAD(&dfp->dfp_dfs_list);
		d_list_add(&dfp->dfp_list, &fs_handle->dpi_info->di_dfp_list);
		new_pool = true;

		uuid_copy(dfp->dfp_pool, dattr.da_puuid);

		/* Connect to DAOS pool */
		rc = daos_pool_connect(dfp->dfp_pool,
				       fs_handle->dpi_info->di_group,
				       DAOS_PC_RW,
				       &dfp->dfp_poh, &dfp->dfp_pool_info,
				       NULL);
		if (rc != -DER_SUCCESS) {
			if (rc == -DER_NO_PERM)
				DFUSE_TRA_DEBUG(ie,
						"daos_pool_connect() failed, "
						DF_RC"\n", DP_RC(rc));
			else
				DFUSE_TRA_WARNING(ie,
						  "daos_pool_connect() failed, "
						  DF_RC"\n", DP_RC(rc));
			D_GOTO(out_err, ret = daos_der2errno(rc));
		}
	}

	d_list_for_each_entry(dfsi, &dfp->dfp_dfs_list,	dfs_list) {
		if (uuid_compare(dattr.da_cuuid, dfsi->dfs_cont) != 0)
			continue;

		DFUSE_TRA_DEBUG(ie, "Reusing dfs %p", dfsi);
		dfs = dfsi;
		break;
	}

	if (!dfs) {
		D_ALLOC_PTR(dfs);
		if (dfs == NULL)
			D_GOTO(out_pool, ret = ENOMEM);

		dfs->dfs_ops = ie->ie_dfs->dfs_ops;
		DFUSE_TRA_UP(dfs, dfp, "dfs");
		uuid_copy(dfs->dfs_cont, dattr.da_cuuid);
		d_list_add(&dfs->dfs_list, &dfp->dfp_dfs_list);
		new_cont = true;

		dfuse_dfs_init(dfs, ie->ie_dfs);

		/* Try to open the DAOS container (the mountpoint) */
		rc = daos_cont_open(dfp->dfp_poh, dfs->dfs_cont, DAOS_COO_RW,
				    &dfs->dfs_coh, NULL, NULL);
		if (rc) {
			DFUSE_LOG_ERROR("Failed container open (%d)",
					rc);
			D_GOTO(out_pool, ret = daos_der2errno(rc));
		}

		rc = dfs_mount(dfp->dfp_poh, dfs->dfs_coh, O_RDWR,
			       &dfs->dfs_ns);
		if (rc) {
			DFUSE_LOG_ERROR("dfs_mount() failed: (%s)",
					strerror(rc));
			D_GOTO(out_cont, ret = rc);
		}
		new_cont = true;
		ie->ie_root = true;

		dfs->dfs_ino = atomic_fetch_add_relaxed(&fs_handle->dpi_ino_next,
							1);
		dfs->dfs_dfp = dfp;
	}

	rc = dfs_release(ie->ie_obj);
	if (rc) {
		DFUSE_TRA_ERROR(dfs, "dfs_release() failed: (%s)",
				strerror(rc));
		D_GOTO(out_umount, ret = rc);
	}

	rc = dfs_lookup(dfs->dfs_ns, "/", O_RDWR, &ie->ie_obj,
			NULL, &ie->ie_stat);
	if (rc) {
		DFUSE_TRA_ERROR(dfs, "dfs_lookup() failed: (%s)",
				strerror(rc));
		D_GOTO(out_umount, ret = rc);
	}

	ie->ie_stat.st_ino = dfs->dfs_ino;

	ie->ie_dfs = dfs;

	DFUSE_TRA_INFO(dfs, "UNS entry point activated, root %lu",
		       dfs->dfs_ino);

	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
	return 0;
out_umount:
	if (new_cont) {
		rc = dfs_umount(dfs->dfs_ns);
		if (rc)
			DFUSE_TRA_ERROR(dfs, "dfs_umount() failed: (%s)",
					strerror(rc));
	}
out_cont:
	if (new_cont) {
		rc = daos_cont_close(dfs->dfs_coh, NULL);
		if (rc)
			DFUSE_TRA_ERROR(dfs, "daos_cont_close() failed %d", rc);
	}
out_pool:
	if (new_pool) {
		rc = daos_pool_disconnect(dfp->dfp_poh, NULL);
		if (rc)
			DFUSE_TRA_ERROR(dfs,
					"daos_pool_disconnect() failed %d", rc);
	}
out_err:
	if (new_cont) {
		d_list_del(&dfs->dfs_list);
		D_FREE(dfs);
	}
	if (new_pool) {
		d_list_del(&dfp->dfp_list);
		D_FREE(dfp);
	}
	D_MUTEX_UNLOCK(&fs_handle->dpi_info->di_lock);
	return ret;
}

void
dfuse_cb_lookup(fuse_req_t req, struct dfuse_inode_entry *parent,
		const char *name)
{
	struct dfuse_projection_info	*fs_handle = fuse_req_userdata(req);
	struct dfuse_inode_entry	*ie = NULL;
	int				rc;

	DFUSE_TRA_DEBUG(fs_handle,
			"Parent:%lu '%s'", parent->ie_stat.st_ino, name);

	D_ALLOC_PTR(ie);
	if (!ie)
		D_GOTO(err, rc = ENOMEM);

	DFUSE_TRA_UP(ie, parent, "inode");

	ie->ie_parent = parent->ie_stat.st_ino;
	ie->ie_dfs = parent->ie_dfs;

	rc = dfs_lookup_rel(parent->ie_dfs->dfs_ns, parent->ie_obj, name,
			    O_RDWR | O_NOFOLLOW, &ie->ie_obj,
			    NULL, &ie->ie_stat);
	if (rc) {
		DFUSE_TRA_DEBUG(parent, "dfs_lookup() failed: (%s)",
				strerror(rc));

		if (rc == ENOENT && ie->ie_dfs->dfs_attr_timeout > 0) {
			struct fuse_entry_param entry = {};

			entry.entry_timeout = ie->ie_dfs->dfs_attr_timeout;

			DFUSE_REPLY_ENTRY(parent, req, entry);
			D_GOTO(free, 0);
		}

		D_GOTO(err, rc);
	}

	strncpy(ie->ie_name, name, NAME_MAX);
	ie->ie_name[NAME_MAX] = '\0';
	atomic_store_relaxed(&ie->ie_ref, 1);

	if (S_ISFIFO(ie->ie_stat.st_mode)) {
		rc = check_for_uns_ep(fs_handle, ie);
		DFUSE_TRA_DEBUG(ie,
				"check_for_uns_ep() returned %d", rc);
		if (rc != 0 && rc != EPERM)
			D_GOTO(err, rc);
	}

	dfuse_reply_entry(fs_handle, ie, NULL, false, req);
	return;

err:
	DFUSE_REPLY_ERR_RAW(fs_handle, req, rc);
	dfs_release(ie->ie_obj);
free:
	D_FREE(ie);
}
