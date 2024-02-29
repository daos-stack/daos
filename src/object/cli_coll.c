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

int
obj_coll_oper_args_init(struct coll_oper_args *coa, struct dc_object *obj, bool for_modify)
{
	struct dc_pool	*pool = obj->cob_pool;
	uint32_t	 node_nr;
	int		 rc = 0;

	D_ASSERT(pool != NULL);
	D_ASSERT(coa->coa_dcts == NULL);

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	node_nr = pool_map_node_nr(pool->dp_map);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	D_ALLOC_ARRAY(coa->coa_dcts, node_nr);
	if (coa->coa_dcts == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/*
	 * Set coa_dct_nr as -1 to indicate that the coa_dcts array may be sparse until
	 * obj_coll_oper_args_collapse(). That is useful for obj_coll_oper_args_fini().
	 */
	coa->coa_dct_nr = -1;
	coa->coa_dct_cap = node_nr;
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
	daos_coll_target_cleanup(coa->coa_dcts,
				 coa->coa_dct_nr < 0 ? coa->coa_dct_cap : coa->coa_dct_nr);
	coa->coa_dcts = NULL;
	coa->coa_dct_cap = 0;
	coa->coa_dct_nr = 0;
}

int
obj_coll_oper_args_collapse(struct coll_oper_args *coa, uint32_t *size)
{
	struct daos_coll_target	*dct;
	struct daos_coll_shard	*dcs;
	uint32_t		 dct_size;
	int			 rc = 0;
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

	if (unlikely(coa->coa_dct_nr == 0))
		/* If all shards are NONEXIST, then need not to send RPC(s). */
		rc = 1;
	else if (coa->coa_dct_cap > coa->coa_dct_nr)
		/* Reset the other dct slots to avoid double free during cleanup. */
		memset(&coa->coa_dcts[coa->coa_dct_nr], 0,
		       sizeof(*dct) * (coa->coa_dct_cap - coa->coa_dct_nr));

	return rc;
}

int
obj_coll_prep_one(struct coll_oper_args *coa, struct dc_object *obj,
		  uint32_t map_ver, uint32_t idx)
{
	struct dc_obj_shard	*shard = NULL;
	struct daos_coll_target	*dct;
	struct daos_coll_shard	*dcs;
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

	/* The ranks for the pool may be not continuous, let's extend the coa_dcts array. */
	if (unlikely(shard->do_target_rank >= coa->coa_dct_cap)) {
		D_REALLOC_ARRAY(dct, coa->coa_dcts, coa->coa_dct_cap, shard->do_target_rank + 4);
		if (dct == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		coa->coa_dcts = dct;
		coa->coa_dct_cap = shard->do_target_rank + 4;
	}

	dct = &coa->coa_dcts[shard->do_target_rank];
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

	if (unlikely(dct->dct_tgt_nr == (uint8_t)(-1)))
		goto out;

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
	uint32_t			 leader;
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

	rc = obj_coll_oper_args_collapse(coa, &tgt_size);
	if (rc != 0)
		goto out;

	leader = coa->coa_dct_nr;

	if (auxi->io_retry) {
		if (unlikely(spa->pa_auxi.shard >= obj->cob_shards_nr))
			goto new_leader;

		/* Try to reuse the same leader. */
		rc = obj_shard_open(obj, spa->pa_auxi.shard, map_ver, &shard);
		if (rc == 0) {
			if (!shard->do_rebuilding && !shard->do_reintegrating) {
				leader = shard->do_target_rank;
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
	if (leader == coa->coa_dct_nr)
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
	if (leader != 0) {
		memcpy(&tmp_tgt, &coa->coa_dcts[0], sizeof(tmp_tgt));
		memcpy(&coa->coa_dcts[0], &coa->coa_dcts[leader], sizeof(tmp_tgt));
		memcpy(&coa->coa_dcts[leader], &tmp_tgt, sizeof(tmp_tgt));
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

	rc = obj_coll_oper_args_collapse(coa, &tmp);
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
