/**
 * (C) Copyright 2022-2023 Intel Corporation.
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
#include <daos/pool.h>
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

static struct chk_instance	*chk_engine;

struct chk_query_pool_args {
	struct chk_instance		*cqpa_ins;
	uint32_t			 cqpa_cap;
	uint32_t			 cqpa_idx;
	struct chk_query_pool_shard	*cqpa_shards;
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
	d_iov_t				 ccr_label_cs;
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

struct chk_pool_mbs_args {
	struct ds_pool_svc		*cpma_svc;
	struct chk_pool_rec		*cpma_cpr;
};

static int chk_engine_report(struct chk_report_unit *cru, uint64_t *seq, int *decision);

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
	daos_iov_free(&ccr->ccr_label_cs);
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
chk_engine_exit(struct chk_instance *ins, uint32_t ins_phase, uint32_t ins_status,
		uint32_t pool_status)
{
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pool_rec	*cpr;
	struct chk_iv		 iv = { 0 };
	int			 rc;

	ins->ci_sched_exiting = 1;

	while ((cpr = d_list_pop_entry(&ins->ci_pool_shutdown_list, struct chk_pool_rec,
				       cpr_shutdown_link)) != NULL) {
		chk_pool_shutdown(cpr, false);
		chk_pool_put(cpr);
	}

	chk_pool_stop_all(ins, pool_status, NULL);

	chk_destroy_pending_tree(ins);
	chk_destroy_pool_tree(ins);

	if (DAOS_FAIL_CHECK(DAOS_CHK_ENGINE_DEATH))
		goto out;

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_ins_status = ins_status;
		if (ins_phase != CHK_INVAL_PHASE)
			cbk->cb_phase = ins_phase;
		cbk->cb_time.ct_stop_time = time(NULL);
		rc = chk_bk_update_engine(cbk);
		if (rc != 0)
			D_WARN(DF_ENGINE" failed to update engine bookmark: "DF_RC"\n",
			       DP_ENGINE(ins), DP_RC(rc));
	}

	if (ins_status != CHK__CHECK_INST_STATUS__CIS_PAUSED &&
	    ins_status != CHK__CHECK_INST_STATUS__CIS_STOPPED &&
	    ins_status != CHK__CHECK_INST_STATUS__CIS_IMPLICATED && ins->ci_iv_ns != NULL) {
		if (DAOS_FAIL_CHECK(DAOS_CHK_PS_NOTIFY_LEADER))
			goto out;

		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = cbk->cb_phase;
		iv.ci_ins_status = ins_status;
		iv.ci_to_leader = 1;

		/* Notify the leader that check instance exit on the engine. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_TO_ROOT,
				   CRT_IV_SYNC_NONE, true);
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" on rank %u notify leader for its exit, status %u: rc = %d\n",
			 DP_ENGINE(ins), dss_self_rank(), ins_status, rc);
	}

out:
	ins->ci_sched_exiting = 0;
}

static int
chk_engine_post_repair(struct chk_pool_rec *cpr, int *result, bool update)
{
	struct chk_bookmark	*cbk = &cpr->cpr_bk;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	int			 rc = 0;

	if (unlikely(*result > 0))
		*result = 0;

	if (*result != 0) {
		if (cpr->cpr_ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT) {
			cbk->cb_time.ct_stop_time = time(NULL);
			cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_FAILED;
		} else {
			*result = 0;
		}
	}

	if (update) {
		uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
		rc = chk_bk_update_pool(cbk, uuid_str);
	}

	return *result != 0 ? *result : rc;
}

static int
chk_engine_pm_orphan(struct chk_pool_rec *cpr, d_rank_t rank, int index)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	d_rank_list_t			 ranks = { 0 };
	struct chk_report_unit		 cru = { 0 };
	char				*strs[2];
	d_iov_t				 iovs[2];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint64_t			 seq = 0;
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	/*
	 * NOTE: The subsequent check after handling orphan pm entry will not access the
	 *	 orphan pm entry. So here even if we failed to handle the orphan pm entry,
	 *	 it will not affect the subsequent check. Then does not set cpr_skip for
	 *	 this case.
	 */
	if (index < 0)
		cla = CHK__CHECK_INCONSIST_CLASS__CIC_ENGINE_NONEXIST_IN_MAP;
	else
		cla = CHK__CHECK_INCONSIST_CLASS__CIC_ENGINE_DOWN_IN_MAP;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * If the rank does not exists in the pool map, then destroy the orphan pool rank
		 * to release space by default.
		 *
		 * NOTE: Currently, we does not support to add the orphan pool rank into the pool
		 *	 map. If want to add them, it can be done via pool extend after DAOS check.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
	case CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_DISCARD;
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			if (index < 0) {
				ranks.rl_ranks = &rank;
				ranks.rl_nr = 1;
				result = ds_mgmt_tgt_pool_destroy_ranks(cpr->cpr_uuid, &ranks);
			} else {
				result = ds_mgmt_tgt_pool_shard_destroy(cpr->cpr_uuid, index, rank);
			}

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

			strs[0] = "Discard the orphan pool shard to release space [suggested].";
			strs[1] = "Keep the orphan pool shard on engine, repair nothing.";

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
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects orphan %s entry in pool map for "
		 DF_UUIDF", rank %u, index %d\n",
		 index < 0 ? "rank" : "target", DP_UUID(cpr->cpr_uuid), rank, index);
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects orphan %s entry in pool map for "
		 DF_UUIDF", rank %u, index %d, action %u (%s), handle_rc %d, "
		 "report_rc %d, decision %d\n",
		 DP_ENGINE(ins), index < 0 ? "rank" : "target", DP_UUID(cpr->cpr_uuid), rank,
		 index, act, option_nr ? "need interact" : "no interact", result, rc, decision);

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
		D_ERROR(DF_ENGINE" got invalid decision %d for orphan %s entry in pool map "
			"for pool "DF_UUIDF", rank %u, index %d. Ignore the inconsistency.\n",
			DP_ENGINE(ins), decision, index < 0 ? "rank" : "target",
			DP_UUID(cpr->cpr_uuid), rank, index);
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
			if (index < 0) {
				ranks.rl_ranks = &rank;
				ranks.rl_nr = 1;
				result = ds_mgmt_tgt_pool_destroy_ranks(cpr->cpr_uuid, &ranks);
			} else {
				result = ds_mgmt_tgt_pool_shard_destroy(cpr->cpr_uuid, index, rank);
			}

			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	}

	goto report;

out:
	return chk_engine_post_repair(cpr, &result, rc <= 0);
}

static int
chk_engine_pm_dangling(struct chk_pool_rec *cpr, struct pool_map *map, struct pool_component *comp,
		       uint32_t status)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct chk_report_unit		 cru = { 0 };
	char				*strs[2];
	d_iov_t				 iovs[2];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 suggested[CHK_MSG_BUFLEN] = { 0 };
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint64_t			 seq = 0;
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	D_ASSERTF(status == PO_COMP_ST_DOWNOUT || status == PO_COMP_ST_DOWN,
		  "Unexpected pool map status %u\n", status);

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_ENGINE_HAS_NO_STORAGE;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		/*
		 * If the target does not has storage on the engine, then mark it as 'DOWN' or
		 * 'DOWNOUT' in the pool map by default.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
		/*
		 * NOTE: For dryrun mode, we will not persistently store the change in
		 *	 subsequent step. Here we only fix the inconsistency in DRAM.
		 */
		cpr->cpr_map_refreshed = 1;
		comp->co_status = status;
		comp->co_fseq = pool_map_bump_version(map);
		cbk->cb_statistics.cs_repaired++;
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		/* Report the inconsistency without repair. */
		cbk->cb_statistics.cs_ignored++;
		/*
		 * For the pool with dangling map entry, if not repair, then the subsequent
		 * check (based on pool map) may fail, then have to skip to avoid confusing.
		 */
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
		} else {
			act = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
			options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			option_nr = 2;

			snprintf(suggested, CHK_MSG_BUFLEN - 1,
				 "Change pool map for the dangling map entry as %s [suggested].",
				 pool_map_status2name(status));
			strs[0] = suggested;
			strs[1] = "Keep the dangling map entry in pool map, repair nothing.";

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
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects dangling %s entry in pool map for pool "
		 DF_UUIDF", rank %u, index %u, (want) mark as %s\n",
		 comp->co_type == PO_COMP_TP_RANK ? "rank" : "target",
		 DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index,
		 pool_map_status2name(status));
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects dangling %s entry in pool map for pool "
		 DF_UUIDF" rank %u, index %u, action %u (%s), handle_rc %d, report_rc %d, "
		 "decision %d, (want) mark as %s\n",
		 DP_ENGINE(ins), comp->co_type == PO_COMP_TP_RANK ? "rank" : "target",
		 DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index, act,
		 option_nr ? "need interact" : "no interact", result, rc, decision,
		 pool_map_status2name(status));

	if (rc < 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		/*
		 * Skip the pool with dangling pm entry if failed to interact with admin for
		 * further action.
		 */
		cpr->cpr_skip = 1;
		result = rc;
	}

	if (rc > 0 || result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;
	detail_nr = 0;

	switch (decision) {
	default:
		D_ERROR(DF_ENGINE" got invalid decision %d for dangling %s entry in pool map "
			"for pool "DF_UUIDF", rank %u, index %u, (want) mark as %s. Ignore.\n",
			DP_ENGINE(ins), decision,
			comp->co_type == PO_COMP_TP_RANK ? "rank" : "target",
			DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index,
			pool_map_status2name(status));
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
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
		/*
		 * NOTE: For dryrun mode, we will not persistently store the change in
		 *	 subsequent step. Here we only fix the inconsistency in DRAM.
		 */
		cpr->cpr_map_refreshed = 1;
		comp->co_status = status;
		comp->co_fseq = pool_map_bump_version(map);
		cbk->cb_statistics.cs_repaired++;
		break;
	}

	goto report;

out:
	return chk_engine_post_repair(cpr, &result, rc <= 0);
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
	uint64_t			 seq = 0;
	int				 rc;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_UNKNOWN;
	act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
	cbk->cb_statistics.cs_total++;
	cbk->cb_statistics.cs_ignored++;
	/* Skip the pool with unknown pm entry. */
	cpr->cpr_skip = 1;

	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = act;
	cru.cru_rank = dss_self_rank();
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_pool_label = cpr->cpr_label;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects unknown target entry in pool map for pool "
		 DF_UUIDF", rank %u, index %u, status %u, skip it. You can change "
		 "its status via DAOS debug tool if it is not for downgraded case.\n",
		 DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index, comp->co_status);
	cru.cru_msg = msg;
	cru.cru_result = 0;

	rc = chk_engine_report(&cru, &seq, NULL);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects unknown target entry in pool map for pool "DF_UUIDF", rank %u, "
		 "target %u, action %u (no interact), handle_rc 0, report_rc %d, decision 0\n",
		 DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid), comp->co_rank, comp->co_index, act, rc);

	return chk_engine_post_repair(cpr, &rc, rc <= 0);
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
			 * NOTE: In the future, we may support to add the target (if exist) back.
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
		case PO_COMP_ST_UPIN:
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
				 * NOTE: The unknown status maybe because of downgraded from new
				 *	 layout? It is better to keep it there with reporting it
				 *	 to admin who can adjust the status via DAOS debug tool.
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
	bool			 down;

	rank_nr = pool_map_find_nodes(map, PO_COMP_ID_ALL, &doms);
	if (rank_nr <= 0)
		D_GOTO(out, rc = rank_nr);

	for (i = 0; i < rank_nr; i++) {
		r_comp = &doms[i].do_comp;
		if (r_comp->co_flags & PO_COMPF_CHK_DONE ||
		    r_comp->co_status == PO_COMP_ST_DOWN || r_comp->co_status == PO_COMP_ST_DOWNOUT)
			continue;

		down = false;

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
			case PO_COMP_ST_UPIN:
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

		/* dangling parent domain. */
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
	daos_prop_t			*label = NULL;
	struct chk_report_unit		 cru = { 0 };
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint64_t			 seq = cpr->cpr_label_seq;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_POOL_BAD_LABEL;
	act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_MS;
	cbk->cb_statistics.cs_total++;

	rc = ds_pool_prop_fetch(ds_pool_svc2pool(svc), DAOS_PO_QUERY_PROP_LABEL, &label);
	if (rc != 0 && rc != -DER_NONEXIST)
		D_GOTO(report, result = rc);

	if (ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
		cbk->cb_statistics.cs_repaired++;
	} else {
		result = ds_pool_svc_update_label(svc, cpr->cpr_label);
		if (result != 0)
			cbk->cb_statistics.cs_failed++;
		else
			cbk->cb_statistics.cs_repaired++;
	}

report:
	cru.cru_gen = cbk->cb_gen;
	cru.cru_cla = cla;
	cru.cru_act = act;
	cru.cru_rank = dss_self_rank();
	cru.cru_pool = (uuid_t *)&cpr->cpr_uuid;
	cru.cru_pool_label = cpr->cpr_label;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects corrupted pool label: %s (MS) vs %s (PS).\n",
		 cpr->cpr_label != NULL ? cpr->cpr_label : "(null)",
		 label != NULL ? label->dpp_entries[0].dpe_str : "(null)");
	cru.cru_msg = msg;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &seq, NULL);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects corrupted label %s (MS) vs %s (PS) for pool "
		 DF_UUIDF", action %u (no interact), handle_rc %d, report_rc %d\n",
		 DP_ENGINE(ins), cpr->cpr_label != NULL ? cpr->cpr_label : "(null)",
		 label != NULL ? label->dpp_entries[0].dpe_str : "(null)",
		 DP_UUID(cpr->cpr_uuid), act, result, rc);

	/*
	 * It is not fatal even if failed to repair inconsistent pool label,
	 * then do not skip current pool for subsequent DAOS check.
	 */

	daos_prop_free(label);

	return chk_engine_post_repair(cpr, &result, rc <= 0);
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
chk_engine_cont_list_remote_cb(struct chk_co_rpc_cb_args *cb_args)
{
	return chk_engine_cont_list_reduce_internal(cb_args->cb_priv, cb_args->cb_data,
						    cb_args->cb_nr);
}

static int
chk_engine_cont_orphan(struct chk_pool_rec *cpr, struct chk_cont_rec *ccr, struct cont_svc *svc)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct chk_report_unit		 cru = { 0 };
	char				*strs[2];
	d_iov_t				 iovs[2];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint64_t			 seq = 0;
	uint32_t			 options[2];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
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
		 * NOTE: Currently, we do not support to add the orphan container back to the CS,
		 *	 that may be implemented in the future when we have enough information to
		 *	 recover necessary prop/attr for the orphan container.
		 *
		 * Fall through.
		 */
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
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

			strs[0] = "Destroy the orphan container to release space [suggested].";
			strs[1] = "Keep the orphan container on engines, repair nothing.";

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
	cru.cru_cont = (uuid_t *)&ccr->ccr_uuid;
	if (ccr->ccr_label_prop != NULL)
		cru.cru_cont_label = ccr->ccr_label_prop->dpp_entries[0].dpe_str;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects orphan container "DF_UUIDF"/"DF_UUIDF"\n",
		 DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid));
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects orphan container "
		 DF_UUIDF"/"DF_UUIDF", action %u (%s), handle_rc %d, report_rc %d, decision %d\n",
		 DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid), act,
		 option_nr ? "need interact" : "no interact", result, rc, decision);

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
	/* NOTE: For orphan container, mark it as 'skip' since we do not support to add it back. */
	ccr->ccr_skip = 1;

	return chk_engine_post_repair(cpr, &result, rc <= 0);
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

static inline bool
chk_engine_cont_target_label_empty(struct chk_cont_rec *ccr)
{
	if (ccr->ccr_label_prop == NULL)
		return true;

	if (strncmp(DAOS_PROP_NO_CO_LABEL, ccr->ccr_label_prop->dpp_entries[0].dpe_str,
		    DAOS_PROP_LABEL_MAX_LEN) == 0)
		return true;

	return false;
}

static inline bool
chk_engine_cont_cs_label_empty(struct chk_cont_rec *ccr)
{
	if (daos_iov_empty(&ccr->ccr_label_cs))
		return true;

	if (strncmp(DAOS_PROP_NO_CO_LABEL, ccr->ccr_label_cs.iov_buf, DAOS_PROP_LABEL_MAX_LEN) == 0)
		return true;

	return false;
}

/*
 * Trust the label in container service or in the container property.
 *
 * \return	1:	trust container service.
 * \return	0:	the same or no trustable.
 * \return	-1:	trust container property.
 */
static inline int
chk_engine_cont_choose_label(struct chk_cont_rec *ccr)
{
	bool	trust_cs = true;
	bool	trust_target = true;

	if (chk_engine_cont_cs_label_empty(ccr))
		trust_cs = false;

	if (chk_engine_cont_target_label_empty(ccr))
		trust_target = false;

	if (!trust_cs && !trust_target)
		return 0;

	/*
	 * If the container label in the container service (cont_svc::cs_uuids)
	 * exists but does not match the label in the container property, then
	 * trust the container service and reset the one in container property
	 * by default.
	 */
	if (trust_cs)
		return 1;

	return -1;
}

static inline char *
chk_engine_ccr2label(struct chk_cont_rec *ccr, bool prefer_target)
{
	if (prefer_target) {
		if (ccr->ccr_label_prop != NULL)
			return ccr->ccr_label_prop->dpp_entries[0].dpe_str;

		if (!daos_iov_empty(&ccr->ccr_label_cs))
			return ccr->ccr_label_cs.iov_buf;
	} else {
		if (!daos_iov_empty(&ccr->ccr_label_cs))
			return ccr->ccr_label_cs.iov_buf;

		if (ccr->ccr_label_prop != NULL)
			return ccr->ccr_label_prop->dpp_entries[0].dpe_str;
	}

	return NULL;
}

static int
chk_engine_cont_set_label(struct chk_pool_rec *cpr, struct chk_cont_rec *ccr, struct cont_svc *svc)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	daos_prop_t			*prop_tmp = NULL;
	struct chk_report_unit		 cru = { 0 };
	char				 strs[3][CHK_MSG_BUFLEN];
	d_iov_t				 iovs[3];
	d_sg_list_t			 sgl;
	d_sg_list_t			*details = NULL;
	char				*label = NULL;
	Chk__CheckInconsistClass	 cla;
	Chk__CheckInconsistAction	 act;
	char				 msg[CHK_MSG_BUFLEN] = { 0 };
	uint64_t			 seq = 0;
	uint32_t			 options[3];
	uint32_t			 option_nr = 0;
	uint32_t			 detail_nr = 0;
	int				 decision = -1;
	int				 result = 0;
	int				 rc = 0;

	cla = CHK__CHECK_INCONSIST_CLASS__CIC_CONT_BAD_LABEL;
	act = prop->cp_policies[cla];
	cbk->cb_statistics.cs_total++;

	switch (act) {
	case CHK__CHECK_INCONSIST_ACTION__CIA_DEFAULT:
		rc = chk_engine_cont_choose_label(ccr);
		if (rc > 0)
			goto trust_ps;

		if (rc < 0)
			goto trust_target;

		goto out;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS:
		rc = chk_engine_cont_choose_label(ccr);
		if (unlikely(rc == 0))
			goto out;

		if (rc < 0)
			goto interact;

trust_ps:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
		label = chk_engine_ccr2label(ccr, false);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			prop_tmp = chk_engine_build_label_prop(&ccr->ccr_label_cs);
			if (prop_tmp == NULL)
				D_GOTO(out, result = -DER_NOMEM);

			result = ds_cont_set_label(svc, ccr->ccr_uuid, prop_tmp,
						   ccr->ccr_label_prop, false);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
		rc = chk_engine_cont_choose_label(ccr);
		if (unlikely(rc == 0))
			goto out;

		if (rc > 0 && chk_engine_cont_target_label_empty(ccr))
			goto interact;

trust_target:
		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
		label = chk_engine_ccr2label(ccr, true);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			if (!daos_iov_empty(&ccr->ccr_label_cs)) {
				prop_tmp = chk_engine_build_label_prop(&ccr->ccr_label_cs);
				if (prop_tmp == NULL)
					D_GOTO(out, result = -DER_NOMEM);
			}

			result = ds_cont_set_label(svc, ccr->ccr_uuid,
						   ccr->ccr_label_prop, prop_tmp, true);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE:
		label = chk_engine_ccr2label(ccr, false);
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
		rc = chk_engine_cont_choose_label(ccr);
		if (unlikely(rc == 0))
			goto out;

interact:
		label = chk_engine_ccr2label(ccr, false);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_AUTO) {
			/* Ignore the inconsistency if admin does not want interaction. */
			act = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
			cbk->cb_statistics.cs_ignored++;
			break;
		}

		act = CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT;

		options[1] = CHK__CHECK_INCONSIST_ACTION__CIA_IGNORE;
		snprintf(strs[1], CHK_MSG_BUFLEN - 1,
			 "Keep the inconsistent container label: %s (CS) vs %s (property), "
			 "repair nothing.", daos_iov_empty(&ccr->ccr_label_cs) ? "(null)" :
			 (char *)ccr->ccr_label_cs.iov_buf, ccr->ccr_label_prop != NULL ?
			 (char *)ccr->ccr_label_prop->dpp_entries[0].dpe_str : "(null)");
		d_iov_set(&iovs[1], strs[1], strlen(strs[1]));

		if (rc > 0) {
			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
			snprintf(strs[0], CHK_MSG_BUFLEN - 1,
				 "Trust the container label %s in container service [suggested].",
				 (char *)ccr->ccr_label_cs.iov_buf);
			d_iov_set(&iovs[0], strs[0], strlen(strs[0]));

			if (chk_engine_cont_target_label_empty(ccr)) {
				option_nr = 2;
				sgl.sg_nr = 2;
			} else {
				options[2] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
				snprintf(strs[2], CHK_MSG_BUFLEN - 1,
					 "Trust the container label %s in container property.",
					 (char *)ccr->ccr_label_prop->dpp_entries[0].dpe_str);
				d_iov_set(&iovs[2], strs[2], strlen(strs[2]));
				option_nr = 3;
				sgl.sg_nr = 3;
			}
		} else {
			options[0] = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
			snprintf(strs[0], CHK_MSG_BUFLEN - 1,
				 "Trust the container label %s in container property [suggested].",
				 ccr->ccr_label_prop != NULL ?
				 (char *)ccr->ccr_label_prop->dpp_entries[0].dpe_str : "(null)");
			d_iov_set(&iovs[0], strs[0], strlen(strs[0]));

			D_ASSERT(chk_engine_cont_cs_label_empty(ccr));

			option_nr = 2;
			sgl.sg_nr = 2;
		}

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
	cru.cru_pool_label = cpr->cpr_label;
	cru.cru_cont = (uuid_t *)&ccr->ccr_uuid;
	cru.cru_cont_label = label;
	snprintf(msg, CHK_MSG_BUFLEN - 1,
		 "Check engine detects inconsistent container label: %s (CS) vs %s (property).\n",
		 daos_iov_empty(&ccr->ccr_label_cs) ? "(null)" : (char *)ccr->ccr_label_cs.iov_buf,
		 ccr->ccr_label_prop != NULL ? (char *)ccr->ccr_label_prop->dpp_entries[0].dpe_str :
		 "(null)");
	cru.cru_msg = msg;
	cru.cru_options = options;
	cru.cru_details = details;
	cru.cru_result = result;

	rc = chk_engine_report(&cru, &seq, &decision);

	D_CDEBUG(result != 0 || rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" detects inconsistent container label for "DF_UUIDF"/"DF_UUIDF
		 ": %s vs %s, action %u (%s), handle_rc %d, report_rc %d, decision %d\n",
		 DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid), DP_UUID(ccr->ccr_uuid),
		 daos_iov_empty(&ccr->ccr_label_cs) ? "(null)" : (char *)ccr->ccr_label_cs.iov_buf,
		 ccr->ccr_label_prop != NULL ? (char *)ccr->ccr_label_prop->dpp_entries[0].dpe_str :
		 "(null)", act, option_nr ? "need interact" : "no interact", result, rc, decision);

	if (rc < 0 && option_nr > 0) {
		cbk->cb_statistics.cs_failed++;
		result = rc;
	}

	if (rc > 0 || result != 0 || option_nr == 0)
		goto out;

	option_nr = 0;
	detail_nr = 0;

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
		if (chk_engine_cont_cs_label_empty(ccr))
			goto ignore;

		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_PS;
		label = chk_engine_ccr2label(ccr, false);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			prop_tmp = chk_engine_build_label_prop(&ccr->ccr_label_cs);
			if (prop_tmp == NULL)
				D_GOTO(out, result = -DER_NOMEM);

			result = ds_cont_set_label(svc, ccr->ccr_uuid, prop_tmp,
						   ccr->ccr_label_prop, false);
			if (result != 0)
				cbk->cb_statistics.cs_failed++;
			else
				cbk->cb_statistics.cs_repaired++;
		}
		break;
	case CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET:
		if (chk_engine_cont_target_label_empty(ccr))
			goto ignore;

		act = CHK__CHECK_INCONSIST_ACTION__CIA_TRUST_TARGET;
		label = chk_engine_ccr2label(ccr, true);
		if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cbk->cb_statistics.cs_repaired++;
		} else {
			if (!daos_iov_empty(&ccr->ccr_label_cs)) {
				prop_tmp = chk_engine_build_label_prop(&ccr->ccr_label_cs);
				if (prop_tmp == NULL)
					D_GOTO(out, result = -DER_NOMEM);
			}

			result = ds_cont_set_label(svc, ccr->ccr_uuid,
						   ccr->ccr_label_prop, prop_tmp, true);
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

	daos_prop_free(prop_tmp);

	return chk_engine_post_repair(cpr, &result, rc <= 0);
}

static int
chk_engine_cont_label_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *arg)
{
	struct chk_cont_label_cb_args	*cclca = arg;
	struct chk_cont_rec		*ccr;
	d_iov_t				 kiov;
	d_iov_t				 riov;
	int				 rc = 0;

	/* key is the label prop from CS::cs_uuids, must not be empty. */
	D_ASSERT(key != NULL);
	D_ASSERT(key->iov_buf != NULL);

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
		rc = daos_iov_copy(&ccr->ccr_label_cs, key);
	else
		ccr->ccr_label_checked = 1;

out:
	if (!(cclca->cclca_cpr->cpr_ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_FAILOUT))
		rc = 0;

	return rc;
}

static int
chk_engine_cont_cleanup(struct chk_pool_rec *cpr, struct ds_pool_svc *ds_svc,
			struct chk_cont_list_aggregator *aggregator)
{
	struct chk_instance		*ins = cpr->cpr_ins;
	struct cont_svc			*svc;
	struct chk_cont_rec		*ccr;
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
		if (!ccr->ccr_skip && !ccr->ccr_label_checked) {
			rc = chk_engine_cont_set_label(cpr, ccr, svc);
			if (rc != 0)
				goto out;
		}
	}

out:
	return rc;
}

static void
chk_engine_pool_notify(struct chk_pool_rec *cpr)
{
	struct chk_instance	*ins =cpr->cpr_ins;
	struct chk_bookmark	*cbk = &cpr->cpr_bk;
	struct chk_iv		 iv = { 0 };
	int			 rc;

	iv.ci_gen = cbk->cb_gen;
	uuid_copy(iv.ci_uuid, cpr->cpr_uuid);
	iv.ci_phase = cbk->cb_phase;
	iv.ci_ins_status = ins->ci_bk.cb_ins_status;
	iv.ci_pool_status = cbk->cb_pool_status;
	iv.ci_from_psl = 1;

	if (!DAOS_FAIL_CHECK(DAOS_CHK_PS_NOTIFY_ENGINE)) {
		/*
		 * Synchronously notify the pool shards with the new check status/phase.
		 * Because some engine maybe not the (refreshed) pool map. Then we will
		 * use ins->ci_iv_ns instead of pool->sp_iv_ns to send the notification
		 * to all engines. Otherwise, the engine out of the pool map cannot get
		 * the notification.
		 */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_NONE, CRT_IV_SYNC_EAGER,
				   true);
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" on rank %u notify pool shards for "DF_UUIDF", phase %u, "
			 "ins_status %u, pool_status %u: rc = %d\n",
			 DP_ENGINE(ins), dss_self_rank(), DP_UUID(cpr->cpr_uuid), iv.ci_phase,
			 iv.ci_ins_status, iv.ci_pool_status, rc);
	}

	if (!DAOS_FAIL_CHECK(DAOS_CHK_PS_NOTIFY_LEADER)) {
		iv.ci_from_psl = 0;
		iv.ci_to_leader = 1;
		/* Synchronously notify the check leader with the new check status/phase. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_TO_ROOT,
				   CRT_IV_SYNC_NONE, true);
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" on rank %u notify check leader for "DF_UUIDF", phase %u, "
			 "ins_status %u, pool_status %u: rc = %d\n",
			 DP_ENGINE(ins), dss_self_rank(), DP_UUID(cpr->cpr_uuid),
			 iv.ci_phase, iv.ci_ins_status, iv.ci_pool_status, rc);
	}
}

static void
chk_engine_pool_ult(void *args)
{
	struct chk_pool_mbs_args	*cpma = args;
	struct chk_pool_rec		*cpr = cpma->cpma_cpr;
	struct ds_pool_svc		*svc = cpma->cpma_svc;
	struct chk_instance		*ins = cpr->cpr_ins;
	struct chk_bookmark		*cbk = &cpr->cpr_bk;
	struct pool_map			*map = NULL;
	struct chk_cont_list_aggregator	 aggregator = { 0 };
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	int				 i;
	int				 rc = 0;
	int				 rc1 = 0;
	int				 rc2 = 0;
	bool				 update = true;

	D_ASSERT(svc != NULL);
	D_ASSERT(cpr != NULL);
	D_ASSERT(cpr->cpr_mbs != NULL);
	D_ASSERTF(cbk->cb_phase >= CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS,
		  "Invalid check phase %u for pool "DF_UUIDF"\n",
		  cbk->cb_phase, DP_UUID(cpr->cpr_uuid));

	D_INFO(DF_ENGINE" pool ult enter for "DF_UUIDF"\n", DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid));

	uuid_unparse_lower(cpr->cpr_uuid, uuid_str);

	if (cpr->cpr_stop)
		goto out;

	if (cbk->cb_phase > CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP)
		goto cont;

	if (cbk->cb_phase < CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP) {
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP;
		chk_engine_pool_notify(cpr);
		/* QUEST: How to estimate the left time? */
		cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE - cbk->cb_phase;
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0 || cpr->cpr_stop)
			goto out;
	}

	rc = ds_pool_svc_load_map(svc, &map);
	if (rc != 0)
		goto out;

	for (i = 0; i < cpr->cpr_shard_nr && !cpr->cpr_stop; i++) {
		rc = chk_engine_pool_mbs_one(cpr, map, &cpr->cpr_mbs[i]);
		if (rc != 0 || cpr->cpr_skip || cpr->cpr_stop)
			goto out;
	}

	/* Lookup for dangling entry in the pool map. */
	rc = chk_engine_find_dangling_pm(cpr, map);
	if (rc != 0 || cpr->cpr_skip || cpr->cpr_stop)
		goto out;

	if (cpr->cpr_map_refreshed) {
		/*
		 * Under dryrun mode, we cannot make the changed pool map to be used by
		 * subsequent check, then have to skip it.
		 */
		if (ins->ci_prop.cp_flags & CHK__CHECK_FLAG__CF_DRYRUN) {
			cpr->cpr_skip = 1;
			goto out;
		}

		/*
		 * Flush the pool map to persistent storage and
		 * distribute the pool map to other pool shards.
		 */
		rc = ds_pool_svc_flush_map(svc, map);
		if (rc != 0 || cpr->cpr_skip || cpr->cpr_stop)
			goto out;
	}

	if (cpr->cpr_delay_label) {
		rc = chk_engine_bad_pool_label(cpr, svc);
		if (rc != 0 || cpr->cpr_skip || cpr->cpr_stop)
			goto out;

		if (DAOS_FAIL_CHECK(DAOS_CHK_LEADER_BLOCK)) {
			while (!cpr->cpr_stop)
				dss_sleep(300);
			goto out;
		}
	}

	/*
	 * Cleanup all old connections. It is no matter even if we cannot evict some
	 * old connections. That is also independent from former check phases result.
	 */
	ds_pool_svc_evict_all(svc);

	if (cpr->cpr_stop)
		goto out;

cont:
	if (cbk->cb_phase < CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST) {
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST;
		chk_engine_pool_notify(cpr);
		/* QUEST: How to estimate the left time? */
		cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE - cbk->cb_phase;
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0 || cpr->cpr_stop)
			goto out;
	}

	if (unlikely(cbk->cb_phase > CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP))
		goto out;

	rc = chk_engine_cont_list_init(cpr->cpr_uuid, &aggregator);
	if (rc != 0)
		goto out;

	/* Collect containers from pool shards. */
	rc = chk_cont_list_remote(ds_pool_svc2pool(svc), cbk->cb_gen,
				  chk_engine_cont_list_remote_cb, &aggregator);
	if (rc != 0 || cpr->cpr_stop)
		goto out;

	if (cbk->cb_phase < CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP) {
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP;
		chk_engine_pool_notify(cpr);
		/* QUEST: How to estimate the left time? */
		cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE - cbk->cb_phase;
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0 || cpr->cpr_stop)
			goto out;
	}

	rc = chk_engine_cont_cleanup(cpr, svc, &aggregator);
	if (rc != 0)
		goto out;

	rc = ds_pool_svc_schedule_reconf(svc);

out:
	chk_engine_cont_list_fini(&aggregator);
	if (map != NULL)
		pool_map_decref(map);

	/*
	 * If someone wants to stop (cpr_stop) the pool ULT, then it needs to
	 * update related pool bookmark and notify other pool shards by itself.
	 */
	if (!cpr->cpr_stop) {
		if (rc != 0) {
			cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_FAILED;
		} else {
			/*
			 * This may be caused by former chk_engine_pool_notify() failed to
			 * notify some other check engine(s) and the leader about the pool
			 * status. It will be synced this time.
			 */
			cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;
			if (likely(cbk->cb_phase != CHK__CHECK_SCAN_PHASE__CSP_DONE))
				cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
			else
				update = false;
		}
		chk_engine_pool_notify(cpr);
		cbk->cb_time.ct_stop_time = time(NULL);
		if (likely(update))
			rc1 = chk_bk_update_pool(cbk, uuid_str);

		if (cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_CHECKED &&
		    !cpr->cpr_not_export_ps) {
			chk_pool_start_svc(cpr, &rc2);
			if (cpr->cpr_started && cpr->cpr_start_post)
				/*
				 * The pool may has been marked as non-connectable before
				 * corruption, re-enable it to allow new connection.
				 */
				rc2 = ds_pool_mark_connectable(svc);
		}
	}

	D_CDEBUG(rc != 0 || rc1 != 0 || rc2 != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u exit pool ULT for "DF_UUIDF" with %s stop: %d/%d/%d\n",
		 DP_ENGINE(ins), dss_self_rank(), DP_UUID(cpr->cpr_uuid),
		 cpr->cpr_stop ? "external" : "self", rc, rc1, rc2);

	ds_pool_svc_put_leader(svc);
	cpr->cpr_done = 1;
	if (ins->ci_sched_running && !ins->ci_sched_exiting &&
	    (cbk->cb_pool_status != CHK__CHECK_POOL_STATUS__CPS_CHECKED || cpr->cpr_not_export_ps))
		d_list_add_tail(&cpr->cpr_shutdown_link, &ins->ci_pool_shutdown_list);
	else
		chk_pool_put(cpr);
	D_FREE(cpma);
}

static void
chk_engine_sched(void *args)
{
	struct chk_instance	*ins = args;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pool_rec	*cpr;
	uint32_t		 ins_phase;
	uint32_t		 ins_status;
	uint32_t		 pool_status;
	d_rank_t		 myrank = dss_self_rank();
	int			 done = 0;
	int			 rc = 0;

	D_INFO(DF_ENGINE" scheduler on rank %u entry at phase %u\n",
	       DP_ENGINE(ins), myrank, cbk->cb_phase);

	while (!ins->ci_sched_exiting) {
		dss_sleep(300);

		/* Someone wants to stop the check. */
		if (ins->ci_sched_exiting)
			D_GOTO(out, rc = 0);

		ins_phase = chk_pools_find_slowest(ins, &done);

		/*
		 * Check @done before update cb_phase. Otherwise, the cb_phase may has become 'DONE'
		 * but cb_ins_status is still 'RUNNING'.
		 */
		if (done != 0) {
			if (done > 0) {
				D_INFO(DF_ENGINE" on rank %u has done\n", DP_ENGINE(ins), myrank);
				rc = 1;
			} else {
				D_INFO(DF_ENGINE" on rank %u is stopped\n", DP_ENGINE(ins), myrank);
				rc = 0;
			}

			D_GOTO(out, rc);
		}

		if (ins_phase > cbk->cb_phase) {
			D_INFO(DF_ENGINE" on rank %u moves from phase %u to phase %u\n",
			       DP_ENGINE(ins), myrank, cbk->cb_phase, ins_phase);

			cbk->cb_phase = ins_phase;
			/* QUEST: How to estimate the left time? */
			cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE - cbk->cb_phase;
			rc = chk_bk_update_engine(cbk);
			if (rc != 0)
				goto out;
		}

		while ((cpr = d_list_pop_entry(&ins->ci_pool_shutdown_list, struct chk_pool_rec,
					       cpr_shutdown_link)) != NULL) {
			chk_pool_shutdown(cpr, false);
			chk_pool_put(cpr);
		}
	}

out:
	ins_phase = CHK_INVAL_PHASE;
	if (rc > 0) {
		/*
		 * If failed to check some pool(s), then the engine will be marked as 'failed'.
		 * It means that there is at least one failure during DAOS check on this engine.
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

	/* The pool scan ULTs will be terminated via chk_engine_exit(). */
	chk_engine_exit(ins, ins_phase, ins_status, pool_status);

	D_INFO(DF_ENGINE" scheduler on rank %u exit at phase %u with status %u: rc %d\n",
	       DP_ENGINE(ins), myrank, cbk->cb_phase, ins_status, rc);

	ins->ci_sched_running = 0;
}

static int
chk_engine_start_prep(struct chk_instance *ins, uint32_t rank_nr, d_rank_t *ranks,
		      uint32_t policy_nr, struct chk_policy *policies, int pool_nr,
		      uuid_t pools[], uint64_t gen, int phase, uint32_t api_flags,
		      d_rank_t leader, uint32_t flags)
{
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct chk_property		*prop = &ins->ci_prop;
	d_rank_list_t			*rank_list = NULL;
	uint32_t			 cbk_phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
	int				 rc = 0;

	/* Check leader has already verified related parameters, trust them. */

	ins->ci_start_flags = flags;

	if (chk_is_on_leader(gen, leader, true)) {
		d_rank_list_free(ins->ci_ranks);
		ins->ci_ranks = NULL;
		rc = chk_prop_fetch(prop, &ins->ci_ranks);
		if (rc != 0)
			goto out;
	} else {
		rank_list = uint32_array_to_rank_list(ranks, rank_nr);
		if (rank_list == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		d_rank_list_sort(rank_list);
	}

	if (ins->ci_start_flags & CSF_RESET_ALL)
		goto reset;

	if (pool_nr > 0) {
		rc = chk_pools_load_list(ins, gen, api_flags, pool_nr, pools, &cbk_phase);
		if (rc != 0)
			goto out;
	} else {
		ctpa.ctpa_ins = ins;
		ctpa.ctpa_gen = gen;
		ctpa.ctpa_phase = cbk_phase;
		rc = chk_traverse_pools(chk_pools_load_from_db, &ctpa);
		if (rc != 0)
			goto out;

		cbk_phase = ctpa.ctpa_phase;
	}

	if (d_list_empty(&ins->ci_pool_list) && !(api_flags & CHK__CHECK_FLAG__CF_ORPHAN_POOL))
		D_GOTO(out, rc = 1);

	goto init;

reset:
	ctpa.ctpa_ins = ins;
	ctpa.ctpa_gen = gen;
	rc = chk_traverse_pools(chk_pools_cleanup_cb, &ctpa);
	if (rc != 0)
		goto out;

	if (pool_nr > 0) {
		rc = chk_pools_load_list(ins, gen, api_flags, pool_nr, pools, NULL);
		if (rc != 0)
			goto out;
	} else {
		rc = ds_mgmt_tgt_pool_iterate(chk_pools_add_from_dir, &ctpa);
		if (rc != 0)
			goto out;

		rc = ds_mgmt_newborn_pool_iterate(chk_pools_add_from_dir, &ctpa);
		if (rc != 0)
			goto out;

		rc = ds_mgmt_zombie_pool_iterate(chk_pools_add_from_dir, &ctpa);
		if (rc != 0)
			goto out;
	}

	memset(cbk, 0, sizeof(*cbk));
	cbk->cb_magic = CHK_BK_MAGIC_ENGINE;
	cbk->cb_version = DAOS_CHK_VERSION;
	cbk_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;

init:
	if (!chk_is_on_leader(gen, leader, true)) {
		rc = chk_prop_prepare(leader, api_flags, phase, policy_nr, policies, rank_list,
				      prop);
		if (rc != 0)
			goto out;

		if (rank_list != NULL) {
			d_rank_list_free(ins->ci_ranks);
			ins->ci_ranks = rank_list;
			rank_list = NULL;
		}
	}

	/* The engine bookmark will be stored via chk_engine_start_post() later. */
	if (cbk->cb_phase > cbk_phase)
		cbk->cb_phase = cbk_phase;

	cbk->cb_gen = gen;
	if (api_flags & CHK__CHECK_FLAG__CF_RESET && !(ins->ci_start_flags & CSF_RESET_ALL)) {
		memset(&cbk->cb_statistics, 0, sizeof(cbk->cb_statistics));
		memset(&cbk->cb_time, 0, sizeof(cbk->cb_time));
	}

	ins->ci_slowest_fail_phase = CHK_INVAL_PHASE;

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
chk_engine_start_post(struct chk_instance *ins)
{
	struct chk_pool_rec	*cpr;
	struct chk_bookmark	*ins_cbk = &ins->ci_bk;
	struct chk_bookmark	*pool_cbk;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	uint32_t		 phase = CHK__CHECK_SCAN_PHASE__CSP_DONE;
	int			 rc = 0;

	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
		pool_cbk = &cpr->cpr_bk;

		if (pool_cbk->cb_phase == CHK__CHECK_SCAN_PHASE__CSP_DONE)
			continue;

		if (phase > pool_cbk->cb_phase)
			phase = pool_cbk->cb_phase;

		pool_cbk->cb_gen = ins_cbk->cb_gen;
		pool_cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
		/* Always refresh the start time. */
		pool_cbk->cb_time.ct_start_time = time(NULL);
		/* QUEST: How to estimate the left time? */
		pool_cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE -
						 pool_cbk->cb_phase;

		uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
		rc = chk_bk_update_pool(pool_cbk, uuid_str);
		if (rc != 0)
			break;
	}

	if (rc == 0) {
		/*
		 * The phase may be CHK__CHECK_SCAN_PHASE__CSP_DONE, it is fine.
		 *
		 * The phase in engine bookmark may be larger than the phase in
		 * some pools that may be new added into current check instance.
		 * So we allow the phase to backward.
		 */
		ins_cbk->cb_phase = phase;
		ins_cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;
		/* Always refresh the start time. */
		ins_cbk->cb_time.ct_start_time = time(NULL);
		/* QUEST: How to estimate the left time? */
		ins_cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE -
						ins_cbk->cb_phase;
		rc = chk_bk_update_engine(ins_cbk);
		if (rc == 0) {
			d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link)
				/* Shutdown former instance left opened pool. */
				chk_pool_shutdown(cpr, false);
		}
	}

	return rc;
}

static int
chk_engine_pool_filter(uuid_t uuid, void *arg, int *phase)
{
	struct chk_instance	*ins = arg;
	struct chk_pool_rec	*cpr;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	D_ASSERT(ins != NULL);

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, uuid, sizeof(uuid_t));

	rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
	if (rc == 0) {
		cpr = (struct chk_pool_rec *)riov.iov_buf;
		*phase = cpr->cpr_bk.cb_phase;
		D_ASSERT(*phase >= 0);
	} else if (rc == -DER_NONEXIST && ins->ci_start_flags & CSF_ORPHAN_POOL) {
		*phase = CHK_INVAL_PHASE;
		rc = 0;
	}

	return rc;
}

int
chk_engine_start(uint64_t gen, uint32_t rank_nr, d_rank_t *ranks, uint32_t policy_nr,
		 struct chk_policy *policies, int pool_nr, uuid_t pools[], uint32_t api_flags,
		 int phase, d_rank_t leader, uint32_t flags, uuid_t iv_uuid,
		 struct ds_pool_clues *clues)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	struct umem_attr		 uma = { 0 };
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	d_rank_t			 myrank = dss_self_rank();
	int				 rc;
	int				 rc1;

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

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pool_btr, &ins->ci_pool_hdl);
	if (rc != 0)
		goto out_tree;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_PA, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pending_btr, &ins->ci_pending_hdl);
	if (rc != 0)
		goto out_tree;

	rc = chk_engine_start_prep(ins, rank_nr, ranks, policy_nr, policies,
				   pool_nr, pools, gen, phase, api_flags, leader, flags);
	if (rc != 0)
		goto out_tree;

	if (chk_is_on_leader(gen, leader, true)) {
		ins->ci_iv_ns = chk_leader_get_iv_ns();
		if (unlikely(ins->ci_iv_ns == NULL))
			goto out_tree;
	} else {
		uuid_unparse_lower(iv_uuid, uuid_str);
		rc = crt_group_secondary_create(uuid_str, NULL, ins->ci_ranks, &ins->ci_iv_group);
		if (rc != 0)
			goto out_tree;

		rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, iv_uuid, ins->ci_iv_group,
				     &ins->ci_iv_id, &ins->ci_iv_ns);
		if (rc != 0)
			goto out_group;

		ds_iv_ns_update(ins->ci_iv_ns, leader, ins->ci_iv_ns->iv_master_term + 1);
	}

	uuid_copy(cbk->cb_iv_uuid, iv_uuid);
	rc = chk_engine_start_post(ins);
	if (rc != 0)
		goto out_stop;

	rc = ds_pool_clues_init(chk_engine_pool_filter, ins, clues);
	if (rc != 0)
		goto out_stop;

	ins->ci_sched_running = 1;

	rc = dss_ult_create(chk_engine_sched, ins, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &ins->ci_sched);
	if (rc != 0) {
		ins->ci_sched_running = 0;
		goto out_stop;
	}

	goto out_done;

out_stop:
	chk_pool_stop_all(ins, CHK__CHECK_POOL_STATUS__CPS_IMPLICATED, NULL);
	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_time.ct_stop_time = time(NULL);
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		rc1 = chk_bk_update_engine(cbk);
		if (rc1 != 0)
			D_WARN(DF_ENGINE" failed to update engine bookmark: "DF_RC"\n",
			       DP_ENGINE(ins), DP_RC(rc1));
	}
	chk_iv_ns_cleanup(&ins->ci_iv_ns);
out_group:
	if (ins->ci_iv_group != NULL) {
		crt_group_secondary_destroy(ins->ci_iv_group);
		ins->ci_iv_group = NULL;
	}
out_tree:
	chk_destroy_pending_tree(ins);
	chk_destroy_pool_tree(ins);
out_done:
	ins->ci_starting = 0;
out_log:
	if (rc >= 0) {
		D_INFO(DF_ENGINE " %s on rank %u with api_flags %x, phase %d, leader %u, "
		       "flags %x, iv "DF_UUIDF": rc %d\n",
		       DP_ENGINE(ins), chk_is_ins_reset(ins, api_flags) ? "start" : "resume",
		       myrank, api_flags, phase, leader, flags, DP_UUID(iv_uuid), rc);

		chk_ranks_dump(ins->ci_ranks->rl_nr, ins->ci_ranks->rl_ranks);
		chk_pools_dump(&ins->ci_pool_list, pool_nr, pools);
	} else {
		D_ERROR(DF_ENGINE" failed to start on rank %u with %d pools, api_flags %x, "
			"phase %d, leader %u, flags %x, gen "DF_X64", iv "DF_UUIDF": "DF_RC"\n",
			DP_ENGINE(ins), myrank, pool_nr, api_flags, phase, leader, flags, gen,
			DP_UUID(iv_uuid), DP_RC(rc));
	}

	return rc;
}

int
chk_engine_stop(uint64_t gen, int pool_nr, uuid_t pools[], uint32_t *flags)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pool_rec	*cpr;
	d_rank_t		 myrank = dss_self_rank();
	int			 rc = 0;
	int			 i;
	int			 active = false;

	if (gen != 0 && gen != cbk->cb_gen)
		D_GOTO(log, rc = -DER_NOTAPPLICABLE);

	if (cbk->cb_magic != CHK_BK_MAGIC_ENGINE)
		D_GOTO(log, rc = -DER_NOTAPPLICABLE);

	if (ins->ci_starting)
		D_GOTO(log, rc = -DER_BUSY);

	if (ins->ci_stopping || ins->ci_sched_exiting)
		D_GOTO(log, rc = -DER_INPROGRESS);

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(log, rc = -DER_ALREADY);

	ins->ci_stopping = 1;

	D_INFO(DF_ENGINE" stopping on rank %u with %d pools\n", DP_ENGINE(ins), myrank, pool_nr);

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

	if (ins->ci_pool_stopped)
		*flags = CSF_POOL_STOPPED;

	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
		if (!cpr->cpr_done && !cpr->cpr_skip && !cpr->cpr_stop) {
			D_ASSERTF(pool_nr != 0, "Hit active pool "DF_UUIDF" after stop all\n",
				  DP_UUID(cpr->cpr_uuid));

			active = true;
			break;
		}
	}

	if (!active) {
		chk_stop_sched(ins);
		/* To indicate that there is no active pool(s) on this rank. */
		rc = 1;
	}

out:
	ins->ci_pool_stopped = 0;
	ins->ci_stopping = 0;
log:
	if (rc >= 0 || rc == -DER_ALREADY) {
		D_INFO(DF_ENGINE" stopped on rank %u with %d pools: rc %d\n", DP_ENGINE(ins),
		       myrank, pool_nr, rc);

		chk_pools_dump(NULL, pool_nr, pools);
		if (rc == -DER_ALREADY)
			rc = 1;
	} else {
		D_ERROR(DF_ENGINE" failed to stop on rank %u with %d pools, "
			"gen "DF_X64": "DF_RC"\n", DP_ENGINE(ins), myrank, pool_nr, gen, DP_RC(rc));
	}

	return rc;
}

/* Query one pool shard on one xstream. */
static int
chk_engine_query_one(void *args)
{
	struct chk_query_pool_shard	*shard = args;
	struct chk_query_target		*target;
	char				*path = NULL;
	daos_handle_t			 poh = DAOS_HDL_INVAL;
	vos_pool_info_t			 info;
	int				 tid = dss_get_module_info()->dmi_tgt_id;
	int				 rc;

	target = &shard->cqps_targets[tid];
	target->cqt_rank = dss_self_rank();
	target->cqt_tgt = tid;

	rc = ds_mgmt_tgt_pool_exist(shard->cqps_uuid,  &path);
	/* We allow the target nonexist. */
	if (rc <= 0)
		goto out;

	rc = vos_pool_open(path, shard->cqps_uuid, VOS_POF_FOR_CHECK_QUERY, &poh);
	if (rc != 0) {
		D_ERROR("Failed to open vos pool "DF_UUIDF" on target %u/%d: "DF_RC"\n",
			DP_UUID(shard->cqps_uuid), dss_self_rank(), tid, DP_RC(rc));
		goto out;
	}

	rc = vos_pool_query(poh, &info);
	if (rc != 0) {
		D_ERROR("Failed to query vos pool "DF_UUIDF" on target %u/%d: "DF_RC"\n",
			DP_UUID(shard->cqps_uuid), dss_self_rank(), tid, DP_RC(rc));
		goto out;
	}

	target->cqt_ins_status = info.pif_chk.cpi_ins_status;
	target->cqt_statistics = info.pif_chk.cpi_statistics;
	target->cqt_time = info.pif_chk.cpi_time;

out:
	if (daos_handle_is_valid(poh))
		vos_pool_close(poh);
	D_FREE(path);
	return rc;
}

static int
chk_engine_query_pool(uuid_t uuid, void *args)
{
	struct chk_query_pool_args	*cqpa = args;
	struct chk_query_pool_shard	*shard;
	struct chk_query_pool_shard	*new_shards;
	struct chk_bookmark		 cbk;
	struct dss_coll_args		 coll_args = { 0 };
	struct dss_coll_ops		 coll_ops = { 0 };
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	int				 rc = 0;

	if (cqpa->cqpa_idx == cqpa->cqpa_cap) {
		D_REALLOC_ARRAY(new_shards, cqpa->cqpa_shards, cqpa->cqpa_cap, cqpa->cqpa_cap << 1);
		if (new_shards == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		cqpa->cqpa_shards = new_shards;
		cqpa->cqpa_cap <<= 1;
	}

	shard = &cqpa->cqpa_shards[cqpa->cqpa_idx++];
	uuid_copy(shard->cqps_uuid, uuid);
	shard->cqps_rank = dss_self_rank();

	uuid_unparse_lower(uuid, uuid_str);
	rc = chk_bk_fetch_pool(&cbk, uuid_str);
	if (rc == -DER_NONEXIST) {
		shard->cqps_status = CHK__CHECK_POOL_STATUS__CPS_UNCHECKED;
		shard->cqps_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
		memset(&shard->cqps_statistics, 0, sizeof(shard->cqps_statistics));
		memset(&shard->cqps_time, 0, sizeof(shard->cqps_time));
		shard->cqps_target_nr = 0;
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
	shard->cqps_target_nr = dss_tgt_nr;

	coll_ops.co_func = chk_engine_query_one;
	coll_args.ca_func_args = shard;

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, 0);

out:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG,
		 DF_ENGINE" on rank %u query pool "DF_UUIDF": "DF_RC"\n",
		 DP_ENGINE(cqpa->cqpa_ins), dss_self_rank(), DP_UUID(uuid), DP_RC(rc));
	return rc;
}

int
chk_engine_query(uint64_t gen, int pool_nr, uuid_t pools[], uint32_t *ins_status,
		 uint32_t *ins_phase, uint32_t *shard_nr, struct chk_query_pool_shard **shards,
		 uint64_t *l_gen)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_query_pool_args	 cqpa = { 0 };
	int				 rc = 0;
	int				 i;

	/*
	 * We will support to check query from new check leader under the case of old leader
	 * crashed, that may have different check generation. So do not check "cb_gen" here,
	 * instead, current engine's "cb_gen" will be returned to leader to indicate whether
	 * it is new leader or not.
	 */

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
		*ins_status = ins->ci_bk.cb_ins_status;
		*ins_phase = ins->ci_bk.cb_phase;
		*shards = cqpa.cqpa_shards;
		*shard_nr = cqpa.cqpa_idx;
		*l_gen = ins->ci_bk.cb_gen;
	}

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_DBG,
		 DF_ENGINE" on rank %u handle query with gen "DF_X64" for %d pools, status %u, "
		 "phase %u: "DF_RC"\n", DP_ENGINE(ins), dss_self_rank(), gen, pool_nr,
		 ins->ci_bk.cb_ins_status, ins->ci_bk.cb_phase, DP_RC(rc));

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

	/* For check engine on the leader, related rank has already been marked as "dead". */
	if (chk_is_on_leader(cbk->cb_gen, prop->cp_leader, true))
		goto group;

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
	 *	 So here, it is not necessary to find out the affected pools and fail them
	 *	 immediately when the death event is reported, instead, it will be handled
	 *	 sometime later as the DAOS check going.
	 */

group:
	if (ins->ci_iv_group != NULL)
		rc = crt_group_secondary_modify(ins->ci_iv_group, rank_list, rank_list,
						CRT_GROUP_MOD_OP_REPLACE, version);

out:
	if (rc == 0) {
		d_rank_list_free(ins->ci_ranks);
		ins->ci_ranks = rank_list;
		rank_list = NULL;
	}

	d_rank_list_free(rank_list);
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" on rank %u mark rank %u as dead with gen "
			 DF_X64", version %u: "DF_RC"\n",
			 DP_ENGINE(ins), dss_self_rank(), rank, gen, version, DP_RC(rc));

	return rc;
}

static int
chk_engine_act_internal(struct chk_instance *ins, uint64_t seq, uint32_t act, bool locked)
{
	struct chk_pending_rec	*cpr = NULL;
	int			 rc;

	rc = chk_pending_del(ins, seq, locked, &cpr);
	if (rc == 0) {
		/* The cpr will be destroyed by the waiter via chk_engine_report(). */
		D_ASSERT(cpr->cpr_busy);

		ABT_mutex_lock(cpr->cpr_mutex);
		/*
		 * It is the control plane's duty to guarantee that the decision is a valid
		 * action from the report options. Otherwise, related inconsistency will be
		 * ignored.
		 */
		cpr->cpr_action = act;
		ABT_cond_broadcast(cpr->cpr_cond);
		ABT_mutex_unlock(cpr->cpr_mutex);
	}

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u takes action for seq "DF_X64" with action %u: %d\n",
		 DP_ENGINE(ins), dss_self_rank(), seq, act, rc);

	return rc;
}

int
chk_engine_act(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, uint32_t flags)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_property	*prop = &ins->ci_prop;
	struct chk_pool_rec	*pool = NULL;
	struct chk_pool_rec	*pool_tmp = NULL;
	struct chk_pending_rec	*cpr = NULL;
	struct chk_pending_rec	*cpr_tmp = NULL;
	int			 rc;

	if (ins->ci_bk.cb_gen != gen)
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

	rc = chk_engine_act_internal(ins, seq, act, false);
	if (rc == -DER_NONEXIST || rc == -DER_NO_HDL)
		rc = 0;

	if (rc != 0 || !(flags & CAF_FOR_ALL))
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

				rc = chk_engine_act_internal(ins, cpr->cpr_seq, act, true);
				if (rc != 0)
					break;
			}
			ABT_rwlock_unlock(ins->ci_abt_lock);
		}
		chk_pool_put(pool);
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
chk_engine_cont_list(uint64_t gen, uuid_t pool_uuid, uuid_t **conts, uint32_t *count)
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

	rc = chk_engine_cont_list_init(pool_uuid, &aggregator);
	if (rc != 0)
		goto out;

	coll_args.ca_func_args = &coll_args.ca_stream_args;
	coll_args.ca_aggregator = &aggregator;

	coll_ops.co_func = chk_engine_cont_list_one;
	coll_ops.co_reduce = chk_engine_cont_list_reduce;
	coll_ops.co_reduce_arg_alloc = chk_engine_cont_list_alloc;
	coll_ops.co_reduce_arg_free = chk_engine_cont_list_free;

	rc = ds_pool_task_collective_reduce(pool_uuid,
					    PO_COMP_ST_NEW | PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT,
					    &coll_ops, &coll_args, 0);

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
chk_engine_pool_start(uint64_t gen, uuid_t uuid, uint32_t phase, uint32_t flags)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_pool_rec	*cpr = NULL;
	struct chk_bookmark	*cbk;
	struct chk_bookmark	 new;
	char			 uuid_str[DAOS_UUID_STR_SIZE];
	d_iov_t			 riov;
	d_iov_t			 kiov;
	int			 rc;

	if (ins->ci_bk.cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_SHUTDOWN);

	uuid_unparse_lower(uuid, uuid_str);

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
	if (rc != 0 && rc != -DER_NONEXIST)
		goto out;

	if (rc == -DER_NONEXIST) {
		if (!(flags & CPSF_FOR_ORPHAN))
			goto out;

		/* It is for orphan pool, add the chk_pool_rec. */
		rc = ds_mgmt_pool_exist(uuid);
		if (unlikely(rc == 0))
			D_GOTO(out, rc = -DER_NONEXIST);

		rc = chk_bk_fetch_pool(&new, uuid_str);
		if (rc != 0 && rc != -DER_NONEXIST)
			goto out;

		if (rc == -DER_NONEXIST) {
			memset(&new, 0, sizeof(new));
			new.cb_magic = CHK_BK_MAGIC_POOL;
			new.cb_version = DAOS_CHK_VERSION;
			new.cb_gen = ins->ci_bk.cb_gen;
			new.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
			new.cb_time.ct_start_time = time(NULL);
		}

		rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid, dss_self_rank(),
					&new, ins, NULL, NULL, NULL, &cpr);
		if (rc != 0)
			goto out;
	} else {
		cpr = (struct chk_pool_rec *)riov.iov_buf;
	}

	if (cpr->cpr_stop)
		D_GOTO(out, rc = -DER_SHUTDOWN);

	/* Maybe resent one. */
	if (unlikely(cpr->cpr_started))
		D_GOTO(out, rc = -DER_ALREADY);

	if (flags & CPSF_NOT_EXPORT_PS)
		cpr->cpr_not_export_ps = 1;

	cbk = &cpr->cpr_bk;
	chk_pool_get(cpr);

	rc = ds_pool_start(uuid);
	if (rc != 0)
		D_GOTO(put, rc = (rc == -DER_NONEXIST ? 1 : rc));

	if (cbk->cb_phase < phase) {
		cbk->cb_phase = cbk->cb_phase;
		/* QUEST: How to estimate the left time? */
		cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE - cbk->cb_phase;
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0) {
			ds_pool_stop(uuid);
			goto put;
		}
	}

	cpr->cpr_started = 1;

put:
	if (rc != 0) {
		if (rc > 0) {
			chk_pool_stop_one(ins, uuid, CHK__CHECK_POOL_STATUS__CPS_CHECKED,
					  CHK__CHECK_SCAN_PHASE__CSP_DONE, NULL);
			rc = 0;
		} else {
			chk_ins_set_fail(ins, cbk->cb_phase > phase ? cbk->cb_phase : phase);
			chk_pool_stop_one(ins, uuid, CHK__CHECK_POOL_STATUS__CPS_FAILED,
					  CHK_INVAL_PHASE, NULL);
		}
	}

	chk_pool_put(cpr);

out:
	if (unlikely(rc == -DER_ALREADY))
		rc = 0;
	else
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" on rank %u start pool "DF_UUIDF" at phase %u: "DF_RC"\n",
			 DP_ENGINE(ins), dss_self_rank(), DP_UUID(uuid), phase, DP_RC(rc));

	return rc;
}

int
chk_engine_pool_mbs(uint64_t gen, uuid_t uuid, uint32_t phase, const char *label, uint64_t seq,
		    uint32_t flags, uint32_t mbs_nr, struct chk_pool_mbs *mbs_array,
		    struct rsvc_hint *hint)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_bookmark		*cbk;
	struct chk_pool_rec		*cpr = NULL;
	struct ds_pool_svc		*svc = NULL;
	struct chk_pool_mbs_args	*cpma = NULL;
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	d_iov_t				 riov;
	d_iov_t				 kiov;
	int				 rc;
	int				 i;

	if (ins->ci_bk.cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_SHUTDOWN);

	rc = ds_pool_svc_lookup_leader(uuid, &svc, hint);
	if (rc != 0)
		goto out;

	d_iov_set(&riov, NULL, 0);
	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
	if (rc != 0)
		goto out;

	cpr = (struct chk_pool_rec *)riov.iov_buf;
	cbk = &cpr->cpr_bk;

	if (cpr->cpr_stop)
		D_GOTO(out, rc = -DER_SHUTDOWN);

	/* Maybe resent one. */
	if (unlikely(cpr->cpr_mbs != NULL))
		D_GOTO(out, rc = 0);

	D_ASSERT(cpr->cpr_thread == ABT_THREAD_NULL);
	chk_pool_get(cpr);

	D_ALLOC_ARRAY(cpr->cpr_mbs, mbs_nr);
	if (cpr->cpr_mbs == NULL)
		D_GOTO(put, rc = -DER_NOMEM);

	cpr->cpr_shard_nr = mbs_nr;
	for (i = 0; i < mbs_nr; i++) {
		D_ALLOC_ARRAY(cpr->cpr_mbs[i].cpm_tgt_status, mbs_array[i].cpm_tgt_nr);
		if (cpr->cpr_mbs[i].cpm_tgt_status == NULL)
			D_GOTO(put, rc = -DER_NOMEM);

		cpr->cpr_mbs[i].cpm_rank = mbs_array[i].cpm_rank;
		cpr->cpr_mbs[i].cpm_tgt_nr = mbs_array[i].cpm_tgt_nr;
		memcpy(cpr->cpr_mbs[i].cpm_tgt_status, mbs_array[i].cpm_tgt_status,
		       sizeof(*mbs_array[i].cpm_tgt_status) * mbs_array[i].cpm_tgt_nr);
	}

	rc = chk_dup_string(&cpr->cpr_label, label, label != NULL ? strlen(label) : 0);
	if (rc != 0)
		goto put;

	cpr->cpr_label_seq = seq;
	if (flags & CMF_REPAIR_LABEL)
		cpr->cpr_delay_label = 1;

	if (cbk->cb_phase < phase) {
		cbk->cb_phase = phase;
		/* QUEST: How to estimate the left time? */
		cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__CSP_DONE - cbk->cb_phase;
		uuid_unparse_lower(cpr->cpr_uuid, uuid_str);
		rc = chk_bk_update_pool(cbk, uuid_str);
		if (rc != 0)
			goto put;
	}

	D_ALLOC_PTR(cpma);
	if (cpma == NULL)
		D_GOTO(put, rc = -DER_NOMEM);

	cpma->cpma_svc = svc;
	cpma->cpma_cpr = cpr;
	svc = NULL;

	rc = dss_ult_create(chk_engine_pool_ult, cpma, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &cpr->cpr_thread);
	if (rc != 0) {
		svc = cpma->cpma_svc;
		D_FREE(cpma);
		rc = dss_abterr2der(rc);
		D_ERROR(DF_ENGINE" on rank %u failed to create ULT for pool "DF_UUIDF": "DF_RC"\n",
			DP_ENGINE(ins), dss_self_rank(), DP_UUID(uuid), DP_RC(rc));
	}

put:
	if (rc != 0) {
		for (i = 0; i < cpr->cpr_shard_nr; i++)
			D_FREE(cpr->cpr_mbs[i].cpm_tgt_status);
		D_FREE(cpr->cpr_mbs);
		D_FREE(cpr->cpr_label);
		cpr->cpr_shard_nr = 0;
		cpr->cpr_delay_label = 0;

		chk_ins_set_fail(ins, cbk->cb_phase > phase ? cbk->cb_phase : phase);
		chk_pool_stop_one(ins, uuid, CHK__CHECK_POOL_STATUS__CPS_FAILED,
				  CHK_INVAL_PHASE, NULL);
		chk_pool_put(cpr);
	}
out:
	if (svc != NULL)
		ds_pool_svc_put_leader(svc);

	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u set pool mbs "DF_UUIDF" at phase %u: "DF_RC"\n",
		 DP_ENGINE(ins), dss_self_rank(), DP_UUID(uuid), phase, DP_RC(rc));

	return rc;
}

/*
 * \return	Positive value if interaction is interrupted, such as check stop.
 *		Zero on success.
 *		Negative value if error.
 */
static int
chk_engine_report(struct chk_report_unit *cru, uint64_t *seq, int *decision)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_pending_rec	*cpr = NULL;
	struct chk_pending_rec	*tmp = NULL;
	struct chk_pool_rec	*pool = NULL;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	int			 rc;

	D_ASSERT(cru->cru_pool != NULL);

	if (*seq == 0) {

new_seq:
		*seq = chk_report_seq_gen(ins);
	}

	if (cru->cru_act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT) {
		d_iov_set(&riov, NULL, 0);
		d_iov_set(&kiov, cru->cru_pool, sizeof(uuid_t));
		rc = dbtree_lookup(ins->ci_pool_hdl, &kiov, &riov);
		if (rc != 0)
			goto log;

		pool = (struct chk_pool_rec *)riov.iov_buf;

		rc = chk_pending_add(ins, &pool->cpr_pending_list, NULL, *cru->cru_pool, *seq,
				     cru->cru_rank, cru->cru_cla, &cpr);
		if (unlikely(rc == -DER_AGAIN))
			goto new_seq;

		if (rc != 0)
			goto log;
	}

	rc = chk_report_remote(ins->ci_prop.cp_leader, ins->ci_bk.cb_gen, cru->cru_cla,
			       cru->cru_act, cru->cru_result, cru->cru_rank, cru->cru_target,
			       cru->cru_pool, cru->cru_pool_label, cru->cru_cont,
			       cru->cru_cont_label, cru->cru_obj, cru->cru_dkey,
			       cru->cru_akey, cru->cru_msg, cru->cru_option_nr, cru->cru_options,
			       cru->cru_detail_nr, cru->cru_details, *seq);
	if (unlikely(rc == -DER_AGAIN)) {
		D_ASSERT(cru->cru_act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT);

		rc = chk_pending_del(ins, *seq, false, &tmp);
		if (rc == 0)
			D_ASSERT(tmp == NULL);
		else if (rc != -DER_NONEXIST)
			goto log;

		chk_pending_destroy(cpr);
		cpr = NULL;

		goto new_seq;
	}

	/* Check cpr->cpr_action for the case of "dmg check repair" by race. */
	if (rc == 0 && pool != NULL &&
	    likely(cpr->cpr_action == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT))
		pool->cpr_bk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_PENDING;

log:
	D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u report with class %u, action %u, seq "
		 DF_X64", handle_rc %d, report_rc %d\n", DP_ENGINE(ins),
		 cru->cru_rank, cru->cru_cla, cru->cru_act, *seq, cru->cru_result, rc);

	if (rc != 0 || cpr == NULL)
		goto out;

	D_ASSERT(cpr->cpr_busy);

	D_INFO(DF_ENGINE" on rank %u need interaction for class %u\n",
	       DP_ENGINE(ins), cru->cru_rank, cru->cru_cla);

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
	if (pool != NULL && pool->cpr_bk.cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_PENDING)
		pool->cpr_bk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;

	if (cpr != NULL)
		chk_pending_destroy(cpr);

	return rc;
}

int
chk_engine_notify(struct chk_iv *iv)
{
	struct chk_instance	*ins = chk_engine;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pool_rec	*cpr;
	int			 rc = 0;

	if (cbk->cb_gen != iv->ci_gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (!uuid_is_null(iv->ci_uuid)) {
		rc = chk_pool_handle_notify(ins, iv);
		goto out;
	}

	/* Pool service leader must specify the pool UUID when notify the pool shards. */
	if (iv->ci_from_psl)
		D_GOTO(out, rc = -DER_INVAL);

	if (iv->ci_phase >= CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS) {
		ins->ci_orphan_done = 1;
		D_INFO(DF_ENGINE" leader completed orphan pools process\n", DP_ENGINE(ins));
	}

	switch (iv->ci_ins_status) {
	case CHK__CHECK_INST_STATUS__CIS_RUNNING:
		if (unlikely(iv->ci_phase < cbk->cb_phase)) {
			rc = -DER_NOTAPPLICABLE;
		} else if (iv->ci_phase != cbk->cb_phase) {
			cbk->cb_phase = iv->ci_phase;
			rc = chk_bk_update_engine(cbk);
			if (rc == 0)
				rc = chk_pools_update_bk(ins, iv->ci_phase);
		}
		break;
	case CHK__CHECK_INST_STATUS__CIS_FAILED:
	case CHK__CHECK_INST_STATUS__CIS_IMPLICATED:
		/* Leader notifies the engine to exit. */
		ins->ci_implicated = 1;
		chk_stop_sched(ins);
		break;
	case CHK__CHECK_INST_STATUS__CIS_COMPLETED:
		/*
		 * Usually, the check leader will not notify its COMPLETE to check engines
		 * unless the check leader has not notify 'ci_orphan_done' yet. Under such
		 * case, there should be no in-processing pools on check engines.
		 */
		d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
			if (!cpr->cpr_done && !cpr->cpr_skip && !cpr->cpr_stop) {
				D_ERROR(DF_ENGINE" there is at least one pool "
					DF_UUID" in processing but leader 'COMPLETED\n",
					DP_ENGINE(ins), DP_UUID(cpr->cpr_uuid));
				D_GOTO(out, rc = -DER_PROTO);
			}
		}
		break;
	default:
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);
	}

out:
	D_CDEBUG(rc != 0 && rc != -DER_NOTAPPLICABLE, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u got notification from rank %u, for pool " DF_UUIDF
		 ", phase %u, ins_status %u, pool_status %u, gen "DF_X64", seq "DF_X64": "DF_RC"\n",
		 DP_ENGINE(ins), dss_self_rank(), iv->ci_rank, DP_UUID(iv->ci_uuid), iv->ci_phase,
		 iv->ci_ins_status, iv->ci_pool_status, iv->ci_gen, iv->ci_seq, DP_RC(rc));

	return (rc == 0 || rc == -DER_NOTAPPLICABLE) ? 0 : rc;
}

void
chk_engine_rejoin(void *args)
{
	struct chk_instance		*ins = chk_engine;
	struct chk_property		*prop = &ins->ci_prop;
	struct chk_bookmark		*cbk = &ins->ci_bk;
	uuid_t				*pools = NULL;
	struct chk_iv			 iv = { 0 };
	struct umem_attr		 uma = { 0 };
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	d_rank_t			 myrank = dss_self_rank();
	uint32_t			 pool_nr = 0;
	uint32_t			 flags = 0;
	int				 rc = 0;
	int				 rc1;
	bool				 need_join = false;

	if (cbk->cb_magic != CHK_BK_MAGIC_ENGINE)
		goto out_log;

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING &&
	    cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_PAUSED)
		goto out_log;

	/* We do NOT support leader (and its associated engine ) to rejoin former check instance. */
	if (chk_is_on_leader(cbk->cb_gen, prop->cp_leader, true))
		goto out_log;

	if (ins->ci_ranks == NULL)
		goto out_log;

	D_ASSERT(ins->ci_starting == 0);
	D_ASSERT(ins->ci_stopping == 0);
	D_ASSERT(ins->ci_sched_running == 0);
	D_ASSERT(ins->ci_iv_group == NULL);
	D_ASSERT(ins->ci_iv_ns == NULL);
	D_ASSERT(ins->ci_sched == ABT_THREAD_NULL);
	D_ASSERT(daos_handle_is_inval(ins->ci_pool_hdl));
	D_ASSERT(d_list_empty(&ins->ci_pool_list));
	D_ASSERT(daos_handle_is_inval(ins->ci_pending_hdl));

	ins->ci_rejoining = 1;
	ins->ci_starting = 1;
	ins->ci_started = 0;
	ins->ci_start_flags = 0;

	need_join = true;
	uma.uma_id = UMEM_CLASS_VMEM;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_POOL, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pool_btr, &ins->ci_pool_hdl);
	if (rc != 0)
		goto out_tree;

	rc = dbtree_create_inplace(DBTREE_CLASS_CHK_PA, 0, CHK_BTREE_ORDER, &uma,
				   &ins->ci_pending_btr, &ins->ci_pending_hdl);
	if (rc != 0)
		goto out_tree;

	uuid_unparse_lower(cbk->cb_iv_uuid, uuid_str);
	rc = crt_group_secondary_create(uuid_str, NULL, ins->ci_ranks, &ins->ci_iv_group);
	if (rc != 0)
		goto out_tree;

	rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, cbk->cb_iv_uuid, ins->ci_iv_group,
			     &ins->ci_iv_id, &ins->ci_iv_ns);
	if (rc != 0)
		goto out_group;

	ds_iv_ns_update(ins->ci_iv_ns, prop->cp_leader, ins->ci_iv_ns->iv_master_term + 1);

again:
	/* Ask leader whether this engine can rejoin or not. */
	rc = chk_rejoin_remote(prop->cp_leader, cbk->cb_gen, myrank, cbk->cb_iv_uuid, &flags,
			       &pool_nr, &pools);
	if (rc != 0) {
		if ((rc == -DER_OOG || rc == -DER_GRPVER) && !ins->ci_pause) {
			D_INFO(DF_ENGINE" Someone is not ready %d, let's rejoin after 1 sec\n",
			       DP_ENGINE(ins), rc);
			dss_sleep(1000);
			if (!ins->ci_pause)
				goto again;
		}

		goto out_iv;
	}

	if (pool_nr == 0) {
		need_join = false;
		D_GOTO(out_iv, rc = 1);
	}

	rc = chk_pools_load_list(ins, cbk->cb_gen, 0, pool_nr, pools, NULL);
	if (rc != 0)
		goto out_notify;

	rc = chk_engine_start_post(ins);
	if (rc != 0)
		goto out_stop;

	ins->ci_sched_running = 1;

	rc = dss_ult_create(chk_engine_sched, ins, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &ins->ci_sched);
	if (rc != 0) {
		ins->ci_sched_running = 0;
		goto out_stop;
	}

	if (flags & CRF_ORPHAN_DONE)
		ins->ci_orphan_done = 1;

	goto out_log;

out_stop:
	chk_pool_stop_all(ins, CHK__CHECK_POOL_STATUS__CPS_IMPLICATED, NULL);
	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		cbk->cb_time.ct_stop_time = time(NULL);
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		rc1 = chk_bk_update_engine(cbk);
		if (rc1 != 0)
			D_WARN(DF_ENGINE" failed to update engine bookmark: "DF_RC"\n",
			       DP_ENGINE(ins), DP_RC(rc1));
	}
out_notify:
	iv.ci_gen = cbk->cb_gen;
	iv.ci_phase = cbk->cb_phase;
	iv.ci_ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
	iv.ci_to_leader = 1;

	/* Notify the leader that check instance exit on the engine. */
	rc1 = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_TO_ROOT, CRT_IV_SYNC_NONE, true);
	D_CDEBUG(rc1 != 0, DLOG_ERR, DLOG_INFO,
		 DF_ENGINE" on rank %u notify leader for its exit, status %u: rc1 = %d\n",
		 DP_ENGINE(ins), myrank, cbk->cb_ins_status, rc1);
out_iv:
	chk_iv_ns_cleanup(&ins->ci_iv_ns);
out_group:
	if (ins->ci_iv_group != NULL) {
		crt_group_secondary_destroy(ins->ci_iv_group);
		ins->ci_iv_group = NULL;
	}
out_tree:
	chk_destroy_pending_tree(ins);
	chk_destroy_pool_tree(ins);
out_log:
	if (need_join)
		D_CDEBUG(rc < 0, DLOG_ERR, DLOG_INFO,
			 DF_ENGINE" rejoin on rank %u with iv "DF_UUIDF": "DF_RC"\n",
			 DP_ENGINE(ins), myrank, DP_UUID(cbk->cb_iv_uuid), DP_RC(rc));
	ins->ci_rejoining = 0;
	ins->ci_starting = 0;
	ins->ci_inited = 1;
}

void
chk_engine_pause(void)
{
	struct chk_instance	*ins = chk_engine;

	chk_stop_sched(ins);
	D_ASSERT(d_list_empty(&ins->ci_pool_list));
}

int
chk_engine_init(void)
{
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_bookmark		*cbk;
	int				 rc;

	rc = chk_ins_init(&chk_engine);
	if (rc != 0)
		goto fini;

	chk_report_seq_init(chk_engine);

	/*
	 * DAOS global consistency check depends on all related engines' local
	 * consistency. If hit some local data corruption, then it is possible
	 * that local consistency is not guaranteed. Need to break and resolve
	 * related local inconsistency firstly.
	 */

	cbk = &chk_engine->ci_bk;
	rc = chk_bk_fetch_engine(cbk);
	if (rc == -DER_NONEXIST)
		goto prop;

	/* It may be caused by local data corruption, let's break. */
	if (rc != 0)
		goto fini;

	if (cbk->cb_magic != 0 && cbk->cb_magic != CHK_BK_MAGIC_ENGINE) {
		D_ERROR("Hit corrupted engine bookmark on rank %u: %u vs %u\n",
			dss_self_rank(), cbk->cb_magic, CHK_BK_MAGIC_ENGINE);
		D_GOTO(fini, rc = -DER_IO);
	}

	if (cbk->cb_ins_status == CHK__CHECK_INST_STATUS__CIS_RUNNING) {
		/*
		 * Leader crashed before normally exit, reset the status as 'PAUSED'
		 * to avoid blocking next CHK_START.
		 */
		cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_PAUSED;
		cbk->cb_time.ct_stop_time = time(NULL);
		rc = chk_bk_update_engine(cbk);
		if (rc != 0) {
			D_ERROR(DF_ENGINE" failed to reset status as 'PAUSED': "DF_RC"\n",
				DP_ENGINE(chk_engine), DP_RC(rc));
			goto fini;
		}

		ctpa.ctpa_gen = cbk->cb_gen;
		rc = chk_traverse_pools(chk_pools_pause_cb, &ctpa);
		/*
		 * Failed to reset pool status will not affect next check start, so it is not fatal,
		 * but related check query result may be confused for user.
		 */
		if (rc != 0)
			D_WARN(DF_ENGINE" failed to reset pools status as 'PAUSED': "DF_RC"\n",
			       DP_ENGINE(chk_engine), DP_RC(rc));
	}

prop:
	rc = chk_prop_fetch(&chk_engine->ci_prop, &chk_engine->ci_ranks);
	if (rc == -DER_NONEXIST)
		rc = 0;
fini:
	if (rc != 0)
		chk_ins_fini(&chk_engine);
	return rc;
}

void
chk_engine_fini(void)
{
	chk_ins_fini(&chk_engine);
}
