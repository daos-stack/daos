/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <abt.h>
#include <cart/api.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/iv.h>
#include <daos_srv/daos_mgmt_srv.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

struct chk_pool_bundle {
	d_list_t		*cpb_head;
	uuid_t			 cpb_uuid;
	uint32_t		*cpb_shard_nr;
	d_rank_t		 cpb_rank;
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
		if (cps == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = ABT_mutex_create(&cpr->cpr_mutex);
	if (rc != 0)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&cpr->cpr_cond);
	if (rc != 0)
		D_GOTO(out, rc = dss_abterr2der(rc));

	D_INIT_LIST_HEAD(&cpr->cpr_shard_list);
	cpr->cpr_refs = 1;
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
	if (rc != 0 && cpr != NULL) {
		if (cpr->cpr_mutex != ABT_MUTEX_NULL)
			ABT_mutex_free(&cpr->cpr_mutex);
		if (cpr->cpr_cond != ABT_COND_NULL)
			ABT_cond_free(&cpr->cpr_cond);
		D_FREE(cps);
		D_FREE(cpr);
	}

	return rc;
}

static int
chk_pool_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct chk_pool_rec	*cpr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_t			*val_iov = args;

	rec->rec_off = UMOFF_NULL;
	if (val_iov != 0)
		d_iov_set(val_iov, cpr, sizeof(*cpr));
	else
		chk_pool_put(cpr);

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
	D_ASSERT(cpb->cpb_data != NULL);

	D_ALLOC_PTR(cps);
	if (cps == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	cps->cps_rank = cpb->cpb_rank;
	cps->cps_data = cpb->cpb_data;
	cps->cps_free_cb = cpb->cpb_free_cb;

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
	D_ASSERT(val_out != NULL);

	D_ALLOC_PTR(cpr);
	if (cpr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ABT_mutex_create(&cpr->cpr_mutex);
	if (rc != 0)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&cpr->cpr_cond);
	if (rc != 0)
		D_GOTO(out, rc = dss_abterr2der(rc));

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

	d_iov_set(val_out, cpr, sizeof(*cpr));

out:
	if (rc != 0) {
		if (cpr != NULL) {
			if (cpr->cpr_mutex != ABT_MUTEX_NULL)
				ABT_mutex_free(&cpr->cpr_mutex);
			if (cpr->cpr_cond != ABT_COND_NULL)
				ABT_cond_free(&cpr->cpr_cond);
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

	if (val_iov != NULL) {
		d_iov_set(val_iov, cpr, sizeof(*cpr));
	} else {
		ABT_mutex_lock(cpr->cpr_mutex);
		if (cpr->cpr_busy) {
			cpr->cpr_exiting = 1;
			ABT_cond_broadcast(cpr->cpr_cond);
			ABT_mutex_unlock(cpr->cpr_mutex);
		} else {
			ABT_mutex_unlock(cpr->cpr_mutex);
			chk_pending_destroy(cpr);
		}
	}

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
	char	 buf[80];
	char	*ptr = buf;
	int	 rc;
	int	 i;

	if (unlikely(rank_nr == 0))
		return;

	D_INFO("Ranks List:\n");

	while (rank_nr >= 8) {
		D_INFO("%8u %8u %8u %8u %8u %8u %8u %8u\n",
		       ranks[0], ranks[1], ranks[2], ranks[3],
		       ranks[4], ranks[5], ranks[6], ranks[7]);
		rank_nr -= 8;
		ranks += 8;
	}

	if (rank_nr > 0) {
		rc = snprintf(ptr, 79, "%8u", ranks[0]);
		D_ASSERT(rc > 0);
		ptr += rc;

		for (i = 1; i < rank_nr; i++) {
			rc = snprintf(ptr, 79 - 8 * i, " %8u", ranks[i]);
			D_ASSERT(rc > 0);
			ptr += rc;
		}

		D_INFO("%s\n", buf);
	}
}

void
chk_pools_dump(d_list_t *head, int pool_nr, uuid_t pools[])
{
	struct chk_pool_rec	*cpr;
	int			 i = 0;

	if (!d_list_empty(head)) {
		D_INFO("Pools List:\n");
		d_list_for_each_entry(cpr, head, cpr_link) {
			D_INFO(DF_UUIDF"\n", DP_UUID(cpr->cpr_uuid));
		}
	} else if (pool_nr > 0) {
		D_INFO("Pools List:\n");
		do {
			D_INFO(DF_UUIDF"\n", DP_UUID(pools[i++]));
		} while (i < pool_nr);
	} else {
		D_INFO("Pools List: all\n");
	}
}

void
chk_pool_remove_nowait(struct chk_pool_rec *cpr, bool destroy)
{
	d_iov_t		kiov;
	char		uuid_str[DAOS_UUID_STR_SIZE];
	int		rc;

	cpr->cpr_skip = 1;
	if (destroy) {
		uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
		rc = chk_bk_delete_pool(uuid_str);
		if (rc != 0 && rc != -DER_NONEXIST)
			D_WARN("Failed to destroy pool bookmark: "DF_RC"\n", DP_RC(rc));
	}

	d_iov_set(&kiov, cpr->cpr_uuid, sizeof(uuid_t));
	rc = dbtree_delete(cpr->cpr_ins->ci_pool_hdl, BTR_PROBE_EQ, &kiov, NULL);
	if (rc != 0 && rc != -DER_NONEXIST && rc != -DER_NO_HDL)
		D_WARN("Failed to delete pool record: "DF_RC"\n", DP_RC(rc));
}

void
chk_pool_stop_one(struct chk_instance *ins, uuid_t uuid, int status, uint32_t phase, int *ret)
{
	struct chk_bookmark	*cbk;
	struct chk_pool_rec	*cpr;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	int			 rc = 0;

	/*
	 * Remove the pool record from the tree firstly, that will cause related scan ULT
	 * for such pool to exit, and then can update the pool's bookmark without race.
	 */

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_delete(ins->ci_pool_hdl, BTR_PROBE_EQ, &kiov, &riov);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_NO_HDL)
			rc = 0;
		else
			D_ERROR("%s on rank %u failed to delete pool record "
				DF_UUIDF" with status %u, phase %u: "DF_RC"\n",
				ins->ci_is_leader ? "leader" : "engine", dss_self_rank(),
				DP_UUID(uuid), status, phase, DP_RC(rc));
	} else {
		cpr = (struct chk_pool_rec *)riov.iov_buf;
		cbk = &cpr->cpr_bk;

		chk_pool_wait(cpr);

		if ((cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_CHECKING ||
		     cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_PENDING) &&
		    status != CHK_INVAL_STATUS) {
			if (phase != CHK_INVAL_PHASE && phase > cbk->cb_phase)
				cbk->cb_phase = phase;
			cbk->cb_pool_status = status;
			cbk->cb_time.ct_stop_time = time(NULL);
			uuid_unparse_lower(uuid, uuid_str);
			rc = chk_bk_update_pool(cbk, uuid_str);
		}

		/*
		 * NOTE: If the pool is successfully checked, then keep the PS for subsequent
		 *	 operations. Otherwise the pool may contain some inconsistency. Under
		 *	 such case, close the pool to avoid further damage.
		 */
		if (cpr->cpr_started &&
		    cpr->cpr_bk.cb_pool_status != CHK__CHECK_POOL_STATUS__CPS_CHECKED) {
			chk_pool_shutdown(cpr);
			cpr->cpr_started = 0;
		}

		/* Drop the reference that is held when create in chk_pool_alloc(). */
		chk_pool_put(cpr);
	}

	if (ret != NULL)
		*ret = rc;
}

int
chk_pools_cleanup_cb(struct sys_db *db, char *table, d_iov_t *key, void *args)
{
	struct chk_traverse_pools_args	*ctpa = args;
	char				*uuid_str = key->iov_buf;
	struct chk_bookmark		 cbk;
	int				 rc = 0;

	if (!daos_is_valid_uuid_string(uuid_str))
		D_GOTO(out, rc = 0);

	rc = chk_bk_fetch_pool(&cbk, uuid_str);
	if (rc != 0)
		goto out;

	if (ctpa->ctpa_ins->ci_start_flags & CSF_RESET_NONCOMP) {
		if (cbk.cb_phase == CHK__CHECK_SCAN_PHASE__DSP_DONE)
			goto out;

		cbk.cb_gen = ctpa->ctpa_gen;
		cbk.cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
		cbk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_UNCHECKED;
		memset(&cbk.cb_statistics, 0, sizeof(cbk.cb_statistics));
		memset(&cbk.cb_time, 0, sizeof(cbk.cb_time));
		rc = chk_bk_update_pool(&cbk, uuid_str);
	} else {
		rc = chk_bk_delete_pool(uuid_str);
	}

out:
	return rc == -DER_NONEXIST ? 0 : rc;
}

int
chk_pool_start_one(struct chk_instance *ins, uuid_t uuid, uint64_t gen)
{
	struct chk_bookmark	cbk = { 0 };
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			rc;

	uuid_unparse_lower(uuid, uuid_str);
	rc = chk_bk_fetch_pool(&cbk, uuid_str);
	if (rc != 0 && rc != -DER_NONEXIST)
		goto out;

	if (cbk.cb_magic != CHK_BK_MAGIC_POOL) {
		memset(&cbk, 0, sizeof(cbk));
		cbk.cb_magic = CHK_BK_MAGIC_POOL;
		cbk.cb_version = DAOS_CHK_VERSION;
		cbk.cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
	}

	cbk.cb_gen = gen;
	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid,
				dss_self_rank(), &cbk, ins, NULL, NULL, NULL);

out:
	return rc;
}

int
chk_pools_load_list(struct chk_instance *ins, uint64_t gen, uint32_t flags,
		    int pool_nr, uuid_t pools[])
{
	struct chk_bookmark	cbk;
	char			uuid_str[DAOS_UUID_STR_SIZE];
	d_rank_t		myrank = dss_self_rank();
	int			i;
	int			rc = 0;

	for (i = 0; i < pool_nr; i++) {
		if (!ins->ci_is_leader) {
			rc = ds_mgmt_pool_exist(pools[i]);
			/* "rc == 0" means non-exist, skip it. */
			if (rc == 0)
				continue;
			if (rc < 0)
				break;
		}

		uuid_unparse_lower(pools[i], uuid_str);
		rc = chk_bk_fetch_pool(&cbk, uuid_str);
		if (rc != 0 && rc != -DER_NONEXIST)
			break;

		if (rc == -DER_NONEXIST || flags & CHK__CHECK_FLAG__CF_RESET) {
			memset(&cbk, 0, sizeof(cbk));
			cbk.cb_magic = CHK_BK_MAGIC_POOL;
			cbk.cb_version = DAOS_CHK_VERSION;
			cbk.cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
		}

		/*
		 * NOTE: If the pool is in che check list, then load it even if its former
		 *	 check has completed, otherwise, it may be handled as dangling one.
		 */
		cbk.cb_gen = gen;
		rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, pools[i],
					myrank, &cbk, ins, NULL, NULL, NULL);
		if (rc != 0)
			break;
	}

	return rc;
}

int
chk_pools_load_from_db(struct sys_db *db, char *table, d_iov_t *key, void *args)
{
	struct chk_traverse_pools_args	*ctpa = args;
	struct chk_instance		*ins = ctpa->ctpa_ins;
	char				*uuid_str = key->iov_buf;
	uuid_t				 uuid;
	struct chk_bookmark		 cbk;
	int				 rc = 0;

	if (!daos_is_valid_uuid_string(uuid_str))
		D_GOTO(out, rc = 0);

	rc = chk_bk_fetch_pool(&cbk, uuid_str);
	if (rc != 0)
		goto out;

	if (cbk.cb_phase == CHK__CHECK_SCAN_PHASE__DSP_DONE)
		goto out;

	uuid_parse(uuid_str, uuid);

	if (!ins->ci_is_leader) {
		rc = ds_mgmt_pool_exist(uuid);
		/* "rc == 0" means non-exist, skip it. */
		if (rc <= 0)
			goto out;
	}

	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid,
				dss_self_rank(), &cbk, ins, NULL, NULL, NULL);

out:
	return rc;
}

int
chk_pools_update_bk(struct chk_instance *ins, uint32_t phase)
{
	struct chk_bookmark	*cbk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	int			 rc = 0;
	int			 rc1;

	/*
	 * Hold reference on each before update to guarantee that the next 'tmp'
	 * will not be unlinked from the list during current 'cpr' update.
	 */
	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link)
		chk_pool_get(cpr);

	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
		cbk = &cpr->cpr_bk;
		if (cbk->cb_phase < phase) {
			cbk->cb_phase = phase;
			uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
			rc1 = chk_bk_update_pool(cbk, uuid_str);
			if (rc1 != 0)
				rc = rc1;
		}
		chk_pool_put(cpr);
	}

	return rc;
}

int
chk_pool_handle_notify(struct chk_instance *ins, struct chk_iv *iv)
{
	struct chk_pool_rec	*cpr = NULL;
	struct chk_bookmark	*cbk;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	int			 rc = 0;
	int			 rc1;

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, iv->ci_uuid, sizeof(uuid_t));
	rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_NO_HDL)
			rc = -DER_NOTAPPLICABLE;

		D_GOTO(out, rc);
	}

	cpr = (struct chk_pool_rec *)riov.iov_buf;
	chk_pool_get(cpr);
	cbk = &cpr->cpr_bk;

	if (cpr->cpr_stop || unlikely(iv->ci_phase < cbk->cb_phase))
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (iv->ci_pool_status == CHK__CHECK_POOL_STATUS__CPS_CHECKED)
		cpr->cpr_done = 1;
	else if (iv->ci_pool_status == CHK__CHECK_POOL_STATUS__CPS_FAILED ||
		 iv->ci_pool_status == CHK__CHECK_POOL_STATUS__CPS_IMPLICATED)
		cpr->cpr_skip = 1;
	else if (iv->ci_pool_status != CHK__CHECK_POOL_STATUS__CPS_CHECKING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (iv->ci_phase != cbk->cb_phase || iv->ci_pool_status != cbk->cb_pool_status) {
		cbk->cb_phase = iv->ci_phase;
		cbk->cb_pool_status = iv->ci_pool_status;
		uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
		rc = chk_bk_update_pool(cbk, uuid_str);
	}

	if (rc == 0 && !cpr->cpr_started &&
	    cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_CHECKED) {
		rc1 = ds_pool_start_with_svc(cpr->cpr_uuid);
		if (rc1 == 0)
			cpr->cpr_started = 1;
		else
			D_WARN("Cannot start the pool for "DF_UUIDF" after check: "DF_RC"\n",
			       DP_UUID(cpr->cpr_uuid), DP_RC(rc1));
	}

out:
	if (cpr != NULL)
		chk_pool_put(cpr);

	return rc;
}

int
chk_pool_add_shard(daos_handle_t hdl, d_list_t *head, uuid_t uuid, d_rank_t rank,
		   struct chk_bookmark *bk, struct chk_instance *ins,
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
	rbund.cpb_bk = bk;
	rbund.cpb_ins = ins;
	rbund.cpb_data = data;
	rbund.cpb_free_cb = free_cb;

	d_iov_set(&riov, &rbund, sizeof(rbund));
	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_upsert(hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE, &kiov, &riov, NULL);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG,
		 "Add pool shard "DF_UUIDF" for rank %u: "DF_RC"\n",
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

	D_ASSERT(cpr != NULL);

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
	if (rc == 0) {
		*cpr = (struct chk_pending_rec *)viov.iov_buf;
		(*cpr)->cpr_busy = 1;
	}
	ABT_rwlock_unlock(ins->ci_abt_lock);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG,
		 "Add pending record with gen "DF_X64", seq "DF_X64", rank %u, class %u: "DF_RC"\n",
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

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG,
		 "Del pending record with gen "DF_X64", seq "DF_X64": "DF_RC"\n",
		 ins->ci_bk.cb_gen, seq, DP_RC(rc));

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
chk_prop_prepare(d_rank_t leader, uint32_t flags, int phase,
		 uint32_t policy_nr, struct chk_policy *policies,
		 d_rank_list_t *ranks, struct chk_property *prop)
{
	int	rc = 0;
	int	i;

	prop->cp_leader = leader;
	if (flags & CHK__CHECK_FLAG__CF_NO_FAILOUT)
		prop->cp_flags &= ~CHK__CHECK_FLAG__CF_FAILOUT;
	if (flags & CHK__CHECK_FLAG__CF_NO_AUTO)
		prop->cp_flags &= ~CHK__CHECK_FLAG__CF_AUTO;
	prop->cp_flags |= flags & ~(CHK__CHECK_FLAG__CF_RESET |
				    CHK__CHECK_FLAG__CF_ORPHAN_POOL |
				    CHK__CHECK_FLAG__CF_NO_FAILOUT |
				    CHK__CHECK_FLAG__CF_NO_AUTO);
	prop->cp_phase = phase;
	if (ranks != NULL)
		prop->cp_rank_nr = ranks->rl_nr;

	/* Reuse former policies if "policy_nr == 0". */
	if (policy_nr > 0) {
		memset(prop->cp_policies, 0, sizeof(Chk__CheckInconsistAction) * CHK_POLICY_MAX);
		for (i = 0; i < policy_nr; i++) {
			if (unlikely(policies[i].cp_class >= CHK_POLICY_MAX)) {
				D_ERROR("Invalid DAOS inconsistency class %u\n",
					policies[i].cp_class);
				D_GOTO(out, rc = -DER_INVAL);
			}

			prop->cp_policies[policies[i].cp_class] = policies[i].cp_action;
		}
	}

	rc = chk_prop_update(prop, ranks);

out:
	return rc;
}

int
chk_ins_init(struct chk_instance **p_ins)
{
	struct chk_instance	*ins = NULL;
	int			 rc = 0;

	D_ASSERT(p_ins != NULL);

	D_ALLOC_PTR(ins);
	if (ins == NULL)
		D_GOTO(out_init, rc = -DER_NOMEM);

	ins->ci_seq = crt_hlc_get();
	ins->ci_sched = ABT_THREAD_NULL;

	ins->ci_rank_hdl = DAOS_HDL_INVAL;
	D_INIT_LIST_HEAD(&ins->ci_rank_list);

	ins->ci_pool_hdl = DAOS_HDL_INVAL;
	D_INIT_LIST_HEAD(&ins->ci_pool_list);

	ins->ci_pending_hdl = DAOS_HDL_INVAL;
	D_INIT_LIST_HEAD(&ins->ci_pending_list);

	rc = ABT_rwlock_create(&ins->ci_abt_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_init, rc = dss_abterr2der(rc));

	rc = ABT_mutex_create(&ins->ci_abt_mutex);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_lock, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&ins->ci_abt_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_mutex, rc = dss_abterr2der(rc));

	D_GOTO(out_init, rc = 0);

out_mutex:
	ABT_mutex_free(&ins->ci_abt_mutex);
out_lock:
	ABT_rwlock_free(&ins->ci_abt_lock);
out_init:
	if (rc == 0)
		*p_ins = ins;

	return rc;
}

void
chk_ins_fini(struct chk_instance **p_ins)
{
	struct chk_instance	*ins;

	D_ASSERT(p_ins != NULL);

	ins = *p_ins;
	if (ins == NULL)
		return;

	chk_iv_ns_cleanup(&ins->ci_iv_ns);

	if (ins->ci_iv_group != NULL)
		crt_group_secondary_destroy(ins->ci_iv_group);

	d_rank_list_free(ins->ci_ranks);

	D_ASSERT(daos_handle_is_inval(ins->ci_rank_hdl));
	D_ASSERT(d_list_empty(&ins->ci_rank_list));

	D_ASSERT(daos_handle_is_inval(ins->ci_pool_hdl));
	D_ASSERT(d_list_empty(&ins->ci_pool_list));

	D_ASSERT(daos_handle_is_inval(ins->ci_pending_hdl));
	D_ASSERT(d_list_empty(&ins->ci_pending_list));

	if (ins->ci_sched != ABT_THREAD_NULL)
		ABT_thread_free(&ins->ci_sched);

	if (ins->ci_abt_cond != ABT_COND_NULL)
		ABT_cond_free(&ins->ci_abt_cond);

	if (ins->ci_abt_mutex != ABT_MUTEX_NULL)
		ABT_mutex_free(&ins->ci_abt_mutex);

	if (ins->ci_abt_lock != ABT_RWLOCK_NULL)
		ABT_rwlock_free(&ins->ci_abt_lock);

	D_FREE(ins);
	*p_ins = NULL;
}
