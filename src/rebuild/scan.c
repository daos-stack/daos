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
	struct pl_target_grp	*tgp_failed;
	daos_rank_list_t	*failed_ranks;
	struct ds_pool		*sa_pool;
	uuid_t			pool_uuid;
	uint32_t		map_ver;
	ABT_mutex		scan_lock;
};

static int
rebuild_obj_fill_buf(daos_handle_t ih, daos_iov_t *key_iov,
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

	D_ASSERT(root->count > 0);
	root->count--;

	D_DEBUG(DB_TRACE, "send oid/con "DF_UOID"/"DF_UUID" cnt %d left %d\n",
		DP_UOID(oids[count]), DP_UUID(arg->current_uuid), arg->count,
		root->count);

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

	while (!dbtree_is_empty(root->root_hdl)) {
		rc = dbtree_iterate(root->root_hdl, false, rebuild_obj_fill_buf,
				    data);
		if (rc < 0)
			return rc;

		/* Exist the loop, if there are enough objects to be sent */
		if (arg->count >= REBUILD_SEND_LIMIT)
			return 1;
	}

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
	int			rc = 0;

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

	while (!dbtree_is_empty(root->root_hdl)) {
		ABT_mutex_lock(scan_arg->scan_lock);
		rc = dbtree_iterate(root->root_hdl, false, rebuild_cont_iter_cb,
				    arg);
		ABT_mutex_unlock(scan_arg->scan_lock);
		if (rc < 0)
			D_GOTO(out, rc);

		if (arg->count >= REBUILD_SEND_LIMIT)
			break;
	}

	if (arg->count == 0)
		D_GOTO(out, rc);

#if 0 /* rework this part */
	{
	struct pool_map		*map;
	struct pool_target	*target;

	map = rebuild_pool_map_get();
	target = pool_map_find_target_by_rank(map, tgt_id);
	D_ASSERT(target);

	if (target->ta_comp.co_status == PO_COMP_ST_DOWN ||
	    target->ta_comp.co_status == PO_COMP_ST_DOWNOUT) {
		rebuild_pool_map_put(map);
		D_GOTO(out, rc = 0); /* ignore it */
	}
	rebuild_pool_map_put(map);
	}
#endif
	D_DEBUG(DB_TRACE, "send rebuild objects "DF_UUID" to tgt %d count %d\n",
		DP_UUID(scan_arg->pool_uuid), tgt_id, arg->count);

	tgt_ep.ep_rank = tgt_id;
	tgt_ep.ep_tag = 0;
	rc = rebuild_req_create(dss_get_module_info()->dmi_ctx, tgt_ep,
				REBUILD_OBJECTS, &rpc);
	if (rc)
		D_GOTO(out, rc);

	rebuild_in = crt_req_get(rpc);
	rebuild_in->roi_map_ver = scan_arg->map_ver;
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
	 */
	rc = dss_rpc_send(rpc);
	if (rc)
		D_GOTO(out, rc);
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

	/* Some one might insert new record to the tree let's reprobe */
	rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, key_iov, NULL);
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

	memset(&root, 0, sizeof(root));
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
		D_DEBUG(DB_TRACE, "update "DF_UOID"/"DF_UUID" in cont_root %p"
			" count %d\n", DP_UOID(oid), DP_UUID(co_uuid),
			cont_root, cont_root->count);
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
	ABT_mutex_unlock(arg->scan_lock);
	if (rc <= 0)
		D_GOTO(out, rc);

	if (rc == 1) {
		/* Check if we need send the object list */
		if (++tgt_root->count >= REBUILD_SEND_LIMIT) {
			rc = ds_rebuild_objects_send(tgt_root, tgt_id, arg);
			if (rc < 0)
				D_GOTO(out, rc);
		}
		rc = 0;
	}

	D_DEBUG(DB_TRACE, "insert "DF_UOID"/"DF_UUID" tgt %u cnt %d rc %d\n",
		DP_UOID(oid), DP_UUID(co_uuid), tgt_id, tgt_root->count, rc);
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

	if (rebuild_gst.rg_abort)
		return 1;

	obj_fetch_md(oid.id_pub, &md, NULL);
	while (1) {
		struct pool_map *poolmap;

		map = pl_map_find(DAOS_HDL_INVAL, oid.id_pub);
		if (map == NULL) {
			D_ERROR(DF_UOID"Cannot find valid placement map\n",
				DP_UOID(oid));
			D_GOTO(out, rc = -DER_INVAL);
		}

		poolmap = rebuild_pool_map_get();
		if (pl_map_version(map) >= pool_map_get_version(poolmap)) {
			/* very likely we just break out from here, unless
			 * there is a cascading failure.
			 */
			rebuild_pool_map_put(poolmap);
			break;
		}
		pl_map_decref(map);

		daos_placement_update(poolmap);
		rebuild_pool_map_put(poolmap);
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

struct rebuild_prepare_arg {
	uuid_t	pool_uuid;
	uuid_t  pool_hdl_uuid;
	uuid_t	cont_hdl_uuid;
	daos_rank_list_t *svc_list;
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
	struct rebuild_prepare_arg	*arg = data;
	struct rebuild_tls		*tls = rebuild_tls_get();
	int				rc;

	tls->rebuild_scanning = 1;
	tls->rebuild_rec_count = 0;
	tls->rebuild_obj_count = 0;

	/* Create ds_container locally */
	rc = ds_cont_local_open(arg->pool_uuid, arg->cont_hdl_uuid,
				NULL, 0, NULL);
	if (rc)
		return rc;

	uuid_copy(tls->rebuild_cont_hdl_uuid, arg->cont_hdl_uuid);
	uuid_copy(tls->rebuild_pool_hdl_uuid, arg->pool_hdl_uuid);
	daos_rank_list_free(tls->rebuild_svc_list);
	daos_rank_list_dup(&tls->rebuild_svc_list, arg->svc_list, true);

	return 0;
}

struct rebuild_iter_arg {
	uuid_t		pool_uuid;
	cont_iter_cb_t	callback;
	void		*arg;
};

int
rebuild_scanner(void *data)
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

/**
 * Wait for pool map and setup global status, then spawn scanners for all
 * service xsteams
 */
static void
rebuild_scan_leader(void *data)
{
	struct rebuild_tls	  *tls = rebuild_tls_get();
	struct rebuild_scan_arg	  *arg = data;
	struct pl_target_grp	  *tgp;
	struct ds_pool		  *pool;
	struct pool_map		  *map;
	struct rebuild_iter_arg    iter_arg;
	int			   i;
	int			   rc;

	D_ASSERT(arg != NULL);
	D_ASSERT(arg->failed_ranks);
	D_ASSERT(!daos_handle_is_inval(arg->rebuild_tree_hdl));

	/* global barrier: wait until the pool map is ready */
	pool = arg->sa_pool;

	ABT_rwlock_rdlock(pool->sp_lock);
	while (pool->sp_map == NULL || pool->sp_map_version < arg->map_ver ||
	       pool_map_get_version(pool->sp_map) < arg->map_ver) {
		ABT_rwlock_unlock(pool->sp_lock);

		D_DEBUG(DB_TRACE, "Waiting for pool=%p, map_ver=%u, cur=%u\n",
			pool, arg->map_ver, pool->sp_map_version);
		ABT_mutex_lock(pool->sp_map_lock);
		ABT_cond_wait(pool->sp_map_cond, pool->sp_map_lock);
		ABT_mutex_unlock(pool->sp_map_lock);

		ABT_rwlock_rdlock(pool->sp_lock);
	}
	ABT_rwlock_unlock(pool->sp_lock);

	ABT_mutex_lock(rebuild_gst.rg_lock);
	while (rebuild_gst.rg_leader && rebuild_gst.rg_leader_barrier) {
		D_DEBUG(DB_TRACE, "Leader is waiting for other targets\n");
		ABT_cond_wait(rebuild_gst.rg_cond, rebuild_gst.rg_lock);
	}

	D_ASSERT(!rebuild_gst.rg_pool);
	rebuild_gst.rg_pool = pool; /* pin it */
	pool = NULL;

	/* refresh placement for the server stack */
	map = rebuild_pool_map_get();
	rc = daos_placement_update(map);
	if (rc != 0) {
		ABT_mutex_unlock(rebuild_gst.rg_lock);
		D_GOTO(out_map, rc = -DER_NOMEM);
	}

	/* local barrier: the puller may have already been started by another
	 * server, but hte puller should be waiting for the pool being pinned
	 * and pool map being updated, so this is the place to notify him.
	 */
	ABT_cond_broadcast(rebuild_gst.rg_cond);
	ABT_mutex_unlock(rebuild_gst.rg_lock);

	/* initialize parameters for scanners */
	D_ALLOC_PTR(tgp);
	if (tgp == NULL)
		D_GOTO(out_map, rc = -DER_NOMEM);

	arg->tgp_failed = tgp;
	tgp->tg_ver = arg->map_ver;
	tgp->tg_target_nr = arg->failed_ranks->rl_nr.num;

	D_ALLOC(tgp->tg_targets, tgp->tg_target_nr * sizeof(*tgp->tg_targets));
	if (tgp->tg_targets == NULL)
		D_GOTO(out_group, rc = -DER_NOMEM);

	for (i = 0; i < arg->failed_ranks->rl_nr.num; i++) {
		struct pool_target      *target;
		daos_rank_t		 rank;

		rank = arg->failed_ranks->rl_ranks[i];
		target = pool_map_find_target_by_rank(map, rank);
		if (target == NULL) {
			D_ERROR("Nonexistent target rank=%d\n", rank);
			D_GOTO(out_group, rc = -DER_NONEXIST);
		}

		tgp->tg_targets[i].pt_pos = target - pool_map_targets(map);
	}

	iter_arg.arg = arg;
	iter_arg.callback = placement_check;
	uuid_copy(iter_arg.pool_uuid, arg->pool_uuid);

	rc = dss_collective(rebuild_scanner, &iter_arg);
	if (rc)
		D_GOTO(out_group, rc);

	D_DEBUG(DB_TRACE, "rebuild scan collective "DF_UUID" is done\n",
		DP_UUID(arg->pool_uuid));

	while (!dbtree_is_empty(arg->rebuild_tree_hdl)) {
		/* walk through the rebuild tree and send the rebuild objects */
		rc = dbtree_iterate(arg->rebuild_tree_hdl, false,
				    rebuild_tgt_iter_cb, arg);
		if (rc)
			D_GOTO(out_group, rc);
	}

	ABT_mutex_lock(rebuild_gst.rg_lock);
	rc = dss_collective(rebuild_scan_done, NULL);
	ABT_mutex_unlock(rebuild_gst.rg_lock);
	if (rc) {
		D_ERROR(DF_UUID" send rebuild object list failed:%d\n",
			DP_UUID(arg->pool_uuid), rc);
		D_GOTO(out_group, rc);
	}

	D_DEBUG(DB_TRACE, DF_UUID" sent objects to initiator %d\n",
		DP_UUID(arg->pool_uuid), rc);
	D_EXIT;
out_group:
	if (tgp->tg_targets != NULL) {
		D_FREE(tgp->tg_targets,
		       tgp->tg_target_nr * sizeof(tgp->tg_targets));
	}
	D_FREE_PTR(tgp);
out_map:
	rebuild_pool_map_put(map);
	daos_rank_list_free(arg->failed_ranks);
	dbtree_destroy(arg->rebuild_tree_hdl);
	if (tls->rebuild_status == 0 && rc != 0)
		tls->rebuild_status = rc;

	if (pool)
		ds_pool_put(pool);

	ABT_mutex_free(&arg->scan_lock);
	D_FREE_PTR(arg);
}

/* Scan the local target and generate rebuild object list */
int
ds_rebuild_scan_handler(crt_rpc_t *rpc)
{
	struct rebuild_scan_in		*rsi;
	struct rebuild_out		*ro;
	struct rebuild_scan_arg		*scan_arg;
	struct ds_pool			*pool;
	struct rebuild_prepare_arg	 prep_arg;
	struct ds_pool_create_arg	 pcrt_arg;
	struct umem_attr		 uma;
	int				 rc;

	rsi = crt_req_get(rpc);
	D_ASSERT(rsi != NULL);

	D_DEBUG(DB_TRACE, "%d scan rebuild for "DF_UUID" ver %d\n",
		dss_get_module_info()->dmi_tid, DP_UUID(rsi->rsi_pool_uuid),
		rsi->rsi_pool_map_ver);

	/* step-1: reset counters etc */
	D_ASSERT(rebuild_gst.rg_pullers);
	rebuild_gst.rg_last_ver = 0;
	rebuild_gst.rg_puller_total = 0;
	memset(rebuild_gst.rg_pullers, 0,
	       rebuild_gst.rg_puller_nxs * sizeof(*rebuild_gst.rg_pullers));
	uuid_copy(rebuild_gst.rg_pool_uuid, rsi->rsi_pool_uuid);

	memset(&pcrt_arg, 0, sizeof(pcrt_arg));

	/* step-2: load the pool to be rebuilt */
	pcrt_arg.pca_map_version = rsi->rsi_pool_map_ver;
	rc = ds_pool_lookup_create(rsi->rsi_pool_uuid, &pcrt_arg, &pool);
	if (rc != 0)
		D_GOTO(out, rc);

	/* step-3: parameters for scanner */
	D_ALLOC_PTR(scan_arg);
	if (scan_arg == NULL)
		D_GOTO(out_pool, rc = -DER_NOMEM);

	scan_arg->sa_pool = pool;
	scan_arg->map_ver = rsi->rsi_pool_map_ver;
	uuid_copy(scan_arg->pool_uuid, rsi->rsi_pool_uuid);
	rc = ABT_mutex_create(&scan_arg->scan_lock);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		D_GOTO(out_arg, rc);
	}

	/* Create the btree root for global object scan list */
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create(DBTREE_CLASS_NV, 0, 4, &uma, NULL,
			   &scan_arg->rebuild_tree_hdl);
	if (rc != 0) {
		D_ERROR("failed to create rebuild tree: %d\n", rc);
		D_GOTO(out_lock, rc);
	}

	rc = daos_rank_list_dup(&scan_arg->failed_ranks,
				rsi->rsi_tgts_failed, true);
	if (rc != 0)
		D_GOTO(out_tree, rc);

	memset(&prep_arg, 0, sizeof(prep_arg));
	uuid_copy(prep_arg.pool_uuid, rsi->rsi_pool_uuid);
	uuid_copy(prep_arg.pool_hdl_uuid, rsi->rsi_rebuild_pool_hdl_uuid);
	uuid_copy(prep_arg.cont_hdl_uuid, rsi->rsi_rebuild_cont_hdl_uuid);
	prep_arg.svc_list = rsi->rsi_svc_list;

	/* step-4: all xstreams initialize context for build, at this point,
	 * this server is ready for contributing data for the rebuild, but
	 * not ready for scanning and pulling so far.
	 */
	rc = dss_collective(rebuild_prepare_one, &prep_arg);
	D_ASSERT(rc == 0); /* XXX */

	/* step-5: start scann leader */
	rc = dss_ult_create(rebuild_scan_leader, scan_arg, -1, NULL);
	D_ASSERT(rc == 0); /* XXX */

	D_GOTO(out, rc);
out_tree:
	dbtree_destroy(scan_arg->rebuild_tree_hdl);
out_lock:
	ABT_mutex_free(&scan_arg->scan_lock);
out_arg:
	D_FREE_PTR(scan_arg);
out_pool:
	ds_pool_put(pool);
out:
	ro = crt_reply_get(rpc);
	ro->ro_status = rc;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	return rc;
}
