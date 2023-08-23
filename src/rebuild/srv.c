/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rebuild: rebuild service
 *
 * Rebuild service module api.
 */
#define D_LOGFAC	DD_FAC(rebuild)

#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/iv.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/security.h>
#include <daos_mgmt.h>
#include "rpc.h"
#include "rebuild_internal.h"

#define RBLD_CHECK_INTV	 2000	/* milliseconds interval to check*/
struct rebuild_global	rebuild_gst;

struct pool_map *
rebuild_pool_map_get(struct ds_pool *pool)
{
	struct pool_map *map = NULL;

	D_ASSERT(pool);
	D_ASSERT(pool->sp_map != NULL);
	ABT_rwlock_rdlock(pool->sp_lock);
	map = pool->sp_map;
	pool_map_addref(map);
	ABT_rwlock_unlock(pool->sp_lock);
	return map;
}

void
rebuild_pool_map_put(struct pool_map *map)
{
	pool_map_decref(map);
}

struct rebuild_pool_tls *
rebuild_pool_tls_lookup(uuid_t pool_uuid, unsigned int ver, uint32_t gen)
{
	struct rebuild_tls *tls = rebuild_tls_get();
	struct rebuild_pool_tls *pool_tls;
	struct rebuild_pool_tls *found = NULL;

	D_ASSERT(tls != NULL);
	/* Only 1 thread will access the list, no need lock */
	d_list_for_each_entry(pool_tls, &tls->rebuild_pool_list,
			      rebuild_pool_list) {
		if (uuid_compare(pool_tls->rebuild_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) || ver == pool_tls->rebuild_pool_ver) &&
		    (gen == (uint32_t)(-1) || gen == pool_tls->rebuild_pool_gen)) {
			found = pool_tls;
			break;
		}
	}

	return found;
}

static struct rebuild_pool_tls *
rebuild_pool_tls_create(uuid_t pool_uuid, uuid_t poh_uuid, uuid_t coh_uuid,
			unsigned int ver, uint32_t gen)
{
	struct rebuild_pool_tls *rebuild_pool_tls;
	struct rebuild_tls *tls = rebuild_tls_get();

	rebuild_pool_tls = rebuild_pool_tls_lookup(pool_uuid, ver, gen);
	D_ASSERT(rebuild_pool_tls == NULL);

	D_ALLOC_PTR(rebuild_pool_tls);
	if (rebuild_pool_tls == NULL)
		return NULL;

	rebuild_pool_tls->rebuild_pool_ver = ver;
	rebuild_pool_tls->rebuild_pool_gen = gen;
	uuid_copy(rebuild_pool_tls->rebuild_pool_uuid, pool_uuid);
	rebuild_pool_tls->rebuild_pool_scanning = 1;
	rebuild_pool_tls->rebuild_pool_scan_done = 0;
	rebuild_pool_tls->rebuild_pool_obj_count = 0;
	rebuild_pool_tls->rebuild_pool_reclaim_obj_count = 0;
	rebuild_pool_tls->rebuild_tree_hdl = DAOS_HDL_INVAL;
	/* Only 1 thread will access the list, no need lock */
	d_list_add(&rebuild_pool_tls->rebuild_pool_list,
		   &tls->rebuild_pool_list);

	D_DEBUG(DB_REBUILD, "TLS create for "DF_UUID" ver %d\n",
		DP_UUID(pool_uuid), ver);
	return rebuild_pool_tls;
}

static void
rebuild_pool_tls_destroy(struct rebuild_pool_tls *tls)
{
	D_DEBUG(DB_REBUILD, "TLS destroy for "DF_UUID" ver %d\n",
		DP_UUID(tls->rebuild_pool_uuid), tls->rebuild_pool_ver);
	if (daos_handle_is_valid(tls->rebuild_tree_hdl))
		rebuild_obj_tree_destroy(tls->rebuild_tree_hdl);
	d_list_del(&tls->rebuild_pool_list);
	D_FREE(tls);
}

static void *
rebuild_tls_init(int tags, int xs_id, int tgt_id)
{
	struct rebuild_tls *tls;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&tls->rebuild_pool_list);
	return tls;
}

static bool
is_rebuild_global_pull_done(struct rebuild_global_pool_tracker *rgt)
{
	int i;

	D_ASSERT(rgt->rgt_servers_number > 0);
	D_ASSERT(rgt->rgt_servers != NULL);

	for (i = 0; i < rgt->rgt_servers_number; i++)
		if (!rgt->rgt_servers[i].pull_done)
			return false;
	return true;
}

static bool
is_rebuild_global_scan_done(struct rebuild_global_pool_tracker *rgt)
{
	int i;

	D_ASSERT(rgt->rgt_servers_number > 0);
	D_ASSERT(rgt->rgt_servers != NULL);

	for (i = 0; i < rgt->rgt_servers_number; i++)
		if (!rgt->rgt_servers[i].scan_done)
			return false;
	return true;
}

static bool
is_rebuild_global_done(struct rebuild_global_pool_tracker *rgt)
{
	return is_rebuild_global_scan_done(rgt) &&
	       is_rebuild_global_pull_done(rgt);

}

#define SCAN_DONE	0x1
#define PULL_DONE	0x2
static void
rebuild_leader_set_status(struct rebuild_global_pool_tracker *rgt,
			  d_rank_t rank, uint32_t resync_ver, unsigned flags)
{
	struct rebuild_server_status	*status = NULL;
	int				i;

	D_ASSERT(rgt->rgt_servers_number > 0);
	D_ASSERT(rgt->rgt_servers != NULL);
	for (i = 0; i < rgt->rgt_servers_number; i++) {
		if (rgt->rgt_servers[i].rank == rank) {
			status = &rgt->rgt_servers[i];
			break;
		}
	}

	if (status == NULL) {
		D_INFO("rank %u is not included in this rebuild.\n", rank);
		return;
	}

	status->dtx_resync_version = resync_ver;
	if (flags & SCAN_DONE)
		status->scan_done = 1;
	if (flags & PULL_DONE)
		status->pull_done = 1;
}

static uint32_t
rebuild_get_global_dtx_resync_ver(struct rebuild_global_pool_tracker *rgt)
{
	uint32_t	min = -1;
	int		i;

	D_ASSERT(rgt->rgt_servers_number > 0);
	D_ASSERT(rgt->rgt_servers != NULL);
	for (i = 0; i < rgt->rgt_servers_number; i++) {
		if (rgt->rgt_servers[i].dtx_resync_version == (uint32_t)(-1))
			continue;

		if (min > rgt->rgt_servers[i].dtx_resync_version)
			min = rgt->rgt_servers[i].dtx_resync_version;
	}

	return min;
}

struct rebuild_tgt_pool_tracker *
rpt_lookup(uuid_t pool_uuid, unsigned int ver, unsigned int gen)
{
	struct rebuild_tgt_pool_tracker	*rpt;
	struct rebuild_tgt_pool_tracker	*found = NULL;

	/* Only stream 0 will access the list */
	d_list_for_each_entry(rpt, &rebuild_gst.rg_tgt_tracker_list, rt_list) {
		if (uuid_compare(rpt->rt_pool_uuid, pool_uuid) == 0 &&
		    rpt->rt_finishing == 0 &&
		    (ver == (unsigned int)(-1) || rpt->rt_rebuild_ver == ver) &&
		    (gen == (unsigned int)(-1) || rpt->rt_rebuild_gen == gen)) {
			rpt_get(rpt);
			found = rpt;
			break;
		}
	}

	return found;
}

int
rebuild_global_status_update(struct rebuild_global_pool_tracker *rgt,
			     struct rebuild_iv *iv)
{
	D_DEBUG(DB_REBUILD, "iv rank %d scan_done %d pull_done %d resync dtx %u\n",
		iv->riv_rank, iv->riv_scan_done, iv->riv_pull_done,
		iv->riv_dtx_resyc_version);

	if (!iv->riv_scan_done) {
		rebuild_leader_set_status(rgt, iv->riv_rank, iv->riv_dtx_resyc_version, 0);
		return 0;
	}

	if (!is_rebuild_global_scan_done(rgt)) {
		rebuild_leader_set_status(rgt, iv->riv_rank, iv->riv_dtx_resyc_version,
					  SCAN_DONE);
		D_DEBUG(DB_REBUILD, "rebuild ver %d tgt %d scan done\n",
			rgt->rgt_rebuild_ver, iv->riv_rank);
		/* If global scan is not done, then you can not trust
		 * pull status. But if the rebuild on that target is
		 * failed(riv_status != 0), then the target will report
		 * both scan and pull status to the leader, i.e. they
		 * both can be trusted.
		 */
		if (iv->riv_status == 0)
			return 0;
	}

	/* Only trust pull done if scan is done globally */
	if (iv->riv_pull_done) {
		rebuild_leader_set_status(rgt, iv->riv_rank, iv->riv_dtx_resyc_version,
					  PULL_DONE);
		D_DEBUG(DB_REBUILD, "rebuild ver %d tgt %d pull done\n",
			rgt->rgt_rebuild_ver, iv->riv_rank);
	}

	return 0;
}

static struct daos_rebuild_status *
rebuild_status_completed_lookup(const uuid_t pool_uuid)
{
	struct rebuild_status_completed	*rsc;
	struct daos_rebuild_status	*rs = NULL;

	d_list_for_each_entry(rsc, &rebuild_gst.rg_completed_list, rsc_list) {
		if (uuid_compare(rsc->rsc_pool_uuid, pool_uuid) == 0) {
			rs = &rsc->rsc_status;
			break;
		}
	}

	return rs;
}

static int
rebuild_status_completed_update(const uuid_t pool_uuid,
				struct daos_rebuild_status *rs)
{
	struct rebuild_status_completed	*rsc;
	struct daos_rebuild_status	*rs_inlist;

	rs_inlist = rebuild_status_completed_lookup(pool_uuid);
	if (rs_inlist != NULL) {
		/* ignore the older version as IV update/refresh in async */
		if (rs->rs_version >= rs_inlist->rs_version)
			memcpy(rs_inlist, rs, sizeof(*rs));
		return 0;
	}

	D_ALLOC_PTR(rsc);
	if (rsc == NULL)
		return -DER_NOMEM;

	uuid_copy(rsc->rsc_pool_uuid, pool_uuid);
	memcpy(&rsc->rsc_status, rs, sizeof(*rs));
	d_list_add(&rsc->rsc_list, &rebuild_gst.rg_completed_list);
	return 0;
}

static void
rebuild_status_completed_remove(const uuid_t pool_uuid)
{
	struct rebuild_status_completed	*rsc;
	struct rebuild_status_completed	*next;

	d_list_for_each_entry_safe(rsc, next, &rebuild_gst.rg_completed_list,
				   rsc_list) {
		if (pool_uuid == NULL ||
		    uuid_compare(rsc->rsc_pool_uuid, pool_uuid) == 0) {
			d_list_del(&rsc->rsc_list);
			D_FREE(rsc);
		}
	}
}

static void
rebuild_tls_fini(int tags, void *data)
{
	struct rebuild_tls *tls = data;
	struct rebuild_pool_tls *pool_tls;
	struct rebuild_pool_tls *tmp;

	d_list_for_each_entry_safe(pool_tls, tmp, &tls->rebuild_pool_list,
				   rebuild_pool_list)
		rebuild_pool_tls_destroy(pool_tls);

	D_FREE(tls);
}

struct rebuild_tgt_query_arg {
	struct rebuild_tgt_pool_tracker *rpt;
	struct rebuild_tgt_query_info *status;
};

bool
rebuild_status_match(struct rebuild_tgt_pool_tracker *rpt,
		     enum pool_comp_state states)
{
	struct pool_target	*tgt;
	unsigned int		idx = dss_get_module_info()->dmi_tgt_id;
	d_rank_t		rank;
	int			rc;

	D_ASSERT(rpt != NULL);
	D_ASSERT(rpt->rt_pool != NULL);
	D_ASSERT(rpt->rt_pool->sp_map != NULL);

	/* Let's use NULL for now, because subgroup == master group for
	 * all of test anyway. Once we resolve the race between cart
	 * group destroy and rebuild, it should use cart group inside
	 * ds_pool. (DAOS-1943) FIXME
	 */
	crt_group_rank(NULL, &rank);
	rc = pool_map_find_target_by_rank_idx(rpt->rt_pool->sp_map, rank,
					      idx, &tgt);
	D_ASSERT(rc == 1);
	if ((tgt->ta_comp.co_status & states) != 0) {
		D_DEBUG(DB_REBUILD, "%d/%d target status %d\n",
			rank, idx, tgt->ta_comp.co_status);
		return true;
	}

	return false;
}

bool
is_current_tgt_unavail(struct rebuild_tgt_pool_tracker *rpt)
{
	return rebuild_status_match(rpt, PO_COMP_ST_DOWNOUT | PO_COMP_ST_DOWN);
}

static int
dss_rebuild_check_one(void *data)
{
	struct rebuild_tgt_query_arg	*arg = data;
	struct rebuild_pool_tls		*pool_tls;
	struct rebuild_tgt_query_info	*status = arg->status;
	struct rebuild_tgt_pool_tracker	*rpt = arg->rpt;
	unsigned int			idx = dss_get_module_info()->dmi_tgt_id;

	if (is_current_tgt_unavail(rpt))
		return 0;

	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
					   rpt->rt_rebuild_gen);
	D_ASSERTF(pool_tls != NULL, DF_UUID" ver %d\n",
		   DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver);

	D_DEBUG(DB_REBUILD, "%d scanning %d status: "DF_RC"\n", idx,
		pool_tls->rebuild_pool_scanning,
		DP_RC(pool_tls->rebuild_pool_status));

	ABT_mutex_lock(status->lock);
	if (pool_tls->rebuild_pool_scanning)
		status->scanning = 1;
	if (pool_tls->rebuild_pool_status != 0 && status->status == 0)
		status->status = pool_tls->rebuild_pool_status;

	status->obj_count += pool_tls->rebuild_pool_reclaim_obj_count;
	status->tobe_obj_count += pool_tls->rebuild_pool_obj_count;
	ABT_mutex_unlock(status->lock);

	return 0;
}

static int
rebuild_tgt_query(struct rebuild_tgt_pool_tracker *rpt,
		  struct rebuild_tgt_query_info *status)
{
	struct ds_migrate_status	dms = { 0 };
	struct rebuild_pool_tls		*tls;
	struct rebuild_tgt_query_arg	arg;
	int				rc;

	arg.rpt = rpt;
	arg.status = status;

	if (rpt->rt_rebuild_op != RB_OP_RECLAIM && rpt->rt_rebuild_op != RB_OP_FAIL_RECLAIM) {
		rc = ds_migrate_query_status(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
					     rpt->rt_rebuild_gen, &dms);
		if (rc)
			D_GOTO(out, rc);
	}

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
				      rpt->rt_rebuild_gen);
	if (tls != NULL && tls->rebuild_pool_status)
		status->status = tls->rebuild_pool_status;

	/* let's check scanning status on every thread*/
	ABT_mutex_lock(rpt->rt_lock);
	rc = dss_thread_collective(dss_rebuild_check_one, &arg, 0);
	if (rc) {
		ABT_mutex_unlock(rpt->rt_lock);
		D_GOTO(out, rc);
	}

	status->obj_count += dms.dm_obj_count;
	status->rec_count = dms.dm_rec_count;
	status->size = dms.dm_total_size;
	if (status->scanning || dms.dm_migrating)
		status->rebuilding = true;
	else
		status->rebuilding = false;

	if (status->status == 0 && dms.dm_status)
		status->status = dms.dm_status;

	ABT_mutex_unlock(rpt->rt_lock);

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" scanning %d/%d rebuilding=%s, "
		"obj_count="DF_U64", tobe_obj="DF_U64" rec_count="DF_U64
		" size= "DF_U64"\n",
		DP_UUID(rpt->rt_pool_uuid), status->scanning,
		status->status, status->rebuilding ? "yes" : "no",
		status->obj_count, status->tobe_obj_count, status->rec_count,
		status->size);
out:
	return rc;
}

void
ds_rebuild_running_query(uuid_t pool_uuid, uint32_t *upper_ver, daos_epoch_t *stable_eph,
			 uint32_t *generation)
{
	struct rebuild_tgt_pool_tracker	*rpt;

	if (upper_ver)
		*upper_ver = 0;
	if (stable_eph)
		*stable_eph = 0;
	if (generation)
		*generation = -1;
	rpt = rpt_lookup(pool_uuid, -1, -1);
	if (rpt != NULL && !rpt->rt_global_done && !rpt->rt_abort) {
		if (stable_eph)
			*stable_eph = rpt->rt_stable_epoch;
		if (upper_ver)
			*upper_ver = rpt->rt_rebuild_ver;
		if (generation)
			*generation = rpt->rt_rebuild_gen;
	}
	if (rpt)
		rpt_put(rpt);
}

/* TODO: Add something about what the current operation is for output status */
int
ds_rebuild_query(uuid_t pool_uuid, struct daos_rebuild_status *status)
{
	struct rebuild_global_pool_tracker	*rgt;
	struct daos_rebuild_status		*rs_inlist;
	int					rc = 0;

	memset(status, 0, sizeof(*status));

	rgt = rebuild_global_pool_tracker_lookup(pool_uuid, -1, -1);
	if (rgt == NULL) {
		rs_inlist = rebuild_status_completed_lookup(pool_uuid);
		if (rs_inlist != NULL) {
			memcpy(status, rs_inlist, sizeof(*status));
		} else {
			struct ds_pool *pool;

			/* XXX sigh, no easy way check if pool has been through
			 * rebuild/exclude process, so let's check pool_map_version
			 * for now.
			 */
			rc = ds_pool_lookup(pool_uuid, &pool);
			if (pool == NULL || pool->sp_map_version < 2) {
				status->rs_state = DRS_NOT_STARTED;
			} else {
				status->rs_state = DRS_COMPLETED;
				status->rs_version = ds_pool_get_version(pool);
			}
			if (pool != NULL)
				ds_pool_put(pool);
			rc = 0;
		}
	} else {
		memcpy(status, &rgt->rgt_status, sizeof(*status));
		status->rs_version = rgt->rgt_rebuild_ver;
		rgt_put(rgt);
	}

	/* If there are still rebuild task queued/running for the pool, let's reset
	 * the done status.
	 */
	if (status->rs_state == DRS_COMPLETED &&
	    (!d_list_empty(&rebuild_gst.rg_queue_list) ||
	     !d_list_empty(&rebuild_gst.rg_running_list))) {
		struct rebuild_task *task;

		d_list_for_each_entry(task, &rebuild_gst.rg_queue_list, dst_list) {
			if (uuid_compare(task->dst_pool_uuid, pool_uuid) == 0) {
				status->rs_state = DRS_IN_PROGRESS;
				D_GOTO(out, rc);
			}
		}

		d_list_for_each_entry(task, &rebuild_gst.rg_running_list, dst_list) {
			if (uuid_compare(task->dst_pool_uuid, pool_uuid) == 0) {
				status->rs_state = DRS_IN_PROGRESS;
				D_GOTO(out, rc);
			}
		}
	}

out:
	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID" state %d rec "DF_U64" obj "
		DF_U64" ver %d err %d\n", DP_UUID(pool_uuid),
		status->rs_state, status->rs_rec_nr, status->rs_obj_nr,
		status->rs_version, status->rs_errno);

	return rc;
}

static void
rebuild_leader_status_notify(struct rebuild_global_pool_tracker *rgt, struct ds_pool *pool,
			     int op, unsigned int rank)
{
	struct rebuild_iv	iv = { 0 };
	int			rc;

	uuid_copy(iv.riv_pool_uuid, rgt->rgt_pool_uuid);
	iv.riv_rank		= rank;
	iv.riv_master_rank	= pool->sp_iv_ns->iv_master_rank;
	iv.riv_ver		= rgt->rgt_rebuild_ver;
	iv.riv_global_scan_done = is_rebuild_global_scan_done(rgt);
	iv.riv_global_done	= rgt->rgt_abort || is_rebuild_global_done(rgt);
	iv.riv_leader_term	= rgt->rgt_leader_term;
	iv.riv_rebuild_gen	= rgt->rgt_rebuild_gen;
	iv.riv_seconds          = rgt->rgt_status.rs_seconds;
	iv.riv_stable_epoch	= rgt->rgt_stable_epoch;
	iv.riv_sync = 1;
	rgt->rgt_dtx_resync_version = iv.riv_global_dtx_resyc_version =
				rebuild_get_global_dtx_resync_ver(rgt);
	iv.riv_dtx_resyc_version = pool->sp_dtx_resync_version;


	D_DEBUG(DB_REBUILD, DF_UUID "/%u/%u op: %s dtx %u scan_gd/gd/abort %u/%u/%u: %d\n",
		DP_UUID(rgt->rgt_pool_uuid), rgt->rgt_rebuild_ver, rgt->rgt_rebuild_gen,
		RB_OP_STR(op), iv.riv_global_dtx_resyc_version, iv.riv_global_scan_done,
		iv.riv_global_done, rgt->rgt_abort, rgt->rgt_status.rs_errno);

	rc = rebuild_iv_update(pool->sp_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
			       CRT_IV_SYNC_LAZY, true);
	if (rc)
		D_ERROR("iv final update fails"DF_UUID":rc "DF_RC"\n",
			DP_UUID(rgt->rgt_pool_uuid), DP_RC(rc));
}

#define RBLD_SBUF_LEN	356

enum {
	RB_BCAST_NONE,
	RB_BCAST_MAP,
	RB_BCAST_QUERY,
};

/*
 * Check rebuild status on the leader. Every other target sends
 * its own rebuild status by IV.
 */
static void
rebuild_leader_status_check(struct ds_pool *pool, uint32_t op,
			    struct rebuild_global_pool_tracker *rgt)
{
	double			last_print = 0;
	unsigned int		total;
	struct sched_req_attr	attr = { 0 };
	d_rank_t		myrank;
	int			rc;

	rc = crt_group_size(pool->sp_group, &total);
	if (rc)
		return;

	rc = crt_group_rank(pool->sp_group, &myrank);
	if (rc)
		return;

	sched_req_attr_init(&attr, SCHED_REQ_MIGRATE, &rgt->rgt_pool_uuid);
	rgt->rgt_ult = sched_req_get(&attr, ABT_THREAD_NULL);
	if (rgt->rgt_ult == NULL)
		return;

	while (1) {
		struct daos_rebuild_status	*rs = &rgt->rgt_status;
		char				sbuf[RBLD_SBUF_LEN];
		double				now;
		char				*str;
		d_rank_list_t			excluded = { 0 };
		int				i;

		ABT_rwlock_rdlock(pool->sp_lock);
		rc = map_ranks_init(pool->sp_map,
				    PO_COMP_ST_UP | PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT,
				    &excluded);
		if (rc != 0) {
			D_INFO(DF_UUID": get rank list: %d\n", DP_UUID(pool->sp_uuid), rc);
			ABT_rwlock_unlock(pool->sp_lock);
			goto sleep;
		}

		for (i = 0; i < excluded.rl_nr; i++) {
			struct pool_domain *dom;

			dom = pool_map_find_node_by_rank(pool->sp_map, excluded.rl_ranks[i]);
			D_ASSERT(dom != NULL);

			/* If the rank is being scheduled after reintegration
			 * start, let's skip the rank, i.e. set it DONE.
			 */
			if (dom->do_comp.co_status == PO_COMP_ST_UP &&
			    dom->do_comp.co_in_ver < rgt->rgt_rebuild_ver)
				continue;

			D_INFO(DF_UUID" exclude rank %d/%x.\n", DP_UUID(pool->sp_uuid),
			       dom->do_comp.co_rank, dom->do_comp.co_status);
			rebuild_leader_set_status(rgt, dom->do_comp.co_rank,
						  -1, SCAN_DONE | PULL_DONE);
		}
		ABT_rwlock_unlock(pool->sp_lock);
		map_ranks_fini(&excluded);

		if (!rgt->rgt_abort && !is_rebuild_global_done(rgt) &&
		    myrank == pool->sp_iv_ns->iv_master_rank)
			rebuild_leader_status_notify(rgt, pool, op, myrank);

		/* query the current rebuild status */
		if (is_rebuild_global_done(rgt))
			rs->rs_state = DRS_COMPLETED;

		if (rs->rs_state == DRS_COMPLETED)
			str = rs->rs_errno ? "failed" : "completed";
		else if (rgt->rgt_abort || rebuild_gst.rg_abort)
			str = "aborted";
		else if (rs->rs_obj_nr == 0 && rs->rs_rec_nr == 0)
			str = "scanning";
		else
			str = "pulling";

		rs->rs_seconds =
			(d_timeus_secdiff(0) - rgt->rgt_time_start) / 1e6;
		snprintf(sbuf, RBLD_SBUF_LEN,
			 "%s [%s] (pool "DF_UUID" leader %u term "DF_U64" dtx gl %u ver=%u,"
			 "gen %u toberb_obj=" DF_U64", rb_obj="DF_U64", rec="DF_U64", size="DF_U64
			 " done %d status %d/%d  stable "DF_X64" reclaim "DF_X64
			 " duration=%d secs)\n",
			 RB_OP_STR(op), str, DP_UUID(pool->sp_uuid), myrank,
			 rgt->rgt_leader_term, rgt->rgt_dtx_resync_version, rgt->rgt_rebuild_ver,
			 rgt->rgt_rebuild_gen, rs->rs_toberb_obj_nr, rs->rs_obj_nr, rs->rs_rec_nr,
			 rs->rs_size, rs->rs_state, rs->rs_errno, rs->rs_fail_rank,
			 rgt->rgt_stable_epoch, rgt->rgt_reclaim_epoch, rs->rs_seconds);

		D_INFO("%s", sbuf);
		if (rs->rs_state == DRS_COMPLETED || rebuild_gst.rg_abort ||
		    rgt->rgt_abort) {
			D_PRINT("%s", sbuf);
			break;
		}

		now = ABT_get_wtime();
		/* print something at least for each 10 seconds */
		if (now - last_print > 10) {
			last_print = now;
			D_PRINT("%s", sbuf);
		}
sleep:
		sched_req_sleep(rgt->rgt_ult, RBLD_CHECK_INTV);
	}

	sched_req_put(rgt->rgt_ult);
	rgt->rgt_ult = NULL;
}

static void
rebuild_global_pool_tracker_destroy(struct rebuild_global_pool_tracker *rgt)
{
	D_ASSERT(rgt->rgt_refcount == 0);
	d_list_del(&rgt->rgt_list);
	if (rgt->rgt_servers)
		D_FREE(rgt->rgt_servers);

	if (rgt->rgt_lock)
		ABT_mutex_free(&rgt->rgt_lock);

	if (rgt->rgt_done_cond)
		ABT_cond_free(&rgt->rgt_done_cond);

	D_FREE(rgt);
}

static int
rebuild_global_pool_tracker_create(struct ds_pool *pool, uint32_t ver, uint32_t rebuild_gen,
				   uint64_t leader_term, daos_epoch_t reclaim_eph,
				   struct rebuild_global_pool_tracker **p_rgt)
{
	struct rebuild_global_pool_tracker *rgt;
	int node_nr;
	struct pool_domain *doms;
	int i;
	int rc = 0;

	D_ALLOC_PTR(rgt);
	if (rgt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&rgt->rgt_list);
	node_nr = pool_map_find_nodes(pool->sp_map, PO_COMP_ID_ALL, &doms);
	if (node_nr < 0)
		D_GOTO(out, rc = node_nr);

	D_ALLOC_ARRAY(rgt->rgt_servers, node_nr);
	if (rgt->rgt_servers == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ABT_mutex_create(&rgt->rgt_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&rgt->rgt_done_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(out, rc = dss_abterr2der(rc));

	for (i = 0; i < node_nr; i++)
		rgt->rgt_servers[i].rank = doms[i].do_comp.co_rank;
	rgt->rgt_servers_number = node_nr;

	uuid_copy(rgt->rgt_pool_uuid, pool->sp_uuid);
	rgt->rgt_rebuild_ver = ver;
	rgt->rgt_status.rs_version = ver;
	rgt->rgt_leader_term = leader_term;
	rgt->rgt_rebuild_gen = rebuild_gen;
	rgt->rgt_time_start = d_timeus_secdiff(0);
	rgt->rgt_reclaim_epoch = reclaim_eph;
	d_list_add(&rgt->rgt_list, &rebuild_gst.rg_global_tracker_list);
	*p_rgt = rgt;
	rgt->rgt_refcount = 1;
out:
	if (rc)
		rebuild_global_pool_tracker_destroy(rgt);
	return rc;
}

void
rgt_get(struct rebuild_global_pool_tracker *rgt)
{
	rgt->rgt_refcount++;
}

void
rgt_put(struct rebuild_global_pool_tracker *rgt)
{
	rgt->rgt_refcount--;
	if (rgt->rgt_refcount == 0)
		rebuild_global_pool_tracker_destroy(rgt);
}

struct rebuild_global_pool_tracker *
rebuild_global_pool_tracker_lookup(const uuid_t pool_uuid, unsigned int ver, unsigned int gen)
{
	struct rebuild_global_pool_tracker	*rgt;
	struct rebuild_global_pool_tracker	*found = NULL;

	/* Only stream 0 will access the list */
	d_list_for_each_entry(rgt, &rebuild_gst.rg_global_tracker_list, rgt_list) {
		if (uuid_compare(rgt->rgt_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) || rgt->rgt_rebuild_ver == ver) &&
		    (gen == (unsigned int)(-1) || rgt->rgt_rebuild_gen == gen)) {
			rgt_get(rgt);
			found = rgt;
			break;
		}
	}

	return found;
}

enum {
	REBUILD_TARGET	= 1,
	UPDATE_TARGET	= 1 << 1,
};

static void
rebuild_tgt_check(struct pool_target *target, uint32_t opc, uint32_t *actions)
{
	switch (opc) {
	case RB_OP_EXCLUDE:
		if (target->ta_comp.co_status == PO_COMP_ST_DOWN)
			*actions |= REBUILD_TARGET | UPDATE_TARGET;
		else if (target->ta_comp.co_status == PO_COMP_ST_UP)
			*actions |= UPDATE_TARGET;
		break;
	case RB_OP_DRAIN:
		if (target->ta_comp.co_status == PO_COMP_ST_DRAIN)
			*actions |= REBUILD_TARGET | UPDATE_TARGET;
		break;
	case RB_OP_REINT:
	case RB_OP_EXTEND:
		if (target->ta_comp.co_status == PO_COMP_ST_UP)
			*actions |= REBUILD_TARGET | UPDATE_TARGET;
		else if (target->ta_comp.co_status == PO_COMP_ST_DRAIN)
			*actions |= UPDATE_TARGET;
		break;
	default:
		D_ASSERTF(0, "invalid opc %u\n", opc);
		break;
	}
}

/* To notify all targets to prepare the rebuild */
static int
rebuild_prepare(struct ds_pool *pool, uint32_t rebuild_ver,
		uint32_t rebuild_gen, uint64_t leader_term,
		daos_epoch_t reclaim_eph,
		struct pool_target_id_list *tgts,
		daos_rebuild_opc_t rebuild_op,
		struct rebuild_global_pool_tracker **rgt, uint32_t *actions)
{
	int	rc = 0;

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" create rebuild iv, op=%s\n",
		DP_UUID(pool->sp_uuid), RB_OP_STR(rebuild_op));

	if (rebuild_op == RB_OP_UPGRADE || rebuild_op == RB_OP_RECLAIM ||
	    rebuild_op == RB_OP_FAIL_RECLAIM) {
		*actions |= REBUILD_TARGET | UPDATE_TARGET;
	} else {
		int	i;

		D_ASSERT(tgts != NULL && tgts->pti_number > 0);
		/* check the status of the target and decide the rebuild operation, i.e. does
		 * it need rebuild/reintegrate or only changing the target status
		 */
		for (i = 0; i < tgts->pti_number; i++) {
			struct pool_target *target;
			int ret;

			ret = pool_map_find_target(pool->sp_map, tgts->pti_ids[i].pti_id,
						   &target);
			if (ret <= 0)
				continue;

			D_ASSERT(target != NULL);
			rebuild_tgt_check(target, rebuild_op, actions);
		}
	}

	/* Create global rgt on leader to track the rebuild status */
	if (*actions & REBUILD_TARGET) {
		/* Update pool iv ns for the pool */
		rc = rebuild_global_pool_tracker_create(pool, rebuild_ver, rebuild_gen,
							leader_term, reclaim_eph, rgt);
		if (rc)
			D_ERROR("rebuild_global_pool_tracker create failed: rc %d\n", rc);
	}

	return rc;
}

/* Broadcast objects scan requests to all server targets to start
 * rebuild.
 */
static int
rebuild_scan_broadcast(struct ds_pool *pool, struct rebuild_global_pool_tracker *rgt,
		       struct pool_target_id_list *tgts_failed, uint32_t layout_version,
		       daos_rebuild_opc_t rebuild_op)
{
	struct rebuild_scan_in	*rsi;
	struct rebuild_scan_out	*rso;
	d_rank_list_t		*excluded = NULL;
	crt_rpc_t		*rpc;
	int			rc;

	/* There might be some other ranks being queued for reintegration,
	 * but not included in this reintegration, so let's exclude those
	 * ranks from this reintegration.
	 */
	D_DEBUG(DB_REBUILD, "rebuild op %d\n", rebuild_op);
	if (rebuild_op == RB_OP_REINT || rebuild_op == RB_OP_EXTEND ||
	    rebuild_op == RB_OP_RECLAIM || rebuild_op == RB_OP_FAIL_RECLAIM) {
		d_rank_list_t	up_ranks = { 0 };
		int		i;
		int		nr = 0;

		ABT_rwlock_rdlock(pool->sp_lock);
		rc = map_ranks_init(pool->sp_map, PO_COMP_ST_UP, &up_ranks);
		ABT_rwlock_unlock(pool->sp_lock);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to create rank list: %d\n",
				DP_UUID(pool->sp_uuid), rc);
			return rc;
		}

		D_DEBUG(DB_REBUILD, "up_ranks %d\n", up_ranks.rl_nr);
		excluded = d_rank_list_alloc(up_ranks.rl_nr);
		/* exclude ranks which is scheduled after the current
		 * reintegraion started.
		 */
		if (excluded == NULL) {
			map_ranks_fini(&up_ranks);
			return -DER_NOMEM;
		}

		for (i = 0; i < up_ranks.rl_nr; i++) {
			struct pool_domain *dom;

			dom = pool_map_find_node_by_rank(pool->sp_map, up_ranks.rl_ranks[i]);
			D_ASSERT(dom != NULL);
			D_DEBUG(DB_REBUILD, "rank %u ver %u rebuild %u\n",
				up_ranks.rl_ranks[i], dom->do_comp.co_in_ver, rgt->rgt_rebuild_ver);
			if (dom->do_comp.co_in_ver < rgt->rgt_rebuild_ver)
				continue;

			excluded->rl_ranks[nr++] = up_ranks.rl_ranks[i];
		}
		excluded->rl_nr = nr;
		map_ranks_fini(&up_ranks);
	}

	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_OBJECTS_SCAN, DAOS_REBUILD_VERSION,
				  &rpc, NULL, excluded);
	if (rc != 0) {
		D_ERROR("pool map broad cast failed: rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rsi = crt_req_get(rpc);
	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID" scan broadcast, op=%s\n",
		DP_UUID(pool->sp_uuid), RB_OP_STR(rebuild_op));

	uuid_copy(rsi->rsi_pool_uuid, pool->sp_uuid);
	rsi->rsi_ns_id = pool->sp_iv_ns->iv_ns_id;
	rsi->rsi_leader_term = rgt->rgt_leader_term;
	rsi->rsi_rebuild_ver = rgt->rgt_rebuild_ver;
	rsi->rsi_rebuild_gen = rgt->rgt_rebuild_gen;
	if (rebuild_op == RB_OP_RECLAIM || rebuild_op == RB_OP_FAIL_RECLAIM)
		D_ASSERT(rgt->rgt_reclaim_epoch != 0);

	rsi->rsi_reclaim_epoch = rgt->rgt_reclaim_epoch;
	rsi->rsi_layout_ver = layout_version;
	rsi->rsi_tgts_num = tgts_failed->pti_number;
	rsi->rsi_rebuild_op = rebuild_op;
	crt_group_rank(pool->sp_group,  &rsi->rsi_master_rank);

	rc = dss_rpc_send(rpc);
	rso = crt_reply_get(rpc);
	if (rc == 0)
		rc = rso->rso_status;

	rgt->rgt_init_scan = 1;
	rgt->rgt_stable_epoch = rso->rso_stable_epoch;
	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID": "DF_RC" got stable/reclaim epoch "
		DF_X64"/"DF_X64"\n", DP_UUID(rsi->rsi_pool_uuid), DP_RC(rc),
		rgt->rgt_stable_epoch, rgt->rgt_reclaim_epoch);
	crt_req_decref(rpc);
out:
	if (excluded)
		d_rank_list_free(excluded);
	return rc;
}

static void
rpt_destroy(struct rebuild_tgt_pool_tracker *rpt)
{
	D_ASSERT(rpt->rt_refcount == 0);
	D_ASSERT(d_list_empty(&rpt->rt_list));
	if (daos_handle_is_valid(rpt->rt_tobe_rb_root_hdl)) {
		dbtree_destroy(rpt->rt_tobe_rb_root_hdl, NULL);
		rpt->rt_tobe_rb_root_hdl = DAOS_HDL_INVAL;
	}
	if (daos_handle_is_valid(rpt->rt_rebuilt_root_hdl)) {
		rebuilt_btr_destroy(rpt->rt_rebuilt_root_hdl);
		rpt->rt_rebuilt_root_hdl = DAOS_HDL_INVAL;
	}

	uuid_clear(rpt->rt_pool_uuid);
	if (rpt->rt_pool != NULL)
		ds_pool_put(rpt->rt_pool);

	if (rpt->rt_svc_list)
		d_rank_list_free(rpt->rt_svc_list);

	if (rpt->rt_lock)
		ABT_mutex_free(&rpt->rt_lock);

	if (rpt->rt_fini_cond)
		ABT_cond_free(&rpt->rt_fini_cond);

	if (rpt->rt_global_dtx_wait_cond)
		ABT_cond_free(&rpt->rt_global_dtx_wait_cond);

	D_FREE(rpt);
}

void
rpt_get(struct rebuild_tgt_pool_tracker	*rpt)
{
	ABT_mutex_lock(rpt->rt_lock);
	D_ASSERT(rpt->rt_refcount >= 0);
	rpt->rt_refcount++;

	D_DEBUG(DB_REBUILD, "rpt %p ref %d\n", rpt, rpt->rt_refcount);
	ABT_mutex_unlock(rpt->rt_lock);
}

void
rpt_put(struct rebuild_tgt_pool_tracker	*rpt)
{
	ABT_mutex_lock(rpt->rt_lock);
	rpt->rt_refcount--;
	D_ASSERT(rpt->rt_refcount >= 0);
	D_DEBUG(DB_REBUILD, "rpt %p ref %d\n", rpt, rpt->rt_refcount);
	if (rpt->rt_refcount == 1 && rpt->rt_finishing)
		ABT_cond_signal(rpt->rt_fini_cond);
	ABT_mutex_unlock(rpt->rt_lock);
}

static void
rebuild_task_destroy(struct rebuild_task *task)
{
	if (task == NULL)
		return;

	d_list_del(&task->dst_list);
	pool_target_id_list_free(&task->dst_tgts);
	D_FREE(task);
}

/**
 * Print out all of the currently queued rebuild tasks
 */
static void
rebuild_debug_print_queue()
{
	struct rebuild_task *task;
	/* Uninitialized stack buffer to write target list into
	 * This only accumulates the targets in a single task, so it doesn't
	 * need to be very big. 200 bytes is enough for ~30 5-digit target ids
	 */
	char tgts_buf[200];
	int i;
	/* Position in stack buffer where str data should be written next */
	size_t tgts_pos;

	D_DEBUG(DB_REBUILD, "Current rebuild queue:\n");

	d_list_for_each_entry(task, &rebuild_gst.rg_queue_list, dst_list) {
		tgts_pos = 0;
		for (i = 0; i < task->dst_tgts.pti_number; i++) {
			if (tgts_pos > sizeof(tgts_buf) - 10) {
				/* Stop a bit before we get to the end of the
				 * buffer to avoid printing a large target id
				 * that gets cut off. Instead just add an
				 * indication there was more data not printed
				 */
				tgts_pos += snprintf(&tgts_buf[tgts_pos],
						     sizeof(tgts_buf) -
						     tgts_pos, "...");
				break;
			}
			tgts_pos += snprintf(&tgts_buf[tgts_pos],
					     sizeof(tgts_buf) - tgts_pos,
					     "%u ",
					     task->dst_tgts.pti_ids[i].pti_id);
		}
		D_DEBUG(DB_REBUILD, DF_UUID" op=%s ver=%u tgts=%s\n",
			DP_UUID(task->dst_pool_uuid), RB_OP_STR(task->dst_rebuild_op),
			task->dst_map_ver, tgts_buf);
	}
}

static uint32_t
rebuild_task_get_min_version(struct pool_map *map, struct pool_target_id_list *tgts)
{
	uint32_t min_version = pool_map_get_version(map);
	int i;

	for (i = 0; i < tgts->pti_number; i++) {
		struct pool_target	*tgt = NULL;
		int			rc;

		rc = pool_map_find_target(map, tgts->pti_ids[i].pti_id, &tgt);
		D_ASSERT(rc == 1);
		D_ASSERT(tgt != NULL);
		if (tgt->ta_comp.co_status == PO_COMP_ST_UP)
			min_version = min(min_version, tgt->ta_comp.co_in_ver);
		else if (tgt->ta_comp.co_status == PO_COMP_ST_DOWN ||
			 tgt->ta_comp.co_status == PO_COMP_ST_DRAIN)
			min_version = min(min_version, tgt->ta_comp.co_fseq);
	}

	return min_version;
}

/** Try merge the tasks to the current task.
 *
 * This will only merge tasks that are for sequential/contiguous version
 * operations on the pool map. It is important that the operations are processed
 * in the correct order to maintain data correctness. This means that even if
 * some failure recovery operations are queued already, if there was a
 * reintegration scheduled for after that, new failures will need to be queued
 * after the reintegration to maintain data correctness.
 *
 * return 1 means the rebuild targets were successfully merged to existing task.
 * return 0 means these targets can not merge.
 * Other return value indicates an error.
 */
static int
rebuild_try_merge_tgts(struct ds_pool *pool, uint32_t map_ver,
		       daos_rebuild_opc_t rebuild_op,
		       struct pool_target_id_list *tgts)
{
	struct rebuild_task *task;
	struct rebuild_task *merge_pre_task = NULL;
	struct rebuild_task *merge_post_task = NULL;
	struct rebuild_task *merge_task = NULL;
	int rc;

	/* Loop over all queued tasks, and evaluate whether this task can safely
	 * join to the queued task.
	 *
	 * Specifically, a task isn't safe to merge to if another operation of
	 * a different type (with higher pool map version) has been scheduled
	 * after a potential merge target. Merging would cause rebuild to
	 * essentially skip the intermediary different-type step because the
	 * rebuild version is set to the task map version after rebuild is
	 * complete.
	 */
	d_list_for_each_entry(task, &rebuild_gst.rg_queue_list, dst_list) {
		if (uuid_compare(task->dst_pool_uuid, pool->sp_uuid) != 0) {
			if (merge_pre_task == NULL)
				continue;
			break;
		}

		if (merge_pre_task == NULL)
			merge_pre_task = task;

		if (task->dst_map_ver <= map_ver) {
			if (merge_pre_task->dst_map_ver < task->dst_map_ver)
				merge_pre_task = task;
		} else {
			merge_post_task = task;
			break;
		}
	}

	if (merge_pre_task != NULL && merge_pre_task->dst_rebuild_op == rebuild_op)
		merge_task = merge_pre_task;
	else if (merge_post_task != NULL && merge_post_task->dst_rebuild_op == rebuild_op)
		merge_task = merge_post_task;

	/* Did not find a suitable target. The task will be added to the @rg_queue_list. */
	if (merge_task == NULL)
		return 0;

	D_DEBUG(DB_REBUILD, "("DF_UUID" ver=%u) id %u merge to task %p op=%s\n",
		DP_UUID(pool->sp_uuid), map_ver, tgts->pti_ids[0].pti_id, merge_task,
		RB_OP_STR(rebuild_op));

	/* Merge the failed ranks to existing rebuild task */
	rc = pool_target_id_list_merge(&merge_task->dst_tgts, tgts);
	if (rc)
		return rc;

	if (merge_task->dst_map_ver < map_ver) {
		D_DEBUG(DB_REBUILD, "rebuild task ver %u --> %u\n",
			merge_task->dst_map_ver, map_ver);
		merge_task->dst_map_ver = map_ver;
	}

	merge_task->dst_reclaim_ver = rebuild_task_get_min_version(pool->sp_map, tgts);
	D_PRINT("%s [queued] ("DF_UUID" ver=%u/%u) id %u\n",
		RB_OP_STR(rebuild_op), DP_UUID(pool->sp_uuid), map_ver,
		merge_task->dst_reclaim_ver, tgts->pti_ids[0].pti_id);

	/* Print out the current queue to the debug log */
	rebuild_debug_print_queue();

	return 1;
}

/**
 * Initiate the rebuild process, i.e. sending rebuild requests to every target
 * to find out the impacted objects.
 */
static int
rebuild_leader_start(struct ds_pool *pool, struct rebuild_task *task,
		     struct rebuild_global_pool_tracker **p_rgt, uint32_t *actions)
{
	uint64_t	leader_term;
	int		rc;

	rc = ds_pool_svc_term_get(pool->sp_uuid, &leader_term);
	if (rc) {
		D_ERROR("Get pool service term failed: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = rebuild_prepare(pool, task->dst_map_ver, ++pool->sp_rebuild_gen,
			     leader_term, task->dst_reclaim_eph, &task->dst_tgts,
			     task->dst_rebuild_op, p_rgt, actions);
	if (rc) {
		D_ERROR("rebuild prepare failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (!(*actions & REBUILD_TARGET)) {
		D_INFO("rebuild "DF_UUID", version=%u/%u, op=%s not need schecdule action %x\n",
		       DP_UUID(pool->sp_uuid), task->dst_map_ver, pool->sp_rebuild_gen,
		       RB_OP_STR(task->dst_rebuild_op), *actions);
		D_GOTO(out, rc);
	}

	D_ASSERT(*p_rgt != NULL);
	D_INFO("rebuild "DF_UUID", version=%u/%u, op=%s\n",
	       DP_UUID(pool->sp_uuid), task->dst_map_ver, pool->sp_rebuild_gen,
	       RB_OP_STR(task->dst_rebuild_op));

	/* broadcast scan RPC to all targets */
	rc = rebuild_scan_broadcast(pool, *p_rgt, &task->dst_tgts,
				    task->dst_new_layout_version, task->dst_rebuild_op);
	if (rc) {
		D_ERROR("object scan failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	return rc;
}

static bool
retry_rebuild_task(struct rebuild_task *task, int error, daos_rebuild_opc_t *opc)
{
	/* Only be called if rebuild task failed */

	/* retry with network error, since the pool map will be changed accordingly, so
	 * rebuild job can be fixed by the new pool map anyway.
	 */
	if (daos_crt_network_error(error) || error == -DER_TIMEDOUT ||
	    error == -DER_GRPVER || error == -DER_STALE || error == -DER_VOS_PARTIAL_UPDATE) {
		D_DEBUG(DB_REBUILD, DF_UUID" retry with error %d\n",
			DP_UUID(task->dst_pool_uuid), error);
		*opc = task->dst_rebuild_op;
		return true;
	}

	/* Reclaim and exclude task needs to retry indefinityly */
	if (task->dst_rebuild_op == RB_OP_RECLAIM ||
	    task->dst_rebuild_op == RB_OP_FAIL_RECLAIM ||
	    task->dst_rebuild_op == RB_OP_EXCLUDE) {
		*opc = task->dst_rebuild_op;
		return true;
	}

	/* upgrade, reintegrate and extend can be canceled, i.e. scheduling reverse job
	 * to restore the pool map.
	 */
	if (task->dst_rebuild_op == RB_OP_EXTEND || task->dst_rebuild_op == RB_OP_REINT)
		*opc = RB_OP_EXCLUDE;
	else if (task->dst_rebuild_op == RB_OP_DRAIN)
		*opc = RB_OP_REINT;
	else
		*opc = RB_OP_NONE;

	return false;
}

static int
rebuild_task_complete_schedule(struct rebuild_task *task, struct ds_pool *pool,
			       struct rebuild_global_pool_tracker *rgt, int ret)
{
	int rc = 0;
	int rc1;

	/* The original job is not being started correctly, let's give another chance */
	if (rgt == NULL) {
		/* ret = 0, it do not need any rebuild, only update target status */
		if (ret == 0) {
			D_INFO(DF_UUID"opc %u/%u only update tgt status: %d\n",
			       DP_UUID(task->dst_pool_uuid), task->dst_rebuild_op,
			       task->dst_map_ver, ret);
			return 0;
		}

		D_INFO(DF_UUID"retry opc %u/%u: %d\n", DP_UUID(task->dst_pool_uuid),
		       task->dst_rebuild_op, task->dst_map_ver, ret);
		rc = ds_rebuild_schedule(pool, task->dst_map_ver, task->dst_reclaim_eph,
					 task->dst_new_layout_version,
					 &task->dst_tgts, task->dst_rebuild_op, 5);
		return rc;
	}

	/* Do not need reschedule for upgrade, let's mark the upgrade as complete */
	if (task->dst_rebuild_op == RB_OP_UPGRADE) {
		rc1 = ret;

		if (rgt != NULL && rgt->rgt_status.rs_errno != 0)
			rc1 = rgt->rgt_status.rs_errno;

		rc = ds_pool_mark_upgrade_completed(pool->sp_uuid, rc1);

		D_INFO("Mark upgraded complete "DF_UUID": %d\n", DP_UUID(task->dst_pool_uuid), rc1);
	}

	if (!is_rebuild_global_done(rgt) || rgt->rgt_status.rs_errno != 0) {
		daos_rebuild_opc_t	retry_opc = 0;

		/* If current job failed */
		rgt->rgt_status.rs_state = DRS_IN_PROGRESS;
		if (task->dst_rebuild_op == RB_OP_RECLAIM ||
		    task->dst_rebuild_op == RB_OP_FAIL_RECLAIM) {
			rc = ds_rebuild_schedule(pool, task->dst_map_ver, rgt->rgt_stable_epoch,
						 task->dst_new_layout_version, &task->dst_tgts,
						 task->dst_rebuild_op, 5);
			return rc;
		}

		/* Schedule reclaim to clean up current op. Let's keep go ahead to retry even if
		 * reclaim the current rebuilding fails.
		 */
		if (rgt->rgt_init_scan) {
			/* NB: dst_reclaim_ver is the minimum rebuild target version, once rebuild
			 * fails, it will be used to discard all of the previous rebuild data
			 * (reclaim - 1 see obj_reclaim()), but keep the in-flight I/O data.
			 */
			rc = ds_rebuild_schedule(pool, task->dst_reclaim_ver - 1,
						 rgt->rgt_stable_epoch,
						 task->dst_new_layout_version,
						 &task->dst_tgts, RB_OP_FAIL_RECLAIM, 5);
			if (rc)
				D_ERROR(DF_UUID "schedule reclaim fail: "DF_RC"\n",
					DP_UUID(task->dst_pool_uuid), DP_RC(rc));
		}

		/* Then check if it needs to retry */
		if (!retry_rebuild_task(task, rgt->rgt_status.rs_errno, &retry_opc)) {
			rgt->rgt_status.rs_state = DRS_COMPLETED;
			/* Update the rebuild complete status for pool query */
			rc1 = rebuild_status_completed_update(task->dst_pool_uuid,
							      &rgt->rgt_status);
			if (rc1 != 0)
				D_ERROR("rebuild_status_completed_update, "DF_UUID" failed:"
					DF_RC"\n", DP_UUID(task->dst_pool_uuid), DP_RC(rc1));

			D_DEBUG(DB_REBUILD, DF_UUID" retry with %u\n",
				DP_UUID(task->dst_pool_uuid), retry_opc);
			if (retry_opc == RB_OP_NONE)
				return rc1;

			/* NB: Still need schedule the job to restore the pool map */
		}

		rc = ds_rebuild_schedule(pool, task->dst_map_ver, rgt->rgt_stable_epoch,
					 task->dst_new_layout_version, &task->dst_tgts,
					 retry_opc, 5);
	} else if (task->dst_rebuild_op == RB_OP_REINT || task->dst_rebuild_op == RB_OP_EXTEND ||
		   task->dst_rebuild_op == RB_OP_UPGRADE) {
		/* Otherwise schedule reclaim for reintegrate/extend/upgrade. */
		rgt->rgt_status.rs_state = DRS_IN_PROGRESS;
		rc = ds_rebuild_schedule(pool, task->dst_map_ver, rgt->rgt_reclaim_epoch,
					 task->dst_new_layout_version,
					 &task->dst_tgts, RB_OP_RECLAIM, 5);
		if (rc != 0)
			D_ERROR("reschedule reclaim, "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(task->dst_pool_uuid), DP_RC(rc));
	}

	if (task->dst_rebuild_op != RB_OP_FAIL_RECLAIM) {
		/* Update the rebuild complete status for pool query */
		rc1 = rebuild_status_completed_update(task->dst_pool_uuid, &rgt->rgt_status);
		if (rc1 != 0) {
			D_ERROR("rebuild_status_completed_update, "DF_UUID" failed: "DF_RC"\n",
				DP_UUID(task->dst_pool_uuid), DP_RC(rc1));
			if (rc == 0)
				rc = rc1;
		}
	}

	return rc;
}

static void
rebuild_task_ult(void *arg)
{
	struct rebuild_task			*task = arg;
	struct ds_pool				*pool;
	uint32_t				global_ver = 0;
	struct rebuild_global_pool_tracker	*rgt = NULL;
	d_rank_t				myrank;
	uint64_t				cur_ts = 0;
	uint32_t				actions = 0;
	int					rc;

	cur_ts = daos_gettime_coarse();
	if (cur_ts < task->dst_schedule_time) {
		D_DEBUG(DB_REBUILD, "rebuild task sleep "DF_U64" second\n",
			task->dst_schedule_time - cur_ts);
		dss_sleep((task->dst_schedule_time - cur_ts) * 1000);
	}

	rc = ds_pool_lookup(task->dst_pool_uuid, &pool);
	if (pool == NULL) {
		D_ERROR(DF_UUID": failed to look up pool: %d\n",
			DP_UUID(task->dst_pool_uuid), rc);
		D_GOTO(out_task, rc = -DER_NONEXIST);
	}

	while (1) {
		/* Check if the leader pool map has been synced to all other targets
		 * to avoid -DER_GRP error.
		 */
		rc = ds_pool_svc_global_map_version_get(task->dst_pool_uuid, &global_ver);
		if (rc) {
			D_ERROR("Get pool service version failed: "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out_pool, rc);
		}

		D_DEBUG(DB_REBUILD, "global_ver %u map ver %u\n", global_ver,
			task->dst_map_ver);

		if (pool->sp_stopping)
			D_GOTO(out_pool, rc = -DER_SHUTDOWN);

		if (pool->sp_map_version <= global_ver)
			break;

		dss_sleep(1000);
	}


	rc = crt_group_rank(pool->sp_group, &myrank);
	D_ASSERT(rc == 0);
	rc = rebuild_notify_ras_start(&task->dst_pool_uuid, task->dst_map_ver,
				      RB_OP_STR(task->dst_rebuild_op));
	if (rc)
		D_ERROR(DF_UUID": failed to send RAS event\n",
			DP_UUID(task->dst_pool_uuid));

	D_PRINT("%s [started] (pool "DF_UUID" ver=%u/%u)\n",
		RB_OP_STR(task->dst_rebuild_op), DP_UUID(task->dst_pool_uuid),
		task->dst_map_ver, task->dst_reclaim_ver);

	rc = rebuild_leader_start(pool, task, &rgt, &actions);
	if (rc != 0) {
		if (rc == -DER_NOTLEADER &&
		    pool->sp_iv_ns->iv_master_rank != (d_rank_t)(-1) &&
		    pool->sp_iv_ns->iv_master_rank != myrank) {
			/* If it is not leader, the new leader will step up
			 * restart rebuild anyway, so do not need reschedule
			 * rebuild on this node anymore.
			 */
			D_DEBUG(DB_REBUILD, "pool "DF_UUID" ver/master %u/%u rebuild is"
				" canceled.\n", DP_UUID(task->dst_pool_uuid),
				task->dst_map_ver, pool->sp_iv_ns->iv_master_rank);
			rc = 0;
			D_PRINT("%s [canceled] (pool "DF_UUID" ver=%u"
				" status="DF_RC")\n",
				DP_UUID(task->dst_pool_uuid),
				RB_OP_STR(task->dst_rebuild_op),
				task->dst_map_ver, DP_RC(rc));
			D_GOTO(output, rc);
		}

		D_PRINT("%s [failed] (pool "DF_UUID" ver=%u status="DF_RC")\n",
			RB_OP_STR(task->dst_rebuild_op),
			DP_UUID(task->dst_pool_uuid), task->dst_map_ver,
			DP_RC(rc));

		D_DEBUG(DB_REBUILD, ""DF_UUID" (ver=%u) rebuild failed: "DF_RC"\n",
			DP_UUID(task->dst_pool_uuid), task->dst_map_ver, DP_RC(rc));
		if (rgt) {
			rgt->rgt_abort = 1;
			rgt->rgt_status.rs_errno = rc;
			D_GOTO(done, rc);
		} else {
			D_GOTO(try_reschedule, rc);
		}
	}

	if (actions & REBUILD_TARGET) {
		/* Start leader tracking ULT to wait until rebuild finished */
		rebuild_leader_status_check(pool, task->dst_rebuild_op, rgt);
	} else if (actions & UPDATE_TARGET) {
		/* Do not need rebuild target, only update the target status */
		D_PRINT("%s [completed] (pool "DF_UUID" ver=%u/%u)\n",
			RB_OP_STR(task->dst_rebuild_op), DP_UUID(task->dst_pool_uuid),
			task->dst_map_ver, task->dst_reclaim_ver);
		D_GOTO(update_tgts, rc);
	} else {
		D_GOTO(output, rc);
	}
done:
	D_ASSERT(rgt != NULL);
	if (!is_rebuild_global_done(rgt)) {
		D_DEBUG(DB_REBUILD, DF_UUID" rebuild is not done: "DF_RC"\n",
			DP_UUID(task->dst_pool_uuid),
			DP_RC(rgt->rgt_status.rs_errno));

		if (rgt->rgt_abort && rgt->rgt_status.rs_errno == 0) {
			/* If the leader is stopped due to the leader change,
			 * then let's do not stop the real rebuild(scan/pull
			 * ults), because the new leader will resend the
			 * scan requests, which will then become the new
			 * leader to track the rebuild.
			 */
			D_DEBUG(DB_REBUILD, DF_UUID" Only stop the leader\n",
				DP_UUID(task->dst_pool_uuid));
			D_GOTO(out_pool, rc);
		}
	} else if (rgt->rgt_status.rs_errno == 0) {
update_tgts:
		if (task->dst_tgts.pti_number <= 0 || task->dst_rebuild_op == RB_OP_UPGRADE)
			goto iv_stop;

		if (task->dst_rebuild_op == RB_OP_EXCLUDE ||
		    task->dst_rebuild_op == RB_OP_DRAIN) {
			rc = ds_pool_tgt_exclude_out(pool->sp_uuid, &task->dst_tgts);
			D_INFO("mark failed target %d of "DF_UUID " as DOWNOUT: "DF_RC"\n",
			       task->dst_tgts.pti_ids[0].pti_id,
			       DP_UUID(task->dst_pool_uuid), DP_RC(rc));
		} else if (task->dst_rebuild_op == RB_OP_REINT ||
			   task->dst_rebuild_op == RB_OP_EXTEND) {
			rc = ds_pool_tgt_add_in(pool->sp_uuid, &task->dst_tgts);
			D_INFO("mark added target %d of "DF_UUID " UPIN: "DF_RC"\n",
			       task->dst_tgts.pti_ids[0].pti_id,
			       DP_UUID(task->dst_pool_uuid), DP_RC(rc));
		}
	}
iv_stop:
	/* NB: even if there are some failures, the leader should
	 * still notify all other servers to stop their local
	 * rebuild.
	 */
	if (rgt && rgt->rgt_init_scan) {
		if (myrank != pool->sp_iv_ns->iv_master_rank) {
			/* If master has been changed, then let's skip
			 * iv sync, and the new leader will take over
			 * the rebuild process anyway.
			 */
			D_DEBUG(DB_REBUILD, "rank %u != master %u\n",
				myrank, pool->sp_iv_ns->iv_master_rank);
			D_GOTO(try_reschedule, rc);
		}

		rebuild_leader_status_notify(rgt, pool, task->dst_rebuild_op, myrank);
	}

try_reschedule:
	rebuild_task_complete_schedule(task, pool, rgt, rc);
output:
	rc = rebuild_notify_ras_end(&task->dst_pool_uuid, task->dst_map_ver,
				    RB_OP_STR(task->dst_rebuild_op),
				    rc);
	if (rc)
		D_ERROR(DF_UUID": failed to send RAS event\n",
			DP_UUID(task->dst_pool_uuid));

out_pool:
	ds_pool_put(pool);
	if (rgt) {
		ABT_mutex_lock(rgt->rgt_lock);
		ABT_cond_signal(rgt->rgt_done_cond);
		ABT_mutex_unlock(rgt->rgt_lock);
		rgt_put(rgt);
	}

out_task:
	rebuild_task_destroy(task);
	rebuild_gst.rg_inflight--;

	return;
}

bool
pool_is_rebuilding(uuid_t pool_uuid)
{
	struct rebuild_task *task;

	d_list_for_each_entry(task, &rebuild_gst.rg_running_list, dst_list) {
		if (uuid_compare(task->dst_pool_uuid, pool_uuid) == 0)
			return true;
	}
	return false;
}

#define REBUILD_MAX_INFLIGHT	10
static void
rebuild_ults(void *arg)
{
	struct rebuild_task	*task;
	struct rebuild_task	*task_tmp;
	int			 rc;

	while (DAOS_FAIL_CHECK(DAOS_REBUILD_HANG))
		ABT_thread_yield();

	while (!d_list_empty(&rebuild_gst.rg_queue_list) ||
	       !d_list_empty(&rebuild_gst.rg_running_list)) {
		if (rebuild_gst.rg_abort) {
			D_DEBUG(DB_REBUILD, "abort rebuild\n");
			break;
		}

		if (d_list_empty(&rebuild_gst.rg_queue_list) ||
		    rebuild_gst.rg_inflight >= REBUILD_MAX_INFLIGHT) {
			D_DEBUG(DB_REBUILD, "in-flight rebuild %u\n",
				rebuild_gst.rg_inflight);
			dss_sleep(5000);
			continue;
		}

		d_list_for_each_entry_safe(task, task_tmp,
				      &rebuild_gst.rg_queue_list, dst_list) {
			/* If a pool is already handling a rebuild operation,
			 * wait to start the next operation until the current
			 * one completes
			 */
			if (pool_is_rebuilding(task->dst_pool_uuid))
				continue;

			rc = dss_ult_create(rebuild_task_ult, task,
					    DSS_XS_SELF, 0, DSS_DEEP_STACK_SZ, NULL);
			if (rc == 0) {
				rebuild_gst.rg_inflight++;
				/* TODO: This needs to be expanded to select the
				 * highest-priority task based on rebuild op,
				 * rather than just the next one in queue
				 */
				d_list_move(&task->dst_list,
					       &rebuild_gst.rg_running_list);
			} else {
				D_ERROR(DF_UUID" create ult failed: "DF_RC"\n",
					DP_UUID(task->dst_pool_uuid),
					DP_RC(rc));
			}
		}
		ABT_thread_yield();
	}

	/* If there are still rebuild task in queue and running list, then
	 * it is forced abort, let's delete the queue_list task, but leave
	 * the running task there, either the new leader will tell these
	 * running rebuild to update their leader or just abort the rebuild
	 * task.
	 */
	d_list_for_each_entry_safe(task, task_tmp, &rebuild_gst.rg_queue_list,
				   dst_list)
		rebuild_task_destroy(task);

	ABT_mutex_lock(rebuild_gst.rg_lock);
	ABT_cond_signal(rebuild_gst.rg_stop_cond);
	rebuild_gst.rg_rebuild_running = 0;
	ABT_mutex_unlock(rebuild_gst.rg_lock);
}

void
ds_rebuild_abort(uuid_t pool_uuid, unsigned int ver, unsigned int gen, uint64_t term)
{
	struct rebuild_tgt_pool_tracker *rpt;
	struct rebuild_tgt_pool_tracker	*tmp;

	rebuild_leader_stop(pool_uuid, ver, gen, term);

	/* Only stream 0 will access the list */
	while(1) {
		bool aborted = true;

		d_list_for_each_entry_safe(rpt, tmp, &rebuild_gst.rg_tgt_tracker_list, rt_list) {
			if (uuid_compare(rpt->rt_pool_uuid, pool_uuid) == 0 &&
			    (ver == (unsigned int)(-1) || rpt->rt_rebuild_ver == ver) &&
			    (gen == (unsigned int)(-1) || rpt->rt_rebuild_gen == gen) &&
			    (term == (uint64_t)(-1) || rpt->rt_leader_term == term)) {
				rpt->rt_abort = 1;
				aborted = false;
			}
		}

		if (aborted)
			break;

		dss_sleep(1000);
		D_INFO(DF_UUID" wait for rebuild abort.\n", DP_UUID(pool_uuid));
	}
	D_INFO(DF_UUID" rebuild aborted\n", DP_UUID(pool_uuid));
}

static void
rgt_leader_stop(struct rebuild_global_pool_tracker *rgt)
{
	rgt_get(rgt);
	D_DEBUG(DB_REBUILD, "try abort rebuild "DF_UUID" version %d\n",
		DP_UUID(rgt->rgt_pool_uuid), rgt->rgt_rebuild_ver);
	rgt->rgt_abort = 1;

	/* Remove it from the rgt list to avoid stopping rgt duplicately */
	d_list_del(&rgt->rgt_list);

	ABT_mutex_lock(rgt->rgt_lock);
	ABT_cond_wait(rgt->rgt_done_cond, rgt->rgt_lock);
	ABT_mutex_unlock(rgt->rgt_lock);

	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID"/ %d is stopped.\n",
		DP_UUID(rgt->rgt_pool_uuid), rgt->rgt_rebuild_ver);

	rgt_put(rgt);
}

/* If this is called on non-leader node, it will do nothing */
void
rebuild_leader_stop(const uuid_t pool_uuid, unsigned int ver, unsigned int gen,
		    uint64_t term)
{
	struct rebuild_global_pool_tracker	*rgt;
	struct rebuild_global_pool_tracker	*rgt_tmp;
	struct rebuild_task			*task;
	struct rebuild_task			*task_tmp;

	/* Remove the rebuild tasks from queue list */
	d_list_for_each_entry_safe(task, task_tmp, &rebuild_gst.rg_queue_list,
				   dst_list) {
		if (uuid_compare(task->dst_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) || task->dst_map_ver == ver))
			rebuild_task_destroy(task);
	}

	/* Only stream 0 will access the list */
	d_list_for_each_entry_safe(rgt, rgt_tmp, &rebuild_gst.rg_global_tracker_list,
				   rgt_list) {
		if (uuid_compare(rgt->rgt_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) || rgt->rgt_rebuild_ver == ver) &&
		    (gen == (unsigned int)(-1) || rgt->rgt_rebuild_gen == gen) &&
		    (term == (uint64_t)(-1) || rgt->rgt_leader_term == term))
			rgt_leader_stop(rgt);
	}
}

void
ds_rebuild_leader_stop_all()
{
	ABT_mutex_lock(rebuild_gst.rg_lock);
	if (!rebuild_gst.rg_rebuild_running) {
		ABT_mutex_unlock(rebuild_gst.rg_lock);
		return;
	}

	/* This will eliminate all of the queued rebuild task, then abort all
	 * running rebuild. Note: this only abort the rebuild tracking ULT
	 * (rebuild_task_ult), and the real rebuild process on each target
	 * triggered by scan/object request are still running. Once the new
	 * leader is elected, it will send those rebuild trigger req with new
	 * term, then each target will only need update its leader information
	 * and report the rebuild status to the new leader.
	 * If the new leader never comes, then those rebuild process can still
	 * finish, but those tracking ULT (rebuild_tgt_status_check_ult) will
	 * keep sending the status report to the stale leader, until it is
	 * aborted.
	 */
	D_DEBUG(DB_REBUILD, "abort rebuild %p\n", &rebuild_gst);
	rebuild_gst.rg_abort = 1;
	if (rebuild_gst.rg_rebuild_running)
		ABT_cond_wait(rebuild_gst.rg_stop_cond,
			      rebuild_gst.rg_lock);
	ABT_mutex_unlock(rebuild_gst.rg_lock);
	if (rebuild_gst.rg_stop_cond)
		ABT_cond_free(&rebuild_gst.rg_stop_cond);
}

static void
rebuild_print_list_update(const uuid_t uuid, const uint32_t map_ver,
			  daos_rebuild_opc_t rebuild_op,
			  struct pool_target_id_list *tgts) {
	int i;

	D_PRINT("%s [queued] (pool="DF_UUID" ver=%u) tgts=",
		RB_OP_STR(rebuild_op), DP_UUID(uuid), map_ver);
	for (i = 0; tgts != NULL && i < tgts->pti_number; i++) {
		if (i > 0)
			D_PRINT(",");
		D_PRINT("%u", tgts->pti_ids[i].pti_id);
	}
	D_PRINT("\n");
}

/**
 * Add rebuild task to the rebuild list and another ULT will rebuild the
 * pool.
 */
int
ds_rebuild_schedule(struct ds_pool *pool, uint32_t map_ver,
		    daos_epoch_t reclaim_eph, uint32_t layout_version,
		    struct pool_target_id_list *tgts,
		    daos_rebuild_opc_t rebuild_op, uint64_t delay_sec)
{
	struct rebuild_task	*new_task;
	struct rebuild_task	*task;
	d_list_t		*inserted_pos;
	int			rc = 0;
	uint64_t		cur_ts = 0;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	if (pool->sp_stopping) {
		D_DEBUG(DB_REBUILD, DF_UUID" is stopping,"
			"do not need schedule here\n",
			DP_UUID(pool->sp_uuid));
		return 0;
	}

	if (tgts != NULL && tgts->pti_number > 0 &&
	    rebuild_op != RB_OP_RECLAIM && rebuild_op != RB_OP_FAIL_RECLAIM) {
		/* Check if the pool already in the queue list */
		rc = rebuild_try_merge_tgts(pool, map_ver, rebuild_op, tgts);
		if (rc)
			return rc == 1 ? 0 : rc;
	}

	/* No existing task was found - allocate a new one and use it */
	D_ALLOC_PTR(new_task);
	if (new_task == NULL)
		return -DER_NOMEM;

	cur_ts = daos_gettime_coarse();

	new_task->dst_schedule_time = cur_ts + delay_sec;
	new_task->dst_map_ver = map_ver;
	new_task->dst_reclaim_ver = map_ver;
	new_task->dst_rebuild_op = rebuild_op;
	new_task->dst_reclaim_eph = reclaim_eph;
	new_task->dst_new_layout_version = layout_version;
	uuid_copy(new_task->dst_pool_uuid, pool->sp_uuid);
	D_INIT_LIST_HEAD(&new_task->dst_list);

	if (tgts != NULL && tgts->pti_number > 0) {
		rc = pool_target_id_list_merge(&new_task->dst_tgts, tgts);
		if (rc)
			D_GOTO(free, rc);

		rebuild_print_list_update(pool->sp_uuid, map_ver, rebuild_op, tgts);
		new_task->dst_reclaim_ver = min(new_task->dst_map_ver,
						rebuild_task_get_min_version(pool->sp_map, tgts));
	}
	/* Insert the task into the queue by order to make sure the rebuild
	 * task with smaller version are being executed first.
	 */
	inserted_pos = &rebuild_gst.rg_queue_list;
	d_list_for_each_entry(task, &rebuild_gst.rg_queue_list, dst_list) {
		if (uuid_compare(task->dst_pool_uuid,
				 new_task->dst_pool_uuid) != 0)
			continue;

		if (new_task->dst_map_ver > task->dst_map_ver)
			continue;

		/* Reclaim task should always be put to the first, i.e. executed
		 * before any other tasks with same version.
		 */
		if (new_task->dst_rebuild_op != RB_OP_RECLAIM &&
		    new_task->dst_rebuild_op != RB_OP_FAIL_RECLAIM &&
		    new_task->dst_map_ver == task->dst_map_ver)
			continue;

		inserted_pos = &task->dst_list;
		break;
	}
	d_list_add_tail(&new_task->dst_list, inserted_pos);

	/* Print out the current queue to the debug log */
	rebuild_debug_print_queue();

	if (!rebuild_gst.rg_rebuild_running) {
		rc = ABT_cond_create(&rebuild_gst.rg_stop_cond);
		if (rc != ABT_SUCCESS)
			D_GOTO(free, rc = dss_abterr2der(rc));

		D_DEBUG(DB_REBUILD, "rebuild ult "DF_UUID" ver=%u/%u, op=%s",
			DP_UUID(pool->sp_uuid), map_ver, new_task->dst_reclaim_ver,
			RB_OP_STR(rebuild_op));
		rebuild_gst.rg_rebuild_running = 1;
		rc = dss_ult_create(rebuild_ults, NULL, DSS_XS_SELF,
				    0, 0, NULL);
		if (rc) {
			ABT_cond_free(&rebuild_gst.rg_stop_cond);
			rebuild_gst.rg_rebuild_running = 0;
			D_GOTO(free, rc);
		}
	}
free:
	if (rc)
		rebuild_task_destroy(new_task);
	return rc;
}

static int
regenerate_task_internal(struct ds_pool *pool, struct pool_target *tgts,
			 unsigned int tgts_cnt, daos_rebuild_opc_t rebuild_op)
{
	daos_epoch_t	eph = d_hlc_get();
	daos_epoch_t	current_eph;
	unsigned int	i;
	int		rc;

	/* If this rebuild task schedule is due to PS leader switch, then let's
	 * use the stable epoch from current running rebuild task.
	 */
	ds_rebuild_running_query(pool->sp_uuid, NULL, &current_eph, NULL);
	for (i = 0; i < tgts_cnt; i++) {
		struct pool_target		*tgt = &tgts[i];
		struct pool_target_id		tgt_id;
		struct pool_target_id_list	id_list;

		tgt_id.pti_id = tgt->ta_comp.co_id;
		id_list.pti_ids = &tgt_id;
		id_list.pti_number = 1;

		if (rebuild_op == RB_OP_EXCLUDE || rebuild_op == RB_OP_DRAIN) {
			rc = ds_rebuild_schedule(pool, tgt->ta_comp.co_fseq,
						 current_eph == 0 ? eph : current_eph,
						 0, &id_list, rebuild_op, 0);
		} else {
			daos_rebuild_opc_t new_op = rebuild_op;

			D_ASSERT(new_op == RB_OP_REINT);
			/* For new target, let's turn to EXTEND command to match
			 * the original action.
			 */
			if (tgt->ta_comp.co_fseq <= 1)
				new_op = RB_OP_EXTEND;

			rc = ds_rebuild_schedule(pool, tgt->ta_comp.co_in_ver,
						 current_eph == 0 ? eph : current_eph,
						 0, &id_list, new_op, 0);
		}
		if (rc) {
			D_ERROR(DF_UUID" schedule op %d ver %d failed: "
				DF_RC"\n",
				DP_UUID(pool->sp_uuid), rebuild_op,
				tgt->ta_comp.co_fseq, DP_RC(rc));
			return rc;
		}
	}

	return DER_SUCCESS;
}

static int
regenerate_task_of_type(struct ds_pool *pool, pool_comp_state_t match_states,
			daos_rebuild_opc_t rebuild_op)
{
	struct pool_target	*tgts;
	unsigned int		tgts_cnt;
	int			rc;

	rc = pool_map_find_tgts_by_state(pool->sp_map, match_states,
					 &tgts, &tgts_cnt);
	if (rc != 0) {
		D_ERROR("failed to create %s tgt_list: "DF_RC"\n",
			RB_OP_STR(rebuild_op), DP_RC(rc));
		return rc;
	}

	return regenerate_task_internal(pool, tgts, tgts_cnt, rebuild_op);
}


/* Regenerate the rebuild tasks when changing the leader. */
int
ds_rebuild_regenerate_task(struct ds_pool *pool, daos_prop_t *prop)
{
	struct daos_prop_entry	*entry;
	int			rc = 0;

	rebuild_gst.rg_abort = 0;

	if (pool->sp_reint_mode == DAOS_REINT_MODE_NO_DATA_SYNC) {
		D_DEBUG(DB_REBUILD, DF_UUID" No data sync for reintegration\n",
			DP_UUID(pool->sp_uuid));
		return DER_SUCCESS;
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SELF_HEAL);
	D_ASSERT(entry != NULL);
	if (entry->dpe_val & DAOS_SELF_HEAL_AUTO_REBUILD) {
		rc = regenerate_task_of_type(pool, PO_COMP_ST_DOWN, RB_OP_EXCLUDE);
		if (rc != 0)
			return rc;

		rc = regenerate_task_of_type(pool, PO_COMP_ST_DRAIN, RB_OP_DRAIN);
		if (rc != 0)
			return rc;
	} else {
		D_DEBUG(DB_REBUILD, DF_UUID" self healing is disabled\n",
			DP_UUID(pool->sp_uuid));
	}

	/* NB: some of the extending job might be regenerate as reintegrate
	 * job here, but it is ok, since the only difference between reintegrate
	 * and extend job would be
	 * 1. extend job needs to add new targets to the pool map.
	 * 2. reintegrate job needs to discard the existing objects/records on the
	 *    reintegrating targets.
	 * But since the pool map already includs these extending targets, and also
	 * discarding on an empty targets is harmless. So it is ok to use REINT to
	 * do EXTEND here.
	 */
	rc = regenerate_task_of_type(pool, PO_COMP_ST_UP, RB_OP_REINT);
	if (rc != 0)
		return rc;

	return DER_SUCCESS;
}

static int
rebuild_fini_one(void *arg)
{
	struct rebuild_tgt_pool_tracker	*rpt = arg;
	struct rebuild_pool_tls		*pool_tls;
	struct ds_pool_child		*dpc;

	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
					   rpt->rt_rebuild_gen);
	if (pool_tls == NULL)
		return 0;

	rebuild_pool_tls_destroy(pool_tls);
	/* close the opened local ds_cont on main XS */
	D_ASSERT(dss_get_module_info()->dmi_xs_id != 0);

	dpc = ds_pool_child_lookup(rpt->rt_pool_uuid);
	D_ASSERT(dpc != NULL);

	/* Reset rebuild epoch, then reset the aggregation epoch, so
	 * it can aggregate the rebuild epoch.
	 */
	D_ASSERT(rpt->rt_rebuild_fence != 0);
	if (rpt->rt_rebuild_fence == dpc->spc_rebuild_fence) {
		dpc->spc_rebuild_fence = 0;
		dpc->spc_rebuild_end_hlc = d_hlc_get();
		D_DEBUG(DB_REBUILD, DF_UUID": Reset aggregation end hlc "
			DF_U64"\n", DP_UUID(rpt->rt_pool_uuid),
			dpc->spc_rebuild_end_hlc);
	} else {
		D_DEBUG(DB_REBUILD, DF_UUID": pool is still being rebuilt"
			" rt_rebuild_fence "DF_U64" spc_rebuild_fence "
			DF_U64"\n", DP_UUID(rpt->rt_pool_uuid),
			rpt->rt_rebuild_fence, dpc->spc_rebuild_fence);
	}

	ds_pool_child_put(dpc);

	return 0;
}

int
rebuild_tgt_fini(struct rebuild_tgt_pool_tracker *rpt)
{
	struct rebuild_pool_tls	*pool_tls;
	int			 rc;

	D_INFO("finishing rebuild for "DF_UUID", map_ver=%u refcount %u\n",
	       DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver, rpt->rt_refcount);

	D_ASSERT(rpt->rt_pool->sp_rebuilding > 0);
	rpt->rt_pool->sp_rebuilding--;

	ABT_mutex_lock(rpt->rt_lock);
	ABT_cond_signal(rpt->rt_global_dtx_wait_cond);
	D_ASSERT(rpt->rt_refcount > 0);
	rpt->rt_finishing = 1;
	/* Wait until all ult/tasks finish and release the rpt.
	 * NB: Because rebuild_tgt_fini will be only called in
	 * rebuild_tgt_status_check_ult, which will make sure when
	 * rt_refcount reaches to 1, either all rebuild is done or
	 * all ult/task has been aborted by rt_abort, i.e. no new
	 * ULT/task will be created after this check. So it is safe
	 * to destroy the rpt after this.
	 */
	if (rpt->rt_refcount > 1)
		ABT_cond_wait(rpt->rt_fini_cond, rpt->rt_lock);
	ABT_mutex_unlock(rpt->rt_lock);

	/* destroy the rebuild pool tls on XS 0 */
	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
					   rpt->rt_rebuild_gen);
	if (pool_tls != NULL)
		rebuild_pool_tls_destroy(pool_tls);

	/* close the rebuild pool/container on all main XS */
	rc = dss_task_collective(rebuild_fini_one, rpt, 0);

	/* destroy the migrate_tls of 0-xstream */
	ds_migrate_stop(rpt->rt_pool, rpt->rt_rebuild_ver, rpt->rt_rebuild_gen);
	d_list_del_init(&rpt->rt_list);
	rpt_put(rpt);
	/* No one should access rpt after rebuild_fini_one.
	 */
	D_ASSERT(rpt->rt_refcount == 0);

	D_INFO("Finalized rebuild for "DF_UUID", map_ver=%u.\n",
	       DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver);

	rpt_destroy(rpt);

	return rc;
}

void
rebuild_tgt_status_check_ult(void *arg)
{
	struct rebuild_tgt_pool_tracker	*rpt = arg;
	struct sched_req_attr	attr = { 0 };

	D_ASSERT(rpt != NULL);
	sched_req_attr_init(&attr, SCHED_REQ_MIGRATE, &rpt->rt_pool_uuid);
	rpt->rt_ult = sched_req_get(&attr, ABT_THREAD_NULL);
	if (rpt->rt_ult == NULL) {
		D_ERROR("Can not start rebuild status check\n");
		goto out;
	}

	while (1) {
		struct rebuild_iv		iv;
		struct rebuild_tgt_query_info	status;
		int				rc;

		memset(&status, 0, sizeof(status));
		rc = ABT_mutex_create(&status.lock);
		if (rc != ABT_SUCCESS)
			break;
		rc = rebuild_tgt_query(rpt, &status);
		ABT_mutex_free(&status.lock);
		if (rc || status.status != 0) {
			D_ERROR(DF_UUID" rebuild failed: "DF_RC"\n",
				DP_UUID(rpt->rt_pool_uuid),
				DP_RC(rc == 0 ? status.status : rc));
			if (status.status == 0)
				status.status = rc;
			if (rpt->rt_errno == 0)
				rpt->rt_errno = status.status;
		}

		memset(&iv, 0, sizeof(iv));
		uuid_copy(iv.riv_pool_uuid, rpt->rt_pool_uuid);

		/* rebuild_tgt_query above possibly lost some counter
		 * when target being excluded.
		 */
		if (status.obj_count < rpt->rt_reported_obj_cnt)
			status.obj_count = rpt->rt_reported_obj_cnt;
		if (status.rec_count < rpt->rt_reported_rec_cnt)
			status.rec_count = rpt->rt_reported_rec_cnt;
		if (status.size < rpt->rt_reported_size)
			status.size = rpt->rt_reported_size;
		if (status.tobe_obj_count < rpt->rt_reported_toberb_objs)
			status.tobe_obj_count = rpt->rt_reported_toberb_objs;
		if (rpt->rt_re_report) {
			iv.riv_toberb_obj_count = status.tobe_obj_count;
			iv.riv_obj_count = status.obj_count;
			iv.riv_rec_count = status.rec_count;
			iv.riv_size = status.size;
		} else {
			iv.riv_toberb_obj_count = status.tobe_obj_count -
						  rpt->rt_reported_toberb_objs;
			iv.riv_obj_count = status.obj_count -
					   rpt->rt_reported_obj_cnt;
			iv.riv_rec_count = status.rec_count -
					   rpt->rt_reported_rec_cnt;
			iv.riv_size = status.size -
					   rpt->rt_reported_size;
		}
		iv.riv_status = status.status;
		if (status.scanning == 0 || rpt->rt_abort ||
		    status.status != 0) {
			iv.riv_scan_done = 1;
			rpt->rt_scan_done = 1;
		}

		/* Only global scan is done, then pull is trustable */
		if ((rpt->rt_global_scan_done && !status.rebuilding) ||
		     rpt->rt_abort)
			iv.riv_pull_done = 1;

		/* Once the rebuild is globally done, the target
		 * does not need update the status, just finish
		 * the rebuild.
		 */
		if (!rpt->rt_global_done) {
			struct ds_iv_ns *ns = rpt->rt_pool->sp_iv_ns;

			iv.riv_master_rank = rpt->rt_leader_rank;
			iv.riv_rank = rpt->rt_rank;
			iv.riv_ver = rpt->rt_rebuild_ver;
			iv.riv_rebuild_gen = rpt->rt_rebuild_gen;
			iv.riv_leader_term = rpt->rt_leader_term;
			iv.riv_dtx_resyc_version = rpt->rt_pool->sp_dtx_resync_version;
			/* Cart does not support failure recovery yet, let's
			 * send the status to root for now. FIXME
			 */
			if (DAOS_FAIL_CHECK(DAOS_REBUILD_TGT_IV_UPDATE_FAIL))
				rc = -DER_INVAL;
			else
				rc = rebuild_iv_update(ns, &iv,
						       CRT_IV_SHORTCUT_TO_ROOT,
						       CRT_IV_SYNC_NONE, false);
			if (rc == 0) {
				if (rpt->rt_re_report) {
					rpt->rt_reported_toberb_objs =
						iv.riv_toberb_obj_count;
					rpt->rt_re_report = 0;
				} else {
					rpt->rt_reported_toberb_objs +=
						iv.riv_toberb_obj_count;
				}
				rpt->rt_reported_obj_cnt = status.obj_count;
				rpt->rt_reported_rec_cnt = status.rec_count;
				rpt->rt_reported_size = status.size;
			} else {
				D_WARN("rebuild iv update failed: %d\n", rc);
				/* Already finish rebuilt, but it can not
				 * its rebuild status on the leader, i.e.
				 * it can not find the IV see crt_iv_hdlr_xx().
				 * let's just stop the rebuild.
				 */
				if (rc == -DER_NONEXIST && !status.rebuilding)
					rpt->rt_global_done = 1;

				if (ns->iv_stop) {
					D_DEBUG(DB_REBUILD, "abort rebuild "
						DF_UUID"\n",
						DP_UUID(rpt->rt_pool_uuid));
					rpt->rt_abort = 1;
				}
			}
		}

		D_INFO(DF_UUID" ver %d gen %u obj "DF_U64" rec "DF_U64" size "
		       DF_U64" scan done %d pull done %d scan gl done %d"
		       " gl done %d status %d abort %s\n",
		       DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver,
		       rpt->rt_pool->sp_rebuild_gen,
		       iv.riv_obj_count, iv.riv_rec_count, iv.riv_size, rpt->rt_scan_done,
		       iv.riv_pull_done, rpt->rt_global_scan_done,
		       rpt->rt_global_done, iv.riv_status, rpt->rt_abort ? "yes" : "no");
		if (rpt->rt_global_done || rpt->rt_abort)
			break;

		sched_req_sleep(rpt->rt_ult, RBLD_CHECK_INTV);
	}

	sched_req_put(rpt->rt_ult);
	rpt->rt_ult = NULL;
out:
	rpt_put(rpt);
	rebuild_tgt_fini(rpt);
}

/**
 * To avoid broadcasting during pool_connect and container
 * open for rebuild, let's create a local ds_pool/ds_container
 * and dc_pool/dc_container, so rebuild client will always
 * use the specified pool_hdl/container_hdl uuid during
 * rebuild.
 */
static int
rebuild_prepare_one(void *data)
{
	struct rebuild_tgt_pool_tracker	*rpt = data;
	struct rebuild_pool_tls		*pool_tls;
	struct ds_pool_child		*dpc;
	int				 rc = 0;

	pool_tls = rebuild_pool_tls_create(rpt->rt_pool_uuid, rpt->rt_poh_uuid,
					   rpt->rt_coh_uuid, rpt->rt_rebuild_ver,
					   rpt->rt_rebuild_gen);
	if (pool_tls == NULL)
		return -DER_NOMEM;

	dpc = ds_pool_child_lookup(rpt->rt_pool_uuid);
	D_ASSERT(dpc != NULL);

	D_ASSERT(dss_get_module_info()->dmi_xs_id != 0);

	/* Set the rebuild epoch per VOS container, so VOS aggregation will not
	 * cross the epoch to cause problem.
	 */
	D_ASSERT(rpt->rt_rebuild_fence != 0);
	dpc->spc_rebuild_fence = rpt->rt_rebuild_fence;
	D_DEBUG(DB_REBUILD, "open local container "DF_UUID"/"DF_UUID
		" rebuild eph "DF_X64" "DF_RC"\n", DP_UUID(rpt->rt_pool_uuid),
		DP_UUID(rpt->rt_coh_uuid), rpt->rt_rebuild_fence, DP_RC(rc));

	ds_pool_child_put(dpc);

	return rc;
}

static int
rpt_create(struct ds_pool *pool, uint32_t master_rank, uint32_t pm_ver,
	   uint64_t leader_term, uint32_t rebuild_gen, uint32_t layout_ver,
	   uint64_t reclaim_epoch, uint32_t tgts_num,
	   struct rebuild_tgt_pool_tracker **p_rpt)
{
	struct rebuild_tgt_pool_tracker	*rpt;
	d_rank_t	rank;
	int		rc;

	D_ALLOC_PTR(rpt);
	if (rpt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&rpt->rt_list);
	rc = ABT_mutex_create(&rpt->rt_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(free, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&rpt->rt_fini_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(free, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&rpt->rt_global_dtx_wait_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(free, rc = dss_abterr2der(rc));

	uuid_copy(rpt->rt_pool_uuid, pool->sp_uuid);
	rpt->rt_reported_toberb_objs = 0;
	rpt->rt_reported_obj_cnt = 0;
	rpt->rt_reported_rec_cnt = 0;
	rpt->rt_reported_size = 0;
	rpt->rt_rebuild_ver = pm_ver;
	rpt->rt_new_layout_ver = layout_ver;
	rpt->rt_leader_term = leader_term;
	rpt->rt_rebuild_gen = rebuild_gen;
	rpt->rt_tgts_num = tgts_num;
	rpt->rt_reclaim_epoch = reclaim_epoch;
	crt_group_rank(pool->sp_group, &rank);
	rpt->rt_rank = rank;
	rpt->rt_leader_rank = master_rank;

	rpt->rt_refcount = 1;
	*p_rpt = rpt;
free:
	if (rc != 0)
		rpt_destroy(rpt);
	return rc;
}

/* rebuild prepare on each target, which will be called after
 * each target get the scan rpc from the master.
 */
int
rebuild_tgt_prepare(crt_rpc_t *rpc, struct rebuild_tgt_pool_tracker **p_rpt)
{
	struct rebuild_scan_in		*rsi = crt_req_get(rpc);
	struct ds_pool			*pool;
	struct rebuild_tgt_pool_tracker	*rpt = NULL;
	struct rebuild_pool_tls		*pool_tls;
	daos_prop_t			prop = { 0 };
	struct daos_prop_entry		*entry;
	uuid_t				cont_uuid;
	int				rc;

	D_DEBUG(DB_REBUILD, "prepare rebuild for "DF_UUID"/%d\n",
		DP_UUID(rsi->rsi_pool_uuid), rsi->rsi_rebuild_ver);

	rc = ds_pool_lookup(rsi->rsi_pool_uuid, &pool);
	if (rc) {
		D_ERROR("Can not find pool "DF_UUID": %d\n", DP_UUID(rsi->rsi_pool_uuid), rc);
		return rc;
	}

	if (ds_pool_get_version(pool) < rsi->rsi_rebuild_ver) {
		D_INFO(DF_UUID" map %u < rsi_rebuild_ver %u\n",
		       DP_UUID(rsi->rsi_pool_uuid), ds_pool_get_version(pool),
		       rsi->rsi_rebuild_ver);
		D_GOTO(out, rc = -DER_BUSY);
	}

	D_ASSERT(pool->sp_group != NULL);
	D_ASSERT(pool->sp_iv_ns != NULL);

	/* Let's invalidate local snapshot cache before
	 * rebuild, so to make sure rebuild will use the updated
	 * snapshot during rebuild fetch, otherwise it may cause
	 * corruption.
	 */
	uuid_clear(cont_uuid);
	rc = ds_cont_revoke_snaps(pool->sp_iv_ns, cont_uuid,
				  CRT_IV_SHORTCUT_NONE,
				  CRT_IV_SYNC_NONE);
	if (rc)
		D_GOTO(out, rc);

	/* Create rpt for the target */
	rc = rpt_create(pool, rsi->rsi_master_rank, rsi->rsi_rebuild_ver,
			rsi->rsi_leader_term, rsi->rsi_rebuild_gen,
			rsi->rsi_layout_ver, rsi->rsi_reclaim_epoch,
			rsi->rsi_tgts_num, &rpt);
	if (rc)
		D_GOTO(out, rc);

	rpt->rt_rebuild_op = rsi->rsi_rebuild_op;

	rc = ds_pool_iv_srv_hdl_fetch(pool, &rpt->rt_poh_uuid,
				      &rpt->rt_coh_uuid);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_REBUILD, "rebuild coh/poh "DF_UUID"/"DF_UUID"\n",
		DP_UUID(rpt->rt_coh_uuid), DP_UUID(rpt->rt_poh_uuid));

	ds_pool_iv_ns_update(pool, rsi->rsi_master_rank, rsi->rsi_leader_term);

	rc = ds_pool_iv_prop_fetch(pool, &prop);
	if (rc)
		D_GOTO(out, rc);

	entry = daos_prop_entry_get(&prop, DAOS_PROP_PO_SVC_LIST);
	D_ASSERT(entry != NULL);
	rc = daos_rank_list_dup(&rpt->rt_svc_list,
				(d_rank_list_t *)entry->dpe_val_ptr);
	if (rc)
		D_GOTO(out, rc);

	pool_tls = rebuild_pool_tls_create(rpt->rt_pool_uuid, rpt->rt_poh_uuid,
					   rpt->rt_coh_uuid, rpt->rt_rebuild_ver,
					   rpt->rt_rebuild_gen);
	if (pool_tls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpt->rt_rebuild_fence = d_hlc_get();
	rc = dss_task_collective(rebuild_prepare_one, rpt, 0);
	if (rc) {
		rpt->rt_rebuild_fence = 0;
		rebuild_pool_tls_destroy(pool_tls);
		D_GOTO(out, rc);
	}

	ABT_mutex_lock(rpt->rt_lock);
	rpt->rt_pool = pool; /* pin it */
	ABT_mutex_unlock(rpt->rt_lock);

	rpt_get(rpt);
	d_list_add(&rpt->rt_list, &rebuild_gst.rg_tgt_tracker_list);
	*p_rpt = rpt;
out:
	if (rc) {
		if (rpt)
			rpt_put(rpt);

		ds_pool_put(pool);
	}
	daos_prop_fini(&prop);

	return rc;
}

static struct crt_corpc_ops rebuild_tgt_scan_co_ops = {
	.co_aggregate	= rebuild_tgt_scan_aggregator,
};

/* Define for cont_rpcs[] array population below.
 * See REBUILD_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler rebuild_handlers[] = {
	REBUILD_PROTO_SRV_RPC_LIST,
};

#undef X

struct dss_module_key rebuild_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = rebuild_tls_init,
	.dmk_fini = rebuild_tls_fini,
};

static int
init(void)
{
	int rc;

	D_INIT_LIST_HEAD(&rebuild_gst.rg_tgt_tracker_list);
	D_INIT_LIST_HEAD(&rebuild_gst.rg_global_tracker_list);
	D_INIT_LIST_HEAD(&rebuild_gst.rg_completed_list);
	D_INIT_LIST_HEAD(&rebuild_gst.rg_queue_list);
	D_INIT_LIST_HEAD(&rebuild_gst.rg_running_list);

	rc = ABT_mutex_create(&rebuild_gst.rg_lock);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	rc = rebuild_iv_init();
	return rc;
}

static int
fini(void)
{
	rebuild_status_completed_remove(NULL);

	if (rebuild_gst.rg_stop_cond)
		ABT_cond_free(&rebuild_gst.rg_stop_cond);

	ABT_mutex_free(&rebuild_gst.rg_lock);

	rebuild_iv_fini();
	return 0;
}

static int
rebuild_cleanup(void)
{
	/* stop all rebuild process */
	ds_rebuild_leader_stop_all();
	return 0;
}

struct dss_module rebuild_module = {
    .sm_name        = "rebuild",
    .sm_mod_id      = DAOS_REBUILD_MODULE,
    .sm_ver         = DAOS_REBUILD_VERSION,
    .sm_proto_count = 1,
    .sm_init        = init,
    .sm_fini        = fini,
    .sm_cleanup     = rebuild_cleanup,
    .sm_proto_fmt   = {&rebuild_proto_fmt},
    .sm_cli_count   = {0},
    .sm_handlers    = {rebuild_handlers},
    .sm_key         = &rebuild_module_key,
};
