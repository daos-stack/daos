/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * rebuild: rebuild service
 *
 * Rebuild service module api.
 */
#define D_LOGFAC	DD_FAC(rebuild)

#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/iv.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/security.h>
#include <daos_mgmt.h>
#include "rpc.h"
#include "rebuild_internal.h"

#define RBLD_CHECK_INTV	 (2 * NSEC_PER_SEC)	/* seconds interval to check*/
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
rebuild_pool_tls_lookup(uuid_t pool_uuid, unsigned int ver)
{
	struct rebuild_tls *tls = rebuild_tls_get();
	struct rebuild_pool_tls *pool_tls;
	struct rebuild_pool_tls *found = NULL;

	D_ASSERT(tls != NULL);
	/* Only 1 thread will access the list, no need lock */
	d_list_for_each_entry(pool_tls, &tls->rebuild_pool_list,
			      rebuild_pool_list) {
		if (uuid_compare(pool_tls->rebuild_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) ||
		     ver == pool_tls->rebuild_pool_ver)) {
			found = pool_tls;
			break;
		}
	}

	return found;
}

static struct rebuild_pool_tls *
rebuild_pool_tls_create(uuid_t pool_uuid, uuid_t poh_uuid, uuid_t coh_uuid,
			unsigned int ver)
{
	struct rebuild_pool_tls *rebuild_pool_tls;
	struct rebuild_tls *tls = rebuild_tls_get();

	rebuild_pool_tls = rebuild_pool_tls_lookup(pool_uuid, ver);
	D_ASSERT(rebuild_pool_tls == NULL);

	D_ALLOC_PTR(rebuild_pool_tls);
	if (rebuild_pool_tls == NULL)
		return NULL;

	rebuild_pool_tls->rebuild_pool_ver = ver;
	uuid_copy(rebuild_pool_tls->rebuild_pool_uuid, pool_uuid);
	uuid_copy(rebuild_pool_tls->rebuild_poh_uuid, poh_uuid);
	uuid_copy(rebuild_pool_tls->rebuild_coh_uuid, coh_uuid);
	rebuild_pool_tls->rebuild_pool_scanning = 1;
	rebuild_pool_tls->rebuild_pool_scan_done = 0;
	rebuild_pool_tls->rebuild_pool_obj_count = 0;
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
	if (!daos_handle_is_inval(tls->rebuild_tree_hdl))
		obj_tree_destroy(tls->rebuild_tree_hdl);
	d_list_del(&tls->rebuild_pool_list);
	D_FREE(tls);
}

static void *
rebuild_tls_init(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key)
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
			  d_rank_t rank, unsigned flags)
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

	D_ASSERTF(status != NULL, "Can not find rank %u\n", rank);
	if (flags & SCAN_DONE)
		status->scan_done = 1;
	if (flags & PULL_DONE)
		status->pull_done = 1;
}

struct rebuild_tgt_pool_tracker *
rpt_lookup(uuid_t pool_uuid, unsigned int ver)
{
	struct rebuild_tgt_pool_tracker	*rpt;
	struct rebuild_tgt_pool_tracker	*found = NULL;

	/* Only stream 0 will access the list */
	d_list_for_each_entry(rpt, &rebuild_gst.rg_tgt_tracker_list, rt_list) {
		if (uuid_compare(rpt->rt_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) || rpt->rt_rebuild_ver == ver)) {
			rpt_get(rpt);
			found = rpt;
			break;
		}
	}

	return found;
}

struct rebuild_global_pool_tracker *
rebuild_global_pool_tracker_lookup(const uuid_t pool_uuid, unsigned int ver)
{
	struct rebuild_global_pool_tracker	*rgt;
	struct rebuild_global_pool_tracker	*found = NULL;

	/* Only stream 0 will access the list */
	d_list_for_each_entry(rgt, &rebuild_gst.rg_global_tracker_list,
			      rgt_list) {
		if (uuid_compare(rgt->rgt_pool_uuid, pool_uuid) == 0 &&
		    (ver == (unsigned int)(-1) ||
		     rgt->rgt_rebuild_ver == ver)) {
			found = rgt;
			break;
		}
	}

	return found;
}

int
rebuild_global_status_update(struct rebuild_global_pool_tracker *rgt,
			     struct rebuild_iv *iv)
{
	D_DEBUG(DB_REBUILD, "iv rank %d scan_done %d pull_done %d\n",
		iv->riv_rank, iv->riv_scan_done, iv->riv_pull_done);

	if (!iv->riv_scan_done)
		return 0;

	if (!is_rebuild_global_scan_done(rgt)) {
		rebuild_leader_set_status(rgt, iv->riv_rank, SCAN_DONE);
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
		rebuild_leader_set_status(rgt, iv->riv_rank, PULL_DONE);
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

int
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
rebuild_tls_fini(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key, void *data)
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

	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
					   rpt->rt_rebuild_ver);
	D_ASSERTF(pool_tls != NULL, DF_UUID" ver %d\n",
		   DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver);

	D_DEBUG(DB_REBUILD, "%d scanning %d status %d\n", idx,
		pool_tls->rebuild_pool_scanning, pool_tls->rebuild_pool_status);

	ABT_mutex_lock(status->lock);
	if (pool_tls->rebuild_pool_scanning)
		status->scanning = 1;
	if (pool_tls->rebuild_pool_status != 0 && status->status == 0)
		status->status = pool_tls->rebuild_pool_status;

	status->tobe_obj_count += pool_tls->rebuild_pool_obj_count;
	ABT_mutex_unlock(status->lock);

	return 0;
}

static int
rebuild_tgt_query(struct rebuild_tgt_pool_tracker *rpt,
		  struct rebuild_tgt_query_info *status)
{
	struct ds_migrate_status	dms = { 0 };
	struct rebuild_tgt_query_arg	arg;
	int				rc;

	arg.rpt = rpt;
	arg.status = status;

	rc = ds_migrate_query_status(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
				     &dms);
	if (rc)
		D_GOTO(out, rc);

	/* let's check scanning status on every thread*/
	ABT_mutex_lock(rpt->rt_lock);
	rc = dss_thread_collective(dss_rebuild_check_one, &arg, 0,
				   DSS_ULT_REBUILD);
	if (rc) {
		ABT_mutex_unlock(rpt->rt_lock);
		D_GOTO(out, rc);
	}

	status->obj_count = dms.dm_obj_count;
	status->rec_count = dms.dm_rec_count;
	status->size = dms.dm_total_size;
	if (status->scanning || dms.dm_migrating)
		status->rebuilding = true;
	else
		status->rebuilding = false;
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

/* TODO: Add something about what the current operation is for output status */
int
ds_rebuild_query(uuid_t pool_uuid, struct daos_rebuild_status *status)
{
	struct rebuild_global_pool_tracker	*rgt;
	struct daos_rebuild_status		*rs_inlist;
	int					rc = 0;

	memset(status, 0, sizeof(*status));

	rgt = rebuild_global_pool_tracker_lookup(pool_uuid, -1);
	if (rgt == NULL) {
		rs_inlist = rebuild_status_completed_lookup(pool_uuid);
		if (rs_inlist != NULL)
			memcpy(status, rs_inlist, sizeof(*status));
		else
			status->rs_done = 1;
	} else {
		memcpy(status, &rgt->rgt_status, sizeof(*status));
		status->rs_version = rgt->rgt_rebuild_ver;
	}

	/* If there are still rebuild task queued for the pool, let's reset
	 * the done status.
	 */
	if (status->rs_done == 1 &&
	    !d_list_empty(&rebuild_gst.rg_queue_list)) {
		struct rebuild_task *task;

		d_list_for_each_entry(task, &rebuild_gst.rg_queue_list,
				      dst_list) {
			if (uuid_compare(task->dst_pool_uuid, pool_uuid) == 0) {
				status->rs_done = 0;
				break;
			}
		}
	}

	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID" done %s rec "DF_U64" obj "
		DF_U64" ver %d err %d\n", DP_UUID(pool_uuid),
		status->rs_done ? "yes" : "no", status->rs_rec_nr,
		status->rs_obj_nr, status->rs_version, status->rs_errno);

	return rc;
}

#define RBLD_SBUF_LEN	256

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
rebuild_leader_status_check(struct ds_pool *pool, uint32_t map_ver,
			    struct rebuild_global_pool_tracker *rgt)
{
	double		last_print = 0;
	unsigned int	total;
	int		rc;

	rc = crt_group_size(pool->sp_group, &total);
	if (rc)
		return;

	rgt->rgt_ult = dss_sleep_ult_create();
	if (rgt->rgt_ult == NULL)
		return;

	while (1) {
		struct daos_rebuild_status	*rs = &rgt->rgt_status;
		struct pool_target		*targets;
		char				sbuf[RBLD_SBUF_LEN];
		unsigned int			failed_tgts_cnt;
		double				now;
		char				*str;

		rc = pool_map_find_failed_tgts(pool->sp_map, &targets,
					       &failed_tgts_cnt);
		if (rc != 0) {
			D_ERROR("failed to create failed tgt list rc %d\n",
				rc);
			return;
		}

		if (targets != NULL) {
			struct pool_domain *dom;
			int i;

			for (i = 0; i < failed_tgts_cnt; i++) {
				dom = pool_map_find_node_by_rank(pool->sp_map,
						targets[i].ta_comp.co_rank);

				D_ASSERT(dom != NULL);
				D_DEBUG(DB_REBUILD, "target %d failed\n",
					dom->do_comp.co_rank);
				if (pool_component_unavail(&dom->do_comp,
							false)) {
					rebuild_leader_set_status(rgt,
						dom->do_comp.co_rank,
						SCAN_DONE|PULL_DONE);
				}
			}
			D_FREE(targets);
		}

		if (!rgt->rgt_abort &&
		    ((!is_rebuild_global_pull_done(rgt) &&
		      is_rebuild_global_scan_done(rgt)) ||
		      !rgt->rgt_notify_stable_epoch)) {
			struct rebuild_iv iv = { 0 };

			D_ASSERT(rgt->rgt_stable_epoch != 0);
			uuid_copy(iv.riv_pool_uuid, rgt->rgt_pool_uuid);
			iv.riv_master_rank = pool->sp_iv_ns->iv_master_rank;
			iv.riv_global_scan_done =
					is_rebuild_global_scan_done(rgt);
			iv.riv_stable_epoch = rgt->rgt_stable_epoch;
			iv.riv_ver = rgt->rgt_rebuild_ver;
			iv.riv_leader_term = rgt->rgt_leader_term;

			/* Notify others the global scan is done, then
			 * each target can reliablly report its pull status
			 */
			rc = rebuild_iv_update(pool->sp_iv_ns,
					       &iv, CRT_IV_SHORTCUT_NONE,
					       CRT_IV_SYNC_LAZY, false);
			if (rc) {
				D_WARN("rebuild master iv update failed: %d\n",
				       rc);
			} else {
				/* Each server uses IV to notify the leader
				 * its rebuild stable epoch, then the leader
				 * will choose the largest epoch as the global
				 * stable epoch to rebuild.
				 */
				rgt->rgt_notify_stable_epoch = 1;
			}
		}

		/* query the current rebuild status */
		if (is_rebuild_global_done(rgt))
			rs->rs_done = 1;

		if (rs->rs_done)
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
			"Rebuild [%s] (pool "DF_UUID" ver=%u, toberb_obj="
			DF_U64", rb_obj="DF_U64", rec="DF_U64", size="DF_U64
			" done %d status %d/%d duration=%d secs)\n",
			str, DP_UUID(pool->sp_uuid), map_ver,
			rs->rs_toberb_obj_nr, rs->rs_obj_nr, rs->rs_rec_nr,
			rs->rs_size, rs->rs_done, rs->rs_errno,
			rs->rs_fail_rank, rs->rs_seconds);

		D_DEBUG(DB_REBUILD, "%s", sbuf);
		if (rs->rs_done || rebuild_gst.rg_abort || rgt->rgt_abort) {
			D_PRINT("%s", sbuf);
			break;
		}

		now = ABT_get_wtime();
		/* print something at least for each 10 secons */
		if (now - last_print > 10) {
			last_print = now;
			D_PRINT("%s", sbuf);
		}

		dss_ult_sleep(rgt->rgt_ult, RBLD_CHECK_INTV);
	}

	dss_sleep_ult_destroy(rgt->rgt_ult);
	rgt->rgt_ult = NULL;
}

static void
rebuild_global_pool_tracker_destroy(struct rebuild_global_pool_tracker *rgt)
{
	d_list_del(&rgt->rgt_list);
	if (rgt->rgt_servers)
		D_FREE(rgt->rgt_servers);

	D_FREE(rgt);
}

static int
rebuild_global_pool_tracker_create(struct ds_pool *pool, uint32_t ver,
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

	for (i = 0; i < node_nr; i++)
		rgt->rgt_servers[i].rank = doms[i].do_comp.co_rank;
	rgt->rgt_servers_number = node_nr;

	uuid_copy(rgt->rgt_pool_uuid, pool->sp_uuid);
	rgt->rgt_rebuild_ver = ver;
	rgt->rgt_status.rs_version = ver;
	d_list_add(&rgt->rgt_list, &rebuild_gst.rg_global_tracker_list);
	*p_rgt = rgt;
out:
	if (rc)
		rebuild_global_pool_tracker_destroy(rgt);
	return rc;
}

/* To notify all targets to prepare the rebuild */
static int
rebuild_prepare(struct ds_pool *pool, uint32_t rebuild_ver,
		uint64_t leader_term,
		struct pool_target_id_list *tgts,
		daos_rebuild_opc_t rebuild_op,
		struct rebuild_global_pool_tracker **rgt)
{
	pool_comp_state_t	match_status;
	unsigned int		master_rank;
	int			rc;

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" create rebuild iv, op=%s\n",
		DP_UUID(pool->sp_uuid), RB_OP_STR(rebuild_op));

	/* Update pool iv ns for the pool */
	crt_group_rank(pool->sp_group, &master_rank);
	ds_pool_iv_ns_update(pool, master_rank);

	rc = rebuild_global_pool_tracker_create(pool, rebuild_ver, rgt);
	if (rc) {
		D_ERROR("rebuild_global_pool_tracker create failed: rc %d\n",
			rc);
		return rc;
	}

	(*rgt)->rgt_leader_term = leader_term;
	(*rgt)->rgt_time_start = d_timeus_secdiff(0);

	D_ASSERT(rebuild_op == RB_OP_FAIL ||
		 rebuild_op == RB_OP_DRAIN ||
		 rebuild_op == RB_OP_ADD);
	match_status = (rebuild_op == RB_OP_FAIL ? PO_COMP_ST_DOWN :
			rebuild_op == RB_OP_DRAIN ? PO_COMP_ST_DRAIN :
			PO_COMP_ST_UP);

	if (tgts != NULL && tgts->pti_number > 0) {
		bool changed = false;
		int i;

		/* Set failed(being rebuilt) targets scan/pull status.*/
		for (i = 0; i < tgts->pti_number; i++) {
			struct pool_target *target;
			struct pool_domain *dom;
			int ret;

			ret = pool_map_find_target(pool->sp_map,
						   tgts->pti_ids[i].pti_id,
						   &target);
			if (ret <= 0)
				continue;

			D_ASSERT(target != NULL);
			if (target->ta_comp.co_status == match_status)
				changed = true;

			dom = pool_map_find_node_by_rank(pool->sp_map,
						target->ta_comp.co_rank);
			if (dom && dom->do_comp.co_status == match_status) {
				D_DEBUG(DB_REBUILD, "rebuild %s rank %u/%u\n",
					rebuild_op == RB_OP_FAIL ? "fail" :
					rebuild_op == RB_OP_DRAIN ? "drain" :
					rebuild_op == RB_OP_ADD ? "add" : "???",
					target->ta_comp.co_rank,
					target->ta_comp.co_id);
			}
		}
		/* These failed targets do not exist in the pool
		 * map anymore -> we need to skip this rebuild.
		 */
		if (!changed) {
			rc = -DER_CANCELED;
			D_ERROR("rebuild targets canceled\n");
		}
	}

	return rc;
}

/* Broadcast objects scan requests to all server targets to start
 * rebuild.
 */
static int
rebuild_scan_broadcast(struct ds_pool *pool,
		       struct rebuild_global_pool_tracker *rgt,
		       struct pool_target_id_list *tgts_failed,
		       daos_rebuild_opc_t rebuild_op)
{
	struct rebuild_scan_in	*rsi;
	struct rebuild_scan_out	*rso;
	crt_rpc_t		*rpc;
	int			rc;

	/* Send rebuild RPC to all targets of the pool to initialize rebuild.
	 * XXX this should be idempotent as well as query and fini.
	 */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_OBJECTS_SCAN, &rpc, NULL,
				  NULL);
	if (rc != 0) {
		D_ERROR("pool map broad cast failed: rc "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rsi = crt_req_get(rpc);
	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID" scan broadcast, op=%s\n",
		DP_UUID(pool->sp_uuid), RB_OP_STR(rebuild_op));

	uuid_copy(rsi->rsi_pool_uuid, pool->sp_uuid);
	rsi->rsi_ns_id = pool->sp_iv_ns->iv_ns_id;
	rsi->rsi_leader_term = rgt->rgt_leader_term;
	rsi->rsi_rebuild_ver = rgt->rgt_rebuild_ver;
	rsi->rsi_tgts_num = tgts_failed->pti_number;
	rsi->rsi_rebuild_op = rebuild_op;
	crt_group_rank(pool->sp_group,  &rsi->rsi_master_rank);

	rc = dss_rpc_send(rpc);
	rso = crt_reply_get(rpc);
	if (rc == 0)
		rc = rso->rso_status;

	rgt->rgt_init_scan = 1;
	rgt->rgt_stable_epoch = rso->rso_stable_epoch;

	crt_req_decref(rpc);
	return rc;
}

static void
rpt_destroy(struct rebuild_tgt_pool_tracker *rpt)
{
	D_ASSERT(rpt->rt_refcount == 0);
	D_ASSERT(d_list_empty(&rpt->rt_list));
	if (!daos_handle_is_inval(rpt->rt_tobe_rb_root_hdl)) {
		dbtree_destroy(rpt->rt_tobe_rb_root_hdl, NULL);
		rpt->rt_tobe_rb_root_hdl = DAOS_HDL_INVAL;
	}
	if (!daos_handle_is_inval(rpt->rt_rebuilt_root_hdl)) {
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

	if (rpt->rt_done_cond)
		ABT_cond_free(&rpt->rt_done_cond);

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

/** Try merge the tasks to the current task.
 *
 * return 1 means the rebuild targets were successfully merged to existing task.
 * return 0 means these targets can not merge.
 * Other return value indicates an error.
 */
static int
rebuild_try_merge_tgts(const uuid_t pool_uuid, uint32_t map_ver,
		       daos_rebuild_opc_t rebuild_op,
		       struct pool_target_id_list *tgts)
{
	struct rebuild_task *task;
	struct rebuild_task *found = NULL;
	int		    rc;

	d_list_for_each_entry(task, &rebuild_gst.rg_queue_list,
			      dst_list) {
		/* Only merge tasks that match both pool AND operation */
		if (uuid_compare(task->dst_pool_uuid, pool_uuid) == 0
		    && task->dst_rebuild_op == rebuild_op) {
			found = task;
			break;
		}
	}

	if (found == NULL)
		return 0;

	D_DEBUG(DB_REBUILD, "("DF_UUID" ver=%u) id %u merge to task %p op=%s\n",
		DP_UUID(pool_uuid), map_ver,
		tgts->pti_ids[0].pti_id, task, RB_OP_STR(rebuild_op));

	/* Merge the failed ranks to existing rebuild task */
	rc = pool_target_id_list_merge(&task->dst_tgts, tgts);
	if (rc)
		return rc;

	if (task->dst_map_ver < map_ver) {
		D_DEBUG(DB_REBUILD, "rebuild task ver %u --> %u\n",
			found->dst_map_ver, map_ver);
		found->dst_map_ver = map_ver;
	}

	D_PRINT("Rebuild [queued] ("DF_UUID" ver=%u) id %u\n",
		DP_UUID(pool_uuid), map_ver, tgts->pti_ids[0].pti_id);

	return 1;
}

/**
 * Initiate the rebuild process, i.e. sending rebuild requests to every target
 * to find out the impacted objects.
 */
static int
rebuild_leader_start(struct ds_pool *pool, uint32_t rebuild_ver,
		     struct pool_target_id_list *tgts,
		     daos_rebuild_opc_t rebuild_op,
		     struct rebuild_global_pool_tracker **p_rgt)
{
	uint32_t	map_ver;
	d_iov_t		map_buf_iov = {0};
	daos_prop_t	*prop = NULL;
	uint64_t	leader_term;
	int		rc;

	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID", rebuild version=%u, op=%s\n",
		DP_UUID(pool->sp_uuid), rebuild_ver, RB_OP_STR(rebuild_op));

	rc = ds_pool_svc_term_get(pool->sp_uuid, &leader_term);
	if (rc) {
		D_ERROR("Get pool service term failed: rc = "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = rebuild_prepare(pool, rebuild_ver, leader_term, tgts, rebuild_op,
			     p_rgt);
	if (rc) {
		D_ERROR("rebuild prepare failed: rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

re_dist:
	rc = ds_pool_map_buf_get(pool->sp_uuid, &map_buf_iov, &map_ver);
	if (rc) {
		D_ERROR("pool map broadcast failed: rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* IV bcast the pool map in case for offline rebuild */
	rc = ds_pool_iv_map_update(pool, map_buf_iov.iov_buf, map_ver);
	D_FREE(map_buf_iov.iov_buf);
	if (rc) {
		/* If the failure is due to stale group version, then maybe
		 * the leader upgrade group version during this time, let's
		 * retry in this case.
		 */
		memset(&map_buf_iov, 0, sizeof(map_buf_iov));
		if (rc == -DER_GRPVER) {
			D_DEBUG(DB_REBUILD, DF_UUID" redistribute pool map\n",
				DP_UUID(pool->sp_uuid));
			goto re_dist;
		} else {
			D_ERROR("pool map broadcast failed: rc "DF_RC"\n",
				DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

	rc = ds_pool_prop_fetch(pool, DAOS_PO_QUERY_PROP_ALL, &prop);
	if (rc) {
		D_ERROR("pool prop fetch failed: rc %d\n", rc);
		D_GOTO(out, rc);
	}

	/* Update pool properties by IV */
	rc = ds_pool_iv_prop_update(pool, prop);
	if (rc) {
		D_ERROR("ds_pool_iv_prop_update failed %d.\n", rc);
		D_GOTO(out, rc);
	}

	/* broadcast scan RPC to all targets */
	rc = rebuild_scan_broadcast(pool, *p_rgt, tgts, rebuild_op);
	if (rc) {
		D_ERROR("object scan failed: rc "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	if (prop != NULL)
		daos_prop_free(prop);
	return rc;
}

static void
rebuild_task_ult(void *arg)
{
	struct rebuild_task			*task = arg;
	struct ds_pool				*pool;
	struct rebuild_global_pool_tracker	*rgt = NULL;
	struct rebuild_iv                       iv = { 0 };
	int					rc;

	pool = ds_pool_lookup(task->dst_pool_uuid);
	if (pool == NULL) {
		D_ERROR(DF_UUID": failed to look up pool\n",
			DP_UUID(task->dst_pool_uuid));
		return;
	}

	D_PRINT("Rebuild [started] (pool "DF_UUID" ver=%u, op=%s)\n",
		DP_UUID(task->dst_pool_uuid), task->dst_map_ver,
		RB_OP_STR(task->dst_rebuild_op));

	rc = rebuild_leader_start(pool, task->dst_map_ver, &task->dst_tgts,
				  task->dst_rebuild_op, &rgt);
	if (rc != 0) {
		if (rc == -DER_CANCELED) {
			D_DEBUG(DB_REBUILD, "pool "DF_UUID" ver %u rebuild is"
				" canceled.\n", DP_UUID(task->dst_pool_uuid),
				task->dst_map_ver);
			rc = 0;
		}

		D_PRINT("Rebuild [failed] (pool "DF_UUID" ver=%u status=%d)\n",
			DP_UUID(task->dst_pool_uuid), task->dst_map_ver, rc);

		D_ERROR(""DF_UUID" (ver=%u) rebuild failed: rc %d\n",
			DP_UUID(task->dst_pool_uuid), task->dst_map_ver, rc);

		if (rgt) {
			rgt->rgt_abort = 1;
			rgt->rgt_status.rs_done = 1;
			rgt->rgt_status.rs_errno = rc;
			D_GOTO(done, rc);
		} else {
			D_GOTO(out_put, rc);
		}
	}

	/* Wait until rebuild finished */
	rebuild_leader_status_check(pool, task->dst_map_ver, rgt);
done:
	if (!is_rebuild_global_done(rgt)) {
		D_DEBUG(DB_REBUILD, DF_UUID" rebuild is not done: %d\n",
			DP_UUID(task->dst_pool_uuid), rgt->rgt_status.rs_errno);

		if (rgt->rgt_abort && rgt->rgt_status.rs_errno == 0) {
			/* If the leader is stopped due to the leader change,
			 * then let's do not stop the real rebuild(scan/pull
			 * ults), because the new leader will resend the
			 * scan requests, which will then become the new
			 * leader to track the rebuild.
			 */
			D_DEBUG(DB_REBUILD, DF_UUID" Only stop the leader\n",
				DP_UUID(task->dst_pool_uuid));
			D_GOTO(out_put, rc);
		}
	} else {
		if (task->dst_tgts.pti_number <= 0)
			goto iv_stop;

		if (task->dst_rebuild_op == RB_OP_FAIL
		    || task->dst_rebuild_op == RB_OP_DRAIN) {
			rc = ds_pool_tgt_exclude_out(pool->sp_uuid,
						     &task->dst_tgts);
			D_DEBUG(DB_REBUILD, "mark failed target %d of "DF_UUID
				" as DOWNOUT: %d\n",
				task->dst_tgts.pti_ids[0].pti_id,
				DP_UUID(task->dst_pool_uuid), rc);
		} else if (task->dst_rebuild_op == RB_OP_ADD) {
			rc = ds_pool_tgt_add_in(pool->sp_uuid, &task->dst_tgts);
			D_DEBUG(DB_REBUILD, "mark added target %d of "DF_UUID
				" UPIN: %d\n", task->dst_tgts.pti_ids[0].pti_id,
				DP_UUID(task->dst_pool_uuid), rc);
		}
	}
iv_stop:
	/* NB: even if there are some failures, the leader should
	 * still notify all other servers to stop their local
	 * rebuild.
	 */
	if (rgt->rgt_init_scan) {
		uuid_copy(iv.riv_pool_uuid, task->dst_pool_uuid);
		iv.riv_master_rank	= pool->sp_iv_ns->iv_master_rank;
		iv.riv_ver		= rgt->rgt_rebuild_ver;
		iv.riv_global_scan_done = is_rebuild_global_scan_done(rgt);
		iv.riv_global_done	= 1;
		iv.riv_leader_term	= rgt->rgt_leader_term;
		iv.riv_toberb_obj_count = rgt->rgt_status.rs_toberb_obj_nr;
		iv.riv_obj_count	= rgt->rgt_status.rs_obj_nr;
		iv.riv_rec_count	= rgt->rgt_status.rs_rec_nr;
		iv.riv_size		= rgt->rgt_status.rs_size;
		iv.riv_seconds          = rgt->rgt_status.rs_seconds;

		rc = rebuild_iv_update(pool->sp_iv_ns, &iv, CRT_IV_SHORTCUT_NONE,
				       CRT_IV_SYNC_LAZY, true);
		if (rc)
			D_ERROR("iv final update fails"DF_UUID":rc %d\n",
				DP_UUID(task->dst_pool_uuid), rc);
	}

	/* Update the rebuild status, so query can get the rebuild status. */
	rc = rebuild_status_completed_update(task->dst_pool_uuid,
					     &rgt->rgt_status);
	if (rc != 0)
		D_ERROR("rebuild_status_completed_update, "DF_UUID" "
			"failed, rc %d.\n", DP_UUID(task->dst_pool_uuid), rc);
out_put:
	if (rgt == NULL || !is_rebuild_global_done(rgt)) {
		int ret;

		/* NB: we can not skip the rebuild of the target,
		 * otherwise it will lose data and also mess the
		 * rebuild sequence, which has to be done by failure
		 * sequence order.
		 */
		ret = ds_rebuild_schedule(pool->sp_uuid,
					  task->dst_map_ver,
					  &task->dst_tgts, RB_OP_FAIL);
		if (ret != 0)
			D_ERROR("reschedule "DF_RC"\n", DP_RC(ret));
		else
			D_DEBUG(DB_REBUILD, DF_UUID" reschedule rebuild\n",
				DP_UUID(pool->sp_uuid));
	}

	ds_pool_put(pool);
	if (rgt)
		rebuild_global_pool_tracker_destroy(rgt);

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
			ABT_thread_yield();
			continue;
		}

		d_list_for_each_entry_safe(task, task_tmp,
				      &rebuild_gst.rg_queue_list, dst_list) {
			if (pool_is_rebuilding(task->dst_pool_uuid))
				continue;

			rc = dss_ult_create(rebuild_task_ult, task,
					    DSS_ULT_REBUILD, DSS_TGT_SELF,
					    0, NULL);
			if (rc == 0) {
				rebuild_gst.rg_inflight++;
				/* TODO: This needs to be expanded to select the
				 * highest-priority task based on rebuild op,
				 * rather than just the next one in queue
				 */
				d_list_move(&task->dst_list,
					       &rebuild_gst.rg_running_list);
			} else {
				D_ERROR(DF_UUID" create ult failed: %d\n",
					DP_UUID(task->dst_pool_uuid), rc);
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
ds_rebuild_abort(uuid_t pool_uuid, unsigned int version)
{
	struct rebuild_tgt_pool_tracker *rpt;

	/* If this is called on non-leader node, it will do nothing */
	ds_rebuild_leader_stop(pool_uuid, version);

	rpt = rpt_lookup(pool_uuid, version);
	if (rpt == NULL)
		return;

	/* If it can find rpt, it means rebuild has not finished yet
	 * on this target, so the rpt has to been hold by someone
	 * else, so it is safe to use rpt after rpt_put().
	 *
	 * And we have to do rpt_put(), otherwise it will hold
	 * rebuild_tgt_fini().
	 */
	D_ASSERT(rpt->rt_refcount > 1);
	rpt_put(rpt);

	rpt->rt_abort = 1;
	/* Since the rpt will be destroyed after signal rt_done_cond,
	 * so we have to use another lock here.
	 */
	ABT_mutex_lock(rebuild_gst.rg_lock);
	ABT_cond_wait(rpt->rt_done_cond, rebuild_gst.rg_lock);
	ABT_mutex_unlock(rebuild_gst.rg_lock);
}

void
ds_rebuild_leader_stop(const uuid_t pool_uuid, unsigned int version)
{
	struct rebuild_global_pool_tracker	*rgt;
	struct rebuild_task			*task;
	struct rebuild_task			*task_tmp;

	/* Remove the rebuild tasks from queue list */
	d_list_for_each_entry_safe(task, task_tmp, &rebuild_gst.rg_queue_list,
				   dst_list) {
		if (uuid_compare(task->dst_pool_uuid, pool_uuid) == 0 &&
		    (version == -1 || task->dst_map_ver == version)) {
			rebuild_task_destroy(task);
			if (version != -1)
				break;
		}
	}

	/* Then check running list, Note: each rebuilding pool can only have one
	 * version being rebuilt each time, so we do not need check version for
	 * running list.
	 */
	rgt = rebuild_global_pool_tracker_lookup(pool_uuid, version);
	if (rgt == NULL)
		return;

	D_DEBUG(DB_REBUILD, "abort rebuild "DF_UUID" version %d\n",
		DP_UUID(pool_uuid), version);
	rgt->rgt_abort = 1;
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
rebuild_print_list_update(const char *const str, const uuid_t uuid,
			  const uint32_t map_ver,
			  daos_rebuild_opc_t rebuild_op,
			  struct pool_target_id_list *tgts) {
	int i;

	D_PRINT("%s (pool="DF_UUID" ver=%u, op=%s) tgts=", str, DP_UUID(uuid),
		map_ver, RB_OP_STR(rebuild_op));
	for (i = 0; i < tgts->pti_number; i++) {
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
ds_rebuild_schedule(const uuid_t uuid, uint32_t map_ver,
		    struct pool_target_id_list *tgts,
		    daos_rebuild_opc_t rebuild_op)
{
	struct rebuild_task	*task;
	int			rc;

	/* Check if the pool already in the queue list */
	rc = rebuild_try_merge_tgts(uuid, map_ver, rebuild_op, tgts);
	if (rc)
		return rc == 1 ? 0 : rc;

	/* No existing task was found - allocate a new one and use it */
	D_ALLOC_PTR(task);
	if (task == NULL)
		return -DER_NOMEM;

	task->dst_map_ver = map_ver;
	task->dst_rebuild_op = rebuild_op;
	uuid_copy(task->dst_pool_uuid, uuid);
	D_INIT_LIST_HEAD(&task->dst_list);

	rc = pool_target_id_list_merge(&task->dst_tgts, tgts);
	if (rc)
		D_GOTO(free, rc);

	rebuild_print_list_update("Rebuild queued",
				  uuid, map_ver, rebuild_op, tgts);
	d_list_add_tail(&task->dst_list, &rebuild_gst.rg_queue_list);

	if (!rebuild_gst.rg_rebuild_running) {
		rc = ABT_cond_create(&rebuild_gst.rg_stop_cond);
		if (rc != ABT_SUCCESS)
			D_GOTO(free, rc = dss_abterr2der(rc));

		rebuild_gst.rg_rebuild_running = 1;
		rc = dss_ult_create(rebuild_ults, NULL, DSS_ULT_REBUILD,
				    DSS_TGT_SELF, 0, NULL);
		if (rc) {
			ABT_cond_free(&rebuild_gst.rg_stop_cond);
			rebuild_gst.rg_rebuild_running = 0;
			D_GOTO(free, rc);
		}
	}
free:
	if (rc)
		rebuild_task_destroy(task);
	return rc;
}

static int
regenerate_task_internal(struct ds_pool *pool, struct pool_target *tgts,
			 unsigned int tgts_cnt, daos_rebuild_opc_t rebuild_op)
{
	unsigned int	i;
	int		rc;

	for (i = 0; i < tgts_cnt; i++) {
		struct pool_target		*tgt = &tgts[i];
		struct pool_target_id		tgt_id;
		struct pool_target_id_list	id_list;

		tgt_id.pti_id = tgt->ta_comp.co_id;
		id_list.pti_ids = &tgt_id;
		id_list.pti_number = 1;

		rc = ds_rebuild_schedule(pool->sp_uuid, tgt->ta_comp.co_fseq,
					 &id_list, rebuild_op);
		if (rc) {
			D_ERROR(DF_UUID" schedule op %d ver %d failed: rc %d\n",
				DP_UUID(pool->sp_uuid), rebuild_op,
				tgt->ta_comp.co_fseq, rc);
			return rc;
		}
	}

	return DER_SUCCESS;
}

/* Regenerate the rebuild tasks when changing the leader. */
int
ds_rebuild_regenerate_task(struct ds_pool *pool)
{
	struct pool_target	*tgts;
	unsigned int		tgts_cnt;
	int			rc;

	rebuild_gst.rg_abort = 0;

	/* get all down targets */
	rc = pool_map_find_down_tgts(pool->sp_map, &tgts, &tgts_cnt);
	if (rc != 0) {
		D_ERROR("failed to create failed tgt list rc "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = regenerate_task_internal(pool, tgts, tgts_cnt, RB_OP_FAIL);
	if (rc != 0)
		return rc;

	/* get all up targets */
	rc = pool_map_find_up_tgts(pool->sp_map, &tgts, &tgts_cnt);
	if (rc != 0) {
		D_ERROR("failed to create up tgt list rc %d\n", rc);
		return rc;
	}

	return regenerate_task_internal(pool, tgts, tgts_cnt, RB_OP_ADD);
}

/* Hang rebuild ULT on the current xstream */
void
rebuild_hang(void)
{
	D_DEBUG(DB_REBUILD, "Hang current rebuild process.\n");
	dss_parameters_set(DMG_KEY_REBUILD_THROTTLING, 0);
}

static int
rebuild_fini_one(void *arg)
{
	struct rebuild_tgt_pool_tracker	*rpt = arg;
	struct rebuild_pool_tls		*pool_tls;
	struct ds_pool_child		*dpc;

	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
					   rpt->rt_rebuild_ver);
	if (pool_tls == NULL)
		return 0;

	if (!daos_handle_is_inval(pool_tls->rebuild_pool_hdl)) {
		D_DEBUG(DB_REBUILD, "close container/pool "
			DF_UUID"/"DF_UUID"\n",
			DP_UUID(rpt->rt_coh_uuid), DP_UUID(rpt->rt_poh_uuid));
		dsc_pool_close(pool_tls->rebuild_pool_hdl);
		pool_tls->rebuild_pool_hdl = DAOS_HDL_INVAL;
	}

	rebuild_pool_tls_destroy(pool_tls);
	ds_migrate_fini_one(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
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
		dpc->spc_rebuild_end_hlc = crt_hlc_get();
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

	D_DEBUG(DB_REBUILD, "Finalize rebuild for "DF_UUID", map_ver=%u\n",
		DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver);

	ABT_mutex_lock(rpt->rt_lock);
	D_ASSERT(rpt->rt_refcount > 0);
	d_list_del_init(&rpt->rt_list);
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
	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
					   rpt->rt_rebuild_ver);
	if (pool_tls != NULL)
		rebuild_pool_tls_destroy(pool_tls);

	/* close the rebuild pool/container on all main XS */
	rc = dss_task_collective(rebuild_fini_one, rpt, 0, DSS_ULT_REBUILD);

	rpt_put(rpt);
	/* No one should access rpt after rebuild_fini_one.
	 */
	D_ASSERT(rpt->rt_refcount == 0);

	/* Notify anyone who is waiting for the rebuild to finish */
	ABT_mutex_lock(rebuild_gst.rg_lock);
	ABT_cond_signal(rpt->rt_done_cond);
	ABT_mutex_unlock(rebuild_gst.rg_lock);

	rpt_destroy(rpt);

	return rc;
}

void
rebuild_tgt_status_check_ult(void *arg)
{
	struct rebuild_tgt_pool_tracker	*rpt = arg;

	D_ASSERT(rpt != NULL);
	rpt->rt_ult = dss_sleep_ult_create();
	if (rpt->rt_ult == NULL) {
		D_ERROR("Can not start rebuild status check\n");
		return;
	}

	while (1) {
		struct rebuild_iv		iv;
		struct rebuild_tgt_query_info	status;
		int				rc;

		memset(&status, 0, sizeof(status));
		ABT_mutex_create(&status.lock);
		rc = rebuild_tgt_query(rpt, &status);
		ABT_mutex_free(&status.lock);
		if (rc || status.status != 0) {
			D_ERROR(DF_UUID" rebuild failed: rc %d\n",
				DP_UUID(rpt->rt_pool_uuid),
				rc == 0 ? status.status : rc);
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
			iv.riv_master_rank =
				rpt->rt_pool->sp_iv_ns->iv_master_rank;
			iv.riv_rank = rpt->rt_rank;
			iv.riv_ver = rpt->rt_rebuild_ver;
			iv.riv_leader_term = rpt->rt_leader_term;

			/* Cart does not support failure recovery yet, let's
			 * send the status to root for now. FIXME
			 */
			if (DAOS_FAIL_CHECK(DAOS_REBUILD_TGT_IV_UPDATE_FAIL))
				rc = -DER_INVAL;
			else
				rc = rebuild_iv_update(rpt->rt_pool->sp_iv_ns,
						   &iv, CRT_IV_SHORTCUT_TO_ROOT,
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
				D_WARN("rebuild tgt iv update failed: %d\n",
					rc);
				/* Already finish rebuilt, but it can not
				 * its rebuild status on the leader, i.e.
				 * it can not find the IV see crt_iv_hdlr_xx().
				 * let's just stop the rebuild.
				 */
				if (rc == -DER_NONEXIST && !status.rebuilding)
					rpt->rt_global_done = 1;
			}
		}

		D_DEBUG(DB_REBUILD, "ver %d obj "DF_U64" rec "DF_U64" size "
			DF_U64" scan done %d pull done %d scan gl done %d"
			" gl done %d status %d\n",
			rpt->rt_rebuild_ver, iv.riv_obj_count,
			iv.riv_rec_count, iv.riv_size, rpt->rt_scan_done,
			iv.riv_pull_done, rpt->rt_global_scan_done,
			rpt->rt_global_done, iv.riv_status);

		if (rpt->rt_global_done || rpt->rt_abort)
			break;

		dss_ult_sleep(rpt->rt_ult, RBLD_CHECK_INTV);
	}

	dss_sleep_ult_destroy(rpt->rt_ult);
	rpt->rt_ult = NULL;

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
					   rpt->rt_coh_uuid,
					   rpt->rt_rebuild_ver);
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
		" rebuild eph "DF_U64" rc %d\n", DP_UUID(rpt->rt_pool_uuid),
		DP_UUID(rpt->rt_coh_uuid), rpt->rt_rebuild_fence, rc);

	ds_pool_child_put(dpc);

	return rc;
}

static int
rpt_create(struct ds_pool *pool, uint32_t pm_ver, uint64_t leader_term,
	   uint32_t tgts_num, struct rebuild_tgt_pool_tracker **p_rpt)
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

	rc = ABT_cond_create(&rpt->rt_done_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(free, rc = dss_abterr2der(rc));

	uuid_copy(rpt->rt_pool_uuid, pool->sp_uuid);
	rpt->rt_reported_toberb_objs = 0;
	rpt->rt_reported_obj_cnt = 0;
	rpt->rt_reported_rec_cnt = 0;
	rpt->rt_reported_size = 0;
	rpt->rt_rebuild_ver = pm_ver;
	rpt->rt_leader_term = leader_term;
	rpt->rt_tgts_num = tgts_num;
	crt_group_rank(pool->sp_group, &rank);
	rpt->rt_rank = rank;

	rpt->rt_refcount = 1;
	*p_rpt = rpt;
free:
	if (rc != 0)
		rpt_destroy(rpt);
	return rc;
}

/**
 * Called by ds_pool_tgt_map_update->update_child_map() to update pool
 * map on each xstream for rebuild.
 */
int ds_rebuild_pool_map_update(struct ds_pool *pool)
{
	struct rebuild_pool_tls *pool_tls;
	int rc;

	pool_tls = rebuild_pool_tls_lookup(pool->sp_uuid, -1);
	if (pool_tls == NULL ||
	    daos_handle_is_inval(pool_tls->rebuild_pool_hdl))
		return 0;

	/* update the pool map over the client stack */
	rc = dc_pool_update_map(pool_tls->rebuild_pool_hdl,
				pool->sp_map);

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
	int				rc;

	D_DEBUG(DB_REBUILD, "prepare rebuild for "DF_UUID"/%d\n",
		DP_UUID(rsi->rsi_pool_uuid), rsi->rsi_rebuild_ver);

	pool = ds_pool_lookup(rsi->rsi_pool_uuid);
	if (pool == NULL) {
		D_ERROR("Can not find pool.\n");
		return -DER_NONEXIST;
	}

	if (pool->sp_group == NULL) {
		char id[DAOS_UUID_STR_SIZE];

		uuid_unparse_lower(pool->sp_uuid, id);
		pool->sp_group = crt_group_lookup(id);
		if (pool->sp_group == NULL) {
			D_ERROR(DF_UUID": pool group not found\n",
				DP_UUID(pool->sp_uuid));
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	if (pool->sp_iv_ns) {
		uuid_t	cont_uuid;

		uuid_clear(cont_uuid);
		/* Let's invalidate local snapshot cache before
		 * rebuild, so to make sure rebuild will use the updated
		 * snapshot during rebuild fetch, otherwise it may cause
		 * corruption.
		 */
		rc = ds_cont_revoke_snaps(pool->sp_iv_ns, cont_uuid,
					  CRT_IV_SHORTCUT_NONE,
					  CRT_IV_SYNC_NONE);
		if (rc)
			D_GOTO(out, rc);
	}

	/* Create rpt for the target */
	rc = rpt_create(pool, rsi->rsi_rebuild_ver, rsi->rsi_leader_term,
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

	D_ASSERT(pool->sp_iv_ns != NULL);
	ds_pool_iv_ns_update(pool, rsi->rsi_master_rank);

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
					   rpt->rt_coh_uuid,
					   rpt->rt_rebuild_ver);
	if (pool_tls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpt->rt_rebuild_fence = crt_hlc_get();
	rc = dss_task_collective(rebuild_prepare_one, rpt, 0, DSS_ULT_REBUILD);
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
	daos_prop_entries_free(&prop);

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
	.sm_name	= "rebuild",
	.sm_mod_id	= DAOS_REBUILD_MODULE,
	.sm_ver		= DAOS_REBUILD_VERSION,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cleanup	= rebuild_cleanup,
	.sm_proto_fmt	= &rebuild_proto_fmt,
	.sm_cli_count	= 0,
	.sm_handlers	= rebuild_handlers,
	.sm_key		= &rebuild_module_key,
};
