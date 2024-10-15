/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/object/cli_coll.c
 *
 * For client side collecitve operation.
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/object.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos/task.h>
#include <daos_task.h>
#include <daos_types.h>
#include <daos_obj.h>
#include "obj_rpc.h"
#include "obj_internal.h"

static int
coll_rec_alloc(struct btr_instance *tins, d_iov_t *key_iov, d_iov_t *val_iov,
	       struct btr_record *rec, d_iov_t *val_out)
{
	struct daos_coll_target	*dct;
	struct coll_oper_args	*coa = val_iov->iov_buf;
	int			 rc = 0;

	D_ALLOC_PTR(dct);
	if (dct == NULL) {
		rc = -DER_NOMEM;
	} else {
		rec->rec_off = umem_ptr2off(&tins->ti_umm, dct);
		d_iov_set(val_out, dct, sizeof(*dct));
		coa->coa_dct_cap++;
	}

	return rc;
}

static int
coll_rec_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct daos_coll_target	*dct;

	dct = (struct daos_coll_target *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	rec->rec_off = UMOFF_NULL;

	daos_coll_shard_cleanup(dct->dct_shards, dct->dct_max_shard + 1);
	D_FREE(dct->dct_bitmap);
	D_FREE(dct->dct_tgt_ids);
	D_FREE(dct);

	return 0;
}

static int
coll_rec_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct daos_coll_target	*dct;

	dct = (struct daos_coll_target *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, dct, sizeof(*dct));

	return 0;
}

static int
coll_rec_update(struct btr_instance *tins, struct btr_record *rec,
		d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct daos_coll_target	*dct;

	dct = (struct daos_coll_target *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_out, dct, sizeof(*dct));

	return 0;
}

btr_ops_t dbtree_coll_ops = {
	.to_rec_alloc	= coll_rec_alloc,
	.to_rec_free	= coll_rec_free,
	.to_rec_fetch	= coll_rec_fetch,
	.to_rec_update	= coll_rec_update,
};

bool
obj_need_coll(struct dc_object *obj, uint32_t *start_shard, uint32_t *shard_nr,
	      uint32_t *grp_nr)
{
	bool	coll = false;

	obj_ptr2shards(obj, start_shard, shard_nr, grp_nr);

	/*
	 * We support object collective operation since release-2.6 (version 10).
	 * The conditions to trigger object collective operation are:
	 *
	 * 1. The shards count exceeds the threshold for collective operation
	 *    (20 by default). Collectively operation will distribute the RPC
	 *    load among more engines even if the total RPCs count may be not
	 *    decreased too much. Or
	 *
	 * 2. The shards count is twice (or even more) of the engines count.
	 *    Means that there are some shards reside on the same engine(s).
	 *    Collectively operation will save some RPCs.
	 */

	if (dc_obj_proto_version < 10 || obj_coll_thd == 0)
		return false;

	if (*shard_nr > obj_coll_thd)
		 return true;

	if (*shard_nr <= 4)
		return false;

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (*shard_nr >= (obj->cob_max_rank - obj->cob_min_rank + 1) * 2)
		coll = true;
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return coll;
}

int
obj_coll_oper_args_init(struct coll_oper_args *coa, struct dc_object *obj, bool for_modify)
{
	struct dc_pool		*pool = obj->cob_pool;
	struct umem_attr	 uma = { 0 };
	uint32_t		 pool_ranks;
	uint32_t		 obj_ranks;
	int			 rc = 0;

	D_ASSERT(pool != NULL);
	D_ASSERT(coa->coa_dcts == NULL);

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	pool_ranks = pool_map_rank_nr(pool->dp_map);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	/* The pool map may be refreshed after last collective operation on the object. */
	if (unlikely(obj->cob_rank_nr > pool_ranks)) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		D_GOTO(out, rc = -DER_STALE);
	}

	if (DAOS_FAIL_CHECK(DAOS_OBJ_COLL_SPARSE)) {
		coa->coa_sparse = 1;
	} else {
		/*
		 * The obj_ranks is estimated, the ranks in the range [cob_min_rank, cob_max_rank]
		 * may be not continuous, the real obj_ranks maybe smaller than the estimated one.
		 * It is no matter if the real ranks count is much smaller than the estimated one,
		 * that only affects current collecitve operation efficiency. The ranks count will
		 * be known after current collective operation.
		 */
		obj_ranks = obj->cob_max_rank - obj->cob_min_rank + 1;

		if (obj->cob_rank_nr > 0) {
			D_ASSERT(obj_ranks >= obj->cob_rank_nr);

			if (obj->cob_rank_nr * 100 / pool_ranks >= 35)
				coa->coa_sparse = 0;
			else
				coa->coa_sparse = 1;
		} else {
			if (obj_ranks * 100 / pool_ranks >= 45)
				coa->coa_sparse = 0;
			else
				coa->coa_sparse = 1;
		}

		if (coa->coa_sparse == 0)
			coa->coa_dct_cap = obj_ranks;
	}
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	if (coa->coa_sparse) {
		D_ALLOC_PTR(coa->coa_tree);
		if (coa->coa_tree == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		uma.uma_id = UMEM_CLASS_VMEM;
		rc = dbtree_create_inplace(DBTREE_CLASS_COLL, 0, COLL_BTREE_ORDER, &uma,
					   &coa->coa_tree->cst_tree_root,
					   &coa->coa_tree->cst_tree_hdl);
		if (rc != 0) {
			D_FREE(coa->coa_tree);
			goto out;
		}

		coa->coa_dct_nr = 0;
		coa->coa_dct_cap = 0;
	} else {
		D_ALLOC_ARRAY(coa->coa_dcts, coa->coa_dct_cap);
		if (coa->coa_dcts == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		/*
		 * Set coa_dct_nr as -1 to indicate that the coa_dcts array may be sparse until
		 * obj_coll_oper_args_collapse(). That is useful for obj_coll_oper_args_fini().
		 */
		coa->coa_dct_nr = -1;
	}

	coa->coa_max_dct_sz = 0;
	coa->coa_max_shard_nr = 0;
	coa->coa_max_bitmap_sz = 0;
	coa->coa_target_nr = 0;
	coa->coa_for_modify = for_modify ? 1 : 0;

out:
	return rc;
}

void
obj_coll_oper_args_fini(struct coll_oper_args *coa)
{
	if (coa->coa_sparse) {
		if (coa->coa_tree != NULL) {
			if (daos_handle_is_valid(coa->coa_tree->cst_tree_hdl))
				dbtree_destroy(coa->coa_tree->cst_tree_hdl, NULL);
			D_FREE(coa->coa_tree);
		}
	} else {
		daos_coll_target_cleanup(coa->coa_dcts,
					 coa->coa_dct_nr < 0 ? coa->coa_dct_cap : coa->coa_dct_nr);
		coa->coa_dcts = NULL;
	}
	coa->coa_dct_cap = 0;
	coa->coa_dct_nr = 0;
}

static int
obj_coll_tree_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *arg)
{
	struct coll_oper_args	*coa = arg;
	struct daos_coll_target	*dct = val->iov_buf;

	D_ASSERTF(coa->coa_dct_nr < coa->coa_dct_cap,
		  "Too short pre-allcoated dct_array: %u vs %u\n",
		  coa->coa_dct_nr, coa->coa_dct_cap);

	memcpy(&coa->coa_dcts[coa->coa_dct_nr++], dct, sizeof(*dct));

	/* The following members have been migrated into coa->coa_dcts. */
	dct->dct_bitmap = NULL;
	dct->dct_shards = NULL;
	dct->dct_tgt_ids = NULL;

	return 0;
}

static int
obj_coll_collapse_tree(struct coll_oper_args *coa, uint32_t *size)
{
	struct coll_sparse_targets	*tree = coa->coa_tree;
	int				 rc = 0;

	if (unlikely(coa->coa_dct_cap == 0))
		D_GOTO(out, rc = 1);

	D_ALLOC_ARRAY(coa->coa_dcts, coa->coa_dct_cap);
	if (coa->coa_dcts == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	coa->coa_sparse = 0;
	rc = dbtree_iterate(tree->cst_tree_hdl, DAOS_INTENT_DEFAULT, false, obj_coll_tree_cb, coa);
	if (rc == 0)
		D_ASSERTF(coa->coa_dct_nr == coa->coa_dct_cap,
			  "Something is wrong when prepare coll target array: %u vs %u\n",
			  coa->coa_dct_nr, coa->coa_dct_cap);

out:
	dbtree_destroy(tree->cst_tree_hdl, NULL);
	D_FREE(tree);

	return rc;
}

static int
obj_coll_collapse_array(struct coll_oper_args *coa, uint32_t *size)
{
	struct daos_coll_target	*dct;
	struct daos_coll_shard	*dcs;
	uint32_t		 dct_size;
	int			 i;
	int			 j;

	for (i = 0, *size = 0, coa->coa_dct_nr = 0; i < coa->coa_dct_cap; i++) {
		dct = &coa->coa_dcts[i];
		if (dct->dct_bitmap != NULL) {
			/* The size may be over estimated, no matter. */
			dct_size = sizeof(*dct) + dct->dct_bitmap_sz +
				   sizeof(dct->dct_shards[0]) * (dct->dct_max_shard + 1);

			for (j = 0; j <= dct->dct_max_shard; j++) {
				dcs = &dct->dct_shards[j];
				if (dcs->dcs_nr > 1)
					dct_size += sizeof(dcs->dcs_buf[0]) * dcs->dcs_nr;
			}

			if (coa->coa_for_modify)
				dct_size += sizeof(dct->dct_tgt_ids[0]) * dct->dct_tgt_nr;

			if (coa->coa_max_dct_sz < dct_size)
				coa->coa_max_dct_sz = dct_size;

			if (coa->coa_dct_nr < i)
				memcpy(&coa->coa_dcts[coa->coa_dct_nr], dct, sizeof(*dct));

			coa->coa_dct_nr++;
			*size += dct_size;
		}
	}

	/* Reset the other dct slots to avoid double free during cleanup. */
	if (coa->coa_dct_cap > coa->coa_dct_nr && coa->coa_dct_nr > 0)
		memset(&coa->coa_dcts[coa->coa_dct_nr], 0,
		       sizeof(*dct) * (coa->coa_dct_cap - coa->coa_dct_nr));

	return 0;
}

static int
obj_coll_oper_args_collapse(struct coll_oper_args *coa, struct dc_object *obj, uint32_t *size)
{
	int	rc;

	if (coa->coa_sparse)
		rc = obj_coll_collapse_tree(coa, size);
	else
		rc = obj_coll_collapse_array(coa, size);

	if (rc >= 0) {
		obj->cob_rank_nr = coa->coa_dct_nr;
		/* If all shards are NONEXIST, then need not to send RPC(s). */
		if (unlikely(coa->coa_dct_nr == 0))
			rc = 1;
	}

	return rc;
}

int
obj_coll_prep_one(struct coll_oper_args *coa, struct dc_object *obj,
		  uint32_t map_ver, uint32_t idx)
{
	struct dc_obj_shard	*shard = NULL;
	struct daos_coll_target	*dct;
	struct daos_coll_shard	*dcs;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	d_iov_t			 viov;
	uint64_t		 key;
	uint32_t		*tmp;
	uint8_t			*new_bm;
	int			 size;
	int			 rc = 0;
	int			 i;

	rc = obj_shard_open(obj, idx, map_ver, &shard);
	if (rc == -DER_NONEXIST)
		D_GOTO(out, rc = 0);

	if (rc != 0 || (shard->do_rebuilding && !coa->coa_for_modify))
		goto out;

	D_RWLOCK_RDLOCK(&obj->cob_lock);

	D_ASSERTF(shard->do_target_rank <= obj->cob_max_rank,
		  "Unexpected shard with rank %u > %u\n", shard->do_target_rank, obj->cob_max_rank);
	D_ASSERTF(shard->do_target_rank >= obj->cob_min_rank,
		  "Unexpected shard with rank %u < %u\n", shard->do_target_rank, obj->cob_min_rank);

	if (coa->coa_sparse) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		key = shard->do_target_rank;
		d_iov_set(&kiov, &key, sizeof(key));
		d_iov_set(&riov, coa, sizeof(*coa));
		d_iov_set(&viov, NULL, 0);
		rc = dbtree_upsert(coa->coa_tree->cst_tree_hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
				   &kiov, &riov, &viov);
		if (rc != 0)
			goto out;

		dct = viov.iov_buf;
	} else {
		dct = &coa->coa_dcts[shard->do_target_rank - obj->cob_min_rank];
		D_RWLOCK_UNLOCK(&obj->cob_lock);
	}

	dct->dct_rank = shard->do_target_rank;

	if (shard->do_target_idx >= dct->dct_bitmap_sz << 3) {
		size = (shard->do_target_idx >> 3) + 1;

		D_ALLOC_ARRAY(dcs, size << 3);
		if (dcs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		if (dct->dct_shards != NULL) {
			memcpy(dcs, dct->dct_shards, sizeof(*dcs) * (dct->dct_max_shard + 1));
			for (i = 0; i <= dct->dct_max_shard; i++) {
				if (dcs[i].dcs_nr == 1)
					dcs[i].dcs_buf = &dcs[i].dcs_inline;
			}
			D_FREE(dct->dct_shards);
		}
		dct->dct_shards = dcs;

		D_REALLOC(new_bm, dct->dct_bitmap, dct->dct_bitmap_sz, size);
		if (new_bm == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		dct->dct_bitmap = new_bm;
		dct->dct_bitmap_sz = size;
	}

	dcs = &dct->dct_shards[shard->do_target_idx];

	if (unlikely(isset(dct->dct_bitmap, shard->do_target_idx))) {
		/* More than one shards reside on the same VOS target. */
		D_ASSERT(dcs->dcs_nr >= 1);

		if (dcs->dcs_nr >= dcs->dcs_cap) {
			D_ALLOC_ARRAY(tmp, dcs->dcs_nr << 1);
			if (tmp == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			memcpy(tmp, dcs->dcs_buf, sizeof(*tmp) * dcs->dcs_nr);
			if (dcs->dcs_buf != &dcs->dcs_inline)
				D_FREE(dcs->dcs_buf);
			dcs->dcs_buf = tmp;
			dcs->dcs_cap = dcs->dcs_nr << 1;
		}
	} else {
		D_ASSERT(dcs->dcs_nr == 0);

		dcs->dcs_idx = idx;
		dcs->dcs_buf = &dcs->dcs_inline;
		setbit(dct->dct_bitmap, shard->do_target_idx);
		if (dct->dct_max_shard < shard->do_target_idx)
			dct->dct_max_shard = shard->do_target_idx;
	}

	dcs->dcs_buf[dcs->dcs_nr++] = shard->do_id.id_shard;

	if (unlikely(dct->dct_tgt_nr == (uint8_t)(-1))) {
		D_WARN("Too much shards for obj "DF_OID"reside on the same target %u/%u\n",
		       DP_OID(obj->cob_md.omd_id), shard->do_target_rank, shard->do_target_idx);
		goto out;
	}

	if (coa->coa_for_modify) {
		if (dct->dct_tgt_nr >= dct->dct_tgt_cap) {
			if (dct->dct_tgt_cap == 0)
				size = 4;
			else if (dct->dct_tgt_cap <= 8)
				size = dct->dct_tgt_cap << 1;
			else
				size = dct->dct_tgt_cap + 8;

			D_REALLOC_ARRAY(tmp, dct->dct_tgt_ids, dct->dct_tgt_cap, size);
			if (tmp == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dct->dct_tgt_ids = tmp;
			dct->dct_tgt_cap = size;
		}

		/*
		 * There may be repeated elements in the dct->dct_tgt_ids array because multiple
		 * object shards reside on the same VOS target. It is no matter to store them in
		 * DTX MBS. Related DTX check logic will handle that.
		 */
		dct->dct_tgt_ids[dct->dct_tgt_nr++] = shard->do_target_id;
		if (coa->coa_max_shard_nr < dct->dct_tgt_nr)
			coa->coa_max_shard_nr = dct->dct_tgt_nr;

		if (coa->coa_target_nr < DTX_COLL_INLINE_TARGETS &&
		    !shard->do_rebuilding && !shard->do_reintegrating)
			coa->coa_targets[coa->coa_target_nr++] = shard->do_target_id;

		if (coa->coa_max_bitmap_sz < dct->dct_bitmap_sz)
			coa->coa_max_bitmap_sz = dct->dct_bitmap_sz;
	} else {
		/* "dct_tgt_cap" is zero, then will not send dct_tgt_ids to server. */
		dct->dct_tgt_nr++;
	}

out:
	if (shard != NULL)
		obj_shard_close(shard);

	return rc;
}

struct obj_coll_punch_cb_args {
	unsigned char		*cpca_buf;
	struct dtx_memberships	*cpca_mbs;
	struct dc_obj_shard	*cpca_shard;
	crt_bulk_t		*cpca_bulks;
	crt_proc_t		 cpca_proc;
	d_sg_list_t		 cpca_sgl;
	d_iov_t			 cpca_iov;
};

static int
dc_obj_coll_punch_cb(tse_task_t *task, void *data)
{
	struct obj_coll_punch_cb_args	*cpca = data;

	if (cpca->cpca_bulks != NULL) {
		if (cpca->cpca_bulks[0] != CRT_BULK_NULL)
			crt_bulk_free(cpca->cpca_bulks[0]);
		D_FREE(cpca->cpca_bulks);
	}

	if (cpca->cpca_proc != NULL)
		crt_proc_destroy(cpca->cpca_proc);

	D_FREE(cpca->cpca_mbs);
	D_FREE(cpca->cpca_buf);
	obj_shard_close(cpca->cpca_shard);

	return 0;
}

static int
dc_obj_coll_punch_mbs(struct coll_oper_args *coa, struct dc_object *obj, uint32_t leader_id,
		      struct dtx_memberships **p_mbs)
{
	struct dtx_memberships	*mbs;
	struct dtx_daos_target	*ddt;
	struct dtx_coll_target	*dct;
	int			 rc = 0;
	int			 i;
	int			 j;

	D_ALLOC(mbs, sizeof(*mbs) + sizeof(*ddt) * coa->coa_target_nr + sizeof(*dct));
	if (mbs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/*
	 * For object collective punch, even if we lost some redundancy groups when DTX resync,
	 * we still continue to punch remaining shards. So let's set dm_grp_cnt as 1 to bypass
	 * redundancy group check.
	 */
	mbs->dm_grp_cnt = 1;
	mbs->dm_tgt_cnt = coa->coa_target_nr;
	mbs->dm_data_size = sizeof(*ddt) * coa->coa_target_nr + sizeof(*dct);
	mbs->dm_flags = DMF_CONTAIN_LEADER | DMF_COLL_TARGET;

	/* ddt[0] will be the lead target. */
	ddt = &mbs->dm_tgts[0];
	ddt[0].ddt_id = leader_id;

	for (i = 0, j = 1; i < coa->coa_target_nr && j < coa->coa_target_nr; i++) {
		if (coa->coa_targets[i] != ddt[0].ddt_id)
			ddt[j++].ddt_id = coa->coa_targets[i];
	}

	dct = (struct dtx_coll_target *)(ddt + coa->coa_target_nr);
	dct->dct_fdom_lvl = obj->cob_md.omd_fdom_lvl;
	dct->dct_pda = obj->cob_md.omd_pda;
	dct->dct_pdom_lvl = obj->cob_md.omd_pdom_lvl;
	dct->dct_layout_ver = obj->cob_layout_version;

	/* The other fields will not be packed on-wire. Related engine will fill them in future. */

	*p_mbs = mbs;

out:
	return rc;
}

static int
dc_obj_coll_punch_bulk(tse_task_t *task, struct coll_oper_args *coa,
		       struct obj_coll_punch_cb_args *cpca, uint32_t *p_size)
{
	/* The proc function may pack more information inside the buffer, enlarge the size a bit. */
	uint32_t	size = (*p_size * 9) >> 3;
	uint32_t	used = 0;
	int		rc = 0;
	int		i;

again:
	D_ALLOC(cpca->cpca_buf, size);
	if (cpca->cpca_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = crt_proc_create(daos_task2ctx(task), cpca->cpca_buf, size, CRT_PROC_ENCODE,
			     &cpca->cpca_proc);
	if (rc != 0)
		goto out;

	for (i = 0; i < coa->coa_dct_nr; i++) {
		rc = crt_proc_struct_daos_coll_target(cpca->cpca_proc, CRT_PROC_ENCODE,
						      &coa->coa_dcts[i]);
		if (rc != 0)
			goto out;
	}

	used = crp_proc_get_size_used(cpca->cpca_proc);
	if (unlikely(used > size)) {
		crt_proc_destroy(cpca->cpca_proc);
		cpca->cpca_proc = NULL;
		D_FREE(cpca->cpca_buf);
		size = used;
		goto again;
	}

	cpca->cpca_iov.iov_buf = cpca->cpca_buf;
	cpca->cpca_iov.iov_buf_len = used;
	cpca->cpca_iov.iov_len = used;

	cpca->cpca_sgl.sg_nr = 1;
	cpca->cpca_sgl.sg_nr_out = 1;
	cpca->cpca_sgl.sg_iovs = &cpca->cpca_iov;

	rc = obj_bulk_prep(&cpca->cpca_sgl, 1, false, CRT_BULK_RO, task, &cpca->cpca_bulks);

out:
	if (rc != 0) {
		if (cpca->cpca_proc != NULL) {
			crt_proc_destroy(cpca->cpca_proc);
			cpca->cpca_proc = NULL;
		}
		D_FREE(cpca->cpca_buf);
	} else {
		*p_size = used;
	}

	return rc;
}

static int
dc_coll_sort_cmp(const void *m1, const void *m2)
{
	const struct daos_coll_target	*dct1 = m1;
	const struct daos_coll_target	*dct2 = m2;

	if (dct1->dct_rank > dct2->dct_rank)
		return 1;

	if (dct1->dct_rank < dct2->dct_rank)
		return -1;

	return 0;
}

int
dc_obj_coll_punch(tse_task_t *task, struct dc_object *obj, struct dtx_epoch *epoch,
		  uint32_t map_ver, daos_obj_punch_t *args, struct obj_auxi_args *auxi)
{
	struct shard_punch_args		*spa = &auxi->p_args;
	struct coll_oper_args		*coa = &spa->pa_coa;
	struct dc_obj_shard		*shard = NULL;
	struct dtx_memberships		*mbs = NULL;
	struct daos_coll_target		*dct;
	struct daos_coll_target		 tmp_tgt;
	struct obj_coll_punch_cb_args	 cpca = { 0 };
	uint32_t			 tgt_size = 0;
	uint32_t			 mbs_max_size;
	uint32_t			 inline_size;
	uint32_t			 flags = ORF_LEADER;
	uint32_t			 leader = -1;
	uint32_t			 len;
	int				 rc;
	int				 i;

	rc = obj_coll_oper_args_init(coa, obj, true);
	if (rc != 0)
		goto out;

	for (i = 0; i < obj->cob_shards_nr; i++) {
		rc = obj_coll_prep_one(coa, obj, map_ver, i);
		if (rc != 0)
			goto out;
	}

	rc = obj_coll_oper_args_collapse(coa, obj, &tgt_size);
	if (rc != 0)
		goto out;

	if (auxi->io_retry) {
		if (unlikely(spa->pa_auxi.shard >= obj->cob_shards_nr))
			goto new_leader;

		/* Try to reuse the same leader. */
		rc = obj_shard_open(obj, spa->pa_auxi.shard, map_ver, &shard);
		if (rc == 0) {
			if (!shard->do_rebuilding && !shard->do_reintegrating) {
				tmp_tgt.dct_rank = shard->do_target_rank;
				dct = bsearch(&tmp_tgt, coa->coa_dcts, coa->coa_dct_nr,
					      sizeof(tmp_tgt), &dc_coll_sort_cmp);
				D_ASSERT(dct != NULL);

				goto gen_mbs;
			}

			obj_shard_close(shard);
			shard = NULL;
		} else if (rc != -DER_NONEXIST) {
			goto out;
		}

		/* Then change to new leader for retry. */
	}

new_leader:
	if (leader == -1)
		/* Randomly select a rank as the leader. */
		leader = d_rand() % coa->coa_dct_nr;
	else
		leader = (leader + 1) % coa->coa_dct_nr;

	dct = &coa->coa_dcts[leader];
	len = dct->dct_bitmap_sz << 3;

	for (i = 0; i < len; i++) {
		if (isset(dct->dct_bitmap, i)) {
			rc = obj_shard_open(obj, dct->dct_shards[i].dcs_idx, map_ver, &shard);
			D_ASSERT(rc == 0);

			if (!shard->do_rebuilding && !shard->do_reintegrating)
				goto gen_mbs;

			obj_shard_close(shard);
			shard = NULL;
		}
	}

	goto new_leader;

gen_mbs:
	if (dct != &coa->coa_dcts[0]) {
		memcpy(&tmp_tgt, &coa->coa_dcts[0], sizeof(tmp_tgt));
		memcpy(&coa->coa_dcts[0], dct, sizeof(tmp_tgt));
		memcpy(dct, &tmp_tgt, sizeof(tmp_tgt));
	}

	rc = dc_obj_coll_punch_mbs(coa, obj, shard->do_target_id, &mbs);
	if (rc < 0)
		goto out;

	inline_size = sizeof(*mbs) + mbs->dm_data_size + sizeof(struct obj_coll_punch_in);
	D_ASSERTF(inline_size < DAOS_BULK_LIMIT,
		  "Too much data to be held inside coll punch RPC body: %u vs %u\n",
		  inline_size, DAOS_BULK_LIMIT);

	if (inline_size + tgt_size >= DAOS_BULK_LIMIT) {
		rc = dc_obj_coll_punch_bulk(task, coa, &cpca, &tgt_size);
		if (rc != 0)
			goto out;
	}

	cpca.cpca_shard = shard;
	cpca.cpca_mbs = mbs;
	rc = tse_task_register_comp_cb(task, dc_obj_coll_punch_cb, &cpca, sizeof(cpca));
	if (rc != 0)
		goto out;

	if (auxi->io_retry) {
		flags |= ORF_RESEND;
		/* Reset @enqueue_id if resend to new leader. */
		if (spa->pa_auxi.target != shard->do_target_id)
			spa->pa_auxi.enqueue_id = 0;
	} else {
		spa->pa_auxi.obj_auxi = auxi;
		daos_dti_gen(&spa->pa_dti, false);
	}

	spa->pa_auxi.target = shard->do_target_id;
	spa->pa_auxi.shard = shard->do_shard_idx;

	if (obj_is_ec(obj))
		flags |= ORF_EC;

	mbs_max_size = sizeof(*mbs) + mbs->dm_data_size +
		       sizeof(coa->coa_targets[0]) * coa->coa_max_shard_nr + coa->coa_max_bitmap_sz;

	return dc_obj_shard_coll_punch(shard, spa, mbs, mbs_max_size, cpca.cpca_bulks, tgt_size,
				       coa->coa_dcts, coa->coa_dct_nr, coa->coa_max_dct_sz, epoch,
				       args->flags, flags, map_ver, &auxi->map_ver_reply, task);

out:
	if (rc > 0)
		rc = 0;

	DL_CDEBUG(rc == 0, DB_IO, DLOG_ERR, rc,
		  "DAOS_OBJ_RPC_COLL_PUNCH for "DF_OID" map_ver %u, task %p",
		  DP_OID(obj->cob_md.omd_id), map_ver, task);

	if (cpca.cpca_bulks != NULL) {
		if (cpca.cpca_bulks[0] != CRT_BULK_NULL)
			crt_bulk_free(cpca.cpca_bulks[0]);
		D_FREE(cpca.cpca_bulks);
	}

	if (cpca.cpca_proc != NULL)
		crt_proc_destroy(cpca.cpca_proc);
	D_FREE(cpca.cpca_buf);

	if (shard != NULL)
		obj_shard_close(shard);
	D_FREE(mbs);

	/* obj_coll_oper_args_fini() will be triggered via complete callback. */
	tse_task_complete(task, rc);

	return rc;
}

int
queue_coll_query_task(tse_task_t *api_task, struct obj_auxi_args *obj_auxi, struct dc_object *obj,
		      struct dtx_id *xid, struct dtx_epoch *epoch, uint32_t map_ver)
{
	struct coll_oper_args		*coa = &obj_auxi->cq_args.cqa_coa;
	struct obj_coll_disp_cursor	*ocdc = &obj_auxi->cq_args.cqa_cur;
	struct dc_cont			*cont = obj->cob_co;
	crt_endpoint_t			 tgt_ep = { 0 };
	uint32_t			 tmp;
	int				 rc = 0;
	int				 i;

	rc = obj_coll_oper_args_collapse(coa, obj, &tmp);
	if (rc != 0)
		goto out;

	obj_coll_disp_init(coa->coa_dct_nr, coa->coa_max_dct_sz, sizeof(struct obj_coll_query_in),
			   0, 0, ocdc);

	for (i = 0; i < ocdc->grp_nr; i++) {
		obj_coll_disp_dest(ocdc, coa->coa_dcts, &tgt_ep);

		tmp = coa->coa_dcts[ocdc->cur_pos].dct_shards[tgt_ep.ep_tag].dcs_idx;
		rc = queue_shard_query_key_task(api_task, obj_auxi, epoch, tmp, map_ver,
						obj, xid, cont->dc_cont_hdl, cont->dc_uuid,
						&coa->coa_dcts[ocdc->cur_pos], ocdc->cur_step);
		if (rc != 0)
			goto out;

		obj_coll_disp_move(ocdc);
	}

out:
	return rc;
}
