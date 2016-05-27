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
 * dsmc: Pool Methods
 */

#include <daos_m.h>
#include <unistd.h>
#include <daos/pool_map.h>
#include "dsm_rpc.h"
#include "dsmc_internal.h"

static inline int
flags_are_valid(unsigned int flags)
{
	unsigned int mode = flags & (DAOS_PC_RO | DAOS_PC_RW | DAOS_PC_EX);

	return (mode = DAOS_PC_RO) || (mode = DAOS_PC_RW) ||
	       (mode = DAOS_PC_EX);
}

static void
pool_free(struct daos_hlink *hlink)
{
	struct dsmc_pool *pool;

	pool = container_of(hlink, struct dsmc_pool, dp_hlink);
	pthread_rwlock_destroy(&pool->dp_co_list_lock);
	D_ASSERT(daos_list_empty(&pool->dp_co_list));
	if (pool->dp_map != NULL)
		pool_map_destroy(pool->dp_map);
	if (pool->dp_map_buf != NULL)
		pool_buf_free(pool->dp_map_buf);
	D_FREE_PTR(pool);
}

static struct daos_hlink_ops pool_h_ops = {
	.hop_free = pool_free,
};

static struct dsmc_pool *
pool_alloc(void)
{
	struct dsmc_pool *pool;

	/** allocate and fill in pool connection */
	D_ALLOC_PTR(pool);
	if (pool == NULL) {
		D_ERROR("failed to allocate pool connection\n");
		return NULL;
	}

	DAOS_INIT_LIST_HEAD(&pool->dp_co_list);
	pthread_rwlock_init(&pool->dp_co_list_lock, NULL);
	daos_hhash_hlink_init(&pool->dp_hlink, &pool_h_ops);

	return pool;
}

struct pool_connect_arg {
	struct dsmc_pool       *pca_pool;
	daos_pool_info_t       *pca_info;
	struct pool_buf	       *pca_map_buf;
};

static int
pool_connect_cp(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct pool_connect_arg	*arg = sp->sp_arg;
	struct dsmc_pool	*pool = arg->pca_pool;
	daos_pool_info_t	*info = arg->pca_info;
	struct pool_buf		*map_buf = arg->pca_map_buf;
	struct pool_connect_in	*pci = dtp_req_get(sp->sp_rpc);
	struct pool_connect_out	*pco = dtp_reply_get(sp->sp_rpc);

	if (rc == -DER_TRUNC) {
		/* TODO: Reallocate a larger buffer and reconnect. */
		D_ERROR("pool map buffer (%ld) < required (%u)\n",
			pool_buf_size(map_buf->pb_nr),
			pco->pco_pool_map_buf_size);
		D_GOTO(out, rc);
	}

	if (rc) {
		D_ERROR("RPC error while connecting to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = pco->pco_ret;
	if (rc) {
		D_ERROR("failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = pool_map_create(map_buf, pco->pco_pool_map_version, &pool->dp_map);
	if (rc != 0) {
		/* TODO: What do we do about the remote connection state? */
		D_ERROR("failed to create local pool map: %d\n", rc);
		D_GOTO(out, rc);
	}

	pool->dp_map_buf = map_buf;

	/* add pool to hash */
	dsmc_pool_add_cache(pool, sp->sp_hdlp);

	if (info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(info->pi_uuid, pool->dp_pool);
	info->pi_ntargets = map_buf->pb_target_nr;
	info->pi_ndisabled = 0;
	info->pi_mode = pco->pco_mode;
	info->pi_space.foo = 0;

out:
	D_DEBUG(DF_DSMC, DF_UUID": leave: hdl "DF_X64"\n",
		DP_UUID(pool->dp_pool), rc == 0 ? sp->sp_hdlp->cookie : 0);
	dtp_bulk_free(pci->pci_pool_map_bulk);
	dtp_req_decref(sp->sp_rpc);
	D_FREE_PTR(arg);
	if (rc)
		pool_buf_free(map_buf);
	dsmc_pool_put(pool);
	return rc;
}

int
dsm_pool_connect(const uuid_t uuid, const char *grp,
		 const daos_rank_list_t *tgts, unsigned int flags,
		 daos_rank_list_t *failed, daos_handle_t *poh,
		 daos_pool_info_t *info, daos_event_t *ev)
{
	dtp_endpoint_t		 ep;
	dtp_rpc_t		*rpc;
	struct pool_connect_in	*pci;
	struct dsmc_pool	*pool;
	struct daos_op_sp	*sp;
	struct pool_connect_arg	*arg;
	struct pool_buf		*map_buf;
	daos_iov_t		 map_iov;
	daos_sg_list_t		 map_sgl;
	int			 rc;

	/* TODO: Implement these. */
	D_ASSERT(grp == NULL);
	/* D_ASSERT(tgts == NULL); */
	D_ASSERT(failed == NULL);

	if (uuid_is_null(uuid) || !flags_are_valid(flags) || poh == NULL)
		return -DER_INVAL;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	D_DEBUG(DF_DSMC, DF_UUID": enter: flags %x\n", DP_UUID(uuid), flags);

	/** allocate and fill in pool connection */
	pool = pool_alloc();
	if (pool == NULL)
		return -DER_NOMEM;

	uuid_copy(pool->dp_pool, uuid);
	uuid_generate(pool->dp_pool_hdl);
	pool->dp_capas = flags;

	/* Prepare "map_sgl" for dtp_bulk_create(). */
	map_buf = pool_buf_alloc(128);
	if (map_buf == NULL)
		D_GOTO(out_pool, rc = -DER_NOMEM);

	map_iov.iov_buf = map_buf;
	map_iov.iov_buf_len = pool_buf_size(map_buf->pb_nr);
	map_iov.iov_len = 0;
	map_sgl.sg_nr.num = 1;
	map_sgl.sg_nr.num_out = 0;
	map_sgl.sg_iovs = &map_iov;

	/* Prepare "arg" for pool_connect_cp(). */
	D_ALLOC_PTR(arg);
	if (arg == NULL)
		D_GOTO(out_map_buf, rc = -DER_NOMEM);

	arg->pca_pool = pool;
	arg->pca_info = info;
	arg->pca_map_buf = map_buf;

	/*
	 * Currently, rank 0 runs the pool and the (only) container service.
	 * ep.ep_grp_id and ep.ep_tag are not used at the moment.
	 */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_POOL_CONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(out_arg, rc);
	}

	/** fill in request buffer */
	pci = dtp_req_get(rpc);
	uuid_copy(pci->pci_pool, uuid);
	uuid_copy(pci->pci_pool_hdl, pool->dp_pool_hdl);
	pci->pci_uid = geteuid();
	pci->pci_gid = getegid();
	pci->pci_capas = flags;

	rc = dtp_bulk_create(daos_ev2ctx(ev), &map_sgl, DTP_BULK_RW,
			     &pci->pci_pool_map_bulk);
	if (rc != 0)
		D_GOTO(out_req, rc);

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc); /** for scratchpad */
	sp->sp_rpc = rpc;
	sp->sp_hdlp = poh;
	sp->sp_arg = arg;

	/**
	 * mark event as in-flight, must be called before sending the request
	 * since it can race with the request callback execution
	 */
	rc = daos_event_launch(ev, NULL, pool_connect_cp);
	if (rc)
		D_GOTO(out_bulk, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, ev);

	return rc;

out_bulk:
	dtp_bulk_free(pci->pci_pool_map_bulk);
out_req:
	dtp_req_decref(rpc); /* scratchpad */
	dtp_req_decref(rpc); /* free req */
out_arg:
	D_FREE_PTR(arg);
out_map_buf:
	pool_buf_free(map_buf);
out_pool:
	dsmc_pool_put(pool);
	return rc;
}

static int
pool_disconnect_cp(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct dsmc_pool		*pool = (struct dsmc_pool *)sp->sp_arg;
	struct pool_disconnect_out	*pdo;

	if (rc) {
		D_ERROR("RPC error while disconnecting from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pdo = dtp_reply_get(sp->sp_rpc);
	rc = pdo->pdo_ret;
	if (rc) {
		D_ERROR("failed to disconnect from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": leave: hdl "DF_X64"\n",
		DP_UUID(pool->dp_pool), sp->sp_hdl.cookie);
	dsmc_pool_del_cache(pool);
	sp->sp_hdl.cookie = 0;
out:
	dtp_req_decref(sp->sp_rpc);
	dsmc_pool_put(pool);
	return rc;
}

int
dsm_pool_disconnect(daos_handle_t poh, daos_event_t *ev)
{
	struct dsmc_pool		*pool;
	dtp_endpoint_t			 ep;
	dtp_rpc_t			*rpc;
	struct pool_disconnect_in	*pdi;
	struct daos_op_sp		*sp;
	int				 rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	pool = dsmc_handle2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	D_DEBUG(DF_DSMC, DF_UUID": enter: hdl "DF_X64"\n",
		DP_UUID(pool->dp_pool), poh.cookie);

	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	/* Let's remove it from the cache no matter if disconnect
	 * succeeds to avoid others accessing the pool from the
	 * cache at the same time. */
	pthread_rwlock_rdlock(&pool->dp_co_list_lock);
	if (!daos_list_empty(&pool->dp_co_list)) {
		pthread_rwlock_unlock(&pool->dp_co_list_lock);
		dsmc_pool_put(pool);
		return -DER_BUSY;
	}
	pool->dp_disconnecting = 1;
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_POOL_DISCONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		dsmc_pool_put(pool);
		return rc;
	}

	/** fill in request buffer */
	pdi = dtp_req_get(rpc);
	D_ASSERT(pdi != NULL);
	uuid_copy(pdi->pdi_pool, pool->dp_pool);
	uuid_copy(pdi->pdi_pool_hdl, pool->dp_pool_hdl);

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc); /** for scratchpad */
	sp->sp_rpc = rpc;
	sp->sp_hdl = poh;
	sp->sp_arg = pool;

	/** mark event as in-flight */
	rc = daos_event_launch(ev, NULL, pool_disconnect_cp);
	if (rc) {
		dtp_req_decref(rpc); /* scratchpad */
		dtp_req_decref(rpc); /* free req */
		dsmc_pool_put(pool);
		return rc;
	}

	/** send the request */
	rc = daos_rpc_send(rpc, ev);
	return rc;
}
