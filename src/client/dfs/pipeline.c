/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS pipeline operations */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/common.h>
#include <daos.h>
#include <daos_fs.h>
#include <daos_pipeline.h>

#include "dfs_internal.h"

struct dfs_pipeline {
	daos_pipeline_t    pipeline;
	dfs_predicate_t    pred;

	mode_t             constant1;
	mode_t             constant2;

	d_iov_t            dkey_iov;
	d_iov_t            const1_iov;
	d_iov_t            const2_iov;
	d_iov_t            const3_iov;

	daos_filter_part_t dkey_ft;
	daos_filter_part_t akey1_ft;
	daos_filter_part_t akey2_ft;
	daos_filter_part_t const0_ft;
	daos_filter_part_t const1_ft;
	daos_filter_part_t const2_ft;
	daos_filter_part_t const3_ft;
	daos_filter_part_t like_ft;
	daos_filter_part_t ba_ft;
	daos_filter_part_t eq_ft;
	daos_filter_part_t gt_ft;
	daos_filter_part_t and_ft;
	daos_filter_part_t or_ft;

	daos_filter_t      pipef;
};

#define DKEY_F   "DAOS_FILTER_DKEY"
#define AKEY_F   "DAOS_FILTER_AKEY"
#define CONST_F  "DAOS_FILTER_CONST"
#define BINARY_F "DAOS_FILTER_TYPE_BINARY"
#define INT8_F   "DAOS_FILTER_TYPE_UINTEGER8"
#define INT4_F   "DAOS_FILTER_TYPE_UINTEGER4"
#define LIKE_F   "DAOS_FILTER_FUNC_LIKE"
#define GT_F     "DAOS_FILTER_FUNC_GT"
#define EQ_F     "DAOS_FILTER_FUNC_EQ"
#define BA_F     "DAOS_FILTER_FUNC_BITAND"
#define AND_F    "DAOS_FILTER_FUNC_AND"
#define OR_F     "DAOS_FILTER_FUNC_OR"
#define COND_F   "DAOS_FILTER_CONDITION"

int
dfs_pipeline_create(dfs_t *dfs, dfs_predicate_t pred, uint64_t flags, dfs_pipeline_t **_dpipe)
{
	daos_size_t     bin_flen   = sizeof(BINARY_F) - 1;
	daos_size_t     dkey_flen  = sizeof(DKEY_F) - 1;
	daos_size_t     akey_flen  = sizeof(AKEY_F) - 1;
	daos_size_t     const_flen = sizeof(CONST_F) - 1;
	daos_size_t     int8_flen  = sizeof(INT8_F) - 1;
	daos_size_t     int4_flen  = sizeof(INT4_F) - 1;
	daos_size_t     like_flen  = sizeof(LIKE_F) - 1;
	daos_size_t     gt_flen    = sizeof(GT_F) - 1;
	daos_size_t     eq_flen    = sizeof(EQ_F) - 1;
	daos_size_t     ba_flen    = sizeof(BA_F) - 1;
	daos_size_t     and_flen   = sizeof(AND_F) - 1;
	daos_size_t     or_flen    = sizeof(OR_F) - 1;
	daos_size_t     cond_flen  = sizeof(COND_F) - 1;
	dfs_pipeline_t *dpipe;
	int             rc;

	D_ALLOC_PTR(dpipe);
	if (dpipe == NULL)
		return ENOMEM;

	/** copy the user predicate conditions */
	memcpy(&dpipe->pred, &pred, sizeof(dfs_predicate_t));

	daos_pipeline_init(&dpipe->pipeline);

	/** build condition for entry name */
	if (flags & DFS_FILTER_NAME) {
		daos_size_t name_len;

		name_len = strnlen(dpipe->pred.dp_name, DFS_MAX_NAME);

		d_iov_set(&dpipe->dkey_ft.part_type, DKEY_F, dkey_flen);
		d_iov_set(&dpipe->dkey_ft.data_type, BINARY_F, bin_flen);
		dpipe->dkey_ft.data_len = DFS_MAX_NAME;

		d_iov_set(&dpipe->const0_ft.part_type, CONST_F, const_flen);
		d_iov_set(&dpipe->const0_ft.data_type, BINARY_F, bin_flen);
		dpipe->const0_ft.num_constants = 1;
		dpipe->const0_ft.constant      = &dpipe->dkey_iov;
		d_iov_set(dpipe->const0_ft.constant, dpipe->pred.dp_name, name_len);

		d_iov_set(&dpipe->like_ft.part_type, LIKE_F, like_flen);
		dpipe->like_ft.num_operands = 2;
	}

	/** build condition for newer than ctime */
	if (flags & DFS_FILTER_NEWER) {
		d_iov_set(&dpipe->akey2_ft.part_type, AKEY_F, akey_flen);
		d_iov_set(&dpipe->akey2_ft.data_type, INT8_F, int8_flen);
		d_iov_set(&dpipe->akey2_ft.akey, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
		dpipe->akey2_ft.data_offset = CTIME_IDX;
		dpipe->akey2_ft.data_len    = sizeof(time_t);

		d_iov_set(&dpipe->const3_ft.part_type, CONST_F, const_flen);
		d_iov_set(&dpipe->const3_ft.data_type, INT8_F, int8_flen);
		dpipe->const3_ft.num_constants = 1;
		dpipe->const3_ft.constant      = &dpipe->const3_iov;
		d_iov_set(dpipe->const3_ft.constant, &dpipe->pred.dp_newer, sizeof(time_t));

		d_iov_set(&dpipe->gt_ft.part_type, GT_F, gt_flen);
		dpipe->gt_ft.num_operands = 2;
	}

	/** If filter on dirs is not requested, return all dirs so they can be traversed */
	if (!(flags & DFS_FILTER_INCLUDE_DIRS)) {
		d_iov_set(&dpipe->akey1_ft.part_type, AKEY_F, akey_flen);
		d_iov_set(&dpipe->akey1_ft.data_type, INT4_F, int4_flen);
		d_iov_set(&dpipe->akey1_ft.akey, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
		dpipe->akey1_ft.data_offset = MODE_IDX;
		dpipe->akey1_ft.data_len    = sizeof(mode_t);

		dpipe->constant1 = S_IFMT;
		d_iov_set(&dpipe->const1_ft.part_type, CONST_F, const_flen);
		d_iov_set(&dpipe->const1_ft.data_type, INT4_F, int4_flen);
		dpipe->const1_ft.num_constants = 1;
		dpipe->const1_ft.constant      = &dpipe->const1_iov;
		d_iov_set(dpipe->const1_ft.constant, &dpipe->constant1, sizeof(mode_t));

		dpipe->constant2 = S_IFDIR;
		d_iov_set(&dpipe->const2_ft.part_type, CONST_F, const_flen);
		d_iov_set(&dpipe->const2_ft.data_type, INT4_F, int4_flen);
		dpipe->const2_ft.num_constants = 1;
		dpipe->const2_ft.constant      = &dpipe->const2_iov;
		d_iov_set(dpipe->const2_ft.constant, &dpipe->constant2, sizeof(mode_t));

		d_iov_set(&dpipe->ba_ft.part_type, BA_F, ba_flen);
		dpipe->ba_ft.num_operands = 2;

		d_iov_set(&dpipe->eq_ft.part_type, EQ_F, eq_flen);
		dpipe->eq_ft.num_operands = 2;
	}

	/** build final condition: IS_DIR || (entry name match && newer match) */

	d_iov_set(&dpipe->and_ft.part_type, AND_F, and_flen);
	dpipe->and_ft.num_operands = 2;

	d_iov_set(&dpipe->or_ft.part_type, OR_F, or_flen);
	dpipe->or_ft.num_operands = 2;

	/** initialize and add all the parts to the pipeline */
	daos_filter_init(&dpipe->pipef);
	d_iov_set(&dpipe->pipef.filter_type, COND_F, cond_flen);

	if (!(flags & DFS_FILTER_INCLUDE_DIRS)) {
		rc = daos_filter_add(&dpipe->pipef, &dpipe->or_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));

		rc = daos_filter_add(&dpipe->pipef, &dpipe->eq_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->ba_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->akey1_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->const1_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->const2_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
	}

	if (flags & DFS_FILTER_NEWER && flags & DFS_FILTER_NAME) {
		rc = daos_filter_add(&dpipe->pipef, &dpipe->and_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
	}

	if (flags & DFS_FILTER_NAME) {
		rc = daos_filter_add(&dpipe->pipef, &dpipe->like_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->dkey_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->const0_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
	}

	if (flags & DFS_FILTER_NEWER) {
		rc = daos_filter_add(&dpipe->pipef, &dpipe->gt_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->akey2_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
		rc = daos_filter_add(&dpipe->pipef, &dpipe->const3_ft);
		if (rc)
			D_GOTO(err, rc = daos_der2errno(rc));
	}

	rc = daos_pipeline_add(&dpipe->pipeline, &dpipe->pipef);
	if (rc)
		D_GOTO(err, rc = daos_der2errno(rc));

	*_dpipe = dpipe;
	return 0;
err:
	printf("failed to create pipeline. rc = %d\n", rc);
	D_FREE(dpipe);
	return rc;
}

int
dfs_pipeline_destroy(dfs_pipeline_t *dpipe)
{
	if (dpipe->pipeline.num_filters)
		D_FREE(dpipe->pipeline.filters);
	D_FREE(dpipe);
	return 0;
}

int
dfs_readdir_with_filter(dfs_t *dfs, dfs_obj_t *obj, dfs_pipeline_t *dpipe, daos_anchor_t *anchor,
			uint32_t *nr, struct dirent *dirs, daos_obj_id_t *oids, daos_size_t *csize,
			uint64_t *nr_scanned)
{
	daos_iod_t       iod;
	daos_key_desc_t *kds;
	d_sg_list_t      sgl_keys, sgl_recs;
	d_iov_t          iov_keys, iov_recs;
	char            *buf_keys = NULL, *buf_recs = NULL;
	daos_recx_t      recxs[4];
	uint32_t         nr_iods, nr_kds, key_nr, i;
	daos_size_t      record_len;
	int              rc = 0;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISDIR(obj->mode))
		return ENOTDIR;
	if (*nr == 0)
		return 0;
	if (dpipe == NULL || dirs == NULL || anchor == NULL)
		return EINVAL;

	/* IOD to retrieve the mode_t and the ctime */
	iod.iod_nr      = 2;
	iod.iod_size    = 1;
	recxs[0].rx_idx = MODE_IDX;
	recxs[0].rx_nr  = sizeof(mode_t);
	recxs[1].rx_idx = CTIME_IDX;
	recxs[1].rx_nr  = sizeof(time_t);
	iod.iod_recxs   = recxs;
	iod.iod_type    = DAOS_IOD_ARRAY;
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	record_len = recxs[0].rx_nr + recxs[1].rx_nr;

	if (oids) {
		recxs[iod.iod_nr].rx_idx = OID_IDX;
		recxs[iod.iod_nr].rx_nr  = sizeof(daos_obj_id_t);
		record_len += recxs[iod.iod_nr].rx_nr;
		iod.iod_nr++;
	}
	if (csize) {
		recxs[iod.iod_nr].rx_idx = CSIZE_IDX;
		recxs[iod.iod_nr].rx_nr  = sizeof(daos_size_t);
		record_len += recxs[iod.iod_nr].rx_nr;
		iod.iod_nr++;
	}

	nr_kds  = *nr;
	nr_iods = 1;

	D_ALLOC_ARRAY(kds, nr_kds);
	if (kds == NULL)
		return ENOMEM;

	/** alloc buffer to store dkeys enumerated */
	sgl_keys.sg_nr     = 1;
	sgl_keys.sg_nr_out = 0;
	sgl_keys.sg_iovs   = &iov_keys;
	D_ALLOC_ARRAY(buf_keys, nr_kds * DFS_MAX_NAME);
	if (buf_keys == NULL)
		D_GOTO(out, rc = ENOMEM);
	d_iov_set(&iov_keys, buf_keys, nr_kds * DFS_MAX_NAME);

	/** alloc buffer to store records enumerated */
	sgl_recs.sg_nr     = 1;
	sgl_recs.sg_nr_out = 0;
	sgl_recs.sg_iovs   = &iov_recs;
	D_ALLOC_ARRAY(buf_recs, nr_kds * record_len);
	if (buf_recs == NULL)
		D_GOTO(out, rc = ENOMEM);
	d_iov_set(&iov_recs, buf_recs, nr_kds * record_len);

	key_nr      = 0;
	*nr_scanned = 0;
	while (!daos_anchor_is_eof(anchor)) {
		daos_pipeline_stats_t stats = {0};
		char                 *ptr1;

		memset(buf_keys, 0, *nr * DFS_MAX_NAME);

		rc = daos_pipeline_run(dfs->coh, obj->oh, &dpipe->pipeline, DAOS_TX_NONE, 0, NULL,
				       &nr_iods, &iod, anchor, &nr_kds, kds, &sgl_keys, &sgl_recs,
				       NULL, NULL, &stats, NULL);
		if (rc)
			D_GOTO(out, rc = daos_der2errno(rc));

		D_ASSERT(nr_iods == 1);
		ptr1 = buf_keys;

		for (i = 0; i < nr_kds; i++) {
			char  *ptr2;
			mode_t mode;
			char  *dkey = (char *)ptr1;

			/** set the dentry name */
			memcpy(dirs[key_nr].d_name, dkey, kds[i].kd_key_len);
			dirs[key_nr].d_name[kds[i].kd_key_len] = '\0';

			/** set the dentry type */
			ptr2 = &buf_recs[i * record_len];
			mode = *((mode_t *)ptr2);

			if (S_ISDIR(mode)) {
				dirs[key_nr].d_type = DT_DIR;
			} else if (S_ISREG(mode)) {
				dirs[key_nr].d_type = DT_REG;
			} else if (S_ISLNK(mode)) {
				dirs[key_nr].d_type = DT_LNK;
			} else {
				D_ERROR("Invalid DFS entry type found, possible data corruption\n");
				D_GOTO(out, rc = EINVAL);
			}

			/** set the OID for dentry if requested */
			if (oids) {
				ptr2 += sizeof(mode_t) + sizeof(time_t);
				oid_cp(&oids[key_nr], *((daos_obj_id_t *)ptr2));
			}

			/** set the chunk size for dentry if requested */
			if (csize) {
				if (oids)
					ptr2 += sizeof(daos_obj_id_t);
				else
					ptr2 += sizeof(mode_t) + sizeof(time_t);
				csize[key_nr] = *((daos_size_t *)ptr2);
			}

			key_nr++;
			ptr1 += kds[i].kd_key_len;
		}

		*nr_scanned += stats.nr_dkeys;
		nr_kds = *nr - key_nr;
		if (nr_kds == 0)
			break;
	}
	*nr = key_nr;

out:
	D_FREE(kds);
	D_FREE(buf_recs);
	D_FREE(buf_keys);
	return rc;
}
