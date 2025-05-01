/**
 * (C) Copyright 2017-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <daos_srv/daos_engine.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/object.h>
#include <daos_srv/vos.h>
#include <daos_srv/dtx_srv.h>
#include "rpc.h"
#include "rebuild_internal.h"

#define REBUILD_SEND_LIMIT	4096
struct rebuild_send_arg {
	struct rebuild_tgt_pool_tracker *rpt;
	daos_unit_oid_t			*oids;
	daos_epoch_t			*ephs;
	daos_epoch_t			*punched_ephs;
	uuid_t				cont_uuid;
	unsigned int			*shards;
	int				count;
	unsigned int			tgt_id;
};

struct rebuild_obj_val {
	daos_epoch_t    eph;
	daos_epoch_t	punched_eph;
	uint32_t	shard;
};

static int
rebuild_obj_fill_buf(daos_handle_t ih, d_iov_t *key_iov,
		     d_iov_t *val_iov, void *data)
{
	struct rebuild_send_arg *arg = data;
	daos_unit_oid_t		*oids = arg->oids;
	daos_epoch_t		*ephs = arg->ephs;
	daos_epoch_t		*punched_ephs = arg->punched_ephs;
	daos_unit_oid_t		*oid = key_iov->iov_buf;
	struct rebuild_obj_val	*obj_val = val_iov->iov_buf;
	unsigned int		*shards = arg->shards;
	int			count = arg->count;
	int			rc;

	D_ASSERT(count < REBUILD_SEND_LIMIT);
	oids[count] = *oid;
	ephs[count] = obj_val->eph;
	punched_ephs[count] = obj_val->punched_eph;
	shards[count] = obj_val->shard;
	arg->count++;

	D_DEBUG(DB_REBUILD, "send oid/con "DF_UOID"/"DF_UUID" ephs "DF_U64
		"shard %d cnt %d tgt_id %d\n", DP_UOID(oids[count]),
		DP_UUID(arg->cont_uuid), obj_val->eph, shards[count],
		arg->count, arg->tgt_id);

	rc = dbtree_iter_delete(ih, NULL);
	if (rc != 0)
		return rc;

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
	uint64_t			enqueue_id;
	uint32_t			max_delay;

	/* re-init the send argument to fill the object buffer */
	arg->count = 0;
	rc = dbtree_iterate(root->tcr_root_hdl, DAOS_INTENT_MIGRATION, false,
			    rebuild_obj_fill_buf, arg);
	if (rc < 0 || arg->count == 0) {
		D_DEBUG(DB_REBUILD, DF_RB " cannot get objects: " DF_RC "\n", DP_RB_RPT(rpt),
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (daos_fail_check(DAOS_REBUILD_TGT_SEND_OBJS_FAIL))
		D_GOTO(out, rc = -DER_IO);

	D_DEBUG(DB_REBUILD,
		DF_RB " send rebuild objects to tgt %d cnt %d stable epoch " DF_U64 "\n",
		DP_RB_RPT(rpt), arg->tgt_id, arg->count, rpt->rt_stable_epoch);
	while (1) {
		enqueue_id = 0;
		max_delay = 0;
		rc = ds_object_migrate_send(rpt->rt_pool, rpt->rt_poh_uuid,
					    rpt->rt_coh_uuid, arg->cont_uuid,
					    arg->tgt_id, rpt->rt_rebuild_ver,
					    rpt->rt_rebuild_gen, rpt->rt_stable_epoch,
					    arg->oids, arg->ephs, arg->punched_ephs, arg->shards,
					    arg->count, rpt->rt_new_layout_ver, rpt->rt_rebuild_op,
					    &enqueue_id, &max_delay);
		/* If it does not need retry */
		if (rc == 0 || (rc != -DER_TIMEDOUT && rc != -DER_GRPVER &&
		    rc != -DER_OVERLOAD_RETRY && rc != -DER_AGAIN &&
		    !daos_crt_network_error(rc)))
			break;

		if (rpt->rt_abort || rpt->rt_finishing) {
			rc = -DER_SHUTDOWN;
			DL_INFO(rc, DF_RB ": give up ds_object_migrate_send, shutdown rebuild",
				DP_RB_RPT(rpt));
			break;
		}

		/* otherwise let's retry */
		D_DEBUG(DB_REBUILD, DF_RB " retry send object to tgt_id %d\n", DP_RB_RPT(rpt),
			arg->tgt_id);
		dss_sleep(daos_rpc_rand_delay(max_delay) << 10);
	}
out:
	return rc;
}

static int
obj_tree_destroy_current_probe(daos_handle_t ih, daos_handle_t cur_hdl, d_iov_t *key_iov)
{
	int rc;

	rc = dbtree_destroy(cur_hdl, NULL);
	if (rc && rc != -DER_NO_HDL) {
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

static int
tgt_tree_destory_cb(daos_handle_t ih, d_iov_t *key_iov,
		    d_iov_t *val_iov, void *data)
{
	struct tree_cache_root *root = val_iov->iov_buf;

	return obj_tree_destroy(root->tcr_root_hdl);
}

int
rebuild_obj_tree_destroy(daos_handle_t btr_hdl)
{
	int	rc;

	rc = dbtree_iterate(btr_hdl, DAOS_INTENT_PUNCH, false,
			    tgt_tree_destory_cb, NULL);
	if (rc) {
		D_ERROR("dbtree iterate failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = dbtree_destroy(btr_hdl, NULL);
out:
	return rc;
}

static int
rebuild_tgt_iter_cb(daos_handle_t ih, d_iov_t *key_iov, d_iov_t *val_iov, void *data)
{
	struct rebuild_send_arg	*arg = data;
	d_iov_t			save_key_iov;
	struct tree_cache_root	*root;
	uint64_t		tgt_id;
	int			ret;
	int			rc = 0;

	tgt_id = *(uint64_t *)key_iov->iov_buf;
	arg->tgt_id = (unsigned int)tgt_id;
	root = val_iov->iov_buf;
	while (!dbtree_is_empty(root->tcr_root_hdl)) {
		rc = rebuild_obj_send_cb(root, arg);
		if (rc < 0) {
			DL_ERROR(rc, DF_RB " rebuild_obj_send_cb failed", DP_RB_RPT(arg->rpt));
			break;
		}
	}

	d_iov_set(&save_key_iov, &tgt_id, sizeof(tgt_id));
	ret = obj_tree_destroy_current_probe(ih, root->tcr_root_hdl, &save_key_iov);
	if (rc == 0)
		rc = ret;
	return rc;
}

static int
rebuild_cont_iter_cb(daos_handle_t ih, d_iov_t *key_iov,
		     d_iov_t *val_iov, void *data)
{
	struct rebuild_send_arg	*arg = data;
	struct tree_cache_root	*root;
	d_iov_t			save_key_iov;
	int			ret;
	int			rc = 0;

	uuid_copy(arg->cont_uuid, *(uuid_t *)key_iov->iov_buf);
	root = val_iov->iov_buf;
	while (!dbtree_is_empty(root->tcr_root_hdl)) {
		rc = dbtree_iterate(root->tcr_root_hdl, DAOS_INTENT_MIGRATION, false,
				    rebuild_tgt_iter_cb, arg);
		if (rc < 0) {
			DL_ERROR(rc, DF_RB " rebuild_tgt_send_cb failed", DP_RB_RPT(arg->rpt));
			break;
		}
	}

	d_iov_set(&save_key_iov, arg->cont_uuid, sizeof(uuid_t));
	ret = obj_tree_destroy_current_probe(ih, root->tcr_root_hdl, &save_key_iov);
	if (rc == 0)
		rc = ret;
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
	daos_epoch_t			*punched_ephs = NULL;
	unsigned int			*shards = NULL;
	int				rc = 0;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
				      rpt->rt_rebuild_gen);
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

	D_ALLOC_ARRAY(punched_ephs, REBUILD_SEND_LIMIT);
	if (ephs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	arg.count = 0;
	arg.oids = oids;
	arg.shards = shards;
	arg.ephs = ephs;
	arg.punched_ephs = punched_ephs;
	arg.rpt = rpt;
	while (!tls->rebuild_pool_scan_done || !dbtree_is_empty(tls->rebuild_tree_hdl)) {
		if (rpt->rt_stable_epoch == 0) {
			dss_sleep(0);
			continue;
		}

		if (dbtree_is_empty(tls->rebuild_tree_hdl)) {
			dss_sleep(0);
			continue;
		}

		/* walk through the rebuild tree and send the rebuild objects */
		rc = dbtree_iterate(tls->rebuild_tree_hdl, DAOS_INTENT_MIGRATION,
				    false, rebuild_cont_iter_cb, &arg);
		if (rc < 0) {
			DL_ERROR(rc, DF_RB " dbtree iterate failed", DP_RB_RPT(rpt));
			break;
		}
		dss_sleep(0);
	}

	D_DEBUG(DB_REBUILD, DF_RB " objects send finished\n", DP_RB_RPT(rpt));
out:
	if (oids != NULL)
		D_FREE(oids);
	if (shards != NULL)
		D_FREE(shards);
	if (ephs != NULL)
		D_FREE(ephs);
	if (punched_ephs != NULL)
		D_FREE(punched_ephs);
	if (rc != 0 && tls->rebuild_pool_status == 0) {
		DL_ERROR(rc, DF_RB " objects send error", DP_RB_RPT(rpt));
		tls->rebuild_pool_status = rc;
	}

	rpt_put(rpt);
}

static int
rebuild_scan_done(void *data)
{
	struct rebuild_tgt_pool_tracker *rpt = data;
	struct rebuild_pool_tls *tls;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
				      rpt->rt_rebuild_gen);
	if (tls != NULL)
		tls->rebuild_pool_scanning = 0;

	return 0;
}

/**
 * The rebuild objects will be gathered into a global objects array by
 * target id.
 **/
static int
rebuild_object_insert(struct rebuild_tgt_pool_tracker *rpt, uuid_t co_uuid,
		      daos_unit_oid_t oid, unsigned int tgt_id, unsigned int shard,
		      daos_epoch_t epoch, daos_epoch_t punched_epoch)
{
	struct rebuild_pool_tls *tls;
	struct rebuild_obj_val	val;
	d_iov_t			val_iov;
	int			rc;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
				      rpt->rt_rebuild_gen);
	D_ASSERT(tls != NULL);
	D_ASSERT(daos_handle_is_valid(tls->rebuild_tree_hdl));

	tls->rebuild_pool_obj_count++;
	val.eph = epoch;
	val.punched_eph = punched_epoch;
	val.shard = shard;
	d_iov_set(&val_iov, &val, sizeof(struct rebuild_obj_val));
	oid.id_shard = shard; /* Convert the OID to rebuilt one */
	rc = obj_tree_insert(tls->rebuild_tree_hdl, co_uuid, tgt_id, oid, &val_iov);
	if (rc == -DER_EXIST) {
		/* If there is reintegrate being restarted due to the failure, then
		 * it might put multiple shards into the same VOS target, because
		 * reclaim is not being scheduled in the previous failure reintegration,
		 * so let's ignore duplicate shards(DER_EXIST) in this case.
		 */
		D_DEBUG(DB_REBUILD, DF_UUID" found duplicate "DF_UOID" %d\n",
			DP_UUID(co_uuid), DP_UOID(oid), tgt_id);
		rc = 0;
	} else {
		D_DEBUG(DB_REBUILD, "insert "DF_UOID"/"DF_UUID" tgt %u "DF_U64"/"DF_U64": "
			DF_RC"\n", DP_UOID(oid), DP_UUID(co_uuid), tgt_id, epoch,
			punched_epoch, DP_RC(rc));
	}

	return rc;
}

#define LOCAL_ARRAY_SIZE	128
#define NUM_SHARDS_STEP_INCREASE	64
/* The structure for scan per xstream */
struct rebuild_scan_arg {
	struct rebuild_tgt_pool_tracker *rpt;
	uuid_t				co_uuid;
	struct cont_props		co_props;
	int				snapshot_cnt;
	uint32_t			yield_freq;
	int32_t				obj_yield_cnt;
	struct ds_cont_child		*cont_child;
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
 * \param[in]	map			placement map
 * \param[in]	gl_layout_ver		global layout version from pool/container.
 * \param[in]	md			object metadata
 * \param[in]	num_rebuild_tgts	the number of targets being rebuilt now
 * \param[in]	rebuild_op		the rebuild operation
 * \param[in]	rebuild_ver		the rebuild version
 * \param[in]	myrank			this system's rank
 * \param[out]	tgts			filled remap list, caller must free if
 *					it is re-allocated.
 * \param[out]	shards			filled remap list, caller must free if
 *					it is re-allocated.
 * \param[in]   orig_array_size		original size of tgts and shards
 *
 * \retval	>= 0	Success
 * \retval	> 0	number of filled entries in tgts and shards needs to be remapped
 * \retval	< 0	-DER_* error. Will not return -DER_REC2BIG
 */
static int
find_rebuild_shards(struct pl_map *map, uint32_t gl_layout_ver, struct daos_obj_md *md,
		    uint32_t num_rebuild_tgts, daos_rebuild_opc_t rebuild_op,
		    uint32_t rebuild_ver, d_rank_t myrank, unsigned int **tgts,
		    unsigned int **shards, uint32_t orig_array_size)
{
	uint32_t max_shards_size = orig_array_size;
	int	rc = 0;

retry:
	rc = pl_obj_find_rebuild(map, gl_layout_ver, md, NULL, rebuild_ver,
				 *tgts, *shards, max_shards_size);
	if (rc == -DER_REC2BIG) {
		/*
		 * The last attempt failed because there was not enough
		 * room for all the remapped shards.
		 *
		 * Need to allocate more space and try again
		 */
		if (max_shards_size != orig_array_size && *tgts != NULL)
			D_FREE(*tgts);
		if (max_shards_size != orig_array_size && *shards != NULL)
			D_FREE(*shards);

		/* Increase by the step size */
		max_shards_size += NUM_SHARDS_STEP_INCREASE;
		D_DEBUG(DB_REBUILD, "Got REC2BIG, increase rebuild array size by %u to %u",
			NUM_SHARDS_STEP_INCREASE, max_shards_size);
		D_ALLOC_ARRAY(*tgts, max_shards_size);
		D_ALLOC_ARRAY(*shards, max_shards_size);
		if (*tgts == NULL || *shards == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		goto retry;
	}

out:
	if (rc < 0) {
		/*
		 * Free any non-stack buffers that were allocated
		 * on the last attempt
		 */
		if (max_shards_size != orig_array_size && *tgts != NULL)
			D_FREE(*tgts);
		if (max_shards_size != orig_array_size && *shards != NULL)
			D_FREE(*shards);
	}

	return rc;
}

static int
obj_reclaim(struct pl_map *map, uint32_t layout_ver, uint32_t new_layout_ver,
	    struct daos_obj_md *md, struct rebuild_tgt_pool_tracker *rpt,
	    d_rank_t myrank, daos_unit_oid_t oid, vos_iter_param_t *param,
	    unsigned *acts)
{
	uint32_t		mytarget = dss_get_module_info()->dmi_tgt_id;
	struct pl_obj_layout	*layout = NULL;
	struct rebuild_pool_tls *tls;
	daos_epoch_range_t	discard_epr;
	bool			still_needed;
	int			rc;

	/*
	 * Compute placement for the object, then check if the layout
	 * still includes the current rank. If not, the object can be
	 * deleted/reclaimed because it is no longer reachable
	 */
	rc = pl_obj_place(map, oid.id_layout_ver, md, DAOS_OO_RO, NULL, &layout);
	if (rc != 0)
		return rc;

	/* If there are further targets failure during reintegration/extend/drain,
	 * rebuild will choose replacement targets for the impacted objects anyway,
	 * so we do not need reclaim these impacted shards by @ignore_rebuild_shard.
	 */
	still_needed = pl_obj_layout_contains(rpt->rt_pool->sp_map, layout, myrank,
					      mytarget, oid.id_shard,
					      rpt->rt_rebuild_op == RB_OP_RECLAIM ? false : true);
	pl_obj_layout_free(layout);
	if (still_needed) {
		if (new_layout_ver > 0) {
			/* upgrade job reclaim */
			if (rpt->rt_rebuild_op == RB_OP_FAIL_RECLAIM) {
				if (oid.id_layout_ver == new_layout_ver) {
					*acts |= VOS_ITER_CB_DELETE;
					vos_obj_delete_ent(param->ip_hdl, oid);
				}
			} else {
				if (oid.id_layout_ver < new_layout_ver) {
					*acts |= VOS_ITER_CB_DELETE;
					vos_obj_delete_ent(param->ip_hdl, oid);
				}
			}
		}
		return 0;
	}

	D_DEBUG(DB_REBUILD, DF_RB " deleting stale object " DF_UOID " oid layout %u/%u",
		DP_RB_RPT(rpt), DP_UOID(oid), oid.id_layout_ver, new_layout_ver);
	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver, rpt->rt_rebuild_gen);
	D_ASSERT(tls != NULL);
	tls->rebuild_pool_reclaim_obj_count++;

	discard_epr.epr_hi = rpt->rt_reclaim_epoch;
	discard_epr.epr_lo = 0;
	/*
	 * It's possible this object might still be being
	 * accessed elsewhere - retry until until it is possible
	 * to delete
	 */
	do {
		/* Inform the iterator and delete the object */
		*acts |= VOS_ITER_CB_DELETE;
		rc = vos_discard(param->ip_hdl, &oid, &discard_epr, NULL, NULL);
		if (rc != -DER_BUSY && rc != -DER_INPROGRESS)
			break;

		D_DEBUG(DB_REBUILD, DF_RB " retry by " DF_RC "/" DF_UOID "\n", DP_RB_RPT(rpt),
			DP_RC(rc), DP_UOID(oid));
		/* Busy - inform iterator and yield */
		*acts |= VOS_ITER_CB_YIELD;
		dss_sleep(0);
	} while (1);

	if (rc != 0)
		DL_ERROR(rc, DF_RB " Failed to delete object " DF_UOID, DP_RB_RPT(rpt),
			 DP_UOID(oid));

	return rc;
}

struct rebuild_obj_arg {
	struct rebuild_tgt_pool_tracker *rpt;
	daos_unit_oid_t			oid;
	uuid_t				co_uuid;
	daos_epoch_t			epoch;
	daos_epoch_t			punched_epoch;
	daos_epoch_t			max_eph;
	uint32_t			shard;
	uint32_t			tgt_index;
};

static void
rebuild_obj_ult(void *data)
{
	struct rebuild_obj_arg		*arg = data;
	struct rebuild_tgt_pool_tracker	*rpt = arg->rpt;

	ds_migrate_object(rpt->rt_pool, rpt->rt_poh_uuid, rpt->rt_coh_uuid, arg->co_uuid,
			  rpt->rt_rebuild_ver, rpt->rt_rebuild_gen, rpt->rt_stable_epoch,
			  rpt->rt_rebuild_op, &arg->oid, &arg->epoch, &arg->punched_epoch,
			  &arg->shard, 1, arg->tgt_index, rpt->rt_new_layout_ver);
	rpt_put(rpt);
	D_FREE(arg);
}

static int
rebuild_object_local(struct rebuild_tgt_pool_tracker *rpt, uuid_t co_uuid,
		     daos_unit_oid_t oid, unsigned int tgt_index, unsigned int shard,
		     daos_epoch_t eph, daos_epoch_t punched_eph)
{
	struct rebuild_obj_arg	*arg;
	int			rc;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		return -DER_NOMEM;

	rpt_get(rpt);
	arg->rpt = rpt;
	arg->oid = oid;
	arg->oid.id_shard = shard; /* Convert the OID to rebuilt one */
	arg->epoch = eph;
	arg->punched_epoch = punched_eph;
	uuid_copy(arg->co_uuid, co_uuid);
	arg->tgt_index = tgt_index;
	arg->shard = shard;

	rc = dss_ult_create(rebuild_obj_ult, arg, DSS_XS_SYS, 0, 0, NULL);
	if (rc) {
		D_FREE(arg);
		rpt_put(rpt);
	}

	return rc;
}

static int
rebuild_object(struct rebuild_tgt_pool_tracker *rpt, uuid_t co_uuid, daos_unit_oid_t oid,
	       unsigned int tgt, uint32_t shard, d_rank_t myrank, vos_iter_entry_t *ent)
{
	uint32_t		mytarget = dss_get_module_info()->dmi_tgt_id;
	struct pool_target	*target;
	daos_epoch_t		eph;
	daos_epoch_t		punched_eph;
	int			rc;

	rc = pool_map_find_target(rpt->rt_pool->sp_map, tgt, &target);
	D_ASSERT(rc == 1);
	rc = 0;

	if (myrank == target->ta_comp.co_rank && mytarget == target->ta_comp.co_index &&
	    (shard == oid.id_shard) && rpt->rt_rebuild_op != RB_OP_UPGRADE) {
		D_DEBUG(DB_REBUILD, DF_RB ": " DF_UOID " %u/%u already on the target shard\n",
			DP_RB_RPT(rpt), DP_UOID(oid), myrank, mytarget);
		return 0;
	}

	if (ent->ie_vis_flags & VOS_VIS_FLAG_COVERED) {
		eph = 0;
		punched_eph = ent->ie_epoch;
	} else {
		eph = ent->ie_epoch;
		punched_eph = 0;
	}

	if (myrank == target->ta_comp.co_rank)
		rc = rebuild_object_local(rpt, co_uuid, oid, target->ta_comp.co_index, shard,
					  eph, punched_eph);
	else
		rc = rebuild_object_insert(rpt, co_uuid, oid, tgt, shard, eph, punched_eph);

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
	struct daos_oclass_attr		*oc_attr;
	uint32_t			grp_size;
	int				rebuild_nr = 0;
	d_rank_t			myrank;
	int				i;
	int				rc = 0;

	if (rpt->rt_abort || arg->cont_child->sc_stopping) {
		D_DEBUG(DB_REBUILD, "rebuild is aborted\n");
		return 1;
	}

	/* If the OID is invisible, then snapshots must be created on the object. */
	D_ASSERTF(!(ent->ie_vis_flags & VOS_VIS_FLAG_COVERED) || arg->snapshot_cnt > 0,
		  "flags %x snapshot_cnt %d\n", ent->ie_vis_flags, arg->snapshot_cnt);
	map = pl_map_find(rpt->rt_pool_uuid, oid.id_pub);
	if (map == NULL) {
		D_ERROR(DF_RB " " DF_UOID ": Cannot find valid placement map\n", DP_RB_RPT(rpt),
			DP_UOID(oid));
		D_GOTO(out, rc = -DER_INVAL);
	}

	oc_attr = daos_oclass_attr_find(oid.id_pub, NULL);
	if (oc_attr == NULL) {
		D_INFO(DF_RB " skip invalid " DF_UOID "\n", DP_RB_RPT(rpt), DP_UOID(oid));
		D_GOTO(out, rc = 0);
	}

	grp_size = daos_oclass_grp_size(oc_attr);

	dc_obj_fetch_md(oid.id_pub, &md);
	crt_group_rank(rpt->rt_pool->sp_group, &myrank);
	md.omd_ver = rpt->rt_rebuild_ver;
	md.omd_fdom_lvl = arg->co_props.dcp_redun_lvl;
	md.omd_pdom_lvl = arg->co_props.dcp_perf_domain;
	md.omd_pda = daos_cont_props2pda(&arg->co_props, daos_oclass_is_ec(oc_attr));
	tgts = tgt_array;
	shards = shard_array;
	switch (rpt->rt_rebuild_op) {
	case RB_OP_REBUILD:
		rc = find_rebuild_shards(map, arg->co_props.dcp_obj_version, &md,
					 rpt->rt_tgts_num, rpt->rt_rebuild_op,
					 rpt->rt_rebuild_ver, myrank,
					 &tgts, &shards, LOCAL_ARRAY_SIZE);
		break;
	case RB_OP_RECLAIM:
	case RB_OP_FAIL_RECLAIM:
		rc = obj_reclaim(map, arg->co_props.dcp_obj_version, rpt->rt_new_layout_ver,
				 &md, rpt, myrank, oid, param, acts);
		break;
	case RB_OP_UPGRADE:
		if (oid.id_layout_ver < rpt->rt_new_layout_ver) {
			rc = obj_layout_diff(map, oid, rpt->rt_new_layout_ver,
					     arg->co_props.dcp_obj_version, &md,
					     tgts, shards, LOCAL_ARRAY_SIZE);
			/* Then only upgrade the layout version */
			if (rc == 0) {
				rc = vos_obj_layout_upgrade(param->ip_hdl, oid,
							    rpt->rt_new_layout_ver);
				if (rc == 0)
					*acts |= VOS_ITER_CB_DELETE;
			}
		}
		break;
	default:
		D_ASSERT(0);
	}

	if (rc <= 0) {
		DL_CDEBUG(rc == 0, DB_REBUILD, DLOG_ERR, rc, DF_RB " " DF_UOID " rebuild shards",
			  DP_RB_RPT(rpt), DP_UOID(oid));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_REBUILD, DF_RB " rebuild obj " DF_UOID " rebuild_nr %d\n", DP_RB_RPT(rpt),
		DP_UOID(oid), rc);
	rebuild_nr = rc;
	rc = 0;
	for (i = 0; i < rebuild_nr; i++) {
		D_DEBUG(DB_REBUILD,
			DF_RB " cont " DF_UUID " rebuild obj " DF_UOID " on %d for shard %d"
			      " eph " DF_U64 " visible %s\n",
			DP_RB_RPT(rpt), DP_UUID(arg->co_uuid), DP_UOID(oid), tgts[i], shards[i],
			ent->ie_epoch, ent->ie_vis_flags & VOS_VIS_FLAG_COVERED ? "no" : "yes");

		/* Ignore the shard if it is not in the same group of failure shard */
		if ((int)tgts[i] == -1 || oid.id_shard / grp_size != shards[i] / grp_size) {
			D_DEBUG(DB_REBUILD,
				DF_RB " i %d stale object " DF_UOID " shards %u "
				      "grp_size %u tgt %d\n",
				DP_RB_RPT(rpt), i, DP_UOID(oid), shards[i], grp_size, (int)tgts[i]);
			continue;
		}

		rc = rebuild_object(rpt, arg->co_uuid, oid, tgts[i], shards[i], myrank, ent);
		if (rc)
			D_GOTO(out, rc);

		arg->obj_yield_cnt--;
	}

out:
	if (tgts != tgt_array && tgts != NULL)
		D_FREE(tgts);

	if (shards != shard_array && shards != NULL)
		D_FREE(shards);

	if (map != NULL)
		pl_map_decref(map);

	if (--arg->yield_freq == 0 || arg->obj_yield_cnt <= 0) {
		D_DEBUG(DB_REBUILD, DF_RB " rebuild yield: %d\n", DP_RB_RPT(rpt), rc);
		arg->yield_freq = SCAN_YIELD_FREQ;
		arg->obj_yield_cnt = SCAN_OBJ_YIELD_CNT;
		if (rc == 0)
			dss_sleep(0);
		*acts |= VOS_ITER_CB_YIELD;
	}

	return rc;
}

static int
rebuild_container_scan_cb(daos_handle_t ih, vos_iter_entry_t *entry,
			  vos_iter_type_t type, vos_iter_param_t *iter_param,
			  void *data, unsigned *acts)
{
	struct rebuild_scan_arg		*arg = data;
	struct rebuild_tgt_pool_tracker *rpt = arg->rpt;
	struct dtx_handle		*dth = NULL;
	vos_iter_param_t		param = { 0 };
	struct vos_iter_anchors		anchor = { 0 };
	daos_handle_t			coh;
	struct ds_cont_child		*cont_child = NULL;
	struct dtx_id			dti = { 0 };
	struct dtx_epoch		epoch = { 0 };
	daos_unit_oid_t			oid = { 0 };
	int				snapshot_cnt = 0;
	int				rc;

	if (uuid_compare(arg->co_uuid, entry->ie_couuid) == 0) {
		D_DEBUG(DB_REBUILD, DF_RB " co_uuid " DF_UUID " already scanned\n", DP_RB_RPT(rpt),
			DP_UUID(arg->co_uuid));
		return 0;
	}

	rc = vos_cont_open(iter_param->ip_hdl, entry->ie_couuid, &coh);
	if (rc == -DER_NONEXIST) {
		D_DEBUG(DB_REBUILD, DF_RB " co_uuid " DF_UUID " already destroyed\n",
			DP_RB_RPT(rpt), DP_UUID(arg->co_uuid));
		return 0;
	}

	if (rc != 0) {
		DL_ERROR(rc, DF_RB " Open container " DF_UUID " failed", DP_RB_RPT(rpt),
			 DP_UUID(entry->ie_couuid));
		return rc;
	}

	rc = ds_cont_child_lookup(rpt->rt_pool_uuid, entry->ie_couuid, &cont_child);
	if (rc == -DER_NONEXIST || rc == -DER_SHUTDOWN) {
		D_DEBUG(DB_REBUILD, DF_RB " co_uuid " DF_UUID " already destroyed or destroying\n",
			DP_RB_RPT(rpt), DP_UUID(arg->co_uuid));
		rc = 0;
		D_GOTO(close, rc);
	}

	if (rc != 0) {
		DL_ERROR(rc, DF_RB " Container " DF_UUID ", ds_cont_child_lookup failed",
			 DP_RB_RPT(rpt), DP_UUID(entry->ie_couuid));
		D_GOTO(close, rc);
	}

	/*
	 * The container may has been closed by the application, but some resource (DRAM) occupied
	 * by DTX may be not released because DTX resync was in-progress at that time. When arrive
	 * here, DTX resync must has completed globally. Let's release related resource.
	 */
	if (unlikely(cont_child->sc_dtx_delay_reset == 1)) {
		stop_dtx_reindex_ult(cont_child, true);
		vos_dtx_cache_reset(cont_child->sc_hdl, false);
	}

	cont_child->sc_rebuilding = 1;

	rc = ds_cont_fetch_snaps(rpt->rt_pool->sp_iv_ns, entry->ie_couuid, NULL,
				 &snapshot_cnt);
	if (rc) {
		DL_ERROR(rc, DF_RB " Container " DF_UUID ", ds_cont_fetch_snaps failed",
			 DP_RB_RPT(rpt), DP_UUID(entry->ie_couuid));
		D_GOTO(close, rc);
	}

	rc = ds_cont_get_props(&arg->co_props, rpt->rt_pool->sp_uuid, entry->ie_couuid);
	if (rc) {
		DL_ERROR(rc, DF_RB " Container " DF_UUID ", ds_cont_get_props failed",
			 DP_RB_RPT(rpt), DP_UUID(entry->ie_couuid));
		D_GOTO(close, rc);
	}

	/* Wait for EC aggregation to finish. NB: migrate needs to wait for EC aggregation to finish */
	while (cont_child->sc_ec_agg_active &&
	       rpt->rt_rebuild_op != RB_OP_RECLAIM &&
	       rpt->rt_rebuild_op != RB_OP_FAIL_RECLAIM) {
		D_ASSERTF(rpt->rt_pool->sp_rebuilding >= 0, DF_UUID" rebuilding %d\n",
			  DP_UUID(rpt->rt_pool_uuid), rpt->rt_pool->sp_rebuilding);
		/* Wait for EC aggregation to abort before discard the object */
		D_INFO(DF_RB " " DF_UUID " wait for ec agg abort, rebuilding %d.\n", DP_RB_RPT(rpt),
		       DP_UUID(entry->ie_couuid), rpt->rt_pool->sp_rebuilding);
		dss_sleep(1000);
		if (rpt->rt_abort || rpt->rt_finishing) {
			D_DEBUG(DB_REBUILD, DF_RB " " DF_UUID " rebuild abort %u/%u.\n",
				DP_RB_RPT(rpt), DP_UUID(entry->ie_couuid), rpt->rt_abort,
				rpt->rt_finishing);
			*acts |= VOS_ITER_CB_ABORT;
			D_GOTO(close, rc);
		}
	}

	epoch.oe_value = rpt->rt_stable_epoch;
	rc = dtx_begin(coh, &dti, &epoch, 0, rpt->rt_rebuild_ver,
		       &oid, NULL, 0, DTX_IGNORE_UNCOMMITTED, NULL, &dth);
	D_ASSERT(rc == 0);
	memset(&param, 0, sizeof(param));
	param.ip_hdl = coh;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = DAOS_EPOCH_MAX;
	param.ip_flags = VOS_IT_FOR_MIGRATION;
	uuid_copy(arg->co_uuid, entry->ie_couuid);
	arg->snapshot_cnt = snapshot_cnt;
	arg->cont_child = cont_child;

	/* If there is no snapshots, then rebuild does not need to migrate
	 * punched objects at all. Ideally, it should ignore any objects
	 * whose creation epoch > snapshot epoch.
	 */
	if (snapshot_cnt > 0)
		param.ip_flags |= VOS_IT_PUNCHED;

	rc = vos_iterate(&param, VOS_ITER_OBJ, false, &anchor,
			 rebuild_obj_scan_cb, NULL, arg, dth);
	dtx_end(dth, NULL, rc);

close:
	vos_cont_close(coh);

	if (cont_child != NULL) {
		cont_child->sc_rebuilding = 0;
		ABT_cond_broadcast(cont_child->sc_rebuild_cond);
		ds_cont_child_put(cont_child);
	}

	D_DEBUG(DB_REBUILD, DF_RB " " DF_UUID " iterate cont done: " DF_RC "\n", DP_RB_RPT(rpt),
		DP_UUID(entry->ie_couuid), DP_RC(rc));

	return rc;
}

bool
is_rebuild_scanning_tgt(struct rebuild_tgt_pool_tracker *rpt)
{
	struct pool_target	*tgt;
	unsigned int		idx = dss_get_module_info()->dmi_tgt_id;
	d_rank_t		rank;
	int			rc;

	D_ASSERT(rpt != NULL);
	D_ASSERT(rpt->rt_pool != NULL);
	D_ASSERT(rpt->rt_pool->sp_map != NULL);

	crt_group_rank(NULL, &rank);
	rc = pool_map_find_target_by_rank_idx(rpt->rt_pool->sp_map, rank,
					      idx, &tgt);
	D_ASSERT(rc == 1);
	switch(tgt->ta_comp.co_status) {
		case PO_COMP_ST_DOWNOUT:
		case PO_COMP_ST_DOWN:
		case PO_COMP_ST_UP:
		case PO_COMP_ST_NEW:
			return false;
		case PO_COMP_ST_UPIN:
		case PO_COMP_ST_DRAIN:
			return true;
		default:
			break;
	}

	return false;
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
	int				rc = 0;

	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
				      rpt->rt_rebuild_gen);
	if (tls == NULL)
		return 0;

	if (!is_rebuild_scanning_tgt(rpt)) {
		D_DEBUG(DB_REBUILD, DF_RB " skip scan\n", DP_RB_RPT(rpt));
		D_GOTO(out, rc = 0);
	}

	while (daos_fail_check(DAOS_REBUILD_TGT_SCAN_HANG)) {
		/* Skip reclaim OP for HANG failure injection */
		if (rpt->rt_rebuild_op == RB_OP_RECLAIM ||
		    rpt->rt_rebuild_op == RB_OP_FAIL_RECLAIM)
			break;

		D_DEBUG(DB_REBUILD, DF_RB " sleep 2 seconds then retry\n", DP_RB_RPT(rpt));
		dss_sleep(2 * 1000);
	}
	D_ASSERT(daos_handle_is_inval(tls->rebuild_tree_hdl));
	/* Create object tree root */
	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	rc = dbtree_create(DBTREE_CLASS_UV, 0, 4, &uma, NULL,
			   &tls->rebuild_tree_hdl);
	if (rc != 0) {
		DL_ERROR(rc, DF_RB " failed to create rebuild tree", DP_RB_RPT(rpt));
		D_GOTO(out, rc);
	}

	if (rpt->rt_rebuild_op != RB_OP_RECLAIM && rpt->rt_rebuild_op != RB_OP_FAIL_RECLAIM) {
		rpt_get(rpt);
		rc = dss_ult_create(rebuild_objects_send_ult, rpt, DSS_XS_SELF,
				    0, 0, &ult_send);
		if (rc != 0) {
			rpt_put(rpt);
			D_GOTO(out, rc);
		}
	}

	child = ds_pool_child_lookup(rpt->rt_pool_uuid);
	if (child == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	param.ip_hdl = child->spc_hdl;
	param.ip_flags = VOS_IT_FOR_MIGRATION;
	arg.rpt = rpt;
	arg.yield_freq = SCAN_YIELD_FREQ;
	arg.obj_yield_cnt = SCAN_OBJ_YIELD_CNT;
	rc = vos_iterate(&param, VOS_ITER_COUUID, false, &anchor,
			 rebuild_container_scan_cb, NULL, &arg, NULL);
	if (rc < 0)
		D_GOTO(put, rc);
	rc = 0; /* rc might be 1 if rebuild is aborted */
put:
	ds_pool_child_put(child);
out:
	tls->rebuild_pool_scan_done = 1;
	if (ult_send != ABT_THREAD_NULL)
		ABT_thread_free(&ult_send);

	if (tls->rebuild_pool_status == 0 && rc != 0)
		tls->rebuild_pool_status = rc;

	D_DEBUG(DB_REBUILD, DF_RB " iterate pool done: " DF_RC "\n", DP_RB_RPT(rpt), DP_RC(rc));
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
	bool			   wait = false;

	D_DEBUG(DB_REBUILD, DF_RB " check resync %u/%u < %u\n", DP_RB_RPT(rpt),
		rpt->rt_pool->sp_dtx_resync_version, rpt->rt_global_dtx_resync_version,
		rpt->rt_rebuild_ver);

	/* Wait for dtx resync to finish */
	while (rpt->rt_global_dtx_resync_version < rpt->rt_rebuild_ver) {
		if (!rpt->rt_abort && !rpt->rt_finishing) {
			ABT_mutex_lock(rpt->rt_lock);
			if (rpt->rt_global_dtx_resync_version < rpt->rt_rebuild_ver) {
				D_INFO(DF_RB " wait for global dtx %u\n", DP_RB_RPT(rpt),
				       rpt->rt_global_dtx_resync_version);
				       wait = true;
				ABT_cond_wait(rpt->rt_global_dtx_wait_cond, rpt->rt_lock);
			}
			ABT_mutex_unlock(rpt->rt_lock);
		}
		if (rpt->rt_abort || rpt->rt_finishing) {
			DL_INFO(-DER_SHUTDOWN, DF_RB ": shutdown rebuild", DP_RB_RPT(rpt));
			D_GOTO(out, rc = -DER_SHUTDOWN);
		}
	}

	if (wait)
		D_INFO(DF_RB " scan collective begin\n", DP_RB_RPT(rpt));
	else
		D_DEBUG(DB_REBUILD, DF_RB " scan collective begin\n", DP_RB_RPT(rpt));

	rc = ds_pool_thread_collective(rpt->rt_pool_uuid, PO_COMP_ST_NEW | PO_COMP_ST_DOWN |
				       PO_COMP_ST_DOWNOUT, rebuild_scanner, rpt,
				       DSS_ULT_DEEP_STACK);
	if (rc)
		D_GOTO(out, rc);

	if (wait)
		D_INFO(DF_RB "rebuild scan collective done\n", DP_RB_RPT(rpt));
	else
		D_DEBUG(DB_REBUILD, DF_RB "rebuild scan collective done\n", DP_RB_RPT(rpt));

	ABT_mutex_lock(rpt->rt_lock);
	rc = ds_pool_task_collective(rpt->rt_pool_uuid, PO_COMP_ST_NEW | PO_COMP_ST_DOWN |
				     PO_COMP_ST_DOWNOUT, rebuild_scan_done, rpt, 0);
	ABT_mutex_unlock(rpt->rt_lock);
	if (rc) {
		DL_ERROR(rc, DF_RB " send rebuild object list failed", DP_RB_RPT(rpt));
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_REBUILD, DF_RB " sent objects to initiator: " DF_RC "\n", DP_RB_RPT(rpt),
		DP_RC(rc));
out:
	tls = rebuild_pool_tls_lookup(rpt->rt_pool_uuid, rpt->rt_rebuild_ver,
				      rpt->rt_rebuild_gen);
	D_ASSERT(tls != NULL);
	if (tls->rebuild_pool_status == 0 && rc != 0)
		tls->rebuild_pool_status = rc;
	DL_INFO(rc, DF_RB " scan leader done", DP_RB_RPT(rpt));
	rpt_put(rpt);
}

/* Scan the local target and generate rebuild object list */
void
rebuild_tgt_scan_handler(crt_rpc_t *rpc)
{
	struct rebuild_scan_in		*rsi;
	struct rebuild_scan_out		*rout;
	struct rebuild_pool_tls		*tls = NULL;
	struct rebuild_tgt_pool_tracker	*rpt = NULL;
	int				 rc;

	rsi = crt_req_get(rpc);
	D_ASSERT(rsi != NULL);

	D_INFO(DF_RB "\n", DP_RB_RSI(rsi));

	/* If PS leader has been changed, and rebuild version is also increased
	 * due to adding new failure targets for rebuild, let's abort previous
	 * rebuild.
	 */
	d_list_for_each_entry(rpt, &rebuild_gst.rg_tgt_tracker_list, rt_list) {
		if (uuid_compare(rpt->rt_pool_uuid, rsi->rsi_pool_uuid) == 0 &&
		    rpt->rt_rebuild_ver < rsi->rsi_rebuild_ver &&
		    rpt->rt_rebuild_op == rsi->rsi_rebuild_op) {
			D_INFO("existing " DF_RB " < incoming " DF_RB "\n", DP_RB_RPT(rpt),
			       DP_RB_RSI(rsi));
			rpt->rt_abort = 1;
			if (rpt->rt_leader_rank != rsi->rsi_master_rank) {
				D_DEBUG(DB_REBUILD,
					"leader change existing " DF_RBF " incoming " DF_RBF "\n",
					DP_RBF_RPT(rpt), DP_RBF_RSI(rsi));
				/* If this is the old leader, then also stop the rebuild
				 * tracking ULT.
				 */
				rebuild_leader_stop(rsi->rsi_pool_uuid, rsi->rsi_rebuild_ver,
						    -1, rpt->rt_leader_term);
			}
		}
	}

	/* check if the rebuild with different leader is already started */
	rpt = rpt_lookup(rsi->rsi_pool_uuid, -1, rsi->rsi_rebuild_ver, -1);
	if (rpt != NULL && rpt->rt_rebuild_op == rsi->rsi_rebuild_op) {
		if (rpt->rt_global_done) {
			D_WARN("previous not cleaned up yet " DF_RBF "\n", DP_RBF_RPT(rpt));
			D_GOTO(out, rc = -DER_BUSY);
		}

		/* Rebuild should never skip the version */
		D_ASSERTF(rsi->rsi_rebuild_ver == rpt->rt_rebuild_ver,
			  "rsi_rebuild_ver %d != rt_rebuild_ver %d\n",
			  rsi->rsi_rebuild_ver, rpt->rt_rebuild_ver);

		/* The same PS leader request rebuild with higher rsi_rebuild_gen.
		 * Is the case of massive failure case, see pool_restart_rebuild_if_rank_wip().
		 */
		if (rpt->rt_leader_rank == rsi->rsi_master_rank &&
		    rpt->rt_leader_term == rsi->rsi_leader_term &&
		    rpt->rt_rebuild_gen < rsi->rsi_rebuild_gen) {
			/* rebuild_leader_status_notify(LAZY rebuild_iv_update),
			 * it will set rpt->rt_global_done to abort rpt.
			 * set rt_abort here just for safe.
			 */
			rpt->rt_abort = 1;
			D_INFO(DF_RBF ", start new rebuild, gen %d -> %d.\n",
			       DP_RBF_RPT(rpt), rpt->rt_rebuild_gen, rsi->rsi_rebuild_gen);
			rpt_put(rpt);
			rpt = NULL;
			goto tls_lookup;
		}

		D_DEBUG(DB_REBUILD, "already started, existing " DF_RBF ", req " DF_RBF "\n",
			DP_RBF_RPT(rpt), DP_RBF_RSI(rsi));

		/* Ignore the rebuild trigger request if it comes from
		 * an old or same leader.
		 */
		if (rsi->rsi_leader_term <= rpt->rt_leader_term)
			D_GOTO(out, rc = 0);

		if (rpt->rt_leader_rank != rsi->rsi_master_rank) {
			D_DEBUG(DB_REBUILD, "new leader existing " DF_RBF "-> req " DF_RBF "\n",
				DP_RBF_RPT(rpt), DP_RBF_RSI(rsi));
			/* re-report the #rebuilt cnt next time */
			rpt->rt_re_report = 1;

			rpt->rt_leader_rank = rsi->rsi_master_rank;

			/* If this is the old leader, then also stop the rebuild tracking ULT. */
			rebuild_leader_stop(rsi->rsi_pool_uuid, rsi->rsi_rebuild_ver,
					    -1, rpt->rt_leader_term);
		}

		rpt->rt_leader_term = rsi->rsi_leader_term;

		D_GOTO(out, rc = 0);
	} else if (rpt != NULL) {
		rpt_put(rpt);
		rpt = NULL;
	}

tls_lookup:
	tls = rebuild_pool_tls_lookup(rsi->rsi_pool_uuid, rsi->rsi_rebuild_ver,
				      rsi->rsi_rebuild_gen);
	if (tls != NULL) {
		D_WARN("previous not cleaned up yet " DF_RBF, DP_RBF_RSI(rsi));
		D_GOTO(out, rc = -DER_BUSY);
	}

	if (daos_fail_check(DAOS_REBUILD_TGT_START_FAIL))
		D_GOTO(out, rc = -DER_INVAL);

	rc = rebuild_tgt_prepare(rpc, &rpt);
	if (rc)
		D_GOTO(out, rc);

	rpt_get(rpt);
	rc = dss_ult_create(rebuild_tgt_status_check_ult, rpt, DSS_XS_SELF,
			    0, DSS_DEEP_STACK_SZ, NULL);
	if (rc) {
		rpt_put(rpt);
		D_GOTO(out, rc);
	}

	rpt->rt_pool->sp_rebuilding++; /* reset in rebuild_tgt_fini */

	rpt_get(rpt);
	/* step-3: start scan leader */
	rc = dss_ult_create(rebuild_scan_leader, rpt, DSS_XS_SELF, 0, 0, NULL);
	if (rc != 0) {
		rpt_put(rpt);
		D_GOTO(out, rc);
	}

out:
	if (tls && tls->rebuild_pool_status == 0 && rc != 0)
		tls->rebuild_pool_status = rc;

	if (rpt) {
		if (rc)
			rpt_delete(rpt);
		rpt_put(rpt);
	}
	rout = crt_reply_get(rpc);
	rout->rso_status = rc;
	rout->rso_stable_epoch = d_hlc_get();
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
