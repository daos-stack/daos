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
pool_connect_cp(void *data, daos_event_t *ev, int rc)
{
	struct daos_op_sp *sp = data;
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

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), sp->sp_hdlp->cookie,
		DP_UUID(pool->dp_pool_hdl));

	if (info == NULL)
		D_GOTO(out, rc = 0);

	uuid_copy(info->pi_uuid, pool->dp_pool);
	info->pi_ntargets = map_buf->pb_target_nr;
	info->pi_ndisabled = 0;
	info->pi_mode = pco->pco_mode;
	info->pi_space.foo = 0;

out:
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

	/** allocate and fill in pool connection */
	pool = pool_alloc();
	if (pool == NULL)
		return -DER_NOMEM;

	uuid_copy(pool->dp_pool, uuid);
	uuid_generate(pool->dp_pool_hdl);
	pool->dp_capas = flags;

	D_DEBUG(DF_DSMC, DF_UUID": connecting: hdl="DF_UUIDF" flags=%x\n",
		DP_UUID(uuid), DP_UUID(pool->dp_pool_hdl), flags);

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
	ep.ep_grp = NULL;
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

	rc = daos_event_register_comp_cb(ev, pool_connect_cp, sp);
	if (rc != 0)
		D_GOTO(out_bulk, rc);
	/**
	 * mark event as in-flight, must be called before sending the request
	 * since it can race with the request callback execution
	 */
	rc = daos_event_launch(ev);
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
pool_disconnect_cp(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp		*sp = arg;
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

	D_DEBUG(DF_DSMC, DF_UUID": disconnected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), sp->sp_hdl.cookie,
		DP_UUID(pool->dp_pool_hdl));

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
	int				 rc = 0;

	pool = dsmc_handle2pool(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	D_DEBUG(DF_DSMC, DF_UUID": disconnecting: hdl="DF_UUID" cookie="DF_X64
		"\n", DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		poh.cookie);

	pthread_rwlock_rdlock(&pool->dp_co_list_lock);
	if (!daos_list_empty(&pool->dp_co_list)) {
		pthread_rwlock_unlock(&pool->dp_co_list_lock);
		dsmc_pool_put(pool);
		return -DER_BUSY;
	}
	pool->dp_disconnecting = 1;
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

	if (pool->dp_slave) {
		D_DEBUG(DF_DSMC, DF_UUID": disconnecting: cookie="DF_X64" hdl="
			DF_UUID" slave\n", DP_UUID(pool->dp_pool), poh.cookie,
			DP_UUID(pool->dp_pool_hdl));
		dsmc_pool_del_cache(pool);
		poh.cookie = 0;
		dsmc_pool_put(pool);
		if (ev != NULL) {
			daos_event_launch(ev);
			daos_event_complete(ev, rc);
		}
		return rc;
	}

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}
	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;
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

	rc = daos_event_register_comp_cb(ev, pool_disconnect_cp, sp);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	/** mark event as in-flight */
	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, ev);
	return rc;
out_put_req:
	dtp_req_decref(rpc); /* scratchpad */
	dtp_req_decref(rpc); /* free req */
	dsmc_pool_put(pool);
	return rc;

}

static inline void
dsmc_swap_pool_buf(struct pool_buf *pb)
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
dsmc_swap_pool_glob(struct dsmc_pool_glob *pool_glob)
{
	D_ASSERT(pool_glob != NULL);

	D_SWAP32S(&pool_glob->dpg_header.hgh_magic);
	D_SWAP32S(&pool_glob->dpg_header.hgh_type);
	/* skip pool_glob->dpg_pool (uuid_t) */
	/* skip pool_glob->dpg_pool_hdl (uuid_t) */
	D_SWAP64S(&pool_glob->dpg_capas);
	D_SWAP32S(&pool_glob->dpg_map_version);
	D_SWAP32S(&pool_glob->dpg_map_pb_nr);
	dsmc_swap_pool_buf(pool_glob->dpg_map_buf);
}

static int
dsmc_pool_l2g(daos_handle_t poh, daos_iov_t *glob)
{
	struct dsmc_pool	*pool;
	struct dsmc_pool_glob	*pool_glob;
	daos_size_t		 glob_buf_size;
	uint32_t		 pb_nr;
	int			 rc = 0;

	D_ASSERT(glob != NULL);

	pool = dsmc_handle2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	pb_nr = pool->dp_map_buf->pb_nr;
	glob_buf_size = dsmc_pool_glob_buf_size(pb_nr);
	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_pool, rc = 0);
	}
	if (glob->iov_buf_len < glob_buf_size) {
		D_ERROR("Larger glob buffer needed ("DF_U64" bytes provided, "
			""DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_pool, rc = -DER_TRUNC);
	}
	glob->iov_len = glob_buf_size;

	/* TODO: possible pool map changing during the l2g? */

	/* init pool global handle */
	pool_glob = (struct dsmc_pool_glob *)glob->iov_buf;
	dsmc_hdl_glob_hdr_init(&pool_glob->dpg_header, DSMC_GLOB_POOL);
	uuid_copy(pool_glob->dpg_pool, pool->dp_pool);
	uuid_copy(pool_glob->dpg_pool_hdl, pool->dp_pool_hdl);
	pool_glob->dpg_capas = pool->dp_capas;
	pool_glob->dpg_map_version = pool_map_get_version(pool->dp_map);
	pool_glob->dpg_map_pb_nr = pb_nr;
	memcpy(pool_glob->dpg_map_buf, pool->dp_map_buf, pool_buf_size(pb_nr));

out_pool:
	dsmc_pool_put(pool);
out:
	if (rc != 0)
		D_ERROR("dsmc_pool_l2g failed, rc: %d\n", rc);
	return rc;
}

int
dsm_pool_local2global(daos_handle_t poh, daos_iov_t *glob)
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
	if (dsmc_handle_type(poh) != DAOS_HTYPE_POOL) {
		D_DEBUG(DF_DSMC, "Bad type (%d) of poh handle.\n",
			dsmc_handle_type(poh));
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dsmc_pool_l2g(poh, glob);

out:
	return rc;
}

static int
dsmc_pool_g2l(struct dsmc_pool_glob *pool_glob, daos_handle_t *poh)
{
	struct dsmc_pool	*pool;
	struct pool_buf		*map_buf;
	int			rc = 0;

	D_ASSERT(pool_glob != NULL);
	D_ASSERT(poh != NULL);
	map_buf = pool_glob->dpg_map_buf;
	D_ASSERT(map_buf != NULL);

	/** allocate and fill in pool connection */
	pool = pool_alloc();
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_copy(pool->dp_pool, pool_glob->dpg_pool);
	uuid_copy(pool->dp_pool_hdl, pool_glob->dpg_pool_hdl);
	pool->dp_capas = pool_glob->dpg_capas;
	/* set slave flag to avoid export it again */
	pool->dp_slave = 1;

	rc = pool_map_create(map_buf, pool_glob->dpg_map_version,
			     &pool->dp_map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: %d\n", rc);
		D_GOTO(out, rc);
	}
	pool->dp_map_buf = pool_buf_dup(map_buf);
	if (pool->dp_map_buf == NULL) {
		D_ERROR("pool_buf_dup failed.\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	/* add pool to hash */
	dsmc_pool_add_cache(pool, poh);

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" slave\n", DP_UUID(pool->dp_pool), poh->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	if (rc != 0) {
		D_ERROR("dsmc_pool_g2l failed, rc: %d.\n", rc);
		/* in error case have not set pool->dp_map_buf */
		pool_buf_free(map_buf);
	}
	if (pool != NULL)
		dsmc_pool_put(pool);
	return rc;
}

int
dsm_pool_global2local(daos_iov_t glob, daos_handle_t *poh)
{
	struct dsmc_pool_glob	 *pool_glob;
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

	pool_glob = (struct dsmc_pool_glob *)glob.iov_buf;
	if (pool_glob->dpg_header.hgh_magic == D_SWAP32(DSM_GLOB_HDL_MAGIC)) {
		dsmc_swap_pool_glob(pool_glob);
		D_ASSERT(pool_glob->dpg_header.hgh_magic == DSM_GLOB_HDL_MAGIC);
	} else if (pool_glob->dpg_header.hgh_magic != DSM_GLOB_HDL_MAGIC) {
		D_ERROR("Bad hgh_magic: 0x%x.\n",
			pool_glob->dpg_header.hgh_magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (pool_glob->dpg_header.hgh_type != DSMC_GLOB_POOL) {
		D_ERROR("Bad hgh_type: %d.\n", pool_glob->dpg_header.hgh_type);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dsmc_pool_g2l(pool_glob, poh);
	if (rc != 0)
		D_ERROR("dsmc_pool_g2l failed, rc: %d.\n", rc);

out:
	return rc;
}
