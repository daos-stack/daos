/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsmc: Pool Methods
 */

#include <daos_m.h>
#include <unistd.h>
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

static int
pool_connect_cp(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct dsmc_pool	*pool = (struct dsmc_pool *)sp->sp_arg;
	struct pool_connect_out	*pco;

	if (rc) {
		D_ERROR("RPC error while connecting to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pco = dtp_reply_get(sp->sp_rpc);
	rc = pco->pco_ret;
	if (rc) {
		D_ERROR("failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* add pool to hash */
	dsmc_pool_add_cache(pool, sp->sp_hdlp);
	D_DEBUG(DF_DSMC, DF_UUID": leave: hdl "DF_X64"\n",
		DP_UUID(pool->dp_pool), sp->sp_hdlp->cookie);

out:
	if (rc)
		dsmc_pool_put(pool);
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dsm_pool_connect(const uuid_t uuid, const char *grp,
		 const daos_rank_list_t *tgts, unsigned int flags,
		 daos_rank_list_t *failed, daos_handle_t *poh, daos_event_t *ev)
{
	dtp_endpoint_t		 ep;
	dtp_rpc_t		*rpc;
	struct pool_connect_in	*pci;
	struct dsmc_pool	*pool;
	struct daos_op_sp	*sp;
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
		D_GOTO(out_pool, rc);
	}

	/** fill in request buffer */
	pci = dtp_req_get(rpc);
	uuid_copy(pci->pci_pool, uuid);
	uuid_copy(pci->pci_pool_hdl, pool->dp_pool_hdl);
	pci->pci_uid = geteuid();
	pci->pci_gid = getegid();
	pci->pci_capas = flags;
	/* pci->pci_pool_map_bulk = NULL;  TODO */

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc); /** for scratchpad */
	sp->sp_rpc = rpc;
	sp->sp_hdlp = poh;
	sp->sp_arg = pool;

	/**
	 * mark event as in-flight, must be called before sending the request
	 * since it can race with the request callback execution
	 */
	rc = daos_event_launch(ev, NULL, pool_connect_cp);
	if (rc)
		D_GOTO(out_req, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, ev);

	return rc;

out_req:
	dtp_req_decref(rpc); /* scratchpad */
	dtp_req_decref(rpc); /* free req */
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
