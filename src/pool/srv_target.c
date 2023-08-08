/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_pool: Target Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related target states.
 *
 * Data structures used here:
 *
 *                 Pool           Container
 *
 *         Global  ds_pool
 *                 ds_pool_hdl
 *
 *   Thread-local  ds_pool_child  ds_cont
 *                                ds_cont_hdl
 */

#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>

#include <gurt/telemetry_producer.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include <daos/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/vos.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/srv_csum.h>
#include "rpc.h"
#include "srv_internal.h"

/* ds_pool_child **************************************************************/

static void
stop_gc_ult(struct ds_pool_child *child)
{
	D_ASSERT(child != NULL);
	/* GC ULT is not started */
	if (child->spc_gc_req == NULL)
		return;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Stopping GC ULT\n",
		DP_UUID(child->spc_uuid), dss_get_module_info()->dmi_tgt_id);

	sched_req_wait(child->spc_gc_req, true);
	sched_req_put(child->spc_gc_req);
	child->spc_gc_req = NULL;
}

static void
stop_flush_ult(struct ds_pool_child *child)
{
	D_ASSERT(child != NULL);
	/* Flush ULT is not started */
	if (child->spc_flush_req == NULL)
		return;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Stopping Flush ULT\n",
		DP_UUID(child->spc_uuid), dss_get_module_info()->dmi_tgt_id);

	sched_req_wait(child->spc_flush_req, true);
	sched_req_put(child->spc_flush_req);
	child->spc_flush_req = NULL;
}

struct ds_pool_child *
ds_pool_child_lookup(const uuid_t uuid)
{
	struct ds_pool_child   *child;
	struct pool_tls	       *tls = pool_tls_get();

	d_list_for_each_entry(child, &tls->dt_pool_list, spc_list) {
		if (uuid_compare(uuid, child->spc_uuid) == 0) {
			child->spc_ref++;
			return child;
		}
	}
	return NULL;
}

struct ds_pool_child *
ds_pool_child_get(struct ds_pool_child *child)
{
	child->spc_ref++;
	return child;
}

void
ds_pool_child_put(struct ds_pool_child *child)
{
	D_ASSERTF(child->spc_ref > 0, "%d\n", child->spc_ref);
	child->spc_ref--;
	if (child->spc_ref == 0) {
		D_DEBUG(DB_MGMT, DF_UUID": destroying\n",
			DP_UUID(child->spc_uuid));
		D_ASSERT(d_list_empty(&child->spc_list));
		D_ASSERT(d_list_empty(&child->spc_cont_list));

		ds_stop_chkpt_ult(child);
		/* only stop gc ULT when all ops ULTs are done */
		stop_gc_ult(child);
		stop_flush_ult(child);

		vos_pool_close(child->spc_hdl);
		dss_module_fini_metrics(DAOS_TGT_TAG, child->spc_metrics);
		ABT_eventual_set(child->spc_ref_eventual,
				 (void *)&child->spc_ref,
				 sizeof(child->spc_ref));
	}
}

static int
gc_rate_ctl(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct ds_pool		*pool = child->spc_pool;
	struct sched_request	*req = child->spc_gc_req;

	if (dss_ult_exiting(req))
		return -1;

	/* Let GC ULT run in tight mode when system is idle */
	if (!dss_xstream_is_busy()) {
		sched_req_yield(req);
		return 0;
	}

	/*
	 * When it's under space pressure, GC will continue run in slack mode
	 * no matter what reclaim policy is used, otherwise, it'll take an extra
	 * sleep to minimize the performance impact.
	 */
	if (sched_req_space_check(req) == SCHED_SPACE_PRESS_NONE) {
		uint32_t msecs;

		msecs = (pool->sp_reclaim == DAOS_RECLAIM_LAZY ||
			 pool->sp_reclaim == DAOS_RECLAIM_DISABLED) ? 2000 : 50;
		sched_req_sleep(req, msecs);
	} else {
		sched_req_yield(req);
	}

	/* Let GC ULT run in slack mode when system is busy */
	return 1;
}

static void
gc_ult(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	int			 rc;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: GC ULT started\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);

	if (child->spc_gc_req == NULL)
		goto out;

	while (!dss_ult_exiting(child->spc_gc_req)) {
		rc = vos_gc_pool(child->spc_hdl, -1, gc_rate_ctl, (void *)child);
		if (rc < 0)
			D_ERROR(DF_UUID"[%d]: GC pool run failed. "DF_RC"\n",
				DP_UUID(child->spc_uuid), dmi->dmi_tgt_id,
				DP_RC(rc));

		if (dss_ult_exiting(child->spc_gc_req))
			break;

		/* It'll be woke up by container destroy or aggregation */
		if (rc > 0)
			sched_req_yield(child->spc_gc_req);
		else
			sched_req_sleep(child->spc_gc_req, 10UL * 1000);
	}
out:
	D_DEBUG(DB_MGMT, DF_UUID"[%d]: GC ULT stopped\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
}

static int
start_gc_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_gc_req == NULL);

	sched_req_attr_init(&attr, SCHED_REQ_GC, &child->spc_uuid);
	attr.sra_flags = SCHED_REQ_FL_NO_DELAY;

	child->spc_gc_req = sched_create_ult(&attr, gc_ult, child, DSS_DEEP_STACK_SZ);
	if (child->spc_gc_req == NULL) {
		D_ERROR(DF_UUID"[%d]: Failed to create GC ULT.\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		return -DER_NOMEM;
	}

	return 0;
}

static void
flush_ult(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	uint32_t		 sleep_ms, nr_flushed, nr_flush = 6000;
	int			 rc;

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Flush ULT started\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);

	D_ASSERT(child->spc_flush_req != NULL);

	while (!dss_ult_exiting(child->spc_flush_req)) {
		rc = vos_flush_pool(child->spc_hdl, false, nr_flush, &nr_flushed);
		if (rc < 0) {
			D_ERROR(DF_UUID"[%d]: Flush pool failed. "DF_RC"\n",
				DP_UUID(child->spc_uuid), dmi->dmi_tgt_id, DP_RC(rc));
			sleep_ms = 2000;
		} else if (rc) {	/* This pool doesn't have NVMe partition */
			sleep_ms = 60000;
		} else if (sched_req_space_check(child->spc_flush_req) == SCHED_SPACE_PRESS_NONE) {
			sleep_ms = 5000;
		} else {
			sleep_ms = (nr_flushed < nr_flush) ? 1000 : 0;
		}

		if (dss_ult_exiting(child->spc_flush_req))
			break;

		if (sleep_ms)
			sched_req_sleep(child->spc_flush_req, sleep_ms);
		else
			sched_req_yield(child->spc_flush_req);
	}

	D_DEBUG(DB_MGMT, DF_UUID"[%d]: Flush ULT stopped\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
}

static int
start_flush_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_flush_req == NULL);

	sched_req_attr_init(&attr, SCHED_REQ_GC, &child->spc_uuid);
	attr.sra_flags = SCHED_REQ_FL_NO_DELAY;

	child->spc_flush_req = sched_create_ult(&attr, flush_ult, child, DSS_DEEP_STACK_SZ);
	if (child->spc_flush_req == NULL) {
		D_ERROR(DF_UUID"[%d]: Failed to create flush ULT.\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		return -DER_NOMEM;
	}

	return 0;
}

struct pool_child_lookup_arg {
	struct ds_pool *pla_pool;
	void	       *pla_uuid;
	uint32_t	pla_map_version;
};

/*
 * Called via dss_thread_collective() to create and add the ds_pool_child object
 * for one thread. This opens the matching VOS pool.
 */
static int
pool_child_add_one(void *varg)
{
	struct pool_child_lookup_arg   *arg = varg;
	struct pool_tls		       *tls = pool_tls_get();
	struct ds_pool_child	       *child;
	struct dss_module_info	       *info = dss_get_module_info();
	char			       *path;
	int				rc;

	child = ds_pool_child_lookup(arg->pla_uuid);
	if (child != NULL) {
		ds_pool_child_put(child);
		return 0;
	}

	D_DEBUG(DB_MGMT, DF_UUID": creating\n", DP_UUID(arg->pla_uuid));

	D_ALLOC_PTR(child);
	if (child == NULL)
		return -DER_NOMEM;

	/* initialize metrics on the target xstream for each module */
	rc = dss_module_init_metrics(DAOS_TGT_TAG, child->spc_metrics,
				     arg->pla_pool->sp_path, info->dmi_tgt_id);
	if (rc != 0) {
		D_ERROR(DF_UUID ": failed to initialize module metrics for pool"
			"." DF_RC "\n", DP_UUID(child->spc_uuid), DP_RC(rc));
		goto out_free;
	}

	rc = ds_mgmt_tgt_file(arg->pla_uuid, VOS_FILE, &info->dmi_tgt_id,
			      &path);
	if (rc != 0)
		goto out_metrics;

	D_ASSERT(child->spc_metrics[DAOS_VOS_MODULE] != NULL);
	rc = vos_pool_open_metrics(path, arg->pla_uuid, VOS_POF_EXCL | VOS_POF_EXTERNAL_FLUSH,
				   child->spc_metrics[DAOS_VOS_MODULE], &child->spc_hdl);

	D_FREE(path);

	if (rc != 0)
		goto out_metrics;

	uuid_copy(child->spc_uuid, arg->pla_uuid);
	child->spc_map_version = arg->pla_map_version;
	child->spc_ref = 1; /* 1 for the list */

	rc = ABT_eventual_create(sizeof(child->spc_ref),
				 &child->spc_ref_eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out_vos;
	}

	child->spc_pool = arg->pla_pool;
	D_INIT_LIST_HEAD(&child->spc_list);
	D_INIT_LIST_HEAD(&child->spc_cont_list);

	rc = start_gc_ult(child);
	if (rc != 0)
		goto out_eventual;

	rc = start_flush_ult(child);
	if (rc != 0)
		goto out_gc;

	rc = ds_start_scrubbing_ult(child);
	if (rc != 0)
		goto out_flush;

	rc = ds_start_chkpt_ult(child);
	if (rc != 0)
		goto out_scrub;

	d_list_add(&child->spc_list, &tls->dt_pool_list);

	/* Load all containers */
	rc = ds_cont_child_start_all(child);
	if (rc)
		goto out_list;

	return 0;

out_list:
	d_list_del_init(&child->spc_list);
	ds_cont_child_stop_all(child);
	ds_stop_chkpt_ult(child);
out_scrub:
	ds_stop_scrubbing_ult(child);
out_flush:
	stop_flush_ult(child);
out_gc:
	stop_gc_ult(child);
out_eventual:
	ABT_eventual_free(&child->spc_ref_eventual);
out_vos:
	vos_pool_close(child->spc_hdl);
out_metrics:
	dss_module_fini_metrics(DAOS_TGT_TAG, child->spc_metrics);
out_free:
	D_FREE(child);
	return rc;
}

/*
 * Called via dss_thread_collective() to delete the ds_pool_child object for one
 * thread. If nobody else is referencing this object, then its VOS pool handle
 * is closed and the object itself is freed.
 */
static int
pool_child_delete_one(void *uuid)
{
	struct ds_pool_child *child;
	int *ref, rc;

	child = ds_pool_child_lookup(uuid);
	if (child == NULL)
		return 0;

	D_ASSERT(d_list_empty(&child->spc_cont_list));
	d_list_del_init(&child->spc_list);
	ds_stop_chkpt_ult(child);
	ds_stop_scrubbing_ult(child);
	ds_pool_child_put(child); /* -1 for the list */

	ds_pool_child_put(child); /* -1 for lookup */

	rc = ABT_eventual_wait(child->spc_ref_eventual, (void **)&ref);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	ABT_eventual_free(&child->spc_ref_eventual);

	/* ds_pool_child must be freed here to keep
	 * spc_ref_enventual usage safe
	 */
	D_FREE(child);

	return 0;
}

/* ds_pool ********************************************************************/

static struct daos_lru_cache   *pool_cache;

static inline struct ds_pool *
pool_obj(struct daos_llink *llink)
{
	return container_of(llink, struct ds_pool, sp_entry);
}

struct ds_pool_create_arg {
	uint32_t	pca_map_version;
};

static int
pool_alloc_ref(void *key, unsigned int ksize, void *varg,
	       struct daos_llink **link)
{
	struct ds_pool_create_arg      *arg = varg;
	struct ds_pool		       *pool;
	struct pool_child_lookup_arg	collective_arg;
	char				group_id[DAOS_UUID_STR_SIZE];
	struct dss_module_info	       *info = dss_get_module_info();
	unsigned int			iv_ns_id;
	int				rc;
	int				rc_tmp;

	if (arg == NULL) {
		/* The caller doesn't want to create a ds_pool object. */
		rc = -DER_NONEXIST;
		goto err;
	}

	D_DEBUG(DB_MGMT, DF_UUID": creating\n", DP_UUID(key));

	D_ALLOC_PTR(pool);
	if (pool == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	rc = ABT_rwlock_create(&pool->sp_lock);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_pool, rc = dss_abterr2der(rc));

	rc = ABT_mutex_create(&pool->sp_mutex);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_lock, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&pool->sp_fetch_hdls_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_mutex, rc = dss_abterr2der(rc));

	rc = ABT_cond_create(&pool->sp_fetch_hdls_done_cond);
	if (rc != ABT_SUCCESS)
		D_GOTO(err_cond, rc = dss_abterr2der(rc));

	D_INIT_LIST_HEAD(&pool->sp_ec_ephs_list);
	uuid_copy(pool->sp_uuid, key);
	pool->sp_map_version = arg->pca_map_version;
	pool->sp_reclaim = DAOS_RECLAIM_LAZY; /* default reclaim strategy */
	pool->sp_policy_desc.policy =
			DAOS_MEDIA_POLICY_IO_SIZE; /* default tiering policy */

	/** set up ds_pool metrics */
	rc = ds_pool_metrics_start(pool);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to set up ds_pool metrics: %d\n",
			DP_UUID(key), rc);
		goto err_done_cond;
	}

	uuid_unparse_lower(key, group_id);
	rc = crt_group_secondary_create(group_id, NULL /* primary_grp */,
					NULL /* ranks */, &pool->sp_group);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool group: %d\n",
			DP_UUID(key), rc);
		goto err_metrics;
	}

	rc = ds_iv_ns_create(info->dmi_ctx, pool->sp_uuid, pool->sp_group,
			     &iv_ns_id, &pool->sp_iv_ns);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool IV NS: %d\n",
			DP_UUID(key), rc);
		goto err_group;
	}

	collective_arg.pla_pool = pool;
	collective_arg.pla_uuid = key;
	collective_arg.pla_map_version = arg->pca_map_version;
	rc = dss_thread_collective(pool_child_add_one, &collective_arg, DSS_ULT_DEEP_STACK);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to add ES pool caches: "DF_RC"\n",
			DP_UUID(key), DP_RC(rc));
		goto err_iv_ns;
	}

	*link = &pool->sp_entry;
	return 0;

err_iv_ns:
	ds_iv_ns_put(pool->sp_iv_ns);
err_group:
	rc_tmp = crt_group_secondary_destroy(pool->sp_group);
	if (rc_tmp != 0)
		D_ERROR(DF_UUID": failed to destroy pool group: "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc_tmp));
err_metrics:
	ds_pool_metrics_stop(pool);
err_done_cond:
	ABT_cond_free(&pool->sp_fetch_hdls_done_cond);
err_cond:
	ABT_cond_free(&pool->sp_fetch_hdls_cond);
err_mutex:
	ABT_mutex_free(&pool->sp_mutex);
err_lock:
	ABT_rwlock_free(&pool->sp_lock);
err_pool:
	D_FREE(pool);
err:
	return rc;
}

static void
pool_free_ref(struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);
	int		rc;

	D_DEBUG(DB_MGMT, DF_UUID": freeing\n", DP_UUID(pool->sp_uuid));

	rc = dss_thread_collective(pool_child_delete_one, pool->sp_uuid, 0);
	if (rc == -DER_CANCELED)
		D_DEBUG(DB_MD, DF_UUID": no ESs\n", DP_UUID(pool->sp_uuid));
	else if (rc != 0)
		D_ERROR(DF_UUID": failed to delete ES pool caches: "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));

	pl_map_disconnect(pool->sp_uuid);
	if (pool->sp_map != NULL)
		pool_map_decref(pool->sp_map);

	ds_iv_ns_put(pool->sp_iv_ns);

	rc = crt_group_secondary_destroy(pool->sp_group);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to destroy pool group: %d\n",
			DP_UUID(pool->sp_uuid), rc);

	/** release metrics */
	ds_pool_metrics_stop(pool);

	ABT_cond_free(&pool->sp_fetch_hdls_cond);
	ABT_cond_free(&pool->sp_fetch_hdls_done_cond);
	ABT_mutex_free(&pool->sp_mutex);
	ABT_rwlock_free(&pool->sp_lock);
	D_FREE(pool);
}

static bool
pool_cmp_keys(const void *key, unsigned int ksize, struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);

	return uuid_compare(key, pool->sp_uuid) == 0;
}

static uint32_t
pool_rec_hash(struct daos_llink *llink)
{
	struct ds_pool *pool = pool_obj(llink);

	return d_hash_string_u32((const char *)pool->sp_uuid, sizeof(uuid_t));
}

static struct daos_llink_ops pool_cache_ops = {
	.lop_alloc_ref	= pool_alloc_ref,
	.lop_free_ref	= pool_free_ref,
	.lop_cmp_keys	= pool_cmp_keys,
	.lop_rec_hash	= pool_rec_hash,
};

int
ds_pool_cache_init(void)
{
	int rc;

	rc = daos_lru_cache_create(-1 /* bits */, D_HASH_FT_NOLOCK /* feats */,
				   &pool_cache_ops, &pool_cache);
	return rc;
}

void
ds_pool_cache_fini(void)
{
	daos_lru_cache_destroy(pool_cache);
}

/**
 * If the pool can not be found due to non-existence or it is being stopped, then
 * @pool will be set to NULL and return proper failure code, otherwise return 0 and
 * set @pool.
 */
int
ds_pool_lookup(const uuid_t uuid, struct ds_pool **pool)
{
	struct daos_llink	*llink;
	int			 rc;

	D_ASSERT(pool != NULL);
	*pool = NULL;
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t),
			       NULL /* create_args */, &llink);
	if (rc != 0)
		return rc;

	*pool = pool_obj(llink);
	if ((*pool)->sp_stopping) {
		D_DEBUG(DB_MD, DF_UUID": is in stopping\n", DP_UUID(uuid));
		ds_pool_put(*pool);
		*pool = NULL;
		return -DER_SHUTDOWN;
	}

	return 0;
}

void
ds_pool_get(struct ds_pool *pool)
{
	D_ASSERT(pool != NULL);
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	daos_lru_ref_add(&pool->sp_entry);
}

void
ds_pool_put(struct ds_pool *pool)
{
	D_ASSERT(pool != NULL);
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	daos_lru_ref_release(pool_cache, &pool->sp_entry);
}

void
pool_fetch_hdls_ult(void *data)
{
	struct ds_pool	*pool = data;
	int		rc = 0;

	/* sp_map == NULL means the IV ns is not setup yet, i.e.
	 * the pool leader does not broadcast the pool map to the
	 * current node yet, see pool_iv_pre_sync().
	 */
	ABT_mutex_lock(pool->sp_mutex);
	if (pool->sp_map == NULL)
		ABT_cond_wait(pool->sp_fetch_hdls_cond, pool->sp_mutex);
	ABT_mutex_unlock(pool->sp_mutex);

	if (pool->sp_stopping) {
		D_DEBUG(DB_MD, DF_UUID": skip fetching hdl due to stop\n",
			DP_UUID(pool->sp_uuid));
		D_GOTO(out, rc);
	}
	rc = ds_pool_iv_conn_hdl_fetch(pool);
	if (rc) {
		D_ERROR("iv conn fetch %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	ABT_mutex_lock(pool->sp_mutex);
	ABT_cond_signal(pool->sp_fetch_hdls_done_cond);
	ABT_mutex_unlock(pool->sp_mutex);

	pool->sp_fetch_hdls = 0;
}

static void
tgt_ec_eph_query_ult(void *data)
{
	ds_cont_tgt_ec_eph_query_ult(data);
}

static int
ds_pool_start_ec_eph_query_ult(struct ds_pool *pool)
{
	struct sched_req_attr	attr;
	uuid_t			anonym_uuid;

	if (unlikely(ec_agg_disabled))
		return 0;

	D_ASSERT(pool->sp_ec_ephs_req == NULL);
	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);
	pool->sp_ec_ephs_req = sched_create_ult(&attr, tgt_ec_eph_query_ult, pool,
						DSS_DEEP_STACK_SZ);
	if (pool->sp_ec_ephs_req == NULL) {
		D_ERROR(DF_UUID": failed create ec eph equery ult.\n",
			DP_UUID(pool->sp_uuid));
		return -DER_NOMEM;
	}

	return 0;
}

static void
ds_pool_tgt_ec_eph_query_abort(struct ds_pool *pool)
{
	if (pool->sp_ec_ephs_req == NULL)
		return;

	D_DEBUG(DB_MD, DF_UUID": Stopping EC query ULT\n",
		DP_UUID(pool->sp_uuid));

	sched_req_wait(pool->sp_ec_ephs_req, true);
	sched_req_put(pool->sp_ec_ephs_req);
	pool->sp_ec_ephs_req = NULL;
	D_INFO(DF_UUID": EC query ULT stopped\n", DP_UUID(pool->sp_uuid));
}

static void
pool_fetch_hdls_ult_abort(struct ds_pool *pool)
{
	if (!pool->sp_fetch_hdls) {
		D_INFO(DF_UUID": fetch hdls ULT aborted\n", DP_UUID(pool->sp_uuid));
		return;
	}

	ABT_mutex_lock(pool->sp_mutex);
	ABT_cond_signal(pool->sp_fetch_hdls_cond);
	ABT_mutex_unlock(pool->sp_mutex);

	ABT_mutex_lock(pool->sp_mutex);
	ABT_cond_wait(pool->sp_fetch_hdls_done_cond, pool->sp_mutex);
	ABT_mutex_unlock(pool->sp_mutex);
	D_INFO(DF_UUID": fetch hdls ULT aborted\n", DP_UUID(pool->sp_uuid));
}

/*
 * Start a pool. Must be called on the system xstream. Hold the ds_pool object
 * till ds_pool_stop. Only for mgmt and pool modules.
 */
int
ds_pool_start(uuid_t uuid)
{
	struct ds_pool			*pool;
	struct daos_llink		*llink;
	struct ds_pool_create_arg	arg = {};
	int				rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	/*
	 * Look up the pool without create_args (see pool_alloc_ref) to see if
	 * the pool is started already.
	 */
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t),
			       NULL /* create_args */, &llink);
	if (rc == 0) {
		pool = pool_obj(llink);
		if (pool->sp_stopping) {
			D_ERROR(DF_UUID": stopping isn't done yet\n",
				DP_UUID(uuid));
			rc = -DER_BUSY;
		}
		/* Already started; drop our reference. */
		daos_lru_ref_release(pool_cache, &pool->sp_entry);
		return rc;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR(DF_UUID": failed to look up pool: %d\n", DP_UUID(uuid),
			rc);
		if (rc == -DER_EXIST)
			rc = -DER_BUSY;
		return rc;
	}

	/* Start it by creating the ds_pool object and hold the reference. */
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t), &arg,
			       &llink);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start pool: %d\n", DP_UUID(uuid),
			rc);
		return rc;
	}

	pool = pool_obj(llink);

	rc = dss_ult_create(pool_fetch_hdls_ult, pool, DSS_XS_SYS,
			    0, 0, NULL);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create fetch ult: %d\n",
			DP_UUID(uuid), rc);
		D_GOTO(failure_pool, rc);
	}

	pool->sp_fetch_hdls = 1;
	rc = ds_pool_start_ec_eph_query_ult(pool);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to start ec eph query ult: %d\n",
			DP_UUID(uuid), rc);
		D_GOTO(failure_ult, rc);
	}

	ds_iv_ns_start(pool->sp_iv_ns);

	return rc;

failure_ult:
	pool_fetch_hdls_ult_abort(pool);
failure_pool:
	ds_pool_put(pool);
	return rc;
}

/*
 * Called via dss_thread_collective() to stop all container services
 * on the current xstream.
 */
static int
pool_child_stop_containers(void *uuid)
{
	struct ds_pool_child *child;

	child = ds_pool_child_lookup(uuid);
	if (child == NULL)
		return 0;

	ds_cont_child_stop_all(child);
	ds_pool_child_put(child); /* -1 for the list */
	return 0;
}

static int
ds_pool_stop_all_containers(struct ds_pool *pool)
{
	int rc;

	rc = dss_thread_collective(pool_child_stop_containers, pool->sp_uuid, 0);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to stop container service: "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));
	return rc;
}

/*
 * Stop a pool. Must be called on the system xstream. Release the ds_pool
 * object reference held by ds_pool_start. Only for mgmt and pool modules.
 */
void
ds_pool_stop(uuid_t uuid)
{
	struct ds_pool *pool;

	ds_pool_failed_remove(uuid);

	ds_pool_lookup(uuid, &pool);
	if (pool == NULL)
		return;
	D_ASSERT(!pool->sp_stopping);
	pool->sp_stopping = 1;

	ds_iv_ns_stop(pool->sp_iv_ns);

	/* Though all containers started in pool_alloc_ref, we need stop all
	 * containers service before tgt_ec_eqh_query_ult(), otherwise container
	 * EC aggregation ULT might try to access ec_eqh_query structure.
	 */
	ds_pool_stop_all_containers(pool);
	ds_pool_tgt_ec_eph_query_abort(pool);
	pool_fetch_hdls_ult_abort(pool);

	ds_rebuild_abort(pool->sp_uuid, -1, -1, -1);
	ds_migrate_stop(pool, -1, -1);
	ds_pool_put(pool); /* held by ds_pool_start */
	ds_pool_put(pool);
	D_INFO(DF_UUID": pool service is aborted\n", DP_UUID(uuid));
}

/* ds_pool_hdl ****************************************************************/

static struct d_hash_table *pool_hdl_hash;

static inline struct ds_pool_hdl *
pool_hdl_obj(d_list_t *rlink)
{
	return container_of(rlink, struct ds_pool_hdl, sph_entry);
}

static bool
pool_hdl_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
		 const void *key, unsigned int ksize)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return uuid_compare(hdl->sph_uuid, key) == 0;
}

static uint32_t
pool_hdl_key_hash(struct d_hash_table *htable, const void *key,
		  unsigned int ksize)
{
	D_ASSERTF(ksize == sizeof(uuid_t), "%u\n", ksize);
	return *((const uint32_t *)key);
}

static uint32_t
pool_hdl_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(link);
	uint32_t *retp = (uint32_t *)hdl->sph_uuid;

	return *retp;
}

static void
pool_hdl_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	pool_hdl_obj(rlink)->sph_ref++;
}

static bool
pool_hdl_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_ASSERTF(hdl->sph_ref > 0, "%d\n", hdl->sph_ref);
	hdl->sph_ref--;
	return hdl->sph_ref == 0;
}

static void
pool_hdl_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct ds_pool_hdl *hdl = pool_hdl_obj(rlink);

	D_DEBUG(DB_MD, DF_UUID": freeing "DF_UUID"\n",
		DP_UUID(hdl->sph_pool->sp_uuid), DP_UUID(hdl->sph_uuid));
	D_ASSERT(d_hash_rec_unlinked(&hdl->sph_entry));
	D_ASSERTF(hdl->sph_ref == 0, "%d\n", hdl->sph_ref);
	daos_iov_free(&hdl->sph_cred);
	ds_pool_put(hdl->sph_pool);
	D_FREE(hdl);
}

static d_hash_table_ops_t pool_hdl_hash_ops = {
	.hop_key_cmp	= pool_hdl_key_cmp,
	.hop_key_hash	= pool_hdl_key_hash,
	.hop_rec_hash	= pool_hdl_rec_hash,
	.hop_rec_addref	= pool_hdl_rec_addref,
	.hop_rec_decref	= pool_hdl_rec_decref,
	.hop_rec_free	= pool_hdl_rec_free
};

int
ds_pool_hdl_hash_init(void)
{
	return d_hash_table_create(0 /* feats */, 4 /* bits */, NULL /* priv */,
				   &pool_hdl_hash_ops, &pool_hdl_hash);
}

void
ds_pool_hdl_hash_fini(void)
{
	d_hash_table_destroy(pool_hdl_hash, true /* force */);
}

static int
pool_hdl_delete_all_cb(d_list_t *link, void *arg)
{
	uuid_copy(arg, pool_hdl_obj(link)->sph_uuid);
	return 1;
}

void
ds_pool_hdl_delete_all(void)
{
	D_DEBUG(DB_MD, "deleting all pool handles\n");
	D_ASSERT(dss_srv_shutting_down());

	/*
	 * The d_hash_table_traverse locking makes it impossible to delete or
	 * even addref in the callback. Hence we traverse and delete one by one.
	 */
	for (;;) {
		uuid_t	arg;
		int	rc;

		uuid_clear(arg);

		rc = d_hash_table_traverse(pool_hdl_hash, pool_hdl_delete_all_cb, arg);
		D_ASSERTF(rc == 0 || rc == 1, DF_RC"\n", DP_RC(rc));

		if (uuid_is_null(arg))
			break;

		/*
		 * Ignore the return code because it's OK for the handle to
		 * have been deleted by someone else.
		 */
		d_hash_rec_delete(pool_hdl_hash, arg, sizeof(uuid_t));
	}
}

static int
pool_hdl_add(struct ds_pool_hdl *hdl)
{
	if (dss_srv_shutting_down())
		return -DER_CANCELED;

	return d_hash_rec_insert(pool_hdl_hash, hdl->sph_uuid,
				 sizeof(uuid_t), &hdl->sph_entry,
				 true /* exclusive */);
}

static void
pool_hdl_delete(struct ds_pool_hdl *hdl)
{
	bool deleted;

	deleted = d_hash_rec_delete(pool_hdl_hash, hdl->sph_uuid,
				    sizeof(uuid_t));
	D_ASSERT(deleted == true);
}

struct ds_pool_hdl *
ds_pool_hdl_lookup(const uuid_t uuid)
{
	d_list_t *rlink;

	rlink = d_hash_rec_find(pool_hdl_hash, uuid, sizeof(uuid_t));
	if (rlink == NULL)
		return NULL;

	return pool_hdl_obj(rlink);
}

void
ds_pool_hdl_put(struct ds_pool_hdl *hdl)
{
	d_hash_rec_decref(pool_hdl_hash, &hdl->sph_entry);
}

static void
aggregate_pool_space(struct daos_pool_space *agg_ps,
		     struct daos_pool_space *ps)
{
	int	i;
	bool	first;

	D_ASSERT(agg_ps && ps);

	if (ps->ps_ntargets == 0) {
		D_DEBUG(DB_TRACE, "Skip empty space info\n");
		return;
	}

	first = (agg_ps->ps_ntargets == 0);
	agg_ps->ps_ntargets += ps->ps_ntargets;

	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		agg_ps->ps_space.s_total[i] += ps->ps_space.s_total[i];
		agg_ps->ps_space.s_free[i] += ps->ps_space.s_free[i];

		if (agg_ps->ps_free_max[i] < ps->ps_free_max[i])
			agg_ps->ps_free_max[i] = ps->ps_free_max[i];
		if (agg_ps->ps_free_min[i] > ps->ps_free_min[i] || first)
			agg_ps->ps_free_min[i] = ps->ps_free_min[i];

		agg_ps->ps_free_mean[i] = agg_ps->ps_space.s_free[i] /
					  agg_ps->ps_ntargets;
	}
}

struct pool_query_xs_arg {
	struct ds_pool		*qxa_pool;
	struct daos_pool_space	 qxa_space;
};

static void
pool_query_xs_reduce(void *agg_arg, void *xs_arg)
{
	struct pool_query_xs_arg	*a_arg = agg_arg;
	struct pool_query_xs_arg	*x_arg = xs_arg;

	if (x_arg->qxa_space.ps_ntargets == 0)
		return;

	D_ASSERT(x_arg->qxa_space.ps_ntargets == 1);
	aggregate_pool_space(&a_arg->qxa_space, &x_arg->qxa_space);
}

static int
pool_query_xs_arg_alloc(struct dss_stream_arg_type *xs, void *agg_arg)
{
	struct pool_query_xs_arg	*x_arg, *a_arg = agg_arg;

	D_ALLOC_PTR(x_arg);
	if (x_arg == NULL)
		return -DER_NOMEM;

	xs->st_arg = x_arg;
	x_arg->qxa_pool = a_arg->qxa_pool;
	return 0;
}

static void
pool_query_xs_arg_free(struct dss_stream_arg_type *xs)
{
	D_ASSERT(xs->st_arg != NULL);
	D_FREE(xs->st_arg);
}

static int
pool_query_space(uuid_t pool_uuid, struct daos_pool_space *x_ps)
{
	struct dss_module_info	*info = dss_get_module_info();
	int			 tid = info->dmi_tgt_id;
	struct ds_pool_child	*pool_child;
	vos_pool_info_t		 vos_pool_info = { 0 };
	struct vos_pool_space	*vps = &vos_pool_info.pif_space;
	int			 i, rc;

	pool_child = ds_pool_child_lookup(pool_uuid);
	if (pool_child == NULL)
		return -DER_NO_HDL;

	rc = vos_pool_query(pool_child->spc_hdl, &vos_pool_info);
	if (rc != 0) {
		D_ERROR("Failed to query pool "DF_UUID", tgt_id: %d, "
			"rc: "DF_RC"\n", DP_UUID(pool_uuid), tid,
			DP_RC(rc));
		goto out;
	}

	x_ps->ps_ntargets = 1;
	x_ps->ps_space.s_total[DAOS_MEDIA_SCM] = SCM_TOTAL(vps);
	x_ps->ps_space.s_total[DAOS_MEDIA_NVME] = NVME_TOTAL(vps);

	/* Exclude the sys reserved space before reporting to user */
	if (SCM_FREE(vps) > SCM_SYS(vps))
		x_ps->ps_space.s_free[DAOS_MEDIA_SCM] =
				SCM_FREE(vps) - SCM_SYS(vps);
	else
		x_ps->ps_space.s_free[DAOS_MEDIA_SCM] = 0;

	if (NVME_FREE(vps) > NVME_SYS(vps))
		x_ps->ps_space.s_free[DAOS_MEDIA_NVME] =
				NVME_FREE(vps) - NVME_SYS(vps);
	else
		x_ps->ps_space.s_free[DAOS_MEDIA_NVME] = 0;

	for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
		x_ps->ps_free_max[i] = x_ps->ps_space.s_free[i];
		x_ps->ps_free_min[i] = x_ps->ps_space.s_free[i];
	}
out:
	ds_pool_child_put(pool_child);
	return rc;
}

static int
pool_query_one(void *vin)
{
	struct dss_coll_stream_args	*reduce = vin;
	struct dss_stream_arg_type	*streams = reduce->csa_streams;
	struct dss_module_info		*info = dss_get_module_info();
	int				 tid = info->dmi_tgt_id;
	struct pool_query_xs_arg	*x_arg = streams[tid].st_arg;
	struct ds_pool			*pool = x_arg->qxa_pool;

	return pool_query_space(pool->sp_uuid, &x_arg->qxa_space);
}

static int
pool_tgt_query(struct ds_pool *pool, struct daos_pool_space *ps)
{
	struct dss_coll_ops		coll_ops;
	struct dss_coll_args		coll_args = { 0 };
	struct pool_query_xs_arg	agg_arg = { 0 };
	int				rc;

	D_ASSERT(ps != NULL);
	memset(ps, 0, sizeof(*ps));

	/* collective operations */
	coll_ops.co_func		= pool_query_one;
	coll_ops.co_reduce		= pool_query_xs_reduce;
	coll_ops.co_reduce_arg_alloc	= pool_query_xs_arg_alloc;
	coll_ops.co_reduce_arg_free	= pool_query_xs_arg_free;

	/* packing arguments for aggregator args */
	agg_arg.qxa_pool		= pool;

	/* setting aggregator args */
	coll_args.ca_aggregator		= &agg_arg;
	coll_args.ca_func_args		= &coll_args.ca_stream_args;

	rc = ds_pool_get_failed_tgt_idx(pool->sp_uuid,
					&coll_args.ca_exclude_tgts,
					&coll_args.ca_exclude_tgts_cnt);
	if (rc) {
		D_ERROR(DF_UUID": failed to get index : rc "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));
		return rc;
	}

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, 0);
	D_FREE(coll_args.ca_exclude_tgts);
	if (rc) {
		D_ERROR("Pool query on pool "DF_UUID" failed, "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc));
		return rc;
	}

	*ps = agg_arg.qxa_space;
	return rc;
}

int
ds_pool_tgt_connect(struct ds_pool *pool, struct pool_iv_conn *pic)
{
	struct ds_pool_hdl	*hdl = NULL;
	d_iov_t			cred_iov;
	int			rc;

	hdl = ds_pool_hdl_lookup(pic->pic_hdl);
	if (hdl != NULL) {
		if (hdl->sph_sec_capas == pic->pic_capas) {
			D_DEBUG(DB_MD, DF_UUID": found compatible pool "
				"handle: hdl="DF_UUID" capas="DF_U64"\n",
				DP_UUID(pool->sp_uuid), DP_UUID(pic->pic_hdl),
				hdl->sph_sec_capas);
			rc = 0;
		} else {
			D_ERROR(DF_UUID": found conflicting pool handle: hdl="
				DF_UUID" capas="DF_U64"\n",
				DP_UUID(pool->sp_uuid), DP_UUID(pic->pic_hdl),
				hdl->sph_sec_capas);
			rc = -DER_EXIST;
		}
		ds_pool_hdl_put(hdl);
		return 0;
	}

	D_ALLOC_PTR(hdl);
	if (hdl == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(hdl->sph_uuid, pic->pic_hdl);
	hdl->sph_flags = pic->pic_flags;
	hdl->sph_sec_capas = pic->pic_capas;
	hdl->sph_global_ver = pic->pic_global_ver;
	hdl->sph_obj_ver = pic->pic_obj_ver;
	ds_pool_get(pool);
	hdl->sph_pool = pool;

	cred_iov.iov_len = pic->pic_cred_size;
	cred_iov.iov_buf_len = pic->pic_cred_size;
	cred_iov.iov_buf = &pic->pic_creds[0];
	rc = daos_iov_copy(&hdl->sph_cred, &cred_iov);
	if (rc != 0) {
		ds_pool_put(pool);
		D_GOTO(out, rc);
	}

	rc = pool_hdl_add(hdl);
	if (rc != 0) {
		daos_iov_free(&hdl->sph_cred);
		ds_pool_put(pool);
		D_GOTO(out, rc);
	}

out:
	if (rc != 0)
		D_FREE(hdl);

	D_DEBUG(DB_MD, DF_UUID": connect "DF_RC"\n",
		DP_UUID(pool->sp_uuid), DP_RC(rc));
	return rc;
}

void
ds_pool_tgt_disconnect(uuid_t uuid)
{
	struct ds_pool_hdl *hdl;

	hdl = ds_pool_hdl_lookup(uuid);
	if (hdl == NULL) {
		D_DEBUG(DB_MD, "handle "DF_UUID" does not exist\n",
			DP_UUID(uuid));
		return;
	}

	ds_pool_iv_conn_hdl_invalidate(hdl->sph_pool, uuid);

	pool_hdl_delete(hdl);
	ds_pool_hdl_put(hdl);
}

void
ds_pool_tgt_disconnect_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_disconnect_in  *in = crt_req_get(rpc);
	struct pool_tgt_disconnect_out *out = crt_reply_get(rpc);
	uuid_t			       *hdl_uuids = in->tdi_hdls.ca_arrays;
	int				i;
	int				rc = 0;

	if (in->tdi_hdls.ca_count == 0)
		D_GOTO(out, rc = 0);

	if (in->tdi_hdls.ca_arrays == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_DEBUG(DB_MD, DF_UUID ": handling rpc: %p hdls[0]=" DF_UUID " nhdls=" DF_U64 "\n",
		DP_UUID(in->tdi_uuid), rpc, DP_UUID(hdl_uuids), in->tdi_hdls.ca_count);

	for (i = 0; i < in->tdi_hdls.ca_count; i++)
		ds_pool_tgt_disconnect(hdl_uuids[i]);
out:
	out->tdo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DB_MD, DF_UUID ": replying rpc: %p %d " DF_RC "\n", DP_UUID(in->tdi_uuid), rpc,
		out->tdo_rc, DP_RC(rc));
	crt_reply_send(rpc);
}

int
ds_pool_tgt_disconnect_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				  void *priv)
{
	struct pool_tgt_disconnect_out *out_source = crt_reply_get(source);
	struct pool_tgt_disconnect_out *out_result = crt_reply_get(result);

	out_result->tdo_rc += out_source->tdo_rc;
	return 0;
}

static int
update_pool_group(struct ds_pool *pool, struct pool_map *map)
{
	uint32_t	version;
	d_rank_list_t	ranks;
	int		rc;

	rc = crt_group_version(pool->sp_group, &version);
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_DEBUG(DB_MD, DF_UUID": %u -> %u\n", DP_UUID(pool->sp_uuid), version,
		pool_map_get_version(map));

	rc = map_ranks_init(map, POOL_GROUP_MAP_STATUS, &ranks);
	if (rc != 0)
		return rc;

	/* Let secondary rank == primary rank. */
	rc = crt_group_secondary_modify(pool->sp_group, &ranks, &ranks,
					CRT_GROUP_MOD_OP_REPLACE,
					pool_map_get_version(map));
	if (rc != 0) {
		if (rc == -DER_OOG)
			D_DEBUG(DB_MD, DF_UUID": SG and PG out of sync: %d\n",
				DP_UUID(pool->sp_uuid), rc);
		else
			D_ERROR(DF_UUID": failed to update group: %d\n",
				DP_UUID(pool->sp_uuid), rc);
	}

	map_ranks_fini(&ranks);
	return rc;
}

/*
 * Called via dss_collective() to update the pool map version in the
 * ds_pool_child object.
 */
static int
update_child_map(void *data)
{
	struct ds_pool		*pool = (struct ds_pool *)data;
	struct ds_pool_child	*child;

	child = ds_pool_child_lookup(pool->sp_uuid);
	if (child == NULL)
		return -DER_NONEXIST;

	child->spc_map_version = pool->sp_map_version;
	ds_pool_child_put(child);
	return 0;
}

int
ds_pool_tgt_map_update(struct ds_pool *pool, struct pool_buf *buf,
		       unsigned int map_version)
{
	struct pool_map *map = NULL;
	bool		update_map = false;
	int		rc = 0;

	if (buf != NULL) {
		rc = pool_map_create(buf, map_version, &map);
		if (rc != 0) {
			D_ERROR(DF_UUID" failed to create pool map: "DF_RC"\n",
				DP_UUID(pool->sp_uuid), DP_RC(rc));
			return rc;
		}
	}

	ABT_rwlock_wrlock(pool->sp_lock);
	/* Check if the pool map needs to to update */
	if (map != NULL &&
	    (pool->sp_map == NULL ||
	     pool_map_get_version(pool->sp_map) < map_version)) {
		struct pool_map *tmp = pool->sp_map;

		D_DEBUG(DB_MD, DF_UUID
			": update pool_map version: %p/%d -> %p/%d\n",
			DP_UUID(pool->sp_uuid), pool->sp_map,
			pool->sp_map ? pool_map_get_version(pool->sp_map) : -1,
			map, pool_map_get_version(map));

		rc = update_pool_group(pool, map);
		if (rc != 0) {
			D_ERROR(DF_UUID": Can not update pool group: "DF_RC"\n",
				DP_UUID(pool->sp_uuid), DP_RC(rc));
			D_GOTO(out, rc);
		}

		rc = pl_map_update(pool->sp_uuid, map,
				   pool->sp_map != NULL ? false : true,
				   DEFAULT_PL_TYPE);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed update pl_map: "
				""DF_RC"\n", DP_UUID(pool->sp_uuid), DP_RC(rc));
			D_GOTO(out, rc);
		}

		rc = pool_map_update_failed_cnt(map);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed fail-cnt update pl_map"
				": %d\n", DP_UUID(pool->sp_uuid), rc);
			D_GOTO(out, rc);
		}

		update_map = true;
		/* drop the stale map */
		pool->sp_map = map;
		map = tmp;
	}

	/* Check if the pool map on each xstream needs to update */
	if (pool->sp_map_version < map_version) {
		D_DEBUG(DB_MD, DF_UUID
			": changed cached map version: %u -> %u\n",
			DP_UUID(pool->sp_uuid), pool->sp_map_version,
			map_version);

		pool->sp_map_version = map_version;
		rc = dss_task_collective(update_child_map, pool, 0);
		D_ASSERT(rc == 0);
		update_map = true;
	}

	if (update_map) {
		struct dtx_scan_args	*arg;
		int ret;

		/* Since the map has been updated successfully, so let's
		 * ignore the dtx resync failure for now.
		 */
		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out, rc);

		uuid_copy(arg->pool_uuid, pool->sp_uuid);
		arg->version = pool->sp_map_version;
		ret = dss_ult_create(dtx_resync_ult, arg, DSS_XS_SYS,
				     0, 0, NULL);
		if (ret) {
			D_ERROR("dtx_resync_ult failure %d\n", ret);
			D_FREE(arg);
		}
	} else {
		D_WARN("Ignore update pool "DF_UUID" %d -> %d\n",
		       DP_UUID(pool->sp_uuid), pool->sp_map_version,
		       map_version);
	}
out:
	ABT_rwlock_unlock(pool->sp_lock);
	if (map != NULL)
		pool_map_decref(map);
	return rc;
}

void
ds_pool_tgt_query_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_query_in	*in = crt_req_get(rpc);
	struct pool_tgt_query_out	*out = crt_reply_get(rpc);
	struct ds_pool			*pool;
	int				 rc;

	/* Single target query */
	if (dss_get_module_info()->dmi_xs_id != 0) {
		rc = pool_query_space(in->tqi_op.pi_uuid, &out->tqo_space);
		goto out;
	}

	/* Aggregate query over all targets on the node */
	rc = ds_pool_lookup(in->tqi_op.pi_uuid, &pool);
	if (rc) {
		D_ERROR("Failed to find pool "DF_UUID": %d\n",
			DP_UUID(in->tqi_op.pi_uuid), rc);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	rc = pool_tgt_query(pool, &out->tqo_space);
	if (rc != 0)
		rc = 1;	/* For query aggregator */
	ds_pool_put(pool);
out:
	out->tqo_rc = rc;
	crt_reply_send(rpc);
}

int
ds_pool_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result, void *priv)
{
	struct pool_tgt_query_out	*out_source = crt_reply_get(source);
	struct pool_tgt_query_out	*out_result = crt_reply_get(result);

	out_result->tqo_rc += out_source->tqo_rc;
	if (out_source->tqo_rc != 0)
		return 0;

	aggregate_pool_space(&out_result->tqo_space, &out_source->tqo_space);
	return 0;
}

static int
update_vos_prop_on_targets(void *in)
{
	struct ds_pool			*pool = (struct ds_pool *)in;
	struct ds_pool_child		*child = NULL;
	struct policy_desc_t		policy_desc = {0};
	int                              ret         = 0;

	child = ds_pool_child_lookup(pool->sp_uuid);
	if (child == NULL)
		return -DER_NONEXIST;	/* no child created yet? */

	policy_desc = pool->sp_policy_desc;
	ret = vos_pool_ctl(child->spc_hdl, VOS_PO_CTL_SET_POLICY, &policy_desc);
	if (ret)
		goto out;

	ret = vos_pool_ctl(child->spc_hdl, VOS_PO_CTL_SET_SPACE_RB, &pool->sp_space_rb);
	if (ret)
		goto out;

	/** If necessary, upgrade the vos pool format */
	if (pool->sp_global_version >= 2)
		ret = vos_pool_upgrade(child->spc_hdl, VOS_POOL_DF_2_4);
	else if (pool->sp_global_version == 1)
		ret = vos_pool_upgrade(child->spc_hdl, VOS_POOL_DF_2_2);

	if (pool->sp_checkpoint_props_changed) {
		pool->sp_checkpoint_props_changed = 0;
		if (child->spc_chkpt_req != NULL)
			sched_req_wakeup(child->spc_chkpt_req);
	}
out:
	ds_pool_child_put(child);

	return ret;
}

int
ds_pool_tgt_prop_update(struct ds_pool *pool, struct pool_iv_prop *iv_prop)
{
	int ret;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	pool->sp_ec_cell_sz = iv_prop->pip_ec_cell_sz;
	pool->sp_global_version = iv_prop->pip_global_version;
	pool->sp_reclaim = iv_prop->pip_reclaim;
	pool->sp_redun_fac = iv_prop->pip_redun_fac;
	pool->sp_ec_pda = iv_prop->pip_ec_pda;
	pool->sp_rp_pda = iv_prop->pip_rp_pda;
	pool->sp_perf_domain = iv_prop->pip_perf_domain;
	pool->sp_space_rb = iv_prop->pip_space_rb;

	if (iv_prop->pip_self_heal & DAOS_SELF_HEAL_AUTO_REBUILD)
		pool->sp_disable_rebuild = 0;
	else
		pool->sp_disable_rebuild = 1;

	if (!daos_policy_try_parse(iv_prop->pip_policy_str,
				   &pool->sp_policy_desc)) {
		D_ERROR("Failed to parse policy string: %s\n",
			iv_prop->pip_policy_str);
		return -DER_MISMATCH;
	}
	D_DEBUG(DB_CSUM, "Updating pool to sched: %lu\n",
		iv_prop->pip_scrub_mode);
	pool->sp_scrub_mode = iv_prop->pip_scrub_mode;
	pool->sp_scrub_freq_sec = iv_prop->pip_scrub_freq;
	pool->sp_scrub_thresh = iv_prop->pip_scrub_thresh;

	pool->sp_checkpoint_props_changed = 0;
	if (pool->sp_checkpoint_mode != iv_prop->pip_checkpoint_mode) {
		pool->sp_checkpoint_mode          = iv_prop->pip_checkpoint_mode;
		pool->sp_checkpoint_props_changed = 1;
	}

	if (pool->sp_checkpoint_freq != iv_prop->pip_checkpoint_freq) {
		pool->sp_checkpoint_freq          = iv_prop->pip_checkpoint_freq;
		pool->sp_checkpoint_props_changed = 1;
	}

	if (pool->sp_checkpoint_thresh != iv_prop->pip_checkpoint_thresh) {
		pool->sp_checkpoint_thresh        = iv_prop->pip_checkpoint_thresh;
		pool->sp_checkpoint_props_changed = 1;
	}

	ret = dss_thread_collective(update_vos_prop_on_targets, pool, 0);

	return ret;
}

/**
 * Query the cached pool map. If the cached version is <= in->tmi_map_version,
 * the pool map will not be transferred to the client.
 */
void
ds_pool_tgt_query_map_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_query_map_in   *in = crt_req_get(rpc);
	struct pool_tgt_query_map_out  *out = crt_reply_get(rpc);
	struct ds_pool		       *pool;
	struct pool_buf		       *buf;
	unsigned int			version;
	int				rc;

	D_DEBUG(DB_TRACE, DF_UUID ": handling rpc: %p hdl=" DF_UUID "\n",
		DP_UUID(in->tmi_op.pi_uuid), rpc, DP_UUID(in->tmi_op.pi_hdl));

	/* Validate the pool handle and get the ds_pool object. */
	if (daos_rpc_from_client(rpc)) {
		struct ds_pool_hdl *hdl;

		hdl = ds_pool_hdl_lookup(in->tmi_op.pi_hdl);
		if (hdl == NULL) {
			D_ERROR(DF_UUID": cannot find pool handle "DF_UUID"\n",
				DP_UUID(in->tmi_op.pi_uuid), DP_UUID(in->tmi_op.pi_hdl));
			rc = -DER_NO_HDL;
			goto out;
		}
		ds_pool_get(hdl->sph_pool);
		pool = hdl->sph_pool;
		ds_pool_hdl_put(hdl);
	} else {
		/*
		 * See the comment on validating the pool handle in
		 * ds_pool_query_handler.
		 */
		rc = ds_pool_lookup(in->tmi_op.pi_uuid, &pool);
		if (rc) {
			D_ERROR(DF_UUID": failed to look up pool: %d\n",
				DP_UUID(in->tmi_op.pi_uuid), rc);
			rc = -DER_NONEXIST;
			goto out;
		}
		rc = ds_pool_hdl_is_from_srv(pool, in->tmi_op.pi_hdl);
		if (rc < 0) {
			D_CDEBUG(rc == -DER_NOTLEADER, DLOG_DBG, DLOG_ERR,
				 DF_UUID": failed to check server pool handle "DF_UUID": "DF_RC"\n",
				 DP_UUID(in->tmi_op.pi_uuid), DP_UUID(in->tmi_op.pi_hdl),
				 DP_RC(rc));
			if (rc == -DER_NOTLEADER)
				rc = -DER_AGAIN;
			goto out_pool;
		} else if (!rc) {
			D_ERROR(DF_UUID": cannot find server pool handle "DF_UUID"\n",
				DP_UUID(in->tmi_op.pi_uuid), DP_UUID(in->tmi_op.pi_hdl));
			rc = -DER_NO_HDL;
			goto out_pool;
		}
	}

	/* Inefficient; better invent some zero-copy IV APIs. */
	ABT_rwlock_rdlock(pool->sp_lock);
	version = (pool->sp_map == NULL ? 0 : pool_map_get_version(pool->sp_map));
	if (version <= in->tmi_map_version) {
		rc = 0;
		ABT_rwlock_unlock(pool->sp_lock);
		goto out_version;
	}
	rc = pool_buf_extract(pool->sp_map, &buf);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0)
		goto out_version;

	rc = ds_pool_transfer_map_buf(buf, version, rpc, in->tmi_map_bulk,
				      &out->tmo_map_buf_size);

	D_FREE(buf);
out_version:
	out->tmo_op.po_map_version = version;
out_pool:
	ds_pool_put(pool);
out:
	out->tmo_op.po_rc = rc;
	D_DEBUG(DB_TRACE, DF_UUID ": replying rpc: %p " DF_RC "\n", DP_UUID(in->tmi_op.pi_uuid),
		rpc, DP_RC(out->tmo_op.po_rc));
	crt_reply_send(rpc);
}

struct tgt_discard_arg {
	uuid_t			     pool_uuid;
	uint64_t		     epoch;
	struct pool_target_addr_list tgt_list;
};

struct child_discard_arg {
	struct tgt_discard_arg	*tgt_discard;
	uuid_t			cont_uuid;
};

static struct tgt_discard_arg*
tgt_discard_arg_alloc(struct pool_target_addr_list *tgt_list)
{
	struct tgt_discard_arg	*arg;
	int			i;
	int			rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return NULL;

	rc = pool_target_addr_list_alloc(tgt_list->pta_number, &arg->tgt_list);
	if (rc != 0) {
		D_FREE(arg);
		return NULL;
	}

	for (i = 0; i < tgt_list->pta_number; i++) {
		arg->tgt_list.pta_addrs[i].pta_rank = tgt_list->pta_addrs[i].pta_rank;
		arg->tgt_list.pta_addrs[i].pta_target = tgt_list->pta_addrs[i].pta_target;
	}

	return arg;
}

static void
tgt_discard_arg_free(struct tgt_discard_arg *arg)
{
	pool_target_addr_list_free(&arg->tgt_list);
	D_FREE(arg);
}

static int
obj_discard_cb(daos_handle_t ch, vos_iter_entry_t *ent,
	       vos_iter_type_t type, vos_iter_param_t *param,
	       void *data, unsigned *acts)
{
	struct child_discard_arg	*arg = data;
	daos_epoch_range_t		epr;
	int				rc;

	epr.epr_hi = arg->tgt_discard->epoch;
	epr.epr_lo = 0;
	do {
		/* Inform the iterator and delete the object */
		*acts |= VOS_ITER_CB_DELETE;
		rc = vos_discard(param->ip_hdl, &ent->ie_oid, &epr, NULL, NULL);
		if (rc != -DER_BUSY && rc != -DER_INPROGRESS)
			break;

		D_DEBUG(DB_REBUILD, "retry by "DF_RC"/"DF_UOID"\n",
			DP_RC(rc), DP_UOID(ent->ie_oid));
		/* Busy - inform iterator and yield */
		*acts |= VOS_ITER_CB_YIELD;
		dss_sleep(0);
	} while (1);


	if (rc != 0)
		D_ERROR("discard object pool/object "DF_UUID"/"DF_UOID" rc: "DF_RC"\n",
			DP_UUID(arg->tgt_discard->pool_uuid), DP_UOID(ent->ie_oid),
			DP_RC(rc));
	return rc;
}

/** vos_iter_cb_t */
static int
cont_discard_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		vos_iter_type_t type, vos_iter_param_t *iter_param,
		void *cb_arg, unsigned int *acts)
{
	struct child_discard_arg *arg = cb_arg;
	struct ds_cont_child	*cont = NULL;
	vos_iter_param_t	param = { 0 };
	struct vos_iter_anchors	anchor = { 0 };
	daos_handle_t		coh;
	int			rc;

	D_ASSERT(type == VOS_ITER_COUUID);
	if (uuid_compare(arg->cont_uuid, entry->ie_couuid) == 0) {
		D_DEBUG(DB_REBUILD, DF_UUID" already discard\n",
			DP_UUID(arg->cont_uuid));
		return 0;
	}

	rc = ds_cont_child_lookup(arg->tgt_discard->pool_uuid, entry->ie_couuid,
				  &cont);
	if (rc != DER_SUCCESS) {
		D_ERROR("Lookup container '"DF_UUIDF"' failed: "DF_RC"\n",
			DP_UUID(entry->ie_couuid), DP_RC(rc));
		return rc;
	}

	rc = vos_cont_open(iter_param->ip_hdl, entry->ie_couuid, &coh);
	if (rc != 0) {
		D_ERROR("Open container "DF_UUID" failed: "DF_RC"\n",
			DP_UUID(entry->ie_couuid), DP_RC(rc));
		D_GOTO(put, rc);
	}

	param.ip_hdl = coh;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = arg->tgt_discard->epoch;
	uuid_copy(arg->cont_uuid, entry->ie_couuid);

	rc = vos_iterate(&param, VOS_ITER_OBJ, false, &anchor, obj_discard_cb, NULL,
			 arg, NULL);
	vos_cont_close(coh);
	D_DEBUG(DB_TRACE, DF_UUID"/"DF_UUID" discard cont done: "DF_RC"\n",
		DP_UUID(arg->tgt_discard->pool_uuid), DP_UUID(entry->ie_couuid),
		DP_RC(rc));

put:
	ds_cont_child_put(cont);
	return rc;
}

static int
pool_child_discard(void *data)
{
	struct tgt_discard_arg	*arg = data;
	struct child_discard_arg cont_arg;
	struct ds_pool_child	*child;
	vos_iter_param_t	param = { 0 };
	struct vos_iter_anchors	anchor = { 0 };
	struct pool_target_addr addr;
	uint32_t		myrank;
	int			rc;

	myrank = dss_self_rank();
	addr.pta_rank = myrank;
	addr.pta_target = dss_get_module_info()->dmi_tgt_id;
	if (!pool_target_addr_found(&arg->tgt_list, &addr)) {
		D_DEBUG(DB_TRACE, "skip discard %u/%u.\n", addr.pta_rank,
			addr.pta_target);
		return 0;
	}

	D_DEBUG(DB_MD, DF_UUID" discard %u/%u\n", DP_UUID(arg->pool_uuid),
		myrank, addr.pta_target);

	child = ds_pool_child_lookup(arg->pool_uuid);
	D_ASSERT(child != NULL);
	param.ip_hdl = child->spc_hdl;

	cont_arg.tgt_discard = arg;
	child->spc_discard_done = 0;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 cont_discard_cb, NULL, &cont_arg, NULL);

	child->spc_discard_done = 1;

	ds_pool_child_put(child);

	return rc;
}

/* Discard the objects by epoch in this pool */
static void
ds_pool_tgt_discard_ult(void *data)
{
	struct ds_pool		*pool;
	struct tgt_discard_arg	*arg = data;
	struct dss_coll_ops	coll_ops = { 0 };
	struct dss_coll_args	coll_args = { 0 };
	int			rc;

	/* If discard failed, let's still go ahead, since reintegration might
	 * still succeed, though it might leave some garbage on the reintegration
	 * target, the future scrub tool might fix it. XXX
	 */
	rc = ds_pool_lookup(arg->pool_uuid, &pool);
	if (pool == NULL) {
		D_INFO(DF_UUID" can not be found: %d\n", DP_UUID(arg->pool_uuid), rc);
		D_GOTO(free, rc = 0);
	}

	/* collective operations */
	coll_ops.co_func = pool_child_discard;
	coll_args.ca_func_args	= arg;
	if (pool->sp_map != NULL) {
		unsigned int status;

		/* It should only discard the target in DOWNOUT state, and skip
		 * targets in other state.
		 */
		status = PO_COMP_ST_UP | PO_COMP_ST_UPIN | PO_COMP_ST_DRAIN |
			 PO_COMP_ST_DOWN | PO_COMP_ST_NEW;
		rc = ds_pool_get_tgt_idx_by_state(arg->pool_uuid, status,
						  &coll_args.ca_exclude_tgts,
						  &coll_args.ca_exclude_tgts_cnt);
		if (rc) {
			D_ERROR(DF_UUID "failed to get index : rc "DF_RC"\n",
				DP_UUID(arg->pool_uuid), DP_RC(rc));
			D_GOTO(put, rc);
		}
	}

	rc = dss_thread_collective_reduce(&coll_ops, &coll_args, DSS_ULT_DEEP_STACK);
	if (coll_args.ca_exclude_tgts)
		D_FREE(coll_args.ca_exclude_tgts);
	D_CDEBUG(rc == 0, DB_MD, DLOG_ERR, DF_UUID" tgt discard:" DF_RC"\n",
		 DP_UUID(arg->pool_uuid), DP_RC(rc));
put:
	pool->sp_need_discard = 0;
	pool->sp_discard_status = rc;

	ds_pool_put(pool);
free:
	tgt_discard_arg_free(arg);
}

void
ds_pool_tgt_discard_handler(crt_rpc_t *rpc)
{
	struct pool_tgt_discard_in	*in = crt_req_get(rpc);
	struct pool_tgt_discard_out	*out = crt_reply_get(rpc);
	struct pool_target_addr_list	pta_list;
	struct tgt_discard_arg		*arg = NULL;
	struct ds_pool			*pool;
	int				rc;

	pta_list.pta_number = in->ptdi_addrs.ca_count;
	pta_list.pta_addrs = in->ptdi_addrs.ca_arrays;
	arg = tgt_discard_arg_alloc(&pta_list);
	if (arg == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* POOL is already started in ds_mgmt_hdlr_tgt_create() during reintegration,
	 * though pool might being stopped for some reason.
	 * Let's do pool lookup to make sure pool child is already created.
	 */
	uuid_copy(arg->pool_uuid, in->ptdi_uuid);
	arg->epoch = DAOS_EPOCH_MAX;
	rc = ds_pool_lookup(arg->pool_uuid, &pool);
	if (rc) {
		D_INFO(DF_UUID" can not be found: %d\n", DP_UUID(arg->pool_uuid), rc);
		D_GOTO(out, rc = 0);
	}

	pool->sp_need_discard = 1;
	pool->sp_discard_status = 0;
	rc = dss_ult_create(ds_pool_tgt_discard_ult, arg, DSS_XS_SYS, 0, 0, NULL);

	ds_pool_put(pool);
out:
	out->ptdo_rc = rc;
	D_DEBUG(DB_MD, DF_UUID": replying rpc "DF_RC"\n", DP_UUID(in->ptdi_uuid),
		DP_RC(rc));
	crt_reply_send(rpc);
	if (rc != 0 && arg != NULL)
		tgt_discard_arg_free(arg);
}
