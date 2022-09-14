/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <time.h>
#include <abt.h>
#include <cart/api.h>
#include <daos/btree.h>
#include <daos/btree_class.h>
#include <daos/common.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/pool.h>
#include <daos_srv/vos.h>
#include <daos_srv/iv.h>
#include <daos_srv/vos_types.h>

#include "chk.pb-c.h"
#include "chk_internal.h"

#define DF_ENGINE	"Check engine (gen: "DF_X64")"
#define DP_ENGINE(ins)	(ins)->ci_bk.cb_gen
#define CHK_MSG_BUFLEN	320

static struct chk_instance	*chk_engine;

struct chk_traverse_pools_args {
	uint64_t			 ctpa_gen;
	struct chk_instance		*ctpa_ins;
	uint32_t			 ctpa_status;
};

struct chk_query_pool_args {
	struct chk_instance		*cqpa_ins;
	uint32_t			 cqpa_cap;
	uint32_t			 cqpa_idx;
	struct chk_query_pool_shard	*cqpa_shards;
};

struct chk_query_xstream_args {
	uuid_t				 cqxa_uuid;
	struct chk_query_pool_args	*cqxa_args;
	struct chk_query_target		 cqxa_target;
};

struct chk_cont_list_args {
	uuid_t				 ccla_pool;
	uint32_t			 ccla_cap;
	uint32_t			 ccla_idx;
	uuid_t				*ccla_conts;
};

struct chk_cont_list_aggregator {
	uuid_t				 ccla_pool;
	d_list_t			 ccla_list;
	daos_handle_t			 ccla_toh;
	struct btr_root			 ccla_btr;
	uint32_t			 ccla_count;
};

struct chk_cont_rec {
	d_list_t			 ccr_link;
	uuid_t				 ccr_uuid;
	struct chk_cont_list_aggregator	*ccr_aggregator;
	daos_prop_t			*ccr_label_prop;
	d_iov_t				 ccr_label_ps;
	uint32_t			 ccr_label_checked:1,
					 ccr_skip:1;
};

struct chk_cont_bundle {
	struct chk_cont_list_aggregator	*ccb_aggregator;
	uuid_t				 ccb_uuid;
};

struct chk_cont_label_cb_args {
	struct chk_cont_list_aggregator	*cclca_aggregator;
	struct cont_svc			*cclca_svc;
	struct chk_pool_rec		*cclca_cpr;
};

static int
chk_cont_hkey_size(void)
{
	return sizeof(uuid_t);
}

static void
chk_cont_hkey_gen(struct btr_instance *tins, d_iov_t *key_iov, void *hkey)
{
	D_ASSERT(key_iov->iov_len == sizeof(uuid_t));

	memcpy(hkey, key_iov->iov_buf, key_iov->iov_len);
}

static int
chk_cont_alloc(struct btr_instance *tins, d_iov_t *key_iov, d_iov_t *val_iov,
	       struct btr_record *rec, d_iov_t *val_out)
{
	struct chk_cont_bundle	*ccb = val_iov->iov_buf;
	struct chk_cont_rec	*ccr;
	int			 rc = 0;

	D_ALLOC_PTR(ccr);
	if (ccr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(ccr->ccr_uuid, ccb->ccb_uuid);
	ccr->ccr_aggregator = ccb->ccb_aggregator;
	d_list_add_tail(&ccr->ccr_link, &ccb->ccb_aggregator->ccla_list);
	ccb->ccb_aggregator->ccla_count++;

	rec->rec_off = umem_ptr2off(&tins->ti_umm, ccr);

out:
	return rc;
}

static int
chk_cont_free(struct btr_instance *tins, struct btr_record *rec, void *args)
{
	struct chk_cont_rec	*ccr = umem_off2ptr(&tins->ti_umm, rec->rec_off);

	rec->rec_off = UMOFF_NULL;

	ccr->ccr_aggregator->ccla_count--;
	d_list_del(&ccr->ccr_link);
	daos_prop_free(ccr->ccr_label_prop);
	daos_iov_free(&ccr->ccr_label_ps);
	D_FREE(ccr);

	return 0;
}

static int
chk_cont_fetch(struct btr_instance *tins, struct btr_record *rec,
	       d_iov_t *key_iov, d_iov_t *val_iov)
{
	struct chk_cont_rec	*ccr;

	D_ASSERT(val_iov != NULL);

	ccr = umem_off2ptr(&tins->ti_umm, rec->rec_off);
	d_iov_set(val_iov, ccr, sizeof(*ccr));

	return 0;
}

static int
chk_cont_update(struct btr_instance *tins, struct btr_record *rec,
		d_iov_t *key, d_iov_t *val, d_iov_t *val_out)
{
	return 0;
}

btr_ops_t chk_cont_ops = {
	.to_hkey_size	= chk_cont_hkey_size,
	.to_hkey_gen	= chk_cont_hkey_gen,
	.to_rec_alloc	= chk_cont_alloc,
	.to_rec_free	= chk_cont_free,
	.to_rec_fetch	= chk_cont_fetch,
	.to_rec_update  = chk_cont_update,
};

static void
chk_destroy_pool_tree(struct chk_instance *ins)
{
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;

	/*
	 * Take reference on each pool record to guarantee that the pool list will not be
	 * broken when we traverse the pool list even if there is yield during the travel.
	 */
	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link)
		chk_pool_get(cpr);

	/*
	 * Once the pool record is deleted from the tree, the initial reference held when
	 * created will be released: if it is current ULT delete the record from the tree,
	 * then it will be via chk_pool_free()->chk_pool_put(). Otherwise, if it has been
	 * deleted from the tree by others, then related logic will call chk_pool_put().
	 */
	chk_destroy_tree(&ins->ci_pool_hdl, &ins->ci_pool_btr);

	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
		chk_pool_wait(cpr);
		chk_pool_shutdown(cpr);
		/* Release the reference held just above. */
		chk_pool_put(cpr);
	}
}

static void
chk_pool_stop_one(struct chk_instance *ins, uuid_t uuid, uint32_t status,
		  bool remove, bool wait, int *ret)
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
		if (rc == -DER_NONEXIST)
			rc = 0;
		else
			D_ERROR(DF_ENGINE" on rank %u failed to delete pool record "
				DF_UUIDF" with status %u: "DF_RC"\n",
				DP_ENGINE(ins), dss_self_rank(), DP_UUID(uuid), status, DP_RC(rc));
	} else {
		cpr = (struct chk_pool_rec *)riov.iov_buf;
		cbk = &cpr->cpr_bk;

		if (wait)
			chk_pool_wait(cpr);
		chk_pool_shutdown(cpr);

		uuid_unparse_lower(uuid, uuid_str);
		if (remove) {
			rc = chk_bk_delete_pool(uuid_str);
			if (rc == -DER_NONEXIST)
				rc = 0;
		} else if (cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_CHECKING ||
			   cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_PENDING) {
			cbk->cb_pool_status = status;
			cbk->cb_time.ct_stop_time = time(NULL);
			rc = chk_bk_update_pool(cbk, uuid_str);
		}

		chk_pool_put(cpr);
	}

	if (ret != NULL)
		*ret = rc;
}

static void
chk_engine_exit(struct chk_instance *ins, uint32_t ins_status, uint32_t pool_status)
{
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	struct chk_iv		 iv = { 0 };
	int			 rc;

	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link)
		chk_pool_stop_one(ins, cpr->cpr_uuid, pool_status, false, true, NULL);

	chk_destroy_pending_tree(ins);
	chk_destroy_pool_tree(ins);

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_ins_status = ins_status;
		cbk->cb_time.ct_stop_time = time(NULL);
		chk_bk_update_engine(cbk);
	}

	if (ins_status != CHK__CHECK_INST_STATUS__CIS_PAUSED &&
	    ins_status != CHK__CHECK_INST_STATUS__CIS_IMPLICATED && ins->ci_iv_ns != NULL) {
		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = cbk->cb_phase;
		iv.ci_status = ins_status;
		iv.ci_to_leader = 1;

		/* Notify the leader that check instance exit on the engine. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_TO_ROOT,
				   CRT_IV_SYNC_NONE, true);
		if (rc != 0)
			D_ERROR(DF_ENGINE" on rank %u failed to notify leader for its exit, "
				"status %u: "DF_RC"\n",
				DP_ENGINE(ins), dss_self_rank(), ins_status, DP_RC(rc));
	}
}

static uint32_t
chk_engine_find_slowest(struct chk_instance *ins, bool *done)
{
	struct chk_pool_rec	*cpr;
	uint32_t		 phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;
	int			 running = 0;

	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
		if (cpr->cpr_bk.cb_phase < phase)
			phase = cpr->cpr_bk.cb_phase;
		if (!cpr->cpr_done)
			running++;
	}

	if (done != NULL && running == 0)
		*done = true;

	return phase;
}

static int
chk_engine_setup_pools(struct chk_instance *ins)
{
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_bookmark	*pool_cbk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	uuid_t			 uuid;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	int			 rc = 0;
	int			 rc1;

	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
		if (cpr->cpr_started || cpr->cpr_stop)
			continue;

		pool_cbk = &cpr->cpr_bk;
		if (pool_cbk->cb_phase < cbk->cb_phase) {
			pool_cbk->cb_phase = cbk->cb_phase;
			/* QUEST: How to estimate the left time? */
			pool_cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE -
							 pool_cbk->cb_phase;
			uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
			rc1 = chk_bk_update_pool(pool_cbk, uuid_str);
			if (rc1 != 0)
				D_WARN("Failed to update pool bookmark (1) for %s: "DF_RC"\n",
				       uuid_str, DP_RC(rc1));
		}

		uuid_copy(uuid, cpr->cpr_uuid);
		rc = ds_pool_start(uuid);
		if (rc != 0) {
			ins->ci_slowest_fail_phase = pool_cbk->cb_phase;
			chk_pool_stop_one(ins, uuid, CHK__CHECK_POOL_STATUS__CPS_FAILED,
					  false, true, NULL);
			D_ERROR(DF_ENGINE " on rank %u failed (%s) to start pool "
				DF_UUIDF": "DF_RC"\n", DP_ENGINE(ins), dss_self_rank(),
				prop->cp_flags & CHK__CHECK_FLAG__CF_FAILOUT ? "out" : "cnt",
				DP_UUID(uuid), DP_RC(rc));

			if (prop->cp_flags & CHK__CHECK_FLAG__CF_FAILOUT)
				goto out;
			rc = 0;
		} else {
			cpr->cpr_started = 1;
			D_INFO(DF_ENGINE" on rank %u start pool "DF_UUIDF" at phase %u\n",
			       DP_ENGINE(ins), dss_self_rank(), DP_UUID(uuid), pool_cbk->cb_phase);
		}
	}

out:
	return rc;
}

static inline void
chk_engine_post_repair(struct chk_instance *ins, int *result)
{
	if (!(ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT))
		*result = 0;
}

static int
chk_engine_pm_orphan(struct chk_pool_rec *cpr, d_rank_t rank, int index)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	d_rank_list_t			 ranks = { 0 };
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	if (index < 0)
		cla = CHK__CHECK_INCONSIST_CLASS__CIC_ENGINE_NONEXIST_IN_MAP;
	else
		cla = CHK__CHECK_INCONSIST_CLASS__CIC_ENGINE_DOWN_IN_MAP;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;
	cpr->cpr_dirty = 1;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * If the rank does not exists in the pool map, then destroy the orphan pool rank
		 * to release space by default.
		 *
		 * XXX: Currently, we does not support to add the orphan pool rank into the pool
		 *	map. If want to add them, it can be done via pool extend after DAOS check.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		/* Fall through. */
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			if (index < 0) {
				ranks.rl_ranks = &rank;
				ranks.rl_nr = 1;
				result = ds_mgmt_tgt_pool_destroy(cpr->cpr_uuid, &ranks);
			} else {
				result = ds_mgmt_tgt_pool_shard_destroy(cpr->cpr_uuid, index, rank);
			}

			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		cpr->cpr_skip = 1;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		/* Report the inconsistency without repair. */
		cbk->cb_statistics.cs_ignored++;
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
			cbk->cb_statistics.cs_ignored++;
			break;
		}

		options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		option_nr = 2;
		break;
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects orphan %s entry in pool map for "
		 DF_UUIDF", rank %u, index %d",
		 index < 0 ? "rank" : "shard", DP_UUID(cpr->cpr_uuid), rank, index);
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects orphan %s entry in pool map for "
		 DF_UUIDF", rank %u, index %d, action %u (%s), handle_rc %d, "
		 "report_rc %d, decision %d\n",
		 DP_ENGINE(ins), index < 0 ? "rank" : "shard", DP_UUID(cpr->cpr_uuid), rank,
		 index, act, option_nr ? "need interact" : "no interact", result, rc, decision);

	if (rc != 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		cpr->cpr_skip = 1;
		result = rc;
	}

	if (result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;

	switch (decision) {
	default:
		D_ERROR(DF_ENGINE" got invalid decision %d for orphan %s entry in pool map "
			"for pool "DF_UUIDF", rank %u, index %d. Ignore the inconsistency.\n",
			DP_ENGINE(ins), decision, index < 0 ? "rank" : "shard",
			DP_UUID(cpr->cpr_uuid), rank, index);
		/*
		 * Invalid option, ignore the inconsistency.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		cbk->cb_statistics.cs_ignored++;
		cpr->cpr_skip = 1;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			if (index < 0) {
				ranks.rl_ranks = &rank;
				ranks.rl_nr = 1;
				result = ds_mgmt_tgt_pool_destroy(cpr->cpr_uuid, &ranks);
			} else {
				result = ds_mgmt_tgt_pool_shard_destroy(cpr->cpr_uuid, index, rank);
			}

			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		cpr->cpr_skip = 1;
		break;
	}

	goto report;

out:
	chk_engine_post_repair(ins, &result);

	return result;
}

static int
chk_engine_pm_dangling(struct chk_pool_rec *cpr, struct pool_map *map, struct pool_component *comp,
		       uint32_t status)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	D_ASSERTF(status == PO_COMP_ST_DOWNOUT || status == PO_COMP_ST_DOWN,
		  "Unexpected pool map status %u\n", status);

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_ENGINE_HAS_NO_STORAGE;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;
	cpr->cpr_dirty = 1;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * If the target does not has storage on the engine, then mark it as 'DOWN' or
		 * 'DOWNOUT' in the pool map by default.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
		/* For dryrun mode, we will not persistently store the change in subsequent step. */
		comp->co_status = status;
		comp->co_fseq = pool_map_bump_version(map);
		cbk->cb_statistics.cs_repaired++;
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
			cbk->cb_statistics.cs_ignored++;
			break;
		}

		options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
		options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		option_nr = 2;
		break;
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects dangling %s entry in pool map for pool "
		 DF_UUIDF", rank %u, index %u",
		 comp->co_type == PO_COMP_TP_RANK ? "rank" : "target",
		 DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index);
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects dangling %s entry in pool map for pool "
		 DF_UUIDF" rank %u, index %u, action %u (%s), handle_rc %d, report_rc %d, "
		 "decision %d\n",
		 DP_ENGINE(ins), comp->co_type == PO_COMP_TP_RANK ? "rank" : "target",
		 DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index, act,
		 option_nr ? "need interact" : "no interact", result, rc, decision);

	if (rc != 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		result = rc;
	}

	if (result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;

	switch (decision) {
	default:
		D_ERROR(DF_ENGINE" got invalid decision %d for dangling %s entry in pool map "
			"for pool "DF_UUIDF", rank %u, index %u. Ignore the inconsistency.\n",
			DP_ENGINE(ins), decision,
			comp->co_type == PO_COMP_TP_RANK ? "rank" : "target",
			DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index);
		/*
		 * Invalid option, ignore the inconsistency.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		cbk->cb_statistics.cs_ignored++;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		/* For dryrun mode, we will not persistently store the change in subsequent step. */
		comp->co_status = status;
		comp->co_fseq = pool_map_bump_version(map);
		cbk->cb_statistics.cs_repaired++;
		break;
	}

	goto report;

out:
	/* For dangling PM entry, mark it as 'skip' in spite of which repair action to take. */
	cpr->cpr_skip = 1;

	chk_engine_post_repair(ins, &result);

	return result;
}

static int
chk_engine_pm_unknown_target(struct chk_pool_rec *cpr, struct pool_component *comp)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	int				 rc;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_UNKNOWN;
	act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
	cbk->cb_statistics.cs_total++;
	cbk->cb_statistics.cs_ignored++;
	cpr->cpr_dirty = 1;

	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = act;
	cru.cru_rank = dss_self_rank();
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects unknown target entry in pool map for pool "
		 DF_UUIDF", rank %u, index %u, status %u, skip.\n"
		 "You can change its status via DAOS debug tool if it is not for downgrade case.",
		 DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index, comp->co_status);
	cru.cru_msg = msg;
	cru.cru_result = 0;

	rc = chk_engine_report(&cru, NULL);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects unknown target entry in pool map for pool "DF_UUIDF", rank %u, "
		 "target %u, action %u (no interact), handle_rc 0, report_rc %d, decision 0\n",
		 DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index, act, rc);

	return 0;
}

static int
chk_engine_pool_mbs_one(struct chk_pool_rec *cpr, struct pool_map *map, struct chk_pool_mbs *mbs)
{
	struct pool_domain	*dom;
	struct pool_component	*comp;
	int			 i;
	int			 rc = 0;
	bool			 unknown;

	dom = pool_map_find_node_by_rank(map, mbs->cpm_rank);
	if (dom == NULL) {
		D_ASSERT(mbs->cpm_rank != dss_self_rank());

		rc = chk_engine_pm_orphan(cpr, mbs->cpm_rank, -1);
		goto out;
	}

	for (i = 0; i < dom->do_target_nr; i++) {
		comp = &dom->do_targets[i].ta_comp;
		unknown = false;

		switch (comp->co_status) {
		case PO_COMP_ST_DOWN:
			/*
			 * XXX: In the future, we may support to add the target (if exist) back.
			 *
			 * Fall through.
			 */
		case PO_COMP_ST_DOWNOUT:
			if (comp->co_index < mbs->cpm_tgt_nr &&
			    (mbs->cpm_tgt_status[comp->co_index] == DS_POOL_TGT_EMPTY ||
			     mbs->cpm_tgt_status[comp->co_index] == DS_POOL_TGT_NORMAL))
				rc = chk_engine_pm_orphan(cpr, mbs->cpm_rank, comp->co_index);
			/*
			 * Otherwise if the down/downout entry only exists in pool map,
			 * then it is useless, do nothing.
			 */
			break;
		case PO_COMP_ST_NEW:
			if (comp->co_index >= mbs->cpm_tgt_nr ||
			    mbs->cpm_tgt_status[comp->co_index] == DS_POOL_TGT_NONEXIST ||
			    mbs->cpm_tgt_status[comp->co_index] == DS_POOL_TGT_EMPTY)
				/* Dangling new entry in pool map, directly mark as 'DOWNOUT'. */
				rc = chk_engine_pm_dangling(cpr, map, comp, PO_COMP_ST_DOWNOUT);
			break;
		default:
			D_WARN(DF_ENGINE" hit knownn pool target status %u for "DF_UUIDF
			       " with rank %u, index %u, ID %u\n",
			       DP_ENGINE(cpr->cpr_ins), comp->co_status, DP_UUID(cpr->cpr_uuid),
			       mbs->cpm_rank, comp->co_index, comp->co_id);
			unknown = true;
			/* Fall through. */
		case PO_COMP_ST_UP:
			/* Fall through. */
		case PO_COMP_ST_UPIN:
			/* Fall through. */
		case PO_COMP_ST_DRAIN:
			if (comp->co_index >= mbs->cpm_tgt_nr ||
			    mbs->cpm_tgt_status[comp->co_index] == DS_POOL_TGT_NONEXIST ||
			    mbs->cpm_tgt_status[comp->co_index] == DS_POOL_TGT_EMPTY)
				/*
				 * Some data may be on the lost target, mark as 'DOWN' that
				 * will be handled via rebuild in subsequent process.
				 */
				rc = chk_engine_pm_dangling(cpr, map, comp, PO_COMP_ST_DOWN);
			else if (mbs->cpm_tgt_status[comp->co_index] == DS_POOL_TGT_NORMAL &&
				 unknown)
				/*
				 * XXX: The unknown status maybe because of downgraded from new
				 *	layout? It is better to keep it there with reporting it
				 *	to admin who can adjust the status via DAOS debug tool.
				 */
				rc = chk_engine_pm_unknown_target(cpr, comp);
			break;
		}

		if (rc != 0)
			goto out;

		/*
		 * Set the target status as DS_POOL_TGT_NONEXIST in
		 * DRAM to bypass the subsequent orphan entry check.
		 */
		if (comp->co_index < mbs->cpm_tgt_nr)
			mbs->cpm_tgt_status[comp->co_index] = DS_POOL_TGT_NONEXIST;

		comp->co_flags |= PO_COMPF_CHK_DONE;
	}

	dom->do_comp.co_flags |= PO_COMPF_CHK_DONE;

	for (i = 0; i < mbs->cpm_tgt_nr; i++) {
		/*
		 * All checked cpm_tgt_status[x] have been marked as 'DS_POOL_TGT_NONEXIST'
		 * in above for() block. So here, these left ones must be orphan targets.
		 */
		if (mbs->cpm_tgt_status[i] == DS_POOL_TGT_EMPTY ||
		    mbs->cpm_tgt_status[i] == DS_POOL_TGT_NORMAL) {
			rc = chk_engine_pm_orphan(cpr, mbs->cpm_rank, i);
			if (rc != 0)
				goto out;
		}
	}

out:
	return rc;
}

static int
chk_engine_find_dangling_pm(struct chk_pool_rec *cpr, struct pool_map *map)
{
	struct pool_domain	*doms = NULL;
	struct pool_component	*r_comp;
	struct pool_component	*t_comp;
	int			 rank_nr;
	int			 rc = 0;
	int			 i;
	int			 j;
	bool			 down = false;

	rank_nr = pool_map_find_nodes(map, PO_COMP_ID_ALL, &doms);
	if (rank_nr <= 0)
		D_GOTO(out, rc = rank_nr);

	for (i = 0; i < rank_nr; i++) {
		r_comp = &doms[i].do_comp;
		if (r_comp->co_flags & PO_COMPF_CHK_DONE ||
		    r_comp->co_status == PO_COMP_ST_DOWN || r_comp->co_status == PO_COMP_ST_DOWNOUT)
			continue;

		for (j = 0; j < doms[i].do_target_nr; j++) {
			t_comp = &doms[i].do_targets[j].ta_comp;

			switch (t_comp->co_status) {
			case PO_COMP_ST_DOWN:
				down = true;
				break;
			case PO_COMP_ST_DOWNOUT:
				/* Do nothing. */
				break;
			case PO_COMP_ST_NEW:
				/* Dangling new entry in pool map, directly mark as 'DOWNOUT'. */
				rc = chk_engine_pm_dangling(cpr, map, t_comp, PO_COMP_ST_DOWNOUT);
				break;
			default:
				D_WARN(DF_ENGINE" hit knownn pool target status %u for "DF_UUIDF
				       " with rank %u, index %u, ID %u\n", DP_ENGINE(cpr->cpr_ins),
				       t_comp->co_status, DP_UUID(cpr->cpr_uuid),
				       t_comp->co_rank, t_comp->co_index, t_comp->co_id);
				/* Fall through. */
			case PO_COMP_ST_UP:
				/* Fall through. */
			case PO_COMP_ST_UPIN:
				/* Fall through. */
			case PO_COMP_ST_DRAIN:
				down = true;
				/*
				 * Some data may be on the lost target, mark as 'DOWN' that
				 * will be handled via rebuild in subsequent process.
				 */
				rc = chk_engine_pm_dangling(cpr, map, t_comp, PO_COMP_ST_DOWN);
				break;
			}

			if (rc != 0)
				goto out;

			t_comp->co_flags |= PO_COMPF_CHK_DONE;
		}

		rc = chk_engine_pm_dangling(cpr, map, r_comp,
					    down ? PO_COMP_ST_DOWN : PO_COMP_ST_DOWNOUT);
		if (rc != 0)
			goto out;

		r_comp->co_flags |= PO_COMPF_CHK_DONE;
	}

out:
	return rc;
}

static int
chk_engine_bad_pool_label(struct chk_pool_rec *cpr, struct ds_pool_svc *svc)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_POOL_BAD_LABEL;
	act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS;
	cbk->cb_statistics.cs_total++;
	cpr->cpr_dirty = 1;

	if (ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
		cbk->cb_statistics.cs_repaired++;
	} else {
		result = ds_pool_svc_update_label(svc, cpr->cpr_label);
		if (result != 0)
			cbk->cb_statistics.cs_failed++;
		else
			cbk->cb_statistics.cs_repaired++;
	}

	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = act;
	cru.cru_rank = dss_self_rank();
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_msg = "Check engine detects corrupted pool label";
	cru.cru_result = result;

	rc = chk_engine_report(&cru, NULL);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects corrupted label for pool "
		 DF_UUIDF", action %u (no interact), MS label %s, handle_rc %d, report_rc %d\n",
		 DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid), act,
		 cpr->cpr_label != NULL ? cpr->cpr_label : "(null)", result, rc);

	/*
	 * It is not fatal even if failed to repair inconsistent pool label,
	 * then do not skip current pool for subsequent DAOS check.
	 */

	chk_engine_post_repair(ins, &result);

	return result;
}

static int
chk_engine_cont_list_init(uuid_t pool, struct chk_cont_list_aggregator *aggregator)
{
	struct umem_attr	uma = { 0 };

	uma.uma_id = UMEM_CLASS_VMEM;
	uuid_copy(aggregator->ccla_pool, pool);
	D_INIT_LIST_HEAD(&aggregator->ccla_list);

	return dbtree_create_inplace(DBTREE_CLASS_CHK_CONT, 0, CHK_BTREE_ORDER, &uma,
				     &aggregator->ccla_btr, &aggregator->ccla_toh);
}

static void
chk_engine_cont_list_fini(struct chk_cont_list_aggregator *aggregator)
{
	if (daos_handle_is_valid(aggregator->ccla_toh)) {
		dbtree_destroy(aggregator->ccla_toh, NULL);
		aggregator->ccla_toh = DAOS_HDL_INVAL;
	}
}

static int
chk_engine_cont_list_reduce_internal(struct chk_cont_list_aggregator *aggregator,
				     uuid_t *conts, uint32_t count)
{
	struct chk_cont_bundle		ccb = { 0 };
	d_iov_t				kiov;
	d_iov_t				riov;
	int				i;
	int				rc = 0;

	ccb.ccb_aggregator = aggregator;
	d_iov_set(&riov, &ccb, sizeof(ccb));

	for (i = 0; i < count; i++) {
		uuid_copy(ccb.ccb_uuid, conts[i]);
		d_iov_set(&kiov, conts[i], sizeof(uuid_t));
		rc = dbtree_upsert(aggregator->ccla_toh, BTR_PROBE_EQ, DAOS_INTENT_UPDATE,
				   &kiov, &riov, NULL);
		if (rc != 0) {
			D_ERROR("Failed to upsert "DF_UUIDF"/"DF_UUIDF" for cont list: "DF_RC"\n",
				DP_UUID(aggregator->ccla_pool), DP_UUID(conts[i]), DP_RC(rc));
			break;
		}
	}

	return rc;
}

static int
chk_engine_cont_list_remote_cb(void *args, uint32_t rank, uint32_t phase,
			       int result, void *data, uint32_t nr)
{
	int	rc;

	rc = chk_engine_cont_list_reduce_internal(args, data, nr);
	chk_fini_conts(data, rank);

	return rc;
}

static int
chk_engine_cont_orphan(struct chk_pool_rec *cpr, struct chk_cont_rec *ccr, struct cont_svc *svc)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN - 1] = { 0 };
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_CONT_NONEXIST_ON_PS;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * If the container is not registered to the container service, then destroy the
		 * orphan container to release space by default.
		 *
		 * XXX: Currently, we do not support to add the orphan container back to the CS,
		 *	that may be implemented in the future when we have enough information to
		 *	recover necessary prop/attr for the orphan container.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		/* Fall through. */
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = ds_cont_destroy_orphan(svc, ccr->ccr_uuid);
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
			cbk->cb_statistics.cs_ignored++;
			break;
		}

		options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		option_nr = 2;
		break;
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_cont = (uuid_t *)&ccr->ccr_uuid;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects orphan container "DF_UUIDF"/"DF_UUIDF,
		 DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid));
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects orphan container "
		 DF_UUIDF"/"DF_UUIDF", action %u (%s), handle_rc %d, report_rc %d, decision %d\n",
		 DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid), act,
		 option_nr ? "need interact" : "no interact", result, rc, decision);

	if (rc != 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		result = rc;
	}

	if (result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;

	switch (decision) {
	default:
		D_ERROR(DF_ENGINE" got invalid decision %d for orphan container "
			DF_UUIDF"/"DF_UUIDF". Ignore the inconsistency.\n",
			DP_ENGINE(ins), decision, DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid));
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
			result = ds_cont_destroy_orphan(svc, ccr->ccr_uuid);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	}

	goto report;

out:
	/* XXX: For orphan container, mark it as 'skip' since we do not support to add it back. */
	ccr->ccr_skip = 1;

	chk_engine_post_repair(ins, &result);

	return result;
}

static daos_prop_t *
chk_engine_build_label_prop(d_iov_t *label)
{
	daos_prop_t	*prop;

	prop = daos_prop_alloc(1);
	if (prop != NULL) {
		prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
		D_STRNDUP(prop->dpp_entries[0].dpe_str, label->iov_buf, label->iov_len);
		if (prop->dpp_entries[0].dpe_str == NULL) {
			daos_prop_free(prop);
			prop = NULL;
		}
	}

	return prop;
}

static int
chk_engine_cont_set_label(struct chk_pool_rec *cpr, struct chk_cont_rec *ccr,
			  struct cont_svc *svc, d_iov_t *label)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	daos_prop_t			*prop_in = NULL;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_CONT_BAD_LABEL;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	if (label != NULL) {
		switch (act) {
		case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
			/*
			 * If the container label in the container service (cont_svc::cs_uuids)
			 * exists but does not match the label in the container property, then
			 * trust the container service and reset the one in container property
			 * by default.
			 *
			 * Fall through.
			 */
		case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
			if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
				cbk->cb_statistics.cs_repaired++;
			} else {
				prop_in = chk_engine_build_label_prop(label);
				if (prop_in == NULL)
					D_GOTO(out, result = -DER_NOMEM);

				result = ds_cont_set_label(svc, ccr->ccr_uuid, prop_in, false);
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
				cbk->cb_statistics.cs_ignored++;
				break;
			}

			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			option_nr = 2;
			break;
		}
	} else {
		switch (act) {
		case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
			/*
			 * If the container label in the container service (cont_svc::cs_uuids)
			 * does not exists, but the one in the container property is there, then
			 * trust the label in container property and add it to container service
			 * by default.
			 *
			 * Fall through.
			 */
		case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
			if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
				cbk->cb_statistics.cs_repaired++;
			} else {
				result = ds_cont_set_label(svc, ccr->ccr_uuid,
							   ccr->ccr_label_prop, true);
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
				cbk->cb_statistics.cs_ignored++;
				break;
			}

			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			option_nr = 2;
			break;
		}
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = option_nr != 0 ? CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT : act;
	cru.cru_rank = dss_self_rank();
	cru.cru_option_nr = option_nr;
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_cont = (uuid_t *)&ccr->ccr_uuid;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects inconsistent container label: SVC %s vs property %s",
		 label != NULL ? (char *)label->iov_buf : "(null)", ccr->ccr_label_prop != NULL ?
		 (char *)ccr->ccr_label_prop->dpp_entries[0].dpe_str : "(null)");
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects inconsistent container label for "DF_UUIDF"/"DF_UUIDF
		 ": %s vs %s, action %u (%s), handle_rc %d, report_rc %d, decision %d\n",
		 DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid),
		 label != NULL ? (char *)label->iov_buf : "(null)", ccr->ccr_label_prop != NULL ?
		 (char *)ccr->ccr_label_prop->dpp_entries[0].dpe_str : "(null)", act,
		 option_nr ? "need interact" : "no interact", result, rc, decision);

	if (rc != 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		result = rc;
	}

	if (result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;

	switch (decision) {

ignore:
	default:
		D_ERROR(DF_ENGINE" got invalid decision %d for inconsistent container label for "
			DF_UUIDF"/"DF_UUIDF". Ignore the inconsistency.\n",
			DP_ENGINE(ins), decision, DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid));
		/*
		 * Invalid option, ignore the inconsistency.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		cbk->cb_statistics.cs_ignored++;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		if (label == NULL)
			goto ignore;

		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
		if (!(prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN)) {
			prop_in = chk_engine_build_label_prop(label);
			if (prop_in == NULL)
				D_GOTO(out, result = -DER_NOMEM);
		}

		/* Fall through. */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
		act = decision;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			result = ds_cont_set_label(svc, ccr->ccr_uuid,
						   prop_in != NULL ? prop_in : ccr->ccr_label_prop,
						   prop_in != NULL ? false : true);
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
	 * It is not fatal even if failed to repair inconsistent container label,
	 * then do not skip current container for subsequent DAOS check.
	 */

	if (prop_in != NULL)
		daos_prop_free(prop_in);

	chk_engine_post_repair(ins, &result);

	return result;
}

static int
chk_engine_cont_label_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *arg)
{
	struct chk_cont_label_cb_args	*cclca = arg;
	struct chk_cont_rec		*ccr;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;

	d_iov_set(&kiov, val->iov_buf, val->iov_len);
	d_iov_set(&riov, NULL, 0);
	rc = dbtree_lookup(cclca->cclca_aggregator->ccla_toh, &kiov, &riov);
	if (rc != 0)
		/*
		 * The container only exists in the container service RDB, but not on
		 * any pool shard yet. It will be created on related pool shards when
		 * be opened next time.
		 */
		D_GOTO(out, rc = (rc == -DER_NONEXIST ? 0 : rc));

	ccr = riov.iov_buf;
	if (ccr->ccr_label_prop == NULL ||
	    strncmp(key->iov_buf, ccr->ccr_label_prop->dpp_entries[0].dpe_str,
		    DAOS_PROP_LABEL_MAX_LEN) != 0)
		rc = daos_iov_copy(&ccr->ccr_label_ps, key);
	else
		ccr->ccr_label_checked = 1;

out:
	if (rc != 0 && cclca->cclca_cpr->cpr_ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT)
		return rc;

	return 0;
}

static int
chk_engine_cont_cleanup(struct chk_pool_rec *cpr, struct ds_pool_svc *ds_svc,
			struct chk_cont_list_aggregator *aggregator)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct cont_svc			*svc;
	struct chk_cont_rec		*ccr;
	d_iov_t				*label;
	struct chk_cont_label_cb_args	 cclca = { 0 };
	int				 rc = 0;
	bool				 failout;

	if (ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT)
		failout = true;
	else
		failout = false;
	svc = ds_pool_ps2cs(ds_svc);

	d_list_for_each_entry(ccr, &aggregator->ccla_list, ccr_link) {
		rc = ds_cont_existence_check(svc, ccr->ccr_uuid, &ccr->ccr_label_prop);
		if (rc == 0)
			continue;

		if (rc != -DER_NONEXIST) {
			D_CDEBUG(failout, DLOG_ERR, DLOG_DBG,
				 DF_ENGINE" on rank %u failed to check container "
				 DF_UUIDF"/"DF_UUIDF": "DF_RC"\n", DP_ENGINE(ins),
				 dss_self_rank(), DP_UUID(cpr->cpr_uuid),
				 DP_UUID(ccr->ccr_uuid), DP_RC(rc));

			if (failout)
				goto out;

			ccr->ccr_skip = 1;
			continue;
		}

		rc = chk_engine_cont_orphan(cpr, ccr, svc);
		if (rc != 0)
			goto out;
	}

	cclca.cclca_aggregator = aggregator;
	cclca.cclca_svc = svc;
	cclca.cclca_cpr = cpr;
	rc = ds_cont_iterate_labels(svc, chk_engine_cont_label_cb, &cclca);
	if (rc != 0)
		goto out;

	d_list_for_each_entry(ccr, &aggregator->ccla_list, ccr_link) {
		if (!ccr->ccr_label_checked && ccr->ccr_label_prop != NULL) {
			if (daos_iov_empty(&ccr->ccr_label_ps))
				label = NULL;
			else
				label = &ccr->ccr_label_ps;
			rc = chk_engine_cont_set_label(cpr, ccr, svc, label);
			if (rc != 0)
				goto out;
		}
	}

out:
	return rc;
}

static void
chk_engine_pool_ult(void *args)
{
	struct chk_cont_list_aggregator	 aggregator = { 0 };
	struct chk_pool_rec		*cpr = args;
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct ds_pool_svc		*svc = NULL;
	struct pool_map			*map = NULL;
	struct ds_pool			*pool = NULL;
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	int				 i;
	int				 rc = 0;
	int				 rc1 = 0;
	int				 rc2;

	uuid_unparse_lower(cpr->cpr_uuid, uuid_str);

	rc = ds_pool_svc_lookup_leader(cpr->cpr_uuid, &svc, NULL);
	if (rc != 0)
		/*
		 * XXX: Before the phase of CHK__CHECK_SCAN_PHASE__CSP_OBJ_SCRUB, the PS
		 *	leader drives the check on engine. Current one is not PS leader.
		 */
		D_GOTO(out, rc = 0);

	ABT_mutex_lock(cpr->cpr_mutex);

again:
	if (cpr->cpr_stop || !ins->ci_sched_running) {
		ABT_mutex_unlock(cpr->cpr_mutex);
		goto out;
	}

	if (cpr->cpr_mbs == NULL) {
		ABT_cond_wait(cpr->cpr_cond, cpr->cpr_mutex);
		goto again;
	}

	ABT_mutex_unlock(cpr->cpr_mutex);

	/*
	 * This engine maybe not PS leader in former run, at time time,
	 * its phase will be marked as 'DONE'. But after check restart,
	 * it becomes new PS leader, let's re-scan the pool/containers.
	 */
	if (cbk->cb_phase > CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP &&
	    cbk->cb_phase != CHK__CHECK_SCAN_PHASE__DSP_DONE)
		goto cont;

	cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP;
	/* QUEST: How to estimate the left time? */
	cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	rc2 = chk_bk_update_pool(cbk, uuid_str);
	if (rc2 != 0)
		D_WARN("Failed to update pool bookmark (2) for %s: "DF_RC"\n",
		       uuid_str, DP_RC(rc2));

	rc = ds_pool_svc_load_map(svc, &map);
	if (rc != 0)
		goto out;

	for (i = 0; i < cpr->cpr_shard_nr; i++) {
		rc = chk_engine_pool_mbs_one(cpr, map, &cpr->cpr_mbs[i]);
		if (rc != 0)
			break;
	}

	/* Lookup for dangling entry in the pool map. */
	if (rc == 0)
		rc = chk_engine_find_dangling_pm(cpr, map);

	if (rc == 0 && cpr->cpr_delay_label)
		rc = chk_engine_bad_pool_label(cpr, svc);

	if (cpr->cpr_dirty) {
		cpr->cpr_dirty = 0;

		rc2 = chk_bk_update_pool(cbk, uuid_str);
		if (rc2 != 0)
			D_WARN("Failed to update pool bookmark (3) for %s: "DF_RC"\n",
			       uuid_str, DP_RC(rc2));

		/*
		 * Flush the pool map to persistent storage (if not under dryrun mode)
		 * and distribute the pool map to other pool shards.
		 */
		rc1 = ds_pool_svc_flush_map(svc, map);
	}

	/*
	 * Cleanup all old connections. It is no matter even if we cannot evict some
	 * old connections. That is also independent from former check phases result.
	 */
	ds_pool_svc_evict_all(svc);

	if (cpr->cpr_skip)
		goto out;

cont:
	cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST;
	/* QUEST: How to estimate the left time? */
	cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	rc2 = chk_bk_update_pool(cbk, uuid_str);
	if (rc2 != 0)
		D_WARN("Failed to update pool bookmark (4) for %s: "DF_RC"\n",
		       uuid_str, DP_RC(rc2));

	rc = chk_engine_cont_list_init(cpr->cpr_uuid, &aggregator);
	if (rc != 0)
		goto out;

	pool = ds_pool_svc2pool(svc);
	/* Collect containers from pool shards. */
	rc = chk_cont_list_remote(pool, cbk->cb_gen, chk_engine_cont_list_remote_cb, &aggregator);
	if (rc != 0)
		goto out;

	cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP;
	/* QUEST: How to estimate the left time? */
	cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	rc2 = chk_bk_update_pool(cbk, uuid_str);
	if (rc2 != 0)
		D_WARN("Failed to update pool bookmark (5) for %s: "DF_RC"\n",
		       uuid_str, DP_RC(rc2));

	rc = chk_engine_cont_cleanup(cpr, svc, &aggregator);

out:
	chk_engine_cont_list_fini(&aggregator);

	if (map != NULL)
		pool_map_decref(map);
	ds_pool_svc_put_leader(svc);

	cpr->cpr_done = 1;
	cbk->cb_phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;
	if (rc != 0 || rc1 != 0)
		cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_FAILED;
	else
		cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;
	cbk->cb_time.ct_stop_time = time(NULL);
	rc2 = chk_bk_update_pool(cbk, uuid_str);
	if (rc2 != 0)
		D_WARN("Failed to update pool bookmark (6) for %s: "DF_RC"\n",
		       uuid_str, DP_RC(rc2));

	chk_pool_put(cpr);
}

static void
chk_engine_sched(void *args)
{
	struct chk_instance	*ins = args;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_bookmark	*cpr_bk;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*pool_cbk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	uuid_t			 uuid;
	uint32_t		 phase;
	uint32_t		 ins_status;
	uint32_t		 pool_status;
	d_rank_t		 myrank = dss_self_rank();
	bool			 done = false;
	int			 rc = 0;
	int			 rc1;

	D_INFO(DF_ENGINE" on rank %u start at the phase %u\n",
	       DP_ENGINE(ins), myrank, cbk->cb_phase);

	if (cbk->cb_phase > CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST) {
		rc = chk_engine_setup_pools(ins);
		if (rc != 0)
			goto out;
	}

	while (ins->ci_sched_running) {
		switch (cbk->cb_phase) {
		case CHK__CHECK_SCAN_PHASE__CSP_PREPARE:
			/*
			 * In this phase, the engine has already offer its known pools' svc list
			 * to the leader via CHK_START RPC reply. The leader will notify engines
			 * to go ahead after chk_leader_handle_pools_p1().
			 */
			/* Fall through to share code. */
		case CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST:
			D_INFO(DF_ENGINE" on rank %u moves to phase %u\n",
			       DP_ENGINE(ins), myrank, cbk->cb_phase);
			/*
			 * Check leader has already done chk_leader_handle_pools_p1(), then engine
			 * needs to setup pool module without pool service. And then notify leader
			 * to start PS on specifiedreplica via chk_leader_handle_pools_p2(). After
			 * that, check leader will notify engines to go ahead.
			 */
			ABT_mutex_lock(ins->ci_abt_mutex);
			if (!ins->ci_sched_running) {
				ABT_mutex_unlock(ins->ci_abt_mutex);
				goto out;
			}

			if (d_list_empty(&ins->ci_pool_list)) {
				ABT_mutex_unlock(ins->ci_abt_mutex);
				D_GOTO(out, rc = 1);
			}

			ABT_cond_wait(ins->ci_abt_cond, ins->ci_abt_mutex);
			ABT_mutex_unlock(ins->ci_abt_mutex);

			/* XXX: How to estimate the left time? */
			cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
			chk_bk_update_engine(cbk);

			break;
		case CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS:
			d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
				D_ASSERT(cpr->cpr_thread == ABT_THREAD_NULL);

				chk_pool_get(cpr);
				cpr_bk = &cpr->cpr_bk;
				/*
				 * This engine maybe not PS leader in former run, at time time,
				 * its phase will be marked as 'DONE'. But after check restart,
				 * it becomes new PS leader, let's re-scan the pool/containers.
				 */
				if (cpr_bk->cb_phase < CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS ||
				    cpr_bk->cb_phase == CHK__CHECK_SCAN_PHASE__DSP_DONE) {
					cpr_bk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS;
					/* QUEST: How to estimate the left time? */
					cpr_bk->cb_time.ct_left_time =
						CHK__CHECK_SCAN_PHASE__DSP_DONE - cpr_bk->cb_phase;
					uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
					rc1 = chk_bk_update_pool(cbk, uuid_str);
					if (rc1 != 0)
						D_WARN("Failed to update pool bookmark (7) for %s: "
						       DF_RC"\n", uuid_str, DP_RC(rc1));
				}

				rc = dss_ult_create(chk_engine_pool_ult, cpr, DSS_XS_SYS, 0,
						    DSS_DEEP_STACK_SZ, &cpr->cpr_thread);
				if (rc != 0) {
					rc = dss_abterr2der(rc);
					pool_cbk = &cpr->cpr_bk;
					uuid_copy(uuid, cpr->cpr_uuid);
					ins->ci_slowest_fail_phase = pool_cbk->cb_phase;
					chk_pool_stop_one(ins, uuid,
							  CHK__CHECK_POOL_STATUS__CPS_FAILED,
							  false, false, NULL);
					chk_pool_put(cpr);
					if (prop->cp_flags & CHK__CHECK_FLAG__CF_FAILOUT) {
						D_ERROR(DF_ENGINE" on rank %u failed to "
							"create ULT for pool "DF_UUIDF": "
							DF_RC". Failout.\n", DP_ENGINE(ins),
							myrank, DP_UUID(uuid), DP_RC(rc));
						goto out;
					}

					D_ERROR(DF_ENGINE" on rank %u failed to create "
						"ULT for pool "DF_UUIDF": "DF_RC". Continue.\n",
						DP_ENGINE(ins), myrank, DP_UUID(uuid), DP_RC(rc));
					rc = 0;
				}
			}

			/* Fall through. */
		case CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP:
		case CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST:
		case CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP:
			D_INFO(DF_ENGINE" on rank %u moves to phase %u\n",
			       DP_ENGINE(ins), myrank, cbk->cb_phase);

			do {
				dss_sleep(300);

				/* Someone wants to stop the check. */
				if (!ins->ci_sched_running)
					D_GOTO(out, rc = 0);

				if (d_list_empty(&ins->ci_pool_list))
					D_GOTO(out, rc = 1);

				phase = chk_engine_find_slowest(ins, &done);
				if (phase > cbk->cb_phase) {
					cbk->cb_phase = phase;
					/* XXX: How to estimate the left time? */
					cbk->cb_time.ct_left_time =
						CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
					chk_bk_update_engine(cbk);
				}
			} while (ins->ci_sched_running && !done);

			break;
		case CHK__CHECK_SCAN_PHASE__CSP_DTX_RESYNC:
		case CHK__CHECK_SCAN_PHASE__CSP_OBJ_SCRUB:
		case CHK__CHECK_SCAN_PHASE__CSP_REBUILD:
		case CHK__CHECK_SCAN_PHASE__OSP_AGGREGATION:
			/* XXX: These phases will be implemented in the future. */
			D_ASSERT(0);
			break;
		case CHK__CHECK_SCAN_PHASE__DSP_DONE:
			D_INFO(DF_ENGINE" on rank %u has done\n", DP_ENGINE(ins), myrank);

			D_GOTO(out, rc = 1);
		default:
			D_ASSERT(0);
			goto out;
		}
	}

out:
	if (rc > 0) {
		/* If failed to check some pool(s), then the engine will be marked as 'failed'. */
		if (ins->ci_slowest_fail_phase != CHK_INVAL_PHASE)
			ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		else
			ins_status = CHK__CHECK_INST_STATUS__CIS_COMPLETED;
		pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;
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

	/* The pool scan ULTs will be terminated via chk_engine_exit(). */
	chk_engine_exit(ins, ins_status, pool_status);

	D_INFO(DF_ENGINE" on rank %u exit at the phase %u with ins_status %u rc: "DF_RC"\n",
	       DP_ENGINE(ins), myrank, cbk->cb_phase, cbk->cb_ins_status, DP_RC(rc));

	/*
	 * The engine scheduler may exit for its own reason (instead of by CHK_STOP),
	 * then reset ci_sched_running to avoid blocking next CHK_START.
	 */
	ins->ci_sched_running = 0;
}

static int
chk_engine_start_prepare(struct chk_instance *ins, uint32_t rank_nr, d_rank_t *ranks,
			 uint32_t policy_nr, struct chk_policy *policies,
			 int pool_nr, uuid_t pools[], uint64_t gen, int phase,
			 uint32_t flags, d_rank_t leader, d_rank_list_t **rlist)
{
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_property	*prop = &ins->ci_prop;
	bool			 reset = (flags & CHK__CHECK_FLAG__CF_RESET) ? true : false;
	int			 rc = 0;
	int			 i;
	int			 j;

	/*
	 * XXX: Currently we cannot distinguish whether it is caused by resent start request
	 *	or not. That can be resolved via introducing new RPC sequence in the future.
	 */
	if (ins->ci_sched_running)
		D_GOTO(out, rc = -DER_ALREADY);

	/* Corrupted bookmark or new created one. */
	if (cbk->cb_magic != CHK_BK_MAGIC_ENGINE) {
		if (!reset)
			D_GOTO(out, rc = -DER_NOT_RESUME);

		if (!chk_is_on_leader(gen, leader, true))
			memset(prop, 0, sizeof(*prop));

		memset(cbk, 0, sizeof(*cbk));
		cbk->cb_magic = CHK_BK_MAGIC_ENGINE;
		cbk->cb_version = DAOS_CHK_VERSION;
		flags |= CHK__CHECK_FLAG__CF_RESET;
		goto init;
	}

	if (cbk->cb_gen > gen)
		D_GOTO(out, rc = -DER_EP_OLD);

	/*
	 * XXX: Leader wants to resume the check but with different generation, then this
	 *	engine must be new joined one for current check instance. Under such case
	 *	we have to restart the scan from scratch.
	 */
	if (cbk->cb_gen != gen && !reset)
		D_GOTO(out, rc = -DER_NOT_RESUME);

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_ALREADY);

	if (reset)
		goto init;

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_COMPLETED)
		D_GOTO(out, rc = 1);

	/* For dryrun mode, restart from the beginning since we did not record former repairing. */
	if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
		if (!reset)
			D_GOTO(out, rc = -DER_NOT_RESUME);

		goto init;
	}

	/*
	 * XXX: If current rank list does not matches the former list, the we need to
	 *	reset the check from scratch. Currently, we do not strictly check that.
	 *	It is control plane's duty to generate valid rank list.
	 */

	/* Add new rank(s), need to reset. */
	if (rank_nr > prop->cp_rank_nr) {
		if (!reset)
			D_GOTO(out, rc = -DER_NOT_RESUME);

		goto init;
	}

	if (prop->cp_pool_nr < 0)
		goto init;

	/* Want to check new pool(s), need to reset. */
	if (pool_nr < 0) {
		if (!reset)
			D_GOTO(out, rc = -DER_NOT_RESUME);

		goto init;
	}

	for (i = 0; i < pool_nr; i++) {
		for (j = 0; j < prop->cp_pool_nr; j++) {
			if (uuid_compare(pools[i], prop->cp_pools[j]) == 0)
				break;
		}

		/* Want to check new pool(s), need to reset. */
		if (j == prop->cp_pool_nr) {
			if (!reset)
				D_GOTO(out, rc = -DER_NOT_RESUME);

			goto init;
		}
	}

init:
	if (reset) {
		ins->ci_slowest_fail_phase = CHK_INVAL_PHASE;
		cbk->cb_gen = gen;
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
		memset(&cbk->cb_statistics, 0, sizeof(cbk->cb_statistics));
	}

	if (chk_is_on_leader(gen, leader, true)) {
		/* The check leader has already verified the rank list. */
		if (rank_nr != 0)
			*rlist = uint32_array_to_rank_list(ranks, rank_nr);
		else
			rc = chk_prop_fetch(prop, rlist);
	} else {
		rc = chk_prop_prepare(rank_nr, ranks, policy_nr, policies, pool_nr,
				      pools, flags, phase, leader, prop, rlist);
	}

out:
	return rc;
}

/* Remove all old pool bookmarks. */
static int
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

	if (cbk.cb_gen >= ctpa->ctpa_gen)
		D_GOTO(out, rc = 0);

	rc = chk_bk_delete_pool(uuid_str);

out:
	return rc;
}

static int
chk_pool_start_one(struct chk_instance *ins, uuid_t uuid, uint64_t gen)
{
	struct chk_bookmark	cbk;
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			rc;

	uuid_unparse_lower(uuid, uuid_str);
	rc = chk_bk_fetch_pool(&cbk, uuid_str);
	if (rc != 0 && rc != -DER_NONEXIST)
		goto out;

	if (cbk.cb_magic != CHK_BK_MAGIC_POOL) {
		cbk.cb_magic = CHK_BK_MAGIC_POOL;
		cbk.cb_version = DAOS_CHK_VERSION;
		cbk.cb_gen = gen;
		cbk.cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
	} else if (cbk.cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_FAILED) {
		if (cbk.cb_phase < ins->ci_slowest_fail_phase)
			ins->ci_slowest_fail_phase = cbk.cb_phase;
	}

	/* Always refresh the start time. */
	cbk.cb_time.ct_start_time = time(NULL);
	/* XXX: How to estimate the left time? */
	cbk.cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk.cb_phase;
	cbk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid, dss_self_rank(),
				&cbk, ins, NULL, NULL, NULL);
	if (rc != 0)
		goto out;

	rc = chk_bk_update_pool(&cbk, uuid_str);
	if (rc != 0)
		chk_pool_del_shard(ins->ci_pool_hdl, uuid, dss_self_rank());

out:
	return rc;
}

static int
chk_pools_add_from_dir(uuid_t uuid, void *args)
{
	struct chk_traverse_pools_args	*ctpa = args;
	struct chk_instance		*ins = ctpa->ctpa_ins;

	return chk_pool_start_one(ins, uuid, ctpa->ctpa_gen);
}

static int
chk_pools_add_from_db(struct sys_db *db, char *table, d_iov_t *key, void *args)
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

	if (cbk.cb_gen != ctpa->ctpa_gen)
		D_GOTO(out, rc = 0);

	if (cbk.cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_FAILED) {
		if (cbk.cb_phase < ins->ci_slowest_fail_phase)
			ins->ci_slowest_fail_phase = cbk.cb_phase;
	}

	uuid_parse(uuid_str, uuid);

	/* Always refresh the start time. */
	cbk.cb_time.ct_start_time = time(NULL);
	/* QUEST: How to estimate the left time? */
	cbk.cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk.cb_phase;
	cbk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid,
				dss_self_rank(), &cbk, ins, NULL, NULL, NULL);
	if (rc != 0)
		goto out;

	rc = chk_bk_update_pool(&cbk, uuid_str);
	if (rc != 0)
		chk_pool_del_shard(ctpa->ctpa_ins->ci_pool_hdl, uuid, dss_self_rank());

out:
	return rc;
}

int
chk_engine_start(uint64_t gen, uint32_t rank_nr, d_rank_t *ranks,
		 uint32_t policy_nr, struct chk_policy *policies, int pool_nr,
		 uuid_t pools[], uint32_t flags, int exp_phase, d_rank_t leader,
		 uint32_t *cur_phase, struct ds_pool_clues *clues)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_property		*prop = &ins->ci_prop;
	d_rank_list_t			*rank_list = NULL;
	struct chk_pool_rec		*cpr;
	struct chk_pool_rec		*tmp;
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_pool_filter_args	 cpfa = { 0 };
	struct umem_attr		 uma = { 0 };
	uuid_t				 dummy_pool;
	d_rank_t			 myrank = dss_self_rank();
	int				 rc;
	int				 i;

	if (ins->ci_starting)
		D_GOTO(out_log, rc = -DER_INPROGRESS);

	if (ins->ci_stopping)
		D_GOTO(out_log, rc = -DER_BUSY);

	ins->ci_starting = 1;

	rc = chk_engine_start_prepare(ins, rank_nr, ranks, policy_nr, policies,
				      pool_nr, pools, gen, exp_phase, flags, leader, &rank_list);
	if (rc != 0)
		goto out_log;

	D_ASSERT(rank_list != NULL);
	D_ASSERT(d_list_empty(&ins->ci_pool_list));
	D_ASSERT(d_list_empty(&ins->ci_pending_list));

	if (ins->ci_sched != ABT_THREAD_NULL)
		ABT_thread_free(&ins->ci_sched);

	chk_iv_ns_cleanup(&ins->ci_iv_ns);

	if (chk_is_on_leader(gen, leader, true)) {
		ins->ci_iv_ns = chk_leader_get_iv_ns();
		if (unlikely(ins->ci_iv_ns == NULL))
			goto out_log;
	} else {
		if (ins->ci_iv_group != NULL) {
			crt_group_secondary_destroy(ins->ci_iv_group);
			ins->ci_iv_group = NULL;
		}

		rc = crt_group_secondary_create(CHK_DUMMY_POOL, NULL, rank_list, &ins->ci_iv_group);
		if (rc != 0)
			goto out_log;

		uuid_parse(CHK_DUMMY_POOL, dummy_pool);
		rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, dummy_pool, ins->ci_iv_group,
				     &ins->ci_iv_id, &ins->ci_iv_ns);
		if (rc != 0)
			goto out_group;

		ds_iv_ns_update(ins->ci_iv_ns, leader);
	}

	uma.uma_id = UMEM_CLASS_VMEM;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pool_btr, &ins->ci_pool_hdl);
	if (rc != 0)
		goto out_iv;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_PA, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pending_btr, &ins->ci_pending_hdl);
	if (rc != 0)
		goto out_tree;

	if (prop->cp_pool_nr <= 0)
		ins->ci_all_pools = 1;
	else
		ins->ci_all_pools = 0;

	if (flags & CHK__CHECK_FLAG__CF_RESET) {
		ctpa.ctpa_gen = cbk->cb_gen;
		rc = chk_traverse_pools(chk_pools_cleanup_cb, &ctpa);
		if (rc != 0)
			goto out_tree;

		ctpa.ctpa_gen = cbk->cb_gen;
		ctpa.ctpa_ins = ins;

		rc = ds_mgmt_tgt_pool_iterate(chk_pools_add_from_dir, &ctpa);
		if (rc != 0)
			goto out_pool;

		rc = ds_mgmt_newborn_pool_iterate(chk_pools_add_from_dir, &ctpa);
		if (rc != 0)
			goto out_pool;

		rc = ds_mgmt_zombie_pool_iterate(chk_pools_add_from_dir, &ctpa);
		if (rc != 0)
			goto out_pool;

		*cur_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
	} else {
		if (ins->ci_all_pools) {
			ctpa.ctpa_gen = cbk->cb_gen;
			ctpa.ctpa_ins = ins;
			rc = chk_traverse_pools(chk_pools_add_from_db, &ctpa);
			if (rc != 0)
				goto out_pool;
		} else {
			for (i = 0; i < pool_nr; i++) {
				rc = ds_mgmt_pool_exist(pools[i]);
				if (rc < 0)
					goto out_pool;

				if (rc > 0) {
					rc = chk_pool_start_one(ins, pools[i], cbk->cb_gen);
					if (rc != 0)
						goto out_pool;
				}
			}
		}

		*cur_phase = chk_engine_find_slowest(ins, NULL);
	}

	cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;
	cbk->cb_phase = *cur_phase;
	/* Always refresh the start time. */
	cbk->cb_time.ct_start_time = time(NULL);
	/* XXX: How to estimate the left time? */
	cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	rc = chk_bk_update_engine(cbk);
	if (rc != 0)
		goto out_pool;

	if (cbk->cb_phase <= CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS) {
		cpfa.cpfa_pool_hdl = ins->ci_pool_hdl;
		rc = ds_pool_clues_init(chk_pool_filter, &cpfa, clues);
		if (rc != 0)
			goto out_bk;
	}

	ins->ci_sched_running = 1;

	rc = dss_ult_create(chk_engine_sched, ins, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &ins->ci_sched);
	if (rc != 0) {
		ins->ci_sched_running = 0;
		goto out_bk;
	}

	goto out_log;

out_bk:
	if (rc != -DER_ALREADY && cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_time.ct_stop_time = time(NULL);
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		chk_bk_update_engine(cbk);
	}
out_pool:
	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link)
		chk_pool_stop_one(ins, cpr->cpr_uuid,
				  CHK__CHECK_POOL_STATUS__CPS_IMPLICATED, false, false, NULL);
out_tree:
	chk_destroy_pending_tree(ins);
	chk_destroy_pool_tree(ins);
out_iv:
	chk_iv_ns_cleanup(&ins->ci_iv_ns);
out_group:
	if (ins->ci_iv_group != NULL) {
		crt_group_secondary_destroy(ins->ci_iv_group);
		ins->ci_iv_group = NULL;
	}
out_log:
	ins->ci_starting = 0;

	if (rc == 0) {
		D_INFO(DF_ENGINE" started on rank %u with %u ranks, %d pools, "
		       "flags %x, phase %d, leader %u\n",
		       DP_ENGINE(ins), myrank, rank_nr, pool_nr, flags, exp_phase, leader);

		chk_ranks_dump(rank_list->rl_nr, rank_list->rl_ranks);

		if (pool_nr > 0)
			chk_pools_dump(pool_nr, pools);
		else if (prop->cp_pool_nr > 0)
			chk_pools_dump(prop->cp_pool_nr, prop->cp_pools);
	} else if (rc > 0) {
		*cur_phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;
	} else if (rc != -DER_ALREADY) {
		D_ERROR(DF_ENGINE" failed to start on rank %u with %u ranks, %d pools, flags %x, "
			"phase %d, leader %u, gen "DF_X64": "DF_RC"\n", DP_ENGINE(ins), myrank,
			rank_nr, pool_nr, flags, exp_phase, leader, gen, DP_RC(rc));
	}

	d_rank_list_free(rank_list);

	return rc;
}

int
chk_engine_stop(uint64_t gen, int pool_nr, uuid_t pools[])
{
	struct chk_instance	*ins = chk_engine;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	int			 rc = 0;
	int			 i;

	if (cbk->cb_magic != CHK_BK_MAGIC_ENGINE || cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (ins->ci_starting)
		D_GOTO(out, rc = -DER_BUSY);

	if (ins->ci_stopping)
		D_GOTO(out, rc = -DER_INPROGRESS);

	ins->ci_stopping = 1;

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_ALREADY);

	if (pool_nr == 0) {
		d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
			chk_pool_stop_one(ins, cpr->cpr_uuid,
					  CHK__CHECK_POOL_STATUS__CPS_STOPPED, false, true, &rc);
			if (rc != 0)
				D_GOTO(out, rc = (rc == -DER_NO_HDL ? 0 : rc));
		}
	} else {
		for (i = 0; i < pool_nr; i++) {
			chk_pool_stop_one(ins, pools[i],
					  CHK__CHECK_POOL_STATUS__CPS_STOPPED, false, true, &rc);
			if (rc != 0)
				D_GOTO(out, rc = (rc == -DER_NO_HDL ? 0 : rc));
		}
	}

	if (d_list_empty(&ins->ci_pool_list)) {
		chk_stop_sched(ins);
		/* To indicate that there is no active pool(s) on this rank. */
		rc = 1;
	}

out:
	ins->ci_stopping = 0;

	if (rc == 0) {
		D_INFO(DF_ENGINE" stopped on rank %u with %d pools\n",
		       DP_ENGINE(ins), dss_self_rank(), pool_nr > 0 ? pool_nr : prop->cp_pool_nr);

		if (pool_nr > 0)
			chk_pools_dump(pool_nr, pools);
		else if (prop->cp_pool_nr > 0)
			chk_pools_dump(prop->cp_pool_nr, prop->cp_pools);
	} else if (rc == -DER_ALREADY) {
		rc = 1;
	} else if (rc < 0) {
		D_ERROR(DF_ENGINE" failed to stop on rank %u with %d pools, "
			"gen "DF_X64": "DF_RC"\n", DP_ENGINE(ins), dss_self_rank(),
			pool_nr > 0 ? pool_nr : prop->cp_pool_nr, gen, DP_RC(rc));
	}

	return rc;
}

/* Query one pool shard on one xstream. */
static int
chk_engine_query_one(void *args)
{
	struct dss_coll_stream_args	*reduce = args;
	struct dss_stream_arg_type	*streams = reduce->csa_streams;
	struct chk_query_xstream_args	*cqxa;
	struct chk_query_target		*target;
	char				*path = NULL;
	daos_handle_t			 poh = DAOS_HDL_INVAL;
	vos_pool_info_t			 info;
	int				 tid = dss_get_module_info()->dmi_tgt_id;
	int				 rc;

	cqxa = streams[tid].st_arg;
	target = &cqxa->cqxa_target;
	rc = ds_mgmt_tgt_pool_exist(cqxa->cqxa_uuid,  &path);
	/* We allow the target nonexist. */
	if (rc <= 0)
		goto out;

	rc = vos_pool_open(path, cqxa->cqxa_uuid, VOS_POF_FOR_CHECK_QUERY, &poh);
	if (rc != 0) {
		D_ERROR("Failed to open vos pool "DF_UUIDF" on target %u/%d: "DF_RC"\n",
			DP_UUID(cqxa->cqxa_uuid), dss_self_rank(), tid, DP_RC(rc));
		goto out;
	}

	rc = vos_pool_query(poh, &info);
	if (rc != 0) {
		D_ERROR("Failed to query vos pool "DF_UUIDF" on target %u/%d: "DF_RC"\n",
			DP_UUID(cqxa->cqxa_uuid), dss_self_rank(), tid, DP_RC(rc));
		goto out;
	}

	target->cqt_rank = dss_self_rank();
	target->cqt_tgt = tid;
	target->cqt_ins_status = info.pif_chk.cpi_ins_status;
	target->cqt_statistics = info.pif_chk.cpi_statistics;
	target->cqt_time = info.pif_chk.cpi_time;

out:
	if (daos_handle_is_valid(poh))
		vos_pool_close(poh);
	D_FREE(path);
	return rc;
}

static void
chk_engine_query_reduce(void *a_args, void *s_args)
{
	struct chk_query_xstream_args	*aggregator = a_args;
	struct chk_query_xstream_args	*stream = s_args;
	struct chk_query_pool_shard	*shard;
	struct chk_query_target		*target;

	shard = &aggregator->cqxa_args->cqpa_shards[aggregator->cqxa_args->cqpa_idx];
	target = &shard->cqps_targets[shard->cqps_target_nr];
	*target = stream->cqxa_target;
	shard->cqps_target_nr++;
}

static int
chk_engine_query_stream_alloc(struct dss_stream_arg_type *args, void *a_arg)
{
	struct chk_query_xstream_args	*cqxa = a_arg;
	int				 rc = 0;

	D_ALLOC(args->st_arg, sizeof(struct chk_query_xstream_args));
	if (args->st_arg == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	memcpy(args->st_arg, cqxa, sizeof(struct chk_query_xstream_args));

out:
	return rc;
}

static void
chk_engine_query_stream_free(struct dss_stream_arg_type *args)
{
	D_FREE(args->st_arg);
}

static int
chk_engine_query_pool(uuid_t uuid, void *args)
{
	struct chk_query_pool_args	*cqpa = args;
	struct chk_query_pool_shard	*shard;
	struct chk_query_pool_shard	*new_shards;
	struct chk_bookmark		 cbk;
	struct chk_query_xstream_args	 cqxa = { 0 };
	struct dss_coll_args		 coll_args = { 0 };
	struct dss_coll_ops		 coll_ops;
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	int				 rc = 0;

	if (cqpa->cqpa_idx == cqpa->cqpa_cap) {
		D_REALLOC_ARRAY(new_shards, cqpa->cqpa_shards, cqpa->cqpa_cap, cqpa->cqpa_cap << 1);
		if (new_shards == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		cqpa->cqpa_shards = new_shards;
		cqpa->cqpa_cap <<= 1;
	}

	shard = &cqpa->cqpa_shards[cqpa->cqpa_idx];
	uuid_copy(shard->cqps_uuid, uuid);
	shard->cqps_rank = dss_self_rank();
	shard->cqps_target_nr = 0;

	uuid_unparse_lower(uuid, uuid_str);
	rc = chk_bk_fetch_pool(&cbk, uuid_str);
	if (rc == -DER_NONEXIST) {
		shard->cqps_status = CHK__CHECK_POOL_STATUS__CPS_UNCHECKED;
		shard->cqps_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
		memset(&shard->cqps_statistics, 0, sizeof(shard->cqps_statistics));
		memset(&shard->cqps_time, 0, sizeof(shard->cqps_time));
		shard->cqps_targets = NULL;

		D_GOTO(out, rc = 0);
	}

	if (rc != 0)
		goto out;

	D_ALLOC_ARRAY(shard->cqps_targets, dss_tgt_nr);
	if (shard->cqps_targets == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	shard->cqps_status = cbk.cb_pool_status;
	shard->cqps_phase = cbk.cb_phase;
	memcpy(&shard->cqps_statistics, &cbk.cb_statistics, sizeof(shard->cqps_statistics));
	memcpy(&shard->cqps_time, &cbk.cb_time, sizeof(shard->cqps_time));

	uuid_copy(cqxa.cqxa_uuid, uuid);
	cqxa.cqxa_args = cqpa;

	coll_args.ca_func_args = &coll_args.ca_stream_args;
	coll_args.ca_aggregator = &cqxa;

	coll_ops.co_func = chk_engine_query_one;
	coll_ops.co_reduce = chk_engine_query_reduce;
	coll_ops.co_reduce_arg_alloc = chk_engine_query_stream_alloc;
	coll_ops.co_reduce_arg_free = chk_engine_query_stream_free;

	rc = dss_task_collective_reduce(&coll_ops, &coll_args, 0);

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG,
		 DF_ENGINE" on rank %u query pool "DF_UUIDF": "DF_RC"\n",
		 DP_ENGINE(cqpa->cqpa_ins), dss_self_rank(), DP_UUID(uuid), DP_RC(rc));
	return rc;
}

int
chk_engine_query(uint64_t gen, int pool_nr, uuid_t pools[],
		 uint32_t *shard_nr, struct chk_query_pool_shard **shards)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_query_pool_args	 cqpa = { 0 };
	int				 rc = 0;
	int				 i;

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	cqpa.cqpa_ins = ins;
	cqpa.cqpa_cap = 2;
	cqpa.cqpa_idx = 0;
	D_ALLOC_ARRAY(cqpa.cqpa_shards, cqpa.cqpa_cap);
	if (cqpa.cqpa_shards == NULL)
		D_GOTO(log, rc = -DER_NOMEM);

	if (pool_nr == 0) {
		rc = ds_mgmt_tgt_pool_iterate(chk_engine_query_pool, &cqpa);
	} else {
		for (i = 0; i < pool_nr; i++) {
			rc = chk_engine_query_pool(pools[i], &cqpa);
			if (rc != 0)
				goto log;
		}
	}

log:
	if (rc != 0) {
		chk_query_free(cqpa.cqpa_shards, cqpa.cqpa_idx);
	} else {
		*shards = cqpa.cqpa_shards;
		*shard_nr = cqpa.cqpa_idx;
	}

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG,
		 DF_ENGINE" on rank %u handle query for %d pools: "DF_RC"\n",
		 DP_ENGINE(ins), dss_self_rank(), pool_nr, DP_RC(rc));

out:
	return rc;
}

int
chk_engine_mark_rank_dead(uint64_t gen, d_rank_t rank, uint32_t version)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	d_rank_list_t		*rank_list = NULL;
	int			 rc = 0;

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	rc = chk_prop_fetch(prop, &rank_list);
	if (rc != 0)
		goto out;

	D_ASSERT(rank_list != NULL);

	if (!chk_remove_rank_from_list(rank_list, rank))
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	prop->cp_rank_nr--;
	rc = chk_prop_update(prop, rank_list);
	if (rc != 0)
		goto out;

	/*
	 * NOTE: If the rank dead before DAOS check start, then subsequent check start will
	 *	 get failure, and then the control plane needs to decide whether or not to
	 *	 exclude the dead rank from the system and re-run DAOS check.
	 *
	 *	 If the rank dead at (or before) CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST, then
	 *	 related PS leader(s) will know that when list the containers, and then mark
	 *	 related pool(s) as 'failed'.
	 *
	 *	 If the rank dead after CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST, then if such
	 *	 rank is not involved in subsequent DAOS check, then no affect for current
	 *	 check instance; otherwise, related pool(s) will be marked as 'failed' when
	 *	 try ro access something on the dead rank.
	 *
	 *	 So here, it is not ncessary to find out the affected pools and fail them
	 *	 immediately when the death event is reported, instead, it will be handled
	 *	 sometime later as the DAOS check going.
	 */

	rc = crt_group_secondary_modify(ins->ci_iv_group, rank_list, rank_list,
					CRT_GROUP_MOD_OP_REPLACE, version);

out:
	d_rank_list_free(rank_list);
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" on rank %u mark rank %u as dead with gen "
			 DF_X64", version %u: "DF_RC"\n",
			 DP_ENGINE(ins), dss_self_rank(), rank, gen, version, DP_RC(rc));

	return rc;
}

int
chk_engine_act(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, uint32_t flags)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pending_rec	*cpr = NULL;
	int			 rc;

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (unlikely(cla >= CHK_POLICY_MAX)) {
		D_ERROR("Invalid DAOS inconsistency class %u\n", cla);
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* The admin may input the wrong option, not acceptable. */
	if (unlikely(act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT)) {
		D_ERROR("%u is not acceptable for interaction decision.\n", cla);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = chk_pending_del(ins, seq, &cpr);
	if (rc == 0) {
		/* The cpr will be destroyed by the waiter via chk_engine_report(). */
		D_ASSERT(cpr->cpr_busy == 1);

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

	if (rc != 0 || !(flags & CAF_FOR_ALL))
		goto out;

	if (likely(prop->cp_policies[cla] != act)) {
		prop->cp_policies[cla] = act;
		rc = chk_prop_update(prop, NULL);
	}

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u takes action for seq "
		 DF_X64" with gen "DF_X64", class %u, action %u, flags %x: "DF_RC"\n",
		 DP_ENGINE(ins), dss_self_rank(), seq, gen, cla, act, flags, DP_RC(rc));

	return rc;
}

static int
chk_engine_cont_list_local_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
			      vos_iter_param_t *param, void *data, unsigned int *acts)
{
	struct chk_cont_list_args	*ccla = data;
	uuid_t				*new_array;
	int				 rc = 0;

	if (ccla->ccla_idx >= ccla->ccla_cap) {
		D_REALLOC_ARRAY(new_array, ccla->ccla_conts, ccla->ccla_cap, ccla->ccla_cap << 1);
		if (new_array == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		ccla->ccla_conts = new_array;
		ccla->ccla_cap <<= 1;
	}

	uuid_copy(ccla->ccla_conts[ccla->ccla_idx++], entry->ie_couuid);

out:
	return rc;
}

/*
 * Enumerate the containers for one pool target.
 * Different pool targets (on the same rank) may have different containers list.
 */
static int
chk_engine_cont_list_one(void *args)
{
	struct dss_coll_stream_args	*reduce = args;
	struct dss_stream_arg_type	*streams = reduce->csa_streams;
	struct chk_cont_list_args	*ccla;
	struct ds_pool_child		*pool;
	vos_iter_param_t		 param = { 0 };
	struct vos_iter_anchors		 anchor = { 0 };
	int				 rc = 0;

	ccla = streams[dss_get_module_info()->dmi_tgt_id].st_arg;
	pool = ds_pool_child_lookup(ccla->ccla_pool);
	/* non-exist pool is not fatal. */
	if (pool != NULL) {
		param.ip_hdl = pool->spc_hdl;
		rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
				 chk_engine_cont_list_local_cb, NULL, ccla, NULL);
		ds_pool_child_put(pool);
	}

	return rc;
}

static void
chk_engine_cont_list_reduce(void *a_args, void *s_args)
{
	struct chk_cont_list_args	*ccla = s_args;

	chk_engine_cont_list_reduce_internal(a_args, ccla->ccla_conts, ccla->ccla_idx);
}

static int
chk_engine_cont_list_alloc(struct dss_stream_arg_type *args, void *a_arg)
{
	struct chk_cont_list_aggregator	*aggregator = a_arg;
	struct chk_cont_list_args	*ccla;
	int					 rc = 0;

	D_ALLOC_PTR(ccla);
	if (ccla == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	ccla->ccla_cap = 2;
	D_ALLOC_ARRAY(ccla->ccla_conts, ccla->ccla_cap);
	if (ccla->ccla_conts == NULL) {
		D_FREE(ccla);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	uuid_copy(ccla->ccla_pool, aggregator->ccla_pool);
	args->st_arg = ccla;

out:
	return rc;
}

static void
chk_engine_cont_list_free(struct dss_stream_arg_type *args)
{
	struct chk_cont_list_args	*ccla = args->st_arg;

	if (ccla != NULL) {
		D_FREE(ccla->ccla_conts);
		D_FREE(args->st_arg);
	}
}

int
chk_engine_cont_list(uint64_t gen, uuid_t pool, uuid_t **conts, uint32_t *count)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_cont_list_aggregator	 aggregator = { 0 };
	struct dss_coll_args		 coll_args = { 0 };
	struct dss_coll_ops		 coll_ops = { 0 };
	struct chk_cont_rec		*ccr;
	uuid_t				*uuids;
	int				 i = 0;
	int				 rc = 0;

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	rc = chk_engine_cont_list_init(pool, &aggregator);
	if (rc != 0)
		goto out;

	coll_args.ca_func_args = &coll_args.ca_stream_args;
	coll_args.ca_aggregator = &aggregator;

	coll_ops.co_func = chk_engine_cont_list_one;
	coll_ops.co_reduce = chk_engine_cont_list_reduce;
	coll_ops.co_reduce_arg_alloc = chk_engine_cont_list_alloc;
	coll_ops.co_reduce_arg_free = chk_engine_cont_list_free;

	rc = dss_task_collective_reduce(&coll_ops, &coll_args, 0);
	if (rc != 0)
		goto out;

out:
	if (rc == 0 && aggregator.ccla_count > 0) {
		D_ALLOC_ARRAY(uuids, aggregator.ccla_count);
		if (uuids == NULL) {
			rc = -DER_NOMEM;
			*conts = NULL;
			*count = 0;
		} else {
			d_list_for_each_entry(ccr, &aggregator.ccla_list, ccr_link)
				uuid_copy(uuids[i++], ccr->ccr_uuid);

			*conts = uuids;
			*count = aggregator.ccla_count;
		}
	} else {
		*conts = NULL;
		*count = 0;
	}

	chk_engine_cont_list_fini(&aggregator);

	return rc;
}

int
chk_engine_pool_mbs(uint64_t gen, uuid_t uuid, const char *label, uint32_t flags,
		    uint32_t mbs_nr, struct chk_pool_mbs *mbs_array, struct rsvc_hint *hint)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_pool_rec	*cpr = NULL;
	struct ds_pool_svc	*svc = NULL;
	d_iov_t			 riov;
	d_iov_t			 kiov;
	int			 rc;

	rc = ds_pool_svc_lookup_leader(uuid, &svc, hint);
	if (rc != 0)
		goto out;

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
	if (rc != 0)
		goto out;

	cpr = (struct chk_pool_rec *)riov.iov_buf;
	ABT_mutex_lock(cpr->cpr_mutex);

	/* XXX: resent RPC. */
	if (unlikely(cpr->cpr_mbs != NULL))
		goto unlock;

	D_ALLOC_ARRAY(cpr->cpr_mbs, mbs_nr);
	if (cpr->cpr_mbs == NULL)
		D_GOTO(unlock, rc = -DER_NOMEM);

	memcpy(cpr->cpr_mbs, mbs_array, sizeof(*mbs_array) * mbs_nr);
	cpr->cpr_shard_nr = mbs_nr;

	if (flags & CMF_REPAIR_LABEL) {
		rc = chk_dup_label(&cpr->cpr_label, label, label != NULL ? strlen(label) : 0);
		if (rc != 0)
			goto unlock;

		cpr->cpr_delay_label = 1;
	}

unlock:
	if (rc != 0) {
		D_FREE(cpr->cpr_mbs);
		cpr->cpr_shard_nr = 0;
		cpr->cpr_stop = 1;
	}

	ABT_cond_broadcast(cpr->cpr_cond);
	ABT_mutex_unlock(cpr->cpr_mutex);
out:
	ds_pool_svc_put_leader(svc);

	return rc;
}

int
chk_engine_report(struct chk_report_unit *cru, int *decision)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_pending_rec	*cpr = NULL;
	uint64_t		 seq = 0;
	int			 rc;

	rc = chk_report_remote(ins->ci_prop.cp_leader, ins->ci_bk.cb_gen, cru->cru_cla,
			       cru->cru_act, cru->cru_result, cru->cru_rank, cru->cru_target,
			       cru->cru_pool, cru->cru_cont, cru->cru_obj, cru->cru_dkey,
			       cru->cru_akey, cru->cru_msg, cru->cru_option_nr, cru->cru_options,
			       cru->cru_detail_nr, cru->cru_details, &seq);
	if (rc != 0)
		goto log;

	if (cru->cru_act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT)
		rc = chk_pending_add(ins, NULL, seq, cru->cru_rank, cru->cru_cla, &cpr);

log:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u report with class %u, action %u, "
		 "handle_rc %d, report_rc %d\n",
		 DP_ENGINE(ins), cru->cru_rank, cru->cru_cla, cru->cru_act, cru->cru_result, rc);

	if (rc != 0 || cpr == NULL)
		goto out;

	D_ASSERT(cpr->cpr_busy == 1);

	D_INFO(DF_ENGINE" on rank %u need interaction for class %u\n",
	       DP_ENGINE(ins), cru->cru_rank, cru->cru_cla);

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
chk_engine_notify(uint64_t gen, uuid_t uuid, d_rank_t rank, uint32_t phase,
		  uint32_t status, bool remove_pool)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	bool			 stop_engine = false;
	int			 rc = 0;

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	/* Ignore notification from non-leader. */
	if (prop->cp_leader != rank)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (remove_pool) {
		if (uuid_is_null(uuid))
			D_GOTO(out, rc = -DER_INVAL);

		chk_pool_stop_one(ins, uuid, CHK__CHECK_POOL_STATUS__CPS_IMPLICATED,
				  true, true, &rc);
		if (d_list_empty(&ins->ci_pool_list))
			stop_engine = true;

		D_GOTO(out, rc = (rc == -DER_NO_HDL) ? -DER_NOTAPPLICABLE : rc);
	}

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		if (unlikely(cbk->cb_phase >= phase))
			D_GOTO(out, rc = -DER_NOTAPPLICABLE);

		ABT_mutex_lock(ins->ci_abt_mutex);
		cbk->cb_phase = phase;
		ABT_cond_broadcast(ins->ci_abt_cond);
		ABT_mutex_unlock(ins->ci_abt_mutex);

		if (phase == CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST)
			rc = chk_engine_setup_pools(ins);

		goto out;
	}

	if (status != CHK__CHECK_INST_STATUS__CIS_FAILED &&
	    status != CHK__CHECK_INST_STATUS__CIS_IMPLICATED)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (!uuid_is_null(uuid)) {
		chk_pool_stop_one(ins, uuid, CHK__CHECK_POOL_STATUS__CPS_IMPLICATED,
				  false, true, &rc);
		if (d_list_empty(&ins->ci_pool_list))
			stop_engine = true;

		D_GOTO(out, rc = (rc == -DER_NO_HDL) ? -DER_NOTAPPLICABLE : rc);
	}

	/* Leader notify to exit the whole check if not specify the pool uuid. */
	stop_engine = true;

out:
	if (stop_engine) {
		ins->ci_implicated = 1;
		chk_stop_sched(ins);
	}

	D_CDEBUG(rc != 0 && rc != -DER_NOTAPPLICABLE, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u got notification from rank %u, for pool "
		 DF_UUIDF", phase %u, status %u, gen "DF_X64", %s pool: "DF_RC"\n",
		 DP_ENGINE(ins), dss_self_rank(), rank, DP_UUID(uuid), phase, status,
		 gen, remove_pool ? "remove" : "keep", DP_RC(rc));

	return (rc == 0 || rc == -DER_NOTAPPLICABLE) ? 0 : rc;
}

static int
chk_rejoin_cb(struct sys_db *db, char *table, d_iov_t *key, void *args)
{
	struct chk_traverse_pools_args	*ctpa = args;
	struct chk_instance		*ins = ctpa->ctpa_ins;
	char				*uuid_str = key->iov_buf;
	uuid_t				 uuid;
	struct chk_bookmark		 cbk;
	int				 rc;

	if (!daos_is_valid_uuid_string(uuid_str))
		goto out;

	rc = chk_bk_fetch_pool(&cbk, uuid_str);
	if (rc != 0) {
		ctpa->ctpa_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		goto out;
	}

	if (cbk.cb_gen != ctpa->ctpa_gen)
		goto out;

	if (cbk.cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_FAILED) {
		if (cbk.cb_phase < ins->ci_slowest_fail_phase)
			ins->ci_slowest_fail_phase = cbk.cb_phase;
		goto out;
	}

	if (cbk.cb_pool_status != CHK__CHECK_POOL_STATUS__CPS_CHECKING &&
	    cbk.cb_pool_status != CHK__CHECK_POOL_STATUS__CPS_PAUSED &&
	    cbk.cb_pool_status != CHK__CHECK_POOL_STATUS__CPS_PENDING)
		goto out;

	uuid_parse(uuid_str, uuid);

	/* Always refresh the start time. */
	cbk.cb_time.ct_start_time = time(NULL);
	/* QUEST: How to estimate the left time? */
	cbk.cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk.cb_phase;
	cbk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid,
				dss_self_rank(), &cbk, ins, NULL, NULL, NULL);
	if (rc != 0) {
		ctpa->ctpa_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		goto out;
	}

	rc = chk_bk_update_pool(&cbk, uuid_str);
	if (rc != 0)
		chk_pool_del_shard(ins->ci_pool_hdl, uuid, dss_self_rank());

out:
	/* Ignore the failure to handle next one. */
	return 0;
}

void
chk_engine_rejoin(void)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	d_rank_list_t			*rank_list = NULL;
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_iv			 iv = { 0 };
	struct umem_attr		 uma = { 0 };
	uuid_t				 dummy_pool;
	d_rank_t			 myrank = dss_self_rank();
	uint32_t			 phase;
	int				 rc = 0;
	bool				 need_join = false;
	bool				 need_iv = false;
	bool				 joined = false;

	if (cbk->cb_magic != CHK_BK_MAGIC_ENGINE)
		goto out;

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING &&
	    cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_PAUSED)
		goto out;

	D_ASSERT(ins->ci_starting == 0);
	D_ASSERT(ins->ci_stopping == 0);
	D_ASSERT(ins->ci_iv_group == NULL);
	D_ASSERT(ins->ci_iv_ns == NULL);

	ins->ci_starting = 1;

	if (chk_is_on_leader(cbk->cb_gen, prop->cp_leader, true)) {
		ins->ci_iv_ns = chk_leader_get_iv_ns();
		/* If is possible that the check leader is not running. */
		if (ins->ci_iv_ns == NULL)
			goto out;

		need_join = true;
	} else {
		need_join = true;

		rc = chk_prop_fetch(prop, &rank_list);
		if (rc != 0)
			goto out;

		D_ASSERT(rank_list != NULL);

		rc = crt_group_secondary_create(CHK_DUMMY_POOL, NULL, rank_list, &ins->ci_iv_group);
		if (rc != 0)
			goto out;

		uuid_parse(CHK_DUMMY_POOL, dummy_pool);
		rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, dummy_pool, ins->ci_iv_group,
				     &ins->ci_iv_id, &ins->ci_iv_ns);
		if (rc != 0)
			goto out;

		ds_iv_ns_update(ins->ci_iv_ns, prop->cp_leader);
	}

	uma.uma_id = UMEM_CLASS_VMEM;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pool_btr, &ins->ci_pool_hdl);
	if (rc != 0)
		goto out;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_PA, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pending_btr, &ins->ci_pending_hdl);
	if (rc != 0)
		goto out;

	/* Ask leader whether this engine can rejoin or not. */
	rc = chk_rejoin_remote(prop->cp_leader, cbk->cb_gen, myrank, cbk->cb_phase);
	if (rc != 0)
		goto out;

	joined = true;

	ctpa.ctpa_gen = cbk->cb_gen;
	ctpa.ctpa_ins = ins;
	rc = chk_traverse_pools(chk_rejoin_cb, &ctpa);
	if (rc != 0)
		goto out;

	phase = chk_engine_find_slowest(ins, NULL);
	if (phase != cbk->cb_phase)
		need_iv = true;

	cbk->cb_phase = phase;
	if (unlikely(d_list_empty(&ins->ci_pool_list))) {
		if (ctpa.ctpa_status == CHK__CHECK_INST_STATUS__CIS_FAILED)
			cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		else
			cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_COMPLETED;
		cbk->cb_time.ct_stop_time = time(NULL);
		need_iv = true;
	} else {
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;
		/* Always refresh the start time. */
		cbk->cb_time.ct_start_time = time(NULL);
		/* XXX: How to estimate the left time? */
		cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	}

	rc = chk_bk_update_engine(cbk);
	if (rc != 0) {
		need_iv = true;
		goto out;
	}

	if (unlikely(d_list_empty(&ins->ci_pool_list)))
		goto out;

	ins->ci_sched_running = 1;

	rc = dss_ult_create(chk_engine_sched, ins, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &ins->ci_sched);
	if (rc != 0)
		need_iv = true;
	else
		/* chk_engine_sched will do IV to leader. */
		need_iv = false;

out:
	ins->ci_starting = 0;
	d_rank_list_free(rank_list);

	if (rc != 0 && joined) {
		chk_engine_exit(ins, CHK__CHECK_INST_STATUS__CIS_FAILED,
				CHK__CHECK_POOL_STATUS__CPS_IMPLICATED);
	} else if (need_iv && cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_IMPLICATED &&
		   ins->ci_iv_ns != NULL) {
		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = cbk->cb_phase;
		iv.ci_status = cbk->cb_ins_status;
		iv.ci_to_leader = 1;

		/* Notify the leader that check instance exit on the engine. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_TO_ROOT,
				   CRT_IV_SYNC_NONE, true);
		if (rc != 0)
			D_ERROR(DF_ENGINE" on rank %u failed to notify leader "
				"for its changes, status %u: "DF_RC"\n",
				DP_ENGINE(ins), myrank, cbk->cb_ins_status, DP_RC(rc));
	}

	if (rc != 0) {
		chk_destroy_pending_tree(ins);
		chk_destroy_pool_tree(ins);
	}

	/*
	 * XXX: It is unnecessary to destroy the IV namespace that can be handled when next
	 *	CHK_START or instance fini.
	 */

	if (need_join)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" rejoin on rank %u: "DF_RC"\n",
			 DP_ENGINE(ins), myrank, DP_RC(rc));
}

void
chk_engine_pause(void)
{
	struct chk_instance	*ins = chk_engine;

	chk_stop_sched(ins);
	D_ASSERT(d_list_empty(&ins->ci_pending_list));
	D_ASSERT(d_list_empty(&ins->ci_pool_list));
}

int
chk_engine_init(void)
{
	struct chk_bookmark	*cbk;
	int			 rc;

	D_ALLOC(chk_engine, sizeof(*chk_engine));
	if (chk_engine == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = chk_ins_init(chk_engine);
	if (rc != 0)
		goto fini;

	/*
	 * XXX: DAOS global consistency check depends on all related engines' local
	 *	consistency. If hit some local data corruption, then it is possible
	 *	that local consistency is not guaranteed. Need to break and resolve
	 *	related local inconsistency firstly.
	 */

	cbk = &chk_engine->ci_bk;
	rc = chk_bk_fetch_engine(cbk);
	if (rc == -DER_NONEXIST)
		rc = 0;

	/* It may be caused by local data corruption, let's break. */
	if (rc != 0)
		goto fini;

	if (cbk->cb_magic != 0 && cbk->cb_magic != CHK_BK_MAGIC_ENGINE) {
		D_ERROR("Hit corrupted engine bookmark on rank %u: %u vs %u\n",
			dss_self_rank(), cbk->cb_magic, CHK_BK_MAGIC_ENGINE);
		D_GOTO(fini, rc = -DER_IO);
	}

	rc = chk_prop_fetch(&chk_engine->ci_prop, NULL);
	if (rc == -DER_NONEXIST)
		rc = 0;

	if (rc != 0)
		goto fini;

	goto out;

fini:
	chk_ins_fini(chk_engine);
	chk_engine = NULL;
out:
	return rc;
}

void
chk_engine_fini(void)
{
	chk_ins_fini(chk_engine);
}
