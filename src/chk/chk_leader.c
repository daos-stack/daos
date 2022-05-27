/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <time.h>
#include <cart/api.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/vos.h>
#include <daos_srv/iv.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

#define DF_LEADER	"Check leader (gen: "DF_X64")"
#define DP_LEADER(ins)	(ins)->ci_bk.cb_gen

static struct chk_instance	*chk_leader;

struct chk_sched_args {
	struct chk_instance	*csa_ins;
	struct btr_root		 csa_btr;
	daos_handle_t		 csa_hdl;
	d_list_t		 csa_list;
	uint32_t		 csa_count;
	uint32_t		 csa_refs;
};

static struct chk_sched_args *
chk_csa_alloc(struct chk_instance *ins)
{
	struct umem_attr	 uma = { 0 };
	struct chk_sched_args	*csa;
	int			 rc;

	D_ALLOC_PTR(csa);
	if (csa == NULL)
		goto out;

	D_INIT_LIST_HEAD(&csa->csa_list);
	csa->csa_refs = 1;
	csa->csa_ins = ins;

	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
				   &csa->csa_btr, &csa->csa_hdl);
	if (rc != 0)
		D_FREE(csa);

out:
	return csa;
}

static inline void
chk_csa_get(struct chk_sched_args *csa)
{
	csa->csa_refs++;
}

static inline void
chk_csa_put(struct chk_sched_args *csa)
{
	if (csa != NULL) {
		csa->csa_refs--;
		if (csa->csa_refs == 0) {
			dbtree_destroy(csa->csa_hdl, NULL);
			D_FREE(csa);
		}
	}
}

struct chk_rank_rec {
	/* Link into chk_instance::ci_rank_list. */
	d_list_t		 crr_link;
	/* The list of chk_pending_rec. */
	d_list_t		 crr_pending_list;
	d_rank_t		 crr_rank;
	uint32_t		 crr_phase;
	struct chk_instance	*crr_ins;
};

struct chk_rank_bundle {
	d_rank_t		 crb_rank;
	uint32_t		 crb_phase;
	struct chk_instance	*crb_ins;
};

static int
chk_rank_hkey_size(void)
{
	return sizeof(d_rank_t);
}

static void
chk_rank_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(d_rank_t));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
chk_rank_alloc(struct btr_instance *tins, d_iov_t *key_iov, d_iov_t *val_iov,
	       struct btr_record *rec, d_iov_t *val_out)
{
	struct chk_rank_bundle	*crb = val_iov->iov_buf;
	struct chk_rank_rec	*crr;
	int			 rc = 0;

	D_ASSERT(crb != NULL);

	D_ALLOC_PTR(crr);
	if (crr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_INIT_LIST_HEAD(&crr->crr_pending_list);
	crr->crr_rank = crb->crb_rank;
	crr->crr_phase = crb->crb_phase;
	crr->crr_ins = crb->crb_ins;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, crr);
	d_list_add_tail(&crr->crr_link, &crb->crb_ins->ci_rank_list);

out:
	return rc;
}

static int
chk_rank_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct chk_rank_rec	*crr;
	struct chk_pending_rec	*cpr;
	struct chk_pending_rec	*tmp;
	struct chk_instance	*ins;
	d_iov_t			 kiov;
	uint64_t		 seq;
	int			 rc = 0;
	int			 rc1 = 0;

	crr = (struct chk_rank_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	rec->rec_off = UMOFF_NULL;
	ins = crr->crr_ins;

	ABT_rwlock_wrlock(ins->ci_abt_lock);
	/* Cleanup all pending records belong to this rank. */
	d_list_for_each_entry_safe(cpr, tmp, &crr->crr_pending_list, cpr_rank_link) {
		ABT_mutex_lock(cpr->cpr_mutex);
		if (cpr->cpr_busy) {
			cpr->cpr_exiting = 1;
			ABT_cond_broadcast(cpr->cpr_cond);
			ABT_mutex_unlock(cpr->cpr_mutex);
		} else {
			ABT_mutex_unlock(cpr->cpr_mutex);
			/* Copy the seq to avoid accessing free DRAM after dbtree_delete. */
			seq = cpr->cpr_seq;
			d_iov_set(&kiov, &seq, sizeof(cpr->cpr_seq));
			rc1 = dbtree_delete(ins->ci_pending_hdl, BTR_PROBE_EQ, &kiov, NULL);
			if (unlikely(rc1 != 0)) {
				D_ERROR("Failed to remove pending rec for rank %u, seq "
					DF_X64", gen "DF_X64": "DF_RC"\n",
					crr->crr_rank, seq, ins->ci_bk.cb_gen, DP_RC(rc1));
				if (rc == 0)
					rc = rc1;
			}
		}
	}
	ABT_rwlock_unlock(ins->ci_abt_lock);

	d_list_del(&crr->crr_link);
	D_FREE(crr);

	return rc;
}

static int
chk_rank_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct chk_rank_rec	*crr;

	D_ASSERT(val_iov != NULL);

	crr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, crr, sizeof(*crr));

	return 0;
}

static int
chk_rank_update(struct btr_instance *tins, struct btr_record *rec,
		d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	struct chk_rank_bundle  *crb = val->iov_buf;
	struct chk_rank_rec	*crr;

	crr = (struct chk_rank_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	crr->crr_phase = crb->crb_phase;

	return 0;
}

btr_ops_t chk_rank_ops = {
	.to_hkey_size	= chk_rank_hkey_size,
	.to_hkey_gen	= chk_rank_hkey_gen,
	.to_rec_alloc	= chk_rank_alloc,
	.to_rec_free	= chk_rank_free,
	.to_rec_fetch	= chk_rank_fetch,
	.to_rec_update  = chk_rank_update,
};

static void
chk_leader_exit(struct chk_instance *ins, uint32_t status, bool bcast)
{
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_iv		 iv = { 0 };
	int			 rc = 0;

	if ((bcast && status == CHK__CHECK_INST_STATUS__CIS_FAILED) ||
	    status == CHK__CHECK_INST_STATUS__CIS_IMPLICATED) {
		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = cbk->cb_phase;
		iv.ci_status = status;

		/* Asynchronously notify the engines that the check leader exit. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
				   CRT_IV_SYNC_LAZY, true);
		if (rc != 0)
			D_ERROR(DF_LEADER" failed to notify the engines its exit, status %u: "
				DF_RC"\n", DP_LEADER(ins), status, DP_RC(rc));
	}

	ABT_rwlock_wrlock(ins->ci_abt_lock);
	rc = dbtree_destroy(ins->ci_pending_hdl, NULL);
	ABT_rwlock_unlock(ins->ci_abt_lock);
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to destroy pending record tree, status %u: "DF_RC"\n",
			DP_LEADER(ins), status, DP_RC(rc));

	rc = dbtree_destroy(ins->ci_rank_hdl, NULL);
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to destroy rank tree, status %u: "DF_RC"\n",
			DP_LEADER(ins), status, DP_RC(rc));

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_ins_status = status;
		cbk->cb_time.ct_stop_time = time(NULL);
		rc = chk_bk_update_leader(cbk);
		if (rc != 0)
			D_ERROR(DF_LEADER" exit with status %u: "DF_RC"\n",
				DP_LEADER(ins), status, DP_RC(rc));
	}
}

static uint32_t
chk_leader_find_slowest(struct chk_instance *ins)
{
	uint32_t		 phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;
	uint32_t		 base = ins->ci_bk.cb_phase;
	struct chk_rank_rec	*crr;

	d_list_for_each_entry(crr, &ins->ci_rank_list, crr_link) {
		if (crr->crr_phase <= base) {
			phase = crr->crr_phase;
			break;
		}

		if (crr->crr_phase < phase)
			phase = crr->crr_phase;
	}

	return phase;
}

static int
chk_leader_handle_pools_p1(struct chk_sched_args *csa)
{
	/* TBD: merge with Liwei's patch. */

	return 0;
}

static int
chk_leader_handle_pools_p2(struct chk_sched_args *csa)
{
	/* TBD: merge with Liwei's patch. */

	return 0;
}

static void
chk_leader_sched(void *args)
{
	struct chk_sched_args	*csa = args;
	struct chk_instance	*ins = csa->csa_ins;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	uint32_t		 phase;
	uint32_t		 status;
	struct chk_iv		 iv = { 0 };
	int			 rc = 0;
	bool			 bcast = false;

	ABT_mutex_lock(ins->ci_abt_mutex);

again:
	if (!ins->ci_sched_running) {
		ABT_mutex_unlock(ins->ci_abt_mutex);
		D_GOTO(out, rc = 0);
	}

	if (ins->ci_started) {
		ABT_mutex_unlock(ins->ci_abt_mutex);
		goto handle;
	}

	ABT_cond_wait(ins->ci_abt_cond, ins->ci_abt_mutex);

	goto again;

handle:
	phase = chk_leader_find_slowest(ins);
	if (phase != cbk->cb_phase) {
		cbk->cb_phase = phase;
		chk_bk_update_leader(cbk);
	}

	if (cbk->cb_phase == CHK__CHECK_SCAN_PHASE__CSP_PREPARE) {
		rc = chk_leader_handle_pools_p1(csa);
		if (rc != 0)
			D_GOTO(out, bcast = true);

		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST;
		iv.ci_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;

		/* Synchronously notify the engines to move ahead. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
				   CRT_IV_SYNC_EAGER, true);
		if (rc != 0) {
			D_ERROR(DF_LEADER" failed to notify the engines to move phase to %u: "
				DF_RC"\n",
				DP_LEADER(ins), CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS, DP_RC(rc));

			/* Have to failout since cannot drive the check to go ahead. */
			D_GOTO(out, bcast = false);
		}

		/*
		 * XXX: Update the bookmark after successfully notify the check engines.
		 *	Do not change the order, otherwise if the check instance restart
		 *	before the phase CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST, then next
		 *	time leader will not IV for CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST.
		 */
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST;
		chk_bk_update_leader(cbk);
	}

	if (cbk->cb_phase == CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST) {
		rc = chk_leader_handle_pools_p2(csa);
		if (rc != 0)
			D_GOTO(out, bcast = true);

		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS;
		iv.ci_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;

		/* Synchronously notify the engines to move ahead. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
				   CRT_IV_SYNC_EAGER, true);
		if (rc != 0) {
			D_ERROR(DF_LEADER" failed to notify the engines to move phase to %u: "
				DF_RC"\n",
				DP_LEADER(ins), CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS, DP_RC(rc));

			/* Have to failout since cannot drive the check to go ahead. */
			D_GOTO(out, bcast = false);
		}

		/*
		 * XXX: Update the bookmark after successfully notify the check engines.
		 *	Do not change the order, otherwise if the check instance restart
		 *	before the phase CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST, then next
		 *	time leader will not IV for CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS.
		 */
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS;
		chk_bk_update_leader(cbk);
	}

	while (ins->ci_sched_running) {
		dss_sleep(300);

		/* Someone wants to stop the check. */
		if (!ins->ci_sched_running)
			D_GOTO(out, rc = 0);

		/*
		 * TBD: The leader may need to detect engines' status/phase actively, otherwise
		 *	if some engine failed to notify the leader for its status/phase changes,
		 *	then the leader will be blocked there.
		 */

		phase = chk_leader_find_slowest(ins);
		if (phase != cbk->cb_phase) {
			cbk->cb_phase = phase;
			/* XXX: How to estimate the left time? */
			cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
			chk_bk_update_leader(cbk);
			if (phase == CHK__CHECK_SCAN_PHASE__DSP_DONE)
				D_GOTO(out, rc = 1);
		}
	}

out:
	if (rc > 0) {
		/* If some engine(s) failed during the start, then mark the instance as 'failed'. */
		if (ins->ci_slowest_fail_phase != CHK__CHECK_SCAN_PHASE__CSP_PREPARE)
			status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		else
			status = CHK__CHECK_INST_STATUS__CIS_COMPLETED;
	} else if (rc == 0) {
		if (ins->ci_implicated)
			status = CHK__CHECK_INST_STATUS__CIS_IMPLICATED;
		else if (ins->ci_stopping)
			status = CHK__CHECK_INST_STATUS__CIS_STOPPED;
		else
			status = CHK__CHECK_INST_STATUS__CIS_PAUSED;
	} else {
		status = CHK__CHECK_INST_STATUS__CIS_FAILED;
	}

	chk_leader_exit(ins, status, bcast);
	chk_csa_put(csa);

	D_INFO(DF_LEADER" exit at the phase %u: "DF_RC"\n", DP_LEADER(ins), cbk->cb_phase, DP_RC(rc));
}

static int
chk_leader_start_prepare(struct chk_instance *ins, uint32_t rank_nr, d_rank_t *ranks,
			 uint32_t policy_nr, struct chk_policy **policies,
			 uint32_t pool_nr, uuid_t pools[], int phase,
			 uint32_t *flags, d_rank_list_t **rlist)
{
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	int			 rc = 0;
	int			 i;
	int			 j;

	/*
	 * XXX: Consider the following scenario:
	 *
	 *	1. Start check on pool_A and pool_B: dmg check start -p pool_A -p pool_B
	 *	2. Before the check done, we stop the check, at the time, pool_A's check is in
	 *	   the phase_A, pool_B's is in the phase_B: dmg check stop
	 *	3. Sometime later, we restart the check for the pool_A: dmg start -p pool_A
	 *	   That will resume the check from the phase_A for the pool_A.
	 *	4. When the check for pool_A is done, the check is marked as 'completed' although
	 *	   pool_B is not full checked.
	 *	5. Then we restart the check on the pool_B: dmg start -p pool_B
	 *	   The expected behavior is to resume the check from the phase_B for the pool_B,
	 *	   but because we trace the check engine process via single bookmark, the real
	 *	   action is re-check pool_B from the beginning. That will waste some of former
	 *	   check work on the pool_B.
	 *
	 *	Let's optimize above scenario in next step.
	 */

	if (ins->ci_sched_running)
		D_GOTO(out, rc = -DER_ALREADY);

	/* Corrupted bookmark or new created one. Nothing can be reused. */
	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER) {
		memset(prop, 0, sizeof(*prop));
		memset(cbk, 0, sizeof(*cbk));
		cbk->cb_magic = CHK_BK_MAGIC_LEADER;
		cbk->cb_version = DAOS_CHK_VERSION;
		*flags |= CHK__CHECK_FLAG__CF_RESET;
		goto init;
	}

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_ALREADY);

	if (*flags & CHK__CHECK_FLAG__CF_RESET)
		goto init;

	/* Former instance is done, restart from the beginning. */
	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_COMPLETED) {
		*flags |= CHK__CHECK_FLAG__CF_RESET;
		goto init;
	}

	if (cbk->cb_phase == CHK__CHECK_SCAN_PHASE__CSP_PREPARE) {
		*flags |= CHK__CHECK_FLAG__CF_RESET;
		goto init;
	}

	/* Drop dryrun flags needs to reset. */
	if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN && !(*flags & CHK__CHECK_FLAG__CF_DRYRUN)) {
		*flags |= CHK__CHECK_FLAG__CF_RESET;
		goto init;
	}

	/*
	 * XXX: If current rank list does not matches the former list, the we need to
	 *	reset the check from scratch. Currently, we do not strictly check that.
	 *	It is control plane's duty to generate valid rank list.
	 */

	/* Add new rank(s), need to reset. */
	if (rank_nr > prop->cp_rank_nr) {
		*flags |= CHK__CHECK_FLAG__CF_RESET;
		goto init;
	}

	if (prop->cp_pool_nr < 0)
		goto init;

	/* Want to check new pool(s), need to reset. */
	if (pool_nr < 0) {
		*flags |= CHK__CHECK_FLAG__CF_RESET;
		goto init;
	}

	for (i = 0; i < pool_nr; i++) {
		for (j = 0; j < prop->cp_pool_nr; j++) {
			if (uuid_compare(pools[i], prop->cp_pools[j]) == 0)
				break;
		}

		/* Want to check new pool(s), need to reset. */
		if (j == prop->cp_pool_nr) {
			*flags |= CHK__CHECK_FLAG__CF_RESET;
			goto init;
		}
	}

init:
	rc = chk_prop_prepare(rank_nr, ranks, policy_nr, policies, pool_nr, pools,
			      *flags, phase, dss_self_rank(), prop, rlist);
	if (rc == 0 && *flags & CHK__CHECK_FLAG__CF_RESET) {
		/* New generation for reset case. */
		cbk->cb_gen = crt_hlc_get();
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
		memset(&cbk->cb_statistics, 0, sizeof(cbk->cb_statistics));
	}

out:
	return rc;
}

static int
chk_leader_dup_clue(struct ds_pool_clue **tgt, struct ds_pool_clue *src)
{
	struct ds_pool_clue		*clue = NULL;
	struct ds_pool_svc_clue		*svc = NULL;
	char				*label = NULL;
	int				 rc = 0;

	D_ALLOC_PTR(clue);
	if (clue == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (src->pc_svc_clue != NULL) {
		D_ALLOC_PTR(svc);
		if (svc == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(svc, src->pc_svc_clue, sizeof(*svc));
		if (src->pc_svc_clue->psc_db_clue.bcl_replicas != NULL) {
			rc = d_rank_list_dup(&svc->psc_db_clue.bcl_replicas,
					     src->pc_svc_clue->psc_db_clue.bcl_replicas);
			if (rc != 0) {
				svc->psc_db_clue.bcl_replicas = NULL;
				goto out;
			}
		}
	}

	if (src->pc_label != NULL) {
		D_ALLOC(label, src->pc_label_len + 1);
		if (label == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(label, src->pc_label, src->pc_label_len);
	}

	memcpy(clue, src, sizeof(*clue));
	clue->pc_svc_clue = svc;
	clue->pc_label = label;

out:
	if (rc != 0) {
		if (svc != NULL) {
			d_rank_list_free(svc->psc_db_clue.bcl_replicas);
			D_FREE(svc);
		}

		D_FREE(clue);
	} else {
		*tgt = clue;
	}

	return rc;
}

static void
chk_leader_free_clue(void *data)
{
	struct ds_pool_clue	*clue = data;

	if (clue != NULL) {
		ds_pool_clue_fini(clue);
		D_FREE(clue);
	}
}

static int
chk_leader_start_cb(void *args, uint32_t rank, uint32_t phase, int result, void *data, uint32_t nr)
{
	struct chk_sched_args	*csa = args;
	struct ds_pool_clue	*clues = data;
	struct ds_pool_clue	*clue;
	d_iov_t			 kiov;
	int			 rc = 0;
	int			 i;

	D_ASSERTF(result >= 0, "Unexpected result for start CB %d\n", result);

	/* The engine has completed the check, remove it from the rank list. */
	if (result > 0) {
		d_iov_set(&kiov, &rank, sizeof(d_rank_t));
		rc = dbtree_delete(csa->csa_ins->ci_rank_hdl, BTR_PROBE_EQ, &kiov, NULL);
		goto out;
	}

	for (i = 0; i < nr; i++) {
		/*
		 * @clues is from chk_start_remote RPC reply, the buffer will be released after
		 * the RPC done. Let's copy all related data to new the buffer for further using.
		 */
		rc = chk_leader_dup_clue(&clue, &clues[i]);
		if (rc != 0)
			goto out;

		rc = chk_pool_add_shard(csa->csa_hdl, &csa->csa_list, clue->pc_uuid,
					clue->pc_rank, 0, NULL, csa->csa_ins, NULL,
					clue, chk_leader_free_clue);
		if (rc != 0) {
			chk_leader_free_clue(clue);
			goto out;
		}
	}

out:
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to handle start CB with ranks %u phase %d, result %d: "
			DF_RC"\n", DP_LEADER(csa->csa_ins), rank, phase, result, DP_RC(rc));

	return rc;
}

int
chk_leader_start(uint32_t rank_nr, d_rank_t *ranks,
		 uint32_t policy_nr, struct chk_policy **policies,
		 uint32_t pool_nr, uuid_t pools[], uint32_t flags, int32_t phase)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	d_rank_list_t		*rank_list = ins->ci_ranks;
	struct chk_sched_args	*csa = NULL;
	struct chk_rank_bundle	 rbund = { 0 };
	d_rank_t		 myrank = dss_self_rank();
	d_iov_t			 riov;
	d_iov_t			 kiov;
	int			 rc;
	int			 i;

	if (ins->ci_starting)
		D_GOTO(out_log, rc = -DER_INPROGRESS);

	if (ins->ci_stopping)
		D_GOTO(out_log, rc = -DER_BUSY);

	ins->ci_starting = 1;
	ins->ci_started = 0;

	rc = chk_leader_start_prepare(ins, rank_nr, ranks, policy_nr, policies,
				      pool_nr, pools, phase, &flags, &rank_list);
	if (rc != 0)
		goto out_log;

	D_ASSERT(rank_list != NULL);
	D_ASSERT(d_list_empty(&ins->ci_rank_list));
	D_ASSERT(d_list_empty(&ins->ci_pending_list));
	D_ASSERT(ins->ci_sched == ABT_THREAD_NULL);

	d_rank_list_free(ins->ci_ranks);
	ins->ci_ranks = rank_list;

	if (ins->ci_iv_ns != NULL) {
		ds_iv_ns_put(ins->ci_iv_ns);
		ins->ci_iv_ns = NULL;
	}

	if (ins->ci_iv_group != NULL) {
		crt_group_secondary_destroy(ins->ci_iv_group);
		ins->ci_iv_group = NULL;
	}

	rc = crt_group_secondary_create(CHK_DUMMY_POOL, NULL, rank_list, &ins->ci_iv_group);
	if (rc != 0)
		goto out_prep;

	rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, (unsigned char *)CHK_DUMMY_POOL,
			     ins->ci_iv_group, &ins->ci_iv_id, &ins->ci_iv_ns);
	if (rc != 0)
		goto out_group;

	ds_iv_ns_update(ins->ci_iv_ns, myrank);

	for (i = 0; i < rank_list->rl_nr; i++) {
		rbund.crb_rank = rank_list->rl_ranks[i];
		rbund.crb_phase = cbk->cb_phase;
		rbund.crb_ins = ins;

		d_iov_set(&riov, &rbund, sizeof(rbund));
		d_iov_set(&kiov, &rank_list->rl_ranks[i], sizeof(d_rank_t));
		rc = dbtree_upsert(ins->ci_rank_hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
				   &kiov, &riov, NULL);
		if (rc != 0)
			goto out_rank;
	}

	/* Always refresh the start time. */
	cbk->cb_time.ct_start_time = time(NULL);
	/* XXX: How to estimate the left time? */
	cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;
	rc = chk_bk_update_leader(cbk);
	if (rc != 0)
		goto out_rank;

	csa = chk_csa_alloc(ins);
	if (csa == NULL)
		D_GOTO(out_bk, rc = -DER_NOMEM);

	/* Take another reference for RPC. */
	chk_csa_get(csa);

	ins->ci_sched_running = 1;

	rc = dss_ult_create(chk_leader_sched, csa, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &ins->ci_sched);
	if (rc != 0) {
		chk_csa_put(csa);
		goto out_csa;
	}

	rc = chk_start_remote(rank_list, cbk->cb_gen, rank_nr, ranks, policy_nr, policies,
			      pool_nr, pools, flags, phase, myrank, chk_leader_start_cb, csa);
	if (rc != 0)
		goto out_sched;

	/* Drop the reference for RPC. */
	chk_csa_put(csa);

	ABT_mutex_lock(ins->ci_abt_mutex);
	ins->ci_started = 1;
	ABT_cond_broadcast(ins->ci_abt_cond);
	ABT_mutex_unlock(ins->ci_abt_mutex);

	goto out_log;

out_sched:
	chk_stop_sched(ins);
out_csa:
	chk_csa_put(csa);
out_bk:
	if (rc != -DER_ALREADY && cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_time.ct_stop_time = time(NULL);
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		chk_bk_update_leader(cbk);
	}
out_rank:
	dbtree_destroy(ins->ci_rank_hdl, NULL);
	ins->ci_rank_hdl = DAOS_HDL_INVAL;
	ds_iv_ns_put(ins->ci_iv_ns);
	ins->ci_iv_ns = NULL;
out_group:
	crt_group_secondary_destroy(ins->ci_iv_group);
	ins->ci_iv_group = NULL;
out_prep:
	d_rank_list_free(ins->ci_ranks);
	ins->ci_ranks = NULL;
	prop->cp_rank_nr = 0;
out_log:
	ins->ci_starting = 0;

	if (rc == 0) {
		D_INFO("Leader %s check on %u ranks for %u pools with "
		       "flags %x, phase %d, leader %u, gen "DF_X64"\n",
		       (flags & CHK__CHECK_FLAG__CF_RESET) ? "start" : "restart",
		       rank_nr, pool_nr, flags, phase, myrank, cbk->cb_gen);

		chk_ranks_dump(ins->ci_ranks->rl_nr, ins->ci_ranks->rl_ranks);

		if (pool_nr > 0)
			chk_pools_dump(pool_nr, pools);
		else if (prop->cp_pool_nr > 0)
			chk_pools_dump(prop->cp_pool_nr, prop->cp_pools);
	} else if (rc != -DER_ALREADY) {
		D_ERROR("Leader failed to start check on %u ranks for %u pools with "
			"flags %x, phase %d, leader %u, gen "DF_X64": "DF_RC"\n",
			rank_nr, pool_nr, flags, phase, myrank, cbk->cb_gen, DP_RC(rc));
	}

	return rc;
}

static int
chk_leader_stop_cb(void *args, uint32_t rank, uint32_t phase, int result, void *data, uint32_t nr)
{
	struct chk_instance	*ins = args;
	d_iov_t			 kiov;
	int			 rc;

	D_ASSERTF(result > 0, "Unexpected result for stop CB %d\n", result);

	/* The engine has stop on the rank, remove it from the rank list. */
	d_iov_set(&kiov, &rank, sizeof(d_rank_t));
	rc = dbtree_delete(ins->ci_rank_hdl, BTR_PROBE_EQ, &kiov, NULL);
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to handle stop CB with ranks %u: "DF_RC"\n",
			DP_LEADER(ins), rank, DP_RC(rc));

	return rc;
}

int
chk_leader_stop(uint32_t pool_nr, uuid_t pools[])
{
	struct chk_instance	*ins = chk_leader;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	d_rank_list_t		*ranks = NULL;
	int			 rc = 0;

	if (ins->ci_starting)
		D_GOTO(out, rc = -DER_BUSY);

	if (ins->ci_stopping)
		D_GOTO(out, rc = -DER_INPROGRESS);

	/*
	 * XXX: It is possible that the check leader is dead. If we want to stop the stale
	 *	check instance on other engines, then we may execute the CHK_STOP from new
	 *	check leader. But if the old leader is still active, but the CHK_STOP dRPC
	 *	is sent to non-leader (or new leader), then it will cause trouble.
	 *
	 *	Here, it is not easy to know whether the old leader is still valid or not.
	 *	We have to trust control plane. It is the control plane duty to guarantee
	 *	that the CHK_STOP dRPC is sent to the right one.
	 */

	ins->ci_stopping = 1;

	/*
	 * The check instance on current engine may have failed or stopped, but we do not know
	 * whether there is active check instance on other engines or not, send stop RPC anyway.
	 */

	if (ins->ci_ranks == NULL) {
		rc = chk_prop_fetch(&ins->ci_prop, &ins->ci_ranks);
		/* We do not know the rank list, the sponsor needs to choose another leader. */
		if (rc == -DER_NONEXIST)
			D_GOTO(out, rc = -DER_NOTLEADER);

		if (rc != 0)
			goto out;

		if (unlikely(ins->ci_ranks == NULL))
			D_GOTO(out, rc = -DER_NOTLEADER);
	}

	rc = chk_stop_remote(ranks, cbk->cb_gen, pool_nr, pools, chk_leader_stop_cb, ins);
	if (rc != 0)
		goto out;

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING &&
	    d_list_empty(&ins->ci_rank_list))
		chk_stop_sched(ins);

out:
	ins->ci_stopping = 0;

	if (rc == 0) {
		D_INFO("Leader stopped check with gen "DF_X64" for %u pools\n",
		       cbk->cb_gen, pool_nr > 0 ? pool_nr : prop->cp_pool_nr);

		if (pool_nr > 0)
			chk_pools_dump(pool_nr, pools);
		else if (prop->cp_pool_nr > 0)
			chk_pools_dump(prop->cp_pool_nr, prop->cp_pools);
	} else {
		D_ERROR("Leader failed to stop check with gen "DF_X64" for %u pools: "DF_RC"\n",
			cbk->cb_gen, pool_nr > 0 ? pool_nr : prop->cp_pool_nr, DP_RC(rc));
	}

	return rc;
}

static int
chk_leader_dup_shard(struct chk_query_pool_shard **tgt, struct chk_query_pool_shard *src)
{
	struct chk_query_pool_shard	*shard = NULL;
	struct chk_query_target		*target = NULL;
	int				 rc = 0;

	D_ALLOC_PTR(shard);
	if (shard == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (src->cqps_targets != NULL) {
		D_ALLOC_ARRAY(target, src->cqps_target_nr);
		if (target == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(target, src->cqps_targets, sizeof(*target) * src->cqps_target_nr);
	}

	memcpy(shard, src, sizeof(*shard));
	shard->cqps_targets = target;

out:
	if (rc != 0)
		D_FREE(shard);
	else
		*tgt = shard;

	return rc;
}

static void
chk_leader_free_shard(void *data)
{
	struct chk_query_pool_shard	*shard = data;

	D_FREE(shard->cqps_targets);
	D_FREE(shard);
}

static int
chk_leader_query_cb(void *args, uint32_t rank, uint32_t phase, int result, void *data, uint32_t nr)
{
	struct chk_sched_args		*csa = args;
	struct chk_query_pool_shard	*shards = data;
	struct chk_query_pool_shard	*shard;
	int				 rc = 0;
	int				 i;

	D_ASSERTF(result == 0, "Unexpected result for query CB %d\n", result);

	for (i = 0; i < nr; i++) {
		/*
		 * @shards is from chk_query_remote RPC reply, the buffer will be released after
		 * the RPC done. Let's copy all related data to new the buffer for further using.
		 */
		rc = chk_leader_dup_shard(&shard, &shards[i]);
		if (rc != 0)
			goto out;

		rc = chk_pool_add_shard(csa->csa_hdl, &csa->csa_list, shard->cqps_uuid,
					shard->cqps_rank, shard->cqps_phase, NULL,
					csa->csa_ins, &csa->csa_count, shard,
					chk_leader_free_shard);
		if (rc != 0) {
			chk_leader_free_shard(shard);
			goto out;
		}
	}

out:
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to handle query CB with ranks %u phase %d, result %d: "
			DF_RC"\n", DP_LEADER(csa->csa_ins), rank, phase, result, DP_RC(rc));

	return rc;
}

int
chk_leader_query(uint32_t pool_nr, uuid_t pools[], chk_query_head_cb_t head_cb,
		 chk_query_pool_cb_t pool_cb, void *buf)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_sched_args	*csa = NULL;
	struct chk_pool_rec	*cpr;
	struct chk_pool_shard	*cps;
	uint32_t		 idx = 0;
	int			 rc;

	/*
	 * XXX: Similar as stop case, we need the ability to query check information from
	 *	new leader if the old one dead. But the information from new leader may be
	 *	not very accurate. It is the control plane duty to send the CHK_QUERY dRPC
	 *	to the right one.
	 */

	if (ins->ci_ranks == NULL) {
		rc = chk_prop_fetch(&ins->ci_prop, &ins->ci_ranks);
		/* We do not know the rank list, the sponsor needs to choose another leader. */
		if (rc == -DER_NONEXIST)
			D_GOTO(out, rc = -DER_NOTLEADER);

		if (rc != 0)
			goto out;

		if (unlikely(ins->ci_ranks == NULL))
			D_GOTO(out, rc = -DER_NOTLEADER);
	}

	csa = chk_csa_alloc(ins);
	if (csa == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = chk_query_remote(ins->ci_ranks, ins->ci_bk.cb_gen, pool_nr,
			      pools, chk_leader_query_cb, csa);
	if (rc != 0)
		goto out;

	rc = head_cb(cbk->cb_ins_status, cbk->cb_phase, &cbk->cb_statistics, &cbk->cb_time,
		     csa->csa_count, buf);
	if (rc != 0)
		goto out;

	d_list_for_each_entry(cpr, &csa->csa_list, cpr_link) {
		d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
			rc = pool_cb(cps->cps_data, idx++, buf);
			if (rc != 0)
				goto out;

			D_ASSERT(csa->csa_count >= idx);
		}
	}
out:
	chk_csa_put(csa);
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Leader query check with gen "DF_X64" for %u pools: "DF_RC"\n",
		 cbk->cb_gen, pool_nr, DP_RC(rc));
	return rc;
}

int
chk_leader_prop(chk_prop_cb_t prop_cb, void *buf)
{
	struct chk_property	*prop = &chk_leader->ci_prop;

	return prop_cb(buf, (struct chk_policy **)&prop->cp_policies,
		       CHK_POLICY_MAX - 1, prop->cp_flags);
}

static void
chk_leader_mark_rank_dead(d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
			  enum crt_event_type type, void *arg)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	d_iov_t			 kiov;
	uint32_t		 version = cbk->cb_gen - prop->cp_rank_nr - 1;
	int			 rc = 0;

	/* Ignore the event that is not applicable to current rank. */

	if (src != CRT_EVS_SWIM || type != CRT_EVT_DEAD)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER ||
	    cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (!chk_remove_rank_from_list(ins->ci_ranks, rank))
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	prop->cp_rank_nr--;
	rc = chk_prop_update(prop, ins->ci_ranks);
	if (rc != 0)
		goto out;

	rc = crt_group_secondary_modify(ins->ci_iv_group, ins->ci_ranks,
					ins->ci_ranks, CRT_GROUP_MOD_OP_REPLACE, version);
	if (rc != 0)
		goto out;

	d_iov_set(&kiov, &rank, sizeof(rank));
	rc = dbtree_delete(ins->ci_rank_hdl, BTR_PROBE_EQ, &kiov, NULL);
	if (rc != 0)
		goto out;

	/* The dead one is the last one, then stop the scheduler. */
	if (d_list_empty(&ins->ci_rank_list))
		chk_stop_sched(ins);
	else
		rc = chk_mark_remote(ins->ci_ranks, cbk->cb_gen, rank, version);

out:
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_LEADER" mark rank %u as dead with version %u: "DF_RC"\n",
			 DP_LEADER(ins), rank, version, DP_RC(rc));
}

int
chk_leader_act(uint64_t seq, uint32_t act, bool for_all)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pending_rec	*cpr = NULL;
	int			 rc;

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER)
		D_GOTO(out, rc = -DER_NOTLEADER);

	/* Tell control plane that no check instance is running via "-DER_NOTAPPLICABLE". */
	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	/* The admin may input the wrong option, not acceptable. */
	if (unlikely(act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT)) {
		D_ERROR("%u is not acceptable for interaction decision.\n", act);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = chk_pending_del(ins, seq, &cpr);
	if (rc != 0)
		goto out;

	D_ASSERT(cpr->cpr_busy == 1);

	if (cpr->cpr_on_leader) {
		ABT_mutex_lock(cpr->cpr_mutex);
		/*
		 * XXX: It is the control plane's duty to guarantee that the decision is a valid
		 *	action from the report options. Otherwise, related inconsistency will be
		 *	ignored.
		 */
		cpr->cpr_action = act;
		ABT_cond_broadcast(cpr->cpr_cond);
		ABT_mutex_unlock(cpr->cpr_mutex);
	}

	if (!cpr->cpr_on_leader || for_all) {
		rc = chk_act_remote(ins->ci_ranks, cbk->cb_gen, seq,
				    cpr->cpr_class, act, cpr->cpr_rank, for_all);
		if (rc != 0)
			goto out;
	}

out:
	if (cpr != NULL && !cpr->cpr_on_leader)
		chk_pending_destroy(cpr);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" takes action for report with seq "DF_X64", action %u, flags %s: "
		 DF_RC"\n", DP_LEADER(ins), seq, act, for_all ? "all" : "once", DP_RC(rc));

	return rc;

}

int
chk_leader_report(struct chk_report_unit *cru, uint64_t *seq, int *decision)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pending_rec	*cpr = NULL;
	int			 rc;

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER)
		D_GOTO(out, rc = -DER_NOTLEADER);

	/* Tell check engine that check leader is not running via "-DER_NOTAPPLICABLE". */
	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	*seq = ++(ins->ci_seq);

	D_INFO(DF_LEADER" handle %s report from rank %u with seq "
	       DF_X64" class %u, action %u, result %d\n", DP_LEADER(ins),
	       decision != NULL ? "local" : "remote", cru->cru_rank, *seq, cru->cru_cla,
	       cru->cru_act, cru->cru_result);

	if (cru->cru_act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT) {
		rc = chk_pending_add(ins, &ins->ci_pending_list, *seq,
				     cru->cru_rank, cru->cru_cla, &cpr);
		if (rc != 0)
			goto log;
	}

	rc = chk_report_upcall(cru->cru_gen, *seq, cru->cru_cla, cru->cru_act, cru->cru_result,
			       cru->cru_rank, cru->cru_target, cru->cru_pool, cru->cru_cont,
			       cru->cru_obj, cru->cru_dkey, cru->cru_akey, cru->cru_msg,
			       cru->cru_option_nr, cru->cru_options, cru->cru_detail_nr,
			       cru->cru_details);

log:
	if (rc != 0) {
		D_ERROR(DF_LEADER" failed to handle %s report from rank %u with seq "
			DF_X64", class %u, action %u, handle_rc %d, report_rc %d\n",
			DP_LEADER(ins), decision != NULL ? "local" : "remote", cru->cru_rank, *seq,
			cru->cru_cla, cru->cru_act, cru->cru_result, rc);
		goto out;
	}

	if (decision == NULL || cpr == NULL)
		goto out;

	D_ASSERT(cpr->cpr_busy == 1);

	D_INFO(DF_LEADER" need interaction for class %u with seq "DF_X64"\n",
	       DP_LEADER(ins), cru->cru_cla, *seq);

	ABT_mutex_lock(cpr->cpr_mutex);
	if (cpr->cpr_action != CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT) {
		ABT_mutex_unlock(cpr->cpr_mutex);
	} else {
		ABT_cond_wait(cpr->cpr_cond, cpr->cpr_mutex);
		ABT_mutex_unlock(cpr->cpr_mutex);
		if (!ins->ci_sched_running || cpr->cpr_exiting)
			goto out;
	}

	*decision = cpr->cpr_action;

out:
	if (cpr != NULL)
		chk_pending_destroy(cpr);

	return rc;
}

int
chk_leader_notify(uint64_t gen, d_rank_t rank, uint32_t phase, uint32_t status)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_rank_bundle	 rbund = { 0 };
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc = 0;

	/* Ignore the notification that is not applicable to current rank. */

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER ||
	    cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	switch (status) {
	case CHK__CHECK_INST_STATUS__CIS_INIT:
	case CHK__CHECK_INST_STATUS__CIS_STOPPED:
	case CHK__CHECK_INST_STATUS__CIS_PAUSED:
	case CHK__CHECK_INST_STATUS__CIS_IMPLICATED:
		/* Directly ignore above. */
		break;
	case CHK__CHECK_INST_STATUS__CIS_RUNNING:
		if (unlikely(phase < cbk->cb_phase))
			D_GOTO(out, rc = -DER_INVAL);

		if (phase == cbk->cb_phase)
			D_GOTO(out, rc = 0);

		rbund.crb_rank = rank;
		rbund.crb_phase = phase;
		rbund.crb_ins = ins;

		d_iov_set(&riov, &rbund, sizeof(rbund));
		d_iov_set(&kiov, &rank, sizeof(rank));
		rc = dbtree_upsert(ins->ci_rank_hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
				   &kiov, &riov, NULL);
		break;
	case CHK__CHECK_INST_STATUS__CIS_COMPLETED:
		/*
		 * XXX: Currently, we do not support to partial check till the specified phase.
		 *	Then the completed phase will be either container cleanup or all done.
		 */
		if (unlikely(phase != CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP &&
			     phase != CHK__CHECK_SCAN_PHASE__DSP_DONE))
			D_GOTO(out, rc = -DER_INVAL);

		d_iov_set(&kiov, &rank, sizeof(rank));
		rc = dbtree_delete(ins->ci_pending_hdl, BTR_PROBE_EQ, &kiov, NULL);
		if (rc == -DER_NONEXIST)
			rc = 0;
		break;
	case CHK__CHECK_INST_STATUS__CIS_FAILED:
		if (ins->ci_slowest_fail_phase > phase)
			ins->ci_slowest_fail_phase = phase;

		d_iov_set(&kiov, &rank, sizeof(rank));
		rc = dbtree_delete(ins->ci_pending_hdl, BTR_PROBE_EQ, &kiov, NULL);
		if (rc != 0 || !(prop->cp_flags & CHK__CHECK_FLAG__CF_FAILOUT))
			D_GOTO(out, rc = (rc == -DER_NONEXIST ? 0 : rc));

		ins->ci_implicated = 1;
		chk_stop_sched(ins);
		break;
	default:
		rc = -DER_INVAL;
		break;
	}

out:
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_LEADER" handle notification from rank %u, phase %u, status %u: "
			 DF_RC"\n", DP_LEADER(ins), rank, phase, status, DP_RC(rc));

	return (rc == 0 || rc == -DER_NOTAPPLICABLE) ? 0 : rc;
}

int
chk_leader_rejoin(uint64_t gen, d_rank_t rank, uint32_t phase)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	int			 rc = 0;

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER)
		D_GOTO(out, rc = -DER_NOTLEADER);

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_STALE);

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_SHUTDOWN);

	/* The rank has been excluded from (or never been part of) the check instance. */
	if (!chk_rank_in_list(ins->ci_ranks, rank))
		D_GOTO(out, rc = -DER_NO_PERM);

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" %u handle rejoin from rank %u with gen "DF_X64", phase %u :"DF_RC"\n",
		 DP_LEADER(ins), cbk->cb_ins_status, rank, gen, phase, DP_RC(rc));

	return rc;
}

void
chk_leader_pause(void)
{
	struct chk_instance	*ins = chk_leader;

	chk_stop_sched(ins);
	D_ASSERT(d_list_empty(&ins->ci_pending_list));
	D_ASSERT(d_list_empty(&ins->ci_rank_list));
}

int
chk_leader_init(void)
{
	struct chk_bookmark	*cbk;
	int			 rc;

	D_ALLOC(chk_leader, sizeof(*chk_leader));
	if (chk_leader == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	chk_leader->ci_is_leader = 1;
	rc = chk_ins_init(chk_leader);
	if (rc != 0)
		goto free;

	/*
	 * XXX: DAOS global consistency check depends on all related engines' local
	 *	consistency. If hit some local data corruption, then it is possible
	 *	that local consistency is not guaranteed. Need to break and resolve
	 *	related local inconsistency firstly.
	 */

	cbk = &chk_leader->ci_bk;
	rc = chk_bk_fetch_leader(cbk);
	if (rc == -DER_NONEXIST)
		rc = 0;

	/* It may be caused by local data corruption, let's break. */
	if (rc != 0)
		goto fini;

	if (unlikely(cbk->cb_magic != CHK_BK_MAGIC_LEADER)) {
		D_ERROR("Hit corrupted leader bookmark on rank %u: %u vs %u\n",
			dss_self_rank(), cbk->cb_magic, CHK_BK_MAGIC_LEADER);
		D_GOTO(fini, rc = -DER_IO);
	}

	rc = chk_prop_fetch(&chk_leader->ci_prop, &chk_leader->ci_ranks);
	if (rc == -DER_NONEXIST)
		rc = 0;

	if (rc != 0)
		goto fini;

	rc = crt_register_event_cb(chk_leader_mark_rank_dead, NULL);
	if (rc != 0)
		goto fini;

	goto out;

fini:
	chk_ins_fini(chk_leader);
free:
	D_FREE(chk_leader);
out:
	return rc;
}

void
chk_leader_fini(void)
{
	crt_unregister_event_cb(chk_leader_mark_rank_dead, NULL);
	chk_ins_fini(chk_leader);
}
