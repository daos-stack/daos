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

static struct chk_instance	*chk_engine;

struct chk_traverse_pools_args {
	uint64_t			 ctpa_gen;
	struct chk_instance		*ctpa_ins;
	uint32_t			 ctpa_status;
};

struct chk_engine_clues_args {
	uint32_t			 ceca_pool_nr;
	uuid_t				*ceca_pools;
};

struct chk_query_pool_args {
	uint32_t			 cqpa_cap;
	uint32_t			 cqpa_idx;
	struct chk_query_pool_shard	*cqpa_shards;
};

struct chk_query_xstream_args {
	uuid_t				 cqxa_uuid;
	struct chk_query_pool_args	*cqxa_args;
	struct chk_query_target		 cqxa_target;
};

static inline bool
chk_engine_on_leader(d_rank_t leader)
{
	return dss_self_rank() == leader;
}

static void
chk_engine_exit(struct chk_instance *ins, uint32_t ins_status, uint32_t pool_status)
{
	struct chk_bookmark	*cbk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	d_iov_t			 kiov;
	uuid_t			 uuid;
	struct chk_iv		 iv;
	int			 rc;

	d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
		cbk = &cpr->cpr_bk;
		uuid_copy(uuid, cpr->cpr_uuid);

		if (cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_CHECKING ||
		    cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_PENDING) {
			cbk->cb_pool_status = pool_status;
			cbk->cb_time.ct_stop_time = time(NULL);
			chk_bk_update_pool(cbk, uuid);
		}

		d_iov_set(&kiov, uuid, sizeof(uuid_t));
		rc = dbtree_delete(ins->ci_pool_hdl, BTR_PROBE_EQ, &kiov, NULL);
		if (rc != 0)
			D_ERROR("Check engine (gen "DF_X64") on rank %u failed to delete "
				"pool record "DF_UUID" when exit: "DF_RC"\n",
				cbk->cb_gen, dss_self_rank(), DP_UUID(uuid), DP_RC(rc));
	}

	cbk = &ins->ci_bk;

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

		/* Synchronously notify the leader that check instance exit on the engine. */
		rc = chk_iv_update(ins->ci_iv_ns, &iv, CRT_IV_SHORTCUT_TO_ROOT,
				   CRT_IV_SYNC_EAGER, true);
		if (rc != 0)
			D_ERROR("Check engine (gen "DF_X64") on rank %u failed to notify leader "
				"for its exit, status %u: "DF_RC"\n",
				cbk->cb_gen, dss_self_rank(), ins_status, DP_RC(rc));
	}
}

static uint32_t
chk_engine_find_slowest(struct chk_instance *ins)
{
	uint32_t		 phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;
	uint32_t		 base = ins->ci_bk.cb_phase;
	struct chk_pool_rec	*cpr;

	d_list_for_each_entry(cpr, &ins->ci_pool_list, cpr_link) {
		if (cpr->cpr_phase <= base) {
			phase = cpr->cpr_phase;
			break;
		}

		if (cpr->cpr_phase < phase)
			phase = cpr->cpr_phase;
	}

	return phase;
}

static void
chk_engine_pool_ult(void *args)
{
	struct chk_pool_rec	*cpr = args;
	/*
	 *
	 * TBD: start pool shard and PS if applicable.
	 *
	 * cpr->cpr_svc_started = 1;
	 *
	 * Drive the check since phase CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS.
	 */
	while (cpr->cpr_ins->ci_sched_running) {
		dss_sleep(300);
	}
}

static void
chk_engine_sched(void *args)
{
	struct chk_instance	*ins = args;
	struct chk_bookmark	*cbk = &ins->ci_bk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	uint32_t		 phase;
	uint32_t		 ins_status;
	uint32_t		 pool_status;
	d_rank_t		 myrank = dss_self_rank();
	int			 rc = 0;

	while (ins->ci_sched_running) {
		switch (cbk->cb_phase) {
		case CHK__CHECK_SCAN_PHASE__CSP_PREPARE:
			/*
			 * In this phase, the engine has already offer its known pools' svc list
			 * to the leader via CHK_START RPC reply. Then let's move to next phase.
			 */
			cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST;
			/* XXX: How to estimate the lfet time? */
			cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
			rc = chk_bk_update_engine(cbk);
			if (rc != 0) {
				D_ERROR("Check engine %u (gen "DF_X64") failed to change phase %u: "
					DF_RC"\n", myrank, cbk->cb_gen, cbk->cb_phase, DP_RC(rc));
				/*
				 * Although above updating bookmark in persistent memory failed,
				 * we still need to mark the in-DRAM check instance as 'failed'.
				 * via chk_engine_exit(); otherwise, it may block next CHK_START.
				 */
				goto out;
			}

			break;
		case CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST:
			/*
			 * Wait for the leader to collect all pools svc information, then it will
			 * notify all related engines to step into next phase after consolidating
			 * the pools list.
			 */
			ABT_mutex_lock(ins->ci_abt_mutex);
			if (!ins->ci_sched_running) {
				ABT_mutex_unlock(ins->ci_abt_mutex);
				goto out;
			}

			ABT_cond_wait(ins->ci_abt_cond, ins->ci_abt_mutex);
			ABT_mutex_unlock(ins->ci_abt_mutex);

			break;
		case CHK__CHECK_SCAN_PHASE__CSP_POOL_MBS:
			d_list_for_each_entry_safe(cpr, tmp, &ins->ci_pool_list, cpr_link) {
				D_ASSERT(cpr->cpr_thread == ABT_THREAD_NULL);

				rc = dss_ult_create(chk_engine_pool_ult, cpr, DSS_XS_SYS, 0,
						    DSS_DEEP_STACK_SZ, &cpr->cpr_thread);
				if (rc != 0) {
					rc = dss_abterr2der(rc);
					D_ERROR("Check engine %u (gen "DF_X64") failed when "
						"create pool scan ULT: "DF_RC"\n",
						myrank, cbk->cb_gen, DP_RC(rc));
					goto out;
				}
			}

			/* Fall through. */
		case CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP:
		case CHK__CHECK_SCAN_PHASE__CSP_CONT_LIST:
		case CHK__CHECK_SCAN_PHASE__CSP_CONT_CLEANUP:
			do {
				dss_sleep(300);

				/* Someone wants to stop the check. */
				if (!ins->ci_sched_running)
					D_GOTO(out, rc = 0);

				phase = chk_engine_find_slowest(ins);
				if (phase != cbk->cb_phase) {
					cbk->cb_phase = phase;
					/* XXX: How to estimate the lfet time? */
					cbk->cb_time.ct_left_time =
						CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
					rc = chk_bk_update_engine(cbk);
					if (rc != 0) {
						D_ERROR("Check engine %u (gen "DF_X64") failed "
							"to change phase %u: "DF_RC"\n", myrank,
							cbk->cb_gen, cbk->cb_phase, DP_RC(rc));
						goto out;
					}
				}
			} while (ins->ci_sched_running);

			break;
		case CHK__CHECK_SCAN_PHASE__CSP_DTX_RESYNC:
		case CHK__CHECK_SCAN_PHASE__CSP_OBJ_SCRUB:
		case CHK__CHECK_SCAN_PHASE__CSP_REBUILD:
		case CHK__CHECK_SCAN_PHASE__OSP_AGGREGATION:
			/* XXX: These phases will be implemented in the future. */
			D_ASSERT(0);
			break;
		case CHK__CHECK_SCAN_PHASE__DSP_DONE:
			D_GOTO(out, rc = 1);
		default:
			D_ASSERT(0);
			goto out;
		}
	}

out:
	if (rc > 0) {
		/* If failed to check some pool(s), then the engine will be marked as 'failed'. */
		if (ins->ci_slowest_fail_phase != CHK__CHECK_SCAN_PHASE__CSP_PREPARE)
			ins_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		else
			ins_status = CHK__CHECK_INST_STATUS__CIS_COMPLETED;
		pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKED;
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
		pool_status = CHK__CHECK_POOL_STATUS__CPS_FAILED;
	}

	/* The pool scan ULTs will be terminated via chk_engine_exit(). */
	chk_engine_exit(ins, ins_status, pool_status);

	D_INFO("Check engine %u (gen "DF_X64") exit at the phase %u with "DF_RC"\n",
	       myrank, cbk->cb_gen, cbk->cb_phase, DP_RC(rc));

	/*
	 * The engine scheduler may exit for its own reason (instead of by CHK_STOP),
	 * then reset ci_sched_running to avoid blocking next CHK_START.
	 */
	ins->ci_sched_running = 0;
}

static int
chk_engine_start_prepare(struct chk_instance *ins, uint32_t rank_nr, d_rank_t *ranks,
			 uint32_t policy_nr, struct chk_policy **policies,
			 uint32_t pool_nr, uuid_t pools[], uint64_t gen, int phase,
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

		if (!chk_engine_on_leader(leader))
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

	/* Drop dryrun flags needs to reset. */
	if (prop->cp_flags & CHK__CHECK_FLAG__CF_DRYRUN && !(flags & CHK__CHECK_FLAG__CF_DRYRUN)) {
		if (!reset)
			D_GOTO(out, rc = -DER_NOT_RESUME);

		goto init;
	}

	/*
	 * XXX: If current rank list does not matches the former list, the we need to reset the check
	 *	from scratch. Currently, we do not strictly check that. It is control plane's duty to
	 *	generate valid rank list.
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
		cbk->cb_gen = gen;
		cbk->cb_phase = CHK__CHECK_SCAN_PHASE__CSP_PREPARE;
		memset(&cbk->cb_statistics, 0, sizeof(cbk->cb_statistics));
	}

	if (chk_engine_on_leader(prop->cp_leader)) {
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
	unsigned char			*uuid = key->iov_buf;
	struct chk_bookmark		 cbk;
	int				 rc = 0;

	if (!d_is_uuid(uuid))
		D_GOTO(out, rc = 0);

	rc = chk_bk_fetch_pool(&cbk, uuid);
	if (rc != 0)
		goto out;

	if (cbk.cb_gen >= ctpa->ctpa_gen)
		D_GOTO(out, rc = 0);

	rc = chk_bk_delete_pool(uuid);

out:
	return rc;
}

static int
chk_pool_start_one(struct chk_instance *ins, uuid_t uuid, uint64_t gen)
{
	struct chk_bookmark	cbk;
	int	rc;

	rc = chk_bk_fetch_pool(&cbk, uuid);
	if (rc != 0 && rc == -DER_NONEXIST)
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
	/* XXX: How to estimate the lfet time? */
	cbk.cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk.cb_phase;
	cbk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid, dss_self_rank(),
				cbk.cb_phase, &cbk, ins, NULL, NULL);
	if (rc != 0)
		goto out;

	rc = chk_bk_update_pool(&cbk, uuid);
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
	unsigned char			*uuid = key->iov_buf;
	struct chk_bookmark		 cbk;
	int				 rc = 0;

	if (!d_is_uuid(uuid))
		D_GOTO(out, rc = 0);

	rc = chk_bk_fetch_pool(&cbk, uuid);
	if (rc != 0)
		goto out;

	if (cbk.cb_gen != ctpa->ctpa_gen)
		D_GOTO(out, rc = 0);

	if (cbk.cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_FAILED) {
		if (cbk.cb_phase < ins->ci_slowest_fail_phase)
			ins->ci_slowest_fail_phase = cbk.cb_phase;
	}

	/* Always refresh the start time. */
	cbk.cb_time.ct_start_time = time(NULL);
	/* XXX: How to estimate the lfet time? */
	cbk.cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk.cb_phase;
	cbk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid,
				dss_self_rank(), ins->ci_bk.cb_phase, &cbk, ins, NULL, NULL);
	if (rc != 0)
		goto out;

	rc = chk_bk_update_pool(&cbk, uuid);
	if (rc != 0)
		chk_pool_del_shard(ctpa->ctpa_ins->ci_pool_hdl, uuid, dss_self_rank());

out:
	return rc;
}

static int
chk_engine_clues_filter(uuid_t uuid, void *arg)
{
	struct chk_engine_clues_args	*ceca = arg;
	int				 i;

	if (ceca->ceca_pool_nr == 0)
		return 0;

	for (i = 0; i < ceca->ceca_pool_nr; i++) {
		if (uuid_compare(uuid, ceca->ceca_pools[i]) == 0)
			return 0;
	}

	return 1;
}

int
chk_engine_start(uint64_t gen, uint32_t rank_nr, d_rank_t *ranks,
		 uint32_t policy_nr, struct chk_policy **policies, uint32_t pool_nr,
		 uuid_t pools[], uint32_t flags, int32_t exp_phase, d_rank_t leader,
		 uint32_t *cur_phase, struct ds_pool_clues *clues)
{
	struct chk_bookmark		*cbk = &chk_engine->ci_bk;
	struct chk_property		*prop = &chk_engine->ci_prop;
	d_rank_list_t			*rank_list = NULL;
	struct chk_pool_rec		*cpr;
	struct chk_pool_rec		*tmp;
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_engine_clues_args	 ceca = { 0 };
	d_rank_t			 myrank = dss_self_rank();
	d_iov_t				 kiov;
	int				 rc;
	int				 i;

	if (chk_engine->ci_starting)
		D_GOTO(out_log, rc = -DER_INPROGRESS);

	if (chk_engine->ci_stopping)
		D_GOTO(out_log, rc = -DER_BUSY);

	chk_engine->ci_starting = 1;

	rc = chk_engine_start_prepare(chk_engine, rank_nr, ranks, policy_nr, policies,
				      pool_nr, pools, gen, exp_phase, flags, leader, &rank_list);
	if (rc != 0)
		goto out_log;

	D_ASSERT(rank_list != NULL);
	D_ASSERT(d_list_empty(&chk_engine->ci_pool_list));
	D_ASSERT(d_list_empty(&chk_engine->ci_pending_list));
	D_ASSERT(chk_engine->ci_sched == ABT_THREAD_NULL);

	if (chk_engine->ci_iv_ns != NULL) {
		ds_iv_ns_put(chk_engine->ci_iv_ns);
		chk_engine->ci_iv_ns = NULL;
	}

	if (chk_engine->ci_iv_group != NULL) {
		crt_group_secondary_destroy(chk_engine->ci_iv_group);
		chk_engine->ci_iv_group = NULL;
	}

	rc = crt_group_secondary_create(CHK_DUMMY_POOL, NULL, rank_list, &chk_engine->ci_iv_group);
	if (rc != 0)
		goto out_log;

	rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, (unsigned char *)CHK_DUMMY_POOL,
			     chk_engine->ci_iv_group, &chk_engine->ci_iv_id, &chk_engine->ci_iv_ns);
	if (rc != 0)
		goto out_group;

	ds_iv_ns_update(chk_engine->ci_iv_ns, leader);

	if (prop->cp_pool_nr <= 0)
		chk_engine->ci_all_pools = 1;
	else
		chk_engine->ci_all_pools = 0;

	if (flags & CHK__CHECK_FLAG__CF_RESET) {
		ctpa.ctpa_gen = cbk->cb_gen;
		rc = chk_traverse_pools(chk_pools_cleanup_cb, &ctpa);
		if (rc != 0)
			goto out_iv;

		ctpa.ctpa_gen = cbk->cb_gen;
		ctpa.ctpa_ins = chk_engine;

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
		if (chk_engine->ci_all_pools) {
			ctpa.ctpa_gen = cbk->cb_gen;
			ctpa.ctpa_ins = chk_engine;
			rc = chk_traverse_pools(chk_pools_add_from_db, &ctpa);
			if (rc != 0)
				goto out_pool;
		} else {
			for (i = 0; i < pool_nr; i++) {
				rc = chk_pool_start_one(chk_engine, pools[i], cbk->cb_gen);
				if (rc != 0)
					goto out_pool;
			}
		}

		*cur_phase = chk_engine_find_slowest(chk_engine);
	}

	cbk->cb_ins_status = CHK__CHECK_INST_STATUS__CIS_RUNNING;
	cbk->cb_phase = *cur_phase;
	/* Always refresh the start time. */
	cbk->cb_time.ct_start_time = time(NULL);
	/* XXX: How to estimate the lfet time? */
	cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	rc = chk_bk_update_engine(cbk);
	if (rc != 0)
		goto out_pool;

	if (cbk->cb_phase == CHK__CHECK_SCAN_PHASE__CSP_PREPARE ||
	    cbk->cb_phase == CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST) {
		ceca.ceca_pool_nr = pool_nr;
		ceca.ceca_pools = pools;
		rc = ds_pool_clues_init(chk_engine_clues_filter, &ceca, clues);
		if (rc != 0)
			goto out_bk;
	}

	chk_engine->ci_sched_running = 1;

	rc = dss_ult_create(chk_engine_sched, chk_engine, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &chk_engine->ci_sched);
	if (rc != 0) {
		chk_engine->ci_sched_running = 0;
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
	d_list_for_each_entry_safe(cpr, tmp, &chk_engine->ci_pool_list, cpr_link) {
		cpr->cpr_bk.cb_time.ct_stop_time = time(NULL);
		cpr->cpr_bk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_IMPLICATED;
		chk_bk_update_pool(&cpr->cpr_bk, cpr->cpr_uuid);

		d_iov_set(&kiov, cpr->cpr_uuid, sizeof(cpr->cpr_uuid));
		dbtree_delete(chk_engine->ci_pool_hdl, BTR_PROBE_EQ, &kiov, NULL);
	}
out_iv:
	ds_iv_ns_put(chk_engine->ci_iv_ns);
	chk_engine->ci_iv_ns = NULL;
out_group:
	crt_group_secondary_destroy(chk_engine->ci_iv_group);
	chk_engine->ci_iv_group = NULL;
out_log:
	chk_engine->ci_starting = 0;

	if (rc == 0) {
		D_INFO("Check engine (gen "DF_X64") started on rank %u with %u ranks, %u pools, "
		       "flags %x, phase %d, leader %u\n",
		       cbk->cb_gen, myrank, rank_nr, pool_nr, flags, exp_phase, leader);

		chk_ranks_dump(rank_list->rl_nr, rank_list->rl_ranks);

		if (pool_nr > 0)
			chk_pools_dump(pool_nr, pools);
		else if (prop->cp_pool_nr > 0)
			chk_pools_dump(prop->cp_pool_nr, prop->cp_pools);
	} else if (rc > 0) {
		*cur_phase = CHK__CHECK_SCAN_PHASE__DSP_DONE;
	} else if (rc != -DER_ALREADY) {
		D_ERROR("Check engine (gen "DF_X64") failed to start on rank %u with %u ranks, "
			"%u pools, flags %x, phase %d, leader %u, gen "DF_X64": "DF_RC"\n",
			cbk->cb_gen, myrank, rank_nr, pool_nr,
			flags, exp_phase, leader, gen, DP_RC(rc));
	}

	d_rank_list_free(rank_list);

	return rc;
}

static int
chk_pool_stop_one(daos_handle_t hdl, struct chk_bookmark *cbk, uuid_t uuid, uint32_t flags)
{
	d_iov_t		kiov;
	int		rc = 0;

	if (cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_CHECKING ||
	    cbk->cb_pool_status == CHK__CHECK_POOL_STATUS__CPS_PENDING) {
		cbk->cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_STOPPED;
		cbk->cb_time.ct_stop_time = time(NULL);
		rc = chk_bk_update_pool(cbk, uuid);
		if (rc != 0)
			goto out;
	}

	d_iov_set(&kiov, uuid, sizeof(uuid_t));
	rc = dbtree_delete(hdl, flags | BTR_PROBE_EQ, &kiov, NULL);
	if (rc != 0)
		D_ERROR("Check engine (gen "DF_X64") on rank %u failed to delete pool record "
			DF_UUID" when exit: "DF_RC"\n",
			cbk->cb_gen, dss_self_rank(), DP_UUID(uuid), DP_RC(rc));

out:
	return rc;
}

int
chk_engine_stop(uint64_t gen, uint32_t pool_nr, uuid_t pools[])
{
	struct chk_property	*prop = &chk_engine->ci_prop;
	struct chk_bookmark	*cbk = &chk_engine->ci_bk;
	struct chk_pool_rec	*cpr;
	struct chk_pool_rec	*tmp;
	d_iov_t			 riov;
	d_iov_t			 kiov;
	uuid_t			 uuid;
	int			 rc = 0;
	int			 i;

	if (cbk->cb_magic != CHK_BK_MAGIC_ENGINE || cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	if (chk_engine->ci_starting)
		D_GOTO(out, rc = -DER_BUSY);

	if (chk_engine->ci_stopping)
		D_GOTO(out, rc = -DER_INPROGRESS);

	chk_engine->ci_stopping = 1;

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = -DER_ALREADY);

	if (pool_nr == 0) {
		d_list_for_each_entry_safe(cpr, tmp, &chk_engine->ci_pool_list, cpr_link) {
			uuid_copy(uuid, cpr->cpr_uuid);
			rc = chk_pool_stop_one(chk_engine->ci_pool_hdl, &cpr->cpr_bk, uuid, 0);
			if (rc != 0)
				goto out;
		}
	} else {
		for (i = 0; i < pool_nr; i++) {
			d_iov_set(&riov, NULL, 0);
			d_iov_set(&kiov, pools[i], sizeof(pools[i]));
			rc = dbtree_lookup(chk_engine->ci_pool_hdl, &kiov, &riov);
			if (rc == -DER_NONEXIST)
				continue;

			if (rc != 0)
				goto out;

			cpr = (struct chk_pool_rec *)riov.iov_buf;
			rc = chk_pool_stop_one(chk_engine->ci_pool_hdl, &cpr->cpr_bk, pools[i],
					       BTR_PROBE_BYPASS);
			if (rc != 0)
				goto out;
		}
	}

	if (d_list_empty(&chk_engine->ci_pool_list)) {
		chk_stop_sched(chk_engine);
		/* To indicate that there is no active pool(s) on this rank. */
		rc = 1;
	}

out:
	chk_engine->ci_stopping = 0;

	if (rc == 0) {
		D_INFO("Check engine (gen "DF_X64") stopped on rank %u with %u pools\n",
		       cbk->cb_gen, dss_self_rank(), pool_nr > 0 ? pool_nr : prop->cp_pool_nr);

		if (pool_nr > 0)
			chk_pools_dump(pool_nr, pools);
		else if (prop->cp_pool_nr > 0)
			chk_pools_dump(prop->cp_pool_nr, prop->cp_pools);
	} else if (rc < 0 && rc == -DER_ALREADY) {
		D_ERROR("Check engine (gen "DF_X64") failed to stop on rank %u with %u pools, "
			"gen "DF_X64": "DF_RC"\n", cbk->cb_gen, dss_self_rank(),
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
	rc = path_gen(cqxa->cqxa_uuid, dss_storage_path, VOS_FILE, &tid, &path);
	if (rc != 0)
		goto out;

	rc = vos_pool_open(path, cqxa->cqxa_uuid, 0, &poh);
	if (rc != 0) {
		D_ERROR("Failed to open vos pool "DF_UUID" on target %u/%d: "DF_RC"\n",
			cqxa->cqxa_uuid, dss_self_rank(), tid, DP_RC(rc));
		goto out;
	}

	rc = vos_pool_query(poh, &info);
	if (rc != 0) {
		D_ERROR("Failed to query vos pool "DF_UUID" on target %u/%d: "DF_RC"\n",
			cqxa->cqxa_uuid, dss_self_rank(), tid, DP_RC(rc));
		goto out;
	}

	target->cqt_rank = dss_self_rank();
	target->cqt_tgt = tid;
	target->cqt_ins_status = info.pif_chk_status;
	target->cqt_statistics = info.pif_chk_statistics;
	target->cqt_time = info.pif_chk_time;

out:
	if (daos_handle_is_valid(poh))
		vos_pool_close(poh);
	D_FREE(path);
	return rc;
}

static void
chk_engine_query_reduce(void *a_args, void *s_args)
{
	struct	chk_query_xstream_args	*aggregator = a_args;
	struct  chk_query_xstream_args	*stream = s_args;
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
	D_ASSERT(args->st_arg != NULL);
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

	rc = chk_bk_fetch_pool(&cbk, uuid);
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
		 "Check engine (gen "DF_X64") on rank %u query pool "DF_UUID":"DF_RC"\n",
		 chk_engine->ci_bk.cb_gen, dss_self_rank(), DP_UUID(uuid), DP_RC(rc));
	return rc;
}

int
chk_engine_query(uint64_t gen, uint32_t pool_nr, uuid_t pools[],
		 uint32_t *shard_nr, struct chk_query_pool_shard **shards)
{
	struct chk_bookmark		*cbk = &chk_engine->ci_bk;
	struct chk_query_pool_args	 cqpa = { 0 };
	int				 rc = 0;
	int				 i;

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

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
		 "Check engine (gen "DF_X64") on rank %u handle query for %u pools :"DF_RC"\n",
		 cbk->cb_gen, dss_self_rank(), pool_nr, DP_RC(rc));

out:
	return rc;
}

int
chk_engine_mark_rank_dead(uint64_t gen, d_rank_t rank, uint32_t version)
{
	struct chk_property	*prop = &chk_engine->ci_prop;
	struct chk_bookmark	*cbk = &chk_engine->ci_bk;
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

	rc = crt_group_secondary_modify(chk_engine->ci_iv_group, rank_list, rank_list,
					CRT_GROUP_MOD_OP_REPLACE, version);

	/* TBD: mark related pools as 'failed'. */

out:
	d_rank_list_free(rank_list);
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 "Check engine (gen "DF_X64") on rank %u mark rank %u as dead with gen "
			 DF_X64", version %u: "DF_RC"\n",
			 cbk->cb_gen, dss_self_rank(), rank, gen, version, DP_RC(rc));

	return rc;
}

int
chk_engine_act(uint64_t gen, uint64_t seq, uint32_t cla, uint32_t act, uint32_t flags)
{
	struct chk_property	*prop = &chk_engine->ci_prop;
	struct chk_bookmark	*cbk = &chk_engine->ci_bk;
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

	rc = chk_pending_del(chk_engine, seq, &cpr);
	if (rc == 0) {
		/* The cpr will be destroyed by the waiter via chk_engine_report(). */
		D_ASSERT(cpr->cpr_busy == 1);

		ABT_mutex_lock(cpr->cpr_mutex);
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
		 "Check engine (gen "DF_X64") on rank %u takes action for seq "
		 DF_X64" with gen "DF_X64", class %u, action %u, flags %x: "DF_RC"\n",
		 cbk->cb_gen, dss_self_rank(), seq, gen, cla, act, flags, DP_RC(rc));

	return rc;
}

int
chk_engine_report(uint32_t cla, uint32_t act, int32_t result, char *pool, char *cont,
		  daos_unit_oid_t *obj, daos_key_t *dkey, daos_key_t *akey, char *msg,
		  uint32_t option_nr, uint32_t *options, uint32_t detail_nr, d_sg_list_t *details)
{
	struct chk_bookmark	*cbk = &chk_engine->ci_bk;
	struct chk_pending_rec	*cpr = NULL;
	uint64_t		 seq = 0;
	int			 rc;

	rc = chk_report_remote(chk_engine->ci_prop.cp_leader, chk_engine->ci_bk.cb_gen, cla,
			       act, result, dss_self_rank(), dss_get_module_info()->dmi_tgt_id,
			       pool, cont, obj, dkey, akey, msg, option_nr, options, detail_nr,
			       details, &seq);
	if (rc != 0)
		goto log;

	if (act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT)
		rc = chk_pending_add(chk_engine, &chk_engine->ci_pending_list, seq,
				     dss_self_rank(), cla, &cpr);

log:
	if (rc != 0) {
		D_ERROR("Check engine (gen "DF_X64") on rank %u failed to report with class %u, "
			"action %u, result %d: "DF_RC"\n",
			cbk->cb_gen, dss_self_rank(), cla, act, result, DP_RC(rc));
		goto out;
	}

	D_CDEBUG(act == CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT, DLOG_INFO, DLOG_DBG,
		 "Check engine (gen "DF_X64") on rank %u handle report with class %u, "
		 "action %u, result %d: "DF_RC"\n",
		 cbk->cb_gen, dss_self_rank(), cla, act, result, DP_RC(rc));

	if (cpr == NULL)
		goto out;

	D_ASSERT(cpr->cpr_busy == 1);

	ABT_mutex_lock(cpr->cpr_mutex);
	if (cpr->cpr_action != CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT) {
		ABT_mutex_unlock(cpr->cpr_mutex);
		goto handle;
	}

	ABT_cond_wait(cpr->cpr_cond, cpr->cpr_mutex);
	ABT_mutex_unlock(cpr->cpr_mutex);
	if (!chk_engine->ci_sched_running || cpr->cpr_exiting)
		goto out;

	D_ASSERT(cpr->cpr_action != CHK__CHECK_INCONSIST_ACTION__CIA_INTERACT);

handle:

	/* TBD: already got the interaction feedback, handle it and log the result. */

out:
	if (cpr != NULL)
		chk_pending_destroy(cpr);

	return rc;
}

int
chk_engine_notify(uint64_t gen, d_rank_t rank, uint32_t phase, uint32_t status)
{
	struct chk_property	*prop = &chk_engine->ci_prop;
	struct chk_bookmark	*cbk = &chk_engine->ci_bk;
	int			 rc = 0;

	if (cbk->cb_gen != gen)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	/* Ignore notification from non-leader. */
	if (prop->cp_leader != rank)
		D_GOTO(out, rc = -DER_NOTAPPLICABLE);

	/* Ignore non-failure notification from leader. */
	if (status != CHK__CHECK_INST_STATUS__CIS_FAILED)
		D_GOTO(out, rc = 0);

	if (cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_RUNNING)
		D_GOTO(out, rc = 0);

	chk_engine->ci_implicated = 1;
	chk_stop_sched(chk_engine);

out:
	if (rc != -DER_NOTAPPLICABLE)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 "Check engine (gen "DF_X64") on rank %u got notification from rank %u, "
			 "phase %u, status %u, gen "DF_X64": "DF_RC"\n",
			 cbk->cb_gen, dss_self_rank(), rank, phase, status, gen, DP_RC(rc));

	return (rc == 0 || rc == -DER_NOTAPPLICABLE) ? 0 : rc;
}

static int
chk_rejoin_cb(struct sys_db *db, char *table, d_iov_t *key, void *args)
{
	struct chk_traverse_pools_args	*ctpa = args;
	struct chk_instance		*ins = ctpa->ctpa_ins;
	unsigned char			*uuid = key->iov_buf;
	struct chk_bookmark		 cbk;
	int				 rc;

	if (!d_is_uuid(uuid))
		goto out;

	rc = chk_bk_fetch_pool(&cbk, uuid);
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

	/* Always refresh the start time. */
	cbk.cb_time.ct_start_time = time(NULL);
	/* XXX: How to estimate the lfet time? */
	cbk.cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk.cb_phase;
	cbk.cb_pool_status = CHK__CHECK_POOL_STATUS__CPS_CHECKING;
	rc = chk_pool_add_shard(ins->ci_pool_hdl, &ins->ci_pool_list, uuid,
				dss_self_rank(), cbk.cb_phase, &cbk, ins, NULL, NULL);
	if (rc != 0) {
		ctpa->ctpa_status = CHK__CHECK_INST_STATUS__CIS_FAILED;
		goto out;
	}

	rc = chk_bk_update_pool(&cbk, uuid);
	if (rc != 0)
		chk_pool_del_shard(ins->ci_pool_hdl, uuid, dss_self_rank());

out:
	/* Ignore the failure to handle next one. */
	return 0;
}

void
chk_engine_rejoin(void)
{
	struct chk_property		*prop = &chk_engine->ci_prop;
	struct chk_bookmark		*cbk = &chk_engine->ci_bk;
	d_rank_list_t			*rank_list = NULL;
	struct chk_traverse_pools_args	 ctpa = { 0 };
	struct chk_iv			 iv;
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

	D_ASSERT(chk_engine->ci_starting == 0);
	D_ASSERT(chk_engine->ci_stopping == 0);
	D_ASSERT(chk_engine->ci_iv_group == NULL);
	D_ASSERT(chk_engine->ci_iv_ns == NULL);

	need_join = true;
	chk_engine->ci_starting = 1;

	rc = chk_prop_fetch(prop, &rank_list);
	if (rc != 0)
		goto out;

	D_ASSERT(rank_list != NULL);

	rc = crt_group_secondary_create(CHK_DUMMY_POOL, NULL, rank_list, &chk_engine->ci_iv_group);
	if (rc != 0)
		goto out;

	rc = ds_iv_ns_create(dss_get_module_info()->dmi_ctx, (unsigned char *)CHK_DUMMY_POOL,
			     chk_engine->ci_iv_group, &chk_engine->ci_iv_id, &chk_engine->ci_iv_ns);
	if (rc != 0)
		goto out;

	ds_iv_ns_update(chk_engine->ci_iv_ns, prop->cp_leader);

	/* Ask leader whether this engine can rejoin or not. */
	rc = chk_rejoin_remote(prop->cp_leader, cbk->cb_gen, myrank, cbk->cb_phase);
	if (rc != 0)
		goto out;

	joined = true;

	ctpa.ctpa_gen = cbk->cb_gen;
	ctpa.ctpa_ins = chk_engine;
	rc = chk_traverse_pools(chk_rejoin_cb, &ctpa);
	if (rc != 0)
		goto out;

	phase = chk_engine_find_slowest(chk_engine);
	if (phase != cbk->cb_phase)
		need_iv = true;

	cbk->cb_phase = phase;
	if (unlikely(d_list_empty(&chk_engine->ci_pool_list))) {
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
		/* XXX: How to estimate the lfet time? */
		cbk->cb_time.ct_left_time = CHK__CHECK_SCAN_PHASE__DSP_DONE - cbk->cb_phase;
	}

	rc = chk_bk_update_engine(cbk);
	if (rc != 0) {
		need_iv = true;
		goto out;
	}

	if (unlikely(d_list_empty(&chk_engine->ci_pool_list)))
		goto out;

	chk_engine->ci_sched_running = 1;

	rc = dss_ult_create(chk_engine_sched, chk_engine, DSS_XS_SYS, 0, DSS_DEEP_STACK_SZ,
			    &chk_engine->ci_sched);
	if (rc != 0)
		need_iv = true;
	else
		/* chk_engine_sched will do IV to leader. */
		need_iv = false;

out:
	chk_engine->ci_starting = 0;
	d_rank_list_free(rank_list);

	if (rc != 0 && joined) {
		chk_engine_exit(chk_engine, CHK__CHECK_INST_STATUS__CIS_FAILED,
				CHK__CHECK_POOL_STATUS__CPS_IMPLICATED);
	} else if (need_iv && cbk->cb_ins_status != CHK__CHECK_INST_STATUS__CIS_IMPLICATED &&
		   chk_engine->ci_iv_ns != NULL) {
		iv.ci_gen = cbk->cb_gen;
		iv.ci_phase = cbk->cb_phase;
		iv.ci_status = cbk->cb_ins_status;
		iv.ci_to_leader = 1;

		/* Synchronously notify the leader that check instance exit on the engine. */
		rc = chk_iv_update(chk_engine->ci_iv_ns, &iv, CRT_IV_SHORTCUT_TO_ROOT,
				   CRT_IV_SYNC_EAGER, true);
		if (rc != 0)
			D_ERROR("Check engine (gen "DF_X64") on rank %u failed to notify leader "
				"for its changes, status %u: "DF_RC"\n",
				cbk->cb_gen, myrank, cbk->cb_ins_status, DP_RC(rc));
	}

	/*
	 * XXX: It is unnecessary to destroy the IV namespace that can be handled when next
	 *	CHK_START or instance fini.
	 */

	if (need_join)
		D_CDEBUG(rc != 0, DLOG_ERR, DLOG_INFO,
			 "Check engine (gen "DF_X64") rejoin on rank %u: "DF_RC"\n",
			 cbk->cb_gen, myrank, DP_RC(rc));
}

void
chk_engine_pause(void)
{
	chk_stop_sched(chk_engine);
	D_ASSERT(d_list_empty(&chk_engine->ci_pending_list));
	D_ASSERT(d_list_empty(&chk_engine->ci_pool_list));
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
		goto free;

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

	if (unlikely(cbk->cb_magic != CHK_BK_MAGIC_ENGINE)) {
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
free:
	D_FREE(chk_engine);
out:
	return rc;
}

void
chk_engine_fini(void)
{
	chk_ins_fini(chk_engine);
}
