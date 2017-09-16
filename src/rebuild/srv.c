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
#define DDSUBSYS	DDFAC(rebuild)

#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/iv.h>
#include "rpc.h"
#include "rebuild_internal.h"

#define RBLD_BCAST_INTV		2	/* seocnds interval to retry bcast */
#define RBLD_BCAST_RETRY_MAX	100	/* more than 3 full cart timeout */

/* rebuild global status */
struct rebuild_globals rebuild_gst;

static int
init(void)
{
	int	rc;

	memset(&rebuild_gst, 0, sizeof(rebuild_gst));
	DAOS_INIT_LIST_HEAD(&rebuild_gst.rg_task_list);

	rc = ABT_mutex_create(&rebuild_gst.rg_lock);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	rc = ds_iv_key_type_register(IV_REBUILD, &rebuild_iv_ops);
	if (rc)
		return rc;

	return 0;
}

static int
fini(void)
{
	int i;

	if (rebuild_gst.rg_stop_cond)
		ABT_cond_free(&rebuild_gst.rg_stop_cond);

	ds_iv_key_type_unregister(IV_REBUILD);
	if (rebuild_gst.rg_pullers) {
		for (i = 0; i < rebuild_gst.rg_puller_nxs; i++) {
			struct rebuild_puller *puller;

			puller = &rebuild_gst.rg_pullers[i];

			if (puller->rp_fini_cond)
				ABT_cond_free(&puller->rp_fini_cond);
			if (puller->rp_lock)
				ABT_mutex_free(&puller->rp_lock);
		}
		D__FREE(rebuild_gst.rg_pullers,
		       rebuild_gst.rg_puller_nxs *
		       sizeof(*rebuild_gst.rg_pullers));
	}
	ABT_mutex_free(&rebuild_gst.rg_lock);
	return 0;
}

struct pool_map *
rebuild_pool_map_get(void)
{
	struct ds_pool  *pool = rebuild_gst.rg_pool;
	struct pool_map *map = NULL;

	D__ASSERT(pool);
	D__ASSERT(pool->sp_map != NULL);
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

/* Initialize rebuild global structure */
int
rebuild_globals_init(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		     uuid_t cont_hdl_uuid, d_rank_list_t *svc_list,
		     uint32_t pm_ver)
{
	d_rank_t rank;
	int i;

	/* Initialize per-thread counters */
	if (!rebuild_gst.rg_pullers) {
		rebuild_gst.rg_puller_nxs = dss_get_threads_number();
		D__ALLOC(rebuild_gst.rg_pullers,
			rebuild_gst.rg_puller_nxs *
			sizeof(*rebuild_gst.rg_pullers));
		if (!rebuild_gst.rg_pullers)
			return -DER_NOMEM;

		for (i = 0; i < rebuild_gst.rg_puller_nxs; i++) {
			struct rebuild_puller *puller;
			int rc;

			puller = &rebuild_gst.rg_pullers[i];
			DAOS_INIT_LIST_HEAD(&puller->rp_dkey_list);
			rc = ABT_mutex_create(&puller->rp_lock);
			if (rc != ABT_SUCCESS)
				return dss_abterr2der(rc);

			rc = ABT_cond_create(&puller->rp_fini_cond);
			if (rc != ABT_SUCCESS)
				return dss_abterr2der(rc);
		}
	} else {
		for (i = 0; i < rebuild_gst.rg_puller_nxs; i++) {
			struct rebuild_puller *puller;

			puller = &rebuild_gst.rg_pullers[i];
			puller->rp_inflight = 0;
			puller->rp_ult_running = 0;
			D__ASSERT(daos_list_empty(&puller->rp_dkey_list));
			D__ASSERT(puller->rp_ult == NULL);
		}
	}

	uuid_copy(rebuild_gst.rg_pool_uuid, pool_uuid);
	if (uuid_is_null(rebuild_gst.rg_pool_hdl_uuid))
		uuid_copy(rebuild_gst.rg_pool_hdl_uuid, pool_hdl_uuid);
	else
		D__ASSERT(uuid_compare(rebuild_gst.rg_pool_hdl_uuid,
				      pool_hdl_uuid) == 0);

	if (uuid_is_null(rebuild_gst.rg_cont_hdl_uuid))
		uuid_copy(rebuild_gst.rg_cont_hdl_uuid, cont_hdl_uuid);
	else
		D__ASSERT(uuid_compare(rebuild_gst.rg_cont_hdl_uuid,
				      cont_hdl_uuid) == 0);
	daos_rank_list_dup(&rebuild_gst.rg_svc_list, svc_list, true);
	rebuild_gst.rg_puller_running = 0;
	rebuild_gst.rg_done = 0;
	rebuild_gst.rg_rec_count = 0;
	rebuild_gst.rg_obj_count = 0;
	rebuild_gst.rg_rebuild_ver = pm_ver;
	/* FIXME add group later */
	crt_group_rank(NULL, &rank);
	rebuild_gst.rg_rank = rank;
	return 0;
}

static void *
rebuild_tls_init(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key)
{
	struct rebuild_tls *tls;

	D__ALLOC_PTR(tls);
	return tls;
}

bool
is_rebuild_container(uuid_t pool_uuid, uuid_t cont_hdl_uuid)
{
	struct rebuild_iv	rebuild_iv;
	struct ds_pool		*pool;
	int			rc;

	if (!uuid_is_null(rebuild_gst.rg_cont_hdl_uuid)) {
		D__DEBUG(DB_TRACE, "rebuild_gst "DF_UUID" cont_hdl_uuid "
			 DF_UUID"\n", DP_UUID(rebuild_gst.rg_cont_hdl_uuid),
			 DP_UUID(cont_hdl_uuid));
		return !uuid_compare(rebuild_gst.rg_cont_hdl_uuid,
				     cont_hdl_uuid);
	}

	/* If the I/O request arrives before the rebuild container
	 * setup, let's fetch the rebuild container uuid.
	 */
	pool = ds_pool_lookup(pool_uuid);
	if (pool == NULL) {
		D__DEBUG(DB_TRACE, "pool "DF_UUID" lookup failed:\n",
			 DP_UUID(pool_uuid));
		return false;
	}

	D__ASSERT(pool->sp_iv_ns != NULL);
	rc = rebuild_iv_fetch(pool->sp_iv_ns, &rebuild_iv);
	ds_pool_put(pool);
	if (rc) {
		D__ERROR("iv fetch "DF_UUID" failed %d\n",
			 DP_UUID(pool->sp_uuid), rc);
		return false;
	}

	if (uuid_compare(rebuild_iv.riv_coh_uuid, cont_hdl_uuid) == 0)
		return true;

	return false;
}

bool
is_rebuild_pool(uuid_t pool_hdl)
{
	return !uuid_compare(rebuild_gst.rg_pool_hdl_uuid, pool_hdl);
}

static void
rebuild_tls_fini(const struct dss_thread_local_storage *dtls,
		 struct dss_module_key *key, void *data)
{
	struct rebuild_tls *tls = data;

	D__FREE_PTR(tls);
}

int
dss_rebuild_check_scanning(void *arg)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	struct rebuild_tgt_query_info	*status = arg;

	ABT_mutex_lock(status->lock);
	if (tls->rebuild_scanning)
		status->scanning++;
	if (tls->rebuild_status != 0 && status->status == 0)
		status->status = tls->rebuild_status;
	status->rec_count += tls->rebuild_rec_count;
	status->obj_count += tls->rebuild_obj_count;
	ABT_mutex_unlock(status->lock);

	return 0;
}

int
rebuild_tgt_query(struct rebuild_tgt_query_info *status)
{
	int	rc;

	/* let's check scanning status on every thread*/
	ABT_mutex_lock(rebuild_gst.rg_lock);
	rc = dss_task_collective(dss_rebuild_check_scanning, status);
	if (rc) {
		ABT_mutex_unlock(rebuild_gst.rg_lock);
		D__GOTO(out, rc);
	}

	if (!status->scanning && !rebuild_gst.rg_puller_running) {
		int	i;
		/* then check pulling status*/
		for (i = 0; i < rebuild_gst.rg_puller_nxs; i++) {
			struct rebuild_puller *puller;

			puller = &rebuild_gst.rg_pullers[i];
			ABT_mutex_lock(puller->rp_lock);
			if (daos_list_empty(&puller->rp_dkey_list) &&
			    puller->rp_inflight == 0) {
				ABT_mutex_unlock(puller->rp_lock);
				continue;
			}
			ABT_mutex_unlock(puller->rp_lock);

			D__DEBUG(DB_TRACE,
				"thread %d rebuilding is still busy.\n", i);
			status->rebuilding = true;
			break;
		}
	} else {
		status->rebuilding = true;
	}
	ABT_mutex_unlock(rebuild_gst.rg_lock);

	D__DEBUG(DB_TRACE, "pool "DF_UUID" scanning %d/%d rebuilding=%s, "
		"obj_count=%d, rec_count=%d\n",
		DP_UUID(rebuild_gst.rg_pool_uuid), status->scanning,
		status->status, status->rebuilding ? "yes" : "no",
		status->obj_count, status->rec_count);
out:
	return rc;
}

int
ds_rebuild_query(uuid_t pool_uuid, struct daos_rebuild_status *status)
{
	struct rebuild_iv iv;
	int		rc;

	memset(status, 0, sizeof(*status));
	memset(&iv, 0, sizeof(iv));
	status->rs_version = rebuild_gst.rg_rebuild_ver;
	if (status->rs_version == 0 || rebuild_gst.rg_pool == NULL) {
		if (daos_list_empty(&rebuild_gst.rg_task_list))
			status->rs_done = 1;
		D__DEBUG(DB_TRACE, "No rebuild in progress, rebuild_task %s\n",
			 status->rs_done ? "no" : "yes");
		D_GOTO(out, rc = 0);
	}

	ABT_mutex_lock(rebuild_gst.rg_lock);
	memcpy(status, &rebuild_gst.rg_status, sizeof(*status));
	ABT_mutex_unlock(rebuild_gst.rg_lock);

	rc = rebuild_iv_fetch(rebuild_gst.rg_pool->sp_iv_ns, &iv);
	if (rc) {
		D__ERROR(""DF_UUID" rebuild iv fetch failed: rc %d\n",
			 DP_UUID(rebuild_gst.rg_pool_uuid), rc);
		D__GOTO(out, rc);
	}

	status->rs_rec_nr = iv.riv_rec_count;
	status->rs_obj_nr = iv.riv_obj_count;
out:
	status->rs_errno = rc == 0 ? iv.riv_status : rc;
	return rc;
}

#define RBLD_SBUF_LEN	256

enum {
	RB_BCAST_NONE,
	RB_BCAST_MAP,
	RB_BCAST_QUERY,
};

static void
rebuild_status_check(struct ds_pool *pool, uint32_t map_ver)
{
	unsigned int	failed_tgts_cnt;
	double		 begin = ABT_get_wtime();
	double		 last_print = 0;
	double		 last_query = 0;
	double		 now;
	unsigned int	 total;
	int		 rc;

	/* FIXME add group later */
	rc = crt_group_size(NULL, &total);
	if (rc)
		return;

	rc = pool_map_find_failed_tgts(pool->sp_map, NULL,
				       &failed_tgts_cnt);
	if (rc != 0) {
		D__ERROR("failed to create failed tgt list rc %d\n", rc);
		return;
	}

	total -= failed_tgts_cnt;

	while (1) {
		char	sbuf[RBLD_SBUF_LEN];
		char	*str;
		bool	done = false;

		now = ABT_get_wtime();
		if (now - last_query < RBLD_BCAST_INTV) {
			/* Yield to other ULTs */
			ABT_thread_yield();
			continue;
		}

		last_query = now;
		/* query the current rebuild status */
		if (rebuild_gst.rg_done >= total)
			done = true;

		if (done)
			str = rebuild_gst.rg_abort ? "failed" : "completed";
		else if (rebuild_gst.rg_obj_count == 0 &&
			 rebuild_gst.rg_rec_count == 0)
			str = "scanning";
		else
			str = "pulling";

		snprintf(sbuf, RBLD_SBUF_LEN,
			"Rebuild [%s] (ver=%u, obj="DF_U64", rec="DF_U64
			", done %d total %d duration=%d secs)\n", str, map_ver,
			rebuild_gst.rg_obj_count, rebuild_gst.rg_rec_count,
			(int)rebuild_gst.rg_done, total, (int)(now - begin));

		D__DEBUG(DB_TRACE, "%s", sbuf);
		if (done) {
			D__PRINT("%s", sbuf);
			break;
		}

		/* print something at least for each 10 secons */
		if (now - last_print > 10) {
			last_print = now;
			D__PRINT("%s", sbuf);
		}
	};
}

/* To notify all targets to prepare the rebuild */
static int
rebuild_prepare(struct ds_pool *pool, uint32_t map_ver,
		d_rank_list_t *exclude_tgts)
 {
	struct rebuild_iv	rebuild_iv;
	unsigned int		master_rank;
	int			rc;

	crt_group_rank(NULL, &master_rank);
	if (pool->sp_iv_ns == NULL ||
	    pool->sp_iv_ns->iv_master_rank != master_rank) {
		/* Check and setup iv class ns if needed, which
		 * happens for off-line rebuild (no pool connect)
		 * or leader has been changed. */
		/* Destroy the previous IV ns */
		if (pool->sp_iv_ns != NULL) {
			ds_iv_ns_destroy(pool->sp_iv_ns);
			pool->sp_iv_ns = NULL;
		}

		rc = rebuild_iv_ns_create(pool, exclude_tgts, master_rank);
		if (rc)
			return rc;

		D__DEBUG(DB_TRACE, "pool "DF_UUID" create rebuild iv\n",
			 DP_UUID(pool->sp_uuid));
	}

	memset(&rebuild_iv, 0, sizeof(rebuild_iv));
	if (uuid_is_null(rebuild_gst.rg_pool_hdl_uuid))
		uuid_generate(rebuild_iv.riv_poh_uuid);
	else
		uuid_copy(rebuild_iv.riv_poh_uuid,
			  rebuild_gst.rg_pool_hdl_uuid);

	if (uuid_is_null(rebuild_gst.rg_cont_hdl_uuid))
		uuid_generate(rebuild_iv.riv_coh_uuid);
	else
		uuid_copy(rebuild_iv.riv_coh_uuid,
			  rebuild_gst.rg_cont_hdl_uuid);

	D__DEBUG(DB_TRACE, "rebuild coh/poh "DF_UUID"/"DF_UUID"\n",
		 DP_UUID(rebuild_iv.riv_coh_uuid),
		 DP_UUID(rebuild_iv.riv_poh_uuid));

	if (uuid_is_null(rebuild_gst.rg_pool_hdl_uuid) ||
	    uuid_is_null(rebuild_gst.rg_cont_hdl_uuid))
		rc = rebuild_iv_update(pool->sp_iv_ns, &rebuild_iv,
					  CRT_IV_SHORTCUT_NONE,
					  CRT_IV_SYNC_LAZY);
	return rc;
}

static int
rebuild_scan(struct ds_pool *pool, d_rank_list_t *tgts_failed,
	     d_rank_list_t *svc_list, unsigned int map_ver)
{
	struct rebuild_scan_in	*rsi;
	struct rebuild_out	*ro;
	crt_rpc_t		*rpc;
	int			rc;

	/* Send rebuild RPC to all targets of the pool to initialize rebuild.
	 * XXX this should be idempotent as well as query and fini.
	 */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_OBJECTS_SCAN, &rpc, NULL,
				  tgts_failed);
	if (rc != 0) {
		D__ERROR("pool map broad cast failed: rc %d\n", rc);
		D__GOTO(out_rpc, rc = 0); /* ignore the failure */
	}

	rsi = crt_req_get(rpc);
	D__DEBUG(DB_TRACE, "rebuild "DF_UUID"\n", DP_UUID(pool->sp_uuid));

	uuid_copy(rsi->rsi_pool_uuid, pool->sp_uuid);
	rsi->rsi_pool_map_ver = map_ver;
	rsi->rsi_tgts_failed = tgts_failed;
	rsi->rsi_svc_list = svc_list;
	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D__GOTO(out_rpc, rc);

	ro = crt_reply_get(rpc);
	rc = ro->ro_status;
	if (rc != 0) {
		D__ERROR(DF_UUID": failed to start pool rebuild: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		D__GOTO(out_rpc, rc);
	}
out_rpc:
       crt_req_decref(rpc);
       return rc;
}

/**
 * Initiate the rebuild process, i.e. sending rebuild requests to every target
 * to find out the impacted objects.
 */
static int
rebuild_internal(struct ds_pool *pool, uint32_t map_ver,
		 d_rank_list_t *tgts_failed, d_rank_list_t *svc_list)
{
	int rc;

	D__DEBUG(DB_TRACE, "rebuild "DF_UUID", map version=%u\n",
		 DP_UUID(pool->sp_uuid), map_ver);

	rc = rebuild_prepare(pool, map_ver, tgts_failed);
	if (rc) {
		D__ERROR("rebuild prepare failed: rc %d\n", rc);
		D__GOTO(out, rc);
	}

	rc = ds_pool_map_update(pool->sp_uuid, tgts_failed);
	if (rc) {
		D__ERROR("pool map broadcast failed: rc %d\n", rc);
		D__GOTO(out, rc);
	}

	/* broadcast scan RPC to all targets */
	rc = rebuild_scan(pool, tgts_failed, svc_list, map_ver);
	if (rc) {
		D__ERROR("object scan failed: rc %d\n", rc);
		D__GOTO(out, rc);
	}

	D_EXIT;
out:
	return rc;
}

struct rebuild_task {
	daos_list_t	dst_list;
	uuid_t		dst_pool_uuid;
	d_rank_list_t	*dst_tgts_failed;
	d_rank_list_t	*dst_svc_list;
	uint32_t	dst_map_ver;
};

static int
rebuild_one(uuid_t pool_uuid, uint32_t map_ver,
	    d_rank_list_t *tgts_failed, d_rank_list_t *svc_list)
{
	struct ds_pool_create_arg pc_arg;
	struct ds_pool		  *pool;
	int			  rc;

	memset(&pc_arg, 0, sizeof(pc_arg));
	pc_arg.pca_map_version = map_ver;
	rc = ds_pool_lookup_create(pool_uuid, &pc_arg, &pool);
	if (rc) {
		D__ERROR("pool lookup and create failed: rc %d\n", rc);
		return rc;
	}

	D__PRINT("Rebuild [started] (ver=%u)\n", map_ver);
	rc = rebuild_internal(pool, map_ver, tgts_failed, svc_list);
	if (rc != 0) {
		D__ERROR(""DF_UUID" (ver=%u) rebuild failed: rc %d\n",
			DP_UUID(pool_uuid), map_ver, rc);
		D__GOTO(out, rc);
	}

	/* Wait until rebuild finished */
	rebuild_status_check(pool, map_ver);

	rc = ds_pool_tgt_exclude_out(pool->sp_uuid, tgts_failed, NULL);
	D__DEBUG(DB_TRACE, "mark failed target %d of "DF_UUID" as DOWNOUT\n",
		 tgts_failed->rl_ranks[0], DP_UUID(pool_uuid));
	D_EXIT;
out:
	ds_pool_put(pool);
	return rc;
}

static void
rebuild_ult(void *arg)
{
	struct rebuild_task	*task;
	struct rebuild_task	*task_tmp;
	int			 rc;

	/* rebuild all failures one by one */
	while (!daos_list_empty(&rebuild_gst.rg_task_list)) {
		task = daos_list_entry(rebuild_gst.rg_task_list.next,
				       struct rebuild_task, dst_list);
		daos_list_del(&task->dst_list);

		ABT_mutex_lock(rebuild_gst.rg_lock);
		memset(&rebuild_gst.rg_status, 0,
		       sizeof(rebuild_gst.rg_status));

		rebuild_gst.rg_status.rs_version = task->dst_map_ver;
		ABT_mutex_unlock(rebuild_gst.rg_lock);

		rc = rebuild_one(task->dst_pool_uuid, task->dst_map_ver,
				 task->dst_tgts_failed, task->dst_svc_list);
		if (rc != 0)
			D__ERROR(""DF_UUID" rebuild failed: rc %d\n",
				DP_UUID(task->dst_pool_uuid), rc);

		daos_rank_list_free(task->dst_tgts_failed);
		daos_rank_list_free(task->dst_svc_list);
		D__FREE_PTR(task);

		if (rebuild_gst.rg_abort)
			break;
		ABT_thread_yield();
	}

	memset(&rebuild_gst.rg_status, 0, sizeof(rebuild_gst.rg_status));

	/* Delete tasks if it is forced abort */
	daos_list_for_each_entry_safe(task, task_tmp,
				      &rebuild_gst.rg_task_list,
				      dst_list) {
		daos_list_del(&task->dst_list);
		daos_rank_list_free(task->dst_tgts_failed);
		daos_rank_list_free(task->dst_svc_list);
		D__FREE_PTR(task);
	}

	ABT_mutex_lock(rebuild_gst.rg_lock);
	ABT_cond_signal(rebuild_gst.rg_stop_cond);
	rebuild_gst.rg_rebuild_running = 0;
	ABT_mutex_unlock(rebuild_gst.rg_lock);
}

void
ds_rebuild_stop()
{
	ABT_mutex_lock(rebuild_gst.rg_lock);
	if (!rebuild_gst.rg_rebuild_running) {
		ABT_mutex_unlock(rebuild_gst.rg_lock);
		return;
	}

	rebuild_gst.rg_abort = 1;
	if (rebuild_gst.rg_rebuild_running)
		ABT_cond_wait(rebuild_gst.rg_stop_cond,
			      rebuild_gst.rg_lock);
	ABT_mutex_unlock(rebuild_gst.rg_lock);
	if (rebuild_gst.rg_stop_cond)
		ABT_cond_free(&rebuild_gst.rg_stop_cond);
	rebuild_gst.rg_abort = 0;
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

	D__ALLOC_PTR(task);
	if (task == NULL)
		return -DER_NOMEM;

	task->dst_map_ver = map_ver;
	uuid_copy(task->dst_pool_uuid, uuid);
	DAOS_INIT_LIST_HEAD(&task->dst_list);

	rc = daos_rank_list_dup(&task->dst_tgts_failed, tgts_failed, true);
	if (rc) {
		D__FREE_PTR(task);
		return rc;
	}

	rc = daos_rank_list_dup(&task->dst_svc_list, svc_list, true);
	if (rc) {
		D__FREE_PTR(task);
		return rc;
	}

	D__PRINT("Rebuild [queued] (ver=%u) failed rank %u\n", map_ver,
		tgts_failed->rl_ranks[0]);
	daos_list_add_tail(&task->dst_list, &rebuild_gst.rg_task_list);

	if (!rebuild_gst.rg_rebuild_running) {
		rc = ABT_cond_create(&rebuild_gst.rg_stop_cond);
		if (rc != ABT_SUCCESS)
			D__GOTO(free, rc = dss_abterr2der(rc));

		rebuild_gst.rg_rebuild_running = 1;
		rc = dss_ult_create(rebuild_ult, NULL, -1, NULL);
		if (rc) {
			ABT_cond_free(&rebuild_gst.rg_stop_cond);
			rebuild_gst.rg_rebuild_running = 0;
			D__GOTO(free, rc);
		}
	}
free:
	if (rc) {
		daos_list_del(&task->dst_list);
		daos_rank_list_free(task->dst_tgts_failed);
		daos_rank_list_free(task->dst_svc_list);
		D__FREE_PTR(task);
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

	/* get all down targets */
	rc = pool_map_find_down_tgts(pool->sp_map, &down_tgts,
				     &down_tgts_cnt);
	if (rc != 0) {
		D__ERROR("failed to create failed tgt list rc %d\n", rc);
		return rc;
	}

	if (down_tgts_cnt == 0)
		return 0;

	for (i = 0; i < down_tgts_cnt; i++) {
		struct pool_target *tgt = &down_tgts[i];
		d_rank_list_t	   rank_list;
		d_rank_t	   rank;

		rank = tgt->ta_comp.co_rank;
		rank_list.rl_nr.num = 1;
		rank_list.rl_nr.num_out = 0;
		rank_list.rl_ranks = &rank;

		rc = ds_rebuild_schedule(pool->sp_uuid, tgt->ta_comp.co_fseq,
					 &rank_list, svc_list);
		if (rc) {
			D__ERROR(DF_UUID" schedule ver %d failed: rc %d\n",
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
	struct rebuild_tls	*tls = rebuild_tls_get();

	D__DEBUG(DB_TRACE, "close container/pool "DF_UUID"/"DF_UUID"\n",
		DP_UUID(rebuild_gst.rg_cont_hdl_uuid),
		DP_UUID(rebuild_gst.rg_pool_hdl_uuid));

	if (!daos_handle_is_inval(tls->rebuild_pool_hdl)) {
		dc_pool_local_close(tls->rebuild_pool_hdl);
		tls->rebuild_pool_hdl = DAOS_HDL_INVAL;
	}

	ds_cont_local_close(rebuild_gst.rg_cont_hdl_uuid);

	return 0;
}

static int
rebuild_tgt_fini(void)
{
	struct ds_pool	*pool;
	int		i;
	int		rc;

	ABT_mutex_lock(rebuild_gst.rg_lock);
	D__DEBUG(DB_TRACE, "Finalize rebuild for "DF_UUID", map_ver=%u\n",
		 rebuild_gst.rg_pool_uuid, rebuild_gst.rg_rebuild_ver);

	rebuild_gst.rg_finishing = 1;
	rebuild_gst.rg_rebuild_ver = 0;

	/* Check each puller */
	for (i = 0; i < rebuild_gst.rg_puller_nxs; i++) {
		struct rebuild_puller	*puller;
		struct rebuild_dkey	*dkey;
		struct rebuild_dkey	*tmp;

		puller = &rebuild_gst.rg_pullers[i];

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
		daos_list_for_each_entry_safe(dkey, tmp, &puller->rp_dkey_list,
					      rd_list) {
			daos_list_del(&dkey->rd_list);
			D__WARN(DF_UUID" left rebuild dkey %*.s\n",
			       DP_UUID(rebuild_gst.rg_pool_uuid),
			       (int)dkey->rd_dkey.iov_len,
			       (char *)dkey->rd_dkey.iov_buf);
			daos_iov_free(&dkey->rd_dkey);
			D__FREE_PTR(dkey);
		}
	}

	/* close the rebuild pool/container */
	rc = dss_task_collective(rebuild_fini_one, NULL);

	pool = rebuild_gst.rg_pool;
	rebuild_gst.rg_pool = NULL;
	rebuild_gst.rg_abort = 0;
	ABT_mutex_unlock(rebuild_gst.rg_lock);

	if (!daos_handle_is_inval(rebuild_gst.rg_local_root_hdl)) {
		dbtree_destroy(rebuild_gst.rg_local_root_hdl);
		rebuild_gst.rg_local_root_hdl = DAOS_HDL_INVAL;
	}

	uuid_clear(rebuild_gst.rg_pool_uuid);

	daos_rank_list_free(rebuild_gst.rg_svc_list);
	rebuild_gst.rg_svc_list = NULL;

	D__ASSERT(pool);
	ds_pool_put(pool);
	return rc;
}

#define RBLD_CHECK_INTV		2	/* seocnds interval to check puller */
void
rebuild_tgt_status_check(void *arg)
{
	double	last_query = 0;
	double	now;

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
		rc = rebuild_tgt_query(&status);
		ABT_mutex_free(&status.lock);
		if (rc || status.status != 0) {
			D__ERROR(DF_UUID" rebuild failed: rc %d\n",
				 DP_UUID(rebuild_gst.rg_pool_uuid),
				 rc == 0 ? status.status : rc);
			if (status.status == 0)
				status.status = rc;
			if (rebuild_gst.rg_status.rs_errno == 0)
				rebuild_gst.rg_status.rs_errno = status.status;
			rebuild_gst.rg_abort = 1;
		}

		memset(&iv, 0, sizeof(iv));
		uuid_copy(iv.riv_poh_uuid, rebuild_gst.rg_pool_hdl_uuid);
		uuid_copy(iv.riv_coh_uuid, rebuild_gst.rg_cont_hdl_uuid);
		/* FIXME: it should reset the obj/rec count after update iv */
		iv.riv_obj_count = status.obj_count;
		iv.riv_rec_count = status.rec_count;
		iv.riv_status = status.status;
		if (!status.rebuilding)
			iv.riv_done = 1;

		iv.riv_rank = rebuild_gst.rg_rank;
		/* Cart does not support failure recovery yet, let's send
		 * the status to root for now. FIXME
		 */
		rc = rebuild_iv_update(rebuild_gst.rg_pool->sp_iv_ns,
				       &iv, CRT_IV_SHORTCUT_TO_ROOT,
				       CRT_IV_SYNC_NONE);
		if (rc) {
			rebuild_gst.rg_abort = 1;
			if (rebuild_gst.rg_status.rs_errno == 0)
				rebuild_gst.rg_status.rs_errno = rc;
		}

		D__DEBUG(DB_TRACE, "ver %d obj "DF_U64" rec "DF_U64
			 " done %d status %d\n", rebuild_gst.rg_rebuild_ver,
			 iv.riv_obj_count, iv.riv_rec_count, iv.riv_done,
			 iv.riv_status);

		if (rebuild_gst.rg_abort || iv.riv_done) {
			rebuild_tgt_fini();
			break;
		}
	}
}

struct rebuild_prepare_arg {
	uuid_t	pool_uuid;
	uuid_t  pool_hdl_uuid;
	uuid_t	cont_hdl_uuid;
	d_rank_list_t *svc_list;
};

/**
 * To avoid broadcasting during pool_connect and container
 * open for rebuild, let's create a local ds_pool/ds_container
 * and dc_pool/dc_container, so rebuild client will always
 * use the specified pool_hdl/container_hdl uuid during
 * rebuild.
 **/
static int
rebuild_prepare_one(void *data)
{
	struct rebuild_tls		*tls = rebuild_tls_get();
	int				rc;

	tls->rebuild_scanning = 1;
	tls->rebuild_rec_count = 0;
	tls->rebuild_obj_count = 0;

	/* Create ds_container locally */
	rc = ds_cont_local_open(rebuild_gst.rg_pool_uuid,
				rebuild_gst.rg_cont_hdl_uuid,
				NULL, 0, NULL);
	if (rc)
		tls->rebuild_status = rc;

	D__DEBUG(DB_TRACE, "open local container "DF_UUID"/"DF_UUID"\n",
		 DP_UUID(rebuild_gst.rg_pool_uuid),
		 DP_UUID(rebuild_gst.rg_cont_hdl_uuid));
	return rc;
}

/* rebuild prepare */
int
rebuild_tgt_prepare(uuid_t pool_uuid, d_rank_list_t *svc_list,
		    unsigned int pmap_ver)
{
	struct ds_pool			*pool;
	struct rebuild_iv		rebuild_iv;
	struct ds_pool_create_arg	pc_arg;
	int				rc;

	D__DEBUG(DB_TRACE, "prepare rebuild for "DF_UUID"/%d\n",
		 DP_UUID(pool_uuid), pmap_ver);

	/* Create and hold ds_pool until rebuild finish,
	 * and the ds_pool will be released in ds_rebuild_fini().
	 * Since there is no pool map yet, let's create ds_pool
	 * with 0 version.
	 */
	memset(&pc_arg, 0, sizeof(pc_arg));
	rc = ds_pool_lookup_create(pool_uuid, &pc_arg, &pool);
	if (rc != 0)
		D__GOTO(out, rc);

	D__ASSERT(pool->sp_iv_ns != NULL);
	rc = rebuild_iv_fetch(pool->sp_iv_ns, &rebuild_iv);
	if (rc)
		D_GOTO(out, rc);

	D__DEBUG(DB_TRACE, "rebuild coh/poh "DF_UUID"/"DF_UUID"\n",
		 DP_UUID(rebuild_iv.riv_coh_uuid),
		 DP_UUID(rebuild_iv.riv_poh_uuid));

	rc = rebuild_globals_init(pool->sp_uuid, rebuild_iv.riv_poh_uuid,
				  rebuild_iv.riv_coh_uuid, svc_list, pmap_ver);
	if (rc)
		D_GOTO(out, rc);

	rc = dss_task_collective(rebuild_prepare_one, NULL);
	if (rc)
		D__GOTO(out, rc);

	rebuild_gst.rg_finishing = 0;

	ABT_mutex_lock(rebuild_gst.rg_lock);
	if (rebuild_gst.rg_pool == NULL)
		/* For off-line rebuild, rg_pool will be set in
		 * rebuild_iv_ns_handler()
		 */
		rebuild_gst.rg_pool = pool; /* pin it */
	ABT_mutex_unlock(rebuild_gst.rg_lock);
	D__DEBUG(DB_TRACE, "rebuild coh/poh "DF_UUID"/"DF_UUID"\n",
		 DP_UUID(rebuild_gst.rg_cont_hdl_uuid),
		 DP_UUID(rebuild_gst.rg_pool_hdl_uuid));
out:
	if (rc)
		ds_pool_put(pool);

	return rc;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler rebuild_handlers[] = {
	{
		.dr_opc		= REBUILD_IV_NS_CREATE,
		.dr_hdlr	= rebuild_iv_ns_handler
	}, {
		.dr_opc		= REBUILD_OBJECTS_SCAN,
		.dr_hdlr	= rebuild_tgt_scan_handler
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
