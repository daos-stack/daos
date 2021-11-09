/*
 * (C) Copyright 2016-2021 Intel Corporation.
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
#include "rpc.h"
#include "srv_internal.h"

/* ds_pool_child **************************************************************/

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
		D_DEBUG(DF_DSMS, DF_UUID": destroying\n",
			DP_UUID(child->spc_uuid));
		D_ASSERT(d_list_empty(&child->spc_list));
		D_ASSERT(d_list_empty(&child->spc_cont_list));
		vos_pool_close(child->spc_hdl);
		dss_module_fini_metrics(DAOS_TGT_TAG, child->spc_metrics);
		DABT_EVENTUAL_SET(child->spc_ref_eventual, (void *)&child->spc_ref,
				     sizeof(child->spc_ref));
	}
}

static void
gc_ult(void *arg)
{
	struct ds_pool_child	*child = (struct ds_pool_child *)arg;
	struct dss_module_info	*dmi = dss_get_module_info();
	int			 rc;

	D_DEBUG(DF_DSMS, DF_UUID"[%d]: GC ULT started\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);

	if (child->spc_gc_req == NULL)
		goto out;

	while (!dss_ult_exiting(child->spc_gc_req)) {
		rc = vos_gc_pool(child->spc_hdl, -1, dss_ult_yield,
				 (void *)child->spc_gc_req);
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
	D_DEBUG(DF_DSMS, DF_UUID"[%d]: GC ULT stopped\n",
		DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
}

static int
start_gc_ult(struct ds_pool_child *child)
{
	struct dss_module_info	*dmi = dss_get_module_info();
	struct sched_req_attr	 attr;
	ABT_thread		 gc = ABT_THREAD_NULL;
	int			 rc;

	D_ASSERT(child != NULL);
	D_ASSERT(child->spc_gc_req == NULL);

	rc = dss_ult_create(gc_ult, child, DSS_XS_SELF, 0, 0, &gc);
	if (rc) {
		D_ERROR(DF_UUID"[%d]: Failed to create GC ULT. %d\n",
			DP_UUID(child->spc_uuid), dmi->dmi_tgt_id, rc);
		return rc;
	}

	D_ASSERT(gc != ABT_THREAD_NULL);
	sched_req_attr_init(&attr, SCHED_REQ_GC, &child->spc_uuid);
	attr.sra_flags = SCHED_REQ_FL_NO_DELAY;
	child->spc_gc_req = sched_req_get(&attr, gc);
	if (child->spc_gc_req == NULL) {
		D_CRIT(DF_UUID"[%d]: Failed to get req for GC ULT\n",
		       DP_UUID(child->spc_uuid), dmi->dmi_tgt_id);
		DABT_THREAD_FREE(&gc);
		return -DER_NOMEM;
	}

	return 0;
}

static void
stop_gc_ult(struct ds_pool_child *child)
{
	D_ASSERT(child != NULL);
	/* GC ULT is not started */
	if (child->spc_gc_req == NULL)
		return;

	D_DEBUG(DF_DSMS, DF_UUID"[%d]: Stopping GC ULT\n",
		DP_UUID(child->spc_uuid), dss_get_module_info()->dmi_tgt_id);

	sched_req_wait(child->spc_gc_req, true);
	sched_req_put(child->spc_gc_req);
	child->spc_gc_req = NULL;
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

	D_DEBUG(DF_DSMS, DF_UUID": creating\n", DP_UUID(arg->pla_uuid));

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
	rc = vos_pool_open_metrics(path, arg->pla_uuid, VOS_POF_EXCL,
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

	rc = ds_start_scrubbing_ult(child);
	if (rc != 0)
		goto out_gc;

	d_list_add(&child->spc_list, &tls->dt_pool_list);

	/* Load all containers */
	rc = ds_cont_child_start_all(child);
	if (rc)
		goto out_list;

	return 0;

out_list:
	d_list_del_init(&child->spc_list);
	ds_cont_child_stop_all(child);
	ds_stop_scrubbing_ult(child);
out_gc:
	stop_gc_ult(child);
out_eventual:
	DABT_EVENTUAL_FREE(&child->spc_ref_eventual);
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
	int *ref;

	child = ds_pool_child_lookup(uuid);
	if (child == NULL)
		return 0;

	d_list_del_init(&child->spc_list);
	ds_cont_child_stop_all(child);
	ds_stop_scrubbing_ult(child);
	ds_pool_child_put(child); /* -1 for the list */

	ds_pool_child_put(child); /* -1 for lookup */

	DABT_EVENTUAL_WAIT(child->spc_ref_eventual, (void **)&ref);
	DABT_EVENTUAL_FREE(&child->spc_ref_eventual);

	/* only stop gc ULT when all ops ULTs are done */
	stop_gc_ult(child);

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

	D_DEBUG(DF_DSMS, DF_UUID": creating\n", DP_UUID(key));

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

	uuid_unparse_lower(key, group_id);
	rc = crt_group_secondary_create(group_id, NULL /* primary_grp */,
					NULL /* ranks */, &pool->sp_group);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool group: %d\n",
			DP_UUID(key), rc);
		goto err_done_cond;
	}

	rc = ds_iv_ns_create(info->dmi_ctx, pool->sp_uuid, pool->sp_group,
			     &iv_ns_id, &pool->sp_iv_ns);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool IV NS: %d\n",
			DP_UUID(key), rc);
		goto err_group;
	}

	/** set up ds_pool metrics */
	rc = ds_pool_metrics_start(pool);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to set up ds_pool metrics: %d\n",
			DP_UUID(key), rc);
		goto err_iv_ns;
	}

	collective_arg.pla_pool = pool;
	collective_arg.pla_uuid = key;
	collective_arg.pla_map_version = arg->pca_map_version;
	rc = dss_thread_collective(pool_child_add_one, &collective_arg, 0);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to add ES pool caches: "DF_RC"\n",
			DP_UUID(key), DP_RC(rc));
		goto err_metrics;
	}

	*link = &pool->sp_entry;
	return 0;

err_metrics:
	ds_pool_metrics_stop(pool);
err_iv_ns:
	ds_iv_ns_put(pool->sp_iv_ns);
err_group:
	rc_tmp = crt_group_secondary_destroy(pool->sp_group);
	if (rc_tmp != 0)
		D_ERROR(DF_UUID": failed to destroy pool group: "DF_RC"\n",
			DP_UUID(pool->sp_uuid), DP_RC(rc_tmp));
err_done_cond:
	ABT_cond_free(&pool->sp_fetch_hdls_done_cond);
err_cond:
	ABT_cond_free(&pool->sp_fetch_hdls_cond);
err_mutex:
	DABT_MUTEX_FREE(&pool->sp_mutex);
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

	D_DEBUG(DF_DSMS, DF_UUID": freeing\n", DP_UUID(pool->sp_uuid));

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
	DABT_MUTEX_FREE(&pool->sp_mutex);
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

struct ds_pool *
ds_pool_lookup(const uuid_t uuid)
{
	struct daos_llink	*llink;
	struct ds_pool		*pool;
	int			 rc;

	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = daos_lru_ref_hold(pool_cache, (void *)uuid, sizeof(uuid_t),
			       NULL /* create_args */, &llink);
	if (rc != 0)
		return NULL;

	pool = pool_obj(llink);
	if (pool->sp_stopping) {
		D_DEBUG(DB_MD, DF_UUID": is in stopping\n", DP_UUID(uuid));
		ds_pool_put(pool);
		return NULL;
	}

	return pool;
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
		DABT_COND_WAIT(pool->sp_fetch_hdls_cond, pool->sp_mutex);
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
	DABT_COND_SIGNAL(pool->sp_fetch_hdls_done_cond);
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
	ABT_thread		ec_eph_query_ult = ABT_THREAD_NULL;
	int			rc;

	if (unlikely(ec_agg_disabled))
		return 0;

	rc = dss_ult_create(tgt_ec_eph_query_ult, pool, DSS_XS_SYS, 0,
			    DSS_DEEP_STACK_SZ, &ec_eph_query_ult);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed create ec eph equery ult: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		return rc;
	}

	D_ASSERT(ec_eph_query_ult != ABT_THREAD_NULL);
	sched_req_attr_init(&attr, SCHED_REQ_GC, &pool->sp_uuid);
	pool->sp_ec_ephs_req = sched_req_get(&attr, ec_eph_query_ult);
	if (pool->sp_ec_ephs_req == NULL) {
		D_ERROR(DF_UUID": Failed to get req for ec eph query ULT\n",
			DP_UUID(pool->sp_uuid));
		DABT_THREAD_FREE(&ec_eph_query_ult);
		return -DER_NOMEM;
	}

	return rc;
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
	DABT_COND_SIGNAL(pool->sp_fetch_hdls_cond);
	ABT_mutex_unlock(pool->sp_mutex);

	ABT_mutex_lock(pool->sp_mutex);
	DABT_COND_WAIT(pool->sp_fetch_hdls_done_cond, pool->sp_mutex);
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
 * Stop a pool. Must be called on the system xstream. Release the ds_pool
 * object reference held by ds_pool_start. Only for mgmt and pool modules.
 */
void
ds_pool_stop(uuid_t uuid)
{
	struct ds_pool *pool;

	pool = ds_pool_lookup(uuid);
	if (pool == NULL)
		return;
	if (pool->sp_stopping)
		return;
	pool->sp_stopping = 1;

	ds_iv_ns_stop(pool->sp_iv_ns);
	ds_pool_tgt_ec_eph_query_abort(pool);
	pool_fetch_hdls_ult_abort(pool);

	ds_rebuild_abort(pool->sp_uuid, -1);
	ds_migrate_abort(pool->sp_uuid, -1);
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

	D_DEBUG(DF_DSMS, DF_UUID": freeing "DF_UUID"\n",
		DP_UUID(hdl->sph_pool->sp_uuid), DP_UUID(hdl->sph_uuid));
	D_ASSERT(d_hash_rec_unlinked(&hdl->sph_entry));
	D_ASSERTF(hdl->sph_ref == 0, "%d\n", hdl->sph_ref);
	daos_iov_free(&hdl->sph_cred);

	/*
	 * FIXME: We currently don't guarantee all caches are cleared before
	 * TLS fini on server shutdown, so we have to avoid calling into
	 * ds_pool_put() (where asserting on xtream ID) if it's from cache
	 * destroy on pool module fini.
	 */
	if (dss_tls_get() == NULL)
		daos_lru_ref_release(pool_cache, &hdl->sph_pool->sp_entry);
	else
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
	/* Currently, we use "force" to purge all ds_pool_hdl objects. */
	d_hash_table_destroy(pool_hdl_hash, true /* force */);
}

static int
pool_hdl_add(struct ds_pool_hdl *hdl)
{
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
			D_DEBUG(DF_DSMS, DF_UUID": found compatible pool "
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

	D_DEBUG(DF_DSMS, DF_UUID": connect "DF_RC"\n",
		DP_UUID(pool->sp_uuid), DP_RC(rc));
	return rc;
}

void
ds_pool_tgt_disconnect(uuid_t uuid)
{
	struct ds_pool_hdl *hdl;

	hdl = ds_pool_hdl_lookup(uuid);
	if (hdl == NULL) {
		D_DEBUG(DF_DSMS, "handle "DF_UUID" does not exist\n",
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

	D_DEBUG(DF_DSMS, DF_UUID": handling rpc %p: hdls[0]="DF_UUID" nhdls="
		DF_U64"\n", DP_UUID(in->tdi_uuid), rpc, DP_UUID(hdl_uuids),
		in->tdi_hdls.ca_count);

	for (i = 0; i < in->tdi_hdls.ca_count; i++)
		ds_pool_tgt_disconnect(hdl_uuids[i]);
out:
	out->tdo_rc = (rc == 0 ? 0 : 1);
	D_DEBUG(DF_DSMS, DF_UUID": replying rpc %p: %d "DF_RC"\n",
		DP_UUID(in->tdi_uuid), rpc, out->tdo_rc, DP_RC(rc));
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

	rc = map_ranks_init(map, MAP_RANKS_UP, &ranks);
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
	if (rc == 0)
		rc = ds_cont_rf_check(pool->sp_uuid);
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
	pool = ds_pool_lookup(in->tqi_op.pi_uuid);
	if (pool == NULL) {
		D_ERROR("Failed to find pool "DF_UUID"\n",
			DP_UUID(in->tqi_op.pi_uuid));
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

int
ds_pool_tgt_prop_update(struct ds_pool *pool, struct pool_iv_prop *iv_prop)
{
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	pool->sp_ec_cell_sz = iv_prop->pip_ec_cell_sz;
	pool->sp_reclaim = iv_prop->pip_reclaim;
	return 0;
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
	struct ds_pool_hdl	       *hdl;
	struct ds_pool		       *pool;
	struct pool_buf		       *buf;
	unsigned int			version;
	int				rc;

	D_DEBUG(DB_TRACE, DF_UUID": handling rpc %p\n",
		DP_UUID(in->tmi_op.pi_uuid), rpc);

	hdl = ds_pool_hdl_lookup(in->tmi_op.pi_hdl);
	if (hdl == NULL) {
		rc = -DER_NO_HDL;
		goto out;
	}

	/* Inefficient; better invent some zero-copy IV APIs. */
	pool = hdl->sph_pool;
	ABT_rwlock_rdlock(pool->sp_lock);
	version = (pool->sp_map == NULL ? 0 : pool_map_get_version(pool->sp_map));
	if (version <= in->tmi_map_version) {
		rc = 0;
		ABT_rwlock_unlock(pool->sp_lock);
		goto out_hdl;
	}
	rc = pool_buf_extract(pool->sp_map, &buf);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0)
		goto out_hdl;

	rc = ds_pool_transfer_map_buf(buf, version, rpc, in->tmi_map_bulk,
				      &out->tmo_map_buf_size);

	D_FREE(buf);
out_hdl:
	out->tmo_op.po_map_version = version;
	ds_pool_hdl_put(hdl);
out:
	out->tmo_op.po_rc = rc;
	D_DEBUG(DB_TRACE, DF_UUID": replying rpc %p: "DF_RC"\n",
		DP_UUID(in->tmi_op.pi_uuid), rpc, DP_RC(out->tmo_op.po_rc));
	crt_reply_send(rpc);
}
