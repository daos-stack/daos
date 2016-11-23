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
 * dc_pool: Pool Client
 *
 * This module is part of libdaos. It implements the pool methods of DAOS API
 * as well as daos/pool.h.
 */

#include <daos_types.h>
#include <daos/pool.h>
#include <daos/placement.h>

#include <daos/rpc.h>
#include "cli_internal.h"
#include "rpc.h"

/**
 * Initialize pool interface
 */
int
dc_pool_init(void)
{
	int rc;

	rc = daos_rpc_register(pool_rpcs, NULL, DAOS_POOL_MODULE);
	if (rc != 0)
		D_ERROR("failed to register pool RPCs: %d\n", rc);

	return rc;
}

/**
 * Finalize pool interface
 */
void
dc_pool_fini(void)
{
	daos_rpc_unregister(pool_rpcs);
}

static void
pool_free(struct daos_hlink *hlink)
{
	struct dc_pool *pool;

	pool = container_of(hlink, struct dc_pool, dp_hlink);
	pthread_rwlock_destroy(&pool->dp_map_lock);
	pthread_rwlock_destroy(&pool->dp_co_list_lock);
	D_ASSERT(daos_list_empty(&pool->dp_co_list));
	if (pool->dp_map != NULL)
		pool_map_destroy(pool->dp_map);
	D_FREE_PTR(pool);
}

static struct daos_hlink_ops pool_h_ops = {
	.hop_free = pool_free,
};

static inline int
flags_are_valid(unsigned int flags)
{
	unsigned int mode = flags & (DAOS_PC_RO | DAOS_PC_RW | DAOS_PC_EX);

	return (mode = DAOS_PC_RO) || (mode = DAOS_PC_RW) ||
	       (mode = DAOS_PC_EX);
}

static struct dc_pool *
pool_alloc(void)
{
	struct dc_pool *pool;

	/** allocate and fill in pool connection */
	D_ALLOC_PTR(pool);
	if (pool == NULL) {
		D_ERROR("failed to allocate pool connection\n");
		return NULL;
	}

	DAOS_INIT_LIST_HEAD(&pool->dp_co_list);
	pthread_rwlock_init(&pool->dp_co_list_lock, NULL);
	pthread_rwlock_init(&pool->dp_map_lock, NULL);
	daos_hhash_hlink_init(&pool->dp_hlink, &pool_h_ops);

	return pool;
}

static int
map_bulk_create(crt_context_t ctx, crt_bulk_t *bulk, struct pool_buf **buf)
{
	daos_iov_t	iov;
	daos_sg_list_t	sgl;
	int		rc;

	/* Use a fixed-size pool buffer until DER_TRUNCs are handled. */
	*buf = pool_buf_alloc(128);
	if (*buf == NULL)
		return -DER_NOMEM;

	daos_iov_set(&iov, *buf, pool_buf_size((*buf)->pb_nr));
	sgl.sg_nr.num = 1;
	sgl.sg_nr.num_out = 0;
	sgl.sg_iovs = &iov;

	rc = crt_bulk_create(ctx, daos2crt_sg(&sgl), CRT_BULK_RW, bulk);
	if (rc != 0) {
		pool_buf_free(*buf);
		*buf = NULL;
	}

	return rc;
}

static void
map_bulk_destroy(crt_bulk_t bulk, struct pool_buf *buf)
{
	crt_bulk_free(bulk);
	pool_buf_free(buf);
}

/*
 * Using "map_buf", "map_version", and "mode", update "pool->dp_map" and fill
 * "tgts" and/or "info" if not NULL.
 */
static int
process_query_reply(struct dc_pool *pool, struct pool_buf *map_buf,
		    uint32_t map_version, uint32_t mode, daos_rank_list_t *tgts,
		    daos_pool_info_t *info)
{
	struct pool_map	       *map;
	struct pool_map	       *map_tmp;
	int			rc;

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: %d\n", rc);
		return rc;
	}

	pthread_rwlock_wrlock(&pool->dp_map_lock);

	if (pool->dp_map == NULL ||
	    map_version > pool_map_get_version(pool->dp_map)) {
		D_DEBUG(DF_DSMC, DF_UUID": updating pool map: %u -> %u\n",
			DP_UUID(pool->dp_pool),
			pool->dp_map == NULL ?
			0 : pool_map_get_version(pool->dp_map), map_version);
		map_tmp = pool->dp_map;
		if (map_tmp != NULL)
			daos_placement_fini(map_tmp);

		rc = daos_placement_init(map);
		if (rc != 0)
			D_GOTO(out_unlock, rc);
		pool->dp_map = map;
		map = map_tmp;
	} else if (map_version < pool_map_get_version(pool->dp_map)) {
		D_DEBUG(DF_DSMC, DF_UUID": received older pool map: %u -> %u\n",
			DP_UUID(pool->dp_pool),
			pool_map_get_version(pool->dp_map), map_version);
	}

	if (map != NULL)
		pool_map_destroy(map);

	/* Scan all targets for info->pi_ndisabled and/or tgts. */
	if (info != NULL || tgts != NULL) {
		struct pool_target     *ts;
		int			i;

		if (info != NULL)
			memset(info, 0, sizeof(*info));

		rc = pool_map_find_target(pool->dp_map, PO_COMP_ID_ALL, &ts);
		D_ASSERTF(rc > 0, "%d\n", rc);
		for (i = 0; i < rc; i++) {
			int status = ts[i].ta_comp.co_status;

			if (info != NULL &&
			    (status == PO_COMP_ST_DOWN ||
			     status == PO_COMP_ST_DOWNOUT))
				info->pi_ndisabled++;
			/* TODO: Take care of tgts. */
		}
		rc = 0;
	}

out_unlock:
	pthread_rwlock_unlock(&pool->dp_map_lock);

	if (info != NULL && rc == 0) {
		uuid_copy(info->pi_uuid, pool->dp_pool);
		info->pi_ntargets = map_buf->pb_target_nr;
		info->pi_mode = mode;
	}

	return rc;
}

struct pool_connect_arg {
	struct dc_pool	       *pca_pool;
	daos_pool_info_t       *pca_info;
	struct pool_buf	       *pca_map_buf;
};

static int
pool_connect_cp(void *data, daos_event_t *ev, int rc)
{
	struct daos_op_sp	*sp = data;
	struct pool_connect_arg	*arg = sp->sp_arg;
	struct dc_pool		*pool = arg->pca_pool;
	daos_pool_info_t	*info = arg->pca_info;
	struct pool_buf		*map_buf = arg->pca_map_buf;
	struct pool_connect_in	*pci = crt_req_get(sp->sp_rpc);
	struct pool_connect_out	*pco = crt_reply_get(sp->sp_rpc);

	if (rc == -DER_TRUNC) {
		/* TODO: Reallocate a larger buffer and reconnect. */
		D_ERROR("pool map buffer (%ld) < required (%u)\n",
			pool_buf_size(map_buf->pb_nr), pco->pco_map_buf_size);
		D_GOTO(out, rc);
	}

	if (rc) {
		D_ERROR("RPC error while connecting to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = pco->pco_op.po_rc;
	if (rc) {
		D_ERROR("failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = process_query_reply(pool, map_buf, pco->pco_op.po_map_version,
				 pco->pco_mode, NULL /* tgts */, info);
	if (rc != 0) {
		/* TODO: What do we do about the remote connection state? */
		D_ERROR("failed to create local pool map: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* add pool to hash */
	dsmc_pool_add_cache(pool, sp->sp_hdlp);

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), sp->sp_hdlp->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	crt_req_decref(sp->sp_rpc);
	D_FREE_PTR(arg);
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
	dc_pool_put(pool);
	return rc;
}

int
dc_pool_connect(const uuid_t uuid, const char *grp,
		const daos_rank_list_t *tgts, unsigned int flags,
		daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev)
{
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct pool_connect_in	*pci;
	struct dc_pool		*pool;
	struct daos_op_sp	*sp;
	struct pool_connect_arg	*arg;
	struct pool_buf		*map_buf;
	int			 rc;

	if (uuid_is_null(uuid) || !flags_are_valid(flags) || poh == NULL)
		return -DER_INVAL;

	/** allocate and fill in pool connection */
	pool = pool_alloc();
	if (pool == NULL)
		return -DER_NOMEM;

	uuid_copy(pool->dp_pool, uuid);
	uuid_generate(pool->dp_pool_hdl);
	pool->dp_capas = flags;

	D_DEBUG(DF_DSMC, DF_UUID": connecting: hdl="DF_UUIDF" flags=%x\n",
		DP_UUID(uuid), DP_UUID(pool->dp_pool_hdl), flags);

	/*
	 * Currently, rank 0 runs the pool and the (only) container service.
	 * ep.ep_grp_id and ep.ep_tag are not used at the moment.
	 */
	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = pool_req_create(daos_ev2ctx(ev), ep, POOL_CONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(out_pool, rc);
	}

	/** fill in request buffer */
	pci = crt_req_get(rpc);
	uuid_copy(pci->pci_op.pi_uuid, uuid);
	uuid_copy(pci->pci_op.pi_hdl, pool->dp_pool_hdl);
	pci->pci_uid = geteuid();
	pci->pci_gid = getegid();
	pci->pci_capas = flags;

	rc = map_bulk_create(daos_ev2ctx(ev), &pci->pci_map_bulk, &map_buf);
	if (rc != 0)
		D_GOTO(out_req, rc);

	/* Prepare "arg" for pool_connect_cp(). */
	D_ALLOC_PTR(arg);
	if (arg == NULL)
		D_GOTO(out_bulk, rc = -DER_NOMEM);

	arg->pca_pool = pool;
	arg->pca_info = info;
	arg->pca_map_buf = map_buf;

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	crt_req_addref(rpc); /** for scratchpad */
	sp->sp_rpc = rpc;
	sp->sp_hdlp = poh;
	sp->sp_arg = arg;

	rc = daos_event_register_comp_cb(ev, pool_connect_cp, sp);
	if (rc != 0)
		D_GOTO(out_sp, rc);

	/**
	 * mark event as in-flight, must be called before sending the request
	 * since it can race with the request callback execution
	 */
	rc = daos_event_launch(ev);
	if (rc)
		D_GOTO(out_sp, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, ev);

	return rc;

out_sp:
	crt_req_decref(sp->sp_rpc);
	D_FREE_PTR(arg);
out_bulk:
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
out_req:
	crt_req_decref(rpc);
out_pool:
	dc_pool_put(pool);
	return rc;
}

static int
pool_disconnect_cp(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp		*sp = arg;
	struct dc_pool			*pool = (struct dc_pool *)sp->sp_arg;
	struct pool_disconnect_out	*pdo;

	if (rc) {
		D_ERROR("RPC error while disconnecting from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pdo = crt_reply_get(sp->sp_rpc);
	rc = pdo->pdo_op.po_rc;
	if (rc) {
		D_ERROR("failed to disconnect from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": disconnected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), sp->sp_hdl.cookie,
		DP_UUID(pool->dp_pool_hdl));

	pthread_rwlock_rdlock(&pool->dp_map_lock);
	daos_placement_fini(pool->dp_map);
	pthread_rwlock_unlock(&pool->dp_map_lock);

	dsmc_pool_del_cache(pool);
	sp->sp_hdl.cookie = 0;
out:
	crt_req_decref(sp->sp_rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_pool_disconnect(daos_handle_t poh, daos_event_t *ev)
{
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct pool_disconnect_in	*pdi;
	struct daos_op_sp		*sp;
	int				 rc = 0;

	pool = dc_pool_lookup(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	D_DEBUG(DF_DSMC, DF_UUID": disconnecting: hdl="DF_UUID" cookie="DF_X64
		"\n", DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		poh.cookie);

	pthread_rwlock_rdlock(&pool->dp_co_list_lock);
	if (!daos_list_empty(&pool->dp_co_list)) {
		pthread_rwlock_unlock(&pool->dp_co_list_lock);
		dc_pool_put(pool);
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
		dc_pool_put(pool);
		if (ev != NULL) {
			daos_event_launch(ev);
			daos_event_complete(ev, rc);
		}
		return rc;
	}

	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;
	rc = pool_req_create(daos_ev2ctx(ev), ep, POOL_DISCONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		dc_pool_put(pool);
		return rc;
	}

	/** fill in request buffer */
	pdi = crt_req_get(rpc);
	D_ASSERT(pdi != NULL);
	uuid_copy(pdi->pdi_op.pi_uuid, pool->dp_pool);
	uuid_copy(pdi->pdi_op.pi_hdl, pool->dp_pool_hdl);

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	crt_req_addref(rpc); /** for scratchpad */
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
	crt_req_decref(rpc); /* scratchpad */
	crt_req_decref(rpc); /* free req */
	dc_pool_put(pool);
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

	D_SWAP32S(&pool_glob->dpg_magic);
	/* skip pool_glob->dpg_padding) */
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
	struct dc_pool		*pool;
	struct pool_buf		*map_buf;
	uint32_t		 map_version;
	struct dsmc_pool_glob	*pool_glob;
	daos_size_t		 glob_buf_size;
	uint32_t		 pb_nr;
	int			 rc = 0;

	D_ASSERT(glob != NULL);

	pool = dc_pool_lookup(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	pthread_rwlock_rdlock(&pool->dp_map_lock);
	map_version = pool_map_get_version(pool->dp_map);
	rc = pool_buf_extract(pool->dp_map, &map_buf);
	pthread_rwlock_unlock(&pool->dp_map_lock);
	if (rc != 0)
		D_GOTO(out_pool, rc);

	pb_nr = map_buf->pb_nr;
	glob_buf_size = dsmc_pool_glob_buf_size(pb_nr);
	if (glob->iov_buf == NULL) {
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_map_buf, rc = 0);
	}
	if (glob->iov_buf_len < glob_buf_size) {
		D_ERROR("Larger glob buffer needed ("DF_U64" bytes provided, "
			""DF_U64" required).\n", glob->iov_buf_len,
			glob_buf_size);
		glob->iov_buf_len = glob_buf_size;
		D_GOTO(out_map_buf, rc = -DER_TRUNC);
	}
	glob->iov_len = glob_buf_size;

	/* init pool global handle */
	pool_glob = (struct dsmc_pool_glob *)glob->iov_buf;
	pool_glob->dpg_magic = DC_POOL_GLOB_MAGIC;
	uuid_copy(pool_glob->dpg_pool, pool->dp_pool);
	uuid_copy(pool_glob->dpg_pool_hdl, pool->dp_pool_hdl);
	pool_glob->dpg_capas = pool->dp_capas;
	pool_glob->dpg_map_version = map_version;
	pool_glob->dpg_map_pb_nr = pb_nr;
	memcpy(pool_glob->dpg_map_buf, map_buf, pool_buf_size(pb_nr));

out_map_buf:
	pool_buf_free(map_buf);
out_pool:
	dc_pool_put(pool);
out:
	if (rc != 0)
		D_ERROR("dsmc_pool_l2g failed, rc: %d\n", rc);
	return rc;
}

int
dc_pool_local2global(daos_handle_t poh, daos_iov_t *glob)
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
	struct dc_pool		*pool;
	struct pool_buf		*map_buf;
	int			 rc = 0;

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

	rc = daos_placement_init(pool->dp_map);
	if (rc != 0)
		D_GOTO(out, rc);

	/* add pool to hash */
	dsmc_pool_add_cache(pool, poh);

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" slave\n", DP_UUID(pool->dp_pool), poh->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	if (rc != 0)
		D_ERROR("dsmc_pool_g2l failed, rc: %d.\n", rc);
	if (pool != NULL)
		dc_pool_put(pool);
	return rc;
}

int
dc_pool_global2local(daos_iov_t glob, daos_handle_t *poh)
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
	if (pool_glob->dpg_magic == D_SWAP32(DC_POOL_GLOB_MAGIC)) {
		dsmc_swap_pool_glob(pool_glob);
		D_ASSERT(pool_glob->dpg_magic == DC_POOL_GLOB_MAGIC);
	} else if (pool_glob->dpg_magic != DC_POOL_GLOB_MAGIC) {
		D_ERROR("Bad hgh_magic: 0x%x.\n", pool_glob->dpg_magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dsmc_pool_g2l(pool_glob, poh);
	if (rc != 0)
		D_ERROR("dsmc_pool_g2l failed, rc: %d.\n", rc);

out:
	return rc;
}

static int
pool_exclude_cp(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp	       *sp = arg;
	struct pool_exclude_out	       *out = crt_reply_get(sp->sp_rpc);
	struct dc_pool		       *pool = sp->sp_arg;

	if (rc != 0) {
		D_ERROR("RPC error while excluding targets: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->peo_op.po_rc;
	if (rc != 0) {
		D_ERROR("failed to exclude targets: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": excluded: hdl="DF_UUID" failed=%u\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		out->peo_targets == NULL ? 0 : out->peo_targets->rl_nr.num);

	if (out->peo_targets != NULL && out->peo_targets->rl_nr.num > 0)
		rc = -DER_INVAL;

out:
	crt_req_decref(sp->sp_rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_pool_exclude(daos_handle_t poh, daos_rank_list_t *tgts, daos_event_t *ev)
{
	struct dc_pool	       *pool;
	crt_endpoint_t		ep;
	crt_rpc_t	       *rpc;
	struct pool_exclude_in *in;
	struct daos_op_sp      *sp;
	int			rc;

	if (tgts == NULL || tgts->rl_nr.num == 0)
		return -DER_INVAL;

	pool = dc_pool_lookup(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	D_DEBUG(DF_DSMC, DF_UUID": excluding %u targets: hdl="DF_UUID
		" tgts[0]=%u\n", DP_UUID(pool->dp_pool), tgts->rl_nr.num,
		DP_UUID(pool->dp_pool_hdl), tgts->rl_ranks[0]);

	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = pool_req_create(daos_ev2ctx(ev), ep, POOL_EXCLUDE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(err_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pei_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->pei_op.pi_hdl, pool->dp_pool_hdl);
	in->pei_targets = tgts;

	sp = daos_ev2sp(ev);
	crt_req_addref(rpc);
	sp->sp_rpc = rpc;
	sp->sp_arg = pool;

	rc = daos_event_register_comp_cb(ev, pool_exclude_cp, sp);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	return daos_rpc_send(rpc, ev);

err_rpc:
	crt_req_decref(sp->sp_rpc);
	crt_req_decref(rpc);
err_pool:
	dc_pool_put(pool);
	return rc;
}

struct dc_pool_query_arg {
	struct dc_pool	       *dqa_pool;
	daos_rank_list_t       *dqa_tgts;
	daos_pool_info_t       *dqa_info;
	struct pool_buf	       *dqa_map_buf;
	dc_pool_query_cb_t	dqa_cb;
	void		       *dqa_cb_arg;
};

static int
dc_pool_query_cb(const struct crt_cb_info *cb_info)
{
	struct dc_pool_query_arg       *arg = cb_info->cci_arg;
	struct pool_query_in	       *in = crt_req_get(cb_info->cci_rpc);
	struct pool_query_out	       *out = crt_reply_get(cb_info->cci_rpc);
	int				rc = cb_info->cci_rc;

	D_DEBUG(DF_DSMC, DF_UUID": query rpc done: %d\n",
		DP_UUID(arg->dqa_pool->dp_pool), rc);

	/* TODO: Upon -DER_TRUNC, reallocate a larger buffer and retry. */
	if (rc == -DER_TRUNC)
		D_ERROR(DF_UUID": pool buffer too small: %d\n",
			DP_UUID(arg->dqa_pool->dp_pool), rc);

	if (rc == 0)
		rc = process_query_reply(arg->dqa_pool, arg->dqa_map_buf,
					 out->pqo_op.po_map_version,
					 out->pqo_mode, arg->dqa_tgts,
					 arg->dqa_info);

	arg->dqa_cb(arg->dqa_pool, arg->dqa_cb_arg, rc, arg->dqa_tgts,
		    arg->dqa_info);

	dc_pool_put(arg->dqa_pool);
	map_bulk_destroy(in->pqi_map_bulk, arg->dqa_map_buf);
	D_FREE_PTR(arg);
	return 0;
}

/**
 * Query the latest pool information (i.e., mainly the pool map). This is meant
 * to be an "uneventful" interface; callers wishing to play with events shall
 * do so with \a cb and \a cb_arg.
 *
 * \param[in]	pool	pool handle object
 * \param[in]	ctx	RPC context
 * \param[out]	tgts	if not NULL, pool target ranks returned on success
 * \param[out]	info	if not NULL, pool information returned on success
 * \param[in]	cb	callback called only on success
 * \param[in]	cb_arg	argument passed to \a cb
 * \return		zero or error
 *
 * TODO: Avoid redundant POOL_QUERY RPCs triggered by multiple
 * threads/operations.
 */
int
dc_pool_query(struct dc_pool *pool, crt_context_t ctx, daos_rank_list_t *tgts,
	      daos_pool_info_t *info, dc_pool_query_cb_t cb, void *cb_arg)
{
	crt_endpoint_t			ep;
	crt_rpc_t		       *rpc;
	struct pool_query_in	       *in;
	struct pool_buf		       *map_buf;
	struct dc_pool_query_arg       *arg;
	int				rc;

	D_ASSERT(tgts == NULL); /* TODO */

	D_DEBUG(DF_DSMC, DF_UUID": querying: hdl="DF_UUID" tgts=%p info=%p\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl), tgts, info);

	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = pool_req_create(ctx, ep, POOL_QUERY, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool query rpc: %d\n",
			DP_UUID(pool->dp_pool), rc);
		D_GOTO(err, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pqi_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->pqi_op.pi_hdl, pool->dp_pool_hdl);

	rc = map_bulk_create(ctx, &in->pqi_map_bulk, &map_buf);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		D_GOTO(err_bulk, rc = -DER_NOMEM);

	dc_pool_get(pool);
	arg->dqa_pool = pool;
	arg->dqa_info = info;
	arg->dqa_map_buf = map_buf;
	arg->dqa_cb = cb;
	arg->dqa_cb_arg = cb_arg;

	rc = crt_req_send(rpc, dc_pool_query_cb, arg);
	if (rc != 0)
		D_GOTO(err_pool, rc);

	return 0;

err_pool:
	dc_pool_put(pool);
	D_FREE_PTR(arg);
err_bulk:
	map_bulk_destroy(in->pqi_map_bulk, map_buf);
err_rpc:
	crt_req_decref(rpc);
err:
	daos_task_complete((struct daos_task *)cb_arg, rc);
	return rc;
}
