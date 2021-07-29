/*
 * (C) Copyright 2016-2021 Intel Corporation.
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
#include <daos/event.h>
#include <daos/mgmt.h>
#include <daos/placement.h>
#include <daos/pool.h>
#include <daos/security.h>
#include <daos_types.h>
#include "cli_internal.h"
#include "rpc.h"

/** Replicated Service client state (used by Management API) */
struct rsvc_client_state {
	struct rsvc_client  scs_client;
	struct dc_mgmt_sys *scs_sys;
};

/**
 * Initialize pool interface
 */
int
dc_pool_init(void)
{
	int rc;

	rc = daos_rpc_register(&pool_proto_fmt, POOL_PROTO_CLI_COUNT,
				NULL, DAOS_POOL_MODULE);
	if (rc != 0)
		D_ERROR("failed to register pool RPCs: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Finalize pool interface
 */
void
dc_pool_fini(void)
{
	daos_rpc_unregister(&pool_proto_fmt);
}

static void
pool_free(struct d_hlink *hlink)
{
	struct dc_pool *pool;

	pool = container_of(hlink, struct dc_pool, dp_hlink);
	D_ASSERT(daos_hhash_link_empty(&pool->dp_hlink));
	D_RWLOCK_DESTROY(&pool->dp_map_lock);
	D_MUTEX_DESTROY(&pool->dp_client_lock);
	D_RWLOCK_DESTROY(&pool->dp_co_list_lock);
	D_ASSERT(d_list_empty(&pool->dp_co_list));

	if (pool->dp_map != NULL)
		pool_map_decref(pool->dp_map);

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
		d_rank_list_t	*ranklist = NULL;

		/* Query MS for replica ranks. Not under client lock. */
		if (cli_lock)
			D_MUTEX_UNLOCK(cli_lock);
		rc = dc_mgmt_pool_find(sys, label, puuid, &ranklist);
		if (rc) {
			D_ERROR(DF_UUID":%s: dc_mgmt_pool_find() failed, "
				DF_RC"\n", DP_UUID(puuid), label ? label : "",
				DP_RC(rc));
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
				D_DEBUG(DF_DSMC, DF_UUID":%s: "
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

/* Assume dp_map_lock is locked before calling this function */
int
dc_pool_map_update(struct dc_pool *pool, struct pool_map *map,
		   unsigned int map_version, bool connect)
{
	int rc;

	D_ASSERT(map != NULL);
	if (pool->dp_map == NULL) {
		rc = pl_map_update(pool->dp_pool, map, connect,
				DEFAULT_PL_TYPE);
		if (rc != 0)
			D_GOTO(out, rc);

		D_DEBUG(DF_DSMC, DF_UUID": init pool map: %u\n",
			DP_UUID(pool->dp_pool), pool_map_get_version(map));
		D_GOTO(out_update, rc = 0);
	}

	if (map_version < pool_map_get_version(pool->dp_map)) {
		D_DEBUG(DF_DSMC, DF_UUID": got older pool map: %u -> %u %p\n",
			DP_UUID(pool->dp_pool),
			pool_map_get_version(pool->dp_map), map_version, pool);
		D_GOTO(out, rc = 0);
	}

	D_DEBUG(DF_DSMC, DF_UUID": updating pool map: %u -> %u\n",
		DP_UUID(pool->dp_pool),
		pool->dp_map == NULL ?
		0 : pool_map_get_version(pool->dp_map), map_version);

	rc = pl_map_update(pool->dp_pool, map, connect, DEFAULT_PL_TYPE);
	if (rc != 0) {
		D_ERROR("Failed to refresh placement map: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	pool_map_decref(pool->dp_map);
out_update:
	pool_map_addref(map);
	pool->dp_map = map;
	if (pool->dp_map_version_known < map_version)
		pool->dp_map_version_known = map_version;
out:
	return rc;
}

/*
 * Using "map_buf", "map_version", and "mode", update "pool->dp_map" and fill
 * "tgts" and/or "info", "prop" if not NULL.
 */
static int
process_query_reply(struct dc_pool *pool, struct pool_buf *map_buf,
		    uint32_t map_version, uint32_t leader_rank,
		    struct daos_pool_space *ps, struct daos_rebuild_status *rs,
		    d_rank_list_t *tgts, daos_pool_info_t *info,
		    daos_prop_t *prop_req, daos_prop_t *prop_reply,
		    bool connect)
{
	struct pool_map	       *map;
	int			rc;

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	D_RWLOCK_WRLOCK(&pool->dp_map_lock);
	rc = dc_pool_map_update(pool, map, map_version, connect);
	if (rc)
		D_GOTO(out_unlock, rc);

	/* Scan all targets for info->pi_ndisabled and/or tgts. */
	if (info != NULL) {
		unsigned int num_disabled = 0;

		rc = pool_map_find_failed_tgts(map, NULL, &num_disabled);
		if (rc == 0)
			info->pi_ndisabled = num_disabled;
		else
			D_ERROR("Couldn't get failed targets, "DF_RC"\n",
				DP_RC(rc));
	}
out_unlock:
	pool_map_decref(map); /* NB: protected by pool::dp_map_lock */
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
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(out->po_rc))) {
		rc = tse_task_reinit(task);
		if (rc != 0)
			return rc;
		return RSVC_CLIENT_RECHOOSE;
	}
	return RSVC_CLIENT_PROCEED;
}

struct pool_connect_arg {
	daos_pool_info_t	*pca_info;
	struct pool_buf		*pca_map_buf;
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
};

static int
pool_connect_cp(tse_task_t *task, void *data)
{
	struct pool_connect_arg *arg = (struct pool_connect_arg *)data;
	struct dc_pool		*pool = dc_task_get_priv(task);
	daos_pool_info_t	*info = arg->pca_info;
	struct pool_buf		*map_buf = arg->pca_map_buf;
	struct pool_connect_in	*pci = crt_req_get(arg->rpc);
	struct pool_connect_out	*pco = crt_reply_get(arg->rpc);
	bool			 put_pool = true;
	int			 rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &pco->pco_op, task);
	if (rc < 0) {
		D_GOTO(out, rc);
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		put_pool = false;
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
		D_DEBUG(DF_DSMC, "current pool map buffer size (%ld) < size "
			"required by server (%u), retry after allocating it\n",
			pool_buf_size(map_buf->pb_nr), pco->pco_map_buf_size);
		pool->dp_map_sz = pco->pco_map_buf_size;
		rc = tse_task_reinit(task);
		if (rc == 0)
			put_pool = false;
		D_GOTO(out, rc);
	} else if (rc != 0) {
		D_ERROR("failed to connect to pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = process_query_reply(pool, map_buf, pco->pco_op.po_map_version,
				 pco->pco_op.po_hint.sh_rank,
				 &pco->pco_space, &pco->pco_rebuild_st,
				 NULL /* tgts */, info, NULL, NULL, true);
	if (rc != 0) {
		/* TODO: What do we do about the remote connection state? */
		D_ERROR("failed to create local pool map: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = dc_mgmt_notify_pool_connect(pool);
	if (rc != 0) {
		D_ERROR("failed to register pool connect with agent: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* add pool to hhash */
	dc_pool_hdl_link(pool); /* +1 ref */
	dc_pool2hdl(pool, arg->hdlp); /* +1 ref */

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), arg->hdlp->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	crt_req_decref(arg->rpc);
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
	/* Ensure credential memory is wiped clean */
	explicit_bzero(pci->pci_cred.iov_buf, pci->pci_cred.iov_buf_len);
	daos_iov_free(&pci->pci_cred);
	if (put_pool)
		dc_pool_put(pool);
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
	/** sy_info.crt_ctx_share_addr */
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
dc_pool_connect_internal(tse_task_t *task, daos_pool_info_t *info,
			 const char *label, daos_handle_t *poh)
{
	struct dc_pool		*pool;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct pool_connect_in	*pci;
	struct pool_buf		*map_buf;
	struct pool_connect_arg	 con_args;
	int			 rc;

	pool = dc_task_get_priv(task);
	/** Choose an endpoint and create an RPC. */
	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(label, pool->dp_pool, &pool->dp_client,
				     &pool->dp_client_lock, pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID":%s: cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool),
			label ? label : "", DP_RC(rc));
		goto out;
	}

	/** Pool connect RPC by UUID (provided, or looked up by label above) */
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_CONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	/** for con_args */
	crt_req_addref(rpc);

	/** fill in request buffer */
	pci = crt_req_get(rpc);

	/** request credentials */
	rc = dc_sec_request_creds(&pci->pci_cred);
	if (rc != 0) {
		D_ERROR("failed to obtain security credential: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out_req, rc);
	}

	uuid_copy(pci->pci_op.pi_uuid, pool->dp_pool);
	uuid_copy(pci->pci_op.pi_hdl, pool->dp_pool_hdl);
	pci->pci_flags = pool->dp_capas;
	pci->pci_query_bits = pool_query_bits(info, NULL);

	rc = map_bulk_create(daos_task2ctx(task), &pci->pci_map_bulk, &map_buf,
			     pool_buf_nr(pool->dp_map_sz));
	if (rc != 0)
		D_GOTO(out_cred, rc);

	/** Prepare "con_args" for pool_connect_cp(). */
	con_args.pca_info = info;
	con_args.pca_map_buf = map_buf;
	con_args.rpc = rpc;
	con_args.hdlp = poh;

	rc = tse_task_register_comp_cb(task, pool_connect_cp, &con_args,
				       sizeof(con_args));
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return daos_rpc_send(rpc, task);

out_bulk:
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
out_cred:
	/* Ensure credential memory is wiped clean */
	explicit_bzero(pci->pci_cred.iov_buf, pci->pci_cred.iov_buf_len);
	daos_iov_free(&pci->pci_cred);
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
	struct dc_pool		*pool = NULL;
	const char		*label;
	uuid_t			 uuid;
	int			 rc;

	args = dc_task_get_args(task);
	pool = dc_task_get_priv(task);

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

	if (pool == NULL) {
		if (!flags_are_valid(args->flags) || args->poh == NULL)
			D_GOTO(out_task, rc = -DER_INVAL);

		/** allocate and fill in pool connection */
		rc = init_pool(label, uuid, args->flags, args->grp, &pool);
		if (rc)
			goto out_task;

		daos_task_set_priv(task, pool);
		D_DEBUG(DF_DSMC, "%s: connecting: hdl="DF_UUIDF" flags=%x\n",
				args->pool ? : "<compat>",
				DP_UUID(pool->dp_pool_hdl), args->flags);
	}

	rc = dc_pool_connect_internal(task, args->info, label, args->poh);
	if (rc)
		goto out_pool;

	return rc;

out_pool:
	dc_pool_put(pool);
out_task:
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
	struct dc_pool			*pool = arg->pool;
	struct pool_disconnect_out	*pdo = crt_reply_get(arg->rpc);
	int				 rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(pool, &arg->rpc->cr_ep, rc,
					   &pdo->pdo_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

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

	D_DEBUG(DF_DSMC, DF_UUID": disconnected: cookie="DF_X64" hdl="DF_UUID
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
	}

	/* remove pool from hhash */
	dc_pool_hdl_unlink(pool);
	dc_pool_put(pool);
	arg->hdl.cookie = 0;

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_pool_disconnect(tse_task_t *task)
{
	daos_pool_disconnect_t		*args;
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct pool_disconnect_in	*pdi;
	struct pool_disconnect_arg	 disc_args;
	int				 rc = 0;

	args = dc_task_get_args(task);

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": disconnecting: hdl="DF_UUID" cookie="DF_X64
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
		D_DEBUG(DF_DSMC, DF_UUID": disconnecting: cookie="DF_X64" hdl="
			DF_UUID" slave\n", DP_UUID(pool->dp_pool),
			args->poh.cookie, DP_UUID(pool->dp_pool_hdl));

		pl_map_disconnect(pool->dp_pool);
		/* remove pool from hhash */
		dc_pool_hdl_unlink(pool);
		dc_pool_put(pool);
		args->poh.cookie = 0;
		D_GOTO(out_pool, rc);
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
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_DISCONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_pool, rc);
	}

	/** fill in request buffer */
	pdi = crt_req_get(rpc);
	D_ASSERT(pdi != NULL);
	uuid_copy(pdi->pdi_op.pi_uuid, pool->dp_pool);
	uuid_copy(pdi->pdi_op.pi_hdl, pool->dp_pool_hdl);

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
		D_ERROR("Larger glob buffer needed ("DF_U64" bytes provided, "
			""DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_client_buf, rc = -DER_TRUNC);
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
		D_DEBUG(DF_DSMC, "Invalid parameter, NULL glob pointer.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (glob->iov_buf != NULL && (glob->iov_buf_len == 0 ||
	    glob->iov_buf_len < glob->iov_len)) {
		D_DEBUG(DF_DSMC, "Invalid parameter of glob, iov_buf %p, "
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

	rc = pool_map_create(map_buf, pool_glob->dpg_map_version,
			     &pool->dp_map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = pl_map_update(pool->dp_pool, pool->dp_map, true,
			DEFAULT_PL_TYPE);
	if (rc != 0)
		D_GOTO(out, rc);

	/* add pool to hash */
	dc_pool_hdl_link(pool); /* +1 ref */
	dc_pool2hdl(pool, poh); /* +1 ref */

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" slave\n", DP_UUID(pool->dp_pool), poh->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	if (rc != 0)
		D_ERROR("failed, rc: "DF_RC"\n", DP_RC(rc));
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
		D_DEBUG(DF_DSMC, "Invalid parameter of glob, iov_buf %p, "
			"iov_buf_len "DF_U64", iov_len "DF_U64".\n",
			glob.iov_buf, glob.iov_buf_len, glob.iov_len);
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (poh == NULL) {
		D_DEBUG(DF_DSMC, "Invalid parameter, NULL poh.\n");
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

struct pool_update_state {
	struct rsvc_client	client;
	struct dc_mgmt_sys     *sys;
};

static int
pool_tgt_update_cp(tse_task_t *task, void *data)
{
	struct pool_update_state	*state = dc_task_get_priv(task);
	crt_rpc_t			*rpc = *((crt_rpc_t **)data);
	struct pool_tgt_update_in	*in = crt_req_get(rpc);
	struct pool_tgt_update_out	*out = crt_reply_get(rpc);
	bool				 free_state = true;
	int				 rc = task->dt_result;

	rc = rsvc_client_complete_rpc(&state->client, &rpc->cr_ep, rc,
				      out->pto_op.po_rc, &out->pto_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED &&
	     daos_rpc_retryable_rc(out->pto_op.po_rc))) {
		rc = tse_task_reinit(task);
		if (rc != 0)
			D_GOTO(out, rc);
		free_state = false;
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

	D_DEBUG(DF_DSMC, DF_UUID": updated: hdl="DF_UUID" failed=%d\n",
		DP_UUID(in->pti_op.pi_uuid), DP_UUID(in->pti_op.pi_hdl),
		(int)out->pto_addr_list.ca_count);

	D_FREE(in->pti_addr_list.ca_arrays);

	if (out->pto_addr_list.ca_arrays != NULL &&
	    out->pto_addr_list.ca_count > 0) {
		D_ERROR("tgt update failed count %zd\n",
			out->pto_addr_list.ca_count);
		rc = -DER_INVAL;
	}

out:
	crt_req_decref(rpc);
	if (free_state) {
		rsvc_client_fini(&state->client);
		dc_mgmt_sys_detach(state->sys);
		D_FREE(state);
	}
	return rc;
}

static int
dc_pool_update_internal(tse_task_t *task, daos_pool_update_t *args,
			int opc)
{
	struct pool_update_state	*state = dc_task_get_priv(task);
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct pool_tgt_update_in	*in;
	struct pool_target_addr_list	list;
	int				i;
	int				rc;

	if (args->tgts == NULL || args->tgts->tl_nr == 0) {
		D_ERROR("NULL tgts or tgts->tl_nr is zero\n");
		D_GOTO(out_task, rc = -DER_INVAL);
	}

	D_DEBUG(DF_DSMC, DF_UUID": opc %d targets:%u tgts[0]=%u/%d\n",
		DP_UUID(args->uuid), opc, args->tgts->tl_nr,
		args->tgts->tl_ranks[0], args->tgts->tl_tgts[0]);

	if (state == NULL) {
		D_ALLOC_PTR(state);
		if (state == NULL) {
			D_GOTO(out_task, rc = -DER_NOMEM);
		}

		rc = dc_mgmt_sys_attach(args->grp, &state->sys);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to sys attach, rc %d.\n",
				DP_UUID(args->uuid), rc);
			D_GOTO(out_state, rc);
		}
		rc = rsvc_client_init(&state->client,
				      state->sys->sy_server ? args->svc : NULL);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to rsvc_client_init, rc %d.\n",
				DP_UUID(args->uuid), rc);
			D_GOTO(out_group, rc);
		}

		daos_task_set_priv(task, state);
	}

	ep.ep_grp = state->sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, args->uuid,
				     &state->client, NULL /* mutex */,
				     state->sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(args->uuid), DP_RC(rc));
		goto out_client;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pti_op.pi_uuid, args->uuid);

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
	in->pti_addr_list.ca_arrays = list.pta_addrs;
	in->pti_addr_list.ca_count = (size_t)list.pta_number;

	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, pool_tgt_update_cp, &rpc,
				       sizeof(rpc));
	if (rc != 0)
		D_GOTO(out_list, rc);

	return daos_rpc_send(rpc, task);

out_list:
	pool_target_addr_list_free(&list);
	crt_req_decref(rpc);
out_rpc:
	crt_req_decref(rpc);
out_client:
	rsvc_client_fini(&state->client);
out_group:
	dc_mgmt_sys_detach(state->sys);
out_state:
	D_FREE(state);
out_task:
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
	d_rank_list_t		*dqa_tgts;
	daos_pool_info_t	*dqa_info;
	daos_prop_t		*dqa_prop;
	struct pool_buf		*dqa_map_buf;
	crt_rpc_t		*rpc;
};

static int
pool_query_cb(tse_task_t *task, void *data)
{
	struct pool_query_arg	       *arg = (struct pool_query_arg *)data;
	struct pool_buf		       *map_buf = arg->dqa_map_buf;
	struct pool_query_in	       *in = crt_req_get(arg->rpc);
	struct pool_query_out	       *out = crt_reply_get(arg->rpc);
	int				rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->dqa_pool, &arg->rpc->cr_ep, rc,
					   &out->pqo_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	D_DEBUG(DF_DSMC, DF_UUID": query rpc done: %d\n",
		DP_UUID(arg->dqa_pool->dp_pool), rc);

	if (rc) {
		D_ERROR("RPC error while querying pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->pqo_op.po_rc;
	if (rc == -DER_TRUNC) {
		struct dc_pool *pool = arg->dqa_pool;

		D_WARN("pool map buffer size (%ld) < required (%u)\n",
			pool_buf_size(map_buf->pb_nr), out->pqo_map_buf_size);

		/* retry with map buffer size required by server */
		D_INFO("retry with map buffer size required by server (%ul)\n",
		       out->pqo_map_buf_size);
		pool->dp_map_sz = out->pqo_map_buf_size;
		rc = tse_task_reinit(task);
		D_GOTO(out, rc);
	} else if (rc != 0) {
		D_ERROR("failed to query pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = process_query_reply(arg->dqa_pool, map_buf,
				 out->pqo_op.po_map_version,
				 out->pqo_op.po_hint.sh_rank,
				 &out->pqo_space, &out->pqo_rebuild_st,
				 arg->dqa_tgts, arg->dqa_info,
				 arg->dqa_prop, out->pqo_prop, false);
out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->dqa_pool);
	map_bulk_destroy(in->pqi_map_bulk, map_buf);
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
	struct dc_pool		       *pool;
	crt_endpoint_t			ep;
	crt_rpc_t		       *rpc;
	struct pool_query_in	       *in;
	struct pool_buf		       *map_buf;
	struct pool_query_arg		query_args;
	int				rc;

	args = dc_task_get_args(task);

	D_ASSERT(args->tgts == NULL); /* TODO */

	/** Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": querying: hdl="DF_UUID" tgts=%p info=%p\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		args->tgts, args->info);

	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(NULL /* label */, pool->dp_pool,
				     &pool->dp_client, &pool->dp_client_lock,
				     pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_QUERY, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool query rpc: %d\n",
			DP_UUID(pool->dp_pool), rc);
		D_GOTO(out_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pqi_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->pqi_op.pi_hdl, pool->dp_pool_hdl);
	in->pqi_query_bits = pool_query_bits(args->info, args->prop);

	/** +1 for args */
	crt_req_addref(rpc);

	rc = map_bulk_create(daos_task2ctx(task), &in->pqi_map_bulk, &map_buf,
			     pool_buf_nr(pool->dp_map_sz));
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	query_args.dqa_pool = pool;
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
	map_bulk_destroy(in->pqi_map_bulk, map_buf);
out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
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

/*
 * Arg and state of map_refresh
 *
 * mra_i is an index in the internal node array of a pool map. It is used to
 * perform a round robin of the array starting from a random element.
 */
struct map_refresh_arg {
	struct dc_pool	       *mra_pool;
	bool			mra_passive;
	unsigned int		mra_map_version;
	int			mra_i;
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

	n = pool_map_find_nodes(arg->mra_pool->dp_map, PO_COMP_ID_ALL, &nodes);
	/* There must be at least one rank. */
	D_ASSERTF(n > 0, "%d\n", n);

	if (arg->mra_i == -1) {
		/* Let i be a random integer in [0, n). */
		i = ((double)rand() / RAND_MAX) * n;
		if (i == n)
			i = 0;
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

	rc = pool_req_create(ctx, &ep, POOL_TGT_QUERY_MAP, &c);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create POOL_TGT_QUERY_MAP: %d\n",
			DP_UUID(pool->dp_pool), rc);
		return rc;
	}

	in = crt_req_get(c);
	uuid_copy(in->tmi_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->tmi_op.pi_hdl, pool->dp_pool_hdl);
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
	} else if (rc != 0) {
		D_ERROR(DF_UUID": failed to fetch pool map: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
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

	rc = dc_pool_map_update(pool, map, out->tmo_op.po_map_version, false /* connect */);

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

	if (!reinit) {
		D_ASSERTF(pool->dp_map_task == task, "%p == %p\n", pool->dp_map_task, task);
		tse_task_decref(pool->dp_map_task);
		pool->dp_map_task = NULL;
	}

	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	if (!reinit) {
		d_backoff_seq_fini(&arg->mra_backoff_seq);
		dc_pool_put(arg->mra_pool);
	}

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

	/* Update the highest known pool map version in all cases. */
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
		D_RWLOCK_UNLOCK(&pool->dp_map_lock);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to depend on active pool map "
				"refresh task: "DF_RC"\n", DP_UUID(pool->dp_pool), DP_RC(rc));
			goto out_task;
		}
		rc = tse_task_reinit(task);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to reinitialize task %p: "DF_RC"\n",
				DP_UUID(pool->dp_pool), task, DP_RC(rc));
			goto out_task;
		}
		goto out;
	}

	/* No active pool map refresh task; become one. */
	D_DEBUG(DB_MD, DF_UUID": %p: becoming active\n", DP_UUID(pool->dp_pool), task);
	tse_task_addref(task);
	pool->dp_map_task = task;

	rank = choose_map_refresh_rank(arg);

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
	return daos_rpc_send(rpc, task);

out_cb_arg:
	crt_req_decref(cb_arg.mrc_rpc);
	destroy_map_refresh_rpc(rpc, cb_arg.mrc_map_buf);
out_map_task:
	D_ASSERTF(pool->dp_map_task == task, "%p == %p\n", pool->dp_map_task, task);
	tse_task_decref(pool->dp_map_task);
	pool->dp_map_task = NULL;
out_task:
	d_backoff_seq_fini(&arg->mra_backoff_seq);
	dc_pool_put(arg->mra_pool);
	tse_task_complete(task, rc);
out:
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
 * \param[in]	pool		pool
 * \param[in]	map_version	known pool map version
 * \param[in]	sched		scheduler
 * \param[out]	task		pool map refresh task
 */
int
dc_pool_create_map_refresh_task(struct dc_pool *pool, uint32_t map_version,
				tse_sched_t *sched, tse_task_t **task)
{
	tse_task_t	       *t;
	struct map_refresh_arg *a;
	int			rc;

	rc = tse_task_create(map_refresh, sched, NULL /* priv */, &t);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool map refresh task: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		return rc;
	}

	a = tse_task_buf_embedded(t, sizeof(*a));
	dc_pool_get(pool);
	a->mra_pool = pool;
	a->mra_passive = false;
	a->mra_map_version = map_version;
	a->mra_i = -1;
	rc = d_backoff_seq_init(&a->mra_backoff_seq, 1 /* nzeros */, 4 /* factor */,
				16 /* next (us) */, 1 << 20 /* max (us) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));

	*task = t;
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
	tse_task_decref(task);
}

struct pool_lc_arg {
	crt_rpc_t			*rpc;
	struct dc_pool			*lca_pool;
	daos_size_t			 lca_req_ncont;
	daos_size_t			*lca_ncont;
	struct daos_pool_cont_info	*lca_cont_buf;
};

static int
pool_list_cont_cb(tse_task_t *task, void *data)
{
	struct pool_lc_arg		*arg = (struct pool_lc_arg *)data;
	struct pool_list_cont_in	*in = crt_req_get(arg->rpc);
	struct pool_list_cont_out	*out = crt_reply_get(arg->rpc);
	int				 rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->lca_pool, &arg->rpc->cr_ep, rc,
					   &out->plco_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	D_DEBUG(DF_DSMC, DF_UUID": list cont rpc done: %d\n",
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
	list_cont_bulk_destroy(in->plci_cont_bulk);
	return rc;
}

int
dc_pool_list_cont(tse_task_t *task)
{
	daos_pool_list_cont_t		*args;
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct pool_list_cont_in	*in;
	struct pool_lc_arg		 lc_cb_args;

	int				 rc;

	args = dc_task_get_args(task);

	/** Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": list containers: hdl="DF_UUID"\n",
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
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_LIST_CONT, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool list cont rpc: "
			DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		D_GOTO(out_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->plci_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->plci_op.pi_hdl, pool->dp_pool_hdl);
	/* If provided cont_buf is NULL, caller needs the number of containers
	 * to be returned in ncont. Set ncont=0 in the request in this case
	 * (caller value may be uninitialized).
	 */
	if (args->cont_buf == NULL)
		in->plci_ncont = 0;
	else
		in->plci_ncont = *args->ncont;
	in->plci_cont_bulk = CRT_BULK_NULL;

	D_DEBUG(DF_DSMC, "req_ncont="DF_U64" (cont_buf=%p, *ncont="DF_U64"\n",
			 in->plci_ncont, args->cont_buf,
			 *args->ncont);

	/** +1 for args */
	crt_req_addref(rpc);

	if ((*args->ncont > 0) && args->cont_buf) {
		rc = list_cont_bulk_create(daos_task2ctx(task),
					   &in->plci_cont_bulk,
					   args->cont_buf, in->plci_ncont);
		if (rc != 0)
			D_GOTO(out_rpc, rc);
	}

	lc_cb_args.lca_pool = pool;
	lc_cb_args.lca_ncont = args->ncont;
	lc_cb_args.lca_cont_buf = args->cont_buf;
	lc_cb_args.rpc = rpc;
	lc_cb_args.lca_req_ncont = in->plci_ncont;

	rc = tse_task_register_comp_cb(task, pool_list_cont_cb, &lc_cb_args,
				       sizeof(lc_cb_args));
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return daos_rpc_send(rpc, task);

out_bulk:
	if (in->plci_ncont > 0)
		list_cont_bulk_destroy(in->plci_cont_bulk);

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
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

	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		return -DER_NO_HDL;

	if (pool->dp_map == NULL) {
		dc_pool_put(pool);
		return -DER_NO_HDL;
	}

	*map_ver = dc_pool_get_version(pool);
	dc_pool_put(pool);

	return 0;
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
	int			      rc;

	arg = (struct pool_query_target_arg *)data;
	out = crt_reply_get(arg->rpc);
	rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->dqa_pool, &arg->rpc->cr_ep, rc,
					   &out->pqio_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	D_DEBUG(DF_DSMC, DF_UUID": target query rpc done: %d\n",
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

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->dqa_pool);
	return rc;
}

int
dc_pool_query_target(tse_task_t *task)
{
	daos_pool_query_target_t	*args;
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct pool_query_info_in	*in;
	struct pool_query_target_arg	 query_args;
	int				 rc;

	args = dc_task_get_args(task);

	/** Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": querying: hdl="DF_UUID" tgt=%d rank=%d\n",
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
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_QUERY_INFO, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool tgt info rpc: %d\n",
			DP_UUID(pool->dp_pool), rc);
		 goto out_pool;
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pqii_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->pqii_op.pi_hdl, pool->dp_pool_hdl);
	in->pqii_rank = args->rank;
	in->pqii_tgt = args->tgt_idx;

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
out_task:
	tse_task_complete(task, rc);
	return rc;
}

struct pool_req_arg {
	struct dc_pool	*pra_pool;
	crt_rpc_t	*pra_rpc;
	crt_bulk_t	 pra_bulk;
	tse_task_cb_t	 pra_callback;
};

enum preq_cleanup_stage {
	CLEANUP_ALL,
	CLEANUP_BULK,
	CLEANUP_RPC,
	CLEANUP_POOL,
};

static void
pool_req_cleanup(enum preq_cleanup_stage stage, struct pool_req_arg *args)
{
	switch (stage) {
	case CLEANUP_ALL:
		crt_req_decref(args->pra_rpc);
	case CLEANUP_BULK:
		if (args->pra_bulk)
			crt_bulk_free(args->pra_bulk);
	case CLEANUP_RPC:
		crt_req_decref(args->pra_rpc);
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
	int			 rc	 = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(pool, &args->pra_rpc->cr_ep,
					   rc, op_out, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	if (rc != 0) {
		D_ERROR("RPC error while querying pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = op_out->po_rc;
	if (rc != 0) {
		D_DEBUG(DF_DSMC, DF_UUID": failed to access pool: %d\n",
			DP_UUID(pool->dp_pool), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": Accessed: using hdl="DF_UUID"\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl));
	if (args->pra_callback != NULL)
		rc = args->pra_callback(task, data);
out:
	pool_req_cleanup(CLEANUP_BULK, args);
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
pool_req_prepare(daos_handle_t poh, enum pool_operation opcode,
		 crt_context_t *ctx, struct pool_req_arg *args)
{
	struct pool_op_in *in;
	crt_endpoint_t	   ep;
	int		   rc;

	args->pra_bulk = NULL;
	args->pra_callback = NULL;
	args->pra_pool = dc_hdl2pool(poh);
	if (args->pra_pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	ep.ep_grp  = args->pra_pool->dp_sys->sy_group;
	D_MUTEX_LOCK(&args->pra_pool->dp_client_lock);
	rc = rsvc_client_choose(&args->pra_pool->dp_client, &ep);
	D_MUTEX_UNLOCK(&args->pra_pool->dp_client_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(args->pra_pool->dp_pool), DP_RC(rc));
		pool_req_cleanup(CLEANUP_POOL, args);
		goto out;
	}

	rc = pool_req_create(ctx, &ep, opcode, &args->pra_rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		pool_req_cleanup(CLEANUP_POOL, args);
		D_GOTO(out, rc);
	}

	in = crt_req_get(args->pra_rpc);
	uuid_copy(in->pi_uuid, args->pra_pool->dp_pool);
	uuid_copy(in->pi_hdl, args->pra_pool->dp_pool_hdl);
out:
	return rc;
}

int
dc_pool_list_attr(tse_task_t *task)
{
	daos_pool_list_attr_t		*args;
	struct pool_attr_list_in	*in;
	struct pool_req_arg		 cb_args;
	int				 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->size == NULL ||
	    (*args->size > 0 && args->buf == NULL)) {
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pool_req_prepare(args->poh, POOL_ATTR_LIST,
			     daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_UUID": listing attributes: hdl="
			 DF_UUID "; size=%lu\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl), *args->size);

	in = crt_req_get(cb_args.pra_rpc);
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
		rc = crt_bulk_create(daos_task2ctx(task), &sgl,
				     CRT_BULK_RW, &in->pali_bulk);
		if (rc != 0) {
			pool_req_cleanup(CLEANUP_RPC, &cb_args);
			D_GOTO(out, rc);
		}
	}

	cb_args.pra_bulk = in->pali_bulk;
	cb_args.pra_callback = attr_list_req_complete;
	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to list pool attributes: "DF_RC"\n",
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
		D_ERROR("Invalid Arguments: n = %d, names = %p, values = %p"
			", sizes = %p", n, names, values, sizes);
		return -DER_INVAL;
	}

	for (i = 0; i < n; i++) {
		if (names[i] == NULL || *names[i] == '\0') {
			D_ERROR("Invalid Arguments: names[%d] = %s",
				i, names[i] == NULL ? "NULL" : "\'\\0\'");

			return -DER_INVAL;
		}
		if (strnlen(names[i], DAOS_ATTR_NAME_MAX + 1) > DAOS_ATTR_NAME_MAX) {
			D_ERROR("Invalid Arguments: names[%d] size > DAOS_ATTR_NAME_MAX",
				i);
			return -DER_INVAL;
		}
		if (sizes != NULL) {
			if (values == NULL)
				sizes[i] = 0;
			else if (values[i] == NULL || sizes[i] == 0) {
				if (!readonly) {
					D_ERROR("Invalid Arguments: values[%d] = %p, sizes[%d] = %lu",
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
	daos_pool_get_attr_t	*args;
	struct pool_attr_get_in	*in;
	struct pool_req_arg	 cb_args;
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

	rc = pool_req_prepare(args->poh, POOL_ATTR_GET,
			     daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_UUID": getting attributes: hdl="DF_UUID"\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl));

	in = crt_req_get(cb_args.pra_rpc);
	in->pagi_count = args->n;
	in->pagi_key_length = 0;

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		uint64_t len;

		len = strnlen(args->names[i], DAOS_ATTR_NAME_MAX);
		in->pagi_key_length += len + 1;
		D_STRNDUP(new_names[i], args->names[i], len);
		if (new_names[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, (void **)args->values,
			      (size_t *)args->sizes, daos_task2ctx(task),
			      CRT_BULK_RW, &in->pagi_bulk);
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_RPC, &cb_args);
		D_GOTO(out, rc);
	}

	cb_args.pra_bulk = in->pagi_bulk;
	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to get pool attributes: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_pool_set_attr(tse_task_t *task)
{
	daos_pool_set_attr_t	*args;
	struct pool_attr_set_in	*in;
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

	rc = pool_req_prepare(args->poh, POOL_ATTR_SET,
			     daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_UUID": setting attributes: hdl="DF_UUID"\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl));

	in = crt_req_get(cb_args.pra_rpc);
	in->pasi_count = args->n;

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		D_STRNDUP(new_names[i], args->names[i], DAOS_ATTR_NAME_MAX);
		if (new_names[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out, rc);
		}
	}

	/* no easy way to determine if a value storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * value in heap
	 */
	D_ALLOC_ARRAY(new_values, args->n);
	if (!new_values)
		D_GOTO(out, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_values,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_values);
		D_GOTO(out, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		D_ALLOC(new_values[i], args->sizes[i]);
		if (new_values[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		memcpy(new_values[i], args->values[i], args->sizes[i]);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_values[i], sizeof(void *));
		if (rc) {
			D_FREE(new_values[i]);
			D_GOTO(out, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, new_values,
			      (size_t *)args->sizes, daos_task2ctx(task),
			      CRT_BULK_RO, &in->pasi_bulk);
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_RPC, &cb_args);
		D_GOTO(out, rc);
	}

	cb_args.pra_bulk = in->pasi_bulk;
	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to set pool attributes: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_pool_del_attr(tse_task_t *task)
{
	daos_pool_del_attr_t	*args;
	struct pool_attr_del_in	*in;
	struct pool_req_arg	 cb_args;
	int			 i, rc;
	char			**new_names;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names, NULL, NULL, true);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = pool_req_prepare(args->poh, POOL_ATTR_DEL,
			      daos_task2ctx(task), &cb_args);
	if (rc != 0)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_UUID": deleting attributes: hdl="DF_UUID"\n",
		DP_UUID(cb_args.pra_pool->dp_pool_hdl),
		DP_UUID(cb_args.pra_pool->dp_pool_hdl));

	in = crt_req_get(cb_args.pra_rpc);
	in->padi_count = args->n;

	/* no easy way to determine if a name storage address is likely
	 * to cause an EFAULT during memory registration, so duplicate
	 * name in heap
	 */
	D_ALLOC_ARRAY(new_names, args->n);
	if (!new_names)
		D_GOTO(out, rc = -DER_NOMEM);
	rc = tse_task_register_comp_cb(task, free_heap_copy, &new_names,
				       sizeof(char *));
	if (rc) {
		D_FREE(new_names);
		D_GOTO(out, rc);
	}
	for (i = 0 ; i < args->n ; i++) {
		D_STRNDUP(new_names[i], args->names[i], DAOS_ATTR_NAME_MAX);
		if (new_names[i] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		rc = tse_task_register_comp_cb(task, free_heap_copy,
					       &new_names[i], sizeof(char *));
		if (rc) {
			D_FREE(new_names[i]);
			D_GOTO(out, rc);
		}
	}

	rc = attr_bulk_create(args->n, new_names, NULL, NULL,
			      daos_task2ctx(task), CRT_BULK_RO, &in->padi_bulk);
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_RPC, &cb_args);
		D_GOTO(out, rc);
	}

	cb_args.pra_bulk = in->padi_bulk;
	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_BULK, &cb_args);
		D_GOTO(out, rc);
	}

	crt_req_addref(cb_args.pra_rpc);
	return daos_rpc_send(cb_args.pra_rpc, task);

out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to del pool attributes: "DF_RC"\n", DP_RC(rc));
	return rc;
}

struct pool_svc_stop_arg {
	struct dc_pool	       *dsa_pool;
	crt_rpc_t	       *rpc;
};

static int
pool_svc_stop_cb(tse_task_t *task, void *data)
{
	struct pool_svc_stop_arg       *arg = (struct pool_svc_stop_arg *)data;
	struct pool_svc_stop_out       *out = crt_reply_get(arg->rpc);
	int				rc = task->dt_result;

	rc = pool_rsvc_client_complete_rpc(arg->dsa_pool, &arg->rpc->cr_ep, rc,
					   &out->pso_op, task);
	if (rc < 0)
		D_GOTO(out, rc);
	else if (rc == RSVC_CLIENT_RECHOOSE)
		D_GOTO(out, rc = 0);

	D_DEBUG(DF_DSMC, DF_UUID": stop rpc done: %d\n",
		DP_UUID(arg->dsa_pool->dp_pool), rc);

	if (rc != 0)
		D_GOTO(out, rc);

	rc = out->pso_op.po_rc;
	if (rc)
		D_GOTO(out, rc);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->dsa_pool);
	return rc;
}

int
dc_pool_stop_svc(tse_task_t *task)
{
	daos_pool_stop_svc_t	       *args;
	struct dc_pool		       *pool;
	crt_endpoint_t			ep;
	crt_rpc_t		       *rpc;
	struct pool_svc_stop_in	       *in;
	struct pool_svc_stop_arg	stop_args;
	int				rc;

	args = dc_task_get_args(task);
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": stopping svc: hdl="DF_UUID"\n",
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

	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_SVC_STOP, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create POOL_SVC_STOP RPC: %d\n",
			DP_UUID(pool->dp_pool), rc);
		goto out_pool;
	}

	in = crt_req_get(rpc);
	uuid_copy(in->psi_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->psi_op.pi_hdl, pool->dp_pool_hdl);

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
	tse_task_complete(task, rc);
	return rc;
}
