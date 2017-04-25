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
#include "rebuild_internal.h"

/**
 * Initiate the rebuild process, i.e. sending rebuild
 * requests to every target to find out the impacted
 * objects.
 */
int
ds_rebuild(const uuid_t uuid, daos_rank_list_t *tgts_failed)
{
	struct ds_pool		*pool;
	crt_rpc_t		*rpc;
	struct rebuild_scan_in	*rsi;
	struct rebuild_out	*ro;
	int			rc;

	D_DEBUG(DB_TRACE, "rebuild "DF_UUID"\n", DP_UUID(uuid));

	/* Broadcast the pool map first */
	rc = ds_pool_pmap_broadcast(uuid, tgts_failed);
	if (rc)
		D_ERROR("pool map broad cast failed: rc %d\n", rc);

	pool = ds_pool_lookup(uuid);
	if (pool == NULL)
		return -DER_NO_HDL;

	/* Then send rebuild RPC to all targets of the pool */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_OBJECTS_SCAN, &rpc, NULL,
				  tgts_failed);
	if (rc != 0) {
		D_ERROR("pool map broad cast failed: rc %d\n", rc);
		D_GOTO(out, rc = 0); /* ignore the failure */
	}

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

#define REBUILD_SEND_LIMIT	512
struct rebuild_send_arg {
	struct rebuild_root *tgt_root;
	daos_unit_oid_t	    *oids;
	uuid_t		    *uuids;
	unsigned int	    *shards;
	uuid_t		    current_uuid;
	int		    count;
};

struct rebuild_scan_arg {
	daos_handle_t		rebuild_tree_hdl;
	struct pool_map		*map;
	struct pl_target_grp	*tgp_failed;
	uuid_t			pool_uuid;
	ABT_mutex		scan_lock;
};

static int
ds_rebuild_obj_rpc_cb(const struct crt_cb_info *cb_info)
{
	struct rebuild_send_arg *arg = cb_info->cci_arg;

	D_FREE(arg->oids, sizeof(*arg->oids) * REBUILD_SEND_LIMIT);
	D_FREE(arg->uuids, sizeof(*arg->uuids) * REBUILD_SEND_LIMIT);
	D_FREE_PTR(arg);
	return 0;
}

static int
rebuild_obj_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		    daos_iov_t *val_iov, void *data)
{
	struct rebuild_send_arg *arg = data;
	struct rebuild_root	*root = arg->tgt_root;
	daos_unit_oid_t		*oids = arg->oids;
	uuid_t			*uuids = arg->uuids;
	unsigned int		*shards = arg->shards;
	int			count = arg->count;
	int			rc;

	D_ASSERT(count < REBUILD_SEND_LIMIT);
	oids[count] = *((daos_unit_oid_t *)key_iov->iov_buf);
	shards[count] = *((unsigned int *)val_iov->iov_buf);
	uuid_copy(uuids[count], arg->current_uuid);
	arg->count++;

	rc = dbtree_iter_delete(ih, NULL);
	if (rc != 0)
		return rc;


	D_DEBUG(DB_TRACE, "send obj "DF_UOID" "DF_UUID" count %d\n",
		DP_UOID(oids[count]), DP_UUID(arg->current_uuid), count);

	D_ASSERT(root->count > 0);
	root->count--;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	/* Exist the loop, if there are enough objects to be sent */
	if (arg->count >= REBUILD_SEND_LIMIT)
		return 1;

	return 0;
}

static int
rebuild_cont_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		     daos_iov_t *val_iov, void *data)
{
	struct rebuild_root *root = val_iov->iov_buf;
	struct rebuild_send_arg *arg = data;
	int rc;

	uuid_copy(arg->current_uuid, *(uuid_t *)key_iov->iov_buf);
	rc = dbtree_iterate(root->root_hdl, false,
			    rebuild_obj_iter_cb, data);
	if (rc < 0)
		return rc;

	/* Exist the loop, if there are enough objects to be sent */
	if (arg->count >= REBUILD_SEND_LIMIT)
		return 1;

	/* Delete the current container tree */
	rc = dbtree_iter_delete(ih, NULL);
	if (rc != 0)
		return rc;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	return rc;
}

static int
ds_rebuild_objects_send(struct rebuild_root *root,
			unsigned int tgt_id, struct rebuild_scan_arg *scan_arg)
{
	struct rebuild_objs_in *rebuild_in;
	struct rebuild_send_arg *arg = NULL;
	daos_unit_oid_t		*oids = NULL;
	uuid_t			*uuids = NULL;
	unsigned int		*shards = NULL;
	crt_rpc_t		*rpc;
	crt_endpoint_t		tgt_ep;
	int			rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return -DER_NOMEM;

	D_ALLOC(oids, sizeof(*oids) * REBUILD_SEND_LIMIT);
	if (oids == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(uuids, sizeof(*uuids) * REBUILD_SEND_LIMIT);
	if (oids == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(shards, sizeof(*shards) * REBUILD_SEND_LIMIT);
	if (shards == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	arg->tgt_root = root;
	arg->count = 0;
	arg->oids = oids;
	arg->uuids = uuids;
	arg->shards = shards;
	ABT_mutex_lock(scan_arg->scan_lock);
	rc = dbtree_iterate(root->root_hdl, false, rebuild_cont_iter_cb, arg);
	ABT_mutex_unlock(scan_arg->scan_lock);
	if (rc < 0)
		D_GOTO(out, rc);

	if (arg->count == 0)
		D_GOTO(out, rc);

	D_DEBUG(DB_TRACE, "send rebuild objects "DF_UUID" to tgt %d count %d\n",
		DP_UUID(scan_arg->pool_uuid), tgt_id, arg->count);

	tgt_ep.ep_rank = tgt_id;
	tgt_ep.ep_tag = 0;
	rc = rebuild_req_create(dss_get_module_info()->dmi_ctx, tgt_ep,
				REBUILD_OBJECTS, &rpc);
	if (rc)
		D_GOTO(out, rc);

	rebuild_in = crt_req_get(rpc);
	rebuild_in->roi_map_ver = pool_map_get_version(scan_arg->map);
	rebuild_in->roi_oids.da_count = arg->count;
	rebuild_in->roi_oids.da_arrays = oids;
	rebuild_in->roi_uuids.da_count = arg->count;
	rebuild_in->roi_uuids.da_arrays = uuids;
	rebuild_in->roi_shards.da_count = arg->count;
	rebuild_in->roi_shards.da_arrays = shards;
	uuid_copy(rebuild_in->roi_pool_uuid, scan_arg->pool_uuid);

	/* Note: if the remote target fails, let's just ignore the
	 * this object list, because the later rebuild will rebuild
	 * it again anyway
	 **/
	rc = crt_req_send(rpc, ds_rebuild_obj_rpc_cb, arg);
	if (rc)
		D_GOTO(out, rc);

	return rc;
out:
	if (oids != NULL)
		D_FREE(oids, sizeof(*oids) * REBUILD_SEND_LIMIT);
	if (uuids != NULL)
		D_FREE(uuids, sizeof(*uuids) * REBUILD_SEND_LIMIT);
	if (shards != NULL)
		D_FREE(shards, sizeof(*shards) * REBUILD_SEND_LIMIT);
	if (arg != NULL)
		D_FREE_PTR(arg);

	return rc;
}

static int
rebuild_tgt_iter_cb(daos_handle_t ih, daos_iov_t *key_iov,
		    daos_iov_t *val_iov, void *data)
{
	struct rebuild_root *root;
	struct rebuild_scan_arg *arg = data;
	unsigned int tgt_id;
	unsigned int rc;

	tgt_id = *((unsigned int *)key_iov->iov_buf);
	root = val_iov->iov_buf;

	/* This is called when scanning is done, so the objects
	 * under each target should less than LIMIT. And also
	 * only 1 thread accessing the tree, so no need lock.
	 **/
	rc = ds_rebuild_objects_send(root, tgt_id, arg);
	if (rc < 0)
		return rc;

	rc = dbtree_destroy(root->root_hdl);
	if (rc)
		return rc;

	rc = dbtree_iter_delete(ih, NULL);
	if (rc)
		return rc;

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, NULL, NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	return rc;
}

/* Create rebuild tree root */
static int
ds_rebuild_tree_create(daos_handle_t toh, unsigned int tree_class,
		       void *key, daos_size_t key_size,
		       struct rebuild_root **rootp)
{
	daos_iov_t key_iov;
	daos_iov_t val_iov;
	struct umem_attr uma;
	struct rebuild_root root;
	struct btr_root	*broot;
	int rc;

	D_ALLOC_PTR(broot);
	if (broot == NULL)
		return -DER_NOMEM;

	root.root_hdl = DAOS_HDL_INVAL;
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;

	rc = dbtree_create_inplace(tree_class, 0, 4, &uma,
				   broot, &root.root_hdl);
	if (rc) {
		D_ERROR("failed to create rebuild tree: %d\n", rc);
		D_FREE_PTR(broot);
		D_GOTO(out, rc);
	}

	daos_iov_set(&key_iov, key, key_size);
	daos_iov_set(&val_iov, &root, sizeof(root));
	rc = dbtree_update(toh, &key_iov, &val_iov);
	if (rc)
		D_GOTO(out, rc);

	daos_iov_set(&val_iov, NULL, 0);
	rc = dbtree_lookup(toh, &key_iov, &val_iov);
	if (rc)
		D_GOTO(out, rc);

	*rootp = val_iov.iov_buf;
	D_ASSERT(*rootp != NULL);
out:
	if (rc < 0) {
		if (!daos_handle_is_inval(root.root_hdl))
			dbtree_destroy(root.root_hdl);
	}
	return rc;
}

/* Create target rebuild tree */
static int
ds_rebuild_tgt_tree_create(daos_handle_t toh, unsigned int tgt_id,
			   struct rebuild_root **rootp)
{
	return ds_rebuild_tree_create(toh, DBTREE_CLASS_UV,
				      &tgt_id, sizeof(tgt_id), rootp);
}

static int
ds_rebuild_uuid_tree_create(daos_handle_t toh, uuid_t uuid,
			    struct rebuild_root **rootp)
{
	return ds_rebuild_tree_create(toh, DBTREE_CLASS_NV,
				      uuid, sizeof(uuid_t), rootp);
}

int
ds_rebuild_cont_obj_insert(daos_handle_t toh, uuid_t co_uuid,
			   daos_unit_oid_t oid, unsigned int shard)
{
	struct rebuild_root *cont_root;
	daos_iov_t	key_iov;
	daos_iov_t	val_iov;
	int		rc;

	daos_iov_set(&key_iov, co_uuid, sizeof(uuid_t));
	daos_iov_set(&val_iov, NULL, 0);
	rc = dbtree_lookup(toh, &key_iov, &val_iov);
	if (rc < 0) {
		if (rc != -DER_NONEXIST)
			D_GOTO(out, rc);

		rc = ds_rebuild_uuid_tree_create(toh, co_uuid,
						 &cont_root);
		if (rc)
			D_GOTO(out, rc);
	} else {
		cont_root = val_iov.iov_buf;
	}

	oid.id_shard = shard;
	/* Finally look up the object under the container tree */
	daos_iov_set(&key_iov, &oid, sizeof(oid));
	daos_iov_set(&val_iov, &shard, sizeof(shard));
	rc = dbtree_lookup(cont_root->root_hdl, &key_iov, &val_iov);
	D_DEBUG(DB_TRACE, "lookup "DF_UOID" in cont "DF_UUID" rc %d\n",
		DP_UOID(oid), DP_UUID(co_uuid), rc);
	if (rc == -DER_NONEXIST) {
		rc = dbtree_update(cont_root->root_hdl, &key_iov, &val_iov);
		if (rc < 0) {
			D_ERROR("failed to insert "DF_UOID": rc %d\n",
				DP_UOID(oid), rc);
			D_GOTO(out, rc);
		}
		cont_root->count++;
		D_DEBUG(DB_TRACE, "update "DF_UOID" in cont_root %p count %d\n",
			DP_UOID(oid), cont_root, cont_root->count);
		return 1;
	}
out:
	return rc;
}

/**
 * The rebuild objects will be gathered into a global objects arrary by
 * target id.
 **/
static int
ds_rebuild_object_insert(struct rebuild_scan_arg *arg, unsigned int tgt_id,
			 unsigned int shard, uuid_t pool_uuid, uuid_t co_uuid,
			 daos_unit_oid_t oid)
{
	daos_iov_t key_iov;
	daos_iov_t val_iov;
	struct rebuild_root *tgt_root;
	daos_handle_t	toh = arg->rebuild_tree_hdl;
	int rc;

	/* look up the target tree */
	daos_iov_set(&key_iov, &tgt_id, sizeof(tgt_id));
	daos_iov_set(&val_iov, NULL, 0);
	ABT_mutex_lock(arg->scan_lock);
	rc = dbtree_lookup(toh, &key_iov, &val_iov);
	if (rc < 0) {
		/* Try to find the target rebuild tree */
		rc = ds_rebuild_tgt_tree_create(toh, tgt_id, &tgt_root);
		if (rc) {
			ABT_mutex_unlock(arg->scan_lock);
			D_GOTO(out, rc);
		}
	} else {
		tgt_root = val_iov.iov_buf;
	}

	rc = ds_rebuild_cont_obj_insert(tgt_root->root_hdl, co_uuid,
					oid, shard);
	if (rc <= 0) {
		ABT_mutex_unlock(arg->scan_lock);
		D_GOTO(out, rc);
	}

	if (rc == 1) {
		/* Check if there are enough objects under the target tree */
		if (++tgt_root->count >= REBUILD_SEND_LIMIT) {
			ABT_mutex_unlock(arg->scan_lock);
			rc = ds_rebuild_objects_send(tgt_root, tgt_id, arg);
			if (rc < 0)
				D_GOTO(out, rc);
		}
	} else {
		ABT_mutex_unlock(arg->scan_lock);
	}

	D_DEBUG(DB_TRACE, "insert "DF_UOID"/"DF_UUID" rebuild tgt %u rc %d\n",
		DP_UOID(oid), DP_UUID(co_uuid), tgt_id, rc);
out:
	return rc;
}

static int
placement_check(uuid_t co_uuid, daos_unit_oid_t oid, void *data)
{
	struct rebuild_scan_arg	*arg = data;
	struct pl_obj_layout	*layout = NULL;
	struct pl_map		*map = NULL;
	struct daos_obj_md	md;
	uint32_t		tgt_rebuild;
	unsigned int		shard_rebuild;
	int			rc;

	obj_fetch_md(oid.id_pub, &md, NULL);

	map = pl_map_find(DAOS_HDL_INVAL, oid.id_pub);
	if (map == NULL) {
		D_ERROR(DF_UOID"Cannot find valid placement map\n",
			DP_UOID(oid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_find_rebuild(map, &md, NULL, arg->tgp_failed,
				 &tgt_rebuild, &shard_rebuild);
	if (rc <= 0) /* No need rebuild */
		D_GOTO(out, rc);

	D_DEBUG(DB_PL, "rebuild obj "DF_UOID"/"DF_UUID"/"DF_UUID
		" on %d for shard %d\n", DP_UOID(oid), DP_UUID(co_uuid),
		DP_UUID(arg->pool_uuid), tgt_rebuild, shard_rebuild);

	rc = ds_rebuild_object_insert(arg, tgt_rebuild, shard_rebuild,
				      arg->pool_uuid, co_uuid, oid);
	if (rc)
		D_GOTO(out, rc);
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
	daos_handle_t			ph = DAOS_HDL_INVAL;
	struct rebuild_tls		*tls = rebuild_tls_get();
	int				rc;

	tls->rebuild_scanning = 1;
	map = ds_pool_get_pool_map(arg->pool_uuid);
	if (map == NULL)
		return -DER_INVAL;

	/* Create ds_pool locally */
	rc = ds_pool_local_open(arg->pool_uuid, pool_map_get_version(map),
				NULL);
	if (rc)
		return rc;

	uuid_copy(tls->rebuild_pool_uuid, arg->pool_uuid);

	/* Create ds_container locally */
	rc = ds_cont_local_open(arg->pool_uuid, arg->cont_hdl_uuid,
				NULL, 0, NULL);
	if (rc)
		return rc;
	uuid_copy(tls->rebuild_cont_hdl_uuid, arg->cont_hdl_uuid);

	/* Create dc_pool locally */
	rc = dc_pool_local_open(arg->pool_uuid, arg->pool_hdl_uuid,
				0, NULL, map, &ph);
	if (rc)
		return rc;

	uuid_copy(tls->rebuild_pool_hdl_uuid, arg->pool_hdl_uuid);
	tls->rebuild_pool_hdl = ph;

	return rc;
}

static int
ds_rebuild_prepare(uuid_t pool_uuid, uuid_t pool_hdl_uuid,
		   uuid_t cont_hdl_uuid)
{
	struct rebuild_cont_open_arg arg;
	struct pool_map		     *map;
	struct rebuild_tls	     *tls = rebuild_tls_get();
	unsigned int		     nthreads = dss_get_threads_number();
	int rc;

	map = ds_pool_get_pool_map(pool_uuid);
	if (map == NULL)
		return -DER_INVAL;

	/* Initialize the placement */
	rc = daos_placement_update(map);
	if (rc)
		return rc;

	if (tls->rebuild_building_nr < nthreads) {
		if (tls->rebuild_building != NULL)
			D_FREE(tls->rebuild_building,
			       tls->rebuild_building_nr *
			       sizeof(*tls->rebuild_building));
		D_ALLOC(tls->rebuild_building,
			nthreads * sizeof(*tls->rebuild_building));
		if (tls->rebuild_building == NULL)
			return -DER_NOMEM;
		tls->rebuild_building_nr = nthreads;
	} else {
		memset(tls->rebuild_building, 0,
		       tls->rebuild_building_nr *
		       sizeof(*tls->rebuild_building));
	}

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

static int
rebuild_scan_done(void *data)
{
	struct rebuild_tls *tls = rebuild_tls_get();

	tls->rebuild_scanning = 0;
	return 0;
}

static void
rebuild_scan_func(void *data)
{
	struct rebuild_iter_arg iter_arg;
	struct rebuild_scan_arg *arg = data;
	struct rebuild_tls *tls = rebuild_tls_get();
	int rc;

	D_ASSERT(arg != NULL);
	D_ASSERT(arg->tgp_failed != NULL);
	D_ASSERT(arg->map != NULL);
	D_ASSERT(!daos_handle_is_inval(arg->rebuild_tree_hdl));

	iter_arg.arg = arg;
	iter_arg.callback = placement_check;
	uuid_copy(iter_arg.pool_uuid, arg->pool_uuid);

	rc = dss_collective(rebuild_obj_iter, &iter_arg);
	if (rc)
		D_GOTO(free, rc);

	D_DEBUG(DB_TRACE, "rebuild scan collective "DF_UUID" is done\n",
		DP_UUID(arg->pool_uuid));

	/* walk through the rebuild tree and send the rebuild objects */
	rc = dbtree_iterate(arg->rebuild_tree_hdl, false, rebuild_tgt_iter_cb,
			    arg);

	dss_collective(rebuild_scan_done, NULL);
	if (rc) {
		D_ERROR(DF_UUID" send rebuild object list failed:%d\n",
			DP_UUID(arg->pool_uuid), rc);
		D_GOTO(free, rc);
	}

	D_DEBUG(DB_TRACE, DF_UUID" send objects to initiator %d\n",
		DP_UUID(arg->pool_uuid), rc);

free:
	if (arg->tgp_failed->tg_targets != NULL) {
		D_FREE(arg->tgp_failed->tg_targets,
		       arg->tgp_failed->tg_target_nr *
			       sizeof(*arg->tgp_failed->tg_targets));
		D_FREE_PTR(arg->tgp_failed);
	}

	if (!daos_handle_is_inval(arg->rebuild_tree_hdl))
		dbtree_destroy(arg->rebuild_tree_hdl);
	if (tls->rebuild_status == 0 && rc != 0)
		tls->rebuild_status = rc;

	ABT_mutex_free(&arg->scan_lock);
	D_FREE_PTR(arg);
}

/* Scan the local target and generate rebuild object list */
int
ds_rebuild_scan_handler(crt_rpc_t *rpc)
{
	struct rebuild_scan_in	*rsi;
	struct rebuild_out	*ro;
	struct pool_map		*map = NULL;
	struct rebuild_scan_arg	*arg = NULL;
	struct pl_target_grp	*pl_grp = NULL;
	struct umem_attr        uma;
	daos_handle_t           root_hdl = DAOS_HDL_INVAL;
	struct btr_root         *broot = NULL;
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
		pl_grp->tg_targets[i].pt_pos = target - pool_map_targets(map);
	}

	/* Create the btree root for global object scan list */
	D_ALLOC_PTR(broot);
	if (broot == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create_inplace(DBTREE_CLASS_NV, 0, 4, &uma,
				   broot, &root_hdl);
	if (rc != 0) {
		D_ERROR("failed to create rebuild tree: %d\n", rc);
		D_GOTO(free, rc);
	}

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		D_GOTO(free, rc = -DER_NOMEM);
	arg->rebuild_tree_hdl = root_hdl;
	arg->map = map;
	arg->tgp_failed = pl_grp;
	uuid_copy(arg->pool_uuid, rsi->rsi_pool_uuid);

	rc = ABT_mutex_create(&arg->scan_lock);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(free, rc);
	}

	rc = dss_ult_create(rebuild_scan_func, arg, -1, NULL);
	if (rc != 0)
		D_GOTO(free, rc);

	D_GOTO(out, rc);
free:
	if (arg != NULL) {
		ABT_mutex_free(&arg->scan_lock);
		D_FREE_PTR(arg);
	}
	if (pl_grp != NULL) {
		if (pl_grp->tg_targets != NULL)
			D_FREE(pl_grp->tg_targets,
			       pl_grp->tg_target_nr *
			       sizeof(*pl_grp->tg_targets));
		D_FREE_PTR(pl_grp);
	}

	if (!daos_handle_is_inval(root_hdl))
		dbtree_destroy(root_hdl);
out:
	ro = crt_reply_get(rpc);
	ro->ro_status = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	return rc;
}

int
ds_rebuild_tgt_handler(crt_rpc_t *rpc)
{
	struct rebuild_tgt_in	*rti;
	struct rebuild_out	*ro;
	int			rc;

	rti = crt_req_get(rpc);
	ro = crt_reply_get(rpc);

	rc = ds_rebuild(rti->rti_pool_uuid, rti->rti_failed_tgts);

	ro->ro_status = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	return rc;
}

struct rebuild_status {
	int scanning;
	int status;
};

int
dss_rebuild_check_scanning(void *arg)
{
	struct rebuild_tls	*tls = rebuild_tls_get();
	struct rebuild_status	*status = arg;

	if (tls->rebuild_scanning)
		status->scanning++;
	if (tls->rebuild_status != 0 && status->status == 0)
		status->status = tls->rebuild_status;
	return 0;
}

int
ds_rebuild_tgt_query_aggregator(crt_rpc_t *source, crt_rpc_t *result,
				void *priv)
{
	struct rebuild_tgt_query_out    *out_source = crt_reply_get(source);
	struct rebuild_tgt_query_out    *out_result = crt_reply_get(result);

	out_result->rtqo_rebuilding += out_source->rtqo_rebuilding;
	if (out_result->rtqo_status == 0 && out_source->rtqo_status != 0)
		out_result->rtqo_status = out_source->rtqo_status;
	return 0;
}

int
ds_rebuild_tgt_query_handler(crt_rpc_t *rpc)
{
	struct rebuild_tls		*tls = rebuild_tls_get();
	struct rebuild_tgt_query_out	*rtqo;
	struct rebuild_status		status;
	int				i;
	bool				rebuilding = false;
	int				rc;

	memset(&status, 0, sizeof(status));
	rtqo = crt_reply_get(rpc);
	rtqo->rtqo_rebuilding = 0;

	/* First let's checking building */
	for (i = 0; i < tls->rebuild_building_nr; i++) {
		if (tls->rebuild_building[i] > 0) {
			D_DEBUG(DB_TRACE, "thread %d still rebuilding\n", i);
			rebuilding = true;
		}
	}

	/* let's check status on every thread*/
	rc = dss_collective(dss_rebuild_check_scanning, &status);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_TRACE, "pool "DF_UUID" scanning %d/%d rebuilding %s\n",
		DP_UUID(tls->rebuild_pool_uuid), status.scanning,
		status.status, rebuilding ? "yes" : "no");

	if (!rebuilding && status.scanning > 0)
		rebuilding = true;

	if (rebuilding)
		rtqo->rtqo_rebuilding = 1;

	if (status.status != 0)
		rtqo->rtqo_status = status.status;
out:
	if (rtqo->rtqo_status == 0)
		rtqo->rtqo_status = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed %d\n", rc);

	return rc;
}

/* query the rebuild status */
int
ds_rebuild_query_handler(crt_rpc_t *rpc)
{
	struct rebuild_query_in		*rqi;
	struct rebuild_query_out	*rqo;
	struct rebuild_tgt_query_in	*rtqi;
	struct rebuild_tgt_query_out	*rtqo;
	struct ds_pool		*pool;
	crt_rpc_t		*tgt_rpc;
	int			rc;

	rqi = crt_req_get(rpc);
	rqo = crt_reply_get(rpc);

	rqo->rqo_done = 0;
	pool = ds_pool_lookup(rqi->rqi_pool_uuid);
	if (pool == NULL) {
		D_ERROR("can not find "DF_UUID" rc %d\n",
			DP_UUID(rqi->rqi_pool_uuid), -DER_NO_HDL);
		D_GOTO(out, rc = -DER_NO_HDL);
	}

	/* Then send rebuild RPC to all targets of the pool */
	rc = ds_pool_bcast_create(dss_get_module_info()->dmi_ctx,
				  pool, DAOS_REBUILD_MODULE,
				  REBUILD_TGT_QUERY, &tgt_rpc, NULL,
				  rqi->rqi_tgts_failed);
	if (rc != 0)
		D_GOTO(out_put, rc);

	rtqi = crt_req_get(tgt_rpc);
	uuid_copy(rtqi->rtqi_uuid, rqi->rqi_pool_uuid);
	rc = dss_rpc_send(tgt_rpc);
	if (rc != 0)
		D_GOTO(out_rpc, rc);

	rtqo = crt_reply_get(tgt_rpc);
	D_DEBUG(DB_TRACE, "%p query rebuild status %d\n", rtqo,
		rtqo->rtqo_rebuilding);
	if (rtqo->rtqo_rebuilding == 0)
		rqo->rqo_done = 1;

	if (rtqo->rtqo_status)
		rqo->rqo_status = rtqo->rtqo_status;
out_rpc:
	crt_req_decref(tgt_rpc);
out_put:
	ds_pool_put(pool);
out:
	if (rqo->rqo_status == 0)
		rqo->rqo_status = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: rc %d\n", rc);
	return rc;
}
