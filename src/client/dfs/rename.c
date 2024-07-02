/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS move/rename & exchange operations */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/common.h>

#include "dfs_internal.h"

static int
xattr_copy(daos_handle_t src_oh, const char *src_name, daos_handle_t dst_oh, const char *dst_name,
	   daos_handle_t th)
{
	daos_key_t      src_dkey, dst_dkey;
	daos_anchor_t   anchor = {0};
	d_sg_list_t     sgl, fsgl;
	d_iov_t         iov, fiov;
	daos_iod_t      iod;
	void           *val_buf;
	char            enum_buf[ENUM_XDESC_BUF];
	daos_key_desc_t kds[ENUM_DESC_NR];
	int             rc = 0;

	/** set dkey for src entry name */
	d_iov_set(&src_dkey, (void *)src_name, strlen(src_name));

	/** set dkey for dst entry name */
	d_iov_set(&dst_dkey, (void *)dst_name, strlen(dst_name));

	/** Set IOD descriptor for fetching every xattr */
	iod.iod_nr    = 1;
	iod.iod_recxs = NULL;
	iod.iod_type  = DAOS_IOD_SINGLE;
	iod.iod_size  = DFS_MAX_XATTR_LEN;

	/** set sgl for fetch - user a preallocated buf to avoid a roundtrip */
	D_ALLOC(val_buf, DFS_MAX_XATTR_LEN);
	if (val_buf == NULL)
		return ENOMEM;
	fsgl.sg_nr     = 1;
	fsgl.sg_nr_out = 0;
	fsgl.sg_iovs   = &fiov;

	/** set sgl for akey_list */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_XDESC_BUF);
	sgl.sg_iovs = &iov;

	/** iterate over every akey to look for xattrs */
	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t number = ENUM_DESC_NR;
		uint32_t i;
		char    *ptr;

		memset(enum_buf, 0, ENUM_XDESC_BUF);
		rc = daos_obj_list_akey(src_oh, th, &src_dkey, &number, kds, &sgl, &anchor, NULL);
		if (rc == -DER_TX_RESTART) {
			D_DEBUG(DB_TRACE, "daos_obj_list_akey() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		} else if (rc) {
			D_ERROR("daos_obj_list_akey() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		/** nothing enumerated, continue loop */
		if (number == 0)
			continue;

		/*
		 * for every entry enumerated, check if it's an xattr, and
		 * insert it in the new entry.
		 */
		for (ptr = enum_buf, i = 0; i < number; i++) {
			/** if not an xattr, go to next entry */
			if (strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			/** set akey as the xattr name */
			d_iov_set(&iod.iod_name, ptr, kds[i].kd_key_len);
			d_iov_set(&fiov, val_buf, DFS_MAX_XATTR_LEN);

			/** fetch the xattr value from the src */
			rc = daos_obj_fetch(src_oh, th, 0, &src_dkey, 1, &iod, &fsgl, NULL, NULL);
			if (rc) {
				D_ERROR("daos_obj_fetch() failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			d_iov_set(&fiov, val_buf, iod.iod_size);

			/** add it to the destination */
			rc = daos_obj_update(dst_oh, th, 0, &dst_dkey, 1, &iod, &fsgl, NULL);
			if (rc) {
				D_ERROR("daos_obj_update() failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}
			ptr += kds[i].kd_key_len;
		}
	}

out:
	D_FREE(val_buf);
	return rc;
}

/* Returns oids for both moved and clobbered files, but does not check either of them */
int
dfs_move_internal(dfs_t *dfs, unsigned int flags, dfs_obj_t *parent, const char *name,
		  dfs_obj_t *new_parent, const char *new_name, daos_obj_id_t *moid,
		  daos_obj_id_t *oid)
{
	struct dfs_entry entry = {0}, new_entry = {0};
	daos_handle_t    th = dfs->th;
	bool             exists;
	daos_key_t       dkey;
	size_t           len;
	size_t           new_len;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;
	if (new_parent == NULL)
		new_parent = &dfs->root;
	else if (!S_ISDIR(new_parent->mode))
		return ENOTDIR;

	if (flags != 0) {
#ifdef RENAME_NOREPLACE
		if (flags != RENAME_NOREPLACE)
			return ENOTSUP;
#else
		return ENOTSUP;
#endif
	}

	rc = check_name(name, &len);
	if (rc)
		return rc;
	rc = check_name(new_name, &new_len);
	if (rc)
		return rc;

	/*
	 * TODO - more permission checks for source & target attributes needed
	 * (immutable, append).
	 */

	if (dfs->use_dtx) {
		rc = daos_tx_open(dfs->coh, &th, 0, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

restart:
	rc = fetch_entry(dfs->layout_v, parent->oh, th, name, len, true, &exists, &entry, 0, NULL,
			 NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc = ENOENT);

	if (moid)
		oid_cp(moid, entry.oid);

	rc = fetch_entry(dfs->layout_v, new_parent->oh, th, new_name, new_len, true, &exists,
			 &new_entry, 0, NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", new_name, rc);
		D_GOTO(out, rc);
	}

	if (exists) {
#ifdef RENAME_NOREPLACE
		if (flags & RENAME_NOREPLACE)
			D_GOTO(out, rc = EEXIST);
#endif

		if (S_ISDIR(new_entry.mode)) {
			uint32_t      nr = 0;
			daos_handle_t oh;

			/** if old entry not a dir, return error */
			if (!S_ISDIR(entry.mode)) {
				D_ERROR("Can't rename non dir over a dir\n");
				D_GOTO(out, rc = EINVAL);
			}

			/** make sure new dir is empty */
			rc = daos_obj_open(dfs->coh, new_entry.oid, DAOS_OO_RW, &oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_open() Failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			rc = get_num_entries(oh, th, &nr, true);
			if (rc) {
				D_ERROR("failed to check dir %s (%d)\n", new_name, rc);
				daos_obj_close(oh, NULL);
				D_GOTO(out, rc);
			}

			rc = daos_obj_close(oh, NULL);
			if (rc) {
				D_ERROR("daos_obj_close() Failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			if (nr != 0)
				D_GOTO(out, rc = ENOTEMPTY);
		}

		rc = remove_entry(dfs, th, new_parent->oh, new_name, new_len, new_entry);
		if (rc) {
			D_ERROR("Failed to remove entry %s (%d)\n", new_name, rc);
			D_GOTO(out, rc);
		}

		if (oid)
			oid_cp(oid, new_entry.oid);
	}

	/** rename symlink */
	if (S_ISLNK(entry.mode)) {
		rc = remove_entry(dfs, th, parent->oh, name, len, entry);
		if (rc) {
			D_ERROR("Failed to remove entry %s (%d)\n", name, rc);
			D_GOTO(out, rc);
		}

		rc = insert_entry(dfs->layout_v, parent->oh, th, new_name, new_len,
				  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry);
		if (rc)
			D_ERROR("Inserting new entry %s failed (%d)\n", new_name, rc);
		D_GOTO(out, rc);
	}

	struct timespec now;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	entry.mtime = entry.ctime = now.tv_sec;
	entry.mtime_nano = entry.ctime_nano = now.tv_nsec;

	/** insert old entry in new parent object */
	rc = insert_entry(dfs->layout_v, new_parent->oh, th, new_name, new_len,
			  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry);
	if (rc) {
		D_ERROR("Inserting entry %s DTX %d failed (%d)\n", new_name, dfs->use_dtx, rc);
		D_GOTO(out, rc);
	}

	/** cp the extended attributes if they exist */
	rc = xattr_copy(parent->oh, name, new_parent->oh, new_name, th);
	if (rc == ERESTART) {
		D_GOTO(out, rc);
	} else if (rc) {
		D_ERROR("Failed to copy extended attributes (%d)\n", rc);
		D_GOTO(out, rc);
	}

	/** remove the old entry from the old parent (just the dkey) */
	d_iov_set(&dkey, (void *)name, len);
	rc = daos_obj_punch_dkeys(parent->oh, th, dfs->use_dtx ? 0 : DAOS_COND_PUNCH, 1, &dkey,
				  NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name, rc);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	if (dfs->use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			if (rc != -DER_TX_RESTART)
				D_ERROR("daos_tx_commit() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

out:
	rc = check_tx(th, rc);
	if (rc == ERESTART)
		goto restart;

	if (entry.value) {
		D_ASSERT(S_ISLNK(entry.mode));
		D_FREE(entry.value);
	}
	if (new_entry.value) {
		D_ASSERT(S_ISLNK(new_entry.mode));
		D_FREE(new_entry.value);
	}
	return rc;
}

/* Wrapper function, only permit oid as a input parameter and return if it has data */
int
dfs_move(dfs_t *dfs, dfs_obj_t *parent, const char *name, dfs_obj_t *new_parent,
	 const char *new_name, daos_obj_id_t *oid)
{
	return dfs_move_internal(dfs, 0, parent, name, new_parent, new_name, NULL, oid);
}

int
dfs_exchange(dfs_t *dfs, dfs_obj_t *parent1, const char *name1, dfs_obj_t *parent2,
	     const char *name2)
{
	struct dfs_entry entry1 = {0}, entry2 = {0};
	daos_handle_t    th = dfs->th;
	bool             exists;
	daos_key_t       dkey;
	size_t           len1;
	size_t           len2;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent1 == NULL)
		parent1 = &dfs->root;
	else if (!S_ISDIR(parent1->mode))
		return ENOTDIR;
	if (parent2 == NULL)
		parent2 = &dfs->root;
	else if (!S_ISDIR(parent2->mode))
		return ENOTDIR;

	rc = check_name(name1, &len1);
	if (rc)
		return rc;
	rc = check_name(name2, &len2);
	if (rc)
		return rc;

	if (dfs->use_dtx) {
		rc = daos_tx_open(dfs->coh, &th, 0, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

restart:
	rc = fetch_entry(dfs->layout_v, parent1->oh, th, name1, len1, true, &exists, &entry1, 0,
			 NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name1, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc = EINVAL);

	rc = fetch_entry(dfs->layout_v, parent2->oh, th, name2, len2, true, &exists, &entry2, 0,
			 NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name2, rc);
		D_GOTO(out, rc);
	}

	if (exists == false)
		D_GOTO(out, rc = EINVAL);

	/** remove the first entry from parent1 (just the dkey) */
	d_iov_set(&dkey, (void *)name1, len1);
	rc = daos_obj_punch_dkeys(parent1->oh, th, 0, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name1, rc);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	/** remove the second entry from parent2 (just the dkey) */
	d_iov_set(&dkey, (void *)name2, len2);
	rc = daos_obj_punch_dkeys(parent2->oh, th, 0, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Punch entry %s failed (%d)\n", name2, rc);
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	struct timespec now;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out, rc = errno);
	entry1.mtime = entry1.ctime = now.tv_sec;
	entry1.mtime_nano = entry1.ctime_nano = now.tv_nsec;

	/** insert entry1 in parent2 object */
	rc = insert_entry(dfs->layout_v, parent2->oh, th, name1, len1,
			  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry1);
	if (rc) {
		D_ERROR("Inserting entry %s failed (%d)\n", name1, rc);
		D_GOTO(out, rc);
	}

	entry2.mtime = entry2.ctime = now.tv_sec;
	entry2.mtime_nano = entry2.ctime_nano = now.tv_nsec;

	/** insert entry2 in parent1 object */
	rc = insert_entry(dfs->layout_v, parent1->oh, th, name2, len2,
			  dfs->use_dtx ? 0 : DAOS_COND_DKEY_INSERT, &entry2);
	if (rc) {
		D_ERROR("Inserting entry %s failed (%d)\n", name2, rc);
		D_GOTO(out, rc);
	}

	if (dfs->use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			if (rc != -DER_TX_RESTART)
				D_ERROR("daos_tx_commit() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

out:
	rc = check_tx(th, rc);
	if (rc == ERESTART)
		goto restart;

	if (entry1.value) {
		D_ASSERT(S_ISLNK(entry1.mode));
		D_FREE(entry1.value);
	}
	if (entry2.value) {
		D_ASSERT(S_ISLNK(entry2.mode));
		D_FREE(entry2.value);
	}
	return rc;
}
