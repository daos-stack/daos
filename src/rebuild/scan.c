/**
 * (C) Copyright 2017 Intel Corporation.
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
 * rebuild: Scanning the objects
 *
 * This file contains the server API methods and the RPC handlers for scanning
 * the object during rebuild.
 */
#define DD_SUBSYS	DD_FAC(rebuild)

#include <daos_srv/pool.h>

#include <daos/btree_class.h>
#include <daos/pool_map.h>
#include <daos/pool.h>
#include <daos/rpc.h>
#include <daos/placement.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/vos.h>
#include "rpc.h"

/*
 * Initiate the rebuild process, i.e. sending rebuild
 * requests to every target to find out the impacted
 * objects.
 **/
int
ds_rebuild(crt_context_t ctx, const uuid_t uuid,
	   daos_rank_list_t *tgts_failed)
{
	struct ds_pool		*pool;
	crt_rpc_t		*rpc;
	struct rebuild_scan_in	*rsi;
	struct rebuild_out	*ro;
	int			rc;

	/* Broadcast the pool map first */
	rc = ds_pool_pmap_broadcast(uuid, tgts_failed);
	if (rc)
		return rc;

	pool = ds_pool_lookup(uuid);
	if (pool == NULL)
		return -DER_NO_HDL;

	/* Then send rebuild RPC to all targets of the pool */
	rc = ds_pool_bcast_create(ctx, pool, DAOS_REBUILD_MODULE,
				  REBUILD_OBJECTS_SCAN, &rpc, NULL,
				  tgts_failed);
	if (rc != 0)
		D_GOTO(out, rc);

	rsi = crt_req_get(rpc);
	uuid_generate(rsi->rsi_rebuild_cont_hdl_uuid);
	uuid_generate(rsi->rsi_rebuild_pool_hdl_uuid);
	uuid_copy(rsi->rsi_pool_uuid, uuid);
	D_DEBUG(DB_TRACE, "rebuild "DF_UUID"/"DF_UUID"\n",
		DP_UUID(rsi->rsi_pool_uuid),
		DP_UUID(rsi->rsi_rebuild_cont_hdl_uuid));
	rsi->rsi_pool_map_ver = pool_map_get_version(pool->sp_map);
	rsi->rsi_tgts_failed = tgts_failed;

	rc = dss_rpc_send(rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	ro = crt_reply_get(rpc);
	rc = ro->ro_status;
	if (rc != 0)
		D_ERROR(DF_UUID": failed to start pool rebuild: %d\n",
			DP_UUID(uuid), rc);
out_rpc:
	crt_req_decref(rpc);
out:
	ds_pool_put(pool);
	return rc;
}

struct rebuild_scan_arg {
	struct pool_map *map;
	struct pl_target_grp *tgp_failed;
};

static int
placement_check(uuid_t co_uuid, daos_unit_oid_t oid, void *data)
{
	struct rebuild_scan_arg	*arg = data;
	struct pl_obj_layout	*layout = NULL;
	struct pl_map		*map = NULL;
	struct daos_obj_md	md;
	uint32_t		tgt_rebuild;
	int			rc;

	obj_fetch_md(oid.id_pub, &md, NULL);

	map = pl_map_find(DAOS_HDL_INVAL, oid.id_pub);
	if (map == NULL) {
		D_ERROR(DF_UOID"Cannot find valid placement map\n",
			DP_UOID(oid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_find_rebuild(map, &md, NULL, arg->tgp_failed,
				 &tgt_rebuild);
	if (rc <= 0) /* No need rebuild */
		D_GOTO(out, rc);

	D_DEBUG(DB_PL, "rebuild obj "DF_UOID" on %d\n",
		DP_UOID(oid), tgt_rebuild);
	/* TODO add the object to the rebuild list */
	rc = 0;

out:
	if (layout != NULL)
		pl_obj_layout_free(layout);

	if (map != NULL)
		pl_map_decref(map);

	return rc;
}

struct rebuild_cont_open_arg {
	uuid_t	pool_uuid;
	uuid_t  pool_hdl_uuid;
	uuid_t	cont_hdl_uuid;
};

/**
 * To avoid broadcasting during pool_connect and container
 * open for rebuild, let's create a local ds_pool/ds_container
 * and dc_pool/dc_container, so rebuild client will always
 * use the specified pool_hdl/container_hdl uuid during
 * rebuild.
 **/
static int
rebuild_prepare_one(void *data)
{
	struct rebuild_cont_open_arg	*arg = data;
	struct pool_map			*map;
	daos_handle_t			ph;
	int				rc;

	map = ds_pool_get_pool_map(arg->pool_uuid);
	if (map == NULL)
		return -DER_INVAL;

	/* Create ds_pool locally */
	rc = ds_pool_local_open(arg->pool_uuid, pool_map_get_version(map),
				NULL);
	if (rc)
		return rc;

	/* Create ds_container locally */
	rc = ds_cont_local_open(arg->pool_uuid, arg->cont_hdl_uuid,
				NULL, 0, NULL);
	if (rc)
		return rc;

	/* Create dc_pool locally */
	rc = dc_pool_local_open(arg->pool_uuid, arg->pool_hdl_uuid,
				0, NULL, map, &ph);

	return rc;
}

static int
ds_rebuild_prepare(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		   uuid_t cont_hdl_uuid)
{
	struct rebuild_cont_open_arg arg;
	struct pool_map		     *map;
	int rc;

	map = ds_pool_get_pool_map(pool_uuid);
	if (map == NULL)
		return -DER_INVAL;

	/* Initialize the placement */
	rc = daos_placement_update(map);
	if (rc)
		return rc;

	uuid_copy(arg.pool_uuid, pool_uuid);
	uuid_copy(arg.pool_hdl_uuid, pool_hdl_uuid);
	uuid_copy(arg.cont_hdl_uuid, cont_hdl_uuid);
	rc = dss_collective(rebuild_prepare_one, &arg);

	return rc;
}

struct rebuild_iter_arg {
	uuid_t		pool_uuid;
	cont_iter_cb_t	callback;
	void		*arg;
};

int
rebuild_obj_iter(void *data)
{
	struct rebuild_iter_arg *arg = data;

	return ds_pool_obj_iter(arg->pool_uuid, arg->callback,
				arg->arg);
}

/* Scan the local target and generate rebuild object list */
int
ds_rebuild_scan_handler(crt_rpc_t *rpc)
{
	struct rebuild_scan_in	*rsi;
	struct rebuild_out	*ro;
	struct pool_map		*map = NULL;
	struct rebuild_scan_arg	*arg = NULL;
	struct rebuild_iter_arg iter_arg;
	struct pl_target_grp	*pl_grp = NULL;
	int			i;
	int			rc;

	rsi = crt_req_get(rpc);
	D_ASSERT(rsi != NULL);

	D_DEBUG(DB_TRACE, "%d scan rebuild for "DF_UUID"\n",
		dss_get_module_info()->dmi_tid,
		DP_UUID(rsi->rsi_pool_uuid));

	rc = ds_rebuild_prepare(rsi->rsi_pool_uuid,
				rsi->rsi_rebuild_pool_hdl_uuid,
				rsi->rsi_rebuild_cont_hdl_uuid);
	if (rc)
		D_GOTO(out, rc);

	map = ds_pool_get_pool_map(rsi->rsi_pool_uuid);
	if (map == NULL)
		/* XXX it might need wait here if the pool
		 * map is not being populated to all targets
		 **/
		D_GOTO(out, rc = -DER_INVAL);

	/* Convert failed rank list to pl_grp, since the rebuild
	 * process might be asynchronous later, let's allocate
	 * every thing for now
	 **/
	D_ALLOC_PTR(pl_grp);
	if (pl_grp == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	pl_grp->tg_ver = rsi->rsi_pool_map_ver;
	pl_grp->tg_target_nr = rsi->rsi_tgts_failed->rl_nr.num;
	D_ALLOC(pl_grp->tg_targets, pl_grp->tg_target_nr *
		sizeof(*pl_grp->tg_targets));
	if (pl_grp->tg_targets == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	for (i = 0; i < rsi->rsi_tgts_failed->rl_nr.num; i++) {
		struct pool_target     *target;
		daos_rank_t		rank;

		rank = rsi->rsi_tgts_failed->rl_ranks[i];
		target = pool_map_find_target_by_rank(map, rank);
		if (target == NULL)
			continue;

		pl_grp->tg_targets[i].pt_pos = target->ta_comp.co_id;
	}

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	arg->map = map;
	arg->tgp_failed = pl_grp;

	iter_arg.arg = arg;
	iter_arg.callback = placement_check;
	uuid_copy(iter_arg.pool_uuid, rsi->rsi_pool_uuid);

	rc = dss_collective(rebuild_obj_iter, &iter_arg);

free:
	if (arg != NULL)
		D_FREE_PTR(arg);
	if (pl_grp != NULL) {
		if (pl_grp->tg_targets != NULL)
			D_FREE(pl_grp->tg_targets,
			       pl_grp->tg_target_nr *
			       sizeof(*pl_grp->tg_targets));
		D_FREE_PTR(pl_grp);
	}
out:
	ro = crt_reply_get(rpc);
	ro->ro_status = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	return rc;
}
