/**
 * (C) Copyright 2022-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <time.h>
#include <cart/api.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/rsvc.h>
#include <daos_srv/vos.h>
#include <daos_srv/iv.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

#define DF_LEADER	"Check leader (gen: "DF_X64")"
#define DP_LEADER(ins)	(ins)->ci_bk.cb_gen

static struct chk_instance	*chk_leader;

struct chk_query_args {
	struct chk_instance	*cqa_ins;
	struct btr_root		 cqa_btr;
	daos_handle_t		 cqa_hdl;
	d_list_t		 cqa_list;
	uint32_t		 cqa_count;
	uint32_t		 cqa_ins_status;
	uint32_t		 cqa_ins_phase;
	uint64_t		 cqa_gen;
};

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
	d_iov_t			*val_iov = args;
	struct chk_rank_rec	*crr;

	crr = (struct chk_rank_rec *)umem_off2ptr(&tins->ti_umm, rec->rec_off);
	rec->rec_off = UMOFF_NULL;
	d_list_del_init(&crr->crr_link);

	if (val_iov != NULL) {
		d_iov_set(val_iov, crr, sizeof(*crr));
	} else {
		/*
		 * This only happens when destroy the rank tree. At that time,
		 * the pending records tree has already been destroyed.
		 */
		D_ASSERT(d_list_empty(&crr->crr_pending_list));
		D_FREE(crr);
	}

	return 0;
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

bool
chk_is_on_leader(uint64_t gen, d_rank_t leader, bool known_leader)
{
	D_ASSERTF(gen != 0, "Invalid gen "DF_X64"\n", gen);

	if (!known_leader)
		leader = chk_leader->ci_prop.cp_leader;

	return chk_leader->ci_bk.cb_gen == gen && leader == dss_self_rank();
}

struct ds_iv_ns *
chk_leader_get_iv_ns(void)
{
	struct chk_instance	*ins = chk_leader;
	struct ds_iv_ns		*ns = ins->ci_iv_ns;

	if (ns != NULL)
		ds_iv_ns_get(ns);

	return ns;
}

static int
chk_rank_del(struct chk_instance *ins, d_rank_t rank)
{
	struct chk_rank_rec	*crr;
	struct chk_pending_rec	*cpr;
	struct chk_pending_rec	*tmp;
	d_iov_t			 riov;
	d_iov_t			 kiov;
	int			 rc;
	int			 rc1;

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, &rank, sizeof(rank));
	rc = dbtree_delete(ins->ci_rank_hdl, BTR_PROBE_EQ, &kiov, &riov);
	if (rc != 0)
		D_GOTO(out, rc = ((rc == -DER_NONEXIST || rc == -DER_NO_HDL) ? 0 : rc));

	crr = (struct chk_rank_rec *)riov.iov_buf;
	if (d_list_empty(&crr->crr_pending_list))
		goto out;

	/* Cleanup all pending records belong to this rank. */
	ABT_rwlock_wrlock(ins->ci_abt_lock);
	d_list_for_each_entry_safe(cpr, tmp, &crr->crr_pending_list, cpr_rank_link) {
		rc1 = chk_pending_wakeup(ins, cpr);
		if (rc1 != 0 && rc == 0)
			rc = rc1;
	}
	ABT_rwlock_unlock(ins->ci_abt_lock);

out:
	return rc;
}

static inline void
chk_leader_destroy_trees(struct chk_instance *ins)
{
	/*
	 * Because the pending reocrd is attached to some rank record, then destroy
	 * the pending records tree before destroying the rank records tree.
	 */
	chk_destroy_pending_tree(ins);
	chk_destroy_tree(&ins->ci_rank_hdl, &ins->ci_rank_btr);
	chk_destroy_pool_tree(ins);
}

static void
chk_leader_exit(struct chk_instance *ins, uint32_t ins_phase, uint32_t ins_status,
		uint32_t pool_status, bool bcast)
{
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_iv		 iv = { 0 };
	int			 rc = 0;

	ins->ci_sched_exiting = 1;

	D_ASSERT(d_list_empty(&ins->ci_pool_shutdown_list));

	chk_pool_stop_all(ins, pool_status, NULL);

	if ((bcast && ins_status == CHK__CHECK_INST_STATUS__CIS_FAILED) ||
	    ins_status == CHK__CHECK_INST_STATUS__CIS_IMPLICATED ||
	    unlikely(ins_status == CHK__CHECK_INST_STATUS__CIS_COMPLETED && !ins->ci_orphan_done)) {
		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = ins_phase != CHK_INVAL_PHASE ? ins_phase : cbk->cb_phase;
		iv.ci_ins_status = ins_status;

		/* Synchronously notify the engines that the check leader exit. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
				   CRT_IV_SYNC_EAGER, true);
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_LEADER" notify the engines its exit, status %u: rc = %d\n",
			 DP_LEADER(ins), ins_status, rc);
	}

	chk_leader_destroy_trees(ins);

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_ins_status = ins_status;
		if (ins_phase != CHK_INVAL_PHASE)
			cbk->cb_phase = ins_phase;
		cbk->cb_time.ct_stop_time = time(NULL);
		rc = chk_bk_update_leader(cbk);
		if (rc != 0)
			D_ERROR(DF_LEADER" exit with status %u: "DF_RC"\n",
				DP_LEADER(ins), ins_status, DP_RC(rc));
	}

	ins->ci_sched_exiting = 0;
}

static void
chk_leader_post_repair(struct chk_instance *ins, struct chk_pool_rec *cpr,
		       int *result, bool update, bool notify)
{
	struct chk_bookmark	*cbk = &cpr->cpr_bk;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	struct chk_iv		 iv = { 0 };
	int			 rc;

	D_ASSERT(cpr != NULL);

	if (unlikely(*result > 0))
		*result = 0;

	if (*result != 0) {
		chk_ins_set_fail(ins, cpr->cpr_bk.cb_phase);
		if (ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT)
			cpr->cpr_skip = 1;
	}

	if (cpr->cpr_skip || cpr->cpr_destroyed) {
		if (*result != 0) {
			cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_FAILED;
		} else if (cpr->cpr_destroyed) {
			/* Since the pool is destroyed, then mark its phase as DONE. */
			cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
			cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;
		} else {
			cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_IMPLICATED;
		}
		cbk->cb_time.ct_stop_time = time(NULL);
		uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0)
			D_WARN("Failed to update pool (" DF_UUID ") bookmark after repair: %d\n",
			       DP_UUID(cpr->cpr_uuid), rc);
	}

	/*
	 * If the operation failed and 'failout' is set, then do nothing here.
	 * chk_leader_exit will handle all the IV and bookmark related things.
	 */
	if (*result == 0 || !(ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT)) {
		if (notify) {
			iv.ci_gen = cbk->cb_gen;
			uuid_copy(iv.ci_uuid, cpr->cpr_uuid);
			iv.ci_ins_status = ins->ci_bk.cb_ins_status;
			iv.ci_phase = cbk->cb_phase;
			iv.ci_pool_status = cbk->cb_pool_status;

			/* Synchronously notify the engines that check on the pool got failure. */
			rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
					   CRT_IV_SYNC_EAGER, true);
			D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
				 DF_LEADER" notify the engines that the check for pool "
				 DF_UUIDF" is done with status %u: rc = %d\n",
				 DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), iv.ci_pool_status, rc);
			if (rc == 0)
				cpr->cpr_notified_exit = 1;
		}

		*result = 0;
	}

	if (update) {
		rc = chk_bk_update_leader(&ins->ci_bk);
		if (rc != 0)
			D_WARN("Cannot update leader bookmark after repair: "DF_RC"\n", DP_RC(rc));
	}
}

static d_rank_list_t *
chk_leader_cpr2ranklist(struct chk_pool_rec *cpr, bool svc)
{
	struct chk_pool_shard	*cps;
	struct ds_pool_clue	*clue;
	d_rank_list_t		*ranks;
	int			 i = 0;

	ranks = d_rank_list_alloc(cpr->cpr_shard_nr);
	if (ranks != NULL) {
		d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
			if (svc) {
				clue = cps->cps_data;
				if (clue == NULL || clue->pc_rc <= 0 || clue->pc_svc_clue == NULL)
					continue;
			}
			ranks->rl_ranks[i++] = cps->cps_rank;
		}

		/* There is at least one valid rank. */
		D_ASSERT(i > 0);

		/* Reset the rl_nr according to the valid ranks. */
		ranks->rl_nr = i;
	}

	return ranks;
}

static int
chk_leader_destroy_pool(struct chk_pool_rec *cpr, uint64_t seq, bool dereg)
{
	d_rank_list_t	*ranks = NULL;
	int		 rc = 0;

	/*
	 * Firstly, deregister from MS. If it is successful but we failed to destroy
	 * related pool target(s) in subsequent steps, then the pool becomes orphan.
	 * It may cause some space leak, but will not cause correctness issue. That
	 * will be handled when run DAOS check next time.
	 */
	if (dereg) {
		rc = ds_chk_deregpool_upcall(seq, cpr->cpr_uuid);
		if (rc != 0 && rc != -DER_NONEXIST)
			goto out;
	}

	ranks = chk_leader_cpr2ranklist(cpr, false);
	if (ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_tgt_pool_destroy_ranks(cpr->cpr_uuid, ranks);
	if (rc == -DER_NONEXIST)
		rc = 0;
	if (rc == 0)
		cpr->cpr_destroyed = 1;
	d_rank_list_free(ranks);

out:
	return rc;
}

static void
chk_leader_fail_pool(struct chk_pool_rec *cpr, int result)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	uint64_t			 seq;
	int				 rc;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_UNKNOWN;
	act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
	seq = chk_report_seq_gen(ins);
	cbk->cb_statistics.cs_ignored++;
	cpr->cpr_skip = 1;

	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = act;
	cru.cru_rank = dss_self_rank();
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_pool_label = cpr->cpr_label;
	cru.cru_msg = "Some engine failed to report information for pool.\n";
	cru.cru_result = result;

	rc = chk_leader_report(&cru, &seq, NULL);

	D_WARN(DF_LEADER" some engine failed to report information for pool "
	       DF_UUIDF", action %u, seq "DF_X64", remote_rc %d, report_rc %d\n",
	       DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), act, seq, result, rc);
}

/*
 * NOTE: Initialize and construct clues_out from cpr. The caller is responsible for freeing
 *	 clues->pcs_array with D_FREE, but the borrowed clues->pcs_array->pc_svc_clue must be kept.
 */
static int
chk_leader_build_pool_clues(struct chk_pool_rec *cpr)
{
	struct chk_instance	*ins = cpr->cpr_ins;
	struct chk_pool_shard	*cps;
	struct ds_pool_clue	*clue;
	struct ds_pool_clues	 clues;
	int			 rc = 0;
	bool			 update = false;

	clues.pcs_cap = 4;
	clues.pcs_len = 0;

	D_ALLOC_ARRAY(clues.pcs_array, clues.pcs_cap);
	if (clues.pcs_array == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
		clue = cps->cps_data;

		/* Related engine failed to report the pool shard(s), have to skip it. */
		if (clue == NULL || clue->pc_tgt_nr < 0) {
			if (clue == NULL)
				rc = -DER_NOMEM;
			else
				rc = clue->pc_rc;
			chk_leader_fail_pool(cpr, rc);

			D_GOTO(out, update = true);
		}

		/* Related engine failed to report PS because of PS shutdown trouble, skip it. */
		if (clue->pc_rc == -DER_BUSY) {
			chk_leader_fail_pool(cpr, rc);

			D_GOTO(out, update = true);
		}

		if (clue->pc_rc <= 0 || clue->pc_svc_clue == NULL)
			continue;

		if (clues.pcs_len == clues.pcs_cap) {
			D_REALLOC_ARRAY(clue, clues.pcs_array, clues.pcs_cap, clues.pcs_cap << 1);
			if (clue == NULL) {
				D_FREE(clues.pcs_array);
				D_GOTO(out, rc = -DER_NOMEM);
			}

			clues.pcs_array = clue;
			clues.pcs_cap <<= 1;
			clue = cps->cps_data;
		}

		memcpy(&clues.pcs_array[clues.pcs_len++], clue, sizeof(*clue));
	}

	memcpy(&cpr->cpr_clues, &clues, sizeof(cpr->cpr_clues));

out:
	if (rc != 0) {
		D_ERROR(DF_LEADER" failed to build pool service clues for "DF_UUIDF": "DF_RC"\n",
			DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), DP_RC(rc));
		/*
		 * We do not know whether the pool is inconsistency or not. But since we cannot
		 * parse the pool clues, then have to skip it. Notify the check engines.
		 */
		cpr->cpr_skip = 1;
		chk_leader_post_repair(ins, cpr, &rc, update, true);
	}

	return rc;
}

/* Only keep the chosen PS replica, destroy all others. */
static int
chk_leader_reset_pool_svc(struct chk_pool_rec *cpr)
{
	struct ds_pool_clues	*clues = &cpr->cpr_clues;
	struct chk_instance	*ins = cpr->cpr_ins;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	d_rank_t		*ranks;
	d_rank_list_t		 rank_list;
	d_iov_t			 psid;
	int			 chosen = cpr->cpr_advice;
	int			 i;
	int			 j;
	int			 rc;

	D_ASSERT(chosen >= 0 && clues->pcs_len > chosen);

	/* If the chosen one is the unique PS replica, then do nothing. */
	if (clues->pcs_len == 1)
		D_GOTO(out, rc = 0);

	D_ALLOC_ARRAY(ranks, clues->pcs_len - 1);
	if (ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Build a list of all ranks except for the chosen one. */
	for (i = 0, j = 0; i < clues->pcs_len; i++) {
		if (i != chosen)
			ranks[j++] = clues->pcs_array[i].pc_rank;
	}

	rank_list.rl_ranks = ranks;
	rank_list.rl_nr = j;

	d_iov_set(&psid, cpr->cpr_uuid, sizeof(uuid_t));
	rc = ds_rsvc_dist_stop(DS_RSVC_CLASS_POOL, &psid, &rank_list, NULL /* excluded */,
			       RDB_NIL_TERM, true /* destroy */);
	D_FREE(ranks);

out:
	if (rc != 0) {
		D_ERROR(DF_LEADER" failed to destroy other pool service replicas for "DF_UUIDF
			": "DF_RC"\n", DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), DP_RC(rc));

		cbk->cb_statistics.cs_failed++;
		cpr->cpr_skip = 1;
	}

	return rc;
}

static int
chk_leader_dangling_pool(struct chk_pool_rec *cpr)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_report_unit		 cru = { 0 };
	char				*strs[2];
	d_iov_t				 iovs[2];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	uint64_t			 seq = 0;
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_POOL_NONEXIST_ON_ENGINE;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * Default action is to de-register the dangling pool from MS.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		/* Fall through. */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		seq = chk_report_seq_gen(ins);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = ds_chk_deregpool_upcall(seq, cpr->cpr_uuid);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		/* Report the inconsistency without repair. */
		cbk->cb_statistics.cs_ignored++;
		break;
	default:
		/*
		 * If the specified action is not applicable to the inconsistency,
		 * then switch to interaction mode for the decision from admin.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT:
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_AUTO) {
			/* Ignore the inconsistency if admin does not want interaction. */
			act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			cbk->cb_statistics.cs_ignored++;
		} else {
			act = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			option_nr = 2;

			strs[0] = "Discard the dangling pool entry from MS [suggested].";
			strs[1] = "Keep the dangling pool entry on MS, repair nothing.";

			d_iov_set(&iovs[0], strs[0], strlen(strs[0]));
			d_iov_set(&iovs[1], strs[1], strlen(strs[1]));

			sgl.sg_nr = 2;
			sgl.sg_nr_out = 0;
			sgl.sg_iovs = iovs;

			details = &sgl;
			detail_nr = 1;
		}
		break;
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_detail_nr = detail_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_pool_label = cpr->cpr_label;
	cru.cru_msg = "Check leader detects dangling pool.\n";
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_leader_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc < 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" detects dangling pool "DF_UUIDF", action %u (%s), seq "
		 DF_X64", handle_rc %d, report_rc %d, decision %d\n",
		 DP_LEADER(ins), DP_UUID(cpr->cpr_uuid),
		 act, option_nr ? "need interact" : "no interact", seq, result, rc, decision);

	if (rc < 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		result = rc;
	}

	if (rc > 0 || result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;
	detail_nr = 0;

	switch (decision) {
	default:
		D_ERROR(DF_LEADER" got invalid decision %d for dangling pool "
			DF_UUIDF" with seq "DF_X64". Ignore the inconsistency.\n",
			DP_LEADER(ins), decision, DP_UUID(cpr->cpr_uuid), seq);
		/*
		 * Invalid option, ignore the inconsistency.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		cbk->cb_statistics.cs_ignored++;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = ds_chk_deregpool_upcall(seq, cpr->cpr_uuid);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	}

	goto report;

out:
	chk_leader_post_repair(ins, cpr, &result, rc <= 0, false);

	return result;
}

static int
chk_leader_orphan_pool(struct chk_pool_rec *cpr)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct ds_pool_clue		*clue = cpr->cpr_clue;
	char				*strs[3];
	d_iov_t				 iovs[3];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	uint64_t			 seq = 0;
	uint32_t			 options[3];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	D_ASSERT(cpr->cpr_advice >= 0);

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_POOL_NONEXIST_ON_MS;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	/* For orphan pool, do not export pool service until being registered to MS successfully. */
	cpr->cpr_not_export_ps = 1;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * If the pool service still can start, then the default action is to register
		 * the orphan pool to MS; otherwise, it is suggested to destroy the orphan pool.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_READD:
		/* Fall through. */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		/*
		 * If some pool shard is in zombie directory, then it is quite possible that the
		 * pool was in destroying before the corruption. It is suggested to continue the
		 * destroying of the orphan pool.
		 */
		if (chk_pool_in_zombie(cpr))
			goto interact;

		act = CHK__CHECK_INCONSIST_ACTION__CIA_READD;
		seq = chk_report_seq_gen(ins);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
			cpr->cpr_exist_on_ms = 1;
		} else {
			result = ds_chk_regpool_upcall(seq, cpr->cpr_uuid, clue->pc_label,
						       clue->pc_svc_clue->psc_db_clue.bcl_replicas);
			if (result != 0) {
				cbk->cb_statistics.cs_failed++;
			} else {
				cbk->cb_statistics.cs_repaired++;
				cpr->cpr_exist_on_ms = 1;
				cpr->cpr_not_export_ps = 0;
			}
		}
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		/* Fall through. */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		seq = chk_report_seq_gen(ins);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = chk_leader_destroy_pool(cpr, seq, false);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		/*
		 * If want to destroy the orphan pool, then skip subsequent check in spite of
		 * whether it is destroyed successfully or not.
		 */
		cpr->cpr_skip = 1;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		/* Report the inconsistency without repair. */
		cbk->cb_statistics.cs_ignored++;
		break;
	default:
		/*
		 * If the specified action is not applicable to the inconsistency,
		 * then switch to interaction mode for the decision from admin.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT:

interact:
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_AUTO) {
			/* Ignore the inconsistency if admin does not want interaction. */
			act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			cbk->cb_statistics.cs_ignored++;
		} else {
			act = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_READD;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
			options[2] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			option_nr = 3;

			strs[0] = "Re-add the orphan pool back to MS [suggested].";
			strs[1] = "Destroy the orphan pool to release space.";
			strs[2] = "Keep the orphan pool entry on engines, repair nothing.";

			d_iov_set(&iovs[0], strs[0], strlen(strs[0]));
			d_iov_set(&iovs[1], strs[1], strlen(strs[1]));
			d_iov_set(&iovs[2], strs[2], strlen(strs[2]));

			sgl.sg_nr = 3;
			sgl.sg_nr_out = 0;
			sgl.sg_iovs = iovs;

			details = &sgl;
			detail_nr = 1;
		}
		break;
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_detail_nr = detail_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_pool_label = clue->pc_label;
	cru.cru_msg = "Check leader detects orphan pool.\n";
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_leader_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc < 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" detects orphan pool "DF_UUIDF", action %u (%s), seq "
		 DF_X64", advice %d, handle_rc %d, report_rc %d, decision %d\n", DP_LEADER(ins),
		 DP_UUID(cpr->cpr_uuid), act, option_nr ? "need interact" : "no interact",
		 seq, cpr->cpr_advice, result, rc, decision);

	if (rc < 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		result = rc;
	}

	if (rc > 0 || result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;
	detail_nr = 0;

	switch (decision) {
	default:

ignore:
		D_ERROR(DF_LEADER" got invalid decision %d for orphan pool "
			DF_UUIDF" with seq "DF_X64". Ignore the inconsistency.\n",
			DP_LEADER(ins), decision, DP_UUID(cpr->cpr_uuid), seq);
		/*
		 * Invalid option, ignore the inconsistency.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		cbk->cb_statistics.cs_ignored++;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = chk_leader_destroy_pool(cpr, seq, false);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		/*
		 * If want to destroy the orphan pool, then skip subsequent check in spite of
		 * whether it is destroyed successfully or not.
		 */
		cpr->cpr_skip = 1;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_READD:
		/* NOTE: currently, we do not support to register the in-destroying pool to MS. */
		if (chk_pool_in_zombie(cpr))
			goto ignore;

		act = CHK__CHECK_INCONSIST_ACTION__CIA_READD;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
			cpr->cpr_exist_on_ms = 1;
		} else {
			result = ds_chk_regpool_upcall(seq, cpr->cpr_uuid, clue->pc_label,
						       clue->pc_svc_clue->psc_db_clue.bcl_replicas);
			if (result != 0) {
				cbk->cb_statistics.cs_failed++;
			} else {
				cbk->cb_statistics.cs_repaired++;
				cpr->cpr_exist_on_ms = 1;
				cpr->cpr_not_export_ps = 0;
			}
		}
		break;
	}

	goto report;

out:
	/*
	 * If the orphan pool is ignored (in spite of because it is required or failed
	 * to fix related inconsistency), then notify check engines to remove related
	 * pool record and bookmark.
	 */
	chk_leader_post_repair(ins, cpr, &result, rc <= 0,
			       cpr->cpr_skip && !cpr->cpr_destroyed ? true : false);

	return result;
}

static int
chk_leader_no_quorum_pool(struct chk_pool_rec *cpr)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct ds_pool_clue		*clue;
	char				*strs[3];
	char				 suggested[CHK_MSG_BUFLEN] = { 0 };
	d_iov_t				 iovs[3];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	uint64_t			 seq = 0;
	uint32_t			 options[3];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_POOL_LESS_SVC_WITHOUT_QUORUM;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	if (cpr->cpr_advice < 0) {
		switch (act) {
		case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * Destroy the corrupted pool by default.
		 *
		 * Fall through.
		 */
		case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
			act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
			seq = chk_report_seq_gen(ins);
			if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
				cbk->cb_statistics.cs_repaired++;
			} else {
				result = chk_leader_destroy_pool(cpr, seq, true);
				if (result != 0)
					cbk->cb_statistics.cs_failed++;
				else
					cbk->cb_statistics.cs_repaired++;
			}
			/*
			 * If want to destroy the pool, then skip subsequent check in spite of
			 * whether it is destroyed successfully or not.
			 */
			cpr->cpr_skip = 1;
			break;
		case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
			/* Report the inconsistency without repair. */
			cbk->cb_statistics.cs_ignored++;
			/* If ignore the corrupted pool, then skip subsequent check. */
			cpr->cpr_skip = 1;
			break;
		default:
			/*
			 * If the specified action is not applicable to the inconsistency,
			 * then switch to interaction mode for the decision from admin.
			 *
			 * Fall through.
			 */
		case CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT:
			if (prop->cp_flags & CHK__CHECK_FLAG__CF_AUTO) {
				/* Ignore the inconsistency if admin does not want interaction. */
				act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
				cbk->cb_statistics.cs_ignored++;
				cpr->cpr_skip = 1;
				break;
			}

			act = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			option_nr = 2;

			strs[0] = "Destroy the corrupted pool from related engines [suggested].";
			strs[1] = "Keep the corrupted pool on related engines, repair nothing.";

			d_iov_set(&iovs[0], strs[0], strlen(strs[0]));
			d_iov_set(&iovs[1], strs[1], strlen(strs[1]));

			sgl.sg_nr = 2;
			sgl.sg_nr_out = 0;
			sgl.sg_iovs = iovs;

			details = &sgl;
			detail_nr = 1;
			break;
		}
	} else {
		switch (act) {
		case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * If we can start some PS under DICTATE mode, then do it by default.
		 *
		 * Fall through.
		 */
		case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
			act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
			seq = chk_report_seq_gen(ins);
			if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
				cbk->cb_statistics.cs_repaired++;
				/*
				 * Under dryrun mode, we cannot start the PS with DICTATE
				 * mode, then have to skip it.
				 */
				cpr->cpr_skip = 1;
				goto report;
			}

			result = chk_leader_reset_pool_svc(cpr);
			if (result != 0 || cpr->cpr_exist_on_ms)
				goto report;

			clue = cpr->cpr_clue;
			result = ds_chk_regpool_upcall(seq, cpr->cpr_uuid, cpr->cpr_label,
						       clue->pc_svc_clue->psc_db_clue.bcl_replicas);
			if (result != 0) {
				cbk->cb_statistics.cs_failed++;
				/* Skip the pool if failed to register to MS. */
				cpr->cpr_skip = 1;
			}
			/*
			 * NOTE: For result == 0 case, it still cannot be regarded as repaired.
			 *	 We need to start the PS under DICTATE mode in subsequent step.
			 */
			break;
		case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
			seq = chk_report_seq_gen(ins);
			if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
				cbk->cb_statistics.cs_repaired++;
			} else {
				result = chk_leader_destroy_pool(cpr, seq, true);
				if (result != 0)
					cbk->cb_statistics.cs_failed++;
				else
					cbk->cb_statistics.cs_repaired++;
			}
			/*
			 * If want to destroy the pool, then skip subsequent check in spite of
			 * whether it is destroyed successfully or not.
			 */
			cpr->cpr_skip = 1;
			break;
		case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
			/* Report the inconsistency without repair. */
			cbk->cb_statistics.cs_ignored++;
			/* If ignore the corrupted pool, then skip subsequent check. */
			cpr->cpr_skip = 1;
			break;
		default:
			/*
			 * If the specified action is not applicable to the inconsistency,
			 * then switch to interaction mode for the decision from admin.
			 *
			 * Fall through.
			 */
		case CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT:
			if (prop->cp_flags & CHK__CHECK_FLAG__CF_AUTO) {
				/* Ignore the inconsistency if admin does not want interaction. */
				act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
				cbk->cb_statistics.cs_ignored++;
				cpr->cpr_skip = 1;
				break;
			}

			act = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
			options[2] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			option_nr = 3;

			clue = cpr->cpr_clue;
			snprintf(suggested, CHK_MSG_BUFLEN - 1,
				 "Start pool service under DICTATE mode from rank %d [suggested].",
				 clue->pc_rank);
			strs[0] = suggested;
			strs[1] = "Destroy the corrupted pool from related engines.";
			strs[2] = "Keep the corrupted pool on related engines, repair nothing.";

			d_iov_set(&iovs[0], strs[0], strlen(strs[0]));
			d_iov_set(&iovs[1], strs[1], strlen(strs[1]));
			d_iov_set(&iovs[2], strs[2], strlen(strs[2]));

			sgl.sg_nr = 3;
			sgl.sg_nr_out = 0;
			sgl.sg_iovs = iovs;

			details = &sgl;
			detail_nr = 1;
			break;
		}
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_detail_nr = detail_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_pool_label = cpr->cpr_label;
	cru.cru_msg = "Check leader detects corrupted pool without quorum.\n";
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_leader_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc < 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" detects corrupted pool "DF_UUIDF", action %u (%s), seq "
		 DF_X64", advice %d, handle_rc %d, report_rc %d, decision %d\n", DP_LEADER(ins),
		 DP_UUID(cpr->cpr_uuid), act, option_nr ? "need interact" : "no interact",
		 seq, cpr->cpr_advice, result, rc, decision);

	if (rc < 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		/* Skip the corrupted pool if failed to interact with admin for further action. */
		cpr->cpr_skip = 1;
		result = rc;
	}

	if (rc > 0 || result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;
	detail_nr = 0;

	switch (decision) {
	default:

ignore:
		D_ERROR(DF_LEADER" got invalid decision %d for corrupted pool "
			DF_UUIDF" with seq "DF_X64". Ignore the inconsistency.\n",
			DP_LEADER(ins), decision, DP_UUID(cpr->cpr_uuid), seq);
		/*
		 * Invalid option, ignore the inconsistency.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		cbk->cb_statistics.cs_ignored++;
		/* If ignore the corrupted pool, then skip subsequent check. */
		cpr->cpr_skip = 1;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = chk_leader_destroy_pool(cpr, seq, true);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		/*
		 * If want to destroy the corrupted pool, then skip subsequent check in spite of
		 * whether it is destroyed successfully or not.
		 */
		cpr->cpr_skip = 1;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		if (unlikely(cpr->cpr_advice < 0))
			goto ignore;

		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
			/*
			 * Under dryrun mode, we cannot start the PS with DICTATE
			 * mode, then have to skip it.
			 */
			cpr->cpr_skip = 1;
			break;
		}

		result = chk_leader_reset_pool_svc(cpr);
		if (result != 0 || cpr->cpr_exist_on_ms)
			break;

		clue = cpr->cpr_clue;
		result = ds_chk_regpool_upcall(seq, cpr->cpr_uuid, cpr->cpr_label,
					       clue->pc_svc_clue->psc_db_clue.bcl_replicas);
		if (result != 0) {
			cbk->cb_statistics.cs_failed++;
			/* Skip the pool if failed to register to MS. */
			cpr->cpr_skip = 1;
		}
		/*
		 * NOTE: For result == 0 case, it still cannot be regarded as repaired.
		 *	 We need to start the PS under DICTATE mode in subsequent step.
		 */
		break;
	}

	goto report;

out:
	/*
	 * If the corrupted pool is ignored (in spite of because it is required or failed
	 * to fix related inconsistency), then notify check engines to remove related
	 * pool record and bookmark.
	 */
	chk_leader_post_repair(ins, cpr, &result, rc <= 0, cpr->cpr_skip ? true : false);

	return result;
}

/*
 * Whether need to stop current check instance or not.
 *
 * \param ins	[IN]	The leader instance.
 * \param ret	[OUT]	When return true, set it as 1 if the checker is completed,
 *			set it as 0 if someone wants to stop the checker.
 */
static int
chk_leader_need_stop(struct chk_instance *ins, int *ret)
{
	struct chk_pool_rec	*cpr;
	bool			 dangling = false;

	if (d_list_empty(&ins->ci_rank_list)) {
		d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
			if (!cpr->cpr_skip && !cpr->cpr_done && cpr->cpr_dangling) {
				dangling = true;
				break;
			}
		}

		if (!dangling) {
			/*
			 * "ci_stopping" means that the user wants to stop checker for some pools.
			 * But the specified pools may be not in checking. "ci_pool_stopped" means
			 * the checker for some pools are really stopped.
			 */
			if (ins->ci_pool_stopped) {
				D_ASSERT(ins->ci_stopping);
				*ret = 0;
				return true;
			}

			*ret = 1;
			return true;
		}
	}

	if (!ins->ci_sched_running || ins->ci_sched_exiting) {
		*ret = 0;
		return true;
	}

	return false;
}

static inline bool
chk_leader_pool_need_stop(struct chk_pool_rec *cpr, int *ret)
{
	if (*ret < 0 || cpr->cpr_skip || cpr->cpr_stop)
		return true;

	return chk_leader_need_stop(cpr->cpr_ins, ret);
}

/*
 * Collect pool svc clues, and try to choose the available replica.
 * After the process, if the pool has no PS replica available, then
 * it is either destroyed or skipped for subsequent check.
 */
static int
chk_leader_handle_pool_clues(struct chk_pool_rec *cpr)
{
	struct ds_pool_clues	*clues;
	int			 rc;

	rc = chk_leader_build_pool_clues(cpr);
	if (chk_leader_pool_need_stop(cpr, &rc))
		goto out;

	clues = &cpr->cpr_clues;
	D_ASSERTF(clues->pcs_len >= 0, "Got invalid clues: %d\n", clues->pcs_len);

	if (clues->pcs_len > 0) {
		rc = ds_pool_check_svc_clues(clues, &cpr->cpr_advice);
		cpr->cpr_clue = &clues->pcs_array[cpr->cpr_advice];
		if (rc == 0) {
			cpr->cpr_healthy = 1;
			goto out;
		}
	} else {
		/* No pool service. */
		cpr->cpr_advice = -1;
	}

	rc = chk_leader_no_quorum_pool(cpr);

out:
	return rc;
}

static int
chk_leader_start_pool_svc(struct chk_pool_rec *cpr)
{
	struct chk_instance	*ins = cpr->cpr_ins;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct ds_pool_clue	*clue;
	d_rank_list_t		*ranks = NULL;
	d_iov_t			 psid;
	int			 rc = 0;

	D_ASSERT(cpr->cpr_advice >= 0);

	d_iov_set(&psid, cpr->cpr_uuid, sizeof(uuid_t));
	if (cpr->cpr_healthy) {
		/*
		 * If the pool has quorum for pool service, then even if some replicas are lost,
		 * it is still not regarded as 'inconsistency'. The raft mechanism will recover
		 * the other pool service replicas automatically.
		 */
		ranks = chk_leader_cpr2ranklist(cpr, true);
		if (ranks == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		/*
		 * We cannot start the pool service via regular quorum, but we can start it under
		 * DS_RSVC_DICTATE mode.
		 */
		ranks = d_rank_list_alloc(1);
		if (ranks == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		clue = cpr->cpr_clue;
		ranks->rl_ranks[0] = clue->pc_rank;
	}

	rc = ds_rsvc_dist_start(DS_RSVC_CLASS_POOL, &psid, cpr->cpr_uuid, ranks, RDB_NIL_TERM,
				cpr->cpr_healthy ? DS_RSVC_START : DS_RSVC_DICTATE,
				false /* bootstrap */, 0 /* size */, 0 /* vos_df_version */);

out:
	d_rank_list_free(ranks);
	if (rc != 0) {
		D_ERROR(DF_LEADER" failed to start pool service (%s) for "DF_UUIDF" at replica %d, "
			"skip it: "DF_RC"\n",
			DP_LEADER(ins), cpr->cpr_healthy ? "healthy" : "unhealthy",
			DP_UUID(cpr->cpr_uuid), cpr->cpr_advice, DP_RC(rc));

		cpr->cpr_skip = 1;
		if (!cpr->cpr_healthy)
			cbk->cb_statistics.cs_failed++;
		chk_leader_post_repair(ins, cpr, &rc, !cpr->cpr_healthy, true);
	} else if (!cpr->cpr_healthy) {
		cbk->cb_statistics.cs_repaired++;
		chk_leader_post_repair(ins, cpr, &rc, true, false);
	}

	return rc;
}

static int
chk_leader_handle_pool_label(struct chk_pool_rec *cpr, struct ds_pool_clue *clue)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	char				 strs[3][CHK_MSG_BUFLEN] = { 0 };
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	d_iov_t				 iovs[3];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	char				*label = NULL;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	uint64_t			 seq = 0;
	uint32_t			 options[3];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_POOL_BAD_LABEL;
	act = prop->cp_policies[cla];

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * The pool label is mainly used by MS to lookup pool UUID by label.
		 * The PS recorded label info is just some kind of backup. So trust
		 * the label info on MS if exist.
		 */
		if (cpr->cpr_label == NULL)
			goto try_ps;

		/* Fall through. */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS;
		/* Delay pool label update on PS until CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP. */
		cpr->cpr_delay_label = 1;
		goto out;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:

try_ps:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
		cbk->cb_statistics.cs_total++;
		seq = chk_report_seq_gen(ins);

		result = chk_dup_string(&label, clue->pc_label,
					clue->pc_label != NULL ? strlen(clue->pc_label) : 0);
		if (result != 0) {
			cbk->cb_statistics.cs_failed++;
			label = clue->pc_label;
			break;
		}

		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = ds_chk_regpool_upcall(seq, cpr->cpr_uuid, clue->pc_label,
						       clue->pc_svc_clue->psc_db_clue.bcl_replicas);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		if (cpr->cpr_label != NULL)
			label = cpr->cpr_label;
		else
			label = clue->pc_label;

		cbk->cb_statistics.cs_total++;
		/* Report the inconsistency without repair. */
		cbk->cb_statistics.cs_ignored++;
		break;
	default:
		/*
		 * If the specified action is not applicable to the inconsistency,
		 * then switch to interaction mode for the decision from admin.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT:
		if (cpr->cpr_label != NULL)
			label = cpr->cpr_label;
		else
			label = clue->pc_label;

		if (prop->cp_flags & CHK__CHECK_FLAG__CF_AUTO) {
			/* Ignore the inconsistency if admin does not want interaction. */
			act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			cbk->cb_statistics.cs_ignored++;
			break;
		}

		act = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

		if (cpr->cpr_label == NULL) {
			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS;
			snprintf(strs[0], CHK_MSG_BUFLEN - 1,
				 "Trust PS pool label: %s [suggested].",
				 clue->pc_label != NULL ? clue->pc_label : "(null)");
			snprintf(strs[1], CHK_MSG_BUFLEN - 1, "Trust MS pool label: (null).");
		} else {
			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
			snprintf(strs[0], CHK_MSG_BUFLEN - 1,
				 "Trust MS pool label: %s [suggested].", cpr->cpr_label);
			snprintf(strs[1], CHK_MSG_BUFLEN - 1, "Trust PS pool label: %s.",
				 clue->pc_label != NULL ? clue->pc_label : "(null)");
		}

		options[2] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		snprintf(strs[2], CHK_MSG_BUFLEN - 1,
			 "Keep the inconsistent pool label: %s (MS) vs %s (PS), repair nothing.",
			 cpr->cpr_label != NULL ? cpr->cpr_label : "(null)",
			 clue->pc_label != NULL ? clue->pc_label : "(null)");
		option_nr = 3;

		d_iov_set(&iovs[0], strs[0], strlen(strs[0]));
		d_iov_set(&iovs[1], strs[1], strlen(strs[1]));
		d_iov_set(&iovs[2], strs[2], strlen(strs[2]));

		sgl.sg_nr = 3;
		sgl.sg_nr_out = 0;
		sgl.sg_iovs = iovs;

		details = &sgl;
		detail_nr = 1;
		break;
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_detail_nr = detail_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_pool_label = label;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check leader detects corrupted pool label: %s (MS) vs %s (PS).\n",
		 cpr->cpr_label != NULL ? cpr->cpr_label : "(null)",
		 clue->pc_label != NULL ? clue->pc_label : "(null)");
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_leader_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc < 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" detects corrupted label for pool "DF_UUIDF", action %u (%s), seq "
		 DF_X64", MS label %s, PS label %s, handle_rc %d, report_rc %d, decision %d\n",
		 DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), act,
		 option_nr ? "need interact" : "no interact", seq,
		 cpr->cpr_label != NULL ? cpr->cpr_label : "(null)",
		 clue->pc_label != NULL ? clue->pc_label : "(null)", result, rc, decision);

	if (rc < 0 && option_nr > 0) {
		cbk->cb_statistics.cs_total++;
		cbk->cb_statistics.cs_failed++;
		/* It is unnecessary to skip the pool if failed to handle label inconsistency. */
		result = rc;
	}

	if (rc > 0 || result != 0 || option_nr == 0) {
		if (label != NULL && label != clue->pc_label && label != cpr->cpr_label) {
			D_FREE(cpr->cpr_label);
			cpr->cpr_label = label;
		}

		goto out;
	}

	option_nr = 0;
	detail_nr = 0;

	switch (decision) {
	default:
		D_ERROR(DF_LEADER" got invalid decision %d for corrupted pool label"
			DF_UUIDF" with seq "DF_X64". Ignore the inconsistency.\n",
			DP_LEADER(ins), decision, DP_UUID(cpr->cpr_uuid), seq);
		/*
		 * Invalid option, ignore the inconsistency.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		cbk->cb_statistics.cs_total++;
		cbk->cb_statistics.cs_ignored++;
		/* It is unnecessary to skip the pool if ignore label inconsistency. */
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS;
		/* Delay pool label update on PS until CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP. */
		cpr->cpr_delay_label = 1;
		cpr->cpr_label_seq = seq;
		goto out;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
		cbk->cb_statistics.cs_total++;

		label = NULL;
		result = chk_dup_string(&label, clue->pc_label,
					clue->pc_label != NULL ? strlen(clue->pc_label) : 0);
		if (result != 0) {
			cbk->cb_statistics.cs_failed++;
			label = clue->pc_label;
			break;
		}

		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = ds_chk_regpool_upcall(seq, cpr->cpr_uuid, clue->pc_label,
						       clue->pc_svc_clue->psc_db_clue.bcl_replicas);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	}

	goto report;

out:
	/*
	 * If decide to delay pool label update on PS until CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP,
	 * then it is unnecessary to update the leader bookmark.
	 */
	chk_leader_post_repair(ins, cpr, &result,
			       (rc > 0 || cpr->cpr_delay_label) ? false : true, false);

	return result;
}

static void
chk_leader_dp_ult(void *arg)
{
	struct chk_pool_rec	*cpr = arg;
	struct chk_bookmark	*cbk = &cpr->cpr_bk;
	int			 rc;

	cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
	rc = chk_leader_dangling_pool(cpr);
	if (rc != 0)
		cpr->cpr_skip = 1;
	else
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;

	cpr->cpr_done = 1;
	chk_pool_put(cpr);
}

static void chk_leader_pool_ult(void *arg);

static int
chk_leader_handle_pools_list(struct chk_instance *ins)
{
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_list_pool		*clp = NULL;
	struct chk_pool_rec		*cpr;
	struct chk_pool_rec		*tmp;
	d_iov_t				 riov;
	d_iov_t				 kiov;
	int				 clp_nr;
	int				 rc = 0;
	int				 i;
	bool				 exit;

	clp_nr = ds_chk_listpool_upcall(&clp);
	if (clp_nr < 0) {
		rc = clp_nr;
		clp_nr = 0;
		goto out;
	}

	if (prop->cp_flags & CHK__CHECK_FLAG__CF_FAILOUT)
		exit = true;
	else
		exit = false;

	/* Firstly, handle dangling pool(s) based on the comparison between engines and MS. */
	for (i = 0; i < clp_nr; i++) {
		d_iov_set(&riov, NULL, 0);
		d_iov_set(&kiov, clp[i].clp_uuid, sizeof(uuid_t));
		rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
		if (rc == 0) {
			cpr = (struct chk_pool_rec *)riov.iov_buf;
			cpr->cpr_exist_on_ms = 1;

			if (cpr->cpr_done)
				continue;

			rc = chk_dup_string(&cpr->cpr_label, clp[i].clp_label,
					    clp[i].clp_label != NULL ?
					    strlen(clp[i].clp_label) : 0);
			if (rc != 0) {
				cpr->cpr_skip = 1;
				goto out;
			}

			/* No engine report shard for the pool, it is dangling pool. */
			if (d_list_empty(&cpr->cpr_shard_list)) {
				chk_pool_get(cpr);
				cpr->cpr_dangling = 1;
				rc = dss_ult_create(chk_leader_dp_ult, cpr, DSS_XS_SYS, 0,
						    DSS_DEEP_STACK_SZ, &cpr->cpr_thread);
				if (rc != 0) {
					D_ERROR("Failed to create ULT for pool "DF_UUIDF
						" with %s (3): "DF_RC"\n", DP_UUID(cpr->cpr_uuid),
						exit ? "failout" : "continue", DP_RC(rc));
					cpr->cpr_skip = 1;
					chk_pool_put(cpr);
					if (exit)
						goto out;
					rc = 0;
				}
				continue;
			}

			/*
			 * For check orphan pool, since exist on both MS and engine,
			 * then it is normally, not orphan pool, remove the @cpr.
			 */
			if (cpr->cpr_for_orphan) {
				chk_pool_remove_nowait(cpr);
				continue;
			}

			chk_pool_get(cpr);
			/*
			 * Each pool will has a dedicated ULT to handle the subsequent check,
			 * then even if a pool check is blocked, it will not affect other pools.
			 */
			rc = dss_ult_create(chk_leader_pool_ult, cpr, DSS_XS_SYS, 0,
					    DSS_DEEP_STACK_SZ, &cpr->cpr_thread);
			if (rc != 0) {
				D_ERROR("Failed to create ULT for pool "DF_UUIDF" with %s (1): "
					DF_RC"\n", DP_UUID(cpr->cpr_uuid),
					exit ? "failout" : "continue", DP_RC(rc));
				cpr->cpr_skip = 1;
				chk_pool_put(cpr);
			}
		} else if (rc == -DER_NONEXIST) {
			/*
			 * If the user specified the pool in the check list, then it must exist in
			 * the pools tree. So if it is not there, then unless the check is for all
			 * pools, otherwise, skip it.
			 */
			if (!(ins->ci_start_flags & CSF_ORPHAN_POOL)) {
				rc = 0;
				continue;
			}

			rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list,
						clp[i].clp_uuid, CHK_LEADER_RANK,
						NULL /* bookmark */, ins, NULL /* shard_nr */,
						NULL /* data */, NULL, &cpr);
			if (rc != 0) {
				D_ERROR("Failed to create record for dangling pool "
					DF_UUIDF" with %s: "DF_RC"\n", DP_UUID(clp[i].clp_uuid),
					exit ? "failout" : "continue", DP_RC(rc));
				if (!exit)
					rc = 0;
			} else {
				cpr->cpr_exist_on_ms = 1;
				rc = chk_dup_string(&cpr->cpr_label, clp[i].clp_label,
						    clp[i].clp_label != NULL ?
						    strlen(clp[i].clp_label) : 0);
				if (rc != 0) {
					cpr->cpr_skip = 1;
					goto out;
				}

				chk_pool_get(cpr);
				cpr->cpr_dangling = 1;
				rc = dss_ult_create(chk_leader_dp_ult, cpr, DSS_XS_SYS, 0,
						    DSS_DEEP_STACK_SZ, &cpr->cpr_thread);
				if (rc != 0) {
					D_ERROR("Failed to create ULT for pool "DF_UUIDF
						" with %s (4): "DF_RC"\n", DP_UUID(cpr->cpr_uuid),
						exit ? "failout" : "continue", DP_RC(rc));
					cpr->cpr_skip = 1;
					chk_pool_put(cpr);
					if (!exit)
						rc = 0;
				}
			}
		} else {
			D_ERROR("Failed to verify pool "DF_UUIDF" existence with %s: "DF_RC"\n",
				DP_UUID(clp[i].clp_uuid), exit ? "failout" : "continue", DP_RC(rc));
			if (!exit)
				rc = 0;
		}

		if (rc != 0)
			goto out;
	}

	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
		if (cpr->cpr_skip || cpr->cpr_done || cpr->cpr_exist_on_ms)
			continue;

		/* The cpr is only for check dangling, can be remove from the list now. */
		if (unlikely(cpr->cpr_bk.cb_phase == CHK__CHECK_SCAN_PHASE__CSP_DONE)) {
			cpr->cpr_done = 1;
			continue;
		}

		chk_pool_get(cpr);
		/*
		 * Each pool will has a dedicated ULT to handle the subsequent check,
		 * then even if a pool check is blocked, it will not affect other pools.
		 */
		rc = dss_ult_create(chk_leader_pool_ult, cpr, DSS_XS_SYS, 0,
				    DSS_DEEP_STACK_SZ, &cpr->cpr_thread);
		if (rc != 0) {
			D_ERROR("Failed to create ULT for pool "DF_UUIDF" with %s (2): "DF_RC"\n",
				DP_UUID(cpr->cpr_uuid), exit ? "failout" : "continue", DP_RC(rc));
			cpr->cpr_skip = 1;
			chk_pool_put(cpr);
			goto out;
		}
	}

out:
	ds_chk_free_pool_list(clp, clp_nr);

	if (rc != 0)
		D_ERROR(DF_LEADER" failed to handle pools list: "DF_RC"\n",
			DP_LEADER(ins), DP_RC(rc));

	return rc;
}

static int
chk_leader_pool_mbs_one(struct chk_pool_rec *cpr)
{
	struct rsvc_client	 client = { 0 };
	crt_endpoint_t		 ep = { 0 };
	struct rsvc_hint	 svc_hint = { 0 };
	struct chk_instance	*ins = cpr->cpr_ins;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	d_rank_list_t		*ps_ranks = NULL;
	struct chk_pool_shard	*cps;
	struct ds_pool_clue	*clue;
	uint32_t		 interval;
	int			 svc_rc = 0;
	int			 rc = 0;
	int			 rc1;
	int			 i = 0;
	bool			 notify = true;

	D_ASSERT(cpr->cpr_mbs == NULL);

	D_ALLOC_ARRAY(cpr->cpr_mbs, cpr->cpr_shard_nr);
	if (cpr->cpr_mbs == NULL)
		D_GOTO(out_post, rc = -DER_NOMEM);

	d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
		clue = cps->cps_data;
		D_ASSERT(i < cpr->cpr_shard_nr);

		cpr->cpr_mbs[i].cpm_rank = cps->cps_rank;
		cpr->cpr_mbs[i].cpm_tgt_nr = clue->pc_tgt_nr;
		/*
		 * NOTE: Do not allocate space for cpm_tgt_status, instead, we can directly
		 *	 use clue->pc_tgt_status, that will be freed when free the pool rec
		 *	 via chk_pool_put()->cps_free_cb().
		 */
		cpr->cpr_mbs[i].cpm_tgt_status = clue->pc_tgt_status;
		i++;
	}

	ps_ranks = chk_leader_cpr2ranklist(cpr, true);
	if (ps_ranks == NULL)
		D_GOTO(out_post, rc = -DER_NOMEM);

	/*
	 * The PS leader election needs some time, we do not need to retry chk_pool_mbs_remote()
	 * too frequently. Here, for each PS leader candidate, we will try once per second. Then
	 * if some one is elected as the PS leader, we will find it about one second later.
	 */
	interval = 1000 / ps_ranks->rl_nr;

	rc = rsvc_client_init(&client, ps_ranks);
	d_rank_list_free(ps_ranks);

	if (rc != 0)
		goto out_post;

again:
	rc = rsvc_client_choose(&client, &ep);
	if (rc != 0)
		goto out_client;

	rc = chk_pool_mbs_remote(ep.ep_rank, CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS, cbk->cb_gen,
				 cpr->cpr_uuid, cpr->cpr_label, cpr->cpr_label_seq,
				 cpr->cpr_delay_label ? CMF_REPAIR_LABEL : 0,
				 cpr->cpr_shard_nr, cpr->cpr_mbs, &svc_rc, &svc_hint);

	rc1 = rsvc_client_complete_rpc(&client, &ep, rc, svc_rc, &svc_hint);
	if (rc1 == RSVC_CLIENT_RECHOOSE ||
	    (rc1 == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(rc))) {
		dss_sleep(interval);
		if (cpr->cpr_stop || !ins->ci_sched_running || ins->ci_sched_exiting) {
			notify = false;
			D_GOTO(out_client, rc = 0);
		}
		goto again;
	}

out_client:
	rsvc_client_fini(&client);

out_post:
	if (rc != 0) {
		cpr->cpr_skip = 1;
		if (rc == -DER_SHUTDOWN || rc == -DER_NONEXIST)
			rc = 0;

		chk_leader_post_repair(ins, cpr, &rc, false, notify);
	}

	return rc;
}

static void
chk_leader_pool_ult(void *arg)
{
	struct chk_pool_rec	*cpr = arg;
	struct chk_instance	*ins = cpr->cpr_ins;
	struct chk_bookmark	*cbk = &cpr->cpr_bk;
	d_rank_list_t		*ranks;
	struct ds_pool_clue	*clue;
	struct chk_iv		 iv = { 0 };
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	uint32_t		 flags = 0;
	int			 rc = 0;

	D_INFO(DF_LEADER" pool ult enter for "DF_UUIDF"\n", DP_LEADER(ins), DP_UUID(cpr->cpr_uuid));

	uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
	if (chk_leader_pool_need_stop(cpr, &rc))
		goto out;

	/*
	 * NOTE: We need build clues even if we are resuming from former stop/pause phase.
	 *	 That is the base for subsequent pool start and pool MBS. But it does not
	 *	 mean we will re-execute all the checking from the scratch. Because if we
	 *	 have passed some phases in former instance, then PS quorum related issues
	 *	 have already been resolved at that time. Then for current check instance,
	 *	 related check be skipped or become noop.
	 */

	rc = chk_leader_handle_pool_clues(cpr);
	if (chk_leader_pool_need_stop(cpr, &rc))
		goto out;

	if (cbk->cb_phase > CHK__CHECK_SCAN_PHASE__CSP_PREPARE)
		goto start;

	if (!cpr->cpr_exist_on_ms) {
		rc = chk_leader_orphan_pool(cpr);
		if (chk_leader_pool_need_stop(cpr, &rc))
			goto out;
	} else {
		clue = cpr->cpr_clue;
		if ((clue->pc_label != NULL && cpr->cpr_label == NULL) ||
		    (clue->pc_label == NULL && cpr->cpr_label != NULL) ||
		    (clue->pc_label != NULL && cpr->cpr_label != NULL &&
		     strcmp(clue->pc_label, cpr->cpr_label) != 0)) {
			rc = chk_leader_handle_pool_label(cpr, clue);
			if (chk_leader_pool_need_stop(cpr, &rc))
				goto out;
		}
	}

	if (cbk->cb_phase < CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST) {
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST;
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0) {
			cpr->cpr_skip = 1;
			goto out;
		}

		if (DAOS_FAIL_CHECK(DAOS_CHK_LEADER_BLOCK)) {
			while (!(chk_leader_pool_need_stop(cpr, &rc)))
				dss_sleep(300);
			goto exit;
		}
	}

start:
	ranks = chk_leader_cpr2ranklist(cpr, false);
	if (ranks == NULL) {
		cpr->cpr_skip = 1;
		D_GOTO(out, rc = -DER_NOMEM);
	}

	if (cpr->cpr_for_orphan)
		flags |= CPSF_FOR_ORPHAN;
	if (cpr->cpr_not_export_ps)
		flags |= CPSF_NOT_EXPORT_PS;

	/*
	 * Notify all related pool shards to start the pool. Piggyback the
	 * phase CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST to the pool shards.
	 */
	rc = chk_pool_start_remote(ranks, cbk->cb_gen, cpr->cpr_uuid,
				   CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST, flags);
	d_rank_list_free(ranks);
	if (rc != 0)
		cpr->cpr_skip = 1;

	if (rc == -DER_SHUTDOWN || rc == -DER_NONEXIST)
		goto exit;

	if (chk_leader_pool_need_stop(cpr, &rc))
		goto out;

	rc = chk_leader_start_pool_svc(cpr);
	if (chk_leader_pool_need_stop(cpr, &rc))
		goto out;

	if (cbk->cb_phase < CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS) {
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS;
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0) {
			cpr->cpr_skip = 1;
			goto out;
		}
	}

	/*
	 * Notify the PS leader to drive the subsequent pool scan. Piggyback the
	 * phase CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS to related PS leader.
	 * The PS leader will handle subsequent pool scan phases.
	 */
	rc = chk_leader_pool_mbs_one(cpr);

out:
	/* For stop case, the pool status will be updated via chk_pool_stop_one() by the sponsor. */
	if ((rc < 0 || cpr->cpr_skip) && !cpr->cpr_notified_exit && !cpr->cpr_stop) {
		iv.ci_gen = cbk->cb_gen;
		uuid_copy(iv.ci_uuid, cpr->cpr_uuid);
		iv.ci_phase = cbk->cb_phase;
		iv.ci_pool_status = CHK__CHECK_POOL_STATUS__CPS_FAILED;

		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
				   CRT_IV_SYNC_EAGER, true);
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_LEADER" notify engines to exit check for pool "DF_UUIDF" failure: %d\n",
			 DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), rc);
		if (rc == 0)
			cpr->cpr_notified_exit = 1;
	}

exit:
	D_INFO(DF_LEADER" pool ult exit for "DF_UUIDF": rc = %d\n",
	       DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), rc);

	if (cpr->cpr_skip)
		cpr->cpr_done = 1;
	chk_pool_put(cpr);
}

static void
chk_leader_mark_rank_dead(struct chk_instance *ins, struct chk_dead_rank *cdr)
{
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	uint32_t		 version = cbk->cb_gen - prop->cp_rank_nr - 1;
	int			 rc = 0;

	if (!chk_remove_rank_from_list(ins->ci_ranks, cdr->cdr_rank))
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	prop->cp_rank_nr--;
	rc = chk_prop_update(prop, ins->ci_ranks);
	if (rc != 0)
		goto out;

	rc = crt_group_secondary_modify(ins->ci_iv_group, ins->ci_ranks, ins->ci_ranks,
					CRT_GROUP_MOD_OP_REPLACE, version);
	if (rc != 0)
		goto out;

	rc = chk_rank_del(ins, cdr->cdr_rank);
	if (rc != 0)
		goto out;

	/*
	 * NOTE: Some thought about removing related shards from the ins->ci_pool_list,
	 *	 that may reduce the possibility of CR failure caused by the dead rank.
	 *	 But consider the rank death event is totally random, we cannot make it
	 *	 to be transparent to user. For example, the dead rank maybe the unique
	 *	 replica of some pool service, that will cause related PS failure after
	 *	 its death.
	 *
	 *	 On the other hand, if we modify the pool shards list for related pools,
	 *	 then it may hide data corruption silently. It may be different from the
	 *	 user expectation.
	 *
	 *	 So here, we do not try to hide the rank death event. If subsequent CR
	 *	 processing failed because of the dead rank, just report it.
	 *
	 */

	if (!d_list_empty(&ins->ci_rank_list))
		rc = chk_mark_remote(ins->ci_ranks, cbk->cb_gen, cdr->cdr_rank, version);

out:
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_LEADER" mark rank %u as dead with version %u: "DF_RC"\n",
			 DP_LEADER(ins), cdr->cdr_rank, version, DP_RC(rc));
	D_FREE(cdr);
}

static void
chk_leader_sched(void *args)
{
	struct chk_instance	*ins = args;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_dead_rank	*cdr;
	struct chk_iv		 iv = { 0 };
	uint32_t		 ins_phase;
	uint32_t		 ins_status;
	uint32_t		 pool_status;
	int			 done = 0;
	int			 rc = 0;
	bool			 bcast = false;
	bool			 more_dead;

	D_INFO(DF_LEADER" scheduler enter at phase %u\n", DP_LEADER(ins), cbk->cb_phase);

	ABT_mutex_lock(ins->ci_abt_mutex);

again:
	if (ins->ci_sched_exiting) {
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
	if (!d_list_empty(&ins->ci_pool_list) || ins->ci_start_flags & CSF_ORPHAN_POOL) {
		rc = chk_leader_handle_pools_list(ins);
		if (rc != 0)
			D_GOTO(out, bcast = true);
	}

	while (1) {
		dss_sleep(300);

check_dead:
		ABT_mutex_lock(ins->ci_abt_mutex);
		if (!d_list_empty(&ins->ci_dead_ranks)) {
			cdr = d_list_pop_entry(&ins->ci_dead_ranks, struct chk_dead_rank, cdr_link);
			if (!d_list_empty(&ins->ci_dead_ranks))
				more_dead = true;
			else
				more_dead = false;
		} else {
			cdr = NULL;
			more_dead = false;
		}
		ABT_mutex_unlock(ins->ci_abt_mutex);

		if (cdr != NULL)
			chk_leader_mark_rank_dead(ins, cdr);

		if (chk_leader_need_stop(ins, &rc))
			D_GOTO(out, bcast = (rc > 0 ? true : false));

		if (more_dead)
			goto check_dead;

		/*
		 * TBD: The leader may need to detect engines' status/phase actively, otherwise
		 *	if some engine failed to notify the leader for its status/phase changes,
		 *	then the leader will be blocked there.
		 */

		ins_phase = chk_pools_find_slowest(ins, &done);

		if (ins_phase >= CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS && !ins->ci_orphan_done &&
		    !DAOS_FAIL_CHECK(DAOS_CHK_SYNC_ORPHAN_PROCESS)) {
			iv.ci_gen = cbk->cb_gen;
			iv.ci_phase = ins_phase;
			iv.ci_ins_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;

			/* Synchronously notify engines that orphan pools have been processed. */
			rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
					   CRT_IV_SYNC_EAGER, true);
			D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
				 DF_LEADER" notify engines that orphan pools have been process: %d\n",
				 DP_LEADER(ins), rc);
			if (rc == 0)
				ins->ci_orphan_done = 1;
		}

		/*
		 * Check @done before update cb_phase. Otherwise, the cb_phase may has become 'DONE'
		 * but cb_ins_status is still 'RUNNING'.
		 */
		if (done != 0) {
			if (done > 0) {
				D_INFO(DF_LEADER" has done\n", DP_LEADER(ins));
				rc = 1;
			} else {
				D_INFO(DF_LEADER" is stopped\n", DP_LEADER(ins));
				rc = 0;
			}

			D_GOTO(out, rc);
		}

		if (cbk->cb_phase == CHK_INVAL_PHASE || cbk->cb_phase < ins_phase) {
			D_INFO(DF_LEADER" moves from phase %u to phase %u\n",
			       DP_LEADER(ins), cbk->cb_phase, ins_phase);

			cbk->cb_phase = ins_phase;
			/* QUEST: How to estimate the left time? */
			cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE - cbk->cb_phase;
			rc = chk_bk_update_leader(cbk);
			if (rc != 0)
				D_GOTO(out, bcast = true);
		}
	}

out:
	ins_phase = CHK_INVAL_PHASE;
	if (rc > 0) {
		/*
		 * If some engine(s) failed during the start, then mark the instance as 'failed'.
		 * It means that there is at least one failure during the DAOS check at somewhere.
		 * pool_status is useless under this case since all pools have done.
		 */
		if (ins->ci_slowest_fail_phase != CHK_INVAL_PHASE &&
		    ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT) {
			ins_phase = ins->ci_slowest_fail_phase;
			ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
			pool_status = CHK__CHECK_POOL_STATUS__CPS_IMPLICATED;
		} else {
			ins_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
			ins_status = CHK__CHECK_INST_STATUS__CIS_COMPLETED;
			pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;
		}
	} else if (rc == 0) {
		if (ins->ci_implicated) {
			ins_status = CHK__CHECK_INST_STATUS__CIS_IMPLICATED;
			pool_status = CHK__CHECK_POOL_STATUS__CPS_IMPLICATED;
		} else if (ins->ci_stopping) {
			ins_status = CHK__CHECK_INST_STATUS__CIS_STOPPED;
			pool_status = CHK__CHECK_POOL_STATUS__CPS_STOPPED;
		} else {
			ins_status = CHK__CHECK_INST_STATUS__CIS_PAUSED;
			pool_status = CHK__CHECK_POOL_STATUS__CPS_PAUSED;
		}
	} else {
		ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		pool_status = CHK__CHECK_POOL_STATUS__CPS_IMPLICATED;
	}

	chk_leader_exit(ins, ins_phase, ins_status, pool_status, bcast);

	D_INFO(DF_LEADER" scheduler exit at phase %u with status %u: rc %d\n",
	       DP_LEADER(ins), cbk->cb_phase, ins_status, rc);

	ins->ci_sched_running = 0;
}

static int
chk_leader_pools2list(struct chk_instance *ins, int *pool_nr, uuid_t **p_pools)
{
	struct chk_pool_rec	*cpr;
	uuid_t			*pools;
	uuid_t			*tmp;
	int			 cap = 4;
	int			 idx = 0;
	int			 rc = 0;

	D_ALLOC_ARRAY(pools, cap);
	if (pools == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
		if (cpr->cpr_skip || cpr->cpr_done || cpr->cpr_for_orphan)
			continue;

		if (idx >= cap) {
			D_REALLOC_ARRAY(tmp, pools, cap, cap << 1);
			if (tmp == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			pools = tmp;
			cap <<= 1;
		}

		uuid_copy(pools[idx++], cpr->cpr_uuid);
	}

out:
	if (rc == 0) {
		*p_pools = pools;
		*pool_nr = idx;
	} else {
		D_FREE(pools);
	}

	return rc;
}

int
chk_leader_ranks_prepare(struct chk_instance *ins, uint32_t rank_nr, d_rank_t *ranks,
			 d_rank_list_t **p_ranks)
{
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_property	*prop = &ins->ci_prop;
	d_rank_list_t		*rank_list = NULL;
	int			 rc = 0;

	rank_list = uint32_array_to_rank_list(ranks, rank_nr);
	if (rank_list == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	d_rank_list_sort(rank_list);

	/* Corrupted bookmark or new created one. Nothing can be reused. */
	if ((ins->ci_is_leader && cbk->cb_magic != CHK_BK_MAGIC_LEADER) ||
	    (!ins->ci_is_leader && cbk->cb_magic != CHK_BK_MAGIC_ENGINE)) {
		memset(prop, 0, sizeof(*prop));
		D_GOTO(out, rc = 1);
	}

	/* Reload former ranks if necessary. */
	if (ins->ci_ranks == NULL) {
		rc = chk_prop_fetch(prop, &ins->ci_ranks);
		if (rc != 0 && rc != -DER_NONEXIST)
			goto out;
	}

	/* New system or add new rank(s), need global reset. */
	if (ins->ci_ranks == NULL)
		D_GOTO(out, rc = 1);

	/* Change rank list must be handled as 'reset' globally. */
	if (rank_nr != ins->ci_ranks->rl_nr ||
	    memcmp(ins->ci_ranks->rl_ranks, rank_list->rl_ranks, sizeof(d_rank_t) * rank_nr) != 0) {
		D_WARN("Use new rank list, reset the check globally\n");
		D_GOTO(out, rc = 1);
	}

out:
	if (rc > 0)
		*p_ranks = rank_list;
	else
		d_rank_list_free(rank_list);

	return rc;
}

static int
chk_leader_start_prep(struct chk_instance *ins, uint32_t rank_nr, d_rank_t *ranks,
		      uint32_t policy_nr, struct chk_policy *policies, int pool_nr,
		      uuid_t pools[], int phase, d_rank_t leader, uint32_t flags)
{
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_rank_bundle		 rbund = { 0 };
	d_iov_t				 riov;
	d_iov_t				 kiov;
	d_rank_list_t			*rank_list = NULL;
	uint64_t			 gen;
	uint32_t			 cbk_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
	int				 rc = 0;
	int				 i;

	if (rank_nr == 0) {
		D_ERROR("Rank list cannot be NULL for check start\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (flags & CHK__CHECK_FLAG__CF_FAILOUT && flags & CHK__CHECK_FLAG__CF_NO_FAILOUT) {
		D_ERROR("failout and non_failout cannot be specified together\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (flags & CHK__CHECK_FLAG__CF_AUTO && flags & CHK__CHECK_FLAG__CF_NO_AUTO) {
		D_ERROR("auto and non_auto cannot be specified together\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* New generation for each instance. */
	gen = d_hlc_get();

	rc = chk_leader_ranks_prepare(ins, rank_nr, ranks, &rank_list);
	if (rc > 0)
		goto reset;
	if (rc < 0)
		goto out;

	if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN)
		ins->ci_start_flags |= CSF_RESET_NONCOMP;

	/*
	 * If "CHK__CHECK_FLAG__CF_RESET" is specified, then restart check from the
	 * scratch for the given pools (pool_nr > 0) or for al pools (pool_nr == 0).
	 */

	if (pool_nr > 0) {
		rc = chk_pools_load_list(ins, gen, flags, pool_nr, pools, &cbk_phase);
		if (rc != 0)
			goto out;
	} else {
		if (flags & CHK__CHECK_FLAG__CF_RESET)
			goto reset;

		ctpa.ctpa_ins = ins;
		ctpa.ctpa_gen = gen;
		ctpa.ctpa_phase = cbk_phase;
		rc = chk_traverse_pools(chk_pools_load_from_db, &ctpa);
		if (rc != 0)
			goto out;

		cbk_phase = ctpa.ctpa_phase;
	}

	if (d_list_empty(&ins->ci_pool_list) && !(flags & CHK__CHECK_FLAG__CF_ORPHAN_POOL))
		D_GOTO(out, rc = 1);

	goto init;

reset:
	ins->ci_start_flags = CSF_RESET_ALL;
	if (pool_nr <= 0)
		ins->ci_start_flags |= CSF_ORPHAN_POOL;

	rc = chk_traverse_pools(chk_pools_cleanup_cb, NULL);
	if (rc != 0)
		goto out;

	memset(cbk, 0, sizeof(*cbk));
	cbk->cb_magic = CHK_BK_MAGIC_LEADER;
	cbk->cb_version = DAOS_CHK_VERSION;

init:
	rc = chk_prop_prepare(leader, flags, phase, policy_nr, policies, rank_list, prop);
	if (rc != 0)
		goto out;

	if (rank_list != NULL) {
		d_rank_list_free(ins->ci_ranks);
		ins->ci_ranks = rank_list;
		rank_list = NULL;
	}

	cbk->cb_gen = gen;
	if (flags & CHK__CHECK_FLAG__CF_RESET && !(ins->ci_start_flags & CSF_RESET_ALL)) {
		memset(&cbk->cb_statistics, 0, sizeof(cbk->cb_statistics));
		memset(&cbk->cb_time, 0, sizeof(cbk->cb_time));
	}

	ins->ci_slowest_fail_phase = CHK_INVAL_PHASE;
	if (flags & CHK__CHECK_FLAG__CF_ORPHAN_POOL)
		ins->ci_start_flags |= CSF_ORPHAN_POOL;

	/* The leader bookmark will be stored via chk_leader_start_post() later. */
	if (cbk->cb_phase > cbk_phase)
		cbk->cb_phase = cbk_phase;

	/* Prepare ranks tree. */
	for (i = 0; i < ins->ci_ranks->rl_nr; i++) {
		rbund.crb_rank = ins->ci_ranks->rl_ranks[i];
		/*
		 * The phase for the rank may be not accurate, that is not important as long as it
		 * is not 'DONE'. If it is DONE, it will be refreshed via chk_leader_start_post().
		 */
		rbund.crb_phase = cbk->cb_phase;
		rbund.crb_ins = ins;

		d_iov_set(&riov, &rbund, sizeof(rbund));
		d_iov_set(&kiov, &ins->ci_ranks->rl_ranks[i], sizeof(d_rank_t));
		rc = dbtree_upsert(ins->ci_rank_hdl, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
				   &kiov, &riov, NULL);
		if (rc != 0)
			break;
	}

out:
	d_rank_list_free(rank_list);
	if (rc < 0) {
		/* Reset ci_ranks if hit failure, then we can reload when use it next time. */
		d_rank_list_free(ins->ci_ranks);
		ins->ci_ranks = NULL;
	}

	return rc;
}

static int
chk_leader_start_post(struct chk_instance *ins)
{
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	struct chk_pool_shard	*cps;
	struct ds_pool_clue	*clue;
	struct chk_rank_rec	*crr;
	struct chk_iv		 iv = { 0 };
	struct chk_bookmark	*ins_cbk = &ins->ci_bk;
	struct chk_bookmark	*pool_cbk;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	uint32_t		 ins_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
	uint32_t		 pool_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
	int			 rc = 0;

	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
		pool_cbk = &cpr->cpr_bk;

		if (pool_cbk->cb_magic != CHK_BK_MAGIC_POOL) {
			memset(pool_cbk, 0, sizeof(*pool_cbk));
			pool_cbk->cb_magic = CHK_BK_MAGIC_POOL;
			pool_cbk->cb_gen = ins_cbk->cb_gen;
			pool_cbk->cb_version = DAOS_CHK_VERSION;
		}

		/*
		 * No engine report shard for the pool, it is dangling pool,
		 * keep it for subsequent dangling pool logic to handle it.
		 */
		if (d_list_empty(&cpr->cpr_shard_list))
			continue;

		d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
			clue = cps->cps_data;
			/*
			 * The pool is only used to handle orphan pool, mark as 'for_orphan',
			 * then only orphan pool logic will handle it, others will skip it.
			 */
			if (clue->pc_phase == CHK_INVAL_PHASE) {
				cpr->cpr_for_orphan = 1;
				goto next;
			}

			if (pool_phase > clue->pc_phase)
				pool_phase = clue->pc_phase;
		}

		if (pool_cbk->cb_phase <= CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS ||
		    pool_phase <= CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS)
			pool_phase = pool_cbk->cb_phase;
		else
			pool_cbk->cb_phase = pool_phase;

		if (likely(pool_phase != CHK__CHECK_SCAN_PHASE__CSP_DONE)) {
			pool_cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
			/* Always refresh the start time. */
			pool_cbk->cb_time.ct_start_time = time(NULL);
			/* QUEST: How to estimate the left time? */
			pool_cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE -
							 pool_cbk->cb_phase;
		} else {
			pool_cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;
			pool_cbk->cb_time.ct_stop_time = time(NULL);
			cpr->cpr_done = 1;
		}

		uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
		rc = chk_bk_update_pool(pool_cbk, uuid_str);
		if (rc != 0)
			break;

		/*
		 * NOTE: The pool has been checked, notify the engines to drop their cpr.
		 *	 But keek the leader's cpr for handling dangling pool.
		 */
		if (unlikely(pool_phase == CHK__CHECK_SCAN_PHASE__CSP_DONE)) {
			iv.ci_gen = ins_cbk->cb_gen;
			uuid_copy(iv.ci_uuid, cpr->cpr_uuid);
			iv.ci_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
			iv.ci_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;

			/*
			 * Synchronously notify the engines that check on the pool is done.
			 * The check leader is the last one to know that. So even if failed
			 * to notify the engine for the check done, that is not fatal. That
			 * can be redo in next check instance.
			 */
			rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
					   CRT_IV_SYNC_EAGER, true);
			D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
				 DF_LEADER" notify engines the pool "DF_UUIDF" is checked: %d\n",
				 DP_LEADER(ins), DP_UUID(cpr->cpr_uuid), rc);
		} else if (ins_phase > pool_phase) {
			ins_phase = pool_phase;
		}

next:
		pool_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
	}

	if (rc == 0) {
		/*
		 * The phase in leader bookmark may be larger than the phase in
		 * some pools that may be new added into current check instance.
		 * So we allow the phase to backward.
		 */
		if (ins_cbk->cb_phase != ins_phase) {
			ins_cbk->cb_phase = ins_phase;
			d_list_for_each_entry(crr, &ins->ci_rank_list, crr_link)
				crr->crr_phase = ins_phase;
		}

		if (likely(ins_phase != CHK__CHECK_SCAN_PHASE__CSP_DONE) ||
		    ins->ci_start_flags & CSF_ORPHAN_POOL) {
			if (ins_phase == CHK__CHECK_SCAN_PHASE__CSP_DONE)
				ins->ci_for_orphan = 1;
			ins_cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;
			/* Always refresh the start time. */
			ins_cbk->cb_time.ct_start_time = time(NULL);
			/* QUEST: How to estimate the left time? */
			ins_cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE -
							ins_cbk->cb_phase;
		} else {
			ins_cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_COMPLETED;
			ins_cbk->cb_time.ct_stop_time = time(NULL);
		}

		rc = chk_bk_update_leader(ins_cbk);
		/*
		 * NOTE: If the check list is not empty or the user specifies 'orphan' option,
		 *	 then still need to further check for dangling/orphan pools. Otherwise
		 *	 we can return 1 to directly exit the check.
		 */
		if (rc == 0 && ins_phase == CHK__CHECK_SCAN_PHASE__CSP_DONE) {
			if (ins->ci_for_orphan)
				/*
				 * Mark instance bookmark as 'CSP_PREPARED' in DRAM to avoid
				 * confused 'CSP_DONE' but with check RUNNING for check query.
				 */
				ins_cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
			else if (d_list_empty(&ins->ci_pool_list))
				rc = 1;
		}
	}

	return rc;
}

static int
chk_leader_dup_clue(struct ds_pool_clue **tgt, struct ds_pool_clue *src)
{
	struct ds_pool_clue		*clue = NULL;
	struct ds_pool_svc_clue		*svc = NULL;
	uint32_t			*status = NULL;
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

	rc = chk_dup_string(&label, src->pc_label, src->pc_label_len);
	if (rc != 0)
		goto out;

	if (src->pc_tgt_status != NULL) {
		D_ALLOC_ARRAY(status, src->pc_tgt_nr);
		if (status == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		memcpy(status, src->pc_tgt_status, sizeof(*status) * src->pc_tgt_nr);
	}

	memcpy(clue, src, sizeof(*clue));
	clue->pc_svc_clue = svc;
	clue->pc_label = label;
	clue->pc_tgt_status = status;

out:
	if (rc != 0) {
		if (svc != NULL) {
			d_rank_list_free(svc->psc_db_clue.bcl_replicas);
			D_FREE(svc);
		}

		D_FREE(status);
		D_FREE(label);
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
chk_leader_start_cb(struct chk_co_rpc_cb_args *cb_args)
{
	struct chk_instance	*ins = cb_args->cb_priv;
	struct ds_pool_clue	*clues = cb_args->cb_data;
	struct ds_pool_clue	*clue;
	int			 rc = 0;
	int			 i;

	D_ASSERTF(cb_args->cb_result >= 0, "Unexpected result for start CB %d\n",
		  cb_args->cb_result);

	/* The engine has completed the check, remove it from the rank list. */
	if (cb_args->cb_result > 0) {
		rc = chk_rank_del(ins, cb_args->cb_rank);
		goto out;
	}

	for (i = 0; i < cb_args->cb_nr; i++) {
		/*
		 * @clues is from chk_start_remote RPC reply, the buffer will be released after
		 * the RPC done. Let's copy all related data to new the buffer for further using.
		 */
		rc = chk_leader_dup_clue(&clue, &clues[i]);
		if (rc != 0)
			goto out;

		rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, clue->pc_uuid,
					clue->pc_rank, NULL, ins, NULL, clue,
					chk_leader_free_clue, NULL);
		if (rc != 0) {
			chk_leader_free_clue(clue);
			goto out;
		}
	}

out:
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to handle start CB with rank %u, result %d: "
			DF_RC"\n", DP_LEADER(ins), cb_args->cb_rank, cb_args->cb_result, DP_RC(rc));

	return rc;
}

int
chk_leader_start(uint32_t rank_nr, d_rank_t *ranks, uint32_t policy_nr, struct chk_policy *policies,
		 int pool_nr, uuid_t pools[], uint32_t api_flags, int phase)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	uuid_t			*c_pools = NULL;
	struct umem_attr	 uma = { 0 };
	uuid_t			 dummy_pool = { 0 };
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	d_rank_t		 myrank = dss_self_rank();
	uint32_t		 flags = api_flags;
	int			 c_pool_nr = 0;
	int			 rc;
	int			 rc1;

	rc = chk_ins_can_start(ins);
	if (rc != 0)
		goto out_log;

	ins->ci_starting = 1;
	ins->ci_started = 0;
	ins->ci_start_flags = 0;
	ins->ci_for_orphan = 0;
	ins->ci_orphan_done = 0;
	ins->ci_implicated = 0;
	ins->ci_pool_stopped = 0;

	D_ASSERT(daos_handle_is_inval(ins->ci_rank_hdl));
	D_ASSERT(d_list_empty(&ins->ci_rank_list));

	D_ASSERT(daos_handle_is_inval(ins->ci_pool_hdl));
	D_ASSERT(d_list_empty(&ins->ci_pool_list));

	D_ASSERT(daos_handle_is_inval(ins->ci_pending_hdl));

	if (ins->ci_sched != ABT_THREAD_NULL)
		ABT_thread_free(&ins->ci_sched);

	chk_iv_ns_cleanup(&ins->ci_iv_ns);

	if (ins->ci_iv_group != NULL) {
		crt_group_secondary_destroy(ins->ci_iv_group);
		ins->ci_iv_group = NULL;
	}

	uma.uma_id = UMEM_CLASS_VMEM;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_RANK, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_rank_btr, &ins->ci_rank_hdl);
	if (rc != 0)
		goto out_tree;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pool_btr, &ins->ci_pool_hdl);
	if (rc != 0)
		goto out_tree;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_PA, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pending_btr, &ins->ci_pending_hdl);
	if (rc != 0)
		goto out_tree;

reset:
	rc = chk_leader_start_prep(ins, rank_nr, ranks, policy_nr, policies, pool_nr, pools,
				   phase, myrank, flags);
	if (rc == 1 && !(flags & CHK__CHECK_FLAG__CF_RESET)) {
		/* Former check instance has done, let's re-start from the beginning. */
		flags |= CHK__CHECK_FLAG__CF_RESET;
		goto reset;
	}

	if (rc != 0)
		goto out_tree;

	if (ins->ci_iv_group != NULL)
		goto remote;

	uuid_generate(dummy_pool);
	uuid_unparse_lower(dummy_pool, uuid_str);
	rc = crt_group_secondary_create(uuid_str, NULL, ins->ci_ranks, &ins->ci_iv_group);
	if (rc != 0)
		goto out_tree;

	rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, dummy_pool, ins->ci_iv_group,
			     &ins->ci_iv_id, &ins->ci_iv_ns);
	if (rc != 0)
		goto out_group;

	ds_iv_ns_update(ins->ci_iv_ns, myrank, ins->ci_iv_ns->iv_master_term + 1);

	if (d_list_empty(&ins->ci_pool_list)) {
		c_pool_nr = pool_nr;
		c_pools = pools;
	} else {
		rc = chk_leader_pools2list(ins, &c_pool_nr, &c_pools);
		if (rc != 0)
			goto out_iv;
	}

remote:
	rc = chk_start_remote(ins->ci_ranks, cbk->cb_gen, rank_nr, ranks, policy_nr, policies,
			      c_pool_nr, c_pools, flags, phase, myrank, ins->ci_start_flags,
			      dummy_pool, chk_leader_start_cb, ins);
	if (rc != 0) {
		if (rc == -DER_OOG || rc == -DER_GRPVER || rc == -DER_AGAIN) {
			D_INFO(DF_LEADER" Someone is not ready %d, let's retry start after 1 sec\n",
			       DP_LEADER(ins), rc);
			if (!d_list_empty(&ins->ci_pool_list))
				chk_pool_shard_cleanup(ins);
			dss_sleep(1000);
			goto remote;
		}

		goto out_iv;
	}

	uuid_copy(cbk->cb_iv_uuid, dummy_pool);
	rc = chk_leader_start_post(ins);
	if (rc == 1 && !(flags & CHK__CHECK_FLAG__CF_RESET)) {
		rc = chk_stop_remote(ins->ci_ranks, cbk->cb_gen, c_pool_nr, c_pools, NULL, NULL);
		if (rc < 0) {
			D_WARN(DF_LEADER" failed to rollback former start before reset: "DF_RC"\n",
			       DP_LEADER(ins), DP_RC(rc));
			goto out_iv;
		}

		/* Former check instance has done, let's re-start from the beginning. */
		flags |= CHK__CHECK_FLAG__CF_RESET;
		goto reset;
	}

	if (rc != 0)
		goto out_stop_remote;

	ins->ci_sched_running = 1;

	rc = dss_ult_create(chk_leader_sched, ins, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &ins->ci_sched);
	if (rc != 0) {
		ins->ci_sched_running = 0;
		goto out_stop_pools;
	}

	D_INFO("Leader %s check with api_flags %x, phase %d, leader %u, flags %x, gen " DF_X64
	       " iv "DF_UUIDF": rc %d\n",
	       chk_is_ins_reset(ins, flags) ? "start" : "resume", api_flags, phase, myrank,
	       ins->ci_start_flags, cbk->cb_gen, DP_UUID(dummy_pool), rc);

	chk_ranks_dump(ins->ci_ranks->rl_nr, ins->ci_ranks->rl_ranks);
	chk_pools_dump(&ins->ci_pool_list, c_pool_nr > 0 ? c_pool_nr : pool_nr,
		       c_pool_nr > 0 ? c_pools : pools);

	ABT_mutex_lock(ins->ci_abt_mutex);
	ins->ci_started = 1;
	ABT_cond_broadcast(ins->ci_abt_cond);
	ABT_mutex_unlock(ins->ci_abt_mutex);

	ins->ci_starting = 0;

	goto out_exit;

out_stop_pools:
	chk_pool_stop_all(ins, CHK__CHECK_POOL_STATUS__CPS_IMPLICATED, NULL);
	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_time.ct_stop_time = time(NULL);
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		rc1 = chk_bk_update_leader(cbk);
		if (rc1 != 0)
			D_WARN(DF_LEADER" failed to update leader bookmark: "DF_RC"\n",
			       DP_LEADER(ins), DP_RC(rc1));
	}
out_stop_remote:
	rc1 = chk_stop_remote(ins->ci_ranks, cbk->cb_gen, c_pool_nr, c_pools, NULL, NULL);
	if (rc1 < 0)
		D_WARN(DF_LEADER" failed to rollback failed check start: "DF_RC"\n",
		       DP_LEADER(ins), DP_RC(rc1));
out_iv:
	chk_iv_ns_cleanup(&ins->ci_iv_ns);
out_group:
	crt_group_secondary_destroy(ins->ci_iv_group);
	ins->ci_iv_group = NULL;
out_tree:
	chk_leader_destroy_trees(ins);
	ins->ci_starting = 0;
out_log:
	D_CDEBUG(likely(rc < 0), DLOG_ERR, DLOG_INFO,
		 "Leader %s to start check on %u ranks for %d pools with "
		 "api_flags %x, phase %d, leader %u, gen "DF_X64": rc = %d\n",
		 rc < 0 ? "failed" : "try", rank_nr, pool_nr, api_flags, phase,
		 myrank, cbk->cb_gen, rc);

	if (unlikely(rc > 0))
		rc = 0;
out_exit:
	/* Notify the control plane that the check (re-)starts from the scratch. */
	if (rc == 0 && chk_is_ins_reset(ins, flags))
		rc = 1;

	if (c_pools != NULL && c_pools != pools)
		D_FREE(c_pools);

	return rc;
}

static int
chk_leader_stop_cb(struct chk_co_rpc_cb_args *cb_args)
{
	struct chk_instance	*ins = cb_args->cb_priv;
	int			 rc;

	D_ASSERTF(cb_args->cb_result > 0, "Unexpected result for stop CB %d\n", cb_args->cb_result);

	if (cb_args->cb_flags & CSF_POOL_STOPPED)
		ins->ci_pool_stopped = 1;

	/* The engine has stop on the rank, remove it from the rank list. */
	rc = chk_rank_del(ins, cb_args->cb_rank);
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to handle stop CB with rank %u: "DF_RC"\n",
			DP_LEADER(ins), cb_args->cb_rank, DP_RC(rc));

	return rc;
}

int
chk_leader_stop(int pool_nr, uuid_t pools[])
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	int			 rc = 0;
	int			 i;

	if (ins->ci_starting)
		D_GOTO(log, rc = -DER_BUSY);

	if (ins->ci_stopping || ins->ci_sched_exiting)
		D_GOTO(log, rc = -DER_INPROGRESS);

	/*
	 * NOTE: It is possible that the check leader is dead. If we want to stop the stale
	 *	 check instance on other engine, then we may execute the CHK_STOP on new
	 *	 check leader. But if the old leader is still active, and if the CHK_STOP
	 *	 dRPC is sent to non-leader (or new leader), then it will cause trouble.
	 *
	 *	 Here, it is not easy to know whether the old leader is still valid or not.
	 *	 We have to trust control plane. It is the control plane duty to guarantee
	 *	 that the CHK_STOP dRPC is sent to the right one.
	 */

	ins->ci_stopping = 1;

	/*
	 * The check instance on current engine may have failed or stopped, but we do not know
	 * whether there is active check instance on other engines or not, send stop RPC anyway.
	 */

	if (ins->ci_ranks == NULL) {
		rc = chk_prop_fetch(&ins->ci_prop, &ins->ci_ranks);
		/*
		 * We do not know the rank list, the sponsor needs to choose another leader.
		 * It may be that the DAOS check has never run on this engine.
		 */
		if (rc == -DER_NONEXIST)
			D_GOTO(out, rc = -DER_NOTLEADER);

		if (rc != 0)
			goto out;

		if (unlikely(ins->ci_ranks == NULL))
			D_GOTO(out, rc = -DER_NOTLEADER);
	}

	/* Use 0 as @gen parameter to all current or former instance by force. */
	rc = chk_stop_remote(ins->ci_ranks, 0, pool_nr, pools, chk_leader_stop_cb, ins);
	if (rc != 0)
		goto out;

	if (pool_nr == 0) {
		chk_pool_stop_all(ins, CHK__CHECK_POOL_STATUS__CPS_STOPPED, &rc);
		if (rc != 0)
			D_GOTO(out, rc);
	} else {
		for (i = 0; i < pool_nr; i++) {
			chk_pool_stop_one(ins, pools[i], CHK__CHECK_POOL_STATUS__CPS_STOPPED,
					  CHK_INVAL_PHASE, &rc);
			if (rc != 0)
				D_GOTO(out, rc);
		}
	}

	if (d_list_empty(&ins->ci_rank_list))
		chk_stop_sched(ins);

out:
	ins->ci_pool_stopped = 0;
	ins->ci_stopping = 0;
log:
	if (rc >= 0) {
		D_INFO("Leader stopped check with gen "DF_X64" for %d pools: rc %d\n",
		       cbk->cb_gen, pool_nr, rc);

		chk_pools_dump(NULL, pool_nr, pools);
	} else {
		D_ERROR("Leader failed to stop check with gen "DF_X64" for %d pools: "DF_RC"\n",
			cbk->cb_gen, pool_nr, DP_RC(rc));
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
chk_leader_query_cb(struct chk_co_rpc_cb_args *cb_args)
{
	struct chk_query_args		*cqa = cb_args->cb_priv;
	struct chk_query_pool_shard	*shards = cb_args->cb_data;
	struct chk_query_pool_shard	*shard;
	int				 rc = 0;
	int				 i;

	if (cb_args->cb_result != 0)
		goto out;

	cqa->cqa_ins_status = cb_args->cb_ins_status;
	cqa->cqa_ins_phase = cb_args->cb_ins_phase;
	cqa->cqa_gen = cb_args->cb_gen;

	for (i = 0; i < cb_args->cb_nr; i++) {
		/*
		 * @shards is from chk_query_remote RPC reply, the buffer will be released after
		 * the RPC done. Let's copy all related data to new the buffer for further using.
		 */
		rc = chk_leader_dup_shard(&shard, &shards[i]);
		if (rc != 0)
			goto out;

		rc = chk_pool_add_shard(cqa->cqa_hdl, &cqa->cqa_list, shard->cqps_uuid,
					shard->cqps_rank, NULL, cqa->cqa_ins, &cqa->cqa_count,
					shard, chk_leader_free_shard, NULL);
		if (rc != 0) {
			chk_leader_free_shard(shard);
			goto out;
		}
	}

out:
	if (rc != 0)
		D_ERROR(DF_LEADER" failed to handle query CB with result %d: "
			DF_RC"\n", DP_LEADER(cqa->cqa_ins), cb_args->cb_result, DP_RC(rc));

	return rc;
}

static struct chk_query_args *
chk_cqa_alloc(struct chk_instance *ins)
{
	struct umem_attr	 uma = { 0 };
	struct chk_query_args	*cqa;
	int			 rc;

	D_ALLOC_PTR(cqa);
	if (cqa == NULL)
		goto out;

	D_INIT_LIST_HEAD(&cqa->cqa_list);
	cqa->cqa_ins = ins;

	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
				   &cqa->cqa_btr, &cqa->cqa_hdl);
	if (rc != 0)
		D_FREE(cqa);

out:
	return cqa;
}

static void
chk_cqa_free(struct chk_query_args *cqa)
{
	if (cqa != NULL) {
		dbtree_destroy(cqa->cqa_hdl, NULL);
		D_FREE(cqa);
	}
}

int
chk_leader_query(int pool_nr, uuid_t pools[], chk_query_head_cb_t head_cb,
		 chk_query_pool_cb_t pool_cb, void *buf)
{
	struct chk_instance		*ins = chk_leader;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_query_args		*cqa = NULL;
	struct chk_pool_rec		*cpr;
	struct chk_pool_rec		*tmp;
	struct chk_pool_shard		*cps;
	struct chk_query_pool_shard	*shard;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	uint64_t			 gen = cbk->cb_gen;
	uint32_t			 status;
	uint32_t			 phase;
	uint32_t			 idx = 0;
	int				 rc;
	int				 i;
	bool				 skip;

	/*
	 * NOTE: Similar as stop case, we need the ability to query check information from
	 *	 new leader if the old one dead. But the information from new leader may be
	 *	 not very accurate. It is the control plane duty to send the CHK_QUERY dRPC
	 *	 to the right one.
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

	cqa = chk_cqa_alloc(ins);
	if (cqa == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

again:
	rc = chk_query_remote(ins->ci_ranks, gen, pool_nr, pools, chk_leader_query_cb, cqa);
	if (rc != 0) {
		if (rc == -DER_OOG || rc == -DER_GRPVER || rc == -DER_AGAIN) {
			D_INFO(DF_LEADER" Someone is not ready %d, let's retry query after 1 sec\n",
			       DP_LEADER(ins), rc);
			if (!d_list_empty(&cqa->cqa_list)) {
				chk_cqa_free(cqa);
				cqa = chk_cqa_alloc(ins);
				if (cqa == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}
			dss_sleep(1000);
			goto again;
		}

		goto out;
	}

	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
		/*
		 * For non-dangling pool, the check engine will return related pool shards
		 * information, and then merge with the check leader owned information via
		 * the subsequent chk_pool_merge_status().
		 */
		if (!cpr->cpr_dangling)
			continue;

		skip = false;
		if (pool_nr != 0) {
			skip = true;
			for (i = 0; i < pool_nr && skip; i++) {
				if (uuid_compare(cpr->cpr_uuid, pools[i]) == 0)
					skip = false;
			}
		}

		if (!skip) {
			D_ALLOC_PTR(shard);
			if (shard == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			uuid_copy(shard->cqps_uuid, cpr->cpr_uuid);
			shard->cqps_status = cpr->cpr_bk.cb_pool_status;
			shard->cqps_phase = cpr->cpr_bk.cb_phase;
			shard->cqps_rank = CHK_LEADER_RANK;

			rc = chk_pool_add_shard(cqa->cqa_hdl, &cqa->cqa_list, cpr->cpr_uuid,
						CHK_LEADER_RANK, NULL, ins, &cqa->cqa_count,
						shard, chk_leader_free_shard, NULL);
			if (rc != 0)
				goto out;
		}
	}

	status = cbk->cb_ins_status;
	phase = cbk->cb_phase;
	chk_ins_merge_info(&status, cqa->cqa_ins_status, &phase, cqa->cqa_ins_phase, &gen,
			   cqa->cqa_gen);
	rc = head_cb(status, phase, &cbk->cb_statistics, &cbk->cb_time, cqa->cqa_count, buf);
	if (rc != 0)
		goto out;

	d_list_for_each_entry(cpr, &cqa->cqa_list, cpr_link) {
		d_iov_set(&riov, NULL, 0);
		d_iov_set(&kiov, cpr->cpr_uuid, sizeof(uuid_t));
		rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
		if (likely(rc == 0))
			tmp = (struct chk_pool_rec *)riov.iov_buf;
		else
			tmp = NULL;

		d_list_for_each_entry(cps, &cpr->cpr_shard_list, cps_link) {
			shard = cps->cps_data;

			/*
			 * NOTE: The pool status on different engines may be different. For example:
			 *	 the PS leader may be in PENDING because of interaction, but others
			 *	 are still in running status. We summarize the status for the query
			 *	 result to avoid confusing. It is just temporary solution, and will
			 *	 be moved to control plane in the future - DAOS-13989.
			 */
			if (cps->cps_rank != CHK_LEADER_RANK && tmp != NULL) {
				shard->cqps_status = chk_pool_merge_status(shard->cqps_status,
									tmp->cpr_bk.cb_pool_status);
				if (shard->cqps_phase < tmp->cpr_bk.cb_phase)
					shard->cqps_phase = tmp->cpr_bk.cb_phase;
			}

			rc = pool_cb(shard, idx++, buf);
			if (rc != 0)
				goto out;

			D_ASSERT(cqa->cqa_count >= idx);
		}
	}
out:
	chk_cqa_free(cqa);
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 "Leader query check with gen "DF_X64" for %d pools: "DF_RC"\n",
		 gen, pool_nr, DP_RC(rc));

	return rc;
}

int
chk_leader_prop(chk_prop_cb_t prop_cb, void *buf)
{
	struct chk_property	*prop = &chk_leader->ci_prop;

	return prop_cb(buf, prop->cp_policies, CHK_POLICY_MAX - 1, prop->cp_flags);
}

static int
chk_leader_act_internal(struct chk_instance *ins, uint64_t seq, uint32_t act, bool for_all,
			bool locked, uint32_t *cla)
{
	struct chk_pending_rec	*pending = NULL;
	struct chk_pool_rec	*pool = NULL;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	rc = chk_pending_del(ins, seq, locked, &pending);
	if (rc != 0)
		goto out;

	D_ASSERT(pending->cpr_busy);

	if (pending->cpr_on_leader) {
		ABT_mutex_lock(pending->cpr_mutex);
		/*
		 * It is the control plane's duty to guarantee that the decision is a valid
		 * action from the report options. Otherwise, related inconsistency will be ignored.
		 */
		pending->cpr_action = act;
		ABT_cond_broadcast(pending->cpr_cond);
		ABT_mutex_unlock(pending->cpr_mutex);

		if (cla != NULL)
			*cla = pending->cpr_class;
	} else {
		d_iov_set(&riov, NULL, 0);
		d_iov_set(&kiov, pending->cpr_uuid, sizeof(uuid_t));
		rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
		if (rc == 0) {
			pool = (struct chk_pool_rec *)riov.iov_buf;
			if (pool->cpr_bk.cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_PENDING)
				pool->cpr_bk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
		} else {
			rc = 0;
		}

		/* For locked case, check engines have already processed related interaction. */
		if (!locked)
			rc = chk_act_remote(ins->ci_ranks, ins->ci_bk.cb_gen, seq,
					    pending->cpr_class, act, pending->cpr_rank, for_all);

		chk_pending_destroy(pending);
	}

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" takes action for report with seq "DF_X64", action %u: "DF_RC"\n",
		 DP_LEADER(ins), seq, act, DP_RC(rc));

	return rc;
}

int
chk_leader_act(uint64_t seq, uint32_t act, bool for_all)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_pool_rec	*pool = NULL;
	struct chk_pool_rec	*pool_tmp = NULL;
	struct chk_pending_rec	*cpr = NULL;
	struct chk_pending_rec	*cpr_tmp = NULL;
	uint32_t		 cla = 0;
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

	rc = chk_leader_act_internal(ins, seq, act, for_all, false, &cla);
	if (rc != 0 || !for_all)
		goto out;

	if (likely(prop->cp_policies[cla] != act)) {
		prop->cp_policies[cla] = act;
		chk_prop_update(prop, NULL);
	}

	/*
	 * Hold reference on each to guarantee that the next 'tmp' will not be unlinked from the
	 * pool list during current pool process.
	 */
	d_list_for_each_entry(pool, &ins->ci_pool_list, cpr_link)
		chk_pool_get(pool);

	d_list_for_each_entry_safe(pool, pool_tmp, &ins->ci_pool_list, cpr_link) {
		if (rc == 0) {
			ABT_rwlock_wrlock(ins->ci_abt_lock);
			d_list_for_each_entry_safe(cpr, cpr_tmp, &pool->cpr_pending_list,
						   cpr_pool_link) {
				if (cpr->cpr_class != cla ||
				    cpr->cpr_action != CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT)
					continue;

				rc = chk_leader_act_internal(ins, cpr->cpr_seq, act, false, true,
							     NULL);
				if (rc != 0)
					break;
			}
			ABT_rwlock_unlock(ins->ci_abt_lock);
		}
		chk_pool_put(pool);
	}

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" takes action for report with seq "DF_X64", action %u, flags %s: %d\n",
		 DP_LEADER(ins), seq, act, for_all ? "all" : "once", rc);

	return rc;
}

/*
 * \return	Positive value if interaction is interrupted, such as check stop.
 *		Zero on success.
 *		Negative value if error.
 */
int
chk_leader_report(struct chk_report_unit *cru, uint64_t *seq, int *decision)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pending_rec	*cpr = NULL;
	struct chk_pool_rec	*pool = NULL;
	struct chk_rank_rec	*crr = NULL;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER)
		D_GOTO(out, rc = -DER_NOTLEADER);

	/* Tell check engine that check leader is not running via "-DER_NOTAPPLICABLE". */
	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (*seq == 0) {

new_seq:
		*seq = chk_report_seq_gen(ins);
	}

	D_INFO(DF_LEADER" handle %s report from rank %u with seq "
	       DF_X64" class %u, action %u, result %d\n", DP_LEADER(ins),
	       decision != NULL ? "local" : "remote", cru->cru_rank, *seq, cru->cru_cla,
	       cru->cru_act, cru->cru_result);

	if (cru->cru_act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT) {
		if (cru->cru_pool == NULL)
			D_GOTO(log, rc = -DER_INVAL);

		d_iov_set(&riov, NULL, 0);
		d_iov_set(&kiov, cru->cru_pool, sizeof(uuid_t));
		rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
		if (rc != 0)
			goto log;

		pool = (struct chk_pool_rec *)riov.iov_buf;

		if (decision == NULL) {
			d_iov_set(&riov, NULL, 0);
			d_iov_set(&kiov, &cru->cru_rank, sizeof(cru->cru_rank));
			rc = dbtree_lookup(ins->ci_rank_hdl, &kiov, &riov);
			if (rc != 0)
				goto log;

			crr = (struct chk_rank_rec *)riov.iov_buf;
		}

		rc = chk_pending_add(ins, &pool->cpr_pending_list,
				     crr != NULL ? &crr->crr_pending_list : NULL,
				     *cru->cru_pool, *seq, cru->cru_rank, cru->cru_cla, &cpr);
		if (decision != NULL) {
			if (unlikely(rc == -DER_AGAIN))
				goto new_seq;

			cpr->cpr_on_leader = 1;
		}

		if (rc != 0)
			goto log;
	}

	rc = chk_report_upcall(cru->cru_gen, *seq, cru->cru_cla, cru->cru_act, cru->cru_result,
			       cru->cru_rank, cru->cru_target, cru->cru_pool, cru->cru_pool_label,
			       cru->cru_cont, cru->cru_cont_label, cru->cru_obj, cru->cru_dkey,
			       cru->cru_akey, cru->cru_msg, cru->cru_option_nr, cru->cru_options,
			       cru->cru_detail_nr, cru->cru_details);
	/* Check cpr->cpr_action for the case of "dmg check repair" by race. */
	if (rc == 0 && pool != NULL &&
	    likely(cpr->cpr_action == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT))
		pool->cpr_bk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_PENDING;

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

	D_ASSERT(cpr->cpr_busy);

	D_INFO(DF_LEADER" need interaction for class %u with seq "DF_X64"\n",
	       DP_LEADER(ins), cru->cru_cla, *seq);

	ABT_mutex_lock(cpr->cpr_mutex);

again:
	if (cpr->cpr_action != CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT) {
		*decision = cpr->cpr_action;
		ABT_mutex_unlock(cpr->cpr_mutex);
		goto out;
	}

	if (!ins->ci_sched_running || ins->ci_sched_exiting || cpr->cpr_exiting) {
		rc = 1;
		ABT_mutex_unlock(cpr->cpr_mutex);
		goto out;
	}

	ABT_cond_wait(cpr->cpr_cond, cpr->cpr_mutex);

	goto again;

out:
	if (pool != NULL && pool->cpr_bk.cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_PENDING &&
	    (rc != 0 || (cpr != NULL &&
			 cpr->cpr_action != CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT)))
		pool->cpr_bk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;

	if ((rc != 0 || decision != NULL) && cpr != NULL)
		chk_pending_destroy(cpr);

	return rc;
}

int
chk_leader_notify(struct chk_iv *iv)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_rank_bundle	 rbund = { 0 };
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc = 0;

	/* Ignore the notification that is not applicable to current rank. */

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (cbk->cb_gen != iv->ci_gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (!uuid_is_null(iv->ci_uuid)) {
		rc = chk_pool_handle_notify(ins, iv);
		goto out;
	}

	switch (iv->ci_ins_status) {
	case CHK__CHECK_INST_STATUS__CIS_INIT:
	case CHK__CHECK_INST_STATUS__CIS_STOPPED:
	case CHK__CHECK_INST_STATUS__CIS_PAUSED:
	case CHK__CHECK_INST_STATUS__CIS_IMPLICATED:
		/* Directly ignore above. */
		break;
	case CHK__CHECK_INST_STATUS__CIS_RUNNING:
		if (unlikely(iv->ci_phase < cbk->cb_phase)) {
			rc = -DER_NOTAPPLICABLE;
		} else if (iv->ci_phase != cbk->cb_phase) {
			rbund.crb_rank = iv->ci_rank;
			rbund.crb_phase = iv->ci_phase;
			rbund.crb_ins = ins;

			d_iov_set(&riov, &rbund, sizeof(rbund));
			d_iov_set(&kiov, &iv->ci_rank, sizeof(iv->ci_rank));
			rc = dbtree_update(ins->ci_rank_hdl, &kiov, &riov);
		}
		break;
	case CHK__CHECK_INST_STATUS__CIS_COMPLETED:
		/*
		 * NOTE: Currently, we do not support to partial check till the specified phase.
		 *	 Then the completed phase will be either container cleanup or all done.
		 */
		if (unlikely(iv->ci_phase != CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP &&
			     iv->ci_phase != CHK__CHECK_SCAN_PHASE__CSP_DONE))
			rc = -DER_INVAL;
		else
			rc = chk_rank_del(ins, iv->ci_rank);
		break;
	case CHK__CHECK_INST_STATUS__CIS_FAILED:
		chk_ins_set_fail(ins, iv->ci_phase);
		rc = chk_rank_del(ins, iv->ci_rank);
		if (rc != 0 || !(prop->cp_flags & CHK__CHECK_FLAG__CF_FAILOUT))
			D_GOTO(out, rc = (rc == -DER_NONEXIST || rc == -DER_NO_HDL ? 0 : rc));

		ins->ci_implicated = 1;
		chk_stop_sched(ins);
		break;
	default:
		rc = -DER_INVAL;
		break;
	}

out:
	D_CDEBUG(rc != 0 && rc != -DER_NOTAPPLICABLE, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" handle notification from rank %u, for pool "
		 DF_UUIDF", phase %u, ins_status %u, pool_status %u, gen "DF_X64", seq "DF_X64
		 ": "DF_RC"\n", DP_LEADER(ins), iv->ci_rank, DP_UUID(iv->ci_uuid), iv->ci_phase,
		 iv->ci_ins_status, iv->ci_pool_status, iv->ci_gen, iv->ci_seq, DP_RC(rc));

	return rc == -DER_NOTAPPLICABLE ? 0 : rc;
}

int
chk_leader_rejoin(uint64_t gen, d_rank_t rank, uuid_t iv_uuid, uint32_t *flags, int *pool_nr,
		  uuid_t **pools)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	int			 rc = 0;

	if (cbk->cb_magic != CHK_BK_MAGIC_LEADER)
		D_GOTO(out, rc = -DER_NOTLEADER);

	if (uuid_compare(cbk->cb_iv_uuid, iv_uuid))
		D_GOTO(out, rc = -DER_STALE);

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_STALE);

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_SHUTDOWN);

	/* The rank has been excluded from (or never been part of) the check instance. */
	if (!chk_rank_in_list(ins->ci_ranks, rank))
		D_GOTO(out, rc = -DER_NO_PERM);

	if (ins->ci_orphan_done)
		*flags = CRF_ORPHAN_DONE;

	rc = chk_leader_pools2list(ins, pool_nr, pools);

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_LEADER" %u handle rejoin from rank %u, gen "DF_X64", iv "DF_UUIDF":"DF_RC"\n",
		 DP_LEADER(ins), cbk->cb_ins_status, rank, gen, DP_UUID(iv_uuid), DP_RC(rc));

	return rc;
}

void
chk_leader_pause(void)
{
	struct chk_instance	*ins = chk_leader;

	chk_stop_sched(ins);
	D_ASSERT(d_list_empty(&ins->ci_rank_list));
}

static void
chk_rank_event_cb(d_rank_t rank, uint64_t incarnation, enum crt_event_source src,
		  enum crt_event_type type, void *arg)
{
	struct chk_instance	*ins = chk_leader;
	struct chk_dead_rank	*cdr = NULL;
	int			 rc = 0;

	/* Ignore the event that is not applicable to current rank. */

	if (src != CRT_EVS_SWIM)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (type != CRT_EVT_DEAD && type != CRT_EVT_ALIVE)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (!ins->ci_sched_running)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (type == CRT_EVT_DEAD) {
		D_ALLOC_PTR(cdr);
		if (cdr == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		cdr->cdr_rank = rank;
	}

	ABT_mutex_lock(ins->ci_abt_mutex);
	if (cdr != NULL) {
		/*
		 * The event may be triggered on non-system SX. Let's notify the leader scheduler
		 * to handle that on system XS.
		 */
		d_list_add_tail(&cdr->cdr_link, &ins->ci_dead_ranks);
	} else {
		/* Remove former non-handled dead rank from the list. */
		d_list_for_each_entry(cdr, &ins->ci_dead_ranks, cdr_link) {
			if (cdr->cdr_rank == rank) {
				d_list_del(&cdr->cdr_link);
				D_FREE(cdr);
				break;
			}
		}
	}
	ABT_mutex_unlock(ins->ci_abt_mutex);

out:
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_LEADER" receive rank %u %s event: "DF_RC"\n",
			 DP_LEADER(ins), rank, type == CRT_EVT_DEAD ? "dead" : "alive", DP_RC(rc));
}

int
chk_leader_init(void)
{
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_bookmark		*cbk;
	int				 rc;

	rc = chk_ins_init(&chk_leader);
	if (rc != 0)
		goto fini;

	chk_leader->ci_is_leader = 1;
	chk_report_seq_init(chk_leader);

	/*
	 * DAOS global consistency check depends on all related engines' local
	 * consistency. If hit some local data corruption, then it is possible
	 * that local consistency is not guaranteed. Need to break and resolve
	 * related local inconsistency firstly.
	 */

	cbk = &chk_leader->ci_bk;
	rc = chk_bk_fetch_leader(cbk);
	if (rc == -DER_NONEXIST)
		goto prop;

	/* It may be caused by local data corruption, let's break. */
	if (rc != 0)
		goto fini;

	/*
	 * NOTE: The unknown magic may be caused by data corruption, also
	 *	 may for downgrade case. If we downgraded from new layout,
	 *	 we do not understand it. Under such case, reporting it as
	 *	 -DER_IO may be not the best choice, but it is better than
	 *	 damaging the system if we modify something with old logic.
	 *
	 *	 On the other hand, if we have to start old DAOS check on
	 *	 new layout, then please manually remove chk bookmark and
	 *	 property KVs from sys_db via DDB tools firstly.
	 */
	if (cbk->cb_magic != 0 && cbk->cb_magic != CHK_BK_MAGIC_LEADER) {
		D_ERROR("Hit corrupted leader bookmark on rank %u: %u vs %u\n",
			dss_self_rank(), cbk->cb_magic, CHK_BK_MAGIC_LEADER);
		D_GOTO(fini, rc = -DER_IO);
	}

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		/*
		 * Leader crashed before normally exit, reset the status as 'PAUSED'
		 * to avoid blocking next CHK_START.
		 */
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_PAUSED;
		cbk->cb_time.ct_stop_time = time(NULL);
		rc = chk_bk_update_leader(cbk);
		if (rc != 0) {
			D_ERROR(DF_LEADER" failed to reset ins status as 'PAUSED': "DF_RC"\n",
				DP_LEADER(chk_leader), DP_RC(rc));
			goto fini;
		}

		ctpa.ctpa_gen = cbk->cb_gen;
		rc = chk_traverse_pools(chk_pools_pause_cb, &ctpa);
		/*
		 * Failed to reset pool status will not affect next check start, so it is not fatal,
		 * but related check query result may be confused for user.
		 */
		if (rc != 0)
			D_WARN(DF_LEADER" failed to reset pools status as 'PAUSED': "DF_RC"\n",
				DP_LEADER(chk_leader), DP_RC(rc));
	}

prop:
	rc = chk_prop_fetch(&chk_leader->ci_prop, &chk_leader->ci_ranks);
	if (rc == 0 || rc == -DER_NONEXIST)
		rc = crt_register_event_cb(chk_rank_event_cb, NULL);
fini:
	if (rc != 0)
		chk_ins_fini(&chk_leader);
	else
		chk_leader->ci_inited = 1;
	return rc;
}

void
chk_leader_fini(void)
{
	crt_unregister_event_cb(chk_rank_event_cb, NULL);
	chk_ins_fini(&chk_leader);
}
