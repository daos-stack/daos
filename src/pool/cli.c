/*
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

static void
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

	pool->dp_map_sz = pool_buf_size(nr);

	return pool;

failed:
	D_FREE(pool);
	return NULL;
}

/* Choose a pool service replica rank. If the rsvc module indicates
 * DER_NOTREPLICA, attempt to refresh the list by querying the MS.
 */
int
dc_pool_choose_svc_rank(const uuid_t puuid, struct rsvc_client *cli,
			pthread_mutex_t *cli_lock, struct dc_mgmt_sys *sys,
			crt_endpoint_t *ep)
{
	int			rc;
	int			i;

	if (cli_lock)
		D_MUTEX_LOCK(cli_lock);
choose:
	rc = rsvc_client_choose(cli, ep);
	if (rc == -DER_NOTREPLICA) {
		d_rank_list_t	*new_ranklist = NULL;

		/* Query MS for replica ranks. Not under client lock. */
		if (cli_lock)
			D_MUTEX_UNLOCK(cli_lock);
		rc = dc_mgmt_get_pool_svc_ranks(sys, puuid, &new_ranklist);
		if (rc) {
			D_ERROR(DF_UUID ": dc_mgmt_get_pool_svc_ranks() "
				"failed, " DF_RC "\n", DP_UUID(puuid),
				DP_RC(rc));
			return rc;
		}
		if (cli_lock)
			D_MUTEX_LOCK(cli_lock);

		/* Reinitialize rsvc client with new rank list, rechoose. */
		rsvc_client_fini(cli);
		rc = rsvc_client_init(cli, new_ranklist);
		d_rank_list_free(new_ranklist);
		new_ranklist = NULL;
		if (rc == 0) {
			for (i = 0; i < cli->sc_ranks->rl_nr; i++) {
				D_DEBUG(DF_DSMC, DF_UUID ": sc_ranks[%d]=%u\n",
					DP_UUID(puuid), i,
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
	pool->dp_ver = map_version;
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

	/* add pool to hhash */
	dc_pool_hdl_link(pool);
	dc_pool2hdl(pool, arg->hdlp);

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), arg->hdlp->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	crt_req_decref(arg->rpc);
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
	daos_iov_free(&pci->pci_cred);
	if (put_pool)
		dc_pool_put(pool);
	return rc;
}

int
dc_pool_update_map(daos_handle_t ph, struct pool_map *map)
{
	struct dc_pool	*pool;
	int		 rc;

	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	if (pool->dp_ver >= pool_map_get_version(map))
		D_GOTO(out, rc = 0); /* nothing to do */

	D_RWLOCK_WRLOCK(&pool->dp_map_lock);
	rc = dc_pool_map_update(pool, map, pool_map_get_version(map), false);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);
out:
	if (pool)
		dc_pool_put(pool);
	return rc;
}

int
dc_pool_connect(tse_task_t *task)
{
	daos_pool_connect_t	*args;
	struct dc_pool		*pool;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct pool_connect_in	*pci;
	struct pool_buf		*map_buf;
	struct pool_connect_arg	 con_args;
	int			 rc;

	args = dc_task_get_args(task);
	pool = dc_task_get_priv(task);

	if (pool == NULL) {
		if (!daos_uuid_valid(args->uuid) ||
		    !flags_are_valid(args->flags) || args->poh == NULL)
			D_GOTO(out_task, rc = -DER_INVAL);

		/** allocate and fill in pool connection */
		pool = dc_pool_alloc(DC_POOL_DEFAULT_COMPONENTS_NR);
		if (pool == NULL)
			D_GOTO(out_task, rc = -DER_NOMEM);
		uuid_copy(pool->dp_pool, args->uuid);
		uuid_generate(pool->dp_pool_hdl);
		pool->dp_capas = args->flags;

		/** attach to the server group and initialize rsvc_client */
		rc = dc_mgmt_sys_attach(args->grp, &pool->dp_sys);
		if (rc != 0)
			D_GOTO(out_pool, rc);

		/** Agent configuration data from pool->dp_sys->sy_info */
		/** sy_info.provider */
		/** sy_info.interface */
		/** sy_info.domain */
		/** sy_info.crt_ctx_share_addr */
		/** sy_info.crt_timeout */

		rc = rsvc_client_init(&pool->dp_client, args->svc);
		if (rc != 0)
			D_GOTO(out_pool, rc);

		daos_task_set_priv(task, pool);
		D_DEBUG(DF_DSMC, DF_UUID": connecting: hdl="DF_UUIDF
			" flags=%x\n", DP_UUID(args->uuid),
			DP_UUID(pool->dp_pool_hdl), args->flags);
	}

	/** Choose an endpoint and create an RPC. */
	ep.ep_grp = pool->dp_sys->sy_group;
	rc = dc_pool_choose_svc_rank(pool->dp_pool, &pool->dp_client,
				     &pool->dp_client_lock, pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_CONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_pool, rc);
	}
	/** for con_argss */
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

	uuid_copy(pci->pci_op.pi_uuid, args->uuid);
	uuid_copy(pci->pci_op.pi_hdl, pool->dp_pool_hdl);
	pci->pci_flags = args->flags;
	pci->pci_query_bits = pool_query_bits(args->info, NULL);

	rc = map_bulk_create(daos_task2ctx(task), &pci->pci_map_bulk, &map_buf,
			     pool_buf_nr(pool->dp_map_sz));
	if (rc != 0)
		D_GOTO(out_cred, rc);

	/** Prepare "con_args" for pool_connect_cp(). */
	con_args.pca_info = args->info;
	con_args.pca_map_buf = map_buf;
	con_args.rpc = rpc;
	con_args.hdlp = args->poh;

	rc = tse_task_register_comp_cb(task, pool_connect_cp, &con_args,
				       sizeof(con_args));
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return rc;

out_bulk:
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
out_cred:
	daos_iov_free(&pci->pci_cred);
out_req:
	crt_req_decref(rpc);
	crt_req_decref(rpc); /* free req */
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
	rc = dc_pool_choose_svc_rank(pool->dp_pool, &pool->dp_client,
				     &pool->dp_client_lock, pool->dp_sys, &ep);
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

	/** send the request */
	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return rc;

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
		D_SWAP16S(&pool_comp->co_type);
		/* skip pool_comp->co_status (uint8_t) */
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
	dc_pool_hdl_link(pool);
	dc_pool2hdl(pool, poh);

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

	if (in->pti_addr_list.ca_arrays)
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
		rc = rsvc_client_init(&state->client, args->svc);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to rsvc_client_init, rc %d.\n",
				DP_UUID(args->uuid), rc);
			D_GOTO(out_group, rc);
		}

		daos_task_set_priv(task, state);
	}

	ep.ep_grp = state->sys->sy_group;
	rc = dc_pool_choose_svc_rank(args->uuid, &state->client,
				     NULL /* mutex */, state->sys, &ep);
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
		crt_req_decref(rpc);
		D_GOTO(out_client, rc);
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
		D_GOTO(out_rpc, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, task);

	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return rc;

out_rpc:
	pool_target_addr_list_free(&list);
	crt_req_decref(rpc);
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
 * Query the latest pool information (i.e., mainly the pool map). This is meant
 * to be an "uneventful" interface; callers wishing to play with events shall
 * do so with \a cb and \a cb_arg.
 *
 * \param[in]	pool	pool handle object
 * \param[in]	ctx	RPC context
 * \param[out]	tgts	if not NULL, pool target ranks returned on success
 * \param[in,out]
 *		info	if not NULL, pool information returned on success
 * \param[in]	cb	callback called only on success
 * \param[in]	cb_arg	argument passed to \a cb
 * \return		zero or error
 *
 * TODO: Avoid redundant POOL_QUERY RPCs triggered by multiple
 * threads/operations.
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
	rc = dc_pool_choose_svc_rank(pool->dp_pool, &pool->dp_client,
				     &pool->dp_client_lock, pool->dp_sys, &ep);
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

	/** send the request */
	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return rc;

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
	struct pool_lc_arg		*arg = (struct pool_lc_arg *) data;
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
	rc = dc_pool_choose_svc_rank(pool->dp_pool, &pool->dp_client,
				     &pool->dp_client_lock, pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_LIST_CONT, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool list cont rpc: "
			DF_RC "\n",
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

	/** send the request */
	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return rc;
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

static int
pool_evict_cp(tse_task_t *task, void *data)
{
	struct pool_evict_state	*state = dc_task_get_priv(task);
	crt_rpc_t		*rpc = *((crt_rpc_t **)data);
	struct pool_evict_in	*in = crt_req_get(rpc);
	struct pool_evict_out	*out = crt_reply_get(rpc);
	bool			 free_state = true;
	int			 rc = task->dt_result;

	rc = rsvc_client_complete_rpc(&state->client, &rpc->cr_ep, rc,
				      out->pvo_op.po_rc, &out->pvo_op.po_hint);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED &&
	     daos_rpc_retryable_rc(out->pvo_op.po_rc))) {
		rc = tse_task_reinit(task);
		if (rc != 0)
			D_GOTO(out, rc);
		free_state = false;
		D_GOTO(out, rc = 0);
	}

	if (rc != 0) {
		D_ERROR("RPC error while evicting pool handles: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = out->pvo_op.po_rc;
	if (rc != 0) {
		D_ERROR("failed to evict pool handles: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": evicted\n", DP_UUID(in->pvi_op.pi_uuid));

out:
	crt_req_decref(rpc);
	if (free_state) {
		rsvc_client_fini(&state->client);
		dc_mgmt_sys_detach(state->sys);
		D_FREE(state);
	}
	return rc;
}

int
dc_pool_evict(tse_task_t *task)
{
	struct pool_evict_state	*state;
	crt_endpoint_t		 ep;
	daos_pool_evict_t	*args;
	crt_rpc_t		*rpc;
	struct pool_evict_in	*in;
	int			 rc;

	args = dc_task_get_args(task);
	state = dc_task_get_priv(task);

	if (state == NULL) {
		if (!daos_uuid_valid(args->uuid))
			D_GOTO(out_task, rc = -DER_INVAL);

		D_DEBUG(DF_DSMC, DF_UUID": evicting\n", DP_UUID(args->uuid));

		D_ALLOC_PTR(state);
		if (state == NULL) {
			D_GOTO(out_task, rc = -DER_NOMEM);
		}

		rc = dc_mgmt_sys_attach(args->grp, &state->sys);
		if (rc != 0)
			D_GOTO(out_state, rc);
		rc = rsvc_client_init(&state->client, args->svc);
		if (rc != 0)
			D_GOTO(out_group, rc);

		daos_task_set_priv(task, state);
	}

	ep.ep_grp = state->sys->sy_group;
	rc = dc_pool_choose_svc_rank(args->uuid, &state->client,
				     NULL /* mutex */, state->sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(args->uuid), DP_RC(rc));
		goto out_client;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_EVICT, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool evict rpc: %d\n",
			DP_UUID(args->uuid), rc);
		D_GOTO(out_client, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pvi_op.pi_uuid, args->uuid);

	crt_req_addref(rpc);

	rc = tse_task_register_comp_cb(task, pool_evict_cp, &rpc, sizeof(rpc));
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return rc;

out_rpc:
	crt_req_decref(rpc);
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

	/** TODO Return pool target space usage and other tgt info */
	arg->dqa_info->ta_state = out->pqio_state;

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
		D_GOTO(out_pool, rc);
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

	/** send the request */
	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return rc;

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
	rc = daos_rpc_send(cb_args.pra_rpc, task);
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_ALL, &cb_args);
		D_GOTO(out, rc);
	}

	return rc;
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
		if (names[i] == NULL || *(names[i]) == '\0') {
			D_ERROR("Invalid Arguments: names[%d] = %s",
				i, names[i] == NULL ? "NULL" : "\'\\0\'");

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

int
dc_pool_get_attr(tse_task_t *task)
{
	daos_pool_get_attr_t	*args;
	struct pool_attr_get_in	*in;
	struct pool_req_arg	 cb_args;
	int			 rc;
	int			 i;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	rc = attr_check_input(args->n, args->names,
			      (const void *const*) args->values,
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
	for (i = 0, in->pagi_key_length = 0; i < args->n; i++)
		in->pagi_key_length += strlen(args->names[i]) + 1;

	rc = attr_bulk_create(args->n, (char **)args->names,
			      (void **)args->values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RW, &in->pagi_bulk);
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
	rc = daos_rpc_send(cb_args.pra_rpc, task);
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_ALL, &cb_args);
		D_GOTO(out, rc);
	}

	return rc;
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
	int			 rc;

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
	rc = attr_bulk_create(args->n, (char **)args->names,
			      (void **)args->values, (size_t *)args->sizes,
			      daos_task2ctx(task), CRT_BULK_RO, &in->pasi_bulk);
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
	rc = daos_rpc_send(cb_args.pra_rpc, task);
	if (rc != 0) {
		pool_req_cleanup(CLEANUP_ALL, &cb_args);
		D_GOTO(out, rc);
	}

	return rc;
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
	int			 rc;

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
	rc = attr_bulk_create(args->n, (char **)args->names, NULL, NULL,
			      daos_task2ctx(task), CRT_BULK_RO, &in->padi_bulk);
	if (rc != 0)
		D_GOTO(cleanup, rc);

	cb_args.pra_bulk = in->padi_bulk;
	rc = tse_task_register_comp_cb(task, pool_req_complete,
				       &cb_args, sizeof(cb_args));
	if (rc != 0)
		D_GOTO(cleanup, rc);

	crt_req_addref(cb_args.pra_rpc);
	rc = daos_rpc_send(cb_args.pra_rpc, task);
	if (rc != 0)
		D_GOTO(cleanup, rc);

	return rc;
cleanup:
	pool_req_cleanup(CLEANUP_BULK, &cb_args);
out:
	tse_task_complete(task, rc);
	D_DEBUG(DF_DSMC, "Failed to del pool attributes: "DF_RC"\n", DP_RC(rc));
	return rc;
}

int
dc_pool_extend(tse_task_t *task)
{
	return -DER_NOSYS;
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
	rc = dc_pool_choose_svc_rank(pool->dp_pool, &pool->dp_client,
				     &pool->dp_client_lock, pool->dp_sys, &ep);
	if (rc != 0) {
		D_ERROR(DF_UUID": cannot find pool service: "DF_RC"\n",
			DP_UUID(pool->dp_pool), DP_RC(rc));
		goto out_pool;
	}
	rc = pool_req_create(daos_task2ctx(task), &ep, POOL_SVC_STOP, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create POOL_SVC_STOP RPC: %d\n",
			DP_UUID(pool->dp_pool), rc);
		D_GOTO(out_pool, rc);
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

	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return rc;

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
out_task:
	tse_task_complete(task, rc);
	return rc;
}
