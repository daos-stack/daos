/**
 * (C) Copyright 2016 Intel Corporation.
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
#include "rpc.h"
#include "rebuild_internal.h"

#define RBLD_BCAST_INTV		2	/* seocnds interval to retry bcast */
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
rebuild_pool_tls_create(uuid_t pool_uuid, unsigned int ver)
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
	d_list_del(&tls->rebuild_pool_list);
	D_FREE_PTR(tls);
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
rebuild_global_pool_tracker_lookup(uuid_t pool_uuid, unsigned int ver)
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

enum {
	SCAN_CHECK,
	PULL_CHECK,
};

static bool
is_rebuild_global_done(struct rebuild_global_pool_tracker *rgt, int type)
{
	uint32_t	*bits;
	int		idx;

	if (type == SCAN_CHECK)
		bits = rgt->rgt_scan_bits;
	else
		bits = rgt->rgt_pull_bits;

	D_ASSERT(bits != NULL);

	D_DEBUG(DB_REBUILD, "%s done check 0x%x [%d-%d]\n",
		type == SCAN_CHECK ? "scan" : "pull", bits[0], 0,
		rgt->rgt_bits_size - 1);

	idx = daos_first_unset_bit(bits, roundup(rgt->rgt_bits_size,
						 DAOS_BITS_SIZE) /
						 DAOS_BITS_SIZE);

	if (idx == -1 || idx >= rgt->rgt_bits_size)
		return true;

	return false;
}

static bool
is_rebuild_global_pull_done(struct rebuild_global_pool_tracker *rgt)
{
	return is_rebuild_global_done(rgt, PULL_CHECK);
}

static bool
is_rebuild_global_scan_done(struct rebuild_global_pool_tracker *rgt)
{
	return is_rebuild_global_done(rgt, SCAN_CHECK);
}

int
rebuild_global_status_update(struct rebuild_global_pool_tracker *rgt,
			     struct rebuild_iv *iv)
{
	D_DEBUG(DB_REBUILD, "iv rank %d scan_done %d pull_done %d\n",
		iv->riv_rank, iv->riv_scan_done, iv->riv_pull_done);

	if (!iv->riv_scan_done)
		return 0;

	if (!rgt->rgt_scan_done) {
		setbit(rgt->rgt_scan_bits, iv->riv_rank);
		D_DEBUG(DB_REBUILD, "rebuild ver %d tgt %d scan"
			" done bits %x\n", rgt->rgt_rebuild_ver,
			iv->riv_rank, rgt->rgt_scan_bits[0]);
		if (is_rebuild_global_scan_done(rgt))
			rgt->rgt_scan_done = 1;

		/* If global scan is not done, then you can not trust
		 * pull status. But if the rebuild on that target is
		 * failed(riv_status != 0), then the target will report
		 * both scan and pull status to the leader, i.e. they
		 * both can be trusted.
		 */
		if (iv->riv_status == 0 && !rgt->rgt_scan_done)
			return 0;
	}

	/* Only trust pull done if scan is done globally */
	if (iv->riv_pull_done) {
		setbit(rgt->rgt_pull_bits, iv->riv_rank);
		D_DEBUG(DB_REBUILD, "rebuild ver %d tgt %d pull"
			" done bits %x\n", rgt->rgt_rebuild_ver,
			iv->riv_rank, rgt->rgt_pull_bits[0]);
		if (is_rebuild_global_pull_done(rgt))
			rgt->rgt_done = 1;
	}

	return 0;
}

bool
is_rebuild_container(uuid_t pool_uuid, uuid_t coh_uuid)
{
	struct rebuild_pool_tls	*tls;
	bool			is_rebuild = false;

	tls = rebuild_pool_tls_lookup(pool_uuid, -1);
	if (tls == NULL)
		return false;

	if (!uuid_is_null(tls->rebuild_coh_uuid)) {
		D_DEBUG(DB_REBUILD, "rebuild "DF_UUID" cont_hdl_uuid "
			DF_UUID"\n", DP_UUID(tls->rebuild_coh_uuid),
			DP_UUID(coh_uuid));
		is_rebuild = !uuid_compare(tls->rebuild_coh_uuid, coh_uuid);
	}

	return is_rebuild;
}

bool
is_rebuild_pool(uuid_t pool_uuid, uuid_t poh_uuid)
{
	struct rebuild_pool_tls	*tls;
	bool			is_rebuild = false;

	tls = rebuild_pool_tls_lookup(pool_uuid, -1);
	if (tls == NULL)
		return false;

	if (!uuid_is_null(tls->rebuild_poh_uuid)) {
		D_DEBUG(DB_REBUILD, "rebuild "DF_UUID" cont_hdl_uuid "
			DF_UUID"\n", DP_UUID(tls->rebuild_poh_uuid),
			DP_UUID(poh_uuid));
		is_rebuild = !uuid_compare(tls->rebuild_poh_uuid, poh_uuid);
	}

	return is_rebuild;
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

	D_FREE_PTR(tls);
}

struct rebuild_tgt_query_arg {
	struct rebuild_tgt_pool_tracker *rpt;
	struct rebuild_tgt_query_info *status;
};

static int
dss_rebuild_check_one(void *data)
{
	struct rebuild_tgt_query_arg	*arg = data;
	struct rebuild_pool_tls		*pool_tls;
	struct rebuild_tgt_query_info	*status = arg->status;
	struct rebuild_tgt_pool_tracker	*rpt = arg->rpt;
	unsigned int idx = dss_get_module_info()->dmi_tid;

	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
					   rpt->rt_rebuild_ver);
	D_ASSERTF(pool_tls != NULL, DF_UUID" ver %d\n",
		   DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver);

	D_DEBUG(DB_REBUILD, "%d rec_count "DF_U64" obj_count "DF_U64
		" scanning %d status %d inflight %d\n",
		idx, pool_tls->rebuild_pool_rec_count,
		pool_tls->rebuild_pool_obj_count,
		pool_tls->rebuild_pool_scanning,
		pool_tls->rebuild_pool_status,
		rpt->rt_pullers[idx].rp_inflight);
	ABT_mutex_lock(status->lock);
	if (pool_tls->rebuild_pool_scanning)
		status->scanning = 1;
	if (pool_tls->rebuild_pool_status != 0 && status->status == 0)
		status->status = pool_tls->rebuild_pool_status;
	status->rec_count += pool_tls->rebuild_pool_rec_count;
	status->obj_count += pool_tls->rebuild_pool_obj_count;
	pool_tls->rebuild_pool_rec_count = 0;
	pool_tls->rebuild_pool_obj_count = 0;
	ABT_mutex_unlock(status->lock);

	return 0;
}

static int
rebuild_tgt_query(struct rebuild_tgt_pool_tracker *rpt,
		  struct rebuild_tgt_query_info *status)
{
	struct rebuild_tgt_query_arg	arg;
	int				rc;

	arg.rpt = rpt;
	arg.status = status;

	/* let's check scanning status on every thread*/
	ABT_mutex_lock(rpt->rt_lock);
	rc = dss_task_collective(dss_rebuild_check_one, &arg);
	if (rc) {
		ABT_mutex_unlock(rpt->rt_lock);
		D_GOTO(out, rc);
	}

	if (!status->scanning && !rpt->rt_lead_puller_running) {
		int i;

		/* then check pulling status*/
		for (i = 0; i < rpt->rt_puller_nxs; i++) {
			struct rebuild_puller *puller;

			puller = &rpt->rt_pullers[i];
			ABT_mutex_lock(puller->rp_lock);
			if (d_list_empty(&puller->rp_dkey_list) &&
			    puller->rp_inflight == 0) {
				ABT_mutex_unlock(puller->rp_lock);
				continue;
			}
			ABT_mutex_unlock(puller->rp_lock);

			D_DEBUG(DB_REBUILD,
				"thread %d rebuilding is still busy.\n", i);
			status->rebuilding = true;
			break;
		}
	} else {
		status->rebuilding = true;
	}
	ABT_mutex_unlock(rpt->rt_lock);

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" scanning %d/%d rebuilding=%s, "
		"obj_count="DF_U64", rec_count="DF_U64"\n",
		DP_UUID(rpt->rt_pool_uuid), status->scanning,
		status->status, status->rebuilding ? "yes" : "no",
		status->obj_count, status->rec_count);
out:
	return rc;
}

int
ds_rebuild_query(uuid_t pool_uuid, struct daos_rebuild_status *status)
{
	struct rebuild_global_pool_tracker	*rgt;
	int					rc = 0;

	memset(status, 0, sizeof(*status));

	rgt = rebuild_global_pool_tracker_lookup(pool_uuid, -1);
	if (rgt == NULL) {
		if (d_list_empty(&rebuild_gst.rg_queue_list) &&
		    rebuild_gst.rg_inflight == 0)
			status->rs_done = 1;
		D_GOTO(out, rc = 0);
	}

	memcpy(status, &rgt->rgt_status, sizeof(*status));
	status->rs_version = rgt->rgt_rebuild_ver;

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

out:
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

static void
rebuild_status_check(struct ds_pool *pool, uint32_t map_ver,
		     struct rebuild_global_pool_tracker *rgt)
{
	double		begin = ABT_get_wtime();
	double		last_print = 0;
	double		last_query = 0;
	unsigned int	total;
	int		rc;

	/* FIXME add group later */
	rc = crt_group_size(NULL, &total);
	if (rc)
		return;

	while (1) {
		struct daos_rebuild_status *rs = &rgt->rgt_status;
		char	sbuf[RBLD_SBUF_LEN];
		struct pool_target *targets;
		unsigned int	failed_tgts_cnt;
		double		now;
		char		*str;

		now = ABT_get_wtime();
		if (now - last_query < RBLD_BCAST_INTV) {
			/* Yield to other ULTs */
			ABT_thread_yield();
			continue;
		}

		last_query = now;

		rc = pool_map_find_failed_tgts(pool->sp_map, &targets,
					       &failed_tgts_cnt);
		if (rc != 0) {
			D_ERROR("failed to create failed tgt list rc %d\n",
				rc);
			return;
		}

		if (targets != NULL) {
			int i;

			for (i = 0; i < failed_tgts_cnt; i++) {
				D_DEBUG(DB_REBUILD, "target %d failed\n",
					targets[i].ta_comp.co_rank);
				setbit(rgt->rgt_scan_bits,
				       targets[i].ta_comp.co_rank);
				setbit(rgt->rgt_pull_bits,
				       targets[i].ta_comp.co_rank);
			}
			D_FREE(targets);
		}

		if (!rgt->rgt_done && rgt->rgt_scan_done) {
			struct rebuild_iv iv;

			memset(&iv, 0, sizeof(iv));
			uuid_copy(iv.riv_pool_uuid, rgt->rgt_pool_uuid);
			iv.riv_master_rank = pool->sp_iv_ns->iv_master_rank;
			iv.riv_global_scan_done = 1;
			iv.riv_ver = rgt->rgt_rebuild_ver;
			iv.riv_leader_term = rgt->rgt_leader_term;

			/* Notify others the global scan is done, then
			 * each target can reliablly report its pull status
			 */
			rc = rebuild_iv_update(pool->sp_iv_ns,
					       &iv, CRT_IV_SHORTCUT_NONE,
					       CRT_IV_SYNC_LAZY);
			if (rc)
				D_WARN("rebuild master iv update failed: %d\n",
				       rc);
		}

		/* query the current rebuild status */
		if (rgt->rgt_done)
			rs->rs_done = 1;

		if (rs->rs_done)
			str = rs->rs_errno ? "failed" : "completed";
		else if (rs->rs_obj_nr == 0 && rs->rs_rec_nr == 0)
			str = "scanning";
		else
			str = "pulling";

		snprintf(sbuf, RBLD_SBUF_LEN,
			"Rebuild [%s] (pool "DF_UUID" ver=%u, obj="DF_U64
			", rec= "DF_U64", done %d status %d duration=%d"
			" secs)\n", str, DP_UUID(pool->sp_uuid), map_ver,
			rs->rs_obj_nr, rs->rs_rec_nr, rs->rs_done,
			rs->rs_errno, (int)(now - begin));

		D_DEBUG(DB_REBUILD, "%s", sbuf);
		if (rs->rs_done || rebuild_gst.rg_abort) {
			D_PRINT("%s", sbuf);
			break;
		}

		/* print something at least for each 10 secons */
		if (now - last_print > 10) {
			last_print = now;
			D_PRINT("%s", sbuf);
		}

		ABT_thread_yield();
	}
}

static void
rebuild_global_pool_tracker_destroy(struct rebuild_global_pool_tracker *rgt)
{
	d_list_del(&rgt->rgt_list);
	if (rgt->rgt_scan_bits)
		D_FREE(rgt->rgt_scan_bits);

	if (rgt->rgt_pull_bits)
		D_FREE(rgt->rgt_pull_bits);

	D_FREE_PTR(rgt);
}

static int
rebuild_global_pool_tracker_create(struct ds_pool *pool, uint32_t ver,
				   struct rebuild_global_pool_tracker **p_rgt)
{
	struct rebuild_global_pool_tracker *rgt;
	unsigned int rank_size;
	unsigned int array_size;
	int rc;

	D_ALLOC_PTR(rgt);
	if (rgt == NULL)
		return -DER_NOMEM;
	D_INIT_LIST_HEAD(&rgt->rgt_list);

	rc = crt_group_size(NULL, &rank_size);
	if (rc)
		D_GOTO(out, rc);

	array_size = roundup(rank_size, DAOS_BITS_SIZE) / DAOS_BITS_SIZE;
	rgt->rgt_bits_size = rank_size;

	D_ALLOC(rgt->rgt_scan_bits, array_size * sizeof(uint32_t));
	if (rgt->rgt_scan_bits == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(rgt->rgt_pull_bits, array_size * sizeof(uint32_t));
	if (rgt->rgt_pull_bits == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(rgt->rgt_pool_uuid, pool->sp_uuid);
	rgt->rgt_rebuild_ver = ver;
	d_list_add(&rgt->rgt_list, &rebuild_gst.rg_global_tracker_list);
	*p_rgt = rgt;
out:
	if (rc)
		rebuild_global_pool_tracker_destroy(rgt);
	return 0;
}

static int
rebuild_pool_group_prepare(struct ds_pool *pool)
{
	struct pool_target	*tgts = NULL;
	unsigned int		tgt_cnt;
	char			id[DAOS_UUID_STR_SIZE];
	d_rank_list_t		rank_list;
	d_rank_t		*ranks = NULL;
	crt_group_t		*grp;
	int			i;
	int			rc;

	if (pool->sp_group != NULL)
		return 0;

	/* During pool leader changing, the cart group might still
	 * exists even if sp_group is NULL.
	 */
	uuid_unparse_lower(pool->sp_uuid, id);
	grp = crt_group_lookup(id);
	if (grp != NULL) {
		pool->sp_group = grp;
		return 0;
	}

	rc = pool_map_find_up_tgts(pool->sp_map, &tgts, &tgt_cnt);
	if (rc)
		return rc;

	D_ALLOC(ranks, sizeof(*ranks) * tgt_cnt);
	if (ranks == NULL)
		D_GOTO(out, rc);

	for (i = 0; i < tgt_cnt; i++) {
		ranks[i] = tgts[i].ta_comp.co_rank;
		D_DEBUG(DB_REBUILD, "i %d rank %d\n", i, ranks[i]);
	}

	rank_list.rl_nr = tgt_cnt;
	rank_list.rl_ranks = ranks;

	rc = dss_group_create(id, &rank_list, &grp);
	if (rc != 0)
		D_GOTO(out, rc);

	pool->sp_group = grp;
out:
	if (ranks != NULL)
		D_FREE(ranks);
	if (tgt_cnt > 0 && tgts != NULL)
		D_FREE(tgts);

	return rc;
}

/* To notify all targets to prepare the rebuild */
static int
rebuild_prepare(struct ds_pool *pool, uint32_t rebuild_ver,
		uint64_t leader_term, d_rank_list_t *exclude_tgts,
		struct rebuild_global_pool_tracker **rgt)
 {
	unsigned int	master_rank;
	int		rc;

	D_DEBUG(DB_REBUILD, "pool "DF_UUID" create rebuild iv\n",
		DP_UUID(pool->sp_uuid));

	rc = rebuild_pool_group_prepare(pool);
	if (rc)
		return rc;

	/* Create pool iv ns for the pool */
	crt_group_rank(pool->sp_group, &master_rank);
	rc = ds_pool_iv_ns_update(pool, master_rank, NULL, -1);
	if (rc)
		return rc;

	rc = rebuild_global_pool_tracker_create(pool, rebuild_ver, rgt);
	if (rc)
		return rc;

	(*rgt)->rgt_leader_term = leader_term;
	uuid_generate((*rgt)->rgt_coh_uuid);
	uuid_generate((*rgt)->rgt_poh_uuid);

	if (exclude_tgts != NULL) {
		int i;

		/* Set excluded targets scan/pull bits */
		for (i = 0; i < exclude_tgts->rl_nr; i++) {
			D_ASSERT(exclude_tgts->rl_ranks[i] <
				  (*rgt)->rgt_bits_size);
			setbit((*rgt)->rgt_scan_bits,
				exclude_tgts->rl_ranks[i]);
			setbit((*rgt)->rgt_pull_bits,
				exclude_tgts->rl_ranks[i]);
		}
	}

	return rc;
}

/* Broadcast objects scan requests to all server targets to start
 * rebuild.
 */
static int
rebuild_trigger(struct ds_pool *pool, struct rebuild_global_pool_tracker *rgt,
		d_rank_list_t *tgts_failed, d_rank_list_t *svc_list,
		uint32_t map_ver, daos_iov_t *map_buf)
{
	struct rebuild_scan_in	*rsi;
	struct rebuild_scan_out	*rso;
	crt_rpc_t		*rpc;
	d_sg_list_t		sgl;
	crt_bulk_t		bulk_hdl;
	int			rc;

	sgl.sg_nr = 1;
	sgl.sg_iovs = map_buf;
	rc = crt_bulk_create(dss_get_module_info()->dmi_ctx,
			     daos2crt_sg(&sgl), CRT_BULK_RW,
			     &bulk_hdl);
	if (rc != 0) {
		D_ERROR("Create bulk for map buffer failed: rc %d\n", rc);
		return rc;
	}

	/* Send rebuild RPC to all targets of the pool to initialize rebuild.
	 * XXX this should be idempotent as well as query and fini.
	 */
retry:
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_OBJECTS_SCAN, &rpc, bulk_hdl,
				  tgts_failed);
	if (rc != 0) {
		D_ERROR("pool map broad cast failed: rc %d\n", rc);
		D_GOTO(out_rpc, rc = 0); /* ignore the failure */
	}

	rsi = crt_req_get(rpc);
	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID"\n", DP_UUID(pool->sp_uuid));

	uuid_copy(rsi->rsi_pool_uuid, pool->sp_uuid);
	uuid_copy(rsi->rsi_pool_hdl_uuid, rgt->rgt_poh_uuid);
	uuid_copy(rsi->rsi_cont_hdl_uuid, rgt->rgt_coh_uuid);
	ds_iv_global_ns_get(pool->sp_iv_ns, &rsi->rsi_ns_iov);
	rsi->rsi_ns_id = pool->sp_iv_ns->iv_ns_id;
	rsi->rsi_pool_map_ver = map_ver;
	rsi->rsi_leader_term = rgt->rgt_leader_term;
	rsi->rsi_rebuild_ver = rgt->rgt_rebuild_ver;
	rsi->rsi_tgts_failed = tgts_failed;
	rsi->rsi_svc_list = svc_list;
	crt_group_rank(pool->sp_group,  &rsi->rsi_master_rank);
	rc = dss_rpc_send(rpc);
	if (rc != 0) {
		/* If it is network failure or timedout, let's refresh
		 * failure list and retry
		 */
		if ((rc == -DER_TIMEDOUT || daos_crt_network_error(rc)) &&
		    !rebuild_gst.rg_abort) {
			crt_req_decref(rpc);
			D_GOTO(retry, rc);
		}
		D_GOTO(out_rpc, rc);
	}

	rso = crt_reply_get(rpc);
	if (rso->rso_ranks_list != NULL) {
		int i;

		/* If the target failed to start rebuild, let's mark the
		 * the target DOWN, and schedule the rebuild for the
		 * target
		 */
		d_rank_list_dump(rso->rso_ranks_list, "failed starting rebuild",
				 strlen("failed starting rebuild"));

		for (i = 0; i < rso->rso_ranks_list->rl_nr; i++) {
			d_rank_list_t fail_rank_list = { 0 };

			fail_rank_list.rl_nr = 1;
			fail_rank_list.rl_ranks =
					&rso->rso_ranks_list->rl_ranks[i];

			rc = ds_pool_tgt_exclude(pool->sp_uuid, &fail_rank_list,
						 NULL);
			if (rc) {
				D_ERROR("Can not exclude rank %d\n",
					rso->rso_ranks_list->rl_ranks[i]);
				break;
			}

			rc = ds_rebuild_schedule(pool->sp_uuid,
					 pool_map_get_version(pool->sp_map),
					 &fail_rank_list, svc_list);
			if (rc != 0) {
				D_ERROR("rebuild fails rc %d\n", rc);
				break;
			}
		}
	}

	rc = rso->rso_status;
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start pool rebuild: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		D_GOTO(out_rpc, rc);
	}
out_rpc:
	crt_req_decref(rpc);
	crt_bulk_free(bulk_hdl);
	return rc;
}

static void
rpt_destroy(struct rebuild_tgt_pool_tracker *rpt)
{
	D_ASSERT(rpt->rt_refcount == 0);
	D_ASSERT(d_list_empty(&rpt->rt_list));
	if (!daos_handle_is_inval(rpt->rt_local_root_hdl))
		dbtree_destroy(rpt->rt_local_root_hdl);

	uuid_clear(rpt->rt_pool_uuid);
	if (rpt->rt_svc_list)
		daos_rank_list_free(rpt->rt_svc_list);

	if (rpt->rt_pool != NULL)
		ds_pool_put(rpt->rt_pool);

	if (rpt->rt_pullers) {
		int i;

		for (i = 0; i < rpt->rt_puller_nxs; i++) {
			struct rebuild_puller *puller;

			puller = &rpt->rt_pullers[i];

			D_ASSERT(puller->rp_ult == NULL);
			if (puller->rp_fini_cond)
				ABT_cond_free(&puller->rp_fini_cond);
			if (puller->rp_lock)
				ABT_mutex_free(&puller->rp_lock);
		}
		D_FREE(rpt->rt_pullers);
	}

	if (rpt->rt_lock)
		ABT_mutex_free(&rpt->rt_lock);

	if (rpt->rt_fini_lock)
		ABT_mutex_free(&rpt->rt_fini_lock);

	if (rpt->rt_fini_cond)
		ABT_cond_free(&rpt->rt_fini_cond);

	D_FREE_PTR(rpt);
}

void
rpt_get(struct rebuild_tgt_pool_tracker	*rpt)
{
	/* rpt_get should not be called once rpt is
	 * about to be destroyed.
	 */
	D_ASSERT(rpt->rt_finishing == 0);
	D_ASSERT(rpt->rt_refcount >= 0);
	rpt->rt_refcount++;

	D_DEBUG(DB_REBUILD, "rpt %p ref %d\n", rpt, rpt->rt_refcount);
}

void
rpt_put(struct rebuild_tgt_pool_tracker	*rpt)
{
	rpt->rt_refcount--;
	D_ASSERT(rpt->rt_refcount >= 0);
	D_DEBUG(DB_REBUILD, "rpt %p ref %d\n", rpt, rpt->rt_refcount);
	if (rpt->rt_refcount == 1 && rpt->rt_finishing) {
		ABT_mutex_lock(rpt->rt_fini_lock);
		ABT_cond_signal(rpt->rt_fini_cond);
		ABT_mutex_unlock(rpt->rt_fini_lock);
	} else if (rpt->rt_refcount == 0) {
		rpt_destroy(rpt);
	}
}

/**
 * Initiate the rebuild process, i.e. sending rebuild requests to every target
 * to find out the impacted objects.
 */
static int
rebuild_internal(struct ds_pool *pool, uint32_t rebuild_ver,
		 d_rank_list_t *tgts_failed, d_rank_list_t *svc_list,
		 struct rebuild_global_pool_tracker **p_rgt)
{
	uint32_t	map_ver;
	daos_iov_t	map_buf_iov = {0};
	uint64_t	leader_term;
	int		rc;

	D_DEBUG(DB_REBUILD, "rebuild "DF_UUID", rebuild version=%u\n",
		DP_UUID(pool->sp_uuid), rebuild_ver);

	rc = ds_pool_svc_term_get(pool->sp_uuid, &leader_term);
	if (rc) {
		D_ERROR("Get pool service term failed: rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = rebuild_prepare(pool, rebuild_ver, leader_term, tgts_failed,
			     p_rgt);
	if (rc) {
		D_ERROR("rebuild prepare failed: rc %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = ds_pool_map_buf_get(pool->sp_uuid, &map_buf_iov, &map_ver);
	if (rc) {
		D_ERROR("pool map broadcast failed: rc %d\n", rc);
		D_GOTO(out, rc);
	}

	/* broadcast scan RPC to all targets */
	rc = rebuild_trigger(pool, *p_rgt, tgts_failed, svc_list, map_ver,
			     &map_buf_iov);
	if (rc) {
		D_ERROR("object scan failed: rc %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

static void
rebuild_one_ult(void *arg)
{
	struct rebuild_task	  *task = arg;
	struct ds_pool_create_arg pc_arg;
	struct ds_pool		  *pool;
	struct rebuild_global_pool_tracker *rgt = NULL;
	struct rebuild_iv	  iv;
	int			  rc;

	memset(&pc_arg, 0, sizeof(pc_arg));
	pc_arg.pca_map_version = task->dst_map_ver;
	rc = ds_pool_lookup_create(task->dst_pool_uuid, &pc_arg, &pool);
	if (rc) {
		D_ERROR("pool lookup and create failed: rc %d\n", rc);
		return;
	}

	D_PRINT("Rebuild [started] (pool "DF_UUID" ver=%u)\n",
		 DP_UUID(task->dst_pool_uuid), task->dst_map_ver);

	rc = rebuild_internal(pool, task->dst_map_ver, task->dst_tgts_failed,
			      task->dst_svc_list, &rgt);
	if (rc != 0) {
		D_ERROR(""DF_UUID" (ver=%u) rebuild failed: rc %d\n",
			DP_UUID(task->dst_pool_uuid), task->dst_map_ver, rc);
		D_GOTO(out, rc);
	}

	/* Wait until rebuild finished */
	rebuild_status_check(pool, task->dst_map_ver, rgt);
	if (rebuild_gst.rg_abort && !rgt->rgt_done)
		D_GOTO(out, rc);

	rc = ds_pool_tgt_exclude_out(pool->sp_uuid, task->dst_tgts_failed,
				     NULL);
	D_DEBUG(DB_REBUILD, "mark failed target %d of "DF_UUID" as DOWNOUT\n",
		task->dst_tgts_failed->rl_ranks[0],
		DP_UUID(task->dst_pool_uuid));

	memset(&iv, 0, sizeof(iv));
	uuid_copy(iv.riv_pool_uuid, task->dst_pool_uuid);
	iv.riv_master_rank = pool->sp_iv_ns->iv_master_rank;
	iv.riv_ver = rgt->rgt_rebuild_ver;
	iv.riv_global_done = 1;
	iv.riv_leader_term = rgt->rgt_leader_term;

	rc = rebuild_iv_update(pool->sp_iv_ns,
			       &iv, CRT_IV_SHORTCUT_NONE,
			       CRT_IV_SYNC_LAZY);
out:
	ds_pool_put(pool);
	if (rgt)
		rebuild_global_pool_tracker_destroy(rgt);

	d_list_del(&task->dst_list);
	daos_rank_list_free(task->dst_tgts_failed);
	daos_rank_list_free(task->dst_svc_list);
	D_FREE_PTR(task);
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

			rc = dss_ult_create(rebuild_one_ult, task, -1, NULL);
			if (rc == 0) {
				rebuild_gst.rg_inflight++;
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
				   dst_list) {
		d_list_del(&task->dst_list);
		daos_rank_list_free(task->dst_tgts_failed);
		daos_rank_list_free(task->dst_svc_list);
		D_FREE_PTR(task);
	}

	ABT_mutex_lock(rebuild_gst.rg_lock);
	ABT_cond_signal(rebuild_gst.rg_stop_cond);
	rebuild_gst.rg_rebuild_running = 0;
	ABT_mutex_unlock(rebuild_gst.rg_lock);
}

void
ds_rebuild_leader_stop()
{
	ABT_mutex_lock(rebuild_gst.rg_lock);
	if (!rebuild_gst.rg_rebuild_running) {
		ABT_mutex_unlock(rebuild_gst.rg_lock);
		return;
	}

	/* This will eliminate all of the queued rebuild task, then abort all
	 * running rebuild. Note: this only abort the rebuild tracking ULT
	 * (rebuild_one_ult), and the real rebuild process on each target
	 * triggered by scan/object request are still running. Once the new
	 * leader is elected, it will send those rebuild trigger req with new
	 * term, then each target will only need update its leader information
	 * and report the rebuild status to the new leader.
	 * If the new leader never comes, then those rebuild process can still
	 * finish, but those tracking ULT (rebuild_tgt_status_check) will keep
	 * sending the status report to the stale leader, until it is aborted.
	 */
	rebuild_gst.rg_abort = 1;
	if (rebuild_gst.rg_rebuild_running)
		ABT_cond_wait(rebuild_gst.rg_stop_cond,
			      rebuild_gst.rg_lock);
	ABT_mutex_unlock(rebuild_gst.rg_lock);
	if (rebuild_gst.rg_stop_cond)
		ABT_cond_free(&rebuild_gst.rg_stop_cond);
}

/**
 * Add rebuild task to the rebuild list and another ULT will rebuild the
 * pool.
 */
int
ds_rebuild_schedule(const uuid_t uuid, uint32_t map_ver,
		    d_rank_list_t *tgts_failed, d_rank_list_t *svc_list)
{
	struct rebuild_task	*task;
	int			rc;

	D_ALLOC_PTR(task);
	if (task == NULL)
		return -DER_NOMEM;

	task->dst_map_ver = map_ver;
	uuid_copy(task->dst_pool_uuid, uuid);
	D_INIT_LIST_HEAD(&task->dst_list);

	rc = daos_rank_list_dup(&task->dst_tgts_failed, tgts_failed);
	if (rc) {
		D_FREE_PTR(task);
		return rc;
	}

	rc = daos_rank_list_dup(&task->dst_svc_list, svc_list);
	if (rc) {
		D_FREE_PTR(task);
		return rc;
	}

	D_PRINT("Rebuild [queued] ("DF_UUID" ver=%u) failed rank %u\n",
		 DP_UUID(uuid), map_ver, tgts_failed->rl_ranks[0]);
	d_list_add_tail(&task->dst_list, &rebuild_gst.rg_queue_list);

	if (!rebuild_gst.rg_rebuild_running) {
		rc = ABT_cond_create(&rebuild_gst.rg_stop_cond);
		if (rc != ABT_SUCCESS)
			D_GOTO(free, rc = dss_abterr2der(rc));

		rebuild_gst.rg_rebuild_running = 1;
		rc = dss_ult_create(rebuild_ults, NULL, -1, NULL);
		if (rc) {
			ABT_cond_free(&rebuild_gst.rg_stop_cond);
			rebuild_gst.rg_rebuild_running = 0;
			D_GOTO(free, rc);
		}
	}
free:
	if (rc) {
		d_list_del(&task->dst_list);
		daos_rank_list_free(task->dst_tgts_failed);
		daos_rank_list_free(task->dst_svc_list);
		D_FREE_PTR(task);
	}
	return rc;
}

/* Regenerate the rebuild tasks when changing the leader. */
int
ds_rebuild_regenerate_task(struct ds_pool *pool, d_rank_list_t *svc_list)
{
	struct pool_target *down_tgts;
	unsigned int	down_tgts_cnt;
	unsigned int	i;
	int		rc;

	rebuild_gst.rg_abort = 0;

	/* get all down targets */
	rc = pool_map_find_down_tgts(pool->sp_map, &down_tgts,
				     &down_tgts_cnt);
	if (rc != 0) {
		D_ERROR("failed to create failed tgt list rc %d\n", rc);
		return rc;
	}

	if (down_tgts_cnt == 0)
		return 0;

	for (i = 0; i < down_tgts_cnt; i++) {
		struct pool_target *tgt = &down_tgts[i];
		d_rank_list_t	   rank_list;
		d_rank_t	   rank;

		rank = tgt->ta_comp.co_rank;
		rank_list.rl_nr = 1;
		rank_list.rl_ranks = &rank;

		rc = ds_rebuild_schedule(pool->sp_uuid, tgt->ta_comp.co_fseq,
					 &rank_list, svc_list);
		if (rc) {
			D_ERROR(DF_UUID" schedule ver %d failed: rc %d\n",
				DP_UUID(pool->sp_uuid), tgt->ta_comp.co_fseq,
				rc);
			break;
		}
	}

	return rc;
}

static int
rebuild_fini_one(void *arg)
{
	struct rebuild_tgt_pool_tracker	*rpt = arg;
	struct rebuild_pool_tls		*pool_tls;

	pool_tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid,
					   rpt->rt_rebuild_ver);
	if (pool_tls == NULL)
		return 0;

	D_DEBUG(DB_REBUILD, "close container/pool "DF_UUID"/"DF_UUID"\n",
		DP_UUID(rpt->rt_coh_uuid), DP_UUID(rpt->rt_poh_uuid));

	if (!daos_handle_is_inval(pool_tls->rebuild_pool_hdl)) {
		dc_pool_local_close(pool_tls->rebuild_pool_hdl);
		pool_tls->rebuild_pool_hdl = DAOS_HDL_INVAL;
	}

	rebuild_pool_tls_destroy(pool_tls);

	ds_cont_local_close(rpt->rt_coh_uuid);

	return 0;
}

int
rebuild_tgt_fini(struct rebuild_tgt_pool_tracker *rpt)
{
	int	i;
	int	rc;

	D_DEBUG(DB_REBUILD, "Finalize rebuild for "DF_UUID", map_ver=%u\n",
		DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver);

	d_list_del_init(&rpt->rt_list);
	rpt->rt_finishing = 1;
	/* Wait until all ult/tasks finish and release the rpt.
	 * NB: Because rebuild_tgt_fini will be only called in
	 * rebuild_tgt_status_check, which will make sure if
	 * rt_refcount reaches to 1, either all rebuild is done or
	 * all ult/task has been aborted by rt_abort, i.e. no new
	 * ULT/task will be created after this check. So it is safe
	 * to destroy the rpt after this.
	 */
	if (rpt->rt_refcount > 1) {
		ABT_mutex_lock(rpt->rt_fini_lock);
		ABT_cond_wait(rpt->rt_fini_cond, rpt->rt_fini_lock);
		ABT_mutex_unlock(rpt->rt_fini_lock);
	}

	/* Check each puller */
	for (i = 0; i < rpt->rt_puller_nxs; i++) {
		struct rebuild_puller	*puller;
		struct rebuild_dkey	*dkey;
		struct rebuild_dkey	*tmp;

		puller = &rpt->rt_pullers[i];

		ABT_mutex_lock(puller->rp_lock);
		if (puller->rp_ult_running)
			ABT_cond_wait(puller->rp_fini_cond, puller->rp_lock);
		ABT_mutex_unlock(puller->rp_lock);

		if (puller->rp_ult) {
			ABT_thread_free(&puller->rp_ult);
			puller->rp_ult = NULL;
		}

		/* since the dkey thread has been stopped, so we do not
		 * need lock here
		 */
		d_list_for_each_entry_safe(dkey, tmp, &puller->rp_dkey_list,
					   rd_list) {
			d_list_del(&dkey->rd_list);
			D_WARN(DF_UUID" left rebuild dkey %*.s\n",
			       DP_UUID(rpt->rt_pool_uuid),
			      (int)dkey->rd_dkey.iov_len,
			      (char *)dkey->rd_dkey.iov_buf);
			daos_iov_free(&dkey->rd_dkey);
			D_FREE_PTR(dkey);
		}
	}

	/* close the rebuild pool/container */
	rc = dss_task_collective(rebuild_fini_one, rpt);

	rpt_put(rpt);

	return rc;
}

#define RBLD_CHECK_INTV		2	/* seocnds interval to check puller */
void
rebuild_tgt_status_check(void *arg)
{
	struct rebuild_tgt_pool_tracker	*rpt = arg;
	double				last_query = 0;
	double				now;

	D_ASSERT(rpt != NULL);
	while (1) {
		struct rebuild_iv		iv;
		struct rebuild_tgt_query_info	status;
		int				rc;

		now = ABT_get_wtime();
		if (now - last_query < RBLD_CHECK_INTV) {
			/* Yield to other ULTs */
			ABT_thread_yield();
			continue;
		}

		last_query = now;
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
			rpt->rt_abort = 1;
		}

		memset(&iv, 0, sizeof(iv));
		uuid_copy(iv.riv_pool_uuid, rpt->rt_pool_uuid);

		iv.riv_obj_count = status.obj_count;
		iv.riv_rec_count = status.rec_count;
		iv.riv_status = status.status;
		if (status.scanning == 0 || rpt->rt_abort)
			iv.riv_scan_done = 1;

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
						   CRT_IV_SYNC_NONE);
			if (rc)
				D_WARN("rebuild tgt iv update failed: %d\n",
					rc);
		}

		D_DEBUG(DB_REBUILD, "ver %d obj "DF_U64" rec "DF_U64
			"scan done %d pull done %d scan gl done %d"
			" gl done %d status %d\n",
			rpt->rt_rebuild_ver, iv.riv_obj_count,
			iv.riv_rec_count, iv.riv_scan_done, iv.riv_pull_done,
			rpt->rt_global_scan_done, rpt->rt_global_done,
			iv.riv_status);

		if (rpt->rt_global_done || rpt->rt_abort)
			break;
	}

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
	int				rc;

	pool_tls = rebuild_pool_tls_create(rpt->rt_pool_uuid,
					   rpt->rt_rebuild_ver);
	if (pool_tls == NULL)
		return -DER_NOMEM;

	pool_tls->rebuild_pool_scanning = 1;
	pool_tls->rebuild_pool_rec_count = 0;
	pool_tls->rebuild_pool_obj_count = 0;

	uuid_copy(pool_tls->rebuild_poh_uuid, rpt->rt_poh_uuid);
	uuid_copy(pool_tls->rebuild_coh_uuid, rpt->rt_coh_uuid);
	/* Create ds_container locally */
	rc = ds_cont_local_open(rpt->rt_pool_uuid, rpt->rt_coh_uuid,
				NULL, 0, NULL);
	if (rc)
		pool_tls->rebuild_pool_status = rc;

	D_DEBUG(DB_REBUILD, "open local container "DF_UUID"/"DF_UUID"\n",
		DP_UUID(rpt->rt_pool_uuid), DP_UUID(rpt->rt_coh_uuid));
	return rc;
}

static int
rpt_create(struct ds_pool *pool, d_rank_list_t *svc_list, uint32_t pm_ver,
	   uint64_t leader_term, struct rebuild_tgt_pool_tracker **p_rpt)
{
	struct rebuild_tgt_pool_tracker	*rpt;
	d_rank_t	rank;
	int		i;
	int		rc;

	D_ALLOC_PTR(rpt);
	if (rpt == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&rpt->rt_list);
	rc = ABT_mutex_create(&rpt->rt_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(free, rc = dss_abterr2der(rc));

	rc = ABT_mutex_create(&rpt->rt_fini_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(free, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&rpt->rt_fini_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(free, rc = dss_abterr2der(rc));

	/* Initialize per-thread counters */
	rpt->rt_puller_nxs = dss_get_threads_number();
	D_ALLOC(rpt->rt_pullers, rpt->rt_puller_nxs *
				  sizeof(*rpt->rt_pullers));
	if (!rpt->rt_pullers)
		D_GOTO(free, rc = -DER_NOMEM);

	for (i = 0; i < rpt->rt_puller_nxs; i++) {
		struct rebuild_puller *puller;

		puller = &rpt->rt_pullers[i];
		D_INIT_LIST_HEAD(&puller->rp_dkey_list);
		rc = ABT_mutex_create(&puller->rp_lock);
		if (rc != ABT_SUCCESS)
			D_GOTO(free, rc = dss_abterr2der(rc));

		rc = ABT_cond_create(&puller->rp_fini_cond);
		if (rc != ABT_SUCCESS)
			D_GOTO(free, rc = dss_abterr2der(rc));
	}

	uuid_copy(rpt->rt_pool_uuid, pool->sp_uuid);
	daos_rank_list_dup(&rpt->rt_svc_list, svc_list);
	rpt->rt_lead_puller_running = 0;
	rpt->rt_rebuild_ver = pm_ver;
	rpt->rt_leader_term = leader_term;
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
	struct ds_pool_create_arg	pc_arg = { 0 };
	struct rebuild_tgt_pool_tracker	*rpt = NULL;
	daos_iov_t			iov = { 0 };
	daos_sg_list_t			sgl;
	int				rc;

	/* lookup create the ds_pool first */
	if (rpc->cr_co_bulk_hdl == NULL) {
		D_ERROR("No pool map in scan rpc\n");
		return -DER_INVAL;
	}

	D_DEBUG(DB_REBUILD, "prepare rebuild for "DF_UUID"/%d/%d\n",
		DP_UUID(rsi->rsi_pool_uuid), rsi->rsi_pool_map_ver,
		rsi->rsi_rebuild_ver);

	/* Note: if ds_pool already exists, for example the pool
	 * is opened, then pca_need_group, pca_map will have zero
	 * effects, i.e. sp_map & sp_group might be NULL in this
	 * case. So let's do extra checking in the following.
	 */
	pc_arg.pca_map_version = rsi->rsi_pool_map_ver;
	rc = ds_pool_lookup_create(rsi->rsi_pool_uuid, &pc_arg, &pool);
	if (rc != 0) {
		D_ERROR("Can not find pool.\n");
		return rc;
	}

	/* update the pool map */
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 1;
	sgl.sg_iovs = &iov;
	rc = crt_bulk_access(rpc->cr_co_bulk_hdl, daos2crt_sg(&sgl));
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_pool_tgt_map_update(pool, iov.iov_buf, rsi->rsi_pool_map_ver);
	if (rc != 0)
		D_GOTO(out, rc);

	/* Then check sp_group */
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

	/* Create rpt for the target */
	rc = rpt_create(pool, rsi->rsi_svc_list, rsi->rsi_rebuild_ver,
			rsi->rsi_leader_term, &rpt);
	if (rc)
		D_GOTO(out, rc);

	uuid_copy(rpt->rt_poh_uuid, rsi->rsi_pool_hdl_uuid);
	uuid_copy(rpt->rt_coh_uuid, rsi->rsi_cont_hdl_uuid);

	D_DEBUG(DB_REBUILD, "rebuild coh/poh "DF_UUID"/"DF_UUID"\n",
		DP_UUID(rpt->rt_coh_uuid), DP_UUID(rpt->rt_poh_uuid));

	rc = ds_pool_iv_ns_update(pool, rsi->rsi_master_rank,
				  &rsi->rsi_ns_iov, rsi->rsi_ns_id);
	if (rc)
		D_GOTO(out, rc);

	rc = dss_task_collective(rebuild_prepare_one, rpt);
	if (rc)
		D_GOTO(out, rc);

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

	return rc;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler rebuild_handlers[] = {
	{
		.dr_opc		= REBUILD_OBJECTS_SCAN,
		.dr_hdlr	= rebuild_tgt_scan_handler,
		.dr_corpc_ops	= {
			.co_aggregate	= rebuild_tgt_scan_aggregator,
			.co_pre_forward	= NULL,
		}
	}, {
		.dr_opc		= REBUILD_OBJECTS,
		.dr_hdlr	= rebuild_obj_handler
	}, {
		.dr_opc		= 0
	}
};

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
	if (rebuild_gst.rg_stop_cond)
		ABT_cond_free(&rebuild_gst.rg_stop_cond);

	ABT_mutex_free(&rebuild_gst.rg_lock);

	rebuild_iv_fini();
	return 0;
}

struct dss_module rebuild_module =  {
	.sm_name	= "rebuild",
	.sm_mod_id	= DAOS_REBUILD_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_srv_rpcs	= rebuild_rpcs,
	.sm_handlers	= rebuild_handlers,
	.sm_key		= &rebuild_module_key,
};
