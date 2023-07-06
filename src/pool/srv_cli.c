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
	 * fields. See pool_query_fini for an example.
	 */
	void (*pscc_fini)(uuid_t uuid, crt_rpc_t *rpc, void *arg);
};

/*
 * Call a PS operation. Everywhere in the engine code shall eventually migrate
 * to this template for calling PS operations.
 *
 * The PS is designated by uuid and ranks, the operation by cbs and arg, and
 * the deadline of the whole call by deadline.
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
	struct dss_module_info *info = dss_get_module_info();
	int			rc;

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
		uint32_t		backoff = d_backoff_seq_next(&backoff_seq);

		ep.ep_grp = NULL;
		rc = rsvc_client_choose(&client, &ep);
		if (rc != 0) {
			D_ERROR(DF_PRE": choose pool service replica: "DF_RC"\n", DP_PRE(uuid, cbs),
				DP_RC(rc));
			break;
		}

		rc = pool_req_create(info->dmi_ctx, &ep, cbs->pscc_op, &rpc);
		if (rc != 0) {
			D_ERROR(DF_PRE": create RPC: "DF_RC"\n", DP_PRE(uuid, cbs), DP_RC(rc));
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
				cbs->pscc_fini(uuid, rpc, arg);
				crt_req_decref(rpc);
				break;
			}
		}

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
	struct pool_buf	       *pqa_map_buf;
	uint32_t		pqa_map_size;
};

static int
pool_query_init(uuid_t pool_uuid, crt_rpc_t *rpc, void *varg)
{
	struct pool_query_arg	 *arg = varg;
	struct dss_module_info	 *info = dss_get_module_info();
	struct pool_query_v5_in	 *in;

	in = crt_req_get(rpc);
	uuid_copy(in->pqi_op.pi_uuid, pool_uuid);
	uuid_clear(in->pqi_op.pi_hdl);
	in->pqi_query_bits = pool_query_bits(arg->pqa_info, NULL);

	return map_bulk_create(info->dmi_ctx, &in->pqi_map_bulk, &arg->pqa_map_buf,
			       arg->pqa_map_size);
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
	struct pool_query_v5_out       *out = crt_reply_get(rpc);
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
	struct pool_query_arg	       *arg = varg;
	struct pool_query_v5_in	       *in = crt_req_get(rpc);

	map_bulk_destroy(in->pqi_map_bulk, arg->pqa_map_buf);
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
