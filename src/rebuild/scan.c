/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(rebuild)

#include <daos_srv/pool.h>

#include <daos/btree_class.h>
#include <daos/pool_map.h>
#include <daos/pool.h>
#include <daos/rpc.h>
#include <daos/placement.h>
#include <daos_srv/container.h>
#include <daos_srv/daos_mgmt_srv.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include "rpc.h"
#include "rebuild_internal.h"

#define REBUILD_SEND_LIMIT	512
struct rebuild_send_arg {
	struct rebuild_tgt_pool_tracker *rpt;
	daos_unit_oid_t			*oids;
	daos_epoch_t			*ephs;
	uuid_t				cont_uuid;
	unsigned int			*shards;
	int				count;
	int				tgt_id;
};

struct rebuild_obj_val {
	daos_epoch_t    eph;
	uint32_t	shard;
	uint32_t        tgt_id;
};

static int
rebuild_obj_fill_buf(daos_handle_t ih, d_iov_t *key_iov,
		     d_iov_t *val_iov, void *data)
{
	struct rebuild_send_arg *arg = data;
	daos_unit_oid_t		*oids = arg->oids;
	daos_epoch_t		*ephs = arg->ephs;
	daos_unit_oid_t		*oid = key_iov->iov_buf;
	struct rebuild_obj_val	*obj_val = val_iov->iov_buf;
	unsigned int		*shards = arg->shards;
	int			count = arg->count;
	int			rc;

	if (arg->tgt_id != -1 && arg->tgt_id != obj_val->tgt_id) {
		D_DEBUG(DB_REBUILD, "Current tgt id %d, entry id %d\n",
			arg->tgt_id, obj_val->tgt_id);
		return 0;
	}

	arg->tgt_id = obj_val->tgt_id;
	D_ASSERT(count < REBUILD_SEND_LIMIT);
	oids[count] = *oid;
	ephs[count] = obj_val->eph;
	shards[count] = obj_val->shard;
	arg->count++;

	rc = dbtree_iter_delete(ih, NULL);
	if (rc != 0)
		return rc;

	D_DEBUG(DB_REBUILD, "send oid/con "DF_UOID"/"DF_UUID" ephs "DF_U64
		"shard %d cnt %d tgt_id %d\n", DP_UOID(oids[count]),
		DP_UUID(arg->cont_uuid), obj_val->eph, shards[count],
		arg->count, arg->tgt_id);

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_MIGRATION, NULL,
			       NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	/* Exist the loop, if there are enough objects to be sent */
	if (arg->count >= REBUILD_SEND_LIMIT)
		return 1;

	return 0;
}

static int
rebuild_obj_send_cb(struct tree_cache_root *root, struct rebuild_send_arg *arg)
{
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	int				rc;

	/* re-init the send argument to fill the object buffer */
	arg->count = 0;
	arg->tgt_id = -1;
	rc = dbtree_iterate(root->root_hdl, DAOS_INTENT_MIGRATION, false,
			    rebuild_obj_fill_buf, arg);
	if (rc < 0 || arg->count == 0) {
		D_DEBUG(DB_REBUILD, "Can not get objects: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (daos_fail_check(DAOS_REBUILD_TGT_SEND_OBJS_FAIL))
		D_GOTO(out, rc = -DER_IO);

	D_DEBUG(DB_REBUILD, "send rebuild objects "DF_UUID" to tgt %d"
		" cnt %d\n", DP_UUID(rpt->rt_pool_uuid), arg->tgt_id,
		arg->count);
	while (1) {
		rc = ds_object_migrate(rpt->rt_pool, rpt->rt_poh_uuid,
				       rpt->rt_coh_uuid, arg->cont_uuid,
				       arg->tgt_id, rpt->rt_rebuild_ver,
				       rpt->rt_stable_epoch, arg->oids,
				       arg->ephs, arg->shards, arg->count,
				       /* Clear containers for reint */
				       rpt->rt_rebuild_op == RB_OP_REINT);
		/* If it does not need retry */
		if (rc == 0 || (rc != -DER_TIMEDOUT && rc != -DER_GRPVER &&
		    rc != -DER_AGAIN && !daos_crt_network_error(rc)))
			break;

		/* otherwise let's retry */
		D_DEBUG(DB_REBUILD, DF_UUID" retry send object to tgt_id %d\n",
			DP_UUID(rpt->rt_pool_uuid), arg->tgt_id);
		ABT_thread_yield();
	}
out:
	return rc;
}

static int
rebuild_cont_send_cb(daos_handle_t ih, d_iov_t *key_iov,
		     d_iov_t *val_iov, void *data)
{
	struct rebuild_send_arg	*arg = data;
	struct tree_cache_root	*root;
	int			rc;

	uuid_copy(arg->cont_uuid, *(uuid_t *)key_iov->iov_buf);
	root = val_iov->iov_buf;
	while (!dbtree_is_empty(root->root_hdl)) {
		rc = rebuild_obj_send_cb(root, arg);
		if (rc < 0) {
			D_ERROR("rebuild_obj_send_cb failed: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}
	}

	rc = dbtree_destroy(root->root_hdl, NULL);
	if (rc) {
		D_ERROR("dbtree_destroy failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Some one might insert new record to the tree let's reprobe */
	rc = dbtree_iter_probe(ih, BTR_PROBE_EQ, DAOS_INTENT_MIGRATION, key_iov,
			       NULL);
	if (rc) {
		D_ERROR("dbtree_iter_probe failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	rc = dbtree_iter_delete(ih, NULL);
	if (rc) {
		D_ERROR("dbtree_iter_delete failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* re-probe the dbtree after delete */
	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_MIGRATION, NULL,
			       NULL);
	if (rc == -DER_NONEXIST)
		return 1;

	return rc;
}

static void
rebuild_objects_send_ult(void *data)
{
	struct rebuild_send_arg		arg = { 0 };
	struct rebuild_tgt_pool_tracker *rpt = data;
	struct rebuild_pool_tls		*tls;
	daos_unit_oid_t			*oids = NULL;
	daos_epoch_t			*ephs = NULL;
	unsigned int			*shards = NULL;
	int				rc = 0;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	D_ALLOC_ARRAY(oids, REBUILD_SEND_LIMIT);
	if (oids == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(shards, REBUILD_SEND_LIMIT);
	if (shards == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(ephs, REBUILD_SEND_LIMIT);
	if (ephs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	arg.count = 0;
	arg.oids = oids;
	arg.shards = shards;
	arg.ephs = ephs;
	arg.rpt = rpt;
	while (!tls->rebuild_pool_scan_done ||
	       !dbtree_is_empty(tls->rebuild_tree_hdl)) {
		if (rpt->rt_stable_epoch == 0) {
			ABT_thread_yield();
			continue;
		}

		/* walk through the rebuild tree and send the rebuild objects */
		rc = dbtree_iterate(tls->rebuild_tree_hdl,
				    DAOS_INTENT_MIGRATION,
				    false, rebuild_cont_send_cb, &arg);
		if (rc < 0) {
			D_ERROR("dbtree iterate failed: rc "DF_RC"\n",
				DP_RC(rc));
			break;
		}
		ABT_thread_yield();
	}

	D_DEBUG(DB_REBUILD, DF_UUID"/%d objects send finish\n",
		DP_UUID(rpt->rt_pool_uuid), rpt->rt_rebuild_ver);
out:
	if (oids != NULL)
		D_FREE(oids);
	if (shards != NULL)
		D_FREE(shards);
	if (ephs != NULL)
		D_FREE(ephs);
	if (rc != 0 && tls->rebuild_pool_status == 0)
		tls->rebuild_pool_status = rc;

	rpt_put(rpt);
}

static int
rebuild_scan_done(void *data)
{
	struct rebuild_tgt_pool_tracker *rpt = data;
	struct rebuild_pool_tls *tls;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	tls->rebuild_pool_scanning = 0;
	return 0;
}

/**
 * The rebuild objects will be gathered into a global objects array by
 * target id.
 **/
static int
rebuild_object_insert(struct rebuild_tgt_pool_tracker *rpt,
		      unsigned int tgt_id, unsigned int shard, uuid_t co_uuid,
		      daos_unit_oid_t oid, daos_epoch_t epoch)
{
	struct rebuild_pool_tls *tls;
	struct rebuild_obj_val	val;
	d_iov_t			val_iov;
	int			rc;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	D_ASSERT(!daos_handle_is_inval(tls->rebuild_tree_hdl));

	tls->rebuild_pool_obj_count++;
	val.eph = epoch;
	val.shard = shard;
	val.tgt_id = tgt_id;
	d_iov_set(&val_iov, &val, sizeof(struct rebuild_obj_val));
	oid.id_shard = shard; /* Convert the OID to rebuilt one */
	rc = obj_tree_insert(tls->rebuild_tree_hdl, co_uuid, oid, &val_iov);
	D_DEBUG(DB_REBUILD, "insert "DF_UOID"/"DF_UUID" tgt %u rc %d\n",
		DP_UOID(oid), DP_UUID(co_uuid), tgt_id, rc);

	return rc;
}

#define LOCAL_ARRAY_SIZE	128
#define NUM_SHARDS_STEP_INCREASE	10
/* The structure for scan per xstream */
struct rebuild_scan_arg {
	struct rebuild_tgt_pool_tracker *rpt;
	uuid_t				co_uuid;
	uint32_t			yield_freq;
};

/**
 * Invoke placement to find the object shards that need rebuilding
 *
 * This is an optimized function - it first tries to use the provided stack
 * arrays to avoid allocating buffers to fill.
 *
 * It's possible that placement might return -DER_REC2BIG, in which case a
 * larger buffer will be allocated and the request repeated until it succeeds.
 *
 * \param[in]	tgt_stack_array		stack buffer of size LOCAL_ARRAY_SIZE
 *					to use if it is big enough
 * \param[in]	shard_stack_array	stack buffer of size LOCAL_ARRAY_SIZE
 *					to use if it is big enough
 * \param[out]	tgts			filled remap list, caller must free if
 *					it does not point to tgt_stack_array
 * \param[out]	shards			filled remap list, caller must free if
 *					it does not point to shard_stack_array
 * \param[in]	map			placement map
 * \param[in]	md			object metadata
 * \param[in]	num_rebuild_tgts	the number of targets being rebuilt now
 * \param[in]	rebuild_op		the rebuild operation
 * \param[in]	rebuild_ver		the rebuild version
 * \param[in]	myrank			this system's rank
 * \param[out]	rebuild_nr		number of filled entries in tgts and
 *					shards that need remapping
 *
 * \retval	0	Success
 * \retval	< 0	-DER_* error. Will not return -DER_REC2BIG
 */
static int
find_rebuild_shards(unsigned int *tgt_stack_array,
		    unsigned int *shard_stack_array,
		    unsigned int **tgts, unsigned int **shards,
		    struct pl_map *map, struct daos_obj_md *md,
		    uint32_t num_rebuild_tgts,
		    daos_rebuild_opc_t rebuild_op,
		    uint32_t rebuild_ver,
		    d_rank_t myrank,
		    int *rebuild_nr)
{
	uint32_t max_shards;
	int rc = 0;

	/*
	 * Compute the starting buffer space allocated for remapped shards.
	 * Use the larger of num_rebuild_tgts or LOCAL_ARRAY_SIZE
	 * There is no point in using anything less than LOCAL_ARRAY_SIZE since
	 * it was already allocated (on the stack)
	 */
	max_shards = num_rebuild_tgts > LOCAL_ARRAY_SIZE ? num_rebuild_tgts :
							   LOCAL_ARRAY_SIZE;

	/* Default to using the provided stack arrays */
	*tgts = tgt_stack_array;
	*shards = shard_stack_array;

	do {
		if (rc == -DER_REC2BIG) {
			/*
			 * The last attempt failed because there was not enough
			 * room for all the remapped shards.
			 *
			 * Need to allocate more space and try again
			 */

			/*
			 * Free any non-stack buffers that were allocated
			 * on the last attempt
			 */
			if (*tgts != tgt_stack_array && *tgts != NULL)
				D_FREE(*tgts);
			if (*shards != shard_stack_array && *shards != NULL)
				D_FREE(*shards);

			/* Increase by the step size */
			max_shards += NUM_SHARDS_STEP_INCREASE;

			D_DEBUG(DB_REBUILD, "Got REC2BIG, increasing rebuild "
					    "array size by %u to %u",
					    NUM_SHARDS_STEP_INCREASE,
					    max_shards);
		}

		/*
		 * If the amount of space is too big for the stack arrays,
		 * allocate some space
		 */
		if (max_shards > LOCAL_ARRAY_SIZE) {
			D_DEBUG(DB_REBUILD, "Insufficient stack buffer to find "
					    "rebuild shards, allocating %u\n",
				max_shards);
			D_ALLOC_ARRAY(*tgts, max_shards);
			D_ALLOC_ARRAY(*shards, max_shards);
			if (*tgts == NULL || *shards == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		if (rebuild_op == RB_OP_FAIL) {
			rc = pl_obj_find_rebuild(map, md, NULL,
						 rebuild_ver,
						 *tgts, *shards,
						 max_shards);
		} else if (rebuild_op == RB_OP_DRAIN) {
			rc = pl_obj_find_rebuild(map, md, NULL,
						 rebuild_ver,
						 *tgts, *shards,
						 max_shards);
		} else if (rebuild_op == RB_OP_REINT) {
			rc = pl_obj_find_reint(map, md, NULL,
					       rebuild_ver,
					       *tgts, *shards,
					       max_shards);
		} else if (rebuild_op == RB_OP_EXTEND) {
			rc = pl_obj_find_addition(map, md, NULL,
						  rebuild_ver,
						  *tgts, *shards,
						  max_shards);
		} else {
			D_ASSERT(rebuild_op == RB_OP_FAIL ||
				 rebuild_op == RB_OP_DRAIN ||
				 rebuild_op == RB_OP_REINT ||
				 rebuild_op == RB_OP_EXTEND);
		}
	} while (rc == -DER_REC2BIG); /* Retry and increase space on -REC2BIG */

	if (rc > 0)  {
		/* Successfully filled the rebuild lists */
		*rebuild_nr = rc;
		rc = 0;
		D_ASSERT(*rebuild_nr < max_shards);
	}

out:
	if (rc != 0) {
		/* Failed - clean up arrays if they were allocated */
		if (*tgts != tgt_stack_array && *tgts != NULL)
			D_FREE(*tgts);
		if (*shards != shard_stack_array && *shards != NULL)
			D_FREE(*shards);
	}

	return rc;
}

static int
rebuild_obj_scan_cb(daos_handle_t ch, vos_iter_entry_t *ent,
		    vos_iter_type_t type, vos_iter_param_t *param,
		    void *data, unsigned *acts)
{
	struct rebuild_scan_arg		*arg = data;
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	struct pl_map			*map = NULL;
	struct daos_obj_md		md;
	daos_unit_oid_t			oid = ent->ie_oid;
	unsigned int			tgt_array[LOCAL_ARRAY_SIZE];
	unsigned int			shard_array[LOCAL_ARRAY_SIZE];
	unsigned int			*tgts = NULL;
	unsigned int			*shards = NULL;
	int				rebuild_nr = 0;
	d_rank_t			myrank;
	int				i;
	int				rc = 0;

	if (rpt->rt_abort) {
		D_DEBUG(DB_REBUILD, "rebuild is aborted\n");
		return 1;
	}

	if (--arg->yield_freq == 0) {
		arg->yield_freq = DEFAULT_YIELD_FREQ;
		ABT_thread_yield();
		*acts |= VOS_ITER_CB_YIELD;
		return 0;
	}

	map = pl_map_find(rpt->rt_pool_uuid, oid.id_pub);
	if (map == NULL) {
		D_ERROR(DF_UOID"Cannot find valid placement map"
			DF_UUID"\n", DP_UOID(oid),
			DP_UUID(rpt->rt_pool_uuid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	dc_obj_fetch_md(oid.id_pub, &md);
	crt_group_rank(rpt->rt_pool->sp_group, &myrank);
	md.omd_ver = rpt->rt_rebuild_ver;

	if (rpt->rt_rebuild_op == RB_OP_FAIL ||
	    rpt->rt_rebuild_op == RB_OP_DRAIN ||
	    rpt->rt_rebuild_op == RB_OP_REINT ||
	    rpt->rt_rebuild_op == RB_OP_EXTEND) {
		/*
		 * Use the appropriate placement algorithm to compute which
		 * shards need to be sent to which targets
		 */
		rc = find_rebuild_shards(tgt_array, shard_array, &tgts, &shards,
					 map, &md, rpt->rt_tgts_num,
					 rpt->rt_rebuild_op,
					 rpt->rt_rebuild_ver,
					 myrank,
					 &rebuild_nr);
		if (rc != 0) {
			D_ERROR("Failed to find rebuild shards for object "
				DF_UOID": "DF_RC,
				DP_UOID(oid), DP_RC(rc));
			D_GOTO(out, rc);
		}
	} else {
		D_ASSERT(rpt->rt_rebuild_op == RB_OP_FAIL ||
			 rpt->rt_rebuild_op == RB_OP_DRAIN ||
			 rpt->rt_rebuild_op == RB_OP_REINT ||
			 rpt->rt_rebuild_op == RB_OP_EXTEND);
	}
	if (rebuild_nr <= 0) /* No need rebuild */
		D_GOTO(out, rc = rebuild_nr);

	for (i = 0; i < rebuild_nr; i++) {
		struct pool_target *target;

		D_DEBUG(DB_REBUILD, "rebuild obj "DF_UOID"/"DF_UUID"/"DF_UUID
			" on %d for shard %d\n", DP_UOID(oid),
			DP_UUID(rpt->rt_pool_uuid), DP_UUID(arg->co_uuid),
			tgts[i], shards[i]);

		rc = pool_map_find_target(map->pl_poolmap, tgts[i], &target);
		D_ASSERT(rc == 1);

		/* During rebuild test, it will manually exclude some target to
		 * trigger the rebuild, then later add it back, so some objects
		 * might exist on some illegal target, so they might use its
		 * "own" target as the spare target, let's skip these object
		 * now. When we have better support from CART exclude/addback,
		 * myrank should always not equal to tgt_rebuild. XXX
		 */
		if (myrank != target->ta_comp.co_rank) {
			rc = rebuild_object_insert(rpt, tgts[i], shards[i],
						   arg->co_uuid,
						   oid, ent->ie_epoch);
			if (rc)
				D_GOTO(out, rc);
		} else {
			D_DEBUG(DB_REBUILD, "rebuild skip "DF_UOID".\n",
				DP_UOID(oid));
			rc = 0;
		}
	}

out:
	if (tgts != tgt_array && tgts != NULL)
		D_FREE(tgts);

	if (shards != shard_array && shards != NULL)
		D_FREE(shards);

	if (map != NULL)
		pl_map_decref(map);

	return rc;
}

static int
rebuild_container_scan_cb(daos_handle_t ih, vos_iter_entry_t *entry,
			  vos_iter_type_t type, vos_iter_param_t *iter_param,
			  void *data, unsigned *acts)
{
	struct rebuild_scan_arg		*arg = data;
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	vos_iter_param_t		param = { 0 };
	struct vos_iter_anchors		anchor = { 0 };
	daos_handle_t			coh;
	int				rc;

	if (uuid_compare(arg->co_uuid, entry->ie_couuid) == 0) {
		D_DEBUG(DB_REBUILD, DF_UUID" already scan\n",
			DP_UUID(arg->co_uuid));
		return 0;
	}

	rc = vos_cont_open(iter_param->ip_hdl, entry->ie_couuid, &coh);
	if (rc != 0) {
		D_ERROR("Open container "DF_UUID" failed: rc = %d\n",
			DP_UUID(entry->ie_couuid), rc);
		return rc;
	}

	memset(&param, 0, sizeof(param));
	param.ip_hdl = coh;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags = VOS_IT_FOR_MIGRATION;
	uuid_copy(arg->co_uuid, entry->ie_couuid);
	rc = vos_iterate(&param, VOS_ITER_OBJ, false, &anchor,
			 rebuild_obj_scan_cb, NULL, arg, NULL);
	vos_cont_close(coh);

	*acts |= VOS_ITER_CB_YIELD;
	D_DEBUG(DB_TRACE, DF_UUID"/"DF_UUID" iterate cont done: rc %d\n",
		DP_UUID(rpt->rt_pool_uuid), DP_UUID(entry->ie_couuid), rc);

	return rc;
}

int
rebuild_scanner(void *data)
{
	struct rebuild_scan_arg		arg = { 0 };
	struct rebuild_tgt_pool_tracker *rpt = data;
	struct ds_pool_child		*child;
	struct rebuild_pool_tls		*tls;
	vos_iter_param_t		param = { 0 };
	struct vos_iter_anchors		anchor = { 0 };
	ABT_thread			ult_send = ABT_THREAD_NULL;
	struct umem_attr		uma;
	int				rc;

	if (rebuild_status_match(rpt, PO_COMP_ST_DOWNOUT |
				      PO_COMP_ST_DOWN |
				      PO_COMP_ST_NEW) ||
	    (!rebuild_status_match(rpt, PO_COMP_ST_DRAIN) &&
	     rpt->rt_rebuild_op == RB_OP_DRAIN)) {
		D_DEBUG(DB_TRACE, DF_UUID" skip scan\n",
			DP_UUID(rpt->rt_pool_uuid));
		return 0;
	}

	while (daos_fail_check(DAOS_REBUILD_TGT_SCAN_HANG)) {
		D_DEBUG(DB_REBUILD, "sleep 2 seconds then retry\n");
		dss_sleep(2 * 1000);
	}

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);

	D_ASSERT(daos_handle_is_inval(tls->rebuild_tree_hdl));
	/* Create object tree root */
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create(DBTREE_CLASS_NV, 0, 4, &uma, NULL,
			   &tls->rebuild_tree_hdl);
	if (rc != 0) {
		D_ERROR("failed to create rebuild tree: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rpt_get(rpt);
	rc = dss_ult_create(rebuild_objects_send_ult, rpt, DSS_ULT_REBUILD,
			    DSS_TGT_SELF, 0, &ult_send);
	if (rc != 0) {
		rpt_put(rpt);
		D_GOTO(out, rc);
	}

	child = ds_pool_child_lookup(rpt->rt_pool_uuid);
	if (child == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	param.ip_hdl = child->spc_hdl;
	param.ip_flags = VOS_IT_FOR_MIGRATION;
	arg.rpt = rpt;
	arg.yield_freq = DEFAULT_YIELD_FREQ;
	if (!rebuild_status_match(rpt, PO_COMP_ST_UP)) {
		rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
				 rebuild_container_scan_cb, NULL, &arg, NULL);
	}

	ds_pool_child_put(child);

out:
	tls->rebuild_pool_scan_done = 1;
	if (ult_send != ABT_THREAD_NULL)
		ABT_thread_join(ult_send);

	if (tls->rebuild_pool_status == 0 && rc != 0)
		tls->rebuild_pool_status = rc;

	D_DEBUG(DB_TRACE, DF_UUID" iterate pool done: rc %d\n",
		DP_UUID(rpt->rt_pool_uuid), rc);
	return rc;
}

/**
 * Wait for pool map and setup global status, then spawn scanners for all
 * service xsteams
 */
static void
rebuild_scan_leader(void *data)
{
	struct rebuild_tgt_pool_tracker *rpt = data;
	struct rebuild_pool_tls	  *tls;
	int			   rc;

	D_DEBUG(DB_REBUILD, DF_UUID "check resync %u < %u\n",
		DP_UUID(rpt->rt_pool_uuid), rpt->rt_pool->sp_dtx_resync_version,
		rpt->rt_rebuild_ver);

	/* Wait for dtx resync to finish */
	while (rpt->rt_pool->sp_dtx_resync_version < rpt->rt_rebuild_ver)
		ABT_thread_yield();

	rc = dss_thread_collective(rebuild_scanner, rpt, 0, DSS_ULT_REBUILD);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_REBUILD, "rebuild scan collective "DF_UUID" done.\n",
		DP_UUID(rpt->rt_pool_uuid));

	ABT_mutex_lock(rpt->rt_lock);
	rc = dss_task_collective(rebuild_scan_done, rpt, 0, DSS_ULT_REBUILD);
	ABT_mutex_unlock(rpt->rt_lock);
	if (rc) {
		D_ERROR(DF_UUID" send rebuild object list failed:%d\n",
			DP_UUID(rpt->rt_pool_uuid), rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_REBUILD, DF_UUID" sent objects to initiator %d\n",
		DP_UUID(rpt->rt_pool_uuid), rc);
out:
	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver);
	D_ASSERT(tls != NULL);
	if (tls->rebuild_pool_status == 0 && rc != 0)
		tls->rebuild_pool_status = rc;
	D_DEBUG(DB_REBUILD, DF_UUID"scan leader done %d\n",
		DP_UUID(rpt->rt_pool_uuid), rc);
	rpt_put(rpt);
}

/* Scan the local target and generate rebuild object list */
void
rebuild_tgt_scan_handler(crt_rpc_t *rpc)
{
	struct rebuild_scan_in		*rsi;
	struct rebuild_scan_out		*ro;
	struct rebuild_pool_tls		*tls = NULL;
	struct rebuild_tgt_pool_tracker	*rpt = NULL;
	int				 rc;

	rsi = crt_req_get(rpc);
	D_ASSERT(rsi != NULL);

	D_DEBUG(DB_REBUILD, "%d scan rebuild for "DF_UUID" ver %d\n",
		dss_get_module_info()->dmi_tgt_id, DP_UUID(rsi->rsi_pool_uuid),
		rsi->rsi_rebuild_ver);

	/* check if the rebuild is already started */
	rpt = rpt_lookup(rsi->rsi_pool_uuid, rsi->rsi_rebuild_ver);
	if (rpt != NULL) {
		/* Rebuild should never skip the version */
		D_ASSERTF(rsi->rsi_rebuild_ver == rpt->rt_rebuild_ver,
			  "rsi_rebuild_ver %d != rt_rebuild_ver %d\n",
			  rsi->rsi_rebuild_ver, rpt->rt_rebuild_ver);

		D_DEBUG(DB_REBUILD, DF_UUID" already started.\n",
			DP_UUID(rsi->rsi_pool_uuid));

		/* Ignore the rebuild trigger request if it comes from
		 * an old or same leader.
		 */
		if (rsi->rsi_leader_term <= rpt->rt_leader_term)
			D_GOTO(out, rc = 0);

		if (rpt->rt_pool->sp_iv_ns != NULL &&
		    rpt->rt_pool->sp_iv_ns->iv_master_rank !=
					rsi->rsi_master_rank) {
			D_DEBUG(DB_REBUILD, DF_UUID" master rank"
				" %d -> %d term "DF_U64" -> "DF_U64"\n",
				DP_UUID(rpt->rt_pool_uuid),
				rpt->rt_pool->sp_iv_ns->iv_master_rank,
				rsi->rsi_master_rank,
				rpt->rt_leader_term,
				rsi->rsi_leader_term);
			/* re-report the #rebuilt cnt next time */
			rpt->rt_re_report = 1;
			/* Update master rank */
			ds_pool_iv_ns_update(rpt->rt_pool,
					     rsi->rsi_master_rank);

			/* If this is the old leader, then also stop the rebuild
			 * tracking ULT.
			 */
			ds_rebuild_leader_stop(rsi->rsi_pool_uuid,
					       rsi->rsi_rebuild_ver);
		}

		rpt->rt_leader_term = rsi->rsi_leader_term;

		D_GOTO(out, rc = 0);
	}

	tls = rebuild_pool_tls_lookup(rsi->rsi_pool_uuid, rsi->rsi_rebuild_ver);
	if (tls != NULL) {
		D_WARN("the previous rebuild "DF_UUID"/%d is not cleanup yet\n",
		       DP_UUID(rsi->rsi_pool_uuid), rsi->rsi_rebuild_ver);
		D_GOTO(out, rc = -DER_BUSY);
	}

	if (daos_fail_check(DAOS_REBUILD_TGT_START_FAIL))
		D_GOTO(out, rc = -DER_INVAL);

	rc = rebuild_tgt_prepare(rpc, &rpt);
	if (rc)
		D_GOTO(out, rc);

	rpt_get(rpt);
	rc = dss_ult_create(rebuild_tgt_status_check_ult, rpt, DSS_ULT_REBUILD,
			    DSS_TGT_SELF, 0, NULL);
	if (rc) {
		rpt_put(rpt);
		D_GOTO(out, rc);
	}

	rpt_get(rpt);
	/* step-3: start scan leader */
	rc = dss_ult_create(rebuild_scan_leader, rpt, DSS_ULT_REBUILD,
			    DSS_TGT_SELF, 0, NULL);
	if (rc != 0) {
		rpt_put(rpt);
		D_GOTO(out, rc);
	}

out:
	if (tls && tls->rebuild_pool_status == 0 && rc != 0)
		tls->rebuild_pool_status = rc;

	if (rpt)
		rpt_put(rpt);
	ro = crt_reply_get(rpc);
	ro->rso_status = rc;
	ro->rso_stable_epoch = crt_hlc_get();
	dss_rpc_reply(rpc, DAOS_REBUILD_DROP_SCAN);
}

int
rebuild_tgt_scan_aggregator(crt_rpc_t *source, crt_rpc_t *result,
			    void *priv)
{
	struct rebuild_scan_out	*src = crt_reply_get(source);
	struct rebuild_scan_out *dst = crt_reply_get(result);

	if (dst->rso_status == 0)
		dst->rso_status = src->rso_status;

	if (src->rso_status == 0 &&
	    dst->rso_stable_epoch < src->rso_stable_epoch)
		dst->rso_stable_epoch = src->rso_stable_epoch;

	return 0;
}
