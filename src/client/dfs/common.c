/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/common.h>
#include <daos/container.h>
#include <daos/object.h>

#include "dfs_internal.h"

static int
decode_one_hint(char *hint, uint32_t rf, daos_oclass_hints_t *obj_hint, enum daos_otype_t *type)
{
	char *name;
	char *val;
	char *save = NULL;

	/** get object type (file or dir) */
	name = strtok_r(hint, ":", &save);
	if (name == NULL) {
		D_ERROR("Invalid object type in hint: %s\n", hint);
		return EINVAL;
	}
	if (strcasecmp(name, "dir") == 0 || strcasecmp(name, "directory") == 0) {
		*type = DAOS_OT_MULTI_HASHED;
		/** get hint value */
		val = strtok_r(NULL, "", &save);
		if (val == NULL) {
			D_ERROR("Invalid Hint value for directory type (%s)\n", hint);
			return EINVAL;
		}
		if (strcasecmp(val, "single") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_TINY;
			else
				*obj_hint = DAOS_OCH_SHD_TINY | DAOS_OCH_RDD_RP;
		} else if (strcasecmp(val, "max") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_MAX;
			else
				*obj_hint = DAOS_OCH_SHD_MAX | DAOS_OCH_RDD_RP;
		} else {
			D_ERROR("Invalid directory hint: %s\n", val);
			return EINVAL;
		}
	} else if (strcasecmp(name, "file") == 0) {
		*type = DAOS_OT_ARRAY_BYTE;
		/** get hint value */
		val = strtok_r(NULL, "", &save);
		if (val == NULL) {
			D_ERROR("Invalid Hint value for file type (%s)\n", hint);
			return EINVAL;
		}
		if (strcasecmp(val, "single") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_TINY;
			else
				*obj_hint = DAOS_OCH_SHD_TINY | DAOS_OCH_RDD_RP;
		} else if (strcasecmp(val, "max") == 0) {
			if (rf == 0)
				*obj_hint = DAOS_OCH_SHD_MAX;
			else
				*obj_hint = DAOS_OCH_SHD_MAX | DAOS_OCH_RDD_EC;
		} else {
			D_ERROR("Invalid file hint: %s\n", val);
			return EINVAL;
		}
	} else {
		D_ERROR("Invalid object type in hint: %s\n", name);
		return EINVAL;
	}

	return 0;
}

int
get_oclass_hints(const char *hints, daos_oclass_hints_t *dir_hints, daos_oclass_hints_t *file_hints,
		 uint64_t rf)
{
	char *end_token = NULL;
	char *t;
	char *local;
	int   rc = 0;

	D_ASSERT(hints);
	*dir_hints = *file_hints = 0;
	D_STRNDUP(local, hints, DAOS_CONT_HINT_MAX_LEN);
	if (!local)
		return ENOMEM;

	/** get a hint value pair */
	t = strtok_r(local, ",", &end_token);
	if (t == NULL) {
		D_ERROR("Invalid hint format: %s\n", hints);
		D_GOTO(out, rc = EINVAL);
	}

	while (t != NULL) {
		daos_oclass_hints_t obj_hint = 0;
		enum daos_otype_t   type;

		rc = decode_one_hint(t, rf, &obj_hint, &type);
		if (rc)
			D_GOTO(out, rc);

		if (type == DAOS_OT_ARRAY_BYTE)
			*file_hints = obj_hint;
		else
			*dir_hints = obj_hint;
		t = strtok_r(NULL, ",", &end_token);
	}

out:
	D_FREE(local);
	return rc;
}

int
fetch_entry(dfs_layout_ver_t ver, daos_handle_t oh, daos_handle_t th, const char *name, size_t len,
	    bool fetch_sym, bool *exists, struct dfs_entry *entry, int xnr, char *xnames[],
	    void *xvals[], daos_size_t *xsizes)
{
	d_sg_list_t  l_sgl, *sgl;
	d_iov_t      sg_iovs[INODE_AKEYS];
	daos_iod_t   l_iod, *iod;
	daos_recx_t  recx;
	daos_key_t   dkey;
	unsigned int i;
	char       **pxnames = NULL;
	d_iov_t     *sg_iovx = NULL;
	d_sg_list_t *sgls    = NULL;
	daos_iod_t  *iods    = NULL;
	int          rc;

	D_ASSERT(name);

	/** TODO - not supported yet */
	if (strcmp(name, ".") == 0)
		return ENOTSUP;

	if (xnr) {
		D_ALLOC_ARRAY(pxnames, xnr);
		if (pxnames == NULL)
			D_GOTO(out, rc = ENOMEM);

		D_ALLOC_ARRAY(sg_iovx, xnr);
		if (sg_iovx == NULL)
			D_GOTO(out, rc = ENOMEM);

		D_ALLOC_ARRAY(sgls, xnr + 1);
		if (sgls == NULL)
			D_GOTO(out, rc = ENOMEM);

		D_ALLOC_ARRAY(iods, xnr + 1);
		if (iods == NULL)
			D_GOTO(out, rc = ENOMEM);

		for (i = 0; i < xnr; i++) {
			pxnames[i] = concat("x:", xnames[i]);
			if (pxnames[i] == NULL)
				D_GOTO(out, rc = ENOMEM);

			d_iov_set(&iods[i].iod_name, pxnames[i], strlen(pxnames[i]));
			iods[i].iod_nr    = 1;
			iods[i].iod_recxs = NULL;
			iods[i].iod_type  = DAOS_IOD_SINGLE;
			iods[i].iod_size  = xsizes[i];

			d_iov_set(&sg_iovx[i], xvals[i], xsizes[i]);
			sgls[i].sg_nr     = 1;
			sgls[i].sg_nr_out = 0;
			sgls[i].sg_iovs   = &sg_iovx[i];
		}

		sgl = &sgls[xnr];
		iod = &iods[xnr];
	} else {
		sgl = &l_sgl;
		iod = &l_iod;
	}

	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iod->iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod->iod_nr    = 1;
	recx.rx_idx    = 0;
	recx.rx_nr     = END_IDX;
	iod->iod_recxs = &recx;
	iod->iod_type  = DAOS_IOD_ARRAY;
	iod->iod_size  = 1;

	i = 0;
	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->uid, sizeof(uid_t));
	d_iov_set(&sg_iovs[i++], &entry->gid, sizeof(gid_t));
	d_iov_set(&sg_iovs[i++], &entry->value_len, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->obj_hlc, sizeof(uint64_t));

	sgl->sg_nr     = i;
	sgl->sg_nr_out = 0;
	sgl->sg_iovs   = sg_iovs;

	rc = daos_obj_fetch(oh, th, DAOS_COND_DKEY_FETCH, &dkey, xnr + 1, iods ? iods : iod,
			    sgls ? sgls : sgl, NULL, NULL);
	if (rc == -DER_NONEXIST) {
		*exists = false;
		D_GOTO(out, rc = 0);
	} else if (rc) {
		D_ERROR("Failed to fetch entry %s " DF_RC "\n", name, DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	for (i = 0; i < xnr; i++)
		xsizes[i] = iods[i].iod_size;

	if (fetch_sym && S_ISLNK(entry->mode)) {
		char       *value;
		daos_size_t val_len;

		/** symlink is empty */
		if (entry->value_len == 0)
			D_GOTO(out, rc = EIO);
		val_len = entry->value_len;
		D_ALLOC(value, val_len + 1);
		if (value == NULL)
			D_GOTO(out, rc = ENOMEM);

		d_iov_set(&iod->iod_name, SLINK_AKEY_NAME, sizeof(SLINK_AKEY_NAME) - 1);
		iod->iod_nr    = 1;
		iod->iod_recxs = NULL;
		iod->iod_type  = DAOS_IOD_SINGLE;
		iod->iod_size  = DAOS_REC_ANY;

		d_iov_set(&sg_iovs[0], value, val_len);
		sgl->sg_nr     = 1;
		sgl->sg_nr_out = 0;
		sgl->sg_iovs   = sg_iovs;

		rc = daos_obj_fetch(oh, th, DAOS_COND_DKEY_FETCH, &dkey, 1, iod, sgl, NULL, NULL);
		if (rc) {
			D_FREE(value);
			if (rc == -DER_NONEXIST) {
				*exists = false;
				D_GOTO(out, rc = 0);
			}
			D_ERROR("Failed to fetch entry %s " DF_RC "\n", name, DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		/** make sure that the akey value size matches what is in the inode */
		if (iod->iod_size != val_len) {
			D_ERROR("Symlink value length inconsistent with inode data\n");
			D_GOTO(out, rc = EIO);
		}
		value[val_len] = 0;

		if (entry->value_len != 0) {
			entry->value = value;
		} else {
			D_ERROR("Failed to load value for symlink\n");
			D_FREE(value);
			D_GOTO(out, rc = EIO);
		}
	}

	if (sgl->sg_nr_out == 0)
		*exists = false;
	else
		*exists = true;

	entry->ref_cnt = 1;
out:
	if (xnr) {
		if (pxnames) {
			for (i = 0; i < xnr; i++)
				D_FREE(pxnames[i]);
			D_FREE(pxnames);
		}
		D_FREE(sg_iovx);
		D_FREE(sgls);
		D_FREE(iods);
	}
	return rc;
}

int
hlm_fetch_entry(daos_handle_t hlm_oh, daos_handle_t th, daos_obj_id_t *oid, struct dfs_entry *entry)
{
	d_sg_list_t  sgl;
	d_iov_t      sg_iovs[HLM_INODE_AKEYS];
	daos_iod_t   iod;
	daos_recx_t  recx;
	daos_key_t   dkey;
	unsigned int i;
	int          rc;

	if (oid == NULL || entry == NULL)
		return EINVAL;

	/* Set dkey as the file's OID */
	d_iov_set(&dkey, oid, sizeof(daos_obj_id_t));

	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = 1;
	recx.rx_idx   = 0;
	recx.rx_nr    = END_HLM_IDX;
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	i = 0;
	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->uid, sizeof(uid_t));
	d_iov_set(&sg_iovs[i++], &entry->gid, sizeof(gid_t));
	d_iov_set(&sg_iovs[i++], &entry->value_len, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->obj_hlc, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ref_cnt, sizeof(uint64_t));

	sgl.sg_nr     = i;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = sg_iovs;

	rc = daos_obj_fetch(hlm_oh, th, DAOS_COND_DKEY_FETCH, &dkey, 1, &iod, &sgl, NULL, NULL);
	if (rc == -DER_NONEXIST) {
		D_ERROR("Entry not found in HLM\n");
		return ENOENT;
	} else if (rc) {
		D_ERROR("Failed to fetch entry from HLM " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (sgl.sg_nr_out == 0) {
		D_ERROR("Entry not found in HLM\n");
		return ENOENT;
	}

	return 0;
}

/**
 * Fetch entry for a dfs object, handling hardlinks transparently.
 * If the object is a hardlink, fetches from HLM and sets parent_oh to DAOS_HDL_INVAL.
 * Otherwise, opens the parent object and fetches the entry from the parent directory.
 * If during fetch we discover the entry has become a hardlink (converted by another
 * DFS instance), we update obj->mode and retry from HLM.
 *
 * \param dfs       DFS handle
 * \param th        Transaction handle (can be DAOS_TX_NONE)
 * \param obj       DFS object to fetch entry for
 * \param parent_oh Output parent object handle (DAOS_HDL_INVAL if hardlink)
 * \param entry     Output entry structure
 * \return          0 on success, errno on failure
 */
int
dfsobj_fetch_entry(dfs_t *dfs, daos_handle_t th, dfs_obj_t *obj, daos_handle_t *parent_oh,
		   struct dfs_entry *entry)
{
	bool exists;
	int  rc;

retry:
	if (dfs_is_hardlink(obj->mode)) {
		/* Fetch entry from HLM using obj's oid as dkey */
		rc = hlm_fetch_entry(dfs->hlm_oh, th, &obj->oid, entry);
		if (rc) {
			D_ERROR("Failed to fetch entry from HLM (%d)\n", rc);
			return rc;
		}
		*parent_oh = DAOS_HDL_INVAL;
	} else {
		/* Open the parent directory object */
		rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, parent_oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() failed (%d)\n", rc);
			return daos_der2errno(rc);
		}

		/* Fetch entry from parent directory */
		rc = fetch_entry(dfs->layout_v, *parent_oh, th, obj->name, strlen(obj->name), false,
				 &exists, entry, 0, NULL, NULL, NULL);
		if (rc) {
			D_ERROR("Failed to fetch entry (%d)\n", rc);
			daos_obj_close(*parent_oh, NULL);
			*parent_oh = DAOS_HDL_INVAL;
			return rc;
		}
		if (!exists) {
			daos_obj_close(*parent_oh, NULL);
			*parent_oh = DAOS_HDL_INVAL;
			return ENOENT;
		}

		/* Verify OID matches - entry may have been replaced */
		if (obj->oid.hi != entry->oid.hi || obj->oid.lo != entry->oid.lo) {
			daos_obj_close(*parent_oh, NULL);
			*parent_oh = DAOS_HDL_INVAL;
			return ENOENT;
		}

		/*
		 * Check if the entry has become a hardlink (another DFS instance
		 * may have converted it). If so, update obj->mode and retry.
		 */
		if (unlikely(dfs_is_hardlink(entry->mode))) {
			daos_obj_close(*parent_oh, NULL);
			*parent_oh = DAOS_HDL_INVAL;
			obj->mode  = entry->mode;
			goto retry;
		}
	}

	return 0;
}

/**
 * Punch the dkey entry from HLM object.
 */
static int
hlm_punch_entry(dfs_t *dfs, daos_handle_t th, daos_obj_id_t *oid)
{
	daos_key_t dkey;
	int        rc;

	d_iov_set(&dkey, oid, sizeof(daos_obj_id_t));
	rc = daos_obj_punch_dkeys(dfs->hlm_oh, th, 0, 1, &dkey, NULL);
	if (rc) {
		D_ERROR("Failed to punch HLM entry " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	return 0;
}

/**
 * Update (increment or decrement) the reference count in HLM for a hardlinked file.
 * Updates entry with ref_cnt, ctime, and ctime_nano that are written to HLM.
 * \param delta  Positive to increment, negative to decrement.
 */
int
hlm_update_ref_cnt(dfs_t *dfs, daos_handle_t th, struct dfs_entry *entry, int delta)
{
	daos_key_t      dkey;
	d_sg_list_t     sgl;
	d_iov_t         sg_iovs[3];
	daos_iod_t      iod;
	daos_recx_t     recxs[3];
	struct timespec now;
	int             rc;

	if (dfs == NULL || entry == NULL)
		return EINVAL;

	/* Protect against underflow */
	if (delta < 0 && entry->ref_cnt < (uint64_t)(-delta)) {
		D_ERROR("ref_cnt underflow: ref_cnt=%lu, delta=%d\n", entry->ref_cnt, delta);
		return EINVAL;
	}

	/* Update ref_cnt */
	entry->ref_cnt += delta;

	/* Set ctime to current time */
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		return errno;

	entry->ctime      = now.tv_sec;
	entry->ctime_nano = now.tv_nsec;

	/* Update ref_cnt and ctime in HLM */
	d_iov_set(&dkey, &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr      = 3;
	recxs[0].rx_idx = REF_CNT_IDX;
	recxs[0].rx_nr  = sizeof(uint64_t);
	recxs[1].rx_idx = CTIME_IDX;
	recxs[1].rx_nr  = sizeof(uint64_t);
	recxs[2].rx_idx = CTIME_NSEC_IDX;
	recxs[2].rx_nr  = sizeof(uint64_t);
	iod.iod_recxs   = recxs;
	iod.iod_type    = DAOS_IOD_ARRAY;
	iod.iod_size    = 1;

	d_iov_set(&sg_iovs[0], &entry->ref_cnt, sizeof(uint64_t));
	d_iov_set(&sg_iovs[1], &entry->ctime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[2], &entry->ctime_nano, sizeof(uint64_t));
	sgl.sg_nr     = 3;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = sg_iovs;

	rc = daos_obj_update(dfs->hlm_oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update ref_cnt in HLM " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	return 0;
}

int
remove_entry(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh, const char *name, size_t len,
	     struct dfs_entry entry, bool *deleted)
{
	daos_key_t    dkey;
	daos_handle_t oh;
	daos_handle_t local_th = th;
	bool          own_tx   = false;
	int           rc;

	/* Assume file is deleted unless it's a hardlink with ref_cnt > 1 */
	if (deleted)
		*deleted = true;

	/*
	 * For hardlinks, if no transaction was passed, create a local one to ensure
	 * consistency across multiple updates (dentry punch, HLM update, file punch).
	 */
	if (dfs_is_hardlink(entry.mode) && !daos_handle_is_valid(th)) {
		rc = daos_tx_open(dfs->coh, &local_th, 0, NULL);
		if (rc) {
			D_ERROR("daos_tx_open() failed (%d)\n", rc);
			return daos_der2errno(rc);
		}
		own_tx = true;
	}

restart:
	/* First, punch the dentry from parent */
	d_iov_set(&dkey, (void *)name, len);
	/** we only need a conditional dkey punch if we are not using a DTX */
	rc = daos_obj_punch_dkeys(parent_oh, local_th,
				  daos_handle_is_valid(local_th) ? 0 : DAOS_COND_PUNCH, 1, &dkey,
				  NULL);
	if (rc) {
		rc = daos_der2errno(rc);
		D_GOTO(out, rc);
	}

	if (S_ISLNK(entry.mode))
		D_GOTO(commit, rc = 0);

	/* If hardlink bit is set, handle hardlink-specific logic */
	if (dfs_is_hardlink(entry.mode)) {
		struct dfs_entry hlm_entry = {0};

		/* Fetch entry from HLM to check ref_cnt */
		rc = hlm_fetch_entry(dfs->hlm_oh, local_th, &entry.oid, &hlm_entry);
		if (rc)
			D_GOTO(out, rc);

		/* If ref_cnt is 1, punch HLM entry and file object */
		if (hlm_entry.ref_cnt == 1) {
			/* Punch the dkey entry from HLM */
			rc = hlm_punch_entry(dfs, local_th, &entry.oid);
			if (rc) {
				D_ERROR("Failed to punch HLM entry (%d)\n", rc);
				D_GOTO(out, rc);
			}

			/* Punch the file object */
			rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
			if (rc) {
				rc = daos_der2errno(rc);
				D_GOTO(out, rc);
			}

			rc = daos_obj_punch(oh, local_th, 0, NULL);
			daos_obj_close(oh, NULL);
			if (rc) {
				rc = daos_der2errno(rc);
				D_GOTO(out, rc);
			}
		} else {
			/* Decrement ref_cnt in HLM - file is NOT deleted */
			if (deleted)
				*deleted = false;
			rc = hlm_update_ref_cnt(dfs, local_th, &hlm_entry, -1);
			if (rc) {
				D_ERROR("Failed to decrement ref_cnt in HLM (%d)\n", rc);
				D_GOTO(out, rc);
			}
		}
		D_GOTO(commit, rc = 0);
	}

	/* Regular file (not hardlink) - punch the file object */
	rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		rc = daos_der2errno(rc);
		D_GOTO(out, rc);
	}

	rc = daos_obj_punch(oh, local_th, 0, NULL);
	daos_obj_close(oh, NULL);
	if (rc) {
		rc = daos_der2errno(rc);
		D_GOTO(out, rc);
	}

commit:
	if (own_tx) {
		rc = daos_tx_commit(local_th, NULL);
		if (rc) {
			if (rc != -DER_TX_RESTART)
				D_ERROR("daos_tx_commit() failed (%d)\n", rc);
			rc = daos_der2errno(rc);
			D_GOTO(out, rc);
		}
	}

out:
	if (own_tx && daos_handle_is_valid(local_th)) {
		if (rc == ERESTART) {
			int rc2 = daos_tx_restart(local_th, NULL);
			if (rc2 == 0)
				goto restart;
			rc = daos_der2errno(rc2);
		}
		daos_tx_close(local_th, NULL);
	}
	return rc;
}

int
insert_entry(dfs_layout_ver_t ver, daos_handle_t oh, daos_handle_t th, const char *name, size_t len,
	     uint64_t flags, struct dfs_entry *entry)
{
	d_sg_list_t  sgls[2];
	d_iov_t      sg_iovs[INODE_AKEYS];
	d_iov_t      sym_iov;
	daos_iod_t   iods[2];
	daos_recx_t  recx;
	daos_key_t   dkey;
	unsigned int i;
	unsigned int nr_iods;
	int          rc;

	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iods[0].iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iods[0].iod_nr    = 1;
	recx.rx_idx       = 0;
	recx.rx_nr        = END_IDX;
	iods[0].iod_recxs = &recx;
	iods[0].iod_type  = DAOS_IOD_ARRAY;
	iods[0].iod_size  = 1;

	i = 0;
	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->uid, sizeof(uid_t));
	d_iov_set(&sg_iovs[i++], &entry->gid, sizeof(gid_t));
	/** Add file size / symlink length. for now, file size cached in the entry is 0. */
	d_iov_set(&sg_iovs[i++], &entry->value_len, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->obj_hlc, sizeof(uint64_t));

	/** add the symlink as a separate akey */
	if (S_ISLNK(entry->mode)) {
		nr_iods = 2;
		d_iov_set(&iods[1].iod_name, SLINK_AKEY_NAME, sizeof(SLINK_AKEY_NAME) - 1);
		iods[1].iod_nr    = 1;
		iods[1].iod_recxs = NULL;
		iods[1].iod_type  = DAOS_IOD_SINGLE;
		iods[1].iod_size  = entry->value_len;

		d_iov_set(&sym_iov, entry->value, entry->value_len);
		sgls[1].sg_nr     = 1;
		sgls[1].sg_nr_out = 0;
		sgls[1].sg_iovs   = &sym_iov;
	} else {
		nr_iods = 1;
	}

	sgls[0].sg_nr     = i;
	sgls[0].sg_nr_out = 0;
	sgls[0].sg_iovs   = sg_iovs;

	rc = daos_obj_update(oh, th, flags, &dkey, nr_iods, iods, sgls, NULL);
	if (rc) {
		/** don't log error if conditional failed */
		if (rc != -DER_EXIST && rc != -DER_NO_PERM)
			D_ERROR("Failed to insert entry '%s', " DF_RC "\n", name, DP_RC(rc));
		return daos_der2errno(rc);
	}
	return 0;
}

int
get_num_entries(daos_handle_t oh, daos_handle_t th, uint32_t *nr, bool check_empty)
{
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_anchor_t   anchor = {0};
	uint32_t        key_nr = 0;
	d_sg_list_t     sgl;
	d_iov_t         iov;
	char            enum_buf[ENUM_DESC_BUF] = {0};

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_DESC_BUF);
	sgl.sg_iovs = &iov;

	/** TODO - Enum of links is expensive. Need to make this faster */
	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t number = ENUM_DESC_NR;
		int      rc;

		rc = daos_obj_list_dkey(oh, th, &number, kds, &sgl, &anchor, NULL);
		if (rc)
			return daos_der2errno(rc);

		if (number == 0)
			continue;

		key_nr += number;

		/** if we just want to check if entries exist, break now */
		if (check_empty)
			break;
	}

	*nr = key_nr;
	return 0;
}

int
update_stbuf_times(struct dfs_entry entry, daos_epoch_t max_epoch, struct stat *stbuf,
		   uint64_t *obj_hlc)
{
	struct timespec obj_mtime, entry_mtime;
	int             rc;

	/** the file/dir have not been touched, so the entry times are accurate */
	if (max_epoch == 0) {
		stbuf->st_ctim.tv_sec  = entry.ctime;
		stbuf->st_ctim.tv_nsec = entry.ctime_nano;
		stbuf->st_mtim.tv_sec  = entry.mtime;
		stbuf->st_mtim.tv_nsec = entry.mtime_nano;
		return 0;
	}

	rc = d_hlc2timespec(max_epoch, &obj_mtime);
	if (rc) {
		D_ERROR("d_hlc2timespec() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	if (obj_hlc)
		*obj_hlc = max_epoch;

	rc = d_hlc2timespec(entry.obj_hlc, &entry_mtime);
	if (rc) {
		D_ERROR("d_hlc2timespec() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	/** ctime should be the greater of the entry and object hlc */
	stbuf->st_ctim.tv_sec  = entry.ctime;
	stbuf->st_ctim.tv_nsec = entry.ctime_nano;
	if (tspec_gt(obj_mtime, stbuf->st_ctim)) {
		stbuf->st_ctim.tv_sec  = obj_mtime.tv_sec;
		stbuf->st_ctim.tv_nsec = obj_mtime.tv_nsec;
	}

	/*
	 * mtime is not like ctime since user can update it manually. So returning the larger mtime
	 * like ctime would not work since the user can manually set the mtime to the past.
	 */
	if (obj_mtime.tv_sec == entry_mtime.tv_sec && obj_mtime.tv_nsec == entry_mtime.tv_nsec) {
		/*
		 * internal mtime entry set through a user set mtime and is up to date with the
		 * object epoch time, which means that the user set mtime in the inode entry is the
		 * correct value to return.
		 */
		stbuf->st_mtim.tv_sec  = entry.mtime;
		stbuf->st_mtim.tv_nsec = entry.mtime_nano;
	} else {
		/*
		 * the user has not updated the mtime explicitly or the object itself was modified
		 * after an explicit mtime update.
		 */
		stbuf->st_mtim.tv_sec  = obj_mtime.tv_sec;
		stbuf->st_mtim.tv_nsec = obj_mtime.tv_nsec;
	}

	return 0;
}

int
entry_stat(dfs_t *dfs, daos_handle_t th, daos_handle_t oh, const char *name, size_t len,
	   struct dfs_obj *obj, bool get_size, struct stat *stbuf, uint64_t *obj_hlc)
{
	struct dfs_entry entry = {0};
	bool             exists;
	daos_size_t      size;
	int              rc;

	memset(stbuf, 0, sizeof(struct stat));

	/* If obj is provided and already known to be a hardlink, fetch directly from HLM */
	if (obj && dfs_is_hardlink(obj->mode)) {
		rc = hlm_fetch_entry(dfs->hlm_oh, th, &obj->oid, &entry);
		if (rc) {
			D_ERROR("Failed to fetch entry '%s' oid=" DF_OID " from HLM (%d)\n", name,
				DP_OID(obj->oid), rc);
			return rc;
		}
	} else {
		/** Check if parent has the entry. */
		rc = fetch_entry(dfs->layout_v, oh, th, name, len, false, &exists, &entry, 0, NULL,
				 NULL, NULL);
		if (rc)
			return rc;

		if (!exists)
			return ENOENT;

		if (obj && (obj->oid.hi != entry.oid.hi || obj->oid.lo != entry.oid.lo))
			return ENOENT;

		/* If dentry indicates hardlink, update obj->mode and retry from HLM */
		if (dfs_is_hardlink(entry.mode)) {
			if (obj)
				obj->mode = entry.mode;
			rc = hlm_fetch_entry(dfs->hlm_oh, th, &entry.oid, &entry);
			if (rc) {
				D_ERROR("Failed to fetch entry '%s' oid=" DF_OID " from HLM (%d)\n",
					name, DP_OID(entry.oid), rc);
				return rc;
			}
		}
	}

	switch (entry.mode & S_IFMT) {
	case S_IFDIR: {
		daos_handle_t dir_oh;
		daos_epoch_t  ep;

		size = sizeof(entry);

		/** check if dir is empty */
		rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RO, &dir_oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed, " DF_RC "\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		/** Use DAOS_TX_NONE - query doesn't need to be part of DTX */
		rc = daos_obj_query_max_epoch(dir_oh, DAOS_TX_NONE, &ep, NULL);
		if (rc) {
			daos_obj_close(dir_oh, NULL);
			return daos_der2errno(rc);
		}

		rc = daos_obj_close(dir_oh, NULL);
		if (rc)
			return daos_der2errno(rc);

		/** object was updated since creation */
		rc = update_stbuf_times(entry, ep, stbuf, obj_hlc);
		if (rc)
			return rc;
		break;
	}
	case S_IFREG: {
		daos_array_stbuf_t array_stbuf = {0};

		stbuf->st_blksize = entry.chunk_size ? entry.chunk_size : dfs->attr.da_chunk_size;

		/** don't stat the array and use the entry mtime */
		if (!get_size) {
			stbuf->st_mtim.tv_sec  = entry.mtime;
			stbuf->st_mtim.tv_nsec = entry.mtime_nano;
			size                   = 0;
			break;
		}

		if (obj) {
			rc = daos_array_stat(obj->oh, th, &array_stbuf, NULL);
			if (rc)
				return daos_der2errno(rc);
		} else {
			daos_handle_t file_oh;

			rc = daos_array_open_with_attr(dfs->coh, entry.oid, th, DAOS_OO_RO, 1,
						       entry.chunk_size ? entry.chunk_size
									: dfs->attr.da_chunk_size,
						       &file_oh, NULL);
			if (rc) {
				D_ERROR("daos_array_open_with_attr() failed " DF_RC "\n",
					DP_RC(rc));
				return daos_der2errno(rc);
			}

			rc = daos_array_stat(file_oh, th, &array_stbuf, NULL);
			if (rc) {
				daos_array_close(file_oh, NULL);
				return daos_der2errno(rc);
			}

			rc = daos_array_close(file_oh, NULL);
			if (rc)
				return daos_der2errno(rc);
		}

		size = array_stbuf.st_size;
		rc   = update_stbuf_times(entry, array_stbuf.st_max_epoch, stbuf, obj_hlc);
		if (rc)
			return rc;

		/*
		 * TODO - this is not accurate since it does not account for sparse files or file
		 * metadata or xattributes.
		 */
		stbuf->st_blocks = (size + (1 << 9) - 1) >> 9;
		break;
	}
	case S_IFLNK:
		size = entry.value_len;
		D_FREE(entry.value);
		stbuf->st_mtim.tv_sec  = entry.mtime;
		stbuf->st_mtim.tv_nsec = entry.mtime_nano;
		stbuf->st_ctim.tv_sec  = entry.ctime;
		stbuf->st_ctim.tv_nsec = entry.ctime_nano;
		break;
	default:
		D_ERROR("Invalid entry type (not a dir, file, symlink).\n");
		return EINVAL;
	}

	stbuf->st_nlink = entry.ref_cnt;
	stbuf->st_size  = size;
	stbuf->st_mode  = entry.mode & ~MODE_HARDLINK_BIT;
	stbuf->st_uid   = entry.uid;
	stbuf->st_gid   = entry.gid;
	if (tspec_gt(stbuf->st_ctim, stbuf->st_mtim)) {
		stbuf->st_atim.tv_sec  = stbuf->st_ctim.tv_sec;
		stbuf->st_atim.tv_nsec = stbuf->st_ctim.tv_nsec;
	} else {
		stbuf->st_atim.tv_sec  = stbuf->st_mtim.tv_sec;
		stbuf->st_atim.tv_nsec = stbuf->st_mtim.tv_nsec;
	}

	DFS_OP_STAT_INCR(dfs, DOS_STAT);
	return 0;
}

/*
 * Create a dir object. If caller passes parent obj, and cid is not set,
 * the child oclass is taken from the parent.
 */
int
create_dir(dfs_t *dfs, dfs_obj_t *parent, daos_oclass_id_t cid, dfs_obj_t *dir)
{
	int rc;

	/** set oclass for dir. order: API, parent dir, cont default */
	if (cid == 0) {
		if (parent->d.oclass == 0)
			cid = dfs->attr.da_dir_oclass_id;
		else {
			cid = parent->d.oclass;
			/*
			 * If the parent oclass is EC, do not use that for a directory and use the
			 * container default instead.
			 */
			if (daos_cid_is_ec(cid))
				cid = dfs->attr.da_dir_oclass_id;
		}
	}

	/** Allocate an OID for the dir - local operation */
	rc = oid_gen(dfs, cid, false, &dir->oid);
	if (rc != 0)
		return rc;

	/** Open the Object - local operation */
	rc = daos_obj_open(dfs->coh, dir->oid, DAOS_OO_RW, &dir->oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed, " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	return 0;
}

int
open_dir(dfs_t *dfs, dfs_obj_t *parent, int flags, daos_oclass_id_t cid, struct dfs_entry *entry,
	 size_t len, dfs_obj_t *dir)
{
	bool          exists;
	int           daos_mode;
	bool          oexcl  = flags & O_EXCL;
	bool          ocreat = flags & O_CREAT;
	daos_handle_t parent_oh;
	int           rc;

	parent_oh = parent ? parent->oh : dfs->super_oh;

	if (ocreat) {
		struct timespec now;

		D_ASSERT(parent);
		/** this generates the OID and opens the object */
		rc = create_dir(dfs, parent, cid, dir);
		if (rc)
			return rc;

		entry->oid  = dir->oid;
		entry->mode = dir->mode;
		rc          = clock_gettime(CLOCK_REALTIME, &now);
		if (rc) {
			rc = errno;
			daos_obj_close(dir->oh, NULL);
			return rc;
		}
		entry->mtime = entry->ctime = now.tv_sec;
		entry->mtime_nano = entry->ctime_nano = now.tv_nsec;
		entry->chunk_size                     = parent->d.chunk_size;
		entry->oclass                         = parent->d.oclass;

		/** since it's a single conditional op, we don't need a DTX */
		rc = insert_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, dir->name, len,
				  DAOS_COND_DKEY_INSERT, entry);
		if (rc == EEXIST && !oexcl) {
			/** just try fetching entry to open the file */
			daos_obj_close(dir->oh, NULL);
		} else if (rc) {
			daos_obj_close(dir->oh, NULL);
			D_DEBUG(DB_TRACE, "Insert dir entry %s failed (%d)\n", dir->name, rc);
			return rc;
		} else {
			/** Success, commit */
			D_ASSERT(rc == 0);
			dir->d.chunk_size = entry->chunk_size;
			dir->d.oclass     = entry->oclass;
			DFS_OP_STAT_INCR(dfs, DOS_MKDIR);
			return 0;
		}
	}

	/* Check if parent has the dirname entry */
	rc = fetch_entry(dfs->layout_v, parent_oh, dfs->th, dir->name, len, false, &exists, entry,
			 0, NULL, NULL, NULL);
	if (rc) {
		D_DEBUG(DB_TRACE, "fetch_entry %s failed %d.\n", dir->name, rc);
		return rc;
	}

	if (!exists)
		return ENOENT;

	/* Check that the opened object is the type that's expected. */
	if (!S_ISDIR(entry->mode))
		return ENOTDIR;

	daos_mode = get_daos_obj_mode(flags);
	if (daos_mode == -1)
		return EINVAL;

	rc = daos_obj_open(dfs->coh, entry->oid, daos_mode, &dir->oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed, " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}
	dir->mode = entry->mode;
	oid_cp(&dir->oid, entry->oid);
	dir->d.chunk_size = entry->chunk_size;
	dir->d.oclass     = entry->oclass;
	DFS_OP_STAT_INCR(dfs, DOS_OPENDIR);
	return 0;
}

static void
set_daos_iod(bool create, daos_iod_t *iod, char *buf, size_t size)
{
	d_iov_set(&iod->iod_name, buf, strlen(buf));
	iod->iod_nr    = 1;
	iod->iod_size  = DAOS_REC_ANY;
	iod->iod_recxs = NULL;
	iod->iod_type  = DAOS_IOD_SINGLE;

	if (create)
		iod->iod_size = size;
}

static void
set_sb_params(bool for_update, daos_iod_t *iods, daos_key_t *dkey)
{
	d_iov_set(dkey, SB_DKEY, sizeof(SB_DKEY) - 1);
	set_daos_iod(for_update, &iods[MAGIC_IDX], MAGIC_NAME, sizeof(dfs_magic_t));
	set_daos_iod(for_update, &iods[SB_VER_IDX], SB_VER_NAME, sizeof(dfs_sb_ver_t));
	set_daos_iod(for_update, &iods[LAYOUT_VER_IDX], LAYOUT_VER_NAME, sizeof(dfs_layout_ver_t));
	set_daos_iod(for_update, &iods[CS_IDX], CS_NAME, sizeof(daos_size_t));
	set_daos_iod(for_update, &iods[OC_IDX], OC_NAME, sizeof(daos_oclass_id_t));
	set_daos_iod(for_update, &iods[FILE_OC_IDX], FILE_OC_NAME, sizeof(daos_oclass_id_t));
	set_daos_iod(for_update, &iods[DIR_OC_IDX], DIR_OC_NAME, sizeof(daos_oclass_id_t));
	set_daos_iod(for_update, &iods[CONT_MODE_IDX], CONT_MODE_NAME, sizeof(uint32_t));
	set_daos_iod(for_update, &iods[CONT_HINT_IDX], CONT_HINT_NAME, DAOS_CONT_HINT_MAX_LEN);
}

int
open_sb(daos_handle_t coh, bool create, bool punch, int omode, daos_obj_id_t super_oid,
	dfs_attr_t *attr, daos_handle_t *oh, dfs_layout_ver_t *ver)
{
	d_sg_list_t      sgls[SB_AKEYS];
	d_iov_t          sg_iovs[SB_AKEYS];
	daos_iod_t       iods[SB_AKEYS];
	daos_key_t       dkey;
	dfs_magic_t      magic;
	dfs_sb_ver_t     sb_ver;
	dfs_layout_ver_t layout_ver;
	daos_size_t      chunk_size  = 0;
	daos_oclass_id_t oclass      = OC_UNKNOWN;
	daos_oclass_id_t dir_oclass  = OC_UNKNOWN;
	daos_oclass_id_t file_oclass = OC_UNKNOWN;
	uint32_t         mode;
	char             hints[DAOS_CONT_HINT_MAX_LEN];
	int              i, rc;

	D_ASSERT(attr);

	/** Open SB object */
	rc = daos_obj_open(coh, super_oid, omode, oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed, " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	d_iov_set(&sg_iovs[MAGIC_IDX], &magic, sizeof(dfs_magic_t));
	d_iov_set(&sg_iovs[SB_VER_IDX], &sb_ver, sizeof(dfs_sb_ver_t));
	d_iov_set(&sg_iovs[LAYOUT_VER_IDX], &layout_ver, sizeof(dfs_layout_ver_t));
	d_iov_set(&sg_iovs[CS_IDX], &chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[OC_IDX], &oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[FILE_OC_IDX], &file_oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[DIR_OC_IDX], &dir_oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[CONT_MODE_IDX], &mode, sizeof(uint32_t));

	for (i = 0; i < SB_AKEYS; i++) {
		sgls[i].sg_nr     = 1;
		sgls[i].sg_nr_out = 0;
		sgls[i].sg_iovs   = &sg_iovs[i];
	}

	set_sb_params(create, iods, &dkey);

	if (punch) {
		rc = daos_obj_punch_dkeys(*oh, DAOS_TX_NONE, 0, 1, &dkey, NULL);
		if (rc) {
			D_ERROR("SB punch failed: " DF_RC "\n", DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}
	}

	/** create the SB and exit */
	if (create) {
		int    num_iods = SB_AKEYS;
		size_t hint_len = strlen(attr->da_hints);

		/** adjust the IOD for the hints string to the actual size */
		if (hint_len) {
			set_daos_iod(true, &iods[CONT_HINT_IDX], CONT_HINT_NAME, hint_len + 1);
			d_iov_set(&sg_iovs[CONT_HINT_IDX], attr->da_hints, hint_len + 1);
		} else {
			num_iods--;
		}

		magic      = DFS_SB_MAGIC;
		sb_ver     = DFS_SB_VERSION;
		layout_ver = DFS_LAYOUT_VERSION;

		if (attr->da_chunk_size != 0)
			chunk_size = attr->da_chunk_size;
		else
			chunk_size = DFS_DEFAULT_CHUNK_SIZE;

		oclass      = attr->da_oclass_id;
		dir_oclass  = attr->da_dir_oclass_id;
		file_oclass = attr->da_file_oclass_id;
		mode        = attr->da_mode;

		rc = daos_obj_update(*oh, DAOS_TX_NONE, DAOS_COND_DKEY_INSERT, &dkey, num_iods,
				     iods, sgls, NULL);
		if (rc) {
			D_ERROR("Failed to create DFS superblock " DF_RC "\n", DP_RC(rc));
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		return 0;
	}

	/** set hints value to max */
	d_iov_set(&sg_iovs[CONT_HINT_IDX], &hints, DAOS_CONT_HINT_MAX_LEN);
	set_daos_iod(false, &iods[CONT_HINT_IDX], CONT_HINT_NAME, DAOS_CONT_HINT_MAX_LEN);

	/* Fetch the values and verify SB */
	rc = daos_obj_fetch(*oh, DAOS_TX_NONE, 0, &dkey, SB_AKEYS, iods, sgls, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch SB info, " DF_RC "\n", DP_RC(rc));
		D_GOTO(err, rc = daos_der2errno(rc));
	}

	/** check if SB info exists */
	if (iods[MAGIC_IDX].iod_size == 0) {
		rc = ENOENT;
		D_ERROR("SB does not exist: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err, rc);
	}

	if (magic != DFS_SB_MAGIC) {
		rc = EINVAL;
		D_ERROR("SB MAGIC verification failed: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err, rc);
	}

	/** check version compatibility */
	if (iods[SB_VER_IDX].iod_size != sizeof(sb_ver) || sb_ver > DFS_SB_VERSION) {
		rc = EINVAL;
		D_ERROR("Incompatible SB version: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err, rc);
	}

	if (iods[LAYOUT_VER_IDX].iod_size != sizeof(layout_ver) ||
	    layout_ver != DFS_LAYOUT_VERSION) {
		rc = EINVAL;
		D_ERROR("Incompatible DFS Layout version %d: %d (%s)\n", layout_ver, rc,
			strerror(rc));
		D_GOTO(err, rc);
	}

	D_DEBUG(DB_ALL, "DFS Container Layout version: %d\n", layout_ver);
	D_DEBUG(DB_ALL, "DFS Library Layout version: %d\n", DFS_LAYOUT_VERSION);

	*ver                    = layout_ver;
	attr->da_chunk_size     = (chunk_size) ? chunk_size : DFS_DEFAULT_CHUNK_SIZE;
	attr->da_oclass_id      = oclass;
	attr->da_dir_oclass_id  = dir_oclass;
	attr->da_file_oclass_id = file_oclass;
	attr->da_mode           = mode;
	if (iods[CONT_HINT_IDX].iod_size != 0)
		strcpy(attr->da_hints, hints);

	return 0;
err:
	daos_obj_close(*oh, NULL);
	return rc;
}

int
dfs_get_sb_layout(daos_key_t *dkey, daos_iod_t *iods[], int *akey_count, int *dfs_entry_key_size,
		  int *dfs_entry_size)
{
	struct dfs_entry entry;

	if (dkey == NULL || akey_count == NULL)
		return EINVAL;

	D_ALLOC_ARRAY(*iods, SB_AKEYS);
	if (*iods == NULL)
		return ENOMEM;

	*akey_count         = SB_AKEYS;
	*dfs_entry_key_size = sizeof(INODE_AKEY_NAME) - 1;
	/** Can't just use sizeof(struct dfs_entry) because it's not accurate */
	*dfs_entry_size =
	    D_ALIGNUP(sizeof(entry.mode) + sizeof(entry.oid) + sizeof(entry.mtime) +
			  sizeof(entry.ctime) + sizeof(entry.chunk_size) + sizeof(entry.oclass) +
			  sizeof(entry.mtime_nano) + sizeof(entry.ctime_nano) + sizeof(entry.uid) +
			  sizeof(entry.gid) + sizeof(entry.value_len) + sizeof(entry.obj_hlc),
		      32);

	set_sb_params(true, *iods, dkey);

	return 0;
}

int
dfs_suggest_oclass(dfs_t *dfs, const char *hint, daos_oclass_id_t *cid)
{
	daos_oclass_hints_t obj_hint = 0;
	char               *local;
	uint32_t            rf;
	enum daos_otype_t   type;
	int                 rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (hint == NULL)
		return EINVAL;
	if (strnlen(hint, DAOS_CONT_HINT_MAX_LEN) > DAOS_CONT_HINT_MAX_LEN + 1)
		return EINVAL;

	/** get the Redundancy Factor */
	rc = dc_cont_hdl2redunfac(dfs->coh, &rf);
	if (rc) {
		D_ERROR("dc_cont_hdl2redunfac() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	D_STRNDUP(local, hint, DAOS_CONT_HINT_MAX_LEN);
	if (!local)
		return ENOMEM;

	rc = decode_one_hint(local, rf, &obj_hint, &type);
	if (rc)
		D_GOTO(out, rc);

	rc = daos_obj_get_oclass(dfs->coh, type, obj_hint, 0, cid);
	if (rc) {
		D_ERROR("daos_obj_get_oclass() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}
out:
	D_FREE(local);
	return rc;
}

/**
 * Copy extended attributes from src dentry to HLM object.
 * The src uses name as dkey, but dst uses the OID directly as binary dkey.
 */
int
hlm_copy_xattr(daos_handle_t src_oh, const char *src_name, daos_handle_t hlm_oh,
	       daos_obj_id_t *dst_oid, daos_handle_t th)
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

	/* Set dkey for src entry name */
	d_iov_set(&src_dkey, (void *)src_name, strlen(src_name));

	/* Set dkey for dst as binary OID */
	d_iov_set(&dst_dkey, dst_oid, sizeof(daos_obj_id_t));

	/* Set IOD descriptor for fetching every xattr */
	iod.iod_nr    = 1;
	iod.iod_recxs = NULL;
	iod.iod_type  = DAOS_IOD_SINGLE;
	iod.iod_size  = DFS_MAX_XATTR_LEN;

	/* Set sgl for fetch - use a preallocated buf to avoid a roundtrip */
	D_ALLOC(val_buf, DFS_MAX_XATTR_LEN);
	if (val_buf == NULL)
		return ENOMEM;
	fsgl.sg_nr     = 1;
	fsgl.sg_nr_out = 0;
	fsgl.sg_iovs   = &fiov;

	/* Set sgl for akey_list */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_XDESC_BUF);
	sgl.sg_iovs = &iov;

	/* Iterate over every akey to look for xattrs */
	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t number = ENUM_DESC_NR;
		uint32_t i;
		char    *ptr;

		memset(enum_buf, 0, ENUM_XDESC_BUF);
		rc = daos_obj_list_akey(src_oh, th, &src_dkey, &number, kds, &sgl, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_obj_list_akey() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		if (number == 0)
			continue;

		/*
		 * For every entry enumerated, check if it's an xattr, and
		 * insert it in the HLM entry.
		 */
		for (ptr = enum_buf, i = 0; i < number; i++) {
			/* If not an xattr, go to next entry */
			if (strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			/* Set akey as the xattr name */
			d_iov_set(&iod.iod_name, ptr, kds[i].kd_key_len);
			d_iov_set(&fiov, val_buf, DFS_MAX_XATTR_LEN);

			/* Fetch the xattr value from the src */
			rc = daos_obj_fetch(src_oh, th, 0, &src_dkey, 1, &iod, &fsgl, NULL, NULL);
			if (rc) {
				D_ERROR("daos_obj_fetch() failed (%d)\n", rc);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			d_iov_set(&fiov, val_buf, iod.iod_size);

			/* Add it to the HLM destination */
			rc = daos_obj_update(hlm_oh, th, 0, &dst_dkey, 1, &iod, &fsgl, NULL);
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

/**
 * Copy the dentry and extended attributes to the HLM object.
 * The dkey in HLM is the OID of the file object.
 * Updates entry with ctime, ctime_nano, and ref_cnt that are written to HLM.
 */
int
hlm_copy_entry(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh, const char *name,
	       struct dfs_entry *entry)
{
	daos_key_t      dkey;
	d_sg_list_t     sgl;
	d_iov_t         sg_iovs[HLM_INODE_AKEYS];
	daos_iod_t      iod;
	daos_recx_t     recx;
	unsigned int    i;
	struct timespec now;
	int             rc;

	/* Set ctime to current time */
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		return errno;

	/* Update entry with values being written to HLM */
	entry->ctime      = now.tv_sec;
	entry->ctime_nano = now.tv_nsec;
	entry->ref_cnt    = 2;
	entry->mode |= MODE_HARDLINK_BIT;

	/* Set dkey as the file's OID in HLM object */
	d_iov_set(&dkey, &entry->oid, sizeof(daos_obj_id_t));

	/* Set up iod for the inode akey */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = 1;
	recx.rx_idx   = 0;
	recx.rx_nr    = END_HLM_IDX;
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	/* Populate sg_iovs with entry fields */
	i = 0;
	d_iov_set(&sg_iovs[i++], &entry->mode, sizeof(mode_t));
	d_iov_set(&sg_iovs[i++], &entry->oid, sizeof(daos_obj_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->chunk_size, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&sg_iovs[i++], &entry->mtime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->ctime_nano, sizeof(uint64_t));
	d_iov_set(&sg_iovs[i++], &entry->uid, sizeof(uid_t));
	d_iov_set(&sg_iovs[i++], &entry->gid, sizeof(gid_t));
	d_iov_set(&sg_iovs[i++], &entry->value_len, sizeof(daos_size_t));
	d_iov_set(&sg_iovs[i++], &entry->obj_hlc, sizeof(uint64_t));
	/* ref_cnt is 2 because we have the original entry and the new hardlink */
	d_iov_set(&sg_iovs[i++], &entry->ref_cnt, sizeof(uint64_t));

	sgl.sg_nr     = i;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = sg_iovs;

	/* Insert dentry into HLM object */
	rc = daos_obj_update(dfs->hlm_oh, th, DAOS_COND_DKEY_INSERT, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to insert entry into HLM " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	/* Copy extended attributes from parent dentry to HLM */
	rc = hlm_copy_xattr(parent_oh, name, dfs->hlm_oh, &entry->oid, th);
	if (rc) {
		D_ERROR("Failed to copy xattrs to HLM (%d)\n", rc);
		return rc;
	}

	return 0;
}

/**
 * Remove extended attributes from a dentry (not from HLM).
 */
int
remove_xattrs_from_entry(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh, const char *name)
{
	daos_key_t      dkey;
	daos_key_t      akey;
	daos_anchor_t   anchor = {0};
	d_sg_list_t     sgl;
	d_iov_t         iov;
	char            enum_buf[ENUM_XDESC_BUF];
	daos_key_desc_t kds[ENUM_DESC_NR];
	int             rc = 0;

	d_iov_set(&dkey, (void *)name, strlen(name));

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_XDESC_BUF);
	sgl.sg_iovs = &iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t number = ENUM_DESC_NR;
		uint32_t i;
		char    *ptr;

		memset(enum_buf, 0, ENUM_XDESC_BUF);
		rc = daos_obj_list_akey(parent_oh, th, &dkey, &number, kds, &sgl, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_obj_list_akey() failed (%d)\n", rc);
			return daos_der2errno(rc);
		}

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			/* Only remove xattrs (prefixed with "x:") */
			if (strncmp("x:", ptr, 2) != 0) {
				ptr += kds[i].kd_key_len;
				continue;
			}

			d_iov_set(&akey, ptr, kds[i].kd_key_len);
			rc = daos_obj_punch_akeys(parent_oh, th,
						  DAOS_COND_DKEY_UPDATE | DAOS_COND_PUNCH, &dkey, 1,
						  &akey, NULL);
			if (rc) {
				D_ERROR("Failed to punch xattr akey (%d)\n", rc);
				return daos_der2errno(rc);
			}
			ptr += kds[i].kd_key_len;
		}
	}

	return 0;
}

/**
 * Update the mode of an entry to set the hardlink bit.
 */
int
set_hardlink_bit(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh, dfs_obj_t *obj, mode_t mode)
{
	daos_key_t  dkey;
	d_sg_list_t sgl;
	d_iov_t     sg_iov;
	daos_iod_t  iod;
	daos_recx_t recx;
	mode_t      new_mode;
	int         rc;

	new_mode = mode | MODE_HARDLINK_BIT;

	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = 1;
	recx.rx_idx   = MODE_IDX;
	recx.rx_nr    = sizeof(mode_t);
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	d_iov_set(&sg_iov, &new_mode, sizeof(mode_t));
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iov;

	rc = daos_obj_update(parent_oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to set hardlink bit " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}
	obj->mode = new_mode;

	return 0;
}

/**
 * Update the mode of an entry to clear the hardlink bit.
 */
int
clear_hardlink_bit(dfs_t *dfs, daos_handle_t th, daos_handle_t parent_oh, dfs_obj_t *obj,
		   mode_t mode)
{
	daos_key_t  dkey;
	d_sg_list_t sgl;
	d_iov_t     sg_iov;
	daos_iod_t  iod;
	daos_recx_t recx;
	mode_t      new_mode;
	int         rc;

	new_mode = mode & ~MODE_HARDLINK_BIT;

	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = 1;
	recx.rx_idx   = MODE_IDX;
	recx.rx_nr    = sizeof(mode_t);
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	d_iov_set(&sg_iov, &new_mode, sizeof(mode_t));
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iov;

	rc = daos_obj_update(parent_oh, th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to clear hardlink bit " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}
	obj->mode = new_mode;

	return 0;
}
