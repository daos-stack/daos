/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS directory ops */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/common.h>
#include <daos/object.h>

#include "dfs_internal.h"

int
dfs_mkdir(dfs_t *dfs, dfs_obj_t *parent, const char *name, mode_t mode, daos_oclass_id_t cid)
{
	dfs_obj_t        new_dir;
	daos_handle_t    th    = dfs->th;
	struct dfs_entry entry = {0};
	size_t           len;
	struct timespec  now;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	strncpy(new_dir.name, name, len + 1);

	rc = create_dir(dfs, parent, cid, &new_dir);
	if (rc)
		return rc;

	entry.oid  = new_dir.oid;
	entry.mode = S_IFDIR | mode;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		return errno;
	entry.mtime = entry.ctime = now.tv_sec;
	entry.mtime_nano = entry.ctime_nano = now.tv_nsec;
	entry.chunk_size                    = parent->d.chunk_size;
	entry.oclass                        = parent->d.oclass;
	entry.uid                           = geteuid();
	entry.gid                           = getegid();

	rc = insert_entry(dfs->layout_v, parent->oh, th, name, len, DAOS_COND_DKEY_INSERT, &entry);
	if (rc != 0) {
		daos_obj_close(new_dir.oh, NULL);
		return rc;
	}

	rc = daos_obj_close(new_dir.oh, NULL);
	if (rc != 0)
		return daos_der2errno(rc);

	return rc;
}

static int
remove_dir_contents(dfs_t *dfs, daos_handle_t th, struct dfs_entry entry)
{
	daos_handle_t   oh;
	daos_key_desc_t kds[ENUM_DESC_NR];
	daos_anchor_t   anchor = {0};
	d_iov_t         iov;
	char            enum_buf[ENUM_DESC_BUF] = {0};
	d_sg_list_t     sgl;
	int             rc;

	D_ASSERT(S_ISDIR(entry.mode));

	rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, ENUM_DESC_BUF);
	sgl.sg_iovs = &iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t number = ENUM_DESC_NR;
		uint32_t i;
		char    *ptr;

		rc = daos_obj_list_dkey(oh, th, &number, kds, &sgl, &anchor, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		if (number == 0)
			continue;

		for (ptr = enum_buf, i = 0; i < number; i++) {
			struct dfs_entry child_entry = {0};
			bool             exists;

			ptr += kds[i].kd_key_len;

			rc = fetch_entry(dfs->layout_v, oh, th, ptr, kds[i].kd_key_len, false,
					 &exists, &child_entry, 0, NULL, NULL, NULL);
			if (rc)
				D_GOTO(out, rc);

			if (!exists)
				continue;

			if (S_ISDIR(child_entry.mode)) {
				rc = remove_dir_contents(dfs, th, child_entry);
				if (rc)
					D_GOTO(out, rc);
			}

			rc = remove_entry(dfs, th, oh, ptr, kds[i].kd_key_len, child_entry);
			if (rc)
				D_GOTO(out, rc);
		}
	}

out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_remove(dfs_t *dfs, dfs_obj_t *parent, const char *name, bool force, daos_obj_id_t *oid)
{
	struct dfs_entry entry = {0};
	daos_handle_t    th    = dfs->th;
	bool             exists;
	size_t           len;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
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
	/** Even with cond punch, need to fetch the entry to check the type */
	rc = fetch_entry(dfs->layout_v, parent->oh, th, name, len, false, &exists, &entry, 0, NULL,
			 NULL, NULL);
	if (rc)
		D_GOTO(out, rc);

	if (!exists)
		D_GOTO(out, rc = ENOENT);

	if (S_ISDIR(entry.mode)) {
		uint32_t      nr = 0;
		daos_handle_t oh;

		/** check if dir is empty */
		rc = daos_obj_open(dfs->coh, entry.oid, DAOS_OO_RW, &oh, NULL);
		if (rc) {
			D_ERROR("daos_obj_open() Failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		rc = get_num_entries(oh, th, &nr, true);
		if (rc) {
			daos_obj_close(oh, NULL);
			D_GOTO(out, rc);
		}

		rc = daos_obj_close(oh, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		if (!force && nr != 0)
			D_GOTO(out, rc = ENOTEMPTY);

		if (force && nr != 0) {
			rc = remove_dir_contents(dfs, th, entry);
			if (rc)
				D_GOTO(out, rc);
		}
	}

	rc = remove_entry(dfs, th, parent->oh, name, len, entry);
	if (rc)
		D_GOTO(out, rc);

	if (dfs->use_dtx) {
		rc = daos_tx_commit(th, NULL);
		if (rc) {
			if (rc != -DER_TX_RESTART)
				D_ERROR("daos_tx_commit() failed (%d)\n", rc);
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

	if (oid)
		oid_cp(oid, entry.oid);

out:
	rc = check_tx(th, rc);
	if (rc == ERESTART)
		goto restart;
	return rc;
}

int
dfs_obj_set_oclass(dfs_t *dfs, dfs_obj_t *obj, int flags, daos_oclass_id_t cid)
{
	daos_handle_t oh;
	d_sg_list_t   sgl;
	d_iov_t       sg_iov;
	daos_iod_t    iod;
	daos_recx_t   recx;
	daos_key_t    dkey;
	int           rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL)
		return EINVAL;
	if (!S_ISDIR(obj->mode))
		return ENOTSUP;
	/** 0 is default, allow setting it */
	if (cid != 0 && !daos_oclass_is_valid(cid))
		return EINVAL;
	if (cid == 0)
		cid = dfs->attr.da_dir_oclass_id;

	/** Open parent object and fetch entry of obj from it */
	rc = daos_obj_open(dfs->coh, obj->parent_oid, DAOS_OO_RW, &oh, NULL);
	if (rc)
		return daos_der2errno(rc);

	/** set dkey as the entry name */
	d_iov_set(&dkey, (void *)obj->name, strlen(obj->name));

	/** set akey as the inode name */
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = 1;
	iod.iod_size  = 1;
	recx.rx_idx   = OCLASS_IDX;
	recx.rx_nr    = sizeof(daos_oclass_id_t);
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;

	/** set sgl for update */
	d_iov_set(&sg_iov, &cid, sizeof(daos_oclass_id_t));
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iov;

	rc = daos_obj_update(oh, dfs->th, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		D_ERROR("Failed to update object class: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	/** if this is root obj, we need to update the cached handle oclass */
	if (daos_oid_cmp(obj->oid, dfs->root.oid) == 0)
		dfs->root.d.oclass = cid;

out:
	daos_obj_close(oh, NULL);
	return rc;
}

int
dfs_obj_anchor_split(dfs_obj_t *obj, uint32_t *nr, daos_anchor_t *anchors)
{
	int rc;

	if (obj == NULL || nr == NULL || !S_ISDIR(obj->mode))
		return EINVAL;

	rc = daos_obj_anchor_split(obj->oh, nr, anchors);
	return daos_der2errno(rc);
}

int
dfs_obj_anchor_set(dfs_obj_t *obj, uint32_t index, daos_anchor_t *anchor)
{
	int rc;

	if (obj == NULL || !S_ISDIR(obj->mode))
		return EINVAL;

	rc = daos_obj_anchor_set(obj->oh, index, anchor);
	return daos_der2errno(rc);
}

int
dfs_dir_anchor_set(dfs_obj_t *obj, const char name[], daos_anchor_t *anchor)
{
	daos_key_t dkey;
	size_t     len;
	int        rc;

	if (obj == NULL || !S_ISDIR(obj->mode))
		return EINVAL;
	rc = check_name(name, &len);
	if (rc)
		return rc;

	d_iov_set(&dkey, (void *)name, len);
	rc = daos_obj_key2anchor(obj->oh, obj->dfs->th, &dkey, NULL, anchor, NULL);
	return daos_der2errno(rc);
}
