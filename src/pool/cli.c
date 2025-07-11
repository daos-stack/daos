/*
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * dc_pool: Pool Client
 *
 * This module is part of libdaos. It implements the pool methods of DAOS API
 * as well as daos/pool.h.
 */

#define D_LOGFAC	DD_FAC(pool)

#include <daos/common.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_producer.h>
#include <daos/event.h>
#include <daos/mgmt.h>
#include <daos/placement.h>
#include <daos/metrics.h>
#include <daos/job.h>
#include <daos/pool.h>
#include <daos/security.h>
#include <daos_types.h>
#include <semaphore.h>
#include "cli_internal.h"
#include "rpc.h"

/** Replicated Service client state (used by Management API) */
struct rsvc_client_state {
	struct rsvc_client  scs_client;
	struct dc_mgmt_sys *scs_sys;
};

int	dc_pool_proto_version;
static pthread_mutex_t warmup_lock = PTHREAD_MUTEX_INITIALIZER;

/* task private context for pool API implementation (some fields are used only by some ops) */
struct pool_task_priv {
	struct d_backoff_seq      backoff_seq;
	uint64_t                  rq_time; /* request time (hybrid logical clock) */
	struct dc_pool           *pool;    /* client pool handle (pool_connect) */
	struct pool_update_state *state;   /* (pool_update_internal) */
};

static int
pool_task_create_priv(tse_task_t *task, struct pool_task_priv **tpriv_out)
{
	struct pool_task_priv *tpriv;

	D_ASSERT(dc_task_get_priv(task) == NULL);

	D_ALLOC_PTR(tpriv);
	if (tpriv == NULL)
		return -DER_NOMEM;

	dc_pool_init_backoff_seq(&tpriv->backoff_seq);

	dc_task_set_priv(task, tpriv);

	*tpriv_out = tpriv;
	return 0;
}

static void
pool_task_destroy_priv(tse_task_t *task)
{
	struct pool_task_priv *tpriv = dc_task_get_priv(task);

	D_ASSERT(tpriv != NULL);
	dc_task_set_priv(task, NULL /* priv */);
	dc_pool_fini_backoff_seq(&tpriv->backoff_seq);
	D_FREE(tpriv);
}

static int
pool_task_reinit(tse_task_t *task)
{
	struct pool_task_priv *tpriv = dc_task_get_priv(task);
	uint32_t               delay = d_backoff_seq_next(&tpriv->backoff_seq);

	return tse_task_reinit_with_delay(task, delay);
}

struct dc_pool_metrics {
	d_list_t dp_pool_list; /* pool metrics list on this thread */
	uuid_t   dp_uuid;
	char     dp_path[D_TM_MAX_NAME_LEN];
	void    *dp_metrics[DAOS_NR_MODULE];
	int      dp_ref;
};

/**
 * Destroy metrics for a specific pool.
 *
 * \param[in]	pool	pointer to ds_pool structure
 */
static void
dc_pool_metrics_free(struct dc_pool_metrics *metrics)
{
	int rc;

	if (!daos_client_metric)
		return;

	daos_module_fini_metrics(DAOS_CLI_TAG, metrics->dp_metrics);
	if (!daos_client_metric_retain) {
		rc = d_tm_del_ephemeral_dir(metrics->dp_path);
		if (rc != 0) {
			D_WARN(DF_UUID ": failed to remove pool metrics dir for pool: " DF_RC "\n",
			       DP_UUID(metrics->dp_uuid), DP_RC(rc));
			return;
		}
	}

	D_INFO(DF_UUID ": destroyed ds_pool metrics: %s\n", DP_UUID(metrics->dp_uuid),
	       metrics->dp_path);
}

static int
dc_pool_metrics_alloc(uuid_t pool_uuid, struct dc_pool_metrics **metrics_p)
{
	struct dc_pool_metrics *metrics = NULL;
	int                     pid;
	size_t                  size;
	int                     rc;

	if (!daos_client_metric)
		return 0;

	D_ALLOC_PTR(metrics);
	if (metrics == NULL)
		return -DER_NOMEM;

	uuid_copy(metrics->dp_uuid, pool_uuid);
	pid = getpid();
	snprintf(metrics->dp_path, sizeof(metrics->dp_path), "pool/" DF_UUIDF,
		 DP_UUID(metrics->dp_uuid));

	/** create new shmem space for per-pool metrics */
	size = daos_module_nr_pool_metrics() * PER_METRIC_BYTES;
	rc   = d_tm_add_ephemeral_dir(NULL, size, metrics->dp_path);
	if (rc != 0) {
		D_WARN(DF_UUID ": failed to create metrics dir for pool: " DF_RC "\n",
		       DP_UUID(metrics->dp_uuid), DP_RC(rc));
		D_FREE(metrics);
		return rc;
	}

	/* initialize metrics on the system xstream for each module */
	rc = daos_module_init_metrics(DAOS_CLI_TAG, metrics->dp_metrics, metrics->dp_path, pid);
	if (rc != 0) {
		D_WARN(DF_UUID ": failed to initialize module metrics: " DF_RC "\n",
		       DP_UUID(metrics->dp_uuid), DP_RC(rc));
		dc_pool_metrics_free(metrics);
		return rc;
	}

	D_INFO(DF_UUID ": created metrics for pool %s\n", DP_UUID(metrics->dp_uuid),
	       metrics->dp_path);
	*metrics_p = metrics;

	return 0;
}

struct dc_pool_metrics *
dc_pool_metrics_lookup(struct dc_pool_tls *tls, uuid_t pool_uuid)
{
	struct dc_pool_metrics *metrics;

	D_MUTEX_LOCK(&tls->dpc_metrics_list_lock);
	d_list_for_each_entry(metrics, &tls->dpc_metrics_list, dp_pool_list) {
		if (uuid_compare(pool_uuid, metrics->dp_uuid) == 0) {
			D_MUTEX_UNLOCK(&tls->dpc_metrics_list_lock);
			return metrics;
		}
	}
	D_MUTEX_UNLOCK(&tls->dpc_metrics_list_lock);

	return NULL;
}

static void *
dc_pool_tls_init(int tags, int xs_id, int pid)
{
	struct dc_pool_tls *tls;
	int                 rc;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	rc = D_MUTEX_INIT(&tls->dpc_metrics_list_lock, NULL);
	if (rc != 0) {
		D_FREE(tls);
		return NULL;
	}

	D_INIT_LIST_HEAD(&tls->dpc_metrics_list);
	return tls;
}

static void
dc_pool_tls_fini(int tags, void *data)
{
	struct dc_pool_tls     *tls = data;
	struct dc_pool_metrics *dpm;
	struct dc_pool_metrics *tmp;

	D_MUTEX_LOCK(&tls->dpc_metrics_list_lock);
	d_list_for_each_entry_safe(dpm, tmp, &tls->dpc_metrics_list, dp_pool_list) {
		if (dpm->dp_ref != 0)
			D_WARN("still reference for pool " DF_UUID " metrics\n",
			       DP_UUID(dpm->dp_uuid));
		d_list_del_init(&dpm->dp_pool_list);
		dc_pool_metrics_free(dpm);
		D_FREE(dpm);
	}
	D_MUTEX_UNLOCK(&tls->dpc_metrics_list_lock);

	D_MUTEX_DESTROY(&tls->dpc_metrics_list_lock);
	D_FREE(tls);
}

struct daos_module_key dc_pool_module_key = {
    .dmk_tags  = DAOS_CLI_TAG,
    .dmk_index = -1,
    .dmk_init  = dc_pool_tls_init,
    .dmk_fini  = dc_pool_tls_fini,
};

/**
 * Initialize pool interface
 */
int
dc_pool_init(void)
{
	uint32_t		ver_array[2] = {DAOS_POOL_VERSION - 1, DAOS_POOL_VERSION};
	int			rc;

	if (daos_client_metric)
		daos_register_key(&dc_pool_module_key);

	dc_pool_proto_version = 0;
	rc = daos_rpc_proto_query(pool_proto_fmt_v6.cpf_base, ver_array, 2, &dc_pool_proto_version);
	if (rc)
		return rc;

	if (dc_pool_proto_version == DAOS_POOL_VERSION - 1) {
		rc = daos_rpc_register(&pool_proto_fmt_v6, POOL_PROTO_CLI_COUNT, NULL,
				       DAOS_POOL_MODULE);
	} else if (dc_pool_proto_version == DAOS_POOL_VERSION) {
		rc = daos_rpc_register(&pool_proto_fmt_v7, POOL_PROTO_CLI_COUNT, NULL,
				       DAOS_POOL_MODULE);
	} else {
		D_ERROR("%d version pool RPC not supported.\n", dc_pool_proto_version);
		rc = -DER_PROTO;
	}

	if (rc)
		D_ERROR("failed to register daos %d version pool RPCs: "DF_RC"\n",
			dc_pool_proto_version, DP_RC(rc));

	return rc;
}

/**
 * Finalize pool interface
 */
void
dc_pool_fini(void)
{
	int rc;

	if (dc_pool_proto_version == DAOS_POOL_VERSION - 1) {
		rc = daos_rpc_unregister(&pool_proto_fmt_v6);
	} else if (dc_pool_proto_version == DAOS_POOL_VERSION) {
		rc = daos_rpc_unregister(&pool_proto_fmt_v7);
	} else {
		rc = -DER_PROTO;
		DL_ERROR(rc, "%d version pool RPC not supported", dc_pool_proto_version);
	}
	if (rc != 0)
		DL_ERROR(rc, "failed to unregister pool RPCs");

	if (daos_client_metric)
		daos_unregister_key(&dc_pool_module_key);
}

static int
dc_pool_metrics_start(struct dc_pool *pool)
{
	struct dc_pool_tls     *tls;
	struct dc_pool_metrics *metrics;
	int                     rc;

	if (!daos_client_metric)
		return 0;

	if (pool->dp_metrics != NULL)
		return 0;

	tls = dc_pool_tls_get();
	D_ASSERT(tls != NULL);

	metrics = dc_pool_metrics_lookup(tls, pool->dp_pool);
	if (metrics != NULL) {
		metrics->dp_ref++;
		pool->dp_metrics = metrics->dp_metrics;
		return 0;
	}

	rc = dc_pool_metrics_alloc(pool->dp_pool, &metrics);
	if (rc != 0)
		return rc;

	D_MUTEX_LOCK(&tls->dpc_metrics_list_lock);
	d_list_add(&metrics->dp_pool_list, &tls->dpc_metrics_list);
	D_MUTEX_UNLOCK(&tls->dpc_metrics_list_lock);
	metrics->dp_ref++;
	pool->dp_metrics = metrics->dp_metrics;

	return 0;
}

static void
dc_pool_metrics_stop(struct dc_pool *pool)
{
	struct dc_pool_metrics *metrics;
	struct dc_pool_tls     *tls;

	if (!daos_client_metric)
		return;

	if (pool->dp_metrics == NULL)
		return;

	tls = dc_pool_tls_get();
	D_ASSERT(tls != NULL);

	metrics = dc_pool_metrics_lookup(tls, pool->dp_pool);
	if (metrics != NULL)
		metrics->dp_ref--;

	pool->dp_metrics = NULL;
}

static void
pool_free(struct d_hlink *hlink)
{
	struct dc_pool *pool;

	pool = container_of(hlink, struct dc_pool, dp_hlink);
	D_ASSERT(daos_hhash_link_empty(&pool->dp_hlink));

	D_RWLOCK_RDLOCK(&pool->dp_co_list_lock);
	D_ASSERT(d_list_empty(&pool->dp_co_list));
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	D_RWLOCK_DESTROY(&pool->dp_map_lock);
	D_MUTEX_DESTROY(&pool->dp_client_lock);
	D_RWLOCK_DESTROY(&pool->dp_co_list_lock);

	if (pool->dp_map != NULL)
		pool_map_decref(pool->dp_map);

	dc_pool_metrics_stop(pool);

	rsvc_client_fini(&pool->dp_client);
	if (pool->dp_sys != NULL)
		dc_mgmt_sys_detach(pool->dp_sys);

	D_FREE(pool);
}

static struct d_hlink_ops pool_h_ops = {
	.hop_free	= pool_free,
};

void
dc_pool_get(struct dc_pool *pool)
{
	daos_hhash_link_getref(&pool->dp_hlink);
}

void
dc_pool_put(struct dc_pool *pool)
{
	daos_hhash_link_putref(&pool->dp_hlink);
}

struct dc_pool *
dc_hdl2pool(daos_handle_t poh)
{
	struct d_hlink *hlink;

	hlink = daos_hhash_link_lookup(poh.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct dc_pool, dp_hlink);
}

void
dc_pool_hdl_link(struct dc_pool *pool)
{
	daos_hhash_link_insert(&pool->dp_hlink, DAOS_HTYPE_POOL);
}

void
dc_pool_hdl_unlink(struct dc_pool *pool)
{
	daos_hhash_link_delete(&pool->dp_hlink);
}

static inline int
flags_are_valid(unsigned int flags)
{
	unsigned int mode = flags & (DAOS_PC_RO | DAOS_PC_RW | DAOS_PC_EX);

	return (mode == DAOS_PC_RO) || (mode == DAOS_PC_RW) ||
	       (mode == DAOS_PC_EX);
}

/* default number of components in pool map */
#define DC_POOL_DEFAULT_COMPONENTS_NR 128

struct dc_pool *
dc_pool_alloc(unsigned int nr)
{
	struct dc_pool *pool;
	int rc = 0;

	/** allocate and fill in pool connection */
	D_ALLOC_PTR(pool);
	if (pool == NULL) {
		return NULL;
	}

	daos_hhash_hlink_init(&pool->dp_hlink, &pool_h_ops);
	D_INIT_LIST_HEAD(&pool->dp_co_list);
	rc = D_RWLOCK_INIT(&pool->dp_co_list_lock, NULL);
	if (rc != 0)
		goto failed;
	rc = D_MUTEX_INIT(&pool->dp_client_lock, NULL);
	if (rc != 0) {
		D_RWLOCK_DESTROY(&pool->dp_co_list_lock);
		goto failed;
	}
	rc = D_RWLOCK_INIT(&pool->dp_map_lock, NULL);
	if (rc != 0) {
		D_RWLOCK_DESTROY(&pool->dp_co_list_lock);
		D_MUTEX_DESTROY(&pool->dp_client_lock);
		goto failed;
	}

	/* Every pool map begins at version 1. */
	pool->dp_map_version_known = 1;
	pool->dp_map_sz = pool_buf_size(nr);

	return pool;

failed:
	D_FREE(pool);
	return NULL;
}

/* Choose a pool service replica rank by label or UUID. If the rsvc module
 * indicates DER_NOTREPLICA, (clients only) try to refresh the list by querying
 * the MS.
 */
int
dc_pool_choose_svc_rank(const char *label, uuid_t puuid,
			struct rsvc_client *cli, pthread_mutex_t *cli_lock,
			struct dc_mgmt_sys *sys, crt_endpoint_t *ep)
{
	int			rc;
	int			i;

	if (cli_lock)
		D_MUTEX_LOCK(cli_lock);
choose:
	rc = rsvc_client_choose(cli, ep);
	if ((rc == -DER_NOTREPLICA) && !sys->sy_server) {
		d_rank_list_t *ranklist = NULL;

		/* Query MS for replica ranks. Not under client lock. */
		if (cli_lock)
			D_MUTEX_UNLOCK(cli_lock);
		rc = dc_mgmt_pool_find(sys, label, puuid, &ranklist);
		if (rc) {
			DL_CDEBUG(rc == -DER_NONEXIST, DB_PL, DLOG_ERR, rc,
				  DF_UUID ":%s: dc_mgmt_pool_find() failed", DP_UUID(puuid),
				  label ? label : "");
			return rc;
		}
		if (cli_lock)
			D_MUTEX_LOCK(cli_lock);

		/* Reinitialize rsvc client with new rank list, rechoose. */
		rsvc_client_fini(cli);
		rc = rsvc_client_init(cli, ranklist);
		d_rank_list_free(ranklist);
		ranklist = NULL;
		if (rc == 0) {
			for (i = 0; i < cli->sc_ranks->rl_nr; i++) {
				D_DEBUG(DB_MD, DF_UUID":%s: "
					"sc_ranks[%d]=%u\n", DP_UUID(puuid),
					label ? label : "", i,
					cli->sc_ranks->rl_ranks[i]);
			}
			goto choose;
		}
	}
	if (cli_lock)
		D_MUTEX_UNLOCK(cli_lock);
	return rc;
}

struct subtract_rsvc_rank_arg {
	struct pool_domain *srra_nodes;
	int                 srra_nodes_len;
};

static bool
subtract_rsvc_rank(d_rank_t rank, void *varg)
{
	struct subtract_rsvc_rank_arg *arg = varg;
	int                            i;

	for (i = 0; i < arg->srra_nodes_len; i++)
		if (arg->srra_nodes[i].do_comp.co_rank == rank)
			return !(arg->srra_nodes[i].do_comp.co_status & DC_POOL_SVC_MAP_STATES);
	return true;
}

/* The pool->dp_map_lock must have been held for write. */
static void
update_rsvc_client(struct dc_pool *pool)
{
	struct subtract_rsvc_rank_arg arg;

	arg.srra_nodes_len = pool_map_find_ranks(pool->dp_map, PO_COMP_ID_ALL, &arg.srra_nodes);
	/* There must be at least one rank. */
	D_ASSERTF(arg.srra_nodes_len > 0, "%d > 0\n", arg.srra_nodes_len);

	D_MUTEX_LOCK(&pool->dp_client_lock);
	rsvc_client_subtract(&pool->dp_client, subtract_rsvc_rank, &arg);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
}

/* Assume dp_map_lock is locked before calling this function */
int
dc_pool_map_update(struct dc_pool *pool, struct pool_map *map, bool connect)
{
	unsigned int	map_version;
	unsigned int	map_version_before = 0;
	int		rc;

	D_ASSERT(map != NULL);
	map_version = pool_map_get_version(map);

	if (pool->dp_map != NULL)
		map_version_before = pool_map_get_version(pool->dp_map);

	if (map_version <= map_version_before) {
		D_DEBUG(DB_MD, DF_UUID ": ignored pool map update: version=%u->%u pool=%p\n",
			DP_UUID(pool->dp_pool), map_version_before, map_version, pool);
		D_GOTO(out, rc = 0);
	}

	D_DEBUG(DB_MD, DF_UUID ": updating pool map: version=%u->%u\n", DP_UUID(pool->dp_pool),
		map_version_before, map_version);

	rc = pl_map_update(pool->dp_pool, map, connect, DEFAULT_PL_TYPE);
	if (rc != 0) {
		D_ERROR("Failed to refresh placement map: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (pool->dp_map != NULL)
		pool_map_decref(pool->dp_map);
	pool_map_addref(map);
	pool->dp_map = map;
	if (pool->dp_map_version_known < map_version)
		pool->dp_map_version_known = map_version;
	update_rsvc_client(pool);
	D_INFO(DF_UUID ": updated pool map: version=%u->%u\n", DP_UUID(pool->dp_pool),
	       map_version_before, map_version);
out:
	return rc;
}

static void
pool_print_range_list(struct dc_pool *pool, d_rank_range_list_t *list, bool enabled)
{
	const size_t	MAXBYTES = 512;
	char		line[MAXBYTES];
	char	       *linepos = line;
	int		ret;
	unsigned int	written = 0;
	unsigned int	remaining = MAXBYTES;
	int		i;

	ret = snprintf(linepos, remaining, DF_UUID": %s ranks: ", DP_UUID(pool->dp_pool),
		       enabled ? "ENABLED" : "DISABLED");
	if ((ret < 0) || (ret >= remaining))
		goto err;
	written += ret;
	remaining -= ret;
	linepos += ret;

	for (i = 0; i < list->rrl_nr; i++) {
		uint32_t	lo = list->rrl_ranges[i].lo;
		uint32_t	hi = list->rrl_ranges[i].hi;
		bool		lastrange = (i == (list->rrl_nr - 1));

		if (lo == hi)
			ret = snprintf(linepos, remaining, "%u%s", lo, lastrange ? "" : ",");
		else
			ret = snprintf(linepos, remaining, "%u-%u%s", lo, hi, lastrange ? "" : ",");
		if ((ret < 0) || (ret >= remaining))
			goto err;
		written += ret;
		remaining -= ret;
		linepos += ret;
	}
	D_DEBUG(DB_MD, "%s\n", line);
	return;
err:
	if (written > 0)
		D_DEBUG(DB_MD, "%s%s\n", line, (ret >= remaining) ? " ...(TRUNCATED): " : "");
	D_ERROR(DF_UUID": snprintf failed, %d\n", DP_UUID(pool->dp_pool), ret);
}

/*
 * Using "map_buf", "map_version", and "mode", update "pool->dp_map" and fill
 * "ranks" and/or "info", "prop" if not NULL.
 */
static int
process_query_reply(struct dc_pool *pool, struct pool_buf *map_buf,
		    uint32_t map_version, uint32_t leader_rank,
		    struct daos_pool_space *ps, struct daos_rebuild_status *rs,
		    d_rank_list_t **ranks, daos_pool_info_t *info,
		    daos_prop_t *prop_req, daos_prop_t *prop_reply,
		    bool connect)
{
	struct pool_map	       *map;
	int			rc;

	D_DEBUG(DB_MD, DF_UUID": info=%p (pi_bits="DF_X64"), ranks=%p\n",
		DP_UUID(pool->dp_pool), info, info ? info->pi_bits : 0, ranks);

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_RWLOCK_WRLOCK(&pool->dp_map_lock);
	rc = dc_pool_map_update(pool, map, connect);
	if (rc)
		goto out_unlock;

	/* Scan all targets for populating info->pi_ndisabled */
	if (info != NULL) {
		rc = pool_map_find_failed_tgts(map, NULL, &info->pi_ndisabled);
		if (rc != 0) {
			D_ERROR("Couldn't get failed targets, "DF_RC"\n", DP_RC(rc));
			goto out_unlock;
		}
	}

	/* Scan all targets for populating ranks */
	if (ranks != NULL) {
		d_rank_range_list_t	*range_list;
		bool	get_enabled = (info ? ((info->pi_bits & DPI_ENGINES_ENABLED) != 0) : false);

		rc = pool_map_get_ranks(pool->dp_pool, map, get_enabled, ranks);
		if (rc != 0)
			goto out_unlock;

		/* For debug logging - convert to rank ranges */
		range_list = d_rank_range_list_create_from_ranks(*ranks);
		if (range_list) {
			pool_print_range_list(pool, range_list, get_enabled);
			d_rank_range_list_free(range_list);
		}
	}

out_unlock:
	pool_map_decref(map); /* NB: protected by pool::dp_map_lock */
	/* Cache redun factor */
	if (rc == 0 && !pool->dp_rf_valid) {
		struct daos_prop_entry	*entry;

		entry = daos_prop_entry_get(prop_reply, DAOS_PROP_PO_REDUN_FAC);
		if (entry) {
			pool->dp_rf = entry->dpe_val;
			pool->dp_rf_valid = 1;
		}
	}
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	if (prop_req != NULL && rc == 0)
		rc = daos_prop_copy(prop_req, prop_reply);

	if (info != NULL && rc == 0)
		pool_query_reply_to_info(pool->dp_pool, map_buf, map_version,
					 leader_rank, ps, rs, info);

	return rc;
}

/*
 * Returns:
 *
 *   < 0			error; end the operation
 *   RSVC_CLIENT_RECHOOSE	task reinited; return 0 from completion cb
 *   RSVC_CLIENT_PROCEED	OK; proceed to process the reply
 */
static int
pool_rsvc_client_complete_rpc(struct dc_pool *pool, const crt_endpoint_t *ep,
			      int rc_crt, struct pool_op_out *out,
			      tse_task_t *task)
{
	int rc;

	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_complete_rpc(&pool->dp_client, ep, rc_crt, out->po_rc,
				      &out->po_hint);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(out->po_rc)))
		return RSVC_CLIENT_RECHOOSE;
	return RSVC_CLIENT_PROCEED;
}

struct pool_connect_arg {
	daos_pool_info_t	*pca_info;
	struct pool_buf		*pca_map_buf;
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
};

static void
warmup_cb(const struct crt_cb_info *info)
{
	sem_t *sem;

	if (info->cci_rc != 0)
		D_ERROR("Ping failed with rc = %d\n", info->cci_rc);

	sem = (sem_t *)info->cci_arg;

	sem_post(sem);
}

/*
 * Pro-actively ping each target in the pool map.
 * This forces underlying connection to be set up.
 */
static void
warmup(struct dc_pool *pool)
{
	static bool         parsed;
	static bool         enabled = false;
	crt_context_t       ctx;
	int                 i;
	int                 shift;
	struct pool_target *tgts;
	sem_t               sem;
	crt_bulk_t          bulk_hdl;
	void               *bulk_buf;
	int                 bulk_len = 4096;
	d_sg_list_t         sgl;
	d_iov_t             iov;
	int                 nr;
	int                 rc = 0;

	if (parsed && !enabled)
		/** fast path when disabled */
		return;

	D_MUTEX_LOCK(&warmup_lock);
	if (!parsed) {
		d_getenv_bool("D_POOL_WARMUP", &enabled);
		parsed = true;
	}
	if (!enabled) {
		D_MUTEX_UNLOCK(&warmup_lock);
		return;
	}

	D_ALLOC(bulk_buf, bulk_len);
	if (bulk_buf == NULL) {
		D_ERROR("Failed to alloc mem\n");
		goto out_unlock;
	}

	ctx = daos_get_crt_ctx();

	d_iov_set(&iov, bulk_buf, bulk_len);
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &iov;
	rc            = crt_bulk_create(ctx, &sgl, CRT_BULK_RW, &bulk_hdl);
	if (rc < 0) {
		D_ERROR("Failed to create bulk handle\n");
		goto out_bulk;
	}

	rc = sem_init(&sem, 0, 0);
	if (rc < 0) {
		D_ERROR("Failed to initialize semaphore\n");
		goto out_hdl;
	}

	/** retrieve all targets from the pool map */
	nr = pool_map_find_target(pool->dp_map, PO_COMP_ID_ALL, &tgts);

	/** Randomize start order to minimize load for large-scale job */
	shift = rand() + (int)getpid();
	if (shift < 0)
		shift = rand();

	D_DEBUG(DB_TRACE, "Pinging %d targets, shifting at %d\n", nr, shift);

	for (i = 0; i < nr; i++) {
		crt_endpoint_t             ep;
		crt_rpc_t                 *rpc = NULL;
		struct pool_tgt_warmup_in *rpc_in;
		int                        idx;
		crt_opcode_t               opcode;

		idx        = (i + shift) % nr;
		if (tgts[idx].ta_comp.co_status == PO_COMP_ST_DOWN ||
		    tgts[idx].ta_comp.co_status == PO_COMP_ST_DOWNOUT)
			continue;
		ep.ep_grp  = pool->dp_sys->sy_group;
		ep.ep_rank = tgts[idx].ta_comp.co_rank;
		ep.ep_tag  = daos_rpc_tag(DAOS_REQ_TGT, tgts[idx].ta_comp.co_index);
		opcode     = DAOS_RPC_OPCODE(POOL_TGT_WARMUP, DAOS_POOL_MODULE,
                                         dc_pool_proto_version ? dc_pool_proto_version
								   : DAOS_POOL_VERSION);
		rc         = crt_req_create(ctx, &ep, opcode, &rpc);
		if (rc != 0) {
			D_ERROR("Failed to allocate req " DF_RC "\n", DP_RC(rc));
			goto out_sem;
		}
		D_ASSERTF(rc == 0, "crt_req_create failed; rc=%d\n", rc);
		rpc_in          = crt_req_get(rpc);
		rpc_in->tw_bulk = bulk_hdl;

		rc = crt_req_send(rpc, warmup_cb, &sem);
		if (rc != 0) {
			D_ERROR("Failed to ping rank=%d:%d, " DF_RC "\n", ep.ep_rank, ep.ep_tag,
				DP_RC(rc));
			goto out_sem;
		}

		while (sem_trywait(&sem) == -1) {
			rc = crt_progress(ctx, 0);
			if (rc && rc != -DER_TIMEDOUT) {
				D_ERROR("failed to progress context, " DF_RC "\n", DP_RC(rc));
				break;
			}
		}
		rc = 0;
	}
	D_DEBUG(DB_TRACE, "Pinging done\n");

out_sem:
	(void)sem_destroy(&sem);
out_hdl:
	crt_bulk_free(bulk_hdl);
out_bulk:
	D_FREE(bulk_buf);
out_unlock:
	D_MUTEX_UNLOCK(&warmup_lock);
}

static int
pool_connect_cp(tse_task_t *task, void *data)
{
	struct pool_connect_arg   *arg = (struct pool_connect_arg *)data;
	struct pool_task_priv     *tpriv   = dc_task_get_priv(task);
	daos_pool_info_t	  *info = arg->pca_info;
	struct pool_buf		  *map_buf = arg->pca_map_buf;
	struct pool_connect_out   *pco     = crt_reply_get(arg->rpc);
	crt_bulk_t                 bulk;
	d_iov_t                   *credp;
	bool                       reinit = false;
	int			   rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(tpriv->pool, &arg->rpc->cr_ep, rc, &pco->pco_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc) {
		D_ERROR("RPC error while connecting to pool: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = pco->pco_op.po_rc;
	if (rc == -DER_TRUNC) {
		/* retry with map buffer size required by server */
		D_DEBUG(DB_MD, "current pool map buffer size (%ld) < size "
			"required by server (%u), retry after allocating it\n",
			pool_buf_size(map_buf->pb_nr), pco->pco_map_buf_size);
		tpriv->pool->dp_map_sz = pco->pco_map_buf_size;
		reinit                 = true;
		D_GOTO(out, rc);
	} else if (rc != 0) {
		D_ERROR("failed to connect to pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = process_query_reply(tpriv->pool, map_buf, pco->pco_op.po_map_version,
				 pco->pco_op.po_hint.sh_rank, &pco->pco_space, &pco->pco_rebuild_st,
				 NULL /* tgts */, info, NULL, NULL, true);
	if (rc != 0) {
		if (rc == -DER_AGAIN) {
			reinit = true;
			D_GOTO(out, rc);
		}

		/* TODO: What do we do about the remote connection state? */
		D_ERROR("failed to create local pool map: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = dc_mgmt_notify_pool_connect(tpriv->pool);
	if (rc != 0) {
		D_ERROR("failed to register pool connect with agent: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* add pool to hhash */
	dc_pool_hdl_link(tpriv->pool);       /* +1 ref */
	dc_pool2hdl(tpriv->pool, arg->hdlp); /* +1 ref */

	D_DEBUG(DB_MD, DF_UUID ": connected: cookie=" DF_X64 " hdl=" DF_UUID " master\n",
		DP_UUID(tpriv->pool->dp_pool), arg->hdlp->cookie,
		DP_UUID(tpriv->pool->dp_pool_hdl));

	warmup(tpriv->pool);
out:
	pool_connect_in_get_cred(arg->rpc, &credp);
	pool_connect_in_get_data(arg->rpc, NULL /* flags */, NULL /* bits */, &bulk,
				 NULL /* version */);
	crt_req_decref(arg->rpc);
	map_bulk_destroy(bulk, map_buf);
	/* Ensure credential memory is wiped clean */
	explicit_bzero(credp->iov_buf, credp->iov_buf_len);
	daos_iov_free(credp);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit) {
		dc_pool_put(tpriv->pool);
		pool_task_destroy_priv(task);
	}
	return rc;
}

/* allocate and initialize a dc_pool by label or uuid */
static int
init_pool(const char *label, uuid_t uuid, uint64_t capas, const char *grp,
	  struct dc_pool **poolp)
{
	struct dc_pool	*pool;
	int		 rc;

	pool = dc_pool_alloc(DC_POOL_DEFAULT_COMPONENTS_NR);
	if (pool == NULL)
		return -DER_NOMEM;

	if (label)
		uuid_clear(pool->dp_pool);
	else
		uuid_copy(pool->dp_pool, uuid);
	uuid_generate(pool->dp_pool_hdl);
	pool->dp_capas = capas;

	/** attach to the server group and initialize rsvc_client */
	rc = dc_mgmt_sys_attach(grp, &pool->dp_sys);
	if (rc != 0)
		D_GOTO(err_pool, rc);

	/** Agent configuration data from pool->dp_sys->sy_info */
	/** sy_info.provider */
	/** sy_info.interface */
	/** sy_info.domain */
	/** sy_info.crt_timeout */

	rc = rsvc_client_init(&pool->dp_client, NULL);
	if (rc != 0)
		D_GOTO(err_pool, rc);

	*poolp = pool;
	return 0;

err_pool:
	dc_pool_put(pool);
	return rc;
}

static int
dc_pool_connect_internal(tse_task_t *task, daos_pool_info_t *info, const char *label,
			 daos_handle_t *poh)
{
	struct pool_task_priv  *tpriv = dc_task_get_priv(task);
	struct dc_pool         *pool  = tpriv->pool;
	crt_endpoint_t          ep;
	crt_rpc_t              *rpc;
	struct pool_buf        *map_buf;
	struct pool_connect_arg con_args;
	d_iov_t                *credp;
	crt_bulk_t              bulk;
	int                     rc;

	/** Choose an endpoint and create an RPC. */
	ep.ep_grp = tpriv->pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(label, pool->dp_pool, &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		DL_CDEBUG(rc == -DER_NONEXIST, DB_PL, DLOG_ERR, rc,
			  DF_UUID ":%s: cannot find pool service", DP_UUID(pool->dp_pool),
			  label ? label : "");
		goto out;
	}

	rc = dc_pool_metrics_start(pool);
	if (rc != 0)
		D_GOTO(out, rc);

	/** Pool connect RPC by UUID (provided, or looked up by label above) */
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_CONNECT, pool->dp_pool,
			     pool->dp_pool_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, "failed to create rpc");
		D_GOTO(out, rc);
	}
	/** for con_args */
	crt_req_addref(rpc);

	/** request credentials */
	pool_connect_in_get_cred(rpc, &credp);
	rc = dc_sec_request_creds(credp);
	if (rc != 0) {
		DL_ERROR(rc, "failed to obtain security credential");
		D_GOTO(out_req, rc);
	}

	rc = map_bulk_create(daos_task2ctx(task), &bulk, &map_buf, pool_buf_nr(pool->dp_map_sz));
	if (rc != 0)
		D_GOTO(out_cred, rc);

	/** fill in request buffer */
	pool_connect_in_set_data(rpc, pool->dp_capas, pool_query_bits(info, NULL), bulk,
				 DAOS_POOL_GLOBAL_VERSION);

	/** Prepare "con_args" for pool_connect_cp(). */
	con_args.pca_info = info;
	con_args.pca_map_buf = map_buf;
	con_args.rpc = rpc;
	con_args.hdlp = poh;

	rc = tse_task_register_comp_cb(task, pool_connect_cp, &con_args, sizeof(con_args));
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return daos_rpc_send(rpc, task);

out_bulk:
	map_bulk_destroy(bulk, map_buf);
out_cred:
	/* Ensure credential memory is wiped clean */
	explicit_bzero(credp->iov_buf, credp->iov_buf_len);
	daos_iov_free(credp);
out_req:
	crt_req_decref(rpc);
	crt_req_decref(rpc); /* free req */
out:
	return rc;
}

int
dc_pool_connect(tse_task_t *task)
{
	daos_pool_connect_t	*args;
	struct pool_task_priv   *tpriv = dc_task_get_priv(task);
	const char		*label;
	uuid_t			 uuid;
	int			 rc;

	args = dc_task_get_args(task);

	if (daos_uuid_valid(args->uuid)) {
		/** Backward compatibility, we are provided a UUID */
		label = NULL;
		uuid_copy(uuid, args->uuid);
	} else if (daos_label_is_valid(args->pool)) {
		/** The provided string is a valid label */
		uuid_clear(uuid);
		label = args->pool;
	} else if (uuid_parse(args->pool, uuid) == 0) {
		/**
		 * The provided string was successfully parsed as a
		 * UUID
		 */
		label = NULL;
	} else {
		/** neither a label nor a UUID ... try again */
		D_GOTO(out_task, rc = -DER_INVAL);
	}

	if (tpriv == NULL) {
		if (!flags_are_valid(args->flags) || args->poh == NULL)
			D_GOTO(out_task, rc = -DER_INVAL);

		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_task;

		/** allocate and fill in pool connection */
		rc = init_pool(label, uuid, args->flags, args->grp, &tpriv->pool);
		if (rc)
			goto out_task;

		D_DEBUG(DB_MD, "%s: connecting: hdl=" DF_UUIDF " flags=%x\n",
			args->pool ?: "<compat>", DP_UUID(tpriv->pool->dp_pool_hdl), args->flags);
	}

	rc = dc_pool_connect_internal(task, args->info, label, args->poh);
	if (rc)
		goto out_pool;

	return rc;

out_pool:
	dc_pool_put(tpriv->pool);
out_task:
	if (tpriv)
		pool_task_destroy_priv(task);
	tse_task_complete(task, rc);
	return rc;
}

struct pool_disconnect_arg {
	struct dc_pool		*pool;
	crt_rpc_t		*rpc;
	daos_handle_t		 hdl;
};

static int
pool_disconnect_cp(tse_task_t *task, void *data)
{
	struct pool_disconnect_arg	*arg =
		(struct pool_disconnect_arg *)data;
	struct dc_pool                  *pool   = arg->pool;
	struct pool_disconnect_out	*pdo = crt_reply_get(arg->rpc);
	bool                             reinit = false;
	int				 rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &pdo->pdo_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc) {
		D_ERROR("RPC error while disconnecting from pool: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = pdo->pdo_op.po_rc;
	if (rc) {
		D_ERROR("failed to disconnect from pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_UUID": disconnected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), arg->hdl.cookie,
		DP_UUID(pool->dp_pool_hdl));

	pl_map_disconnect(pool->dp_pool);

	rc = dc_mgmt_notify_pool_disconnect(pool);
	if (rc != 0) {
		/* It's not fatal if we don't notify the agent of the disconnect
		 * however it isn't ideal. It will try to send the control plane
		 * a clean up rpc on process termination however it will be noop
		 * on the server side.
		 */
		D_ERROR("failed to notify agent of pool disconnect: "DF_RC"\n",
			DP_RC(rc));
		rc = 0;
	}

	/* remove pool from hhash */
	dc_pool_hdl_unlink(pool);
	dc_pool_put(pool);
	arg->hdl.cookie = 0;

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		pool_task_destroy_priv(task);
	return rc;
}

int
dc_pool_disconnect(tse_task_t *task)
{
	daos_pool_disconnect_t		*args;
	struct dc_pool			*pool;
	struct pool_task_priv           *tpriv = dc_task_get_priv(task);
	crt_endpoint_t			 ep;
	crt_rpc_t                       *rpc;
	struct pool_disconnect_arg	 disc_args;
	int				 rc = 0;

	args = dc_task_get_args(task);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DB_MD, DF_UUID": disconnecting: hdl="DF_UUID" cookie="DF_X64
		"\n", DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		args->poh.cookie);

	D_RWLOCK_RDLOCK(&pool->dp_co_list_lock);
	if (!d_list_empty(&pool->dp_co_list)) {
		D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);
		D_ERROR("cannot disconnect pool "DF_UUID", container not closed, "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(-DER_BUSY));
		D_GOTO(out_pool, rc = -DER_BUSY);
	}
	pool->dp_disconnecting = 1;
	D_RWLOCK_UNLOCK(&pool->dp_co_list_lock);

	if (pool->dp_slave) {
		D_DEBUG(DB_MD, DF_UUID": disconnecting: cookie="DF_X64" hdl="
			DF_UUID" slave\n", DP_UUID(pool->dp_pool),
			args->poh.cookie, DP_UUID(pool->dp_pool_hdl));

		pl_map_disconnect(pool->dp_pool);
		/* remove pool from hhash */
		dc_pool_hdl_unlink(pool);
		dc_pool_put(pool);
		args->poh.cookie = 0;
		D_GOTO(out_pool, rc);
	}

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_pool;
	}

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_DISCONNECT, pool->dp_pool,
			     pool->dp_pool_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, "failed to create rpc");
		D_GOTO(out_pool, rc);
	}

	disc_args.pool = pool;
	disc_args.hdl = args->poh;
	crt_req_addref(rpc);
	disc_args.rpc = rpc;

	rc = tse_task_register_comp_cb(task, pool_disconnect_cp, &disc_args,
				       sizeof(disc_args));
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return daos_rpc_send(rpc, task);

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
out_task:
	if (tpriv)
		pool_task_destroy_priv(task);
	tse_task_complete(task, rc);
	return rc;
}

#define DC_POOL_GLOB_MAGIC	(0x16da0386)

/* Structure of global buffer for dc_pool */
struct dc_pool_glob {
	/* magic number, DC_POOL_GLOB_MAGIC */
	uint32_t	dpg_magic;
	uint32_t	dpg_padding;
	/* pool UUID, pool handle UUID, and capas */
	uuid_t		dpg_pool;
	uuid_t		dpg_pool_hdl;
	uint64_t	dpg_capas;
	/* poolmap version */
	uint32_t	dpg_map_version;
	/* number of component of poolbuf, same as pool_buf::pb_nr */
	uint32_t	dpg_map_pb_nr;
	struct pool_buf	dpg_map_buf[0];
	/* rsvc_client */
	/* dc_mgmt_sys */
};

static inline daos_size_t
dc_pool_glob_buf_size(unsigned int pb_nr, size_t client_len, size_t sys_len)
{
	return offsetof(struct dc_pool_glob, dpg_map_buf) +
	       pool_buf_size(pb_nr) + client_len + sys_len;
}

static inline void
swap_pool_buf(struct pool_buf *pb)
{
	struct pool_component	*pool_comp;
	int			 i;

	D_ASSERT(pb != NULL);

	D_SWAP32S(&pb->pb_csum);
	D_SWAP32S(&pb->pb_nr);
	D_SWAP32S(&pb->pb_domain_nr);
	D_SWAP32S(&pb->pb_target_nr);

	for (i = 0; i < pb->pb_nr; i++) {
		pool_comp = &pb->pb_comps[i];
		/* skip pool_comp->co_type (uint8_t) */
		/* skip pool_comp->co_status (uint8_t) */
		/* skip pool_comp->co_index (uint8_t) */
		/* skip pool_comp->co_padding (uint8_t) */
		D_SWAP32S(&pool_comp->co_id);
		D_SWAP32S(&pool_comp->co_rank);
		D_SWAP32S(&pool_comp->co_ver);
		D_SWAP32S(&pool_comp->co_fseq);
		D_SWAP32S(&pool_comp->co_nr);
	}
}

static inline void
swap_pool_glob(struct dc_pool_glob *pool_glob)
{
	D_ASSERT(pool_glob != NULL);

	D_SWAP32S(&pool_glob->dpg_magic);
	/* skip pool_glob->dpg_padding */
	/* skip pool_glob->dpg_pool (uuid_t) */
	/* skip pool_glob->dpg_pool_hdl (uuid_t) */
	D_SWAP64S(&pool_glob->dpg_capas);
	D_SWAP32S(&pool_glob->dpg_map_version);
	D_SWAP32S(&pool_glob->dpg_map_pb_nr);
	swap_pool_buf(pool_glob->dpg_map_buf);
}

static int
dc_pool_l2g(daos_handle_t poh, d_iov_t *glob)
{
	struct dc_pool		*pool;
	struct pool_buf		*map_buf;
	uint32_t		 map_version;
	struct dc_pool_glob	*pool_glob;
	daos_size_t		 glob_buf_size;
	uint32_t		 pb_nr;
	void			*client_buf;
	size_t			 client_len;
	size_t			 sys_len;
	void			*p;
	int			 rc = 0;

	D_ASSERT(glob != NULL);

	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	map_version = pool_map_get_version(pool->dp_map);
	rc = pool_buf_extract(pool->dp_map, &map_buf);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	if (rc != 0)
		D_GOTO(out_pool, rc);

	D_MUTEX_LOCK(&pool->dp_client_lock);
	client_len = rsvc_client_encode(&pool->dp_client, NULL /* buf */);
	D_ALLOC(client_buf, client_len);
	if (client_buf == NULL) {
		D_MUTEX_UNLOCK(&pool->dp_client_lock);
		D_GOTO(out_map_buf, rc = -DER_NOMEM);
	}
	rsvc_client_encode(&pool->dp_client, client_buf);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);

	sys_len = dc_mgmt_sys_encode(pool->dp_sys, NULL /* buf */, 0 /* cap */);

	pb_nr = map_buf->pb_nr;
	glob_buf_size = dc_pool_glob_buf_size(pb_nr, client_len, sys_len);
	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_client_buf, rc = 0);
	}
	if (glob->iov_buf_len < glob_buf_size) {
		rc = -DER_TRUNC;
		D_ERROR("Larger glob buffer needed (" DF_U64 " bytes provided, " DF_U64
			" required) " DF_RC "\n",
			glob->iov_buf_len, glob_buf_size, DP_RC(rc));
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_client_buf, rc);
	}
	glob->iov_len = glob_buf_size;

	/* init pool global handle */
	pool_glob = (struct dc_pool_glob *)glob->iov_buf;
	pool_glob->dpg_magic = DC_POOL_GLOB_MAGIC;
	uuid_copy(pool_glob->dpg_pool, pool->dp_pool);
	uuid_copy(pool_glob->dpg_pool_hdl, pool->dp_pool_hdl);
	pool_glob->dpg_capas = pool->dp_capas;
	pool_glob->dpg_map_version = map_version;
	pool_glob->dpg_map_pb_nr = pb_nr;
	memcpy(pool_glob->dpg_map_buf, map_buf, pool_buf_size(pb_nr));
	/* rsvc_client */
	p = (void *)pool_glob->dpg_map_buf + pool_buf_size(pb_nr);
	memcpy(p, client_buf, client_len);
	/* dc_mgmt_sys */
	p += client_len;
	rc = dc_mgmt_sys_encode(pool->dp_sys, p,
				glob_buf_size - (p - (void *)pool_glob));
	D_ASSERTF(rc == sys_len, "%d == %zu\n", rc, sys_len);
	rc = 0;

out_client_buf:
	D_FREE(client_buf);
out_map_buf:
	pool_buf_free(map_buf);
out_pool:
	dc_pool_put(pool);
out:
	if (rc != 0)
		D_ERROR("failed, rc: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_pool_local2global(daos_handle_t poh, d_iov_t *glob)
{
	int	rc = 0;

	if (glob == NULL) {
		D_DEBUG(DB_MD, "Invalid parameter, NULL glob pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
	    glob->iov_buf_len < glob->iov_len)) {
		D_DEBUG(DB_MD, "Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob->iov_buf, glob->iov_buf_len, glob->iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dc_pool_l2g(poh, glob);

out:
	return rc;
}

static int
dc_pool_g2l(struct dc_pool_glob *pool_glob, size_t len, daos_handle_t *poh)
{
	struct dc_pool		*pool;
	struct pool_buf		*map_buf;
	struct pool_map		*map = NULL;
	void			*p;
	int			 rc = 0;

	D_ASSERT(pool_glob != NULL);
	D_ASSERT(poh != NULL);
	map_buf = pool_glob->dpg_map_buf;
	D_ASSERT(map_buf != NULL);

	/** allocate and fill in pool connection */
	pool = dc_pool_alloc(pool_glob->dpg_map_pb_nr);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(pool->dp_pool, pool_glob->dpg_pool);
	uuid_copy(pool->dp_pool_hdl, pool_glob->dpg_pool_hdl);
	pool->dp_capas = pool_glob->dpg_capas;
	/* set slave flag to avoid export it again */
	pool->dp_slave = 1;
	p = (void *)map_buf + pool_buf_size(map_buf->pb_nr);
	rc = rsvc_client_decode(p, len - (p - (void *)pool_glob),
				&pool->dp_client);
	if (rc < 0)
		goto out;

	p += rc;
	rc = dc_mgmt_sys_decode(p, len - (p - (void *)pool_glob),
				&pool->dp_sys);
	if (rc < 0)
		goto out;

	rc = dc_pool_metrics_start(pool);
	if (rc != 0)
		goto out;

	rc = pool_map_create(map_buf, pool_glob->dpg_map_version, &map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = dc_pool_map_update(pool, map, true /* connect */);
	if (rc != 0)
		D_GOTO(out, rc);

	/* add pool to hash */
	dc_pool_hdl_link(pool); /* +1 ref */
	dc_pool2hdl(pool, poh); /* +1 ref */

	D_DEBUG(DB_MD, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" slave\n", DP_UUID(pool->dp_pool), poh->cookie,
		DP_UUID(pool->dp_pool_hdl));

	warmup(pool);
out:
	if (rc != 0)
		D_ERROR("failed, rc: "DF_RC"\n", DP_RC(rc));
	if (map != NULL)
		pool_map_decref(map);
	if (pool != NULL)
		dc_pool_put(pool);
	return rc;
}

int
dc_pool_global2local(d_iov_t glob, daos_handle_t *poh)
{
	struct dc_pool_glob	 *pool_glob;
	int			  rc = 0;

	if (glob.iov_buf == NULL || glob.iov_buf_len == 0 ||
	    glob.iov_len == 0 || glob.iov_buf_len < glob.iov_len) {
		D_DEBUG(DB_MD, "Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (poh == NULL) {
		D_DEBUG(DB_MD, "Invalid parameter, NULL poh.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	pool_glob = (struct dc_pool_glob *)glob.iov_buf;
	if (pool_glob->dpg_magic == D_SWAP32(DC_POOL_GLOB_MAGIC)) {
		swap_pool_glob(pool_glob);
		D_ASSERT(pool_glob->dpg_magic == DC_POOL_GLOB_MAGIC);
	} else if (pool_glob->dpg_magic != DC_POOL_GLOB_MAGIC) {
		D_ERROR("Bad dpg_magic: %#x.\n", pool_glob->dpg_magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dc_pool_g2l(pool_glob, glob.iov_len, poh);
	if (rc != 0)
		D_ERROR("failed, rc: "DF_RC"\n", DP_RC(rc));

out:
	return rc;
}

int
dc_pool_hdl2uuid(daos_handle_t poh, uuid_t *hdl_uuid, uuid_t *uuid)
{
	struct dc_pool *dp;

	dp = dc_hdl2pool(poh);
	if (dp == NULL)
		return -DER_NO_HDL;

	if (hdl_uuid != NULL)
		uuid_copy(*hdl_uuid, dp->dp_pool_hdl);
	if (uuid != NULL)
		uuid_copy(*uuid, dp->dp_pool);
	dc_pool_put(dp);
	return 0;
}

struct pool_update_state {
	struct rsvc_client	client;
	struct dc_mgmt_sys     *sys;
};

static int
pool_tgt_update_cp(tse_task_t *task, void *data)
{
	struct pool_task_priv           *tpriv = dc_task_get_priv(task);
	crt_rpc_t			*rpc = *((crt_rpc_t **)data);
	struct pool_tgt_update_in	*in = crt_req_get(rpc);
	struct pool_tgt_update_out	*out = crt_reply_get(rpc);
	struct pool_target_addr         *addrs;
	int                              n_addrs;
	bool                             reinit = false;
	int				 rc = task->dt_result;
	uint32_t                         flags;

	rc = rsvc_client_complete_rpc(&tpriv->state->client, &rpc->cr_ep, rc, out->pto_op.po_rc,
				      &out->pto_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED &&
	     daos_rpc_retryable_rc(out->pto_op.po_rc))) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while excluding targets: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->pto_op.po_rc;
	if (rc != 0) {
		D_ERROR("failed to exclude targets: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_UUID": updated: hdl="DF_UUID" failed=%d\n",
		DP_UUID(in->pti_op.pi_uuid), DP_UUID(in->pti_op.pi_hdl),
		(int)out->pto_addr_list.ca_count);

	pool_tgt_update_in_get_data(rpc, &addrs, &n_addrs, &flags);
	D_FREE(addrs);

	if (out->pto_addr_list.ca_arrays != NULL &&
	    out->pto_addr_list.ca_count > 0) {
		D_ERROR("tgt update failed count %zd\n",
			out->pto_addr_list.ca_count);
		rc = -DER_INVAL;
	}

out:
	crt_req_decref(rpc);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit) {
		rsvc_client_fini(&tpriv->state->client);
		dc_mgmt_sys_detach(tpriv->state->sys);
		D_FREE(tpriv->state);
		pool_task_destroy_priv(task);
	}
	return rc;
}

static int
dc_pool_update_internal(tse_task_t *task, daos_pool_update_t *args, int opc)
{
	struct pool_task_priv           *tpriv = dc_task_get_priv(task);
	crt_endpoint_t			 ep;
	crt_rpc_t                       *rpc;
	struct pool_target_addr_list	list;
	uuid_t                           null_uuid;
	int				i;
	int				rc;

	if (args->tgts == NULL || args->tgts->tl_nr == 0) {
		D_ERROR("NULL tgts or tgts->tl_nr is zero\n");
		D_GOTO(out_task, rc = -DER_INVAL);
	}

	D_DEBUG(DB_MD, DF_UUID": opc %d targets:%u tgts[0]=%u/%d\n",
		DP_UUID(args->uuid), opc, args->tgts->tl_nr,
		args->tgts->tl_ranks[0], args->tgts->tl_tgts[0]);

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_task;

		D_ALLOC_PTR(tpriv->state);
		if (tpriv->state == NULL) {
			D_GOTO(out_task, rc = -DER_NOMEM);
		}

		rc = dc_mgmt_sys_attach(args->grp, &tpriv->state->sys);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to sys attach, rc %d.\n",
				DP_UUID(args->uuid), rc);
			D_GOTO(out_state, rc);
		}
		rc = rsvc_client_init(&tpriv->state->client,
				      tpriv->state->sys->sy_server ? args->svc : NULL);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to rsvc_client_init, rc %d.\n",
				DP_UUID(args->uuid), rc);
			D_GOTO(out_group, rc);
		}
	}

	ep.ep_grp = tpriv->state->sys->sy_group;
	rc        = dc_pool_choose_svc_rank(NULL /* label */, args->uuid, &tpriv->state->client,
					    NULL /* mutex */, tpriv->state->sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(args->uuid), DP_RC(rc));
		goto out_client;
	}
	uuid_clear(null_uuid);
	rc = pool_req_create(daos_task2ctx(task), &ep, opc, args->uuid, null_uuid, &tpriv->rq_time,
			     &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_client, rc);
	}

	rc = pool_target_addr_list_alloc(args->tgts->tl_nr, &list);
	if (rc) {
		D_ERROR(DF_UUID": pool_target_addr_list_alloc failed, rc %d.\n",
			DP_UUID(args->uuid), rc);
		D_GOTO(out_rpc, rc);
	}

	for (i = 0; i < args->tgts->tl_nr; i++) {
		list.pta_addrs[i].pta_rank = args->tgts->tl_ranks[i];
		list.pta_addrs[i].pta_target = args->tgts->tl_tgts[i];
	}

	pool_tgt_update_in_set_data(rpc, list.pta_addrs, (size_t)list.pta_number,
				    POOL_TGT_UPDATE_SKIP_RF_CHECK);

	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, pool_tgt_update_cp, &rpc, sizeof(rpc));
	if (rc != 0)
		D_GOTO(out_list, rc);

	return daos_rpc_send(rpc, task);

out_list:
	pool_target_addr_list_free(&list);
	crt_req_decref(rpc);
out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&tpriv->state->client);
out_group:
	dc_mgmt_sys_detach(tpriv->state->sys);
out_state:
	D_FREE(tpriv->state);
out_task:
	if (tpriv)
		pool_task_destroy_priv(task);
	tse_task_complete(task, rc);
	return rc;
}

int
dc_pool_exclude(tse_task_t *task)
{
	daos_pool_update_t *args;

	args = dc_task_get_args(task);

	return dc_pool_update_internal(task, args, POOL_EXCLUDE);
}

int
dc_pool_reint(tse_task_t *task)
{
	daos_pool_update_t *args;

	args = dc_task_get_args(task);

	return dc_pool_update_internal(task, args, POOL_REINT);
}

int
dc_pool_drain(tse_task_t *task)
{
	daos_pool_update_t *args;

	args = dc_task_get_args(task);

	return dc_pool_update_internal(task, args, POOL_DRAIN);
}

int
dc_pool_exclude_out(tse_task_t *task)
{
	daos_pool_update_t *args;

	args = dc_task_get_args(task);

	return dc_pool_update_internal(task, args, POOL_EXCLUDE_OUT);
}

struct pool_query_arg {
	struct dc_pool		*dqa_pool;
	d_rank_list_t	       **dqa_ranks;
	daos_pool_info_t	*dqa_info;
	daos_prop_t		*dqa_prop;
	crt_bulk_t               dqa_bulk;
	struct pool_buf		*dqa_map_buf;
	crt_rpc_t		*rpc;
};

static int
pool_query_cb(tse_task_t *task, void *data)
{
	struct pool_query_arg	       *arg = (struct pool_query_arg *)data;
	struct pool_buf		       *map_buf = arg->dqa_map_buf;
	struct pool_query_out          *out_v5  = crt_reply_get(arg->rpc);
	d_rank_list_t		       *ranks = NULL;
	d_rank_list_t		      **ranks_arg;
	bool                            reinit = false;
	int				rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->dqa_pool, &arg->rpc->cr_ep, rc,
					   &out_v5->pqo_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	D_DEBUG(DB_MD, DF_UUID": query rpc done: %d\n",
		DP_UUID(arg->dqa_pool->dp_pool), rc);

	if (rc) {
		D_ERROR("RPC error while querying pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out_v5->pqo_op.po_rc;
	if (rc == -DER_TRUNC) {
		struct dc_pool *pool = arg->dqa_pool;

		D_WARN("pool map buffer size (%ld) < required (%u)\n",
		       pool_buf_size(map_buf->pb_nr), out_v5->pqo_map_buf_size);

		/* retry with map buffer size required by server */
		D_INFO("retry with map buffer size required by server (%ul)\n",
		       out_v5->pqo_map_buf_size);
		pool->dp_map_sz = out_v5->pqo_map_buf_size;
		reinit          = true;
		D_GOTO(out, rc = 0);
	} else if (rc != 0) {
		D_ERROR("failed to query pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	ranks_arg = arg->dqa_ranks ? arg->dqa_ranks : &ranks;

	rc = process_query_reply(arg->dqa_pool, map_buf,
				 out_v5->pqo_op.po_map_version,
				 out_v5->pqo_op.po_hint.sh_rank,
				 &out_v5->pqo_space, &out_v5->pqo_rebuild_st,
				 ranks_arg, arg->dqa_info,
				 arg->dqa_prop, out_v5->pqo_prop, false);
	if (rc != 0) {
		if (rc == -DER_AGAIN) {
			reinit = true;
			D_GOTO(out, rc = 0);
		}
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_MD, DF_UUID": got ranklist with %u ranks\n",
		DP_UUID(arg->dqa_pool->dp_pool), (*ranks_arg)->rl_nr);
	if (ranks_arg == &ranks)
		d_rank_list_free(ranks);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->dqa_pool);
	map_bulk_destroy(arg->dqa_bulk, map_buf);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		pool_task_destroy_priv(task);
	return rc;
}

/**
 * Query the latest pool information.
 *
 * For pool map refreshes, use dc_pool_create_map_refresh_task instead.
 */
int
dc_pool_query(tse_task_t *task)
{
	daos_pool_query_t	       *args;
	struct pool_task_priv          *tpriv;
	struct dc_pool		       *pool;
	crt_endpoint_t			ep;
	crt_rpc_t                      *rpc;
	struct pool_buf		       *map_buf;
	struct pool_query_arg		query_args;
	int				rc;

	args = dc_task_get_args(task);
	tpriv = dc_task_get_priv(task);

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_task;
	}

	/** Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_tpriv, rc = -DER_NO_HDL);

	D_DEBUG(DB_MD, DF_UUID": querying: hdl="DF_UUID" ranks=%p info=%p\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		args->ranks, args->info);

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_QUERY, pool->dp_pool, pool->dp_pool_hdl,
			     NULL /* req_timep */, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create pool query rpc", DP_UUID(pool->dp_pool));
		D_GOTO(out_pool, rc);
	}

	/** +1 for args */
	crt_req_addref(rpc);

	rc = map_bulk_create(daos_task2ctx(task), &query_args.dqa_bulk, &map_buf,
			     pool_buf_nr(pool->dp_map_sz));
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	pool_query_in_set_data(rpc, query_args.dqa_bulk, pool_query_bits(args->info, args->prop));
	query_args.dqa_pool = pool;
	query_args.dqa_ranks = args->ranks;
	query_args.dqa_info = args->info;
	query_args.dqa_prop = args->prop;
	query_args.dqa_map_buf = map_buf;
	query_args.rpc = rpc;

	rc = tse_task_register_comp_cb(task, pool_query_cb, &query_args,
				       sizeof(query_args));
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return daos_rpc_send(rpc, task);

out_bulk:
	map_bulk_destroy(query_args.dqa_bulk, map_buf);
out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
out_tpriv:
	pool_task_destroy_priv(task);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

/*
 * Is the cached pool map known to be stale? Must be called under
 * pool->dp_map_lock.
 */
static bool
map_known_stale(struct dc_pool *pool)
{
	unsigned int cached = pool_map_get_version(pool->dp_map);

	D_ASSERTF(pool->dp_map_version_known >= cached, "%u >= %u\n",
		  pool->dp_map_version_known, cached);

	return (pool->dp_map_version_known > cached);
}

static void
register_map_task(struct dc_pool *pool, tse_task_t *task)
{
	D_DEBUG(DB_MD, DF_UUID": registering map task %p\n", DP_UUID(pool->dp_pool), task);
	D_ASSERT(pool->dp_map_task == NULL);
	tse_task_addref(task);
	pool->dp_map_task = task;
}

static void
unregister_map_task(struct dc_pool *pool, tse_task_t *task)
{
	D_DEBUG(DB_MD, DF_UUID": unregistering map task %p\n", DP_UUID(pool->dp_pool),
		pool->dp_map_task);
	D_ASSERTF(pool->dp_map_task == task, "%p == %p\n", pool->dp_map_task, task);
	tse_task_decref(pool->dp_map_task);
	pool->dp_map_task = NULL;
}

/*
 * Arg and state of map_refresh
 *
 * mra_i is an index in the internal node array of a pool map. It is used to
 * perform a round robin of the array starting from a random element.
 *
 * mra_n is the number of "serious" errors at which we will fall back to
 * POOL_QUERY via dc_pool_query. "Serious" errors like -DER_NO_HDL or
 * -DER_NONEXIST are not "retryable" normally. In the case of
 * POOL_TGT_QUERY_MAP, since they may indicate that the engines we have chosen
 * simply don't have the info locally, we retry for mra_n such errors and then
 * fall back to the PS.
 */
struct map_refresh_arg {
	struct dc_pool	       *mra_pool;
	daos_handle_t		mra_pool_hdl;
	bool			mra_passive;
	bool			mra_fallen_back;
	unsigned int		mra_map_version;
	int			mra_i;
	int			mra_n;
	struct d_backoff_seq	mra_backoff_seq;
};

/*
 * When called repeatedly, this performs a round robin of the pool map rank
 * array starting from a random index.
 */
static d_rank_t
choose_map_refresh_rank(struct map_refresh_arg *arg)
{
	struct pool_domain     *nodes;
	int			n;
	int			i;
	int			j;
	int			k = -1;

	if (arg->mra_n <= 0)
		return CRT_NO_RANK;

	n = pool_map_find_ranks(arg->mra_pool->dp_map, PO_COMP_ID_ALL, &nodes);
	/* There must be at least one rank. */
	D_ASSERTF(n > 0, "%d\n", n);

	if (arg->mra_i == -1) {
		/* Let i be a random integer in [0, n). */
		i = d_rand() % n;
	} else {
		/* Continue the round robin. */
		i = arg->mra_i;
	}

	/* Find next UPIN rank via a round robin from i. */
	for (j = 0; j < n; j++) {
		k = (i + j) % n;

		if (nodes[k].do_comp.co_status == PO_COMP_ST_UPIN)
			break;
	}
	/* There must be at least one UPIN rank. */
	D_ASSERT(j < n);
	D_ASSERT(k != -1);

	arg->mra_i = k + 1;

	return nodes[k].do_comp.co_rank;
}

static int
create_map_refresh_rpc(struct dc_pool *pool, unsigned int map_version,
		       crt_context_t ctx, crt_group_t *group, d_rank_t rank,
		       crt_rpc_t **rpc, struct pool_buf **map_buf)
{
	crt_endpoint_t			ep;
	crt_rpc_t		       *c;
	struct pool_tgt_query_map_in   *in;
	struct pool_buf		       *b;
	int				rc;

	ep.ep_grp = group;
	ep.ep_rank = rank;
	ep.ep_tag = 0;

	rc = pool_req_create(ctx, &ep, POOL_TGT_QUERY_MAP, pool->dp_pool, pool->dp_pool_hdl,
			     NULL /* req_timep */, &c);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create POOL_TGT_QUERY_MAP",
			 DP_UUID(pool->dp_pool));
		return rc;
	}

	in                  = crt_req_get(c);
	in->tmi_map_version = map_version;

	rc = map_bulk_create(ctx, &in->tmi_map_bulk, &b, pool_buf_nr(pool->dp_map_sz));
	if (rc != 0) {
		crt_req_decref(c);
		return rc;
	}

	*rpc = c;
	*map_buf = b;
	return 0;
}

static void
destroy_map_refresh_rpc(crt_rpc_t *rpc, struct pool_buf *map_buf)
{
	struct pool_tgt_query_map_in *in = crt_req_get(rpc);

	map_bulk_destroy(in->tmi_map_bulk, map_buf);
	crt_req_decref(rpc);
}

struct map_refresh_cb_arg {
	crt_rpc_t	       *mrc_rpc;
	struct pool_buf	       *mrc_map_buf;
};

static int
map_refresh_cb(tse_task_t *task, void *varg)
{
	struct map_refresh_cb_arg      *cb_arg = varg;
	struct map_refresh_arg	       *arg = tse_task_buf_embedded(task, sizeof(*arg));
	struct dc_pool		       *pool = arg->mra_pool;
	struct pool_tgt_query_map_in   *in = crt_req_get(cb_arg->mrc_rpc);
	struct pool_tgt_query_map_out  *out = crt_reply_get(cb_arg->mrc_rpc);
	unsigned int			version_cached;
	struct pool_map		       *map;
	bool				reinit = false;
	int				rc = task->dt_result;

	/* Get an extra reference for the reinit case. */
	dc_pool_get(pool);

	/*
	 * If it turns out below that we do need to update the cached pool map,
	 * then holding the lock while doing so will be okay, since we probably
	 * do not want other threads to proceed with a known-stale pool anyway.
	 * Otherwise, we will release the lock quickly.
	 */
	D_RWLOCK_WRLOCK(&pool->dp_map_lock);

	D_DEBUG(DB_MD, DF_UUID": %p: crt: "DF_RC"\n", DP_UUID(pool->dp_pool), task, DP_RC(rc));
	if (daos_rpc_retryable_rc(rc)) {
		reinit = true;
		goto out;
	} else if (rc != 0) {
		goto out;
	}

	if (DAOS_FAIL_CHECK(DAOS_POOL_FAIL_MAP_REFRESH_SERIOUSLY))
		out->tmo_op.po_rc = -DER_NO_HDL;

	rc = out->tmo_op.po_rc;
	if (rc == -DER_TRUNC) {
		/*
		 * cb_arg->mrc_map_buf is not large enough. Retry with the size
		 * suggested by the server side.
		 */
		D_DEBUG(DB_MD, DF_UUID": %p: map buf < required %u\n",
			DP_UUID(pool->dp_pool), task, out->tmo_map_buf_size);
		pool->dp_map_sz = out->tmo_map_buf_size;
		reinit = true;
		goto out;
	} else if (daos_rpc_retryable_rc(rc) || rc == -DER_AGAIN) {
		D_DEBUG(DB_MD, DF_UUID": %p: retryable: "DF_RC"\n", DP_UUID(pool->dp_pool), task,
			DP_RC(rc));
		reinit = true;
		goto out;
	} else if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": %p: serious: "DF_RC"\n", DP_UUID(pool->dp_pool), task,
			DP_RC(rc));
		arg->mra_n--;
		reinit = true;
		goto out;
	}

	if (DAOS_FAIL_CHECK(DAOS_POOL_FAIL_MAP_REFRESH))
		out->tmo_op.po_map_version = 0;

	if (out->tmo_op.po_map_version <= in->tmi_map_version) {
		/*
		 * The server side does not have a version we requested for. If
		 * the rank has a version < the highest known version, it has a
		 * stale version itself, for which we need to try another one.
		 * If the cached pool map version is known to be stale, we also
		 * need to retry. Otherwise, we are done.
		 */
		D_DEBUG(DB_MD,
			DF_UUID": %p: no requested version from rank %u: "
			"requested=%u known=%u remote=%u\n",
			DP_UUID(pool->dp_pool), task,
			cb_arg->mrc_rpc->cr_ep.ep_rank, in->tmi_map_version,
			pool->dp_map_version_known, out->tmo_op.po_map_version);
		if (out->tmo_op.po_map_version < pool->dp_map_version_known ||
		    map_known_stale(pool))
			reinit = true;
		goto out;
	}

	version_cached = pool_map_get_version(pool->dp_map);

	if (out->tmo_op.po_map_version < pool->dp_map_version_known ||
	    out->tmo_op.po_map_version <= version_cached) {
		/*
		 * The server side has provided a version we requested for, but
		 * we are no longer interested in it.
		 */
		D_DEBUG(DB_MD, DF_UUID": %p: got stale %u < known %u or <= cached %u\n",
			DP_UUID(pool->dp_pool), task, out->tmo_op.po_map_version,
			pool->dp_map_version_known, version_cached);
		reinit = true;
		goto out;
	}

	rc = pool_map_create(cb_arg->mrc_map_buf, out->tmo_op.po_map_version, &map);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool map: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out;
	}

	rc = dc_pool_map_update(pool, map, false /* connect */);
	pool_map_decref(map);

out:
	destroy_map_refresh_rpc(cb_arg->mrc_rpc, cb_arg->mrc_map_buf);

	if (reinit) {
		uint32_t	backoff;
		int		rc_tmp;

		backoff = d_backoff_seq_next(&arg->mra_backoff_seq);
		rc_tmp = tse_task_reinit_with_delay(task, backoff);
		if (rc_tmp == 0) {
			D_DEBUG(DB_MD,
				DF_UUID": %p: reinitialized due to "DF_RC" with backoff %u\n",
				DP_UUID(pool->dp_pool), task, DP_RC(rc), backoff);
			rc = 0;
		} else {
			D_ERROR(DF_UUID": failed to reinitialize pool map "
				"refresh task: "DF_RC"\n", DP_UUID(pool->dp_pool), DP_RC(rc));
			if (rc == 0)
				rc = rc_tmp;
			reinit = false;
		}
	}

	if (!reinit)
		unregister_map_task(pool, task);

	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	if (!reinit) {
		d_backoff_seq_fini(&arg->mra_backoff_seq);
		dc_pool_put(arg->mra_pool);
	}

	dc_pool_put(pool);
	return rc;
}

static int
map_refresh(tse_task_t *task)
{
	struct map_refresh_arg	       *arg = tse_task_buf_embedded(task, sizeof(*arg));
	struct dc_pool		       *pool = arg->mra_pool;
	d_rank_t			rank;
	unsigned int			version;
	crt_rpc_t		       *rpc;
	struct map_refresh_cb_arg	cb_arg;
	int				rc;

	/* Get an extra reference for the reinit cases. */
	dc_pool_get(pool);

	if (arg->mra_passive) {
		/*
		 * Passive pool map refresh tasks do nothing besides waiting
		 * for the active one to complete. They avoid complexities like
		 * whether a dc_pool_create_map_refresh_task caller should
		 * schedule the resulting task or not and how the caller would
		 * register its completion callback to the bottom of the
		 * resulting task's callback stack.
		 */
		D_DEBUG(DB_MD, DF_UUID": %p: passive done\n", DP_UUID(pool->dp_pool), task);
		rc = 0;
		goto out_task;
	}

	D_RWLOCK_WRLOCK(&pool->dp_map_lock);

	if (arg->mra_fallen_back) {
		unregister_map_task(pool, task);
		D_RWLOCK_UNLOCK(&pool->dp_map_lock);
		D_DEBUG(DB_MD, DF_UUID": %p: fallen-back done\n", DP_UUID(pool->dp_pool), task);
		rc = 0;
		goto out_task;
	}

	/*
	 * Update the highest known pool map version when every map_refresh
	 * task runs for the first time.
	 */
	if (pool->dp_map_version_known < arg->mra_map_version)
		pool->dp_map_version_known = arg->mra_map_version;

	if (arg->mra_map_version != 0 && !map_known_stale(pool)) {
		D_RWLOCK_UNLOCK(&pool->dp_map_lock);
		rc = 0;
		goto out_task;
	}

	if (pool->dp_map_task != NULL && pool->dp_map_task != task) {
		/*
		 * An active pool map refresh task already exists; become a
		 * passive one. If this is use case 1 (see
		 * dc_pool_create_map_refresh_task), there is little benefit in
		 * immediately querying the server side again. If this is use
		 * case 2, the active pool map refresh task will pick up the
		 * known version here via the pool->dp_map_version_known update
		 * above, and retry till the highest known version is cached.
		 */
		D_DEBUG(DB_MD, DF_UUID": %p: becoming passive waiting for %p\n",
			DP_UUID(pool->dp_pool), task, pool->dp_map_task);
		arg->mra_passive = true;
		rc = tse_task_register_deps(task, 1, &pool->dp_map_task);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to depend on active pool map "
				"refresh task: "DF_RC"\n", DP_UUID(pool->dp_pool), DP_RC(rc));
			D_RWLOCK_UNLOCK(&pool->dp_map_lock);
			goto out_task;
		}
		rc = tse_task_reinit(task);
		D_RWLOCK_UNLOCK(&pool->dp_map_lock);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to reinitialize task %p: "DF_RC"\n",
				DP_UUID(pool->dp_pool), task, DP_RC(rc));
			goto out_task;
		}
		goto out_pool;
	}

	if (pool->dp_map_task == NULL) {
		/* No active pool map refresh task; become one */
		D_DEBUG(DB_MD, DF_UUID": %p: becoming active\n", DP_UUID(pool->dp_pool), task);
		register_map_task(pool, task);
	}

	rank = choose_map_refresh_rank(arg);
	if (rank == CRT_NO_RANK) {
		tse_task_t	       *query_task;
		daos_pool_query_t      *query_arg;

		/* Fall back to dc_pool_query. */
		D_RWLOCK_UNLOCK(&pool->dp_map_lock);
		D_DEBUG(DB_MD, DF_UUID": %p: falling back to dc_pool_query\n",
			DP_UUID(pool->dp_pool), task);
		arg->mra_fallen_back = true;
		rc = dc_task_create(dc_pool_query, tse_task2sched(task), NULL /* ev */,
				    &query_task);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to create pool query task: "DF_RC"\n",
				DP_UUID(pool->dp_pool), DP_RC(rc));
			goto out_map_task;
		}
		query_arg = dc_task_get_args(query_task);
		query_arg->poh = arg->mra_pool_hdl;
		rc = tse_task_register_deps(task, 1, &query_task);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to depend on pool query task: "DF_RC"\n",
				DP_UUID(pool->dp_pool), DP_RC(rc));
			dc_task_decref(query_task);
			goto out_map_task;
		}
		rc = tse_task_reinit(task);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to reinitialize task %p: "DF_RC"\n",
				DP_UUID(pool->dp_pool), task, DP_RC(rc));
			dc_task_decref(query_task);
			goto out_map_task;
		}
		rc = dc_task_schedule(query_task, true);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to run pool query task %p: "DF_RC"\n",
				DP_UUID(pool->dp_pool), query_task, DP_RC(rc));
			goto out_map_task;
		}
		goto out_pool;
	}

	/*
	 * The server side will see if it has a pool map version >
	 * in->tmi_map_version. So here we are asking for a version >= the
	 * highest version known but also > the version cached.
	 */
	version = max(pool->dp_map_version_known - 1, pool_map_get_version(pool->dp_map));

	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	rc = create_map_refresh_rpc(pool, version, daos_task2ctx(task), pool->dp_sys->sy_group,
				    rank, &rpc, &cb_arg.mrc_map_buf);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool refresh RPC: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_map_task;
	}

	crt_req_addref(rpc);
	cb_arg.mrc_rpc = rpc;

	rc = tse_task_register_comp_cb(task, map_refresh_cb, &cb_arg, sizeof(cb_arg));
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to task completion callback: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_cb_arg;
	}

	D_DEBUG(DB_MD, DF_UUID": %p: asking rank %u for version > %u\n",
		DP_UUID(pool->dp_pool), task, rank, version);
	dc_pool_put(pool);
	return daos_rpc_send(rpc, task);

out_cb_arg:
	crt_req_decref(cb_arg.mrc_rpc);
	destroy_map_refresh_rpc(rpc, cb_arg.mrc_map_buf);
out_map_task:
	D_RWLOCK_WRLOCK(&pool->dp_map_lock);
	unregister_map_task(pool, task);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
out_task:
	d_backoff_seq_fini(&arg->mra_backoff_seq);
	dc_pool_put(arg->mra_pool);
	tse_task_complete(task, rc);
out_pool:
	dc_pool_put(pool);
	return rc;
}

/**
 * Create a pool map refresh task. All pool map refreshes shall use this
 * interface. Two use cases are anticipated:
 *
 *   1 Check if there is a pool map version > the cached version, and if there
 *     is, get it. In this case, pass 0 in \a map_version.
 *
 *   2 Get a pool map version >= a known version (learned from a server). In
 *     this case, pass the known version in \a map_version.
 *
 * In either case, the pool map refresh task may temporarily miss the latest
 * pool map version in certain scenarios, resulting in extra retries.
 *
 * \param[in]	pool_hdl	pool handle
 * \param[in]	map_version	known pool map version
 * \param[in]	sched		scheduler
 * \param[out]	task		pool map refresh task
 */
int
dc_pool_create_map_refresh_task(daos_handle_t pool_hdl, uint32_t map_version, tse_sched_t *sched,
				tse_task_t **task)
{
	struct dc_pool	       *pool;
	tse_task_t	       *t;
	struct map_refresh_arg *a;
	int			rc;

	pool = dc_hdl2pool(pool_hdl);
	if (pool == NULL) {
		D_ERROR("failed to find pool handle "DF_X64"\n", pool_hdl.cookie);
		return -DER_NO_HDL;
	}

	rc = tse_task_create(map_refresh, sched, NULL /* priv */, &t);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool map refresh task: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		dc_pool_put(pool);
		return rc;
	}

	a = tse_task_buf_embedded(t, sizeof(*a));
	dc_pool_get(pool);
	a->mra_pool = pool;
	a->mra_pool_hdl = pool_hdl;
	a->mra_passive = false;
	a->mra_fallen_back = false;
	a->mra_map_version = map_version;
	a->mra_i = -1;
	a->mra_n = 4;
	rc = d_backoff_seq_init(&a->mra_backoff_seq, 0 /* nzeros */, 8 /* factor */,
				1 << 10 /* next (us) */, 4 << 20 /* max (us) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));

	*task = t;
	dc_pool_put(pool);
	return 0;
}

/**
 * Destroy a pool map refresh task that has not been scheduled yet, typically
 * for error handling purposes.
 */
void
dc_pool_abandon_map_refresh_task(tse_task_t *task)
{
	struct map_refresh_arg *arg = tse_task_buf_embedded(task, sizeof(*arg));

	d_backoff_seq_fini(&arg->mra_backoff_seq);
	dc_pool_put(arg->mra_pool);
	tse_task_complete(task, -DER_CANCELED);
}

struct pool_lc_arg {
	crt_rpc_t			*rpc;
	struct dc_pool			*lca_pool;
	daos_size_t			 lca_req_ncont;
	daos_size_t			*lca_ncont;
	crt_bulk_t                       lca_bulk;
	struct daos_pool_cont_info	*lca_cont_buf;
};

static int
pool_list_cont_cb(tse_task_t *task, void *data)
{
	struct pool_lc_arg		*arg = (struct pool_lc_arg *)data;
	struct pool_list_cont_in	*in = crt_req_get(arg->rpc);
	struct pool_list_cont_out	*out = crt_reply_get(arg->rpc);
	bool                             reinit = false;
	int				 rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->lca_pool, &arg->rpc->cr_ep, rc,
					   &out->plco_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	D_DEBUG(DB_MD, DF_UUID": list cont rpc done: %d\n",
		DP_UUID(arg->lca_pool->dp_pool), rc);

	if (rc) {
		D_ERROR("RPC error while listing containers: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->plco_op.po_rc;
	*arg->lca_ncont = out->plco_ncont;
	/* arg->lca_cont_buf written by bulk transfer if buffer provided */

	if (arg->lca_cont_buf && (rc == -DER_TRUNC)) {
		D_WARN("ncont provided ("DF_U64") < required ("DF_U64")\n",
				in->plci_ncont, out->plco_ncont);
		D_GOTO(out, rc);
	} else if (rc != 0) {
		D_ERROR("failed to list containers %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->lca_pool);
	list_cont_bulk_destroy(arg->lca_bulk);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		pool_task_destroy_priv(task);
	return rc;
}

int
dc_pool_list_cont(tse_task_t *task)
{
	daos_pool_list_cont_t		*args;
	struct pool_task_priv           *tpriv;
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	daos_size_t                      ncont;
	struct pool_lc_arg               lc_cb_args;
	int				 rc;

	args = dc_task_get_args(task);
	tpriv = dc_task_get_priv(task);

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_task;
	}

	/** Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_tpriv, rc = -DER_NO_HDL);

	D_DEBUG(DB_MD, DF_UUID": list containers: hdl="DF_UUID"\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl));

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}

	/* TODO: deprecate POOL_LIST_CONT RPC, and change list containers implementation
	 * to use POOL_FILTER_CONT RPC and a NULL filter input.
	 */
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_LIST_CONT, pool->dp_pool,
			     pool->dp_pool_hdl, NULL /* req_timep */, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create pool list cont rpc",
			 DP_UUID(pool->dp_pool));
		D_GOTO(out_pool, rc);
	}

	/* If provided cont_buf is NULL, caller needs the number of containers
	 * to be returned in ncont. Set ncont=0 in the request in this case
	 * (caller value may be uninitialized).
	 */
	if (args->cont_buf == NULL)
		ncont = 0;
	else
		ncont = *args->ncont;
	lc_cb_args.lca_bulk = CRT_BULK_NULL;

	D_DEBUG(DB_MD, "req_ncont=" DF_U64 " (cont_buf=%p, *ncont=" DF_U64 "\n", ncont,
		args->cont_buf, *args->ncont);

	/** +1 for args */
	crt_req_addref(rpc);

	if ((*args->ncont > 0) && args->cont_buf) {
		rc =
		    list_cont_bulk_create(daos_task2ctx(task), &lc_cb_args.lca_bulk, args->cont_buf,
					  ncont * sizeof(struct daos_pool_cont_info));
		if (rc != 0)
			D_GOTO(out_rpc, rc);
	}

	pool_list_cont_in_set_data(rpc, lc_cb_args.lca_bulk, ncont);

	lc_cb_args.lca_pool = pool;
	lc_cb_args.lca_ncont = args->ncont;
	lc_cb_args.lca_cont_buf = args->cont_buf;
	lc_cb_args.rpc = rpc;
	lc_cb_args.lca_req_ncont = ncont;

	rc = tse_task_register_comp_cb(task, pool_list_cont_cb, &lc_cb_args, sizeof(lc_cb_args));
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return daos_rpc_send(rpc, task);

out_bulk:
	if (ncont > 0)
		list_cont_bulk_destroy(lc_cb_args.lca_bulk);

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
out_tpriv:
	pool_task_destroy_priv(task);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

struct pool_fc_arg {
	crt_rpc_t			*rpc;
	struct dc_pool			*fca_pool;
	daos_size_t			 fca_req_ncont;
	daos_size_t			*fca_ncont;
	crt_bulk_t                       fca_bulk;
	struct daos_pool_cont_info2	*fca_cont_buf;
};

static int
pool_filter_cont_cb(tse_task_t *task, void *data)
{
	struct pool_fc_arg		*arg = (struct pool_fc_arg *)data;
	struct pool_filter_cont_in	*in = crt_req_get(arg->rpc);
	struct pool_filter_cont_out	*out = crt_reply_get(arg->rpc);
	bool                             reinit = false;
	int				 rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->fca_pool, &arg->rpc->cr_ep, rc,
					   &out->pfco_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	D_DEBUG(DB_MD, DF_UUID": filter cont rpc done: %d\n",
		DP_UUID(arg->fca_pool->dp_pool), rc);

	if (rc) {
		D_ERROR("RPC error while filtering containers: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->pfco_op.po_rc;
	*arg->fca_ncont = out->pfco_ncont;
	/* arg->fca_cont_buf written by bulk transfer if buffer provided */

	if (arg->fca_cont_buf && (rc == -DER_TRUNC)) {
		D_WARN("ncont provided ("DF_U64") < required ("DF_U64")\n",
		       in->pfci_ncont, out->pfco_ncont);
		D_GOTO(out, rc);
	} else if (rc != 0) {
		D_ERROR("failed to filter containers %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->fca_pool);
	list_cont_bulk_destroy(arg->fca_bulk);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		pool_task_destroy_priv(task);
	return rc;
}

int
dc_pool_filter_cont(tse_task_t *task)
{
	daos_pool_filter_cont_t		*args;
	struct pool_task_priv           *tpriv;
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct pool_filter_cont_in	*in;
	daos_size_t                      ncont;
	struct pool_fc_arg		 fc_cb_args;
	int				 rc;

	args = dc_task_get_args(task);
	tpriv = dc_task_get_priv(task);

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_task;
	}

	/* Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_tpriv, rc = -DER_NO_HDL);

	D_DEBUG(DB_MD, DF_UUID": filter containers: hdl="DF_UUID", args=%p, filt=%p, ncont=%p, "
		"*ncont="DF_U64", cont_buf=%p\n", DP_UUID(pool->dp_pool),
		DP_UUID(pool->dp_pool_hdl), args, args->filt, args->ncont, *args->ncont,
		args->cont_buf);

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool, &pool->dp_client,
				     &pool->dp_client_lock, pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_FILTER_CONT, pool->dp_pool,
			     pool->dp_pool_hdl, NULL /* req_timep */, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create pool filter cont rpc",
			 DP_UUID(pool->dp_pool));
		D_GOTO(out_pool, rc);
	}

	in = crt_req_get(rpc);

	/* If provided cont_buf is NULL, caller needs the number of matching containers
	 * to be returned in ncont. Set ncont=0 in the request in this case
	 * (caller value may be uninitialized).
	 */
	if (args->cont_buf == NULL)
		ncont = 0;
	else
		ncont = *args->ncont;
	fc_cb_args.fca_bulk = CRT_BULK_NULL;

	D_DEBUG(DB_MD, "req_ncont="DF_U64" (cont_buf=%p, *ncont="DF_U64"\n",
		in->pfci_ncont, args->cont_buf, *args->ncont);

	/** +1 for args */
	crt_req_addref(rpc);

	if ((*args->ncont > 0) && args->cont_buf) {
		rc =
		    list_cont_bulk_create(daos_task2ctx(task), &fc_cb_args.fca_bulk, args->cont_buf,
					  ncont * sizeof(struct daos_pool_cont_info2));
		if (rc != 0)
			D_GOTO(out_rpc, rc);
	}

	pool_filter_cont_in_set_data(rpc, fc_cb_args.fca_bulk, ncont, args->filt);

	fc_cb_args.fca_pool = pool;
	fc_cb_args.fca_ncont = args->ncont;
	fc_cb_args.fca_cont_buf = args->cont_buf;
	fc_cb_args.rpc = rpc;
	fc_cb_args.fca_req_ncont = in->pfci_ncont;

	rc = tse_task_register_comp_cb(task, pool_filter_cont_cb, &fc_cb_args, sizeof(fc_cb_args));
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return daos_rpc_send(rpc, task);

out_bulk:
	if (in->pfci_ncont > 0)
		list_cont_bulk_destroy(fc_cb_args.fca_bulk);

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
out_tpriv:
	pool_task_destroy_priv(task);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

struct pool_evict_state {
	struct rsvc_client	client;
	struct dc_mgmt_sys     *sys;
};

int
dc_pool_map_version_get(daos_handle_t ph, unsigned int *map_ver)
{
	struct dc_pool *pool;
	int		rc = 0;

	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		return -DER_NO_HDL;

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	if (pool->dp_map == NULL)
		rc = -DER_NO_HDL;
	else
		*map_ver = pool_map_get_version(pool->dp_map);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	dc_pool_put(pool);

	return rc;
}

struct pool_query_target_arg {
	struct dc_pool		*dqa_pool;
	uint32_t		 dqa_tgt_idx;
	d_rank_t		 dqa_rank;
	daos_target_info_t	*dqa_info;
	crt_rpc_t		*rpc;
};

static int
pool_query_target_cb(tse_task_t *task, void *data)
{
	struct pool_query_target_arg *arg;
	struct pool_query_info_out   *out;
	bool                          reinit = false;
	int			      rc;

	arg = (struct pool_query_target_arg *)data;
	out = crt_reply_get(arg->rpc);
	rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->dqa_pool, &arg->rpc->cr_ep, rc,
					   &out->pqio_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	D_DEBUG(DB_MD, DF_UUID": target query rpc done: %d\n",
		DP_UUID(arg->dqa_pool->dp_pool), rc);

	if (rc) {
		D_ERROR("RPC error while querying pool target: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->pqio_op.po_rc;

	if (rc != 0) {
		D_ERROR("failed to query pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	arg->dqa_info->ta_state = out->pqio_state;
	arg->dqa_info->ta_space = out->pqio_space;
	D_DEBUG(DB_MD, DF_UUID": rank %u index %u state %d\n", DP_UUID(arg->dqa_pool->dp_pool),
		arg->dqa_rank, arg->dqa_tgt_idx, arg->dqa_info->ta_state);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->dqa_pool);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		pool_task_destroy_priv(task);
	return rc;
}

int
dc_pool_query_target(tse_task_t *task)
{
	daos_pool_query_target_t	*args;
	struct pool_task_priv           *tpriv;
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t                       *rpc;
	struct pool_query_target_arg	 query_args;
	int				 rc;

	args = dc_task_get_args(task);
	tpriv = dc_task_get_priv(task);

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_task;
	}

	/** Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_tpriv, rc = -DER_NO_HDL);

	D_DEBUG(DB_MD, DF_UUID": querying: hdl="DF_UUID" tgt=%d rank=%d\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		args->tgt_idx, args->rank);

	ep.ep_grp = pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&pool->dp_client_lock);
	rc = rsvc_client_choose(&pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_QUERY_INFO, pool->dp_pool,
			     pool->dp_pool_hdl, NULL /* req_timep */, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create pool tgt info rpc",
			 DP_UUID(pool->dp_pool));
		goto out_pool;
	}

	pool_query_info_in_set_data(rpc, args->rank, args->tgt_idx);

	/** +1 for args */
	crt_req_addref(rpc);

	query_args.dqa_pool = pool;
	query_args.dqa_info = args->info;
	query_args.dqa_tgt_idx = args->tgt_idx;
	query_args.dqa_rank = args->rank;
	query_args.rpc = rpc;

	rc = tse_task_register_comp_cb(task, pool_query_target_cb, &query_args,
				       sizeof(query_args));
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return daos_rpc_send(rpc, task);

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
out_tpriv:
	pool_task_destroy_priv(task);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

struct pool_req_arg {
	struct dc_pool        *pra_pool;
	crt_rpc_t             *pra_rpc;
	crt_bulk_t             pra_bulk;
	tse_task_cb_t          pra_callback;
	struct pool_task_priv *pra_tpriv;
};

enum preq_cleanup_stage {
	CLEANUP_ALL,
	CLEANUP_BULK,
	CLEANUP_RPC,
	CLEANUP_TASK_PRIV,
	CLEANUP_POOL,
};

static void
pool_req_cleanup(enum preq_cleanup_stage stage, tse_task_t *task, bool free_tpriv,
		 struct pool_req_arg *args)
{
	switch (stage) {
	case CLEANUP_ALL:
		crt_req_decref(args->pra_rpc);
	case CLEANUP_BULK:
		if (args->pra_bulk)
			crt_bulk_free(args->pra_bulk);
	case CLEANUP_RPC:
		crt_req_decref(args->pra_rpc);
	case CLEANUP_TASK_PRIV:
		if (free_tpriv && args->pra_tpriv != NULL) {
			args->pra_tpriv = NULL;
			pool_task_destroy_priv(task);
		}
	case CLEANUP_POOL:
		dc_pool_put(args->pra_pool);
	}
}

static int
pool_req_complete(tse_task_t *task, void *data)
{
	struct pool_req_arg	*args = data;
	struct dc_pool		*pool	 = args->pra_pool;
	struct pool_op_out	*op_out	 = crt_reply_get(args->pra_rpc);
	bool                     reinit  = false;
	int			 rc	 = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(pool, &args->pra_rpc->cr_ep,
					   rc, op_out, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while querying pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = op_out->po_rc;
	if (rc != 0) {
		D_DEBUG(DB_MD, DF_UUID": failed to access pool: %d\n",
			DP_UUID(pool->dp_pool), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MD, DF_UUID": Accessed: using hdl="DF_UUID"\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl));
	if (args->pra_callback != NULL)
		rc = args->pra_callback(task, data);
out:
	pool_req_cleanup(CLEANUP_BULK, task, !reinit, args);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0 && args->pra_tpriv != NULL) {
			args->pra_tpriv = NULL;
			pool_task_destroy_priv(task);
		}
	}
	return rc;
}

static int
attr_list_req_complete(tse_task_t *task, void *data)
{
	struct pool_req_arg	  *args = data;
	daos_pool_list_attr_t	  *task_args = dc_task_get_args(task);
	struct pool_attr_list_out *out = crt_reply_get(args->pra_rpc);

	*task_args->size = out->palo_size;
	return 0;
}

static int
pool_req_prepare(daos_handle_t poh, enum pool_operation opcode, crt_context_t *ctx,
		 tse_task_t *task, struct pool_req_arg *args)
{
	struct pool_task_priv *tpriv = dc_task_get_priv(task);
	crt_endpoint_t	   ep;
	int		   rc;

	args->pra_bulk = NULL;
	args->pra_callback = NULL;
	args->pra_pool = dc_hdl2pool(poh);
	if (args->pra_pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0) {
			pool_req_cleanup(CLEANUP_POOL, task, false /* free_tpriv */, args);
			goto out;
		}
	}
	args->pra_tpriv = tpriv;

	ep.ep_grp  = args->pra_pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&args->pra_pool->dp_client_lock);
	rc = rsvc_client_choose(&args->pra_pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&args->pra_pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(args->pra_pool->dp_pool), DP_RC(rc));
		pool_req_cleanup(CLEANUP_TASK_PRIV, task, true /* free_tpriv */, args);
		goto out;
	}

	rc = pool_req_create(ctx, &ep, opcode, args->pra_pool->dp_pool, args->pra_pool->dp_pool_hdl,
			     &tpriv->rq_time, &args->pra_rpc);
	if (rc != 0) {
		DL_ERROR(rc, "failed to create rpc");
		pool_req_cleanup(CLEANUP_TASK_PRIV, task, true /* free_tpriv */, args);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

int
dc_pool_list_attr(tse_task_t *task)
{
	daos_pool_list_attr_t           *args;
	struct pool_req_arg		 cb_args;
	crt_bulk_t                       bulk = CRT_BULK_NULL;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->size == NULL ||
	    (*args->size > 0 && args->buf == NULL)) {
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pool_req_prepare(args->poh, POOL_ATTR_LIST, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_UUID": listing attributes: hdl="
			 DF_UUID "; size=%lu\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl), *args->size);

	if (*args->size > 0) {
		d_iov_t iov = {
			.iov_buf     = args->buf,
			.iov_buf_len = *args->size,
			.iov_len     = 0
		};
		d_sg_list_t sgl = {
			.sg_nr_out = 0,
			.sg_nr	   = 1,
			.sg_iovs   = &iov
		};
		rc = crt_bulk_create(daos_task2ctx(task), &sgl, CRT_BULK_RW, &bulk);
		if (rc != 0) {
			pool_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
			D_GOTO(out, rc);
		}
		pool_attr_list_in_set_data(cb_args.pra_rpc, bulk);
	}

	cb_args.pra_bulk     = bulk;
	cb_args.pra_callback = attr_list_req_complete;
	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to list pool attributes: "DF_RC"\n",
		DP_RC(rc));
	return rc;
}

static int
attr_bulk_create(int n, char *names[], void *values[], size_t sizes[],
		 crt_context_t crt_ctx, crt_bulk_perm_t perm, crt_bulk_t *bulk)
{
	int		rc;
	int		i;
	int		j;
	d_sg_list_t	sgl;

	/* Buffers = 'n' names */
	sgl.sg_nr_out	= 0;
	sgl.sg_nr	= n;

	/* + 1 sizes */
	if (sizes != NULL)
		++sgl.sg_nr;

	/* + non-null values */
	if (sizes != NULL && values != NULL) {
		for (j = 0; j < n; j++)
			if (sizes[j] > 0)
				++sgl.sg_nr;
	}

	D_ALLOC_ARRAY(sgl.sg_iovs, sgl.sg_nr);
	if (sgl.sg_iovs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* names */
	for (j = 0, i = 0; j < n; ++j)
		d_iov_set(&sgl.sg_iovs[i++], (void *)(names[j]),
			     strlen(names[j]) + 1 /* trailing '\0' */);

	/* TODO: Add packing/unpacking of non-byte-arrays to rpc.[hc] ? */

	/* sizes */
	if (sizes != NULL)
		d_iov_set(&sgl.sg_iovs[i++], (void *)sizes, n * sizeof(*sizes));

	/* values */
	if (sizes != NULL && values != NULL) {
		for (j = 0; j < n; ++j)
			if (sizes[j] > 0)
				d_iov_set(&sgl.sg_iovs[i++],
					  values[j], sizes[j]);
	}

	rc = crt_bulk_create(crt_ctx, &sgl, perm, bulk);
	D_FREE(sgl.sg_iovs);
out:
	return rc;
}

/*
 * Check for valid inputs. If readonly is true, normalizes
 * by setting corresponding size to zero for NULL values.
 * Otherwise, values may not be NULL.
 */
static int
attr_check_input(int n, char const *const names[], void const *const values[],
		 size_t sizes[], bool readonly)
{
	int i;

	if (n <= 0 || names == NULL || ((sizes == NULL
	    || values == NULL) && !readonly)) {
		D_ERROR("Invalid Arguments: n = %d, names = %p, values = %p, sizes = %p\n", n,
			names, values, sizes);
		return -DER_INVAL;
	}

	for (i = 0; i < n; i++) {
		if (names[i] == NULL || *names[i] == '\0') {
			D_ERROR("Invalid Arguments: names[%d] = %s\n", i,
				names[i] == NULL ? "NULL" : "\'\\0\'");

			return -DER_INVAL;
		}
		if (strnlen(names[i], DAOS_ATTR_NAME_MAX + 1) > DAOS_ATTR_NAME_MAX) {
			D_ERROR("Invalid Arguments: names[%d] size > DAOS_ATTR_NAME_MAX\n", i);
			return -DER_INVAL;
		}
		if (sizes != NULL) {
			if (values == NULL)
				sizes[i] = 0;
			else if (values[i] == NULL || sizes[i] == 0) {
				if (!readonly) {
					D_ERROR(
					    "Invalid Arguments: values[%d] = %p, sizes[%d] = %lu\n",
					    i, values[i], i, sizes[i]);
					return -DER_INVAL;
				}
				sizes[i] = 0;
			}
		}
	}
	return 0;
}

static int
free_heap_copy(tse_task_t *task, void *args)
{
	char *name = *(char **)args;

	D_FREE(name);
	return 0;
}

int
dc_pool_get_attr(tse_task_t *task)
{
	daos_pool_get_attr_t     *args;
	struct pool_req_arg	 cb_args;
	uint64_t                  key_length = 0;
	int			 rc;
	int			 i;
	char			**new_names = NULL;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names,
			      (const void *const*)args->values,
			      (size_t *)args->sizes, true);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = pool_req_prepare(args->poh, POOL_ATTR_GET, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_UUID": getting attributes: hdl="DF_UUID"\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl));

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out_rpc, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out_rpc, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		uint64_t len;

		len = strnlen(args->names[i], DAOS_ATTR_NAME_MAX);
		key_length += len + 1;
		D_STRNDUP(new_names[i], args->names[i], len);
		if (new_names[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out_rpc, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, (void **)args->values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RW, &cb_args.pra_bulk);
	if (rc != 0)
		goto out_rpc;

	rc = tse_task_register_comp_cb(task, pool_req_complete, &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	pool_attr_get_in_set_data(cb_args.pra_rpc, args->n, key_length, cb_args.pra_bulk);

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out_rpc:
	pool_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to get pool attributes: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_pool_set_attr(tse_task_t *task)
{
	daos_pool_set_attr_t     *args;
	struct pool_req_arg	 cb_args;
	int			 i, rc;
	char			**new_names = NULL;
	void			**new_values = NULL;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names, args->values,
			      (size_t *)args->sizes, false);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = pool_req_prepare(args->poh, POOL_ATTR_SET, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_UUID": setting attributes: hdl="DF_UUID"\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl));

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out_rpc, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out_rpc, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		D_STRNDUP(new_names[i], args->names[i], DAOS_ATTR_NAME_MAX);
		if (new_names[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			goto out_rpc;
		}
	}

	/* no easy way to determine if a value storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * value in heap
	 */
	D_ALLOC_ARRAY(new_values, args->n);
	if (!new_values)
		D_GOTO(out_rpc, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_values,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_values);
		goto out_rpc;
	}
	for (i = 0 ; i < args->n ; i++) {
		D_ALLOC(new_values[i], args->sizes[i]);
		if (new_values[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);
		memcpy(new_values[i], args->values[i], args->sizes[i]);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_values[i], sizeof(void *));
		if (rc) {
			D_FREE(new_values[i]);
			goto out_rpc;
		}
	}

	rc = attr_bulk_create(args->n, new_names, new_values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RO, &cb_args.pra_bulk);
	if (rc != 0)
		goto out_rpc;
	pool_attr_set_in_set_data(cb_args.pra_rpc, args->n, cb_args.pra_bulk);

	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out_rpc:
	pool_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to set pool attributes: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_pool_del_attr(tse_task_t *task)
{
	daos_pool_del_attr_t     *args;
	struct pool_req_arg	 cb_args;
	int			 i, rc;
	char			**new_names;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names, NULL, NULL, true);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = pool_req_prepare(args->poh, POOL_ATTR_DEL, daos_task2ctx(task), task, &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_UUID": deleting attributes: hdl="DF_UUID"\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl));

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out_rpc, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out_rpc, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		D_STRNDUP(new_names[i], args->names[i], DAOS_ATTR_NAME_MAX);
		if (new_names[i] == NULL)
			D_GOTO(out_rpc, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out_rpc, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, NULL, NULL, daos_task2ctx(task), CRT_BULK_RO,
			      &cb_args.pra_bulk);
	if (rc != 0)
		goto out_rpc;

	pool_attr_del_in_set_data(cb_args.pra_rpc, args->n, cb_args.pra_bulk);

	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, task, true /* free_tpriv */, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out_rpc:
	pool_req_cleanup(CLEANUP_RPC, task, true /* free_tpriv */, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DB_MD, "Failed to del pool attributes: "DF_RC"\n", DP_RC(rc));
	return rc;
}

struct pool_svc_stop_arg {
	struct dc_pool	       *dsa_pool;
	crt_rpc_t	       *rpc;
};

static int
pool_svc_stop_cb(tse_task_t *task, void *data)
{
	struct pool_svc_stop_arg       *arg    = (struct pool_svc_stop_arg *)data;
	struct pool_svc_stop_out       *out = crt_reply_get(arg->rpc);
	bool                            reinit = false;
	int				rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->dsa_pool, &arg->rpc->cr_ep, rc,
					   &out->pso_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		reinit = true;
		D_GOTO(out, rc = 0);
	}

	D_DEBUG(DB_MD, DF_UUID": stop rpc done: %d\n",
		DP_UUID(arg->dsa_pool->dp_pool), rc);

	if (rc != 0)
		D_GOTO(out, rc);

	rc = out->pso_op.po_rc;
	if (rc)
		D_GOTO(out, rc);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->dsa_pool);
	if (reinit) {
		rc = pool_task_reinit(task);
		if (rc != 0)
			reinit = false;
	}
	if (!reinit)
		pool_task_destroy_priv(task);
	return rc;
}

int
dc_pool_stop_svc(tse_task_t *task)
{
	daos_pool_stop_svc_t	       *args;
	struct pool_task_priv          *tpriv = dc_task_get_priv(task);
	struct dc_pool		       *pool;
	crt_endpoint_t			ep;
	crt_rpc_t                      *rpc;
	struct pool_svc_stop_arg	stop_args;
	int				rc;

	args = dc_task_get_args(task);
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DB_MD, DF_UUID": stopping svc: hdl="DF_UUID"\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl));

	if (tpriv == NULL) {
		rc = pool_task_create_priv(task, &tpriv);
		if (rc != 0)
			goto out_pool;
	}
	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}

	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_SVC_STOP, pool->dp_pool,
			     pool->dp_pool_hdl, &tpriv->rq_time, &rpc);
	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to create POOL_SVC_STOP RPC",
			 DP_UUID(pool->dp_pool));
		goto out_pool;
	}

	stop_args.dsa_pool = pool;
	crt_req_addref(rpc);
	stop_args.rpc = rpc;

	rc = tse_task_register_comp_cb(task, pool_svc_stop_cb, &stop_args,
				       sizeof(stop_args));
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return daos_rpc_send(rpc, task);

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
out_task:
	if (tpriv != NULL)
		pool_task_destroy_priv(task);
	tse_task_complete(task, rc);
	return rc;
}

int dc_pool_get_redunc(daos_handle_t poh)
{
	struct daos_prop_entry	*entry;
	daos_prop_t		*prop_query;
	int			rf;
	struct dc_pool		*pool = dc_hdl2pool(poh);

	if (pool == NULL)
		return -DER_NO_HDL;

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	if (pool->dp_rf_valid) {
		rf = pool->dp_rf;
		D_RWLOCK_UNLOCK(&pool->dp_map_lock);
		dc_pool_put(pool);
		return rf;
	}
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	dc_pool_put(pool);

	prop_query = daos_prop_alloc(1);
	if (prop_query == NULL)
		return -DER_NOMEM;

	prop_query->dpp_entries[0].dpe_type = DAOS_PROP_PO_REDUN_FAC;
	rf = daos_pool_query(poh, NULL, NULL, prop_query, NULL);
	if (rf) {
		daos_prop_free(prop_query);
		return rf;
	}
	entry = daos_prop_entry_get(prop_query, DAOS_PROP_PO_REDUN_FAC);
	D_ASSERT(entry != NULL);
	rf = entry->dpe_val;
	daos_prop_free(prop_query);

	return rf;
}

/**
 * Get pool_target by dc pool and target index.
 *
 * \param[in]  pool	dc pool
 * \param[in]  tgt_idx	target index.
 * \param[out] tgt	pool target pointer.
 *
 * \return		0 if get the pool_target.
 * \return		errno if it does not get the pool_target.
 */
int
dc_pool_tgt_idx2ptr(struct dc_pool *pool, uint32_t tgt_idx,
		    struct pool_target **tgt)
{
	int		 n;

	/* Get map_tgt so that we can have the rank of the target. */
	D_ASSERT(pool != NULL);
	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	n = pool_map_find_target(pool->dp_map, tgt_idx, tgt);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
	if (n != 1) {
		D_ERROR("failed to find target %u\n", tgt_idx);
		return -DER_INVAL;
	}
	return 0;
}

static int
pool_mark_slave(struct d_hlink *link, void *arg)
{
	struct dc_pool *pool;

	pool           = container_of(link, struct dc_pool, dp_hlink);
	pool->dp_slave = 1;

	return 0;
}

int
dc_pool_mark_all_slave(void)
{
	return daos_hhash_traverse(DAOS_HTYPE_POOL, pool_mark_slave, NULL);
}
