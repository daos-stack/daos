/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <time.h>
#include <abt.h>
#include <cart/api.h>
#include <daos/rpc.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/pool.h>
#include <daos_srv/vos.h>
#include <daos_srv/iv.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

struct chk_pool_bundle {
	d_list_t		*cpb_head;
	uint32_t		*cpb_shard_nr;
	uuid_t			 cpb_uuid;
	d_rank_t		 cpb_rank;
	uint32_t		 cpb_phase;
	struct chk_instance	*cpb_ins;
	/* Pointer to the pool bookmark. */
	struct chk_bookmark	*cpb_bk;
	void			*cpb_data;
	chk_pool_free_data_t	 cpb_free_cb;
};

static int
chk_pool_hkey_size(void)
{
	return sizeof(uuid_t);
}

static void
chk_pool_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(uuid_t));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
chk_pool_alloc(struct btr_instance *tins, d_iov_t *key_iov, d_iov_t *val_iov,
	       struct btr_record *rec, d_iov_t *val_out)
{
	struct chk_pool_bundle	*cpb = val_iov->iov_buf;
	struct chk_pool_rec	*cpr = NULL;
	struct chk_pool_shard	*cps = NULL;
	int			 rc = 0;

	D_ASSERT(cpb != NULL);

	D_ALLOC_PTR(cpr);
	if (cpr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (cpb->cpb_data != NULL) {
		D_ALLOC_PTR(cps);
		if (cps == NULL) {
			D_FREE(cpr);
			D_GOTO(out, rc = -DER_NOMEM);
		}
	}

	D_INIT_LIST_HEAD(&cpr->cpr_shard_list);
	cpr->cpr_shard_nr = 0;
	cpr->cpr_started = 0;
	cpr->cpr_phase = cpb->cpb_phase;
	uuid_copy(cpr->cpr_uuid, cpb->cpb_uuid);
	cpr->cpr_thread = ABT_THREAD_NULL;
	if (cpb->cpb_bk != NULL)
		memcpy(&cpr->cpr_bk, cpb->cpb_bk, sizeof(cpr->cpr_bk));
	cpr->cpr_ins = cpb->cpb_ins;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, cpr);
	d_list_add_tail(&cpr->cpr_link, cpb->cpb_head);

	if (cps != NULL) {
		cps->cps_rank = cpb->cpb_rank;
		cps->cps_data = cpb->cpb_data;
		cps->cps_free_cb = cpb->cpb_free_cb;

		d_list_add_tail(&cps->cps_link, &cpr->cpr_shard_list);
		cpr->cpr_shard_nr++;
		if (cpb->cpb_shard_nr != NULL)
			(*cpb->cpb_shard_nr)++;
	}

out:
	return rc;
}

static int
chk_pool_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct chk_pool_rec	*cpr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_t			*val_iov = args;
	struct chk_pool_shard	*cps;
	d_iov_t			 psid;

	rec->rec_off = UMOFF_NULL;
	d_list_del_init(&cpr->cpr_link);

	if (cpr->cpr_thread != ABT_THREAD_NULL) {
		cpr->cpr_stop = 1;
		ABT_thread_join(cpr->cpr_thread);
		ABT_thread_free(&cpr->cpr_thread);
	}

	if (cpr->cpr_started) {
		d_iov_set(&psid, cpr->cpr_uuid, sizeof(uuid_t));
		ds_rsvc_stop(DS_RSVC_CLASS_POOL, &psid, false);
		ds_pool_stop(cpr->cpr_uuid);
		cpr->cpr_started = 0;
	}

	while ((cps = d_list_pop_entry(&cpr->cpr_shard_list, struct chk_pool_shard,
				       cps_link)) != NULL) {
		if (cps->cps_free_cb != NULL)
			cps->cps_free_cb(cps->cps_data);
		else
			D_FREE(cps->cps_data);
		D_FREE(cps);
	}

	if (val_iov != 0)
		d_iov_set(val_iov, cpr, sizeof(*cpr));
	else
		D_FREE(cpr);

	return 0;
}

static int
chk_pool_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct chk_pool_rec	*cpr;

	D_ASSERT(val_iov != NULL);

	cpr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, cpr, sizeof(*cpr));

	return 0;
}

static int
chk_pool_update(struct btr_instance *tins, struct btr_record *rec,
		d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct chk_pool_bundle	*cpb = val->iov_buf;
	struct chk_pool_rec	*cpr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	struct chk_pool_shard	*cps;
	int			 rc = 0;

	D_ASSERT(cpb != NULL);

	D_ALLOC_PTR(cps);
	if (cps == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	cps->cps_rank = cpb->cpb_rank;
	cps->cps_data = cpb->cpb_data;
	cps->cps_free_cb = cpb->cpb_free_cb;

	if (cpb->cpb_phase < cpr->cpr_phase)
		cpr->cpr_phase = cpb->cpb_phase;

	d_list_add_tail(&cps->cps_link, &cpr->cpr_shard_list);
	cpr->cpr_shard_nr++;
	if (cpb->cpb_shard_nr != NULL)
		(*cpb->cpb_shard_nr)++;

out:
	return rc;
}

btr_ops_t chk_pool_ops = {
	.to_hkey_size	= chk_pool_hkey_size,
	.to_hkey_gen	= chk_pool_hkey_gen,
	.to_rec_alloc	= chk_pool_alloc,
	.to_rec_free	= chk_pool_free,
	.to_rec_fetch	= chk_pool_fetch,
	.to_rec_update  = chk_pool_update,
};

struct chk_pending_bundle {
	d_list_t		*cpb_ins_head;
	d_list_t		*cpb_rank_head;
	d_rank_t		 cpb_rank;
	uint32_t		 cpb_class;
	uint64_t		 cpb_seq;
};

static int
chk_pending_hkey_size(void)
{
	return sizeof(uint64_t);
}

static void
chk_pending_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(uint64_t));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
chk_pending_alloc(struct btr_instance *tins, d_iov_t *key_iov, d_iov_t *val_iov,
		  struct btr_record *rec, d_iov_t *val_out)
{
	struct chk_pending_bundle	*cpb = val_iov->iov_buf;
	struct chk_pending_rec		*cpr = NULL;
	int				 rc = 0;

	D_ASSERT(cpb != NULL);

	D_ALLOC_PTR(cpr);
	if (cpr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* It means that the caller wants to wait for the interaction from admin. */
	if (val_out != NULL) {
		rc = ABT_mutex_create(&cpr->cpr_mutex);
		if (rc != 0)
			D_GOTO(out, rc = dss_abterr2der(rc));

		rc = ABT_cond_create(&cpr->cpr_cond);
		if (rc != 0)
			D_GOTO(out, rc = dss_abterr2der(rc));

		d_iov_set(val_iov, cpr, sizeof(*cpr));
	}

	cpr->cpr_seq = cpb->cpb_seq;
	cpr->cpr_rank = cpb->cpb_rank;
	cpr->cpr_class = cpb->cpb_class;
	cpr->cpr_action = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

	if (cpb->cpb_rank_head != NULL)
		d_list_add_tail(&cpr->cpr_rank_link, cpb->cpb_rank_head);
	else
		D_INIT_LIST_HEAD(&cpr->cpr_rank_link);

	rec->rec_off = umem_ptr2off(&tins->ti_umm, cpr);
	d_list_add_tail(&cpr->cpr_ins_link, cpb->cpb_ins_head);

out:
	if (rc != 0) {
		if (cpr != NULL) {
			if (cpr->cpr_mutex != ABT_MUTEX_NULL)
				ABT_mutex_free(&cpr->cpr_mutex);
			D_FREE(cpr);
		}
	}

	return rc;
}

static int
chk_pending_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct chk_pending_rec	*cpr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_t			*val_iov = args;

	rec->rec_off = UMOFF_NULL;
	d_list_del_init(&cpr->cpr_ins_link);
	d_list_del_init(&cpr->cpr_rank_link);

	if (val_iov != NULL)
		d_iov_set(val_iov, cpr, sizeof(*cpr));
	else
		chk_pending_destroy(cpr);

	return 0;
}

static int
chk_pending_fetch(struct btr_instance *tins, struct btr_record *rec,
		  d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct chk_pending_rec	*cpr;

	D_ASSERT(val_iov != NULL);

	cpr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, cpr, sizeof(*cpr));

	return 0;
}

static int
chk_pending_update(struct btr_instance *tins, struct btr_record *rec,
		   d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	D_ASSERTF(0, "It should not be here\n");

	return 0;
}

btr_ops_t chk_pending_ops = {
	.to_hkey_size	= chk_pending_hkey_size,
	.to_hkey_gen	= chk_pending_hkey_gen,
	.to_rec_alloc	= chk_pending_alloc,
	.to_rec_free	= chk_pending_free,
	.to_rec_fetch	= chk_pending_fetch,
	.to_rec_update  = chk_pending_update,
};

void
chk_ranks_dump(uint32_t rank_nr, d_rank_t *ranks)
{
	char	 buf[128];
	char	*ptr = buf;
	int	 rc;
	int	 i;

	if (unlikely(rank_nr == 0))
		return;

	D_INFO("Ranks List:\n");

	while (rank_nr >= 8) {
		snprintf(ptr, 127, "%10u %10u %10u %10u %10u %10u %10u %10u",
			 ranks[0], ranks[1], ranks[2], ranks[3],
			 ranks[4], ranks[5], ranks[6], ranks[7]);
		D_INFO("%s\n", ptr);
		rank_nr -= 8;
		ranks += 8;
	}

	if (rank_nr > 0) {
		rc = snprintf(ptr, 127, "%10u", ranks[0]);
		D_ASSERT(rc > 0);
		ptr += rc;
	}

	for (i = 1; i < rank_nr; i++) {
		rc = snprintf(ptr, 127, " %10u", ranks[i]);
		D_ASSERT(rc > 0);
		ptr += rc;
	}

	D_INFO("%s\n", buf);
}

void
chk_pools_dump(uint32_t pool_nr, uuid_t pools[])
{
	char	 buf[256];
	char	*ptr = buf;
	int	 rc;
	int	 i;

	D_INFO("Pools List:\n");

	while (pool_nr > 8) {
		snprintf(buf, 255, "%s %s %s %s %s %s %s %s",
			 pools[0], pools[1], pools[2], pools[3],
			 pools[4], pools[5], pools[6], pools[7]);
		D_INFO("%s\n", buf);
		pool_nr -= 8;
		pools += 8;
	}

	if (pool_nr > 0) {
		rc = snprintf(ptr, 255, "%s", pools[0]);
		D_ASSERT(rc > 0);
		ptr += rc;
	}

	for (i = 1; i < pool_nr; i++) {
		rc = snprintf(ptr, 255, " %s", pools[i]);
		D_ASSERT(rc > 0);
		ptr += rc;
	}

	D_INFO("%s\n", buf);
}

void
chk_stop_sched(struct chk_instance *ins)
{
	if (ins->ci_sched != ABT_THREAD_NULL && ins->ci_sched_running) {
		ABT_mutex_lock(ins->ci_abt_mutex);
		ins->ci_sched_running = 0;
		ABT_cond_broadcast(ins->ci_abt_cond);
		ABT_mutex_unlock(ins->ci_abt_mutex);

		ABT_thread_join(ins->ci_sched);
		ABT_thread_free(&ins->ci_sched);
	}
}

int
chk_prop_prepare(uint32_t rank_nr, d_rank_t *ranks, uint32_t policy_nr,
		 struct chk_policy **policies, uint32_t pool_nr, uuid_t pools[],
		 uint32_t flags, int phase, d_rank_t leader,
		 struct chk_property *prop, d_rank_list_t **rlist)
{
	d_rank_list_t	*result = NULL;
	uint32_t	 saved = prop->cp_rank_nr;
	int		 rc = 0;
	int		 i;

	D_ASSERT(rlist != NULL);

	if (rank_nr != 0) {
		result = uint32_array_to_rank_list(ranks, rank_nr);
		if (result == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		prop->cp_rank_nr = rank_nr;
	} else if (*rlist == NULL) {
		D_ERROR("Rank list cannot be NULL for check start\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	prop->cp_leader = leader;
	prop->cp_flags = flags;
	prop->cp_phase = phase;

	/* Reuse former policies if "policy_nr == 0". */
	if (policy_nr > 0) {
		memset(prop->cp_policies, 0, sizeof(Chk__CheckInconsistAction) * CHK_POLICY_MAX);
		for (i = 0; i < policy_nr; i++) {
			if (unlikely(policies[i]->cp_class >= CHK_POLICY_MAX)) {
				D_ERROR("Invalid DAOS inconsistency class %u\n",
					policies[i]->cp_class);
				D_GOTO(out, rc = -DER_INVAL);
			}

			prop->cp_policies[policies[i]->cp_class] = policies[i]->cp_action;
		}
	}

	/* Reuse former pools if "pool_nr == 0". */
	if (pool_nr >= CHK_POOLS_MAX || pool_nr < 0) {
		prop->cp_pool_nr = -1;
	} else if (pool_nr > 0) {
		for (i = 0; i < pool_nr; i++)
			uuid_copy(prop->cp_pools[i], pools[i]);
		prop->cp_pool_nr = pool_nr;
	}

	if (prop->cp_pool_nr == 0)
		prop->cp_pool_nr = -1;

	rc = chk_prop_update(prop, result);
	if (rc == 0) {
		if (result != NULL)
			*rlist = result;
	} else {
		/* Keep the prop->cp_rank_nr to always match the rank list. */
		prop->cp_rank_nr = saved;
		d_rank_list_free(result);
	}

out:
	return rc;
}

int
chk_pool_add_shard(daos_handle_t hdl, d_list_t *head, uuid_t uuid, d_rank_t rank,
		   uint32_t phase, struct chk_bookmark *bk, struct chk_instance *ins,
		   uint32_t *shard_nr, void *data, chk_pool_free_data_t free_cb)
{
	struct chk_pool_bundle	rbund;
	d_iov_t			kiov;
	d_iov_t			riov;
	int			rc;

	rbund.cpb_head = head;
	rbund.cpb_shard_nr = shard_nr;
	uuid_copy(rbund.cpb_uuid, uuid);
	rbund.cpb_rank = rank;
	rbund.cpb_phase = phase;
	rbund.cpb_bk = bk;
	rbund.cpb_ins = ins;
	rbund.cpb_data = data;
	rbund.cpb_free_cb = free_cb;

	d_iov_set(&riov, &rbund, sizeof(rbund));
	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_upsert(hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, &kiov, &riov, NULL);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG, "Add pool shard "DF_UUID" for rank %u: "DF_RC"\n",
		 DP_UUID(uuid), rank, DP_RC(rc));

	return rc;
}

int
chk_pool_del_shard(daos_handle_t hdl, uuid_t uuid, d_rank_t rank)
{
	struct chk_pool_rec	*cpr;
	struct chk_pool_shard	*cps;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_lookup(hdl, &kiov, &riov);
	if (rc != 0)
		goto out;

	cpr = (struct chk_pool_rec *)riov.iov_buf;
	d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
		if (cps->cps_rank == rank) {
			d_list_del(&cps->cps_link);
			if (cps->cps_free_cb != NULL)
				cps->cps_free_cb(cps->cps_data);
			else
				D_FREE(cps->cps_data);
			D_FREE(cps);
			cpr->cpr_shard_nr--;
			if (d_list_empty(&cpr->cpr_shard_list)) {
				D_ASSERTF(cpr->cpr_shard_nr == 0,
					  "Invalid shard count %u for pool "DF_UUID"\n",
					  cpr->cpr_shard_nr, DP_UUID(uuid));
				rc = dbtree_delete(hdl, BTR_PROBE_EQ, &kiov, NULL);
			}

			goto out;
		}
	}

	rc = -DER_ENOENT;

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG, "Del pool shard "DF_UUID" for rank %u: "DF_RC"\n",
		 DP_UUID(uuid), rank, DP_RC(rc));

	return rc;
}

int
chk_pending_add(struct chk_instance *ins, d_list_t *rank_head, uint64_t seq,
		uint32_t rank, uint32_t cla, struct chk_pending_rec **cpr)
{
	struct chk_pending_bundle	rbund;
	d_iov_t				kiov;
	d_iov_t				riov;
	d_iov_t				viov;
	int				rc;

	rbund.cpb_ins_head = &ins->ci_pending_list;
	rbund.cpb_rank_head = rank_head;
	rbund.cpb_seq = seq;
	rbund.cpb_rank = rank;
	rbund.cpb_class = cla;

	d_iov_set(&viov, NULL, 0);
	d_iov_set(&riov, &rbund, sizeof(rbund));
	d_iov_set(&kiov, &seq, sizeof(seq));

	/* The access may from multiple XS (on check engine), so taking the lock firstly. */
	ABT_rwlock_wrlock(ins->ci_abt_lock);
	rc = dbtree_upsert(ins->ci_pending_hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
			   &kiov, &riov, &viov);
	if (rc == 0 && cpr != NULL) {
		*cpr = (struct chk_pending_rec *)viov.iov_buf;
		(*cpr)->cpr_busy = 1;
	}
	ABT_rwlock_unlock(ins->ci_abt_lock);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG, "Add pending record with gen "DF_X64", seq "
		 DF_X64", rank %u, class %u: "DF_RC"\n",
		 ins->ci_bk.cb_gen, seq, rank, cla, DP_RC(rc));

	return rc;
}

int
chk_pending_del(struct chk_instance *ins, uint64_t seq, struct chk_pending_rec **cpr)
{
	d_iov_t		kiov;
	d_iov_t		riov;
	int		rc;

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, &seq, sizeof(seq));

	ABT_rwlock_wrlock(ins->ci_abt_lock);
	rc = dbtree_delete(ins->ci_pending_hdl, BTR_PROBE_EQ, &kiov, &riov);
	ABT_rwlock_unlock(ins->ci_abt_lock);

	if (rc == 0)
		*cpr = (struct chk_pending_rec *)riov.iov_buf;
	else
		*cpr = NULL;

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG, "Del pending record with gen "DF_X64", seq "
		 DF_X64": "DF_RC"\n", ins->ci_bk.cb_gen, seq, DP_RC(rc));

	return rc;
}

void
chk_pending_destroy(struct chk_pending_rec *cpr)
{
	D_ASSERT(d_list_empty(&cpr->cpr_ins_link));
	D_ASSERT(d_list_empty(&cpr->cpr_rank_link));

	if (cpr->cpr_cond != ABT_COND_NULL)
		ABT_cond_free(&cpr->cpr_cond);

	if (cpr->cpr_mutex != ABT_MUTEX_NULL)
		ABT_mutex_free(&cpr->cpr_mutex);

	D_FREE(cpr);
}

int
chk_ins_init(struct chk_instance *ins)
{
	struct umem_attr	uma = { 0 };
	int			rc;

	D_ASSERT(ins != NULL);

	D_INIT_LIST_HEAD(&ins->ci_pending_list);
	ins->ci_sched = ABT_THREAD_NULL;
	ins->ci_seq = crt_hlc_get();

	if (ins->ci_is_leader)
		D_INIT_LIST_HEAD(&ins->ci_rank_list);
	else
		D_INIT_LIST_HEAD(&ins->ci_pool_list);

	rc = ABT_rwlock_create(&ins->ci_abt_lock);
		D_GOTO(out_init, rc = dss_abterr2der(rc));

	rc = ABT_mutex_create(&ins->ci_abt_mutex);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_lock, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&ins->ci_abt_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_mutex, rc = dss_abterr2der(rc));

	uma.uma_id = UMEM_CLASS_VMEM;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_PA, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pending_btr, &ins->ci_pending_hdl);
	if (rc != 0)
		goto out_cond;

	if (ins->ci_is_leader)
		rc = dbtree_create_inplace(DBTREE_CLASS_CHK_RANK, 0, CHK_BTREE_ORDER, &uma,
					   &ins->ci_rank_btr, &ins->ci_rank_hdl);
	else
		rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
					   &ins->ci_pool_btr, &ins->ci_pool_hdl);
	if (rc != 0)
		goto out_pending;

	D_GOTO(out_init, rc = 0);

out_pending:
	dbtree_destroy(ins->ci_pending_hdl, NULL);
	ins->ci_pending_hdl = DAOS_HDL_INVAL;
out_cond:
	ABT_cond_free(&ins->ci_abt_cond);
	ins->ci_abt_cond = ABT_COND_NULL;
out_mutex:
	ABT_mutex_free(&ins->ci_abt_mutex);
	ins->ci_abt_mutex = ABT_MUTEX_NULL;
out_lock:
	ABT_rwlock_free(&ins->ci_abt_lock);
	ins->ci_abt_lock = ABT_RWLOCK_NULL;
out_init:
	return rc;
}

void
chk_ins_fini(struct chk_instance *ins)
{
	if (ins == NULL)
		return;

	if (ins->ci_iv_ns != NULL)
		ds_iv_ns_put(ins->ci_iv_ns);

	if (ins->ci_iv_group != NULL)
		crt_group_secondary_destroy(ins->ci_iv_group);

	d_rank_list_free(ins->ci_ranks);

	if (ins->ci_is_leader) {
		if (daos_handle_is_valid(ins->ci_rank_hdl))
			dbtree_destroy(ins->ci_rank_hdl, NULL);

		D_ASSERT(d_list_empty(&ins->ci_rank_list));
	} else {
		if (daos_handle_is_valid(ins->ci_pool_hdl))
			dbtree_destroy(ins->ci_pool_hdl, NULL);

		D_ASSERT(d_list_empty(&ins->ci_pool_list));
	}

	if (daos_handle_is_valid(ins->ci_pending_hdl))
		dbtree_destroy(ins->ci_pending_hdl, NULL);

	D_ASSERT(d_list_empty(&ins->ci_pending_list));
	D_ASSERT(ins->ci_sched == ABT_THREAD_NULL);

	if (ins->ci_abt_cond != ABT_COND_NULL)
		ABT_cond_free(&ins->ci_abt_cond);

	if (ins->ci_abt_mutex != ABT_MUTEX_NULL)
		ABT_mutex_free(&ins->ci_abt_mutex);

	if (ins->ci_abt_lock != ABT_RWLOCK_NULL)
		ABT_rwlock_free(&ins->ci_abt_lock);

	D_FREE(ins);
}
