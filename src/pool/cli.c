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
#define DD_SUBSYS	DD_FAC(pool)

#include <daos_task.h>
#include <daos_types.h>
#include <daos/event.h>
#include <daos/placement.h>
#include <daos/pool.h>
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
pool_free(struct dc_pool *pool)
{
	pthread_rwlock_destroy(&pool->dp_map_lock);
	pthread_rwlock_destroy(&pool->dp_co_list_lock);
	D_ASSERT(daos_list_empty(&pool->dp_co_list));
	if (pool->dp_map != NULL)
		pool_map_destroy(pool->dp_map);
	if (pool->dp_group != NULL)
	    daos_group_detach(pool->dp_group);
	D_FREE_PTR(pool);
}

void
dc_pool_get(struct dc_pool *pool)
{
	pool->dp_ref++;
}

void
dc_pool_put(struct dc_pool *pool)
{
	D_ASSERT(pool->dp_ref > 0);
	if (--pool->dp_ref == 0)
		pool_free(pool);
}

struct dc_pool *
dc_hdl2pool(daos_handle_t poh)
{
	struct dc_pool *dp = (struct dc_pool *)poh.cookie;

	dp->dp_ref++;
	return dp;
}

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
	pool->dp_ref = 1;

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

/* Assume dp_map_lock is locked before calling this function */
static int
pool_map_update(struct dc_pool *pool, struct pool_map **map,
		unsigned int map_version)
{
	struct pool_map *tmp_map;
	int rc;

	D_ASSERT(map != NULL);
	if (pool->dp_map == NULL) {
		rc = daos_placement_init(*map);
		if (rc == 0) {
			D_DEBUG(DF_DSMC, DF_UUID": init pool map: %u\n",
				DP_UUID(pool->dp_pool),
				pool_map_get_version(*map));
			pool->dp_map = *map;
			*map = NULL;
		}
		return rc;
	}

	if (map_version <= pool_map_get_version(pool->dp_map)) {
		D_DEBUG(DF_DSMC, DF_UUID": got older pool map: %u -> %u %p\n",
			DP_UUID(pool->dp_pool),
			pool_map_get_version(pool->dp_map), map_version, pool);
		return 0;
	}

	D_DEBUG(DF_DSMC, DF_UUID": updating pool map: %u -> %u\n",
		DP_UUID(pool->dp_pool),
		pool->dp_map == NULL ?
		0 : pool_map_get_version(pool->dp_map), map_version);

	rc = daos_placement_update(*map);
	if (rc != 0) {
		D_ERROR("Failed to refresh placement map: %d\n", rc);
		return rc;
	}

	tmp_map = pool->dp_map;
	pool->dp_map = *map;
	*map = tmp_map;
	return rc;
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
	int			rc;

	rc = pool_map_create(map_buf, map_version, &map);
	if (rc != 0) {
		D_ERROR("failed to create local pool map: %d\n", rc);
		return rc;
	}

	pthread_rwlock_wrlock(&pool->dp_map_lock);
	rc = pool_map_update(pool, &map, map_version);
	if (rc)
		D_GOTO(out_unlock, rc);

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

	if (map != NULL)
		pool_map_destroy(map);

	if (info != NULL && rc == 0) {
		uuid_copy(info->pi_uuid, pool->dp_pool);
		info->pi_ntargets = map_buf->pb_target_nr;
		info->pi_mode = mode;
	}

	return rc;
}

struct pool_connect_arg {
	struct dc_pool		*pca_pool;
	daos_pool_info_t	*pca_info;
	struct pool_buf		*pca_map_buf;
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
};

static int
pool_connect_cp(struct daos_task *task, void *data)
{
	struct pool_connect_arg *arg = (struct pool_connect_arg *)data;
	struct dc_pool		*pool = arg->pca_pool;
	daos_pool_info_t	*info = arg->pca_info;
	struct pool_buf		*map_buf = arg->pca_map_buf;
	struct pool_connect_in	*pci = crt_req_get(arg->rpc);
	struct pool_connect_out	*pco = crt_reply_get(arg->rpc);
	int			 rc = task->dt_result;

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
	dc_pool2hdl(pool, arg->hdlp);

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), arg->hdlp->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	crt_req_decref(arg->rpc);
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
	dc_pool_put(pool);
	return rc;
}

int
dc_pool_local_close(daos_handle_t ph)
{
	struct dc_pool	*pool;

	pool = dc_hdl2pool(ph);
	if (pool == NULL)
		return 0;

	pthread_rwlock_rdlock(&pool->dp_map_lock);
	daos_placement_fini(pool->dp_map);
	pthread_rwlock_unlock(&pool->dp_map_lock);

	dc_pool_put(pool);
	return 0;
}

int
dc_pool_local_open(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		   unsigned int flags, const char *grp,
		   struct pool_map *map, daos_handle_t *ph)
{
	struct dc_pool	*pool;
	int		rc = 0;

	if (!daos_handle_is_inval(*ph)) {
		pool = dc_hdl2pool(*ph);
		if (pool != NULL)
			D_GOTO(out, rc = 0);
	}

	/** allocate and fill in pool connection */
	pool = pool_alloc();
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_TRACE, "after alloc "DF_UUIDF"\n", DP_UUID(pool_uuid));
	uuid_copy(pool->dp_pool, pool_uuid);
	uuid_copy(pool->dp_pool_hdl, pool_hdl_uuid);
	pool->dp_capas = flags;

	D_DEBUG(DB_TRACE, "before update "DF_UUIDF"\n", DP_UUID(pool_uuid));
	rc = pool_map_update(pool, &map, pool_map_get_version(map));
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DF_DSMC, DF_UUID": create: hdl="DF_UUIDF" flags=%x\n",
		DP_UUID(pool_uuid), DP_UUID(pool->dp_pool_hdl), flags);

	dc_pool2hdl(pool, ph);
out:
	if (pool != NULL)
		dc_pool_put(pool);

	return rc;
}

int
dc_pool_connect(struct daos_task *task)
{
	daos_pool_connect_t	*args;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct pool_connect_in	*pci;
	struct dc_pool		*pool;
	struct pool_buf		*map_buf;
	struct pool_connect_arg	 con_args;
	int			 rc;

	args = daos_task_get_args(DAOS_OPC_POOL_CONNECT, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (uuid_is_null(args->uuid) || !flags_are_valid(args->flags) ||
	    args->poh == NULL)
		return -DER_INVAL;

	/** allocate and fill in pool connection */
	pool = pool_alloc();
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NOMEM);

	uuid_copy(pool->dp_pool, args->uuid);
	uuid_generate(pool->dp_pool_hdl);
	pool->dp_capas = args->flags;

	rc = daos_group_attach(args->grp, &pool->dp_group);
	if (rc != 0)
		D_GOTO(out_pool, rc);

	D_DEBUG(DF_DSMC, DF_UUID": connecting: hdl="DF_UUIDF" flags=%x\n",
		DP_UUID(args->uuid), DP_UUID(pool->dp_pool_hdl), args->flags);

	/** Currently, rank 0 runs the pool and the (only) container service. */
	ep.ep_grp = pool->dp_group;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = pool_req_create(daos_task2ctx(task), ep, POOL_CONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(out_pool, rc);
	}

	/** for con_argss */
	crt_req_addref(rpc);

	/** fill in request buffer */
	pci = crt_req_get(rpc);
	uuid_copy(pci->pci_op.pi_uuid, args->uuid);
	uuid_copy(pci->pci_op.pi_hdl, pool->dp_pool_hdl);
	pci->pci_uid = geteuid();
	pci->pci_gid = getegid();
	pci->pci_capas = args->flags;

	rc = map_bulk_create(daos_task2ctx(task), &pci->pci_map_bulk, &map_buf);
	if (rc != 0)
		D_GOTO(out_req, rc);

	/** Prepare "con_args" for pool_connect_cp(). */
	con_args.pca_pool = pool;
	con_args.pca_info = args->info;
	con_args.pca_map_buf = map_buf;
	con_args.rpc = rpc;
	con_args.hdlp = args->poh;

	rc = daos_task_register_comp_cb(task, pool_connect_cp,
					sizeof(con_args), &con_args);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return rc;

out_bulk:
	map_bulk_destroy(pci->pci_map_bulk, map_buf);
out_req:
	crt_req_decref(rpc);
	crt_req_decref(rpc); /* free req */
out_pool:
	dc_pool_put(pool);
out_task:
	daos_task_complete(task, rc);
	return rc;
}

struct pool_disconnect_arg {
	struct dc_pool		*pool;
	crt_rpc_t		*rpc;
	daos_handle_t		 hdl;
};

static int
pool_disconnect_cp(struct daos_task *task, void *data)
{
	struct pool_disconnect_arg	*arg =
		(struct pool_disconnect_arg *)data;
	struct dc_pool			*pool = arg->pool;
	struct pool_disconnect_out	*pdo;
	int				 rc = task->dt_result;

	if (rc) {
		D_ERROR("RPC error while disconnecting from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pdo = crt_reply_get(arg->rpc);
	rc = pdo->pdo_op.po_rc;
	if (rc) {
		D_ERROR("failed to disconnect from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": disconnected: cookie="DF_X64" hdl="DF_UUID
		" master\n", DP_UUID(pool->dp_pool), arg->hdl.cookie,
		DP_UUID(pool->dp_pool_hdl));

	pthread_rwlock_rdlock(&pool->dp_map_lock);
	daos_placement_fini(pool->dp_map);
	pthread_rwlock_unlock(&pool->dp_map_lock);

	dc_pool_put(pool);
	arg->hdl.cookie = 0;

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_pool_disconnect(struct daos_task *task)
{
	daos_pool_disconnect_t		*args;
	struct dc_pool			*pool;
	crt_endpoint_t			 ep;
	crt_rpc_t			*rpc;
	struct pool_disconnect_in	*pdi;
	struct pool_disconnect_arg	 disc_args;
	int				 rc = 0;

	args = daos_task_get_args(DAOS_OPC_POOL_DISCONNECT, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": disconnecting: hdl="DF_UUID" cookie="DF_X64
		"\n", DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		args->poh.cookie);

	pthread_rwlock_rdlock(&pool->dp_co_list_lock);
	if (!daos_list_empty(&pool->dp_co_list)) {
		pthread_rwlock_unlock(&pool->dp_co_list_lock);
		D_GOTO(out_pool, rc = -DER_BUSY);
	}
	pool->dp_disconnecting = 1;
	pthread_rwlock_unlock(&pool->dp_co_list_lock);

	if (pool->dp_slave) {
		D_DEBUG(DF_DSMC, DF_UUID": disconnecting: cookie="DF_X64" hdl="
			DF_UUID" slave\n", DP_UUID(pool->dp_pool),
			args->poh.cookie, DP_UUID(pool->dp_pool_hdl));

		pthread_rwlock_rdlock(&pool->dp_map_lock);
		daos_placement_fini(pool->dp_map);
		pthread_rwlock_unlock(&pool->dp_map_lock);

		dc_pool_put(pool);
		args->poh.cookie = 0;
		D_GOTO(out_pool, rc);
	}

	ep.ep_grp = pool->dp_group;
	ep.ep_rank = 0;
	ep.ep_tag = 0;
	rc = pool_req_create(daos_task2ctx(task), ep, POOL_DISCONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
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

	rc = daos_task_register_comp_cb(task, pool_disconnect_cp,
					sizeof(disc_args), &disc_args);
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
	daos_task_complete(task, rc);
	return rc;

}

#define DC_POOL_GLOB_MAGIC	(0x16da0386)

/* Structure of global buffer for dc_pool */
struct dc_pool_glob {
	/* magic number, DC_POOL_GLOB_MAGIC */
	uint32_t	dpg_magic;
	uint32_t	dpg_padding;
	/* pool group_id, uuid, and capas */
	char		dpg_group_id[CRT_GROUP_ID_MAX_LEN];
	uuid_t		dpg_pool;
	uuid_t		dpg_pool_hdl;
	uint64_t	dpg_capas;
	/* poolmap version */
	uint32_t	dpg_map_version;
	/* number of component of poolbuf, same as pool_buf::pb_nr */
	uint32_t	dpg_map_pb_nr;
	struct pool_buf	dpg_map_buf[0];
};

static inline daos_size_t
dc_pool_glob_buf_size(unsigned int pb_nr)
{
	return offsetof(struct dc_pool_glob, dpg_map_buf) +
	       pool_buf_size(pb_nr);
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
	/* skip pool_glob->dpg_group_id[] */
	/* skip pool_glob->dpg_pool (uuid_t) */
	/* skip pool_glob->dpg_pool_hdl (uuid_t) */
	D_SWAP64S(&pool_glob->dpg_capas);
	D_SWAP32S(&pool_glob->dpg_map_version);
	D_SWAP32S(&pool_glob->dpg_map_pb_nr);
	swap_pool_buf(pool_glob->dpg_map_buf);
}

static int
dc_pool_l2g(daos_handle_t poh, daos_iov_t *glob)
{
	struct dc_pool		*pool;
	struct pool_buf		*map_buf;
	uint32_t		 map_version;
	struct dc_pool_glob	*pool_glob;
	daos_size_t		 glob_buf_size;
	uint32_t		 pb_nr;
	int			 rc = 0;

	D_ASSERT(glob != NULL);

	pool = dc_hdl2pool(poh);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	pthread_rwlock_rdlock(&pool->dp_map_lock);
	map_version = pool_map_get_version(pool->dp_map);
	rc = pool_buf_extract(pool->dp_map, &map_buf);
	pthread_rwlock_unlock(&pool->dp_map_lock);
	if (rc != 0)
		D_GOTO(out_pool, rc);

	pb_nr = map_buf->pb_nr;
	glob_buf_size = dc_pool_glob_buf_size(pb_nr);
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
	pool_glob = (struct dc_pool_glob *)glob->iov_buf;
	pool_glob->dpg_magic = DC_POOL_GLOB_MAGIC;
	strncpy(pool_glob->dpg_group_id, pool->dp_group->cg_grpid,
		sizeof(pool_glob->dpg_group_id) - 1);
	pool_glob->dpg_group_id[sizeof(pool_glob->dpg_group_id) - 1] = '\0';
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
		D_ERROR("dc_pool_l2g failed, rc: %d\n", rc);
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

	rc = dc_pool_l2g(poh, glob);

out:
	return rc;
}

static int
dc_pool_g2l(struct dc_pool_glob *pool_glob, daos_handle_t *poh)
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

	rc = daos_group_attach(pool_glob->dpg_group_id, &pool->dp_group);
	if (rc != 0)
		D_GOTO(out, rc);

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
	dc_pool2hdl(pool, poh);

	D_DEBUG(DF_DSMC, DF_UUID": connected: cookie="DF_X64" hdl="DF_UUID
		" slave\n", DP_UUID(pool->dp_pool), poh->cookie,
		DP_UUID(pool->dp_pool_hdl));

out:
	if (rc != 0)
		D_ERROR("dc_pool_g2l failed, rc: %d.\n", rc);
	if (pool != NULL)
		dc_pool_put(pool);
	return rc;
}

int
dc_pool_global2local(daos_iov_t glob, daos_handle_t *poh)
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
		D_ERROR("Bad hgh_magic: 0x%x.\n", pool_glob->dpg_magic);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = dc_pool_g2l(pool_glob, poh);
	if (rc != 0)
		D_ERROR("dc_pool_g2l failed, rc: %d.\n", rc);

out:
	return rc;
}

struct pool_tgt_update_arg {
	struct dc_pool		*pool;
	crt_rpc_t		*rpc;
};

static int
pool_tgt_update_cp(struct daos_task *task, void *data)
{
	struct pool_tgt_update_arg	*arg;
	struct dc_pool			*pool;
	struct pool_tgt_update_out	*out;
	int				 rc = task->dt_result;

	arg = (struct pool_tgt_update_arg *)data;
	pool = arg->pool;
	out = crt_reply_get(arg->rpc);

	if (rc != 0) {
		D_ERROR("RPC error while excluding targets: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->pto_op.po_rc;
	if (rc != 0) {
		D_ERROR("failed to exclude targets: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": updated: hdl="DF_UUID" failed=%u\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		out->pto_targets == NULL ? 0 : out->pto_targets->rl_nr.num);

	if (out->pto_targets != NULL && out->pto_targets->rl_nr.num > 0)
		rc = -DER_INVAL;

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	return rc;
}

static int
dc_pool_update_internal(struct daos_task *task, daos_pool_update_t *args,
			int opc)
{
	struct dc_pool		*pool;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct pool_tgt_update_in *in;
	struct pool_tgt_update_arg update_args;
	int			 rc;

	if (args->tgts == NULL || args->tgts->rl_nr.num == 0)
		return -DER_INVAL;

	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": excluding %u targets: hdl="DF_UUID
		" tgts[0]=%u\n", DP_UUID(pool->dp_pool), args->tgts->rl_nr.num,
		DP_UUID(pool->dp_pool_hdl), args->tgts->rl_ranks[0]);

	ep.ep_grp = pool->dp_group;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = pool_req_create(daos_task2ctx(task), ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		D_GOTO(out_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pti_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->pti_op.pi_hdl, pool->dp_pool_hdl);
	in->pti_targets = args->tgts;

	update_args.pool = pool;
	crt_req_addref(rpc);
	update_args.rpc = rpc;

	rc = daos_task_register_comp_cb(task, pool_tgt_update_cp,
					sizeof(update_args), &update_args);
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
	daos_task_complete(task, rc);
	return rc;
}

int
dc_pool_exclude(struct daos_task *task)
{
	daos_pool_update_t *args;

	args = daos_task_get_args(DAOS_OPC_POOL_EXCLUDE, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_pool_update_internal(task, args, POOL_EXCLUDE);
}

int
dc_pool_add(struct daos_task *task)
{
	daos_pool_update_t *args;

	args = daos_task_get_args(DAOS_OPC_POOL_ADD, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_pool_update_internal(task, args, POOL_ADD);
}

int
dc_pool_exclude_out(struct daos_task *task)
{
	daos_pool_update_t *args;

	args = daos_task_get_args(DAOS_OPC_POOL_EXCLUDE_OUT, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return dc_pool_update_internal(task, args, POOL_EXCLUDE_OUT);
}

struct pool_query_arg {
	struct dc_pool	       *dqa_pool;
	daos_rank_list_t       *dqa_tgts;
	daos_pool_info_t       *dqa_info;
	struct pool_buf	       *dqa_map_buf;
	crt_rpc_t	       *rpc;
};

static int
pool_query_cb(struct daos_task *task, void *data)
{
	struct pool_query_arg	       *arg = (struct pool_query_arg *)data;
	struct pool_query_in	       *in = crt_req_get(arg->rpc);
	struct pool_query_out	       *out = crt_reply_get(arg->rpc);
	int				rc = task->dt_result;

	D_DEBUG(DF_DSMC, DF_UUID": query rpc done: %d\n",
		DP_UUID(arg->dqa_pool->dp_pool), rc);

	/* TODO: Upon -DER_TRUNC, reallocate a larger buffer and retry. */
	if (rc == -DER_TRUNC)
		D_ERROR(DF_UUID": pool buffer too small: %d\n",
			DP_UUID(arg->dqa_pool->dp_pool), rc);
	if (rc)
		D_GOTO(out, rc);

	rc = out->pqo_op.po_rc;
	if (rc)
		D_GOTO(out, rc);

	rc = process_query_reply(arg->dqa_pool, arg->dqa_map_buf,
				 out->pqo_op.po_map_version,
				 out->pqo_mode, arg->dqa_tgts, arg->dqa_info);

out:
	crt_req_decref(arg->rpc);
	dc_pool_put(arg->dqa_pool);
	map_bulk_destroy(in->pqi_map_bulk, arg->dqa_map_buf);
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
 * \param[out]	info	if not NULL, pool information returned on success
 * \param[in]	cb	callback called only on success
 * \param[in]	cb_arg	argument passed to \a cb
 * \return		zero or error
 *
 * TODO: Avoid redundant POOL_QUERY RPCs triggered by multiple
 * threads/operations.
 */
int
dc_pool_query(struct daos_task *task)
{
	daos_pool_query_t	       *args;
	struct dc_pool		       *pool;
	crt_endpoint_t			ep;
	crt_rpc_t		       *rpc;
	struct pool_query_in	       *in;
	struct pool_buf		       *map_buf;
	struct pool_query_arg		query_args;
	int				rc;

	args = daos_task_get_args(DAOS_OPC_POOL_QUERY, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	D_ASSERT(args->tgts == NULL); /* TODO */

	/** Lookup bumps pool ref ,1 */
	pool = dc_hdl2pool(args->poh);
	if (pool == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	D_DEBUG(DF_DSMC, DF_UUID": querying: hdl="DF_UUID" tgts=%p info=%p\n",
		DP_UUID(pool->dp_pool), DP_UUID(pool->dp_pool_hdl),
		args->tgts, args->info);

	ep.ep_grp = pool->dp_group;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = pool_req_create(daos_task2ctx(task), ep, POOL_QUERY, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool query rpc: %d\n",
			DP_UUID(pool->dp_pool), rc);
		D_GOTO(out_pool, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pqi_op.pi_uuid, pool->dp_pool);
	uuid_copy(in->pqi_op.pi_hdl, pool->dp_pool_hdl);

	/** +1 for args */
	crt_req_addref(rpc);

	rc = map_bulk_create(daos_task2ctx(task), &in->pqi_map_bulk, &map_buf);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	query_args.dqa_pool = pool;
	query_args.dqa_info = args->info;
	query_args.dqa_map_buf = map_buf;
	query_args.rpc = rpc;

	rc = daos_task_register_comp_cb(task, pool_query_cb, sizeof(query_args),
					&query_args);
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
	daos_task_complete(task, rc);
	return rc;
}

static int
pool_evict_cp(struct daos_task *task, void *data)
{
	crt_rpc_t		*rpc = *((crt_rpc_t **)data);
	struct pool_evict_in	*in = crt_req_get(rpc);
	struct pool_evict_out	*out = crt_reply_get(rpc);
	int			 rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("RPC error while evicting pool handles: %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = out->pvo_op.po_rc;
	if (rc != 0) {
		D_ERROR("failed to evict pool handles: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, DF_UUID": evicted\n", DP_UUID(in->pvi_op.pi_uuid));

out:
	daos_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

int
dc_pool_evict(struct daos_task *task)
{
	crt_endpoint_t		 ep;
	daos_pool_evict_t	*args;
	crt_rpc_t		*rpc;
	struct pool_evict_in	*in;
	int			 rc;

	args = daos_task_get_args(DAOS_OPC_POOL_EVICT, task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (uuid_is_null(args->uuid))
		D_GOTO(out_task, rc = -DER_INVAL);

	D_DEBUG(DF_DSMC, DF_UUID": evicting\n", DP_UUID(args->uuid));

	rc = daos_group_attach(args->grp, &ep.ep_grp);
	if (rc != 0)
		D_GOTO(out_task, rc);

	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = pool_req_create(daos_task2ctx(task), ep, POOL_EVICT, &rpc);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create pool evict rpc: %d\n",
			DP_UUID(args->uuid), rc);
		D_GOTO(out_group, rc);
	}

	in = crt_req_get(rpc);
	uuid_copy(in->pvi_op.pi_uuid, args->uuid);

	crt_req_addref(rpc);

	rc = daos_task_register_comp_cb(task, pool_evict_cp, sizeof(rpc), &rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	rc = daos_rpc_send(rpc, task);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	return rc;

out_rpc:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_group:
	daos_group_detach(ep.ep_grp);
out_task:
	daos_task_complete(task, rc);
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

	pthread_rwlock_rdlock(&pool->dp_map_lock);
	*map_ver = pool_map_get_version(pool->dp_map);
	pthread_rwlock_unlock(&pool->dp_map_lock);
	dc_pool_put(pool);

	return 0;
}

int
dc_pool_target_query(struct daos_task *task)
{
	return -DER_NOSYS;
}

int
dc_pool_extend(struct daos_task *task)
{
	return -DER_NOSYS;
}
