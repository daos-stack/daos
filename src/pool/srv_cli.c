/*
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * This file includes functions to call client daos API on the server side.
 */

#define D_LOGFAC DD_FAC(pool)

#include <daos/pool.h>
#include <daos/mgmt.h>
#include <daos/event.h>
#include <daos/task.h>
#include <daos_types.h>
#include <daos_errno.h>
#include <daos_event.h>
#include <daos_task.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/rdb.h>
#include "cli_internal.h"
#include "rpc.h"

int
dsc_pool_close(daos_handle_t ph)
{
	struct dc_pool	*pool;

	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		return 0;

	pl_map_disconnect(pool->dp_pool);
	dc_pool_hdl_unlink(pool); /* -1 ref from dc_pool_hdl_link(pool); */
	dc_pool_put(pool);	  /* -1 ref from dc_pool2hdl(pool, ph); */

	dc_pool_put(pool);
	return 0;
}

int
dsc_pool_open(uuid_t pool_uuid, uuid_t poh_uuid, unsigned int flags,
	      const char *grp, struct pool_map *map, d_rank_list_t *svc_list,
	      daos_handle_t *ph)
{
	struct dc_pool	*pool;
	int		rc = 0;

	if (daos_handle_is_valid(*ph)) {
		pool = dc_hdl2pool(*ph);
		if (pool != NULL)
			D_GOTO(out, rc = 0);
	}

	/** allocate and fill in pool connection */
	pool = dc_pool_alloc(pool_map_comp_cnt(map));
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_TRACE, "after alloc "DF_UUIDF"\n", DP_UUID(pool_uuid));
	uuid_copy(pool->dp_pool, pool_uuid);
	uuid_copy(pool->dp_pool_hdl, poh_uuid);
	pool->dp_capas = flags;

	/** attach to the server group and initialize rsvc_client */
	rc = dc_mgmt_sys_attach(NULL, &pool->dp_sys);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ASSERT(svc_list != NULL);
	rc = rsvc_client_init(&pool->dp_client, svc_list);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_TRACE, "before update "DF_UUIDF"\n", DP_UUID(pool_uuid));
	rc = dc_pool_map_update(pool, map, true);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_MD, DF_UUID": create: hdl="DF_UUIDF" flags=%x\n",
		DP_UUID(pool_uuid), DP_UUID(pool->dp_pool_hdl), flags);

	dc_pool_hdl_link(pool); /* +1 ref */
	dc_pool2hdl(pool, ph);  /* +1 ref */

out:
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}

int
dsc_pool_tgt_exclude(const uuid_t uuid, const char *grp,
		     const d_rank_list_t *svc, struct d_tgt_list *tgts)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_EXCLUDE);

	rc = dc_task_create(dc_pool_exclude, dsc_scheduler(), NULL, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dsc_task_run(task, NULL, NULL, 0, true);
}

int
dsc_pool_tgt_reint(const uuid_t uuid, const char *grp,
		   const d_rank_list_t *svc, struct d_tgt_list *tgts)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_EXCLUDE);

	rc = dc_task_create(dc_pool_reint, dsc_scheduler(), NULL, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dsc_task_run(task, NULL, NULL, 0, true);
}

enum dsc_pool_svc_call_consume_cb_rc {
	DSC_POOL_SVC_CALL_AGAIN		= 1,
	DSC_POOL_SVC_CALL_AGAIN_NOW
};

struct dsc_pool_svc_call_cbs {
	/* Pool service operation. */
	enum pool_operation pscc_op;

	/*
	 * Initialize the request of \a rpc and potentially certain \a arg
	 * fields. See pool_query_init for an example.
	 */
	int (*pscc_init)(uuid_t uuid, crt_rpc_t *rpc, void *arg);

	/*
	 * Process the reply of \a rpc. If DSC_POOL_SVC_CALL_AGAIN is returned,
	 * a new RPC will be created to make the call again after a backoff. If
	 * DSC_POOL_SVC_CALL_AGAIN_NOW is returned, a new RPC will be created
	 * to make the call again without any backoff (so please use this
	 * return value selectively). See pool_query_consume for an example.
	 */
	int (*pscc_consume)(uuid_t uuid, crt_rpc_t *rpc, void *arg);

	/*
	 * Finalize the request of \a rpc and potentially certain \a arg
	 * fields. See pool_query_fini for an example. This can be NULL.
	 */
	void (*pscc_fini)(uuid_t uuid, crt_rpc_t *rpc, void *arg);
};

/*
 * Call a PS operation. Everywhere in the engine code shall eventually migrate
 * to this template for calling PS operations.
 *
 * The PS is designated by uuid and ranks, the operation by cbs and arg, and
 * the deadline in milliseconds of the whole call by deadline.
 *
 * The implementation is simple, if not overly so. A few ideas for future
 * consideration:
 *
 *   - Cache rsvc_client objects beyond an individual call, so that subsequent
 *     calls to the same PS don't need to begin with a leader search.
 *
 *   - Use some RPC that can tolerate a shorter RPC timeout to search for a
 *     leader.
 *
 *   - Cache the availability status of a PS for a while, so that not every
 *     call to an unavailable PS needs to reach its deadline.
 */
static int
dsc_pool_svc_call(uuid_t uuid, d_rank_list_t *ranks, struct dsc_pool_svc_call_cbs *cbs, void *arg,
		  uint64_t deadline)
{
#define DF_PRE DF_UUID": %s"
#define DP_PRE(uuid, cbs) DP_UUID(uuid), dc_pool_op_str(cbs->pscc_op)

	struct rsvc_client	client;
	struct d_backoff_seq	backoff_seq;
	uuid_t                  no_uuid;
	struct dss_module_info *info = dss_get_module_info();
	int			rc;

	uuid_clear(no_uuid);
	rc = rsvc_client_init(&client, ranks);
	if (rc != 0) {
		D_ERROR(DF_PRE": initialize replicated service client: "DF_RC"\n",
			DP_PRE(uuid, cbs), DP_RC(rc));
		return rc;
	}

	/*
	 * Since it's common that the first RPC gets a "not leader" reply with
	 * a valid hint on who the leader is, let the first backoff be zero.
	 */
	rc = d_backoff_seq_init(&backoff_seq, 1 /* nzeros */, 16 /* factor */, 8 /* next (ms) */,
				1 << 12 /* max (ms) */);
	D_ASSERTF(rc == 0, DF_PRE": initialize backoff sequence: "DF_RC"\n", DP_PRE(uuid, cbs),
		  DP_RC(rc));

	/* Retry until the deadline. */
	for (;;) {
		crt_endpoint_t		ep;
		crt_rpc_t	       *rpc;
		uint32_t		rpc_timeout;
		uint64_t		t;
		struct pool_op_out     *out;
		uint64_t                req_time = 0;
		uint32_t		backoff = d_backoff_seq_next(&backoff_seq);

		ep.ep_grp = NULL;
		rc = rsvc_client_choose(&client, &ep);
		if (rc != 0) {
			D_ERROR(DF_PRE": choose pool service replica: "DF_RC"\n", DP_PRE(uuid, cbs),
				DP_RC(rc));
			break;
		}

		rc = pool_req_create(info->dmi_ctx, &ep, cbs->pscc_op, uuid, no_uuid, &req_time,
				     &rpc);
		if (rc != 0) {
			DL_ERROR(rc, DF_PRE ": create RPC", DP_PRE(uuid, cbs));
			break;
		}

		rc = cbs->pscc_init(uuid, rpc, arg);
		if (rc != 0) {
			D_ERROR(DF_PRE": initialize RPC: "DF_RC"\n", DP_PRE(uuid, cbs), DP_RC(rc));
			crt_req_decref(rpc);
			break;
		}

		/* Cap the RPC timeout according to the deadline. */
		t = daos_getmtime_coarse();
		if (t >= deadline) {
			if (cbs->pscc_fini != NULL)
				cbs->pscc_fini(uuid, rpc, arg);
			crt_req_decref(rpc);
			goto time_out;
		}
		rc = crt_req_get_timeout(rpc, &rpc_timeout);
		D_ASSERTF(rc == 0, DF_PRE": get RPC timeout: "DF_RC"\n", DP_PRE(uuid, cbs),
			  DP_RC(rc));
		if (t + rpc_timeout * 1000 > deadline) {
			/* We know that t < deadline here. See above. */
			rpc_timeout = (deadline - t) / 1000;
			/*
			 * If the RPC timeout becomes less than 1 s, just time
			 * out the call.
			 */
			if (rpc_timeout < 1) {
				if (cbs->pscc_fini != NULL)
					cbs->pscc_fini(uuid, rpc, arg);
				crt_req_decref(rpc);
				goto time_out;
			}
			rc = crt_req_set_timeout(rpc, rpc_timeout);
			D_ASSERTF(rc == 0, DF_PRE": set RPC timeout: "DF_RC"\n", DP_PRE(uuid, cbs),
				  DP_RC(rc));
		}

		rc = dss_rpc_send(rpc);

		out = crt_reply_get(rpc);
		rc = rsvc_client_complete_rpc(&client, &ep, rc, rc == 0 ? out->po_rc : 0,
					      rc == 0 ? &out->po_hint : NULL);
		if (rc == RSVC_CLIENT_PROCEED && !daos_rpc_retryable_rc(out->po_rc)) {
			rc = cbs->pscc_consume(uuid, rpc, arg);
			D_DEBUG(DB_TRACE, DF_PRE": consume: %d\n", DP_PRE(uuid, cbs), rc);
			if (rc == DSC_POOL_SVC_CALL_AGAIN_NOW) {
				backoff = 0;
			} else if (rc != DSC_POOL_SVC_CALL_AGAIN) {
				if (cbs->pscc_fini != NULL)
					cbs->pscc_fini(uuid, rpc, arg);
				crt_req_decref(rpc);
				break;
			}
		}

		if (cbs->pscc_fini != NULL)
			cbs->pscc_fini(uuid, rpc, arg);
		crt_req_decref(rpc);

		t = daos_getmtime_coarse();
		if (t >= deadline || t + backoff >= deadline) {
time_out:
			/*
			 * If we were to return before reaching the deadline,
			 * the current control plane code would have just
			 * enough time to call us again but would soon give up,
			 * leaving us behind until the second deadline. Hence,
			 * sleep to the deadline.
			 */
			if (t < deadline)
				dss_sleep(deadline - t);
			rc = -DER_TIMEDOUT;
			D_ERROR(DF_PRE": "DF_RC"\n", DP_PRE(uuid, cbs), DP_RC(rc));
			break;
		}

		if (backoff > 0)
			dss_sleep(backoff);
	}

	d_backoff_seq_fini(&backoff_seq);
	rsvc_client_fini(&client);
	return rc;

#undef DP_PRE
#undef DF_PRE
}

struct pool_query_arg {
	d_rank_list_t	      **pqa_ranks;
	daos_pool_info_t       *pqa_info;
	uint32_t	       *pqa_layout_ver;
	uint32_t	       *pqa_upgrade_layout_ver;
	crt_bulk_t              pqa_bulk;
	struct pool_buf	       *pqa_map_buf;
	uint32_t		pqa_map_size;
};

static int
pool_query_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_query_arg  *arg  = varg;
	struct dss_module_info *info = dss_get_module_info();
	struct pool_query_in   *in;
	uint64_t                query_bits;
	int                     rc;

	in = crt_req_get(rpc);
	uuid_copy(in->pqi_op.pi_uuid, pool_uuid);
	uuid_clear(in->pqi_op.pi_hdl);
	query_bits = pool_query_bits(arg->pqa_info, NULL);

	rc = map_bulk_create(info->dmi_ctx, &arg->pqa_bulk, &arg->pqa_map_buf, arg->pqa_map_size);
	if (rc != 0)
		return rc;
	pool_query_in_set_data(rpc, arg->pqa_bulk, query_bits);

	return rc;
}

static int
process_query_result(d_rank_list_t **ranks, daos_pool_info_t *info, uuid_t pool_uuid,
		     uint32_t map_version, uint32_t leader_rank, struct daos_pool_space *ps,
		     struct daos_rebuild_status *rs, struct pool_buf *map_buf)
{
	struct pool_map	       *map;
	int			rc;
	unsigned int		num_disabled = 0;

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create local pool map, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		return rc;
	}

	rc = pool_map_find_failed_tgts(map, NULL, &num_disabled);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to get num disabled tgts, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));
		goto out;
	}
	info->pi_ndisabled = num_disabled;

	if (ranks != NULL) {
		bool	get_enabled = (info ? ((info->pi_bits & DPI_ENGINES_ENABLED) != 0) : false);

		rc = pool_map_get_ranks(pool_uuid, map, get_enabled, ranks);
		if (rc != 0) {
			D_ERROR(DF_UUID": pool_map_get_ranks() failed, "DF_RC"\n",
				DP_UUID(pool_uuid), DP_RC(rc));
			goto out;
		}
		D_DEBUG(DB_MD, DF_UUID": found %u %s ranks in pool map\n",
			DP_UUID(pool_uuid), (*ranks)->rl_nr, get_enabled ? "ENABLED" : "DISABLED");
	}

	pool_query_reply_to_info(pool_uuid, map_buf, map_version, leader_rank, ps, rs, info);

out:
	pool_map_decref(map);
	return rc;
}

static int
pool_query_consume(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_query_arg	       *arg = varg;
	struct pool_query_out          *out = crt_reply_get(rpc);
	int				rc = out->pqo_op.po_rc;

	if (rc == -DER_TRUNC) {
		/*
		 * Our map buffer is too small. Since the PS has told us the
		 * correct size to use, and that size is unlikely to change
		 * frequently, request a retry without any backoff.
		 */
		arg->pqa_map_size = out->pqo_map_buf_size;
		return DSC_POOL_SVC_CALL_AGAIN_NOW;
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to query pool, "DF_RC"\n", DP_UUID(pool_uuid), DP_RC(rc));
		return rc < 0 ? rc : -DER_PROTO;
	}

	D_DEBUG(DB_MGMT, DF_UUID": Successfully queried pool\n", DP_UUID(pool_uuid));

	rc = process_query_result(arg->pqa_ranks, arg->pqa_info, pool_uuid,
				  out->pqo_op.po_map_version, out->pqo_op.po_hint.sh_rank,
				  &out->pqo_space, &out->pqo_rebuild_st, arg->pqa_map_buf);
	if (arg->pqa_layout_ver)
		*arg->pqa_layout_ver = out->pqo_pool_layout_ver;
	if (arg->pqa_upgrade_layout_ver)
		*arg->pqa_upgrade_layout_ver = out->pqo_upgrade_layout_ver;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to process pool query results, "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(rc));

	return rc;
}

static void
pool_query_fini(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_query_arg *arg = varg;

	map_bulk_destroy(arg->pqa_bulk, arg->pqa_map_buf);
	arg->pqa_map_buf = NULL;
}

static struct dsc_pool_svc_call_cbs pool_query_cbs = {
	.pscc_op	= POOL_QUERY,
	.pscc_init	= pool_query_init,
	.pscc_consume	= pool_query_consume,
	.pscc_fini	= pool_query_fini
};

/**
 * Query the pool without holding a pool handle.
 *
 * \param[in]	pool_uuid		UUID of the pool
 * \param[in]	ps_ranks		Ranks of pool svc replicas
 * \param[in]	deadline		Unix time deadline in milliseconds
 * \param[out]	ranks			Optional, returned storage ranks in this pool.
 *					If #pool_info is NULL, engines with disabled targets.
 *					If #pool_info is passed, engines with enabled or disabled
 *					targets according to #pi_bits (DPI_ENGINES_ENABLED bit).
 *					Note: ranks may be empty (i.e., *ranks->rl_nr may be 0).
 *					The caller must free the list with d_rank_list_free().
 * \param[out]	pool_info		Results of the pool query
 * \param[out]	pool_layout_ver		Results of the current pool global version
 * \param[out]	pool_upgrade_layout_ver	Results of the target latest pool global version
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		Negative value	Error
 */
int
dsc_pool_svc_query(uuid_t pool_uuid, d_rank_list_t *ps_ranks, uint64_t deadline,
		   d_rank_list_t **ranks, daos_pool_info_t *pool_info, uint32_t *pool_layout_ver,
		   uint32_t *upgrade_layout_ver)
{
	struct pool_query_arg arg = {
		.pqa_ranks		= ranks,
		.pqa_info		= pool_info,
		.pqa_layout_ver		= pool_layout_ver,
		.pqa_upgrade_layout_ver	= upgrade_layout_ver,
		.pqa_map_size		= 127 /* 4 KB */
	};

	return dsc_pool_svc_call(pool_uuid, ps_ranks, &pool_query_cbs, &arg, deadline);
}

struct pool_query_info_arg {
	d_rank_t            pqia_rank;
	uint32_t            pqia_tgt_idx;
	daos_target_info_t *pqia_info;
};

static int
pool_query_info_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_query_info_arg *arg = varg;

	pool_query_info_in_set_data(rpc, arg->pqia_rank, arg->pqia_tgt_idx);
	return 0;
}

static int
pool_query_info_consume(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_query_info_arg *arg = varg;
	struct pool_query_info_out *out = crt_reply_get(rpc);
	int                         i;
	int                         rc = out->pqio_op.po_rc;

	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to query pool rank %u target %u", DP_UUID(pool_uuid),
			 arg->pqia_rank, arg->pqia_tgt_idx);
		return rc;
	}

	D_DEBUG(DB_MGMT, DF_UUID ": Successfully queried pool rank %u target %u\n",
		DP_UUID(pool_uuid), arg->pqia_rank, arg->pqia_tgt_idx);

	arg->pqia_info->ta_type  = DAOS_TP_UNKNOWN;
	arg->pqia_info->ta_state = out->pqio_state;
	for (i = 0; i < DAOS_MEDIA_MAX; i++) {
		arg->pqia_info->ta_space.s_total[i] = out->pqio_space.s_total[i];
		arg->pqia_info->ta_space.s_free[i]  = out->pqio_space.s_free[i];
	}

	return 0;
}

static struct dsc_pool_svc_call_cbs pool_query_info_cbs = {
	.pscc_op	= POOL_QUERY_INFO,
	.pscc_init	= pool_query_info_init,
	.pscc_consume	= pool_query_info_consume,
	.pscc_fini	= NULL
};

/**
 * Query pool target information without holding a pool handle.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ps_ranks	Ranks of pool svc replicas
 * \param[in]	deadline	Unix time deadline in milliseconds
 * \param[in]	rank		Pool storage engine rank
 * \param[in]	tgt_idx		Target index within the pool storage engine
 * \param[out]	ti		Target information (state, storage capacity and usage)
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		Negative value	Other error
 */
int
dsc_pool_svc_query_target(uuid_t pool_uuid, d_rank_list_t *ps_ranks, uint64_t deadline,
			  d_rank_t rank, uint32_t tgt_idx, daos_target_info_t *ti)
{
	struct pool_query_info_arg arg = {
		.pqia_rank	= rank,
		.pqia_tgt_idx	= tgt_idx,
		.pqia_info	= ti
	};

	if (ti == NULL)
		return -DER_INVAL;
	D_DEBUG(DB_MGMT, DF_UUID ": Querying pool target %u\n", DP_UUID(pool_uuid), tgt_idx);
	return dsc_pool_svc_call(pool_uuid, ps_ranks, &pool_query_info_cbs, &arg, deadline);
}

struct pool_evict_arg {
	uuid_t   *pea_handles;
	size_t    pea_n_handles;
	char     *pea_machine;
	uint32_t  pea_destroy;
	uint32_t  pea_force;
	uint32_t *pea_count;
};

static int
pool_evict_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_evict_arg *arg = varg;
	struct pool_evict_in  *in  = crt_req_get(rpc);

	in->pvi_hdls.ca_arrays = arg->pea_handles;
	in->pvi_hdls.ca_count  = arg->pea_n_handles;
	in->pvi_machine        = arg->pea_machine;
	/* Pool destroy (force=false): assert no open handles / do not evict.
	 * Pool destroy (force=true): evict any/all open handles on the pool.
	 */
	in->pvi_pool_destroy       = arg->pea_destroy;
	in->pvi_pool_destroy_force = arg->pea_force;
	return 0;
}

static int
pool_evict_consume(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_evict_arg *arg = varg;
	struct pool_evict_out *out = crt_reply_get(rpc);
	int                    rc  = out->pvo_op.po_rc;

	if (rc != 0)
		DL_ERROR(rc, DF_UUID ": pool destroy failed to evict handles", DP_UUID(pool_uuid));
	if (arg->pea_count != NULL)
		*arg->pea_count = out->pvo_n_hdls_evicted;
	return rc;
}

static struct dsc_pool_svc_call_cbs pool_evict_cbs = {
	.pscc_op	= POOL_EVICT,
	.pscc_init	= pool_evict_init,
	.pscc_consume	= pool_evict_consume,
	.pscc_fini	= NULL
};

/**
 * Test and (if applicable based on destroy and force option) evict all open
 * handles on a pool.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[in]	deadline	Unix time deadline in milliseconds
 * \param[in]	handles		List of handles to selectively evict
 * \param[in]	n_handles	Number of items in handles
 * \param[in]	destroy		If true the evict request is a destroy request
 * \param[in]	force		If true and destroy is true request all handles
 *				be forcibly evicted
 * \param[in]   machine		Hostname to use as filter for evicting handles
 * \param[out]	count		Number of handles evicted
 *
 * \return	0		Success
 *		-DER_BUSY	Open pool handles exist and no force requested
 */
int
dsc_pool_svc_check_evict(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline, uuid_t *handles,
			 size_t n_handles, uint32_t destroy, uint32_t force, char *machine,
			 uint32_t *count)
{
	struct pool_evict_arg arg = {
		.pea_handles	= handles,
		.pea_n_handles	= n_handles,
		.pea_machine	= machine,
		.pea_destroy	= destroy,
		.pea_force	= force,
		.pea_count	= count
	};

	D_DEBUG(DB_MGMT, DF_UUID ": Destroy pool (force: %d), inspect/evict handles\n",
		DP_UUID(pool_uuid), force);
	return dsc_pool_svc_call(pool_uuid, ranks, &pool_evict_cbs, &arg, deadline);
}

struct pool_get_prop_arg {
	daos_prop_t *pgpa_prop;
};

static int
pool_get_prop_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_get_prop_arg *arg = varg;

	pool_prop_get_in_set_data(rpc, pool_query_bits(NULL, arg->pgpa_prop));
	return 0;
}

static int
pool_get_prop_consume(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_get_prop_arg *arg = varg;
	struct pool_prop_get_out *out = crt_reply_get(rpc);
	int                       rc  = out->pgo_op.po_rc;

	if (rc != 0) {
		DL_ERROR(rc, DF_UUID ": failed to get prop for pool", DP_UUID(pool_uuid));
		return rc;
	}

	return daos_prop_copy(arg->pgpa_prop, out->pgo_prop);
}

static struct dsc_pool_svc_call_cbs pool_get_prop_cbs = {
	.pscc_op	= POOL_PROP_GET,
	.pscc_init	= pool_get_prop_init,
	.pscc_consume	= pool_get_prop_consume,
	.pscc_fini	= NULL
};

/**
 * Get the ACL pool property.
 *
 * \param[in]		pool_uuid	UUID of the pool
 * \param[in]		ranks		Pool service replicas
 * \param[in]		deadline	Unix time deadline in milliseconds
 * \param[in][out]	prop		Prop with requested properties, to be
 *					filled out and returned.
 *
 * \return	0		Success
 *
 */
int
dsc_pool_svc_get_prop(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline, daos_prop_t *prop)
{
	struct pool_get_prop_arg arg = {
		.pgpa_prop	= prop
	};

	D_DEBUG(DB_MGMT, DF_UUID ": Getting prop\n", DP_UUID(pool_uuid));
	return dsc_pool_svc_call(pool_uuid, ranks, &pool_get_prop_cbs, &arg, deadline);
}

struct pool_set_prop_arg {
	daos_prop_t *pspa_prop;
};

static int
pool_set_prop_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_set_prop_arg *arg = varg;

	pool_prop_set_in_set_data(rpc, arg->pspa_prop);
	return 0;
}

static int
pool_set_prop_consume(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_prop_set_out *out = crt_reply_get(rpc);
	int                       rc  = out->pso_op.po_rc;

	if (rc != 0)
		DL_ERROR(rc, DF_UUID ": failed to set prop for pool", DP_UUID(pool_uuid));
	return rc;
}

static struct dsc_pool_svc_call_cbs pool_set_prop_cbs = {
	.pscc_op	= POOL_PROP_GET,
	.pscc_init	= pool_set_prop_init,
	.pscc_consume	= pool_set_prop_consume,
	.pscc_fini	= NULL
};

/**
 * Set the requested pool properties.
 *
 * \param[in]	pool_uuid	UUID of the pool
 * \param[in]	ranks		Pool service replicas
 * \param[in]	deadline	Unix time deadline in milliseconds
 * \param[in]	prop		Pool prop
 *
 * \return	0		Success
 */
int
dsc_pool_svc_set_prop(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline, daos_prop_t *prop)
{
	struct pool_set_prop_arg arg ={
		.pspa_prop	= prop
	};

	D_DEBUG(DB_MGMT, DF_UUID ": Setting pool prop\n", DP_UUID(pool_uuid));

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_PERF_DOMAIN)) {
		D_ERROR("Can't set perf_domain on existing pool.\n");
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_REDUN_FAC)) {
		D_ERROR("Can't set set redundancy factor on existing pool.\n");
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_EC_PDA)) {
		D_ERROR("Can't set EC performance domain affinity on existing pool\n");
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_RP_PDA)) {
		D_ERROR("Can't set RP performance domain affinity on existing pool\n");
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_GLOBAL_VERSION)) {
		D_ERROR("Can't set pool global version if pool is created.\n");
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_UPGRADE_STATUS)) {
		D_ERROR("Can't set pool upgrade status if pool is created.\n");
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_OPS_ENABLED)) {
		D_ERROR("Can't set pool svc_ops_enabled on existing pool.\n");
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_OPS_ENTRY_AGE)) {
		D_ERROR("Can't set pool svc_ops_entry_age on existing pool.\n");
		return -DER_NO_PERM;
	}

	/* Disallow to begin with; will support in the future. */
	if (daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_REDUN_FAC)) {
		D_ERROR(DF_UUID ": cannot set pool service redundancy factor on existing pool\n",
			DP_UUID(pool_uuid));
		return -DER_NO_PERM;
	}

	if (daos_prop_entry_get(prop, DAOS_PROP_PO_OBJ_VERSION)) {
		D_ERROR("Can't set pool obj version if pool is created.\n");
		return -DER_NO_PERM;
	}

	return dsc_pool_svc_call(pool_uuid, ranks, &pool_set_prop_cbs, &arg, deadline);
}

struct pool_extend_arg {
	int                  pea_ntargets;
	const d_rank_list_t *pea_rank_list;
	int                  pea_ndomains;
	const uint32_t      *pea_domains;
};

static int
pool_extend_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_extend_arg *arg = varg;
	struct pool_extend_in  *in  = crt_req_get(rpc);

	in->pei_ntgts             = arg->pea_ntargets;
	in->pei_ndomains          = arg->pea_ndomains;
	in->pei_tgt_ranks         = (d_rank_list_t *)arg->pea_rank_list;
	in->pei_domains.ca_count  = arg->pea_ndomains;
	in->pei_domains.ca_arrays = (uint32_t *)arg->pea_domains;
	return 0;
}

static int
pool_extend_consume(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_extend_out *out = crt_reply_get(rpc);
	int                     rc  = out->peo_op.po_rc;

	if (rc != 0)
		DL_ERROR(rc, DF_UUID ": Failed to set targets to UP state for reintegration",
			 DP_UUID(pool_uuid));
	return rc;
}

static struct dsc_pool_svc_call_cbs pool_extend_cbs = {
	.pscc_op	= POOL_EXTEND,
	.pscc_init	= pool_extend_init,
	.pscc_consume	= pool_extend_consume,
	.pscc_fini	= NULL
};

int
dsc_pool_svc_extend(uuid_t pool_uuid, d_rank_list_t *svc_ranks, uint64_t deadline, int ntargets,
		    const d_rank_list_t *rank_list, int ndomains, const uint32_t *domains)
{
	struct pool_extend_arg arg = {
		.pea_ntargets	= ntargets,
		.pea_rank_list	= rank_list,
		.pea_ndomains	= ndomains,
		.pea_domains	= domains
	};

	return dsc_pool_svc_call(pool_uuid, svc_ranks, &pool_extend_cbs, &arg, deadline);
}

struct pool_update_target_state_arg {
	struct pool_target_addr_list *puta_target_addrs;
	pool_comp_state_t             puta_state;
};

static int
pool_update_target_state_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_update_target_state_arg *arg = varg;

	pool_tgt_update_in_set_data(rpc, arg->puta_target_addrs->pta_addrs,
				    (size_t)arg->puta_target_addrs->pta_number);
	return 0;
}

static int
pool_update_target_state_consume(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_update_target_state_arg *arg = varg;
	struct pool_tgt_update_out          *out = crt_reply_get(rpc);
	int                                  rc  = out->pto_op.po_rc;

	if (rc != 0)
		DL_ERROR(rc, DF_UUID ": Failed to set targets to %s state", DP_UUID(pool_uuid),
			 arg->puta_state == PO_COMP_ST_DOWN ? "DOWN"
			 : arg->puta_state == PO_COMP_ST_UP ? "UP"
							    : "UNKNOWN");
	return rc;
}

static struct dsc_pool_svc_call_cbs pool_exclude_cbs = {
	.pscc_op	= POOL_EXCLUDE,
	.pscc_init	= pool_update_target_state_init,
	.pscc_consume	= pool_update_target_state_consume,
	.pscc_fini	= NULL
};

static struct dsc_pool_svc_call_cbs pool_reint_cbs = {
	.pscc_op	= POOL_REINT,
	.pscc_init	= pool_update_target_state_init,
	.pscc_consume	= pool_update_target_state_consume,
	.pscc_fini	= NULL
};

static struct dsc_pool_svc_call_cbs pool_drain_cbs = {
	.pscc_op	= POOL_DRAIN,
	.pscc_init	= pool_update_target_state_init,
	.pscc_consume	= pool_update_target_state_consume,
	.pscc_fini	= NULL
};

int
dsc_pool_svc_update_target_state(uuid_t pool_uuid, d_rank_list_t *ranks, uint64_t deadline,
				 struct pool_target_addr_list *target_addrs,
				 pool_comp_state_t             state)
{
	struct pool_update_target_state_arg arg = {
		.puta_target_addrs	= target_addrs,
		.puta_state		= state
	};
	struct dsc_pool_svc_call_cbs       *cbs;

	switch (state) {
	case PO_COMP_ST_DOWN:
		cbs = &pool_exclude_cbs;
		break;
	case PO_COMP_ST_UP:
		cbs = &pool_reint_cbs;
		break;
	case PO_COMP_ST_DRAIN:
		cbs = &pool_drain_cbs;
		break;
	default:
		return -DER_INVAL;
	}

	return dsc_pool_svc_call(pool_uuid, ranks, cbs, &arg, deadline);
}
