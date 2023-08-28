/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos_sr
 *
 * src/object/cli_obj.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/object.h>
#include <daos/container.h>
#include <daos/cont_props.h>
#include <daos/pool.h>
#include <daos/task.h>
#include <daos_task.h>
#include <daos_types.h>
#include <daos_obj.h>
#include "obj_rpc.h"
#include "obj_internal.h"
#include "cli_csum.h"

/**
 * Open an object shard (shard object), cache the open handle.
 */
int
obj_shard_open(struct dc_object *obj, unsigned int shard, unsigned int map_ver,
	       struct dc_obj_shard **shard_ptr)
{
	struct dc_obj_shard	*obj_shard;
	bool			 lock_upgraded = false;
	int			 rc = 0;

	if (shard >= obj->cob_shards_nr) {
		D_ERROR("shard %u obj_shards_nr %u\n", shard,
			obj->cob_shards_nr);
		return -DER_INVAL;
	}

	D_RWLOCK_RDLOCK(&obj->cob_lock);
open_retry:
	if (obj->cob_version != map_ver) {
		D_DEBUG(DB_IO, "ol ver %d != map ver %d\n",
			obj->cob_version, map_ver);
		D_GOTO(unlock, rc = -DER_STALE);
	}

	obj_shard = &obj->cob_shards->do_shards[shard];

	/* Skip the invalid shards and targets */
	if (obj_shard->do_shard == -1 || obj_shard->do_target_id == -1) {
		D_DEBUG(DB_IO, "shard %u does not exist.\n", shard);
		D_GOTO(unlock, rc = -DER_NONEXIST);
	}

	D_DEBUG(DB_TRACE, "Open object shard %d\n", shard);

	if (obj_shard->do_obj == NULL) {
		daos_unit_oid_t	 oid;

		/* upgrade to write lock to safely update open shard cache */
		if (!lock_upgraded) {
			D_RWLOCK_UNLOCK(&obj->cob_lock);
			D_RWLOCK_WRLOCK(&obj->cob_lock);
			lock_upgraded = true;
			goto open_retry;
		}

		oid.id_shard  = obj_shard->do_shard;
		oid.id_pub    = obj->cob_md.omd_id;
		oid.id_layout_ver = obj->cob_layout_version;
		oid.id_padding = 0;
		/* NB: obj open is a local operation, so it is ok to call
		 * it in sync mode, at least for now.
		 */
		rc = dc_obj_shard_open(obj, oid, obj->cob_mode, obj_shard);
		if (rc)
			D_GOTO(unlock, rc);
	}

	if (rc == 0) {
		/* hold the object shard */
		obj_shard_addref(obj_shard);
		*shard_ptr = obj_shard;
	}

unlock:
	D_RWLOCK_UNLOCK(&obj->cob_lock);
	return rc;
}

static int
close_shard_cb(tse_task_t *task, void *data)
{
	struct dc_obj_shard *obj_shard = *((struct dc_obj_shard **)data);

	obj_shard_close(obj_shard);
	return 0;
}

static void
obj_layout_free(struct dc_object *obj)
{
	struct dc_obj_layout	*layout = NULL;
	int			 i;

	if (obj->cob_shards == NULL)
		return;

	for (i = 0; i < obj->cob_shards_nr; i++) {
		if (obj->cob_shards->do_shards[i].do_obj != NULL)
			obj_shard_close(&obj->cob_shards->do_shards[i]);
	}

	D_SPIN_LOCK(&obj->cob_spin);
	if (obj->cob_shards->do_open_count == 0)
		layout = obj->cob_shards;
	obj->cob_shards = NULL;
	obj->cob_shards_nr = 0;
	D_SPIN_UNLOCK(&obj->cob_spin);

	D_FREE(layout);
}

static void
obj_free(struct d_hlink *hlink)
{
	struct dc_object *obj;

	obj = container_of(hlink, struct dc_object, cob_hlink);
	D_ASSERT(daos_hhash_link_empty(&obj->cob_hlink));
	dc_pool_put(obj->cob_pool);
	dc_cont_put(obj->cob_co);
	obj_layout_free(obj);
	D_FREE(obj->cob_time_fetch_leader);
	D_SPIN_DESTROY(&obj->cob_spin);
	D_RWLOCK_DESTROY(&obj->cob_lock);
	D_FREE(obj);
}

static struct d_hlink_ops obj_h_ops = {
	.hop_free	= obj_free,
};

static struct dc_object *
obj_alloc(void)
{
	struct dc_object *obj;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return NULL;

	daos_hhash_hlink_init(&obj->cob_hlink, &obj_h_ops);
	return obj;
}

void
obj_decref(struct dc_object *obj)
{
	if (obj != NULL)
		daos_hhash_link_putref(&obj->cob_hlink);
}

struct dc_object *
obj_addref(struct dc_object *obj)
{
	if (obj != NULL)
		daos_hhash_link_getref(&obj->cob_hlink);
	return obj;
}

struct dc_object *
obj_hdl2ptr(daos_handle_t oh)
{
	struct d_hlink *hlink;

	hlink = daos_hhash_link_lookup(oh.cookie);
	if (hlink == NULL)
		return NULL;

	return container_of(hlink, struct dc_object, cob_hlink);
}

static void
obj_hdl_link(struct dc_object *obj)
{
	daos_hhash_link_insert(&obj->cob_hlink, DAOS_HTYPE_OBJ);
}

static void
obj_hdl_unlink(struct dc_object *obj)
{
	daos_hhash_link_delete(&obj->cob_hlink);
}

static uint32_t
dc_obj_get_redun_lvl(struct dc_object *obj)
{
	struct cont_props	props;

	props = obj->cob_co->dc_props;

	return props.dcp_redun_lvl;
}

uint32_t
dc_obj_hdl2redun_lvl(daos_handle_t oh)
{
	struct dc_object	*obj;
	uint32_t		 lvl;

	obj = obj_hdl2ptr(oh);
	D_ASSERT(obj != NULL);
	lvl = dc_obj_get_redun_lvl(obj);
	obj_decref(obj);
	return lvl;
}

daos_handle_t
dc_obj_hdl2cont_hdl(daos_handle_t oh)
{
	struct dc_object *obj;
	daos_handle_t hdl;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return DAOS_HDL_INVAL;

	daos_hhash_link_key(&obj->cob_co->dc_hlink, &hdl.cookie);
	obj_decref(obj);
	return hdl;
}

uint32_t
dc_obj_hdl2layout_ver(daos_handle_t oh)
{
	struct dc_object *obj;
	uint32_t ver;

	obj = obj_hdl2ptr(oh);
	D_ASSERT(obj != NULL);
	ver = obj->cob_layout_version;
	obj_decref(obj);
	return ver;
}

static uint32_t
dc_obj_get_pda(struct dc_object *obj)
{
	return daos_cont_props2pda(&obj->cob_co->dc_props, obj_is_ec(obj));
}

uint32_t
dc_obj_hdl2pda(daos_handle_t oh)
{
	struct dc_object	*obj;
	uint32_t		 pda;

	obj = obj_hdl2ptr(oh);
	D_ASSERT(obj != NULL);
	pda = dc_obj_get_pda(obj);
	obj_decref(obj);
	return pda;
}

static uint32_t
dc_obj_get_pdom(struct dc_object *obj)
{
	return obj->cob_co->dc_props.dcp_perf_domain;
}

uint32_t
dc_obj_hdl2pdom(daos_handle_t oh)
{
	struct dc_object	*obj;
	uint32_t		 pdom;

	obj = obj_hdl2ptr(oh);
	D_ASSERT(obj != NULL);
	pdom = dc_obj_get_pdom(obj);
	obj_decref(obj);
	return pdom;
}

static int
obj_layout_create(struct dc_object *obj, unsigned int mode, bool refresh)
{
	struct pl_obj_layout	*layout = NULL;
	struct dc_pool		*pool;
	struct pl_map		*map;
	uint32_t		old;
	int			i;
	int			rc;

	pool = obj->cob_pool;
	D_ASSERT(pool != NULL);

	map = pl_map_find(pool->dp_pool, obj->cob_md.omd_id);
	if (map == NULL) {
		D_DEBUG(DB_PL, "Cannot find valid placement map\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	obj->cob_md.omd_ver = dc_pool_get_version(pool);
	obj->cob_md.omd_pdom_lvl = dc_obj_get_pdom(obj);
	obj->cob_md.omd_fdom_lvl = dc_obj_get_redun_lvl(obj);
	obj->cob_md.omd_pda = dc_obj_get_pda(obj);
	rc = obj_pl_place(map, obj->cob_layout_version, &obj->cob_md, mode,
			  NULL, &layout);
	pl_map_decref(map);
	if (rc != 0) {
		D_DEBUG(DB_PL, DF_OID" Failed to generate object layout fdom_lvl %d\n",
			DP_OID(obj->cob_md.omd_id), obj->cob_md.omd_fdom_lvl);
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_PL, DF_OID" Place object on %d targets ver %d, fdom_lvl %d\n",
		DP_OID(obj->cob_md.omd_id), layout->ol_nr, layout->ol_ver,
		obj->cob_md.omd_fdom_lvl);
	D_ASSERT(layout->ol_nr == layout->ol_grp_size * layout->ol_grp_nr);

	if (refresh)
		obj_layout_dump(obj->cob_md.omd_id, layout);

	obj->cob_version = layout->ol_ver;

	D_ASSERT(obj->cob_shards == NULL);
	D_ALLOC(obj->cob_shards, sizeof(struct dc_obj_layout) +
		sizeof(struct dc_obj_shard) * layout->ol_nr);
	if (obj->cob_shards == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	obj->cob_shards_nr = layout->ol_nr;
	obj->cob_grp_size = layout->ol_grp_size;
	old = obj->cob_grp_nr;
	obj->cob_grp_nr = obj->cob_shards_nr / obj->cob_grp_size;

	if (obj->cob_grp_size > 1 && srv_io_mode == DIM_DTX_FULL_ENABLED &&
	    old < obj->cob_grp_nr) {
		D_FREE(obj->cob_time_fetch_leader);

		D_ALLOC_ARRAY(obj->cob_time_fetch_leader, obj->cob_grp_nr);
		if (obj->cob_time_fetch_leader == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	for (i = 0; i < layout->ol_nr; i++) {
		struct dc_obj_shard *obj_shard;

		obj_shard = &obj->cob_shards->do_shards[i];
		obj_shard->do_shard = layout->ol_shards[i].po_shard;
		obj_shard->do_shard_idx = i;
		obj_shard->do_target_id = layout->ol_shards[i].po_target;
		obj_shard->do_fseq = layout->ol_shards[i].po_fseq;
		obj_shard->do_rebuilding = layout->ol_shards[i].po_rebuilding;
		obj_shard->do_reintegrating = layout->ol_shards[i].po_reintegrating;
	}
out:
	if (layout)
		pl_obj_layout_free(layout);
	return rc;
}

static int
obj_layout_refresh(struct dc_object *obj)
{
	int	rc;

	D_RWLOCK_WRLOCK(&obj->cob_lock);
	obj_layout_free(obj);
	rc = obj_layout_create(obj, 0, true);
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return rc;
}

static bool
tgt_in_failed_tgts_list(int32_t tgt, struct obj_auxi_tgt_list *tgt_list)
{
	int i;

	D_ASSERT(tgt_list != NULL);
	for (i = 0; i < tgt_list->tl_nr; i++) {
		if (tgt_list->tl_tgts[i] == tgt)
			return true;
	}

	return false;
}

static int
obj_auxi_add_failed_tgt(struct obj_auxi_args *obj_auxi, uint32_t tgt)
{
	struct obj_auxi_tgt_list	*tgt_list = obj_auxi->failed_tgt_list;
	bool				allocated = false;
	uint32_t			*tgts;

	if (tgt_list == NULL) {
		D_ALLOC_PTR(tgt_list);
		if (tgt_list == NULL)
			return -DER_NOMEM;
		allocated = true;
	} else {
		if (tgt_in_failed_tgts_list(tgt, tgt_list)) {
			D_DEBUG(DB_IO, "tgt %u exists in failed.\n", tgt);
			return 0;
		}
	}

	D_REALLOC_ARRAY(tgts, tgt_list->tl_tgts, tgt_list->tl_nr,
			tgt_list->tl_nr + 1);
	if (tgts == NULL) {
		if (allocated)
			D_FREE(tgt_list);
		return -DER_NOMEM;
	}
	D_DEBUG(DB_IO, "Add tgt %u to %p failed list.\n", tgt, obj_auxi);
	tgts[tgt_list->tl_nr] = tgt;
	tgt_list->tl_tgts = tgts;
	tgt_list->tl_nr++;
	obj_auxi->failed_tgt_list = tgt_list;

	return 0;
}

static void
obj_auxi_free_failed_tgt_list(struct obj_auxi_args *obj_auxi)
{
	if (obj_auxi->failed_tgt_list == NULL)
		return;

	D_FREE(obj_auxi->failed_tgt_list->tl_tgts);
	D_FREE(obj_auxi->failed_tgt_list);
}

static int
obj_init_oca(struct dc_object *obj)
{
	struct daos_oclass_attr *oca;
	uint32_t		nr_grps;

	oca = daos_oclass_attr_find(obj->cob_md.omd_id, &nr_grps);
	if (!oca)
		return -DER_INVAL;

	obj->cob_oca = *oca;
	obj->cob_oca.ca_grp_nr = nr_grps;
	if (daos_oclass_is_ec(oca))
		/* Inherit cell size from container property */
		obj->cob_oca.u.ec.e_len = obj->cob_co->dc_props.dcp_ec_cell_sz;

	return 0;
}

struct daos_oclass_attr *
obj_get_oca(struct dc_object *obj)
{
	return &obj->cob_oca;
}

bool
obj_is_ec(struct dc_object *obj)
{
	return daos_oclass_is_ec(obj_get_oca(obj));
}

int
obj_get_replicas(struct dc_object *obj)
{
	struct daos_oclass_attr *oc_attr = obj_get_oca(obj);

	if (daos_oclass_is_ec(oc_attr))
		return obj_ec_tgt_nr(oc_attr);

	D_ASSERT(oc_attr->ca_resil == DAOS_RES_REPL);
	if (oc_attr->u.rp.r_num == DAOS_OBJ_REPL_MAX)
		return obj->cob_grp_size;

	return oc_attr->u.rp.r_num;
}

int
obj_get_grp_size(struct dc_object *obj)
{
	return obj->cob_grp_size;
}

int
dc_obj_get_grp_size(daos_handle_t oh, int *grp_size)
{
	struct dc_object        *obj;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	*grp_size = obj_get_grp_size(obj);
	obj_decref(obj);
	return 0;
}

int
dc_obj_hdl2oid(daos_handle_t oh, daos_obj_id_t *oid)
{
	struct dc_object        *obj;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	*oid = obj->cob_md.omd_id;
	obj_decref(obj);
	return 0;
}

int
obj_get_grp_nr(struct dc_object *obj)
{
	return obj->cob_grp_nr;
}

/* Get a valid shard from an replicate object group for readonly operation */
static int
obj_replica_grp_fetch_valid_shard_get(struct dc_object *obj, int grp_idx,
				      unsigned int map_ver,
				      struct obj_auxi_tgt_list *failed_list)
{
	int grp_start;
	int idx;
	int grp_size;
	int i = 0;

	D_ASSERT(!obj_is_ec(obj));
	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	D_ASSERT(obj->cob_shards_nr > 0);

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_version != map_ver) {
		/* Sigh, someone else change the pool map */
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_STALE;
	}

	if (DAOS_FAIL_CHECK(DAOS_OBJ_TRY_SPECIAL_SHARD)) {
		idx = daos_fail_value_get();
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		D_DEBUG(DB_IO, "choose special idx %d\n", idx);
		return idx;
	}

	D_DEBUG(DB_IO, "grp size %d replicas %d\n", grp_size,
		obj_get_replicas(obj));
	/* Start from an random offset within this group, NB: we should
	 * use replica number directly, instead of group size, which might
	 * included extended shard, see pl_map_extend().
	 */
	D_ASSERT(grp_size >= obj_get_replicas(obj));
	grp_start = grp_idx * grp_size;
	idx = d_rand() % obj_get_replicas(obj);
	for (i = 0; i < obj_get_replicas(obj); i++) {
		uint32_t tgt_id;
		int index;

		index = (idx + i) % obj_get_replicas(obj) + grp_start;
		/* let's skip the rebuild shard */
		if (obj->cob_shards->do_shards[index].do_rebuilding)
			continue;

		/* skip the reintegrating shard as well */
		if (obj->cob_shards->do_shards[index].do_reintegrating)
			continue;

		/* Skip the target which is already in the failed list, i.e.
		 * they have been tried.
		 */
		tgt_id = obj->cob_shards->do_shards[index].do_target_id;
		if (failed_list && tgt_in_failed_tgts_list(tgt_id, failed_list))
			continue;

		if (DAOS_FAIL_CHECK(DAOS_FAIL_SHARD_OPEN) &&
		    daos_shard_in_fail_value(index))
			continue;

		/* Skip the invalid shards and targets */
		if (obj->cob_shards->do_shards[index].do_target_id != -1 ||
		    obj->cob_shards->do_shards[index].do_shard != -1) {
			idx = index;
			break;
		}
	}

	D_RWLOCK_UNLOCK(&obj->cob_lock);

	if (i == obj_get_replicas(obj))
		return -DER_NONEXIST;

	return idx;
}

static int
obj_shard_find_replica(struct dc_object *obj, unsigned int target,
		       struct obj_auxi_tgt_list *tgt_list)
{
	int grp_idx;
	int idx;

	for (idx = 0; idx < obj->cob_shards_nr; idx++) {
		if (obj->cob_shards->do_shards[idx].do_target_id == target)
			break;
	}

	if (idx == obj->cob_shards_nr)
		return -DER_NONEXIST;

	grp_idx = idx / obj_get_replicas(obj);
	return obj_replica_grp_fetch_valid_shard_get(obj, grp_idx, obj->cob_version,
						     tgt_list);
}

static int
obj_ec_leader_select(struct dc_object *obj, int grp_idx, bool cond_modify, uint32_t map_ver,
		     uint64_t dkey_hash, uint8_t *bit_map)
{
	struct daos_oclass_attr *oca;
	struct pl_obj_shard	*pl_shard;
	int			tgt_idx;
	int			grp_size;
	int			grp_start;
	int			rc = 0;
	int			i;
	int			shard = 0;

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_version != map_ver)
		D_GOTO(unlock, rc = -DER_STALE);

	oca = obj_get_oca(obj);
	grp_size = obj_ec_tgt_nr(oca);
	grp_start = grp_idx * obj_get_grp_size(obj);

	/* 1. Find one from parity, and start from the last parity. */
	tgt_idx = obj_ec_shard_idx(obj, dkey_hash, grp_size - 1);
	for (i = 0; i < obj_ec_parity_tgt_nr(oca);
	     i++, tgt_idx = (tgt_idx - 1 + grp_size) % grp_size) {

		shard = grp_start + tgt_idx;
		pl_shard = obj_get_shard(obj, shard);
		if (pl_shard->po_target == -1 || pl_shard->po_shard == -1 ||
		    pl_shard->po_rebuilding ||
		    (DAOS_FAIL_CHECK(DAOS_FAIL_SHARD_OPEN) &&
		     daos_shard_in_fail_value(grp_size - 1 - i))) {
			/* Then try former one */
			continue;
		}
		D_GOTO(unlock, rc = shard);
	}

	/*
	 * If no parity node is available, then handle related task that has conditional
	 * modification via distributed tranasaction.
	 */
	if (cond_modify)
		D_GOTO(unlock, rc = -DER_NEED_TX);

	/* Choose one from data shards within bit_map, and also make sure there are
	 * no further data shards failed.
	 **/
	tgt_idx = obj_ec_shard_idx(obj, dkey_hash, 0);
	for (i = 0; i < obj_ec_data_tgt_nr(oca); i++, tgt_idx = (tgt_idx + 1) % grp_size) {
		if (bit_map != NIL_BITMAP && isclr(bit_map, tgt_idx))
			continue;

		shard = grp_start + tgt_idx;
		pl_shard = obj_get_shard(obj, shard);
		if (pl_shard->po_target == -1 || pl_shard->po_shard == -1 ||
		    pl_shard->po_rebuilding) {
			D_ERROR(DF_OID" unhealthy targets exceed the max redundancy, e_p %d"
				" shard %d %u/%u/%u\n", DP_OID(obj->cob_md.omd_id),
				obj_ec_parity_tgt_nr(oca), shard, pl_shard->po_target,
				pl_shard->po_shard, pl_shard->po_rebuilding);
			D_GOTO(unlock, rc = -DER_IO);
		}
		break;
	}

	if (i == obj_ec_data_tgt_nr(oca)) {
		D_WARN(DF_OID" no shards %d are in bitmaps, retry later.\n",
		       DP_OID(obj->cob_md.omd_id), obj_ec_parity_tgt_nr(oca));
		D_GOTO(unlock, rc = -DER_STALE);
	}
	rc = shard;

unlock:
	D_RWLOCK_UNLOCK(&obj->cob_lock);
	D_DEBUG(DB_TRACE, DF_OID" choose shard %d as leader for group%d layout %u: %d\n",
		DP_OID(obj->cob_md.omd_id), shard, grp_idx, obj->cob_layout_version, rc);

	return rc;
}

static int
obj_replica_leader_select(struct dc_object *obj, unsigned int grp_idx, uint64_t dkey_hash,
			  unsigned int map_ver)
{
	struct pl_obj_shard	*shard;
	struct daos_oclass_attr	*oca;
	uint32_t		grp_size;
	int			start;
	int			pos;
	int			replica_idx;
	int			rc;
	int			i;

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_version != map_ver)
		D_GOTO(unlock, rc = -DER_STALE);

	oca = daos_oclass_attr_find(obj->cob_md.omd_id, NULL);
	D_ASSERT(oca != NULL);
	grp_size = obj_get_grp_size(obj);
	if (grp_size == 1) {
		pos = grp_idx * obj_get_grp_size(obj);
		shard = obj_get_shard(obj, pos);
		if (shard->po_target == -1) {
			D_ERROR(DF_OID" grp_size 1, obj_get_shard failed\n",
				DP_OID(obj->cob_md.omd_id));
			return -DER_IO;
		}

		/*
		 * Note that even though there's only one replica here, this
		 * object can still be rebuilt during addition or drain as
		 * it moves between ranks
		 */
		/* return pos rather than shard->po_shard for pool extending */
		D_GOTO(unlock, rc = pos);
	}

	/* XXX: The shards within [start, start + replicas) will search from
	 *      the same @preferred position, then they will have the same
	 *      leader. The shards (belonging to the same object) in
	 *      other redundancy group may get different leader node.
	 *
	 *      The one with the lowest f_seq will be elected as the leader
	 *      to avoid leader switch.
	 */
	start = grp_idx * obj_get_grp_size(obj);
	replica_idx = (dkey_hash + grp_idx) % grp_size;
	for (i = 0, pos = -1; i < grp_size;
	     i++, replica_idx = (replica_idx + 1) % obj_get_grp_size(obj)) {
		int off = start + replica_idx;

		shard = obj_get_shard(obj, off);
		/*
		 * Cannot select in-rebuilding shard as leader (including the
		 * case that during reintegration we may have an extended
		 * layout that with in-adding shards with po_rebuilding set).
		 */
		if (shard->po_target == -1 || shard->po_shard == -1 || shard->po_rebuilding)
			continue;

		if (pos == -1 || obj_get_shard(obj, pos)->po_fseq > shard->po_fseq)
			pos = off;
	}

	if (pos != -1) {
		/*
		 * Here should not return "pl_get_shard(data, pos)->po_shard",
		 * because it possibly not equal to "pos" in pool extending.
		 */
		rc = pos;
	} else {
		/* If all the replicas are failed or in-rebuilding, then EIO. */
		D_ERROR(DF_OID" all the replicas are failed or in-rebuilding\n",
			DP_OID(obj->cob_md.omd_id));
		rc = -DER_IO;
	}

unlock:
	D_RWLOCK_UNLOCK(&obj->cob_lock);
	return rc;
}

int
obj_grp_leader_get(struct dc_object *obj, int grp_idx, uint64_t dkey_hash,
		   bool cond_modify, unsigned int map_ver, uint8_t *bit_map)
{
	if (obj_is_ec(obj))
		return obj_ec_leader_select(obj, grp_idx, cond_modify, map_ver, dkey_hash,
					    bit_map);

	return obj_replica_leader_select(obj, grp_idx, dkey_hash, map_ver);
}

/* If the client has been asked to fetch (list/query) from leader replica,
 * then means that related data is associated with some prepared DTX that
 * may be committable on the leader replica. According to our current DTX
 * batched commit policy, it is quite possible that such DTX is not ready
 * to be committed, or it is committable but cached on the leader replica
 * for some time. On the other hand, such DTX may contain more data update
 * than current fetch. If the subsequent fetch against the same redundancy
 * group come very soon (within the OBJ_FETCH_LEADER_INTERVAL), then it is
 * possible that related target for the next fetch is covered by the same
 * DTX that is still not committed yet. If the assumption is right, asking
 * the application to fetch from leader replica directly can avoid one RPC
 * round-trip with non-leader replica. If such assumption is wrong, it may
 * increase the server load on which the leader replica resides in a short
 * time but it will not correctness issues.
 */
#define		OBJ_FETCH_LEADER_INTERVAL	2

int
obj_dkey2grpidx(struct dc_object *obj, uint64_t hash, unsigned int map_ver)
{
	struct dc_pool	*pool;
	int		grp_size;
	unsigned int	pool_map_ver;
	uint64_t	grp_idx;

	pool = obj->cob_pool;
	D_ASSERT(pool != NULL);

	D_RWLOCK_RDLOCK(&pool->dp_map_lock);
	pool_map_ver = pool_map_get_version(pool->dp_map);
	D_RWLOCK_UNLOCK(&pool->dp_map_lock);

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_version != map_ver || map_ver < pool_map_ver) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		D_DEBUG(DB_IO, "cob_ersion %u map_ver %u pool_map_ver %u\n",
			obj->cob_version, map_ver, pool_map_ver);
		return -DER_STALE;
	}

	D_ASSERT(obj->cob_shards_nr >= grp_size);

	grp_idx = obj_pl_grp_idx(obj->cob_layout_version, hash,
				 obj->cob_shards_nr / grp_size);
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return grp_idx;
}

static int
obj_dkey2grpmemb(struct dc_object *obj, uint64_t hash, uint32_t map_ver,
		 uint32_t *start_shard, uint32_t *grp_size)
{
	int	 grp_idx;

	grp_idx = obj_dkey2grpidx(obj, hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	*grp_size = obj_get_grp_size(obj);
	*start_shard = grp_idx * *grp_size;
	return 0;
}

static int
obj_shard2tgtid(struct dc_object *obj, uint32_t shard, uint32_t map_ver,
		uint32_t *tgt_id)
{
	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (map_ver == obj->cob_version)
		D_ASSERTF(shard < obj->cob_shards_nr, "bad shard %d exceed %d "
			  "map_ver %d\n", shard, obj->cob_shards_nr, map_ver);
	if (shard >= obj->cob_shards_nr) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_NONEXIST;
	}

	*tgt_id = obj->cob_shards->do_shards[shard].do_target_id;
	D_RWLOCK_UNLOCK(&obj->cob_lock);
	return 0;
}

/**
 * Create reasb_req and set iod's value, akey reuse buffer from input
 * iod, iod_type/iod_size assign as input iod, iod_kcsum/iod_nr/iod_recx/
 * iod_csums/iod_eprs array will set as 0/NULL.
 */
int
obj_reasb_req_init(struct obj_reasb_req *reasb_req, struct dc_object *obj, daos_iod_t *iods,
		   uint32_t iod_nr)
{
	daos_size_t			 size_iod, size_sgl, size_oiod;
	daos_size_t			 size_recx, size_tgt_nr, size_singv;
	daos_size_t			 size_sorter, size_array, size_fetch_stat, buf_size;
	daos_iod_t			*uiod, *riod;
	struct obj_ec_recx_array	*ec_recx;
	void				*buf;
	uint8_t				*tmp_ptr;
	int				 i;

	reasb_req->orr_oca = obj_get_oca(obj);
	size_iod = roundup(sizeof(daos_iod_t) * iod_nr, 8);
	size_sgl = roundup(sizeof(d_sg_list_t) * iod_nr, 8);
	size_oiod = roundup(sizeof(struct obj_io_desc) * iod_nr, 8);
	size_recx = roundup(sizeof(struct obj_ec_recx_array) * iod_nr, 8);
	size_sorter = roundup(sizeof(struct obj_ec_seg_sorter) * iod_nr, 8);
	size_singv = roundup(sizeof(struct dcs_layout) * iod_nr, 8);
	size_array = sizeof(daos_size_t) * obj_get_grp_size(obj) * iod_nr;
	size_fetch_stat = sizeof(struct shard_fetch_stat) * iod_nr;
	/* for oer_tgt_recx_nrs/_idxs */
	size_tgt_nr = roundup(sizeof(uint32_t) * obj_get_grp_size(obj), 8);
	buf_size = size_iod + size_sgl + size_oiod + size_recx + size_sorter +
		   size_singv + size_array + size_tgt_nr * iod_nr * 2 + OBJ_TGT_BITMAP_LEN +
		   size_fetch_stat;
	D_ALLOC(buf, buf_size);
	if (buf == NULL)
		return -DER_NOMEM;

	tmp_ptr = buf;
	reasb_req->orr_iods = (void *)tmp_ptr;
	tmp_ptr += size_iod;
	reasb_req->orr_sgls = (void *)tmp_ptr;
	tmp_ptr += size_sgl;
	reasb_req->orr_oiods = (void *)tmp_ptr;
	tmp_ptr += size_oiod;
	reasb_req->orr_recxs = (void *)tmp_ptr;
	tmp_ptr += size_recx;
	reasb_req->orr_sorters = (void *)tmp_ptr;
	tmp_ptr += size_sorter;
	reasb_req->orr_singv_los = (void *)tmp_ptr;
	tmp_ptr += size_singv;
	reasb_req->orr_data_sizes = (void *)tmp_ptr;
	tmp_ptr += size_array;
	reasb_req->tgt_bitmap = (void *)tmp_ptr;
	tmp_ptr += OBJ_TGT_BITMAP_LEN;
	reasb_req->orr_fetch_stat = (void *)tmp_ptr;
	tmp_ptr += size_fetch_stat;

	for (i = 0; i < iod_nr; i++) {
		uiod = &iods[i];
		riod = &reasb_req->orr_iods[i];
		riod->iod_name = uiod->iod_name;
		riod->iod_type = uiod->iod_type;
		riod->iod_size = uiod->iod_size;
		ec_recx = &reasb_req->orr_recxs[i];
		ec_recx->oer_tgt_recx_nrs = (void *)tmp_ptr;
		tmp_ptr += size_tgt_nr;
		ec_recx->oer_tgt_recx_idxs = (void *)tmp_ptr;
		tmp_ptr += size_tgt_nr;
	}

	D_ASSERT((uintptr_t)(tmp_ptr - size_tgt_nr) <=
		 (uintptr_t)(buf + buf_size));
	D_MUTEX_INIT(&reasb_req->orr_mutex, NULL);

	return 0;
}

void
obj_reasb_req_fini(struct obj_reasb_req *reasb_req, uint32_t iod_nr)
{
	daos_iod_t			*iod;
	int				 i;

	if (reasb_req->orr_iods == NULL)
		return;

	for (i = 0; i < iod_nr; i++) {
		iod = &reasb_req->orr_iods[i];
		D_FREE(iod->iod_recxs);
		d_sgl_fini(&reasb_req->orr_sgls[i], false);
		obj_io_desc_fini(&reasb_req->orr_oiods[i]);
		obj_ec_recxs_fini(&reasb_req->orr_recxs[i]);
		obj_ec_seg_sorter_fini(&reasb_req->orr_sorters[i]);
		obj_ec_tgt_oiod_fini(reasb_req->tgt_oiods);
		reasb_req->tgt_oiods = NULL;
	}
	D_MUTEX_DESTROY(&reasb_req->orr_mutex);
	obj_ec_fail_info_free(reasb_req);
	D_FREE(reasb_req->orr_iods);
	memset(reasb_req, 0, sizeof(*reasb_req));
}

static int
obj_rw_req_reassemb(struct dc_object *obj, daos_obj_rw_t *args,
		    struct dtx_epoch *epoch, struct obj_auxi_args *obj_auxi)
{
	struct obj_reasb_req	*reasb_req = &obj_auxi->reasb_req;
	daos_obj_id_t		 oid = obj->cob_md.omd_id;
	int			 rc = 0;

	D_ASSERT(obj_is_ec(obj));

	if (epoch != NULL && !obj_auxi->req_reasbed)
		reasb_req->orr_epoch = *epoch;
	if (obj_auxi->req_reasbed) {
		D_DEBUG(DB_TRACE, DF_OID" req reassembled (retry case).\n", DP_OID(oid));
		D_ASSERTF(reasb_req->orr_iod_nr == args->nr, "%d != %d.\n",
			  reasb_req->orr_iod_nr, args->nr);
		memset(reasb_req->orr_fetch_stat, 0, args->nr * sizeof(*reasb_req->orr_fetch_stat));
		if (!reasb_req->orr_size_fetched)
			return 0;
	}

	if (args->extra_flags & DIOF_CHECK_EXISTENCE ||
	    args->extra_flags & DIOF_TO_SPEC_SHARD)
		return 0;

	if (!obj_auxi->req_reasbed) {
		rc = obj_reasb_req_init(&obj_auxi->reasb_req, obj, args->iods, args->nr);
		if (rc) {
			D_ERROR(DF_OID" obj_reasb_req_init failed %d.\n",
				DP_OID(oid), rc);
			return rc;
		}
		reasb_req->orr_args = args;
	}

	rc = obj_ec_req_reasb(obj, args->iods, obj_auxi->dkey_hash,
			      args->sgls, reasb_req, args->nr,
			      obj_auxi->opc == DAOS_OBJ_RPC_UPDATE);
	if (rc == 0) {
		obj_auxi->flags |= ORF_EC;
		obj_auxi->req_reasbed = true;
		if (reasb_req->orr_iods != NULL)
			args->iods = reasb_req->orr_iods;
		if (reasb_req->orr_sgls != NULL &&
		    !reasb_req->orr_size_fetch &&
		    !reasb_req->orr_single_tgt)
			args->sgls = reasb_req->orr_sgls;
	} else {
		D_ERROR(DF_OID" obj_ec_req_reasb failed %d.\n",
			DP_OID(oid), rc);
		obj_reasb_req_fini(&obj_auxi->reasb_req, obj_auxi->iod_nr);
	}

	return rc;
}

bool
obj_op_is_ec_fetch(struct obj_auxi_args *obj_auxi)
{
	return obj_auxi->is_ec_obj && obj_auxi->opc == DAOS_OBJ_RPC_FETCH;
}

/**
 * Query target info. ec_tgt_idx only used for EC obj fetch.
 */
static int
obj_shard_tgts_query(struct dc_object *obj, uint32_t map_ver, uint32_t shard,
		     struct daos_shard_tgt *shard_tgt,
		     struct obj_auxi_args *obj_auxi, uint8_t *bitmap)
{
	struct dc_obj_shard	*obj_shard;
	int			rc;

	rc = obj_shard_open(obj, shard, map_ver, &obj_shard);
	if (rc != 0) {
		D_CDEBUG(rc == -DER_STALE || rc == -DER_NONEXIST, DB_IO, DLOG_ERR,
			 DF_OID " obj_shard_open %u opc %u, rc " DF_RC "\n",
			 DP_OID(obj->cob_md.omd_id), obj_auxi->opc, shard, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (bitmap != NIL_BITMAP) {
		uint32_t tgt_idx;
		uint32_t grp_idx;

		grp_idx = shard / obj_get_grp_size(obj);
		tgt_idx = obj_shard->do_id.id_shard -
			  grp_idx * daos_oclass_grp_size(obj_get_oca(obj_auxi->obj));

		if (isclr(bitmap, tgt_idx)) {
			D_DEBUG(DB_TRACE, DF_OID" shard %u is not in bitmap\n",
				DP_OID(obj->cob_md.omd_id), obj_shard->do_id.id_shard);
			D_GOTO(close, rc = -DER_NONEXIST);
		}
		shard_tgt->st_ec_tgt = tgt_idx;
	}
	shard_tgt->st_rank	= obj_shard->do_target_rank;
	shard_tgt->st_shard	= shard;
	shard_tgt->st_shard_id	= obj_shard->do_id.id_shard;
	shard_tgt->st_tgt_idx	= obj_shard->do_target_idx;
	if (obj_auxi->cond_modify && (obj_shard->do_rebuilding || obj_shard->do_reintegrating))
		shard_tgt->st_flags |= DTF_DELAY_FORWARD;
	if (obj_shard->do_reintegrating)
		obj_auxi->reintegrating = 1;
	if (obj_shard->do_rebuilding)
		obj_auxi->rebuilding = 1;

	rc = obj_shard2tgtid(obj, shard, map_ver, &shard_tgt->st_tgt_id);
	D_DEBUG(DB_TRACE, DF_OID" shard %u rank %u tgt %u %d/%d %p: %d\n",
		DP_OID(obj->cob_md.omd_id), shard, (uint32_t)shard_tgt->st_rank,
		(uint32_t)shard_tgt->st_tgt_id, obj_shard->do_reintegrating,
		obj_shard->do_rebuilding, obj->cob_shards, rc);
close:
	obj_shard_close(obj_shard);
out:
	return rc;
}

/* a helper for debugging purpose */
void
obj_req_tgts_dump(struct obj_req_tgts *req_tgts)
{
	int	i, j;

	D_PRINT("content of obj_req_tgts %p:\n", req_tgts);
	D_PRINT("ort_srv_disp %d, ort_start_shard %d, ort_grp_nr %d, "
		"ort_grp_size %d.\n", req_tgts->ort_srv_disp,
		req_tgts->ort_start_shard, req_tgts->ort_grp_nr,
		req_tgts->ort_grp_size);
	for (i = 0; i < req_tgts->ort_grp_nr; i++) {
		struct daos_shard_tgt *tgt;

		tgt = req_tgts->ort_shard_tgts + i * req_tgts->ort_grp_size;
		D_PRINT("grp %4d - ", i);
		for (j = 0; j < req_tgts->ort_grp_size; j++) {
			if (j > 0)
				D_PRINT("           ");
			D_PRINT("[%4d] rank %4d, shard %4d, tgt_idx %4d, "
				"tgt_id %4d.\n", j, tgt->st_rank, tgt->st_shard,
				tgt->st_tgt_idx, tgt->st_tgt_id);
			tgt++;
		}
		D_PRINT("\n");
	}
}

/* only send to leader and need not forward */
#define OBJ_TGT_FLAG_LEADER_ONLY	(1U << 0)
/* client side dispatch, despite of srv_io_mode setting */
#define OBJ_TGT_FLAG_CLI_DISPATCH	(1U << 1)
/* Forward leader information. */
#define OBJ_TGT_FLAG_FW_LEADER_INFO	(1U << 2)

static int
obj_shards_2_fwtgts(struct dc_object *obj, uint32_t map_ver, uint8_t *bit_map,
		    uint32_t start_shard, uint32_t shard_cnt, uint32_t grp_nr,
		    uint32_t flags, struct obj_auxi_args *obj_auxi)
{
	struct obj_req_tgts	*req_tgts = &obj_auxi->req_tgts;
	struct daos_shard_tgt	*tgt = NULL;
	struct daos_oclass_attr	*oca = obj_get_oca(obj);
	uint32_t		 i;
	uint32_t		 shard_idx, grp_size;
	bool			 cli_disp = flags & OBJ_TGT_FLAG_CLI_DISPATCH;
	int			 rc = 0;

	D_ASSERT(shard_cnt >= 1);
	grp_size = shard_cnt / grp_nr;
	D_ASSERT(grp_size * grp_nr == shard_cnt);
	if (cli_disp || bit_map != NIL_BITMAP)
		D_ASSERT(grp_nr == 1);
	/* start_shard is the shard index, but ort_start_shard is the start shard ID.
	 * in OSA case, possibly obj_get_grp_size > daos_oclass_grp_size so the start_shard
	 * is different with ort_start_shard.
	 */
	req_tgts->ort_start_shard = (start_shard / obj_get_grp_size(obj)) *
				    daos_oclass_grp_size(oca);
	req_tgts->ort_srv_disp = !cli_disp && grp_size > 1;

	if (shard_cnt > OBJ_TGT_INLINE_NR) {
		if (req_tgts->ort_shard_tgts != NULL &&
		    req_tgts->ort_grp_nr * req_tgts->ort_grp_size != shard_cnt) {
			if (req_tgts->ort_shard_tgts != req_tgts->ort_tgts_inline)
				D_FREE(req_tgts->ort_shard_tgts);
			req_tgts->ort_shard_tgts = NULL;
		}
		if (req_tgts->ort_shard_tgts == NULL) {
			D_ALLOC_ARRAY(req_tgts->ort_shard_tgts, shard_cnt);
			if (req_tgts->ort_shard_tgts == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	} else {
		if (req_tgts->ort_shard_tgts != NULL &&
		    req_tgts->ort_shard_tgts != req_tgts->ort_tgts_inline)
			D_FREE(req_tgts->ort_shard_tgts);
		req_tgts->ort_shard_tgts = req_tgts->ort_tgts_inline;
	}

	if (obj_auxi->spec_shard) {
		D_ASSERT(grp_nr == 1);
		D_ASSERT(shard_cnt == 1);
		D_ASSERT(bit_map == NIL_BITMAP);
		D_ASSERT(req_tgts->ort_srv_disp == 0);

		tgt = req_tgts->ort_shard_tgts;

		req_tgts->ort_grp_nr = 1;
		req_tgts->ort_grp_size = 1;
		if (obj_is_ec(obj))
			req_tgts->ort_start_shard = (start_shard/obj_get_grp_size(obj)) *
						    daos_oclass_grp_size(obj_get_oca(obj));

		rc = obj_shard_tgts_query(obj, map_ver, start_shard, req_tgts->ort_shard_tgts,
					  obj_auxi, NIL_BITMAP);
		return rc;
	}

	req_tgts->ort_grp_nr = grp_nr;
	req_tgts->ort_grp_size = grp_size;
	shard_idx = start_shard;
	for (i = 0; i < grp_nr; i++) {
		struct daos_shard_tgt	*head;
		int			leader_shard = 0;
		uint32_t		grp_start;
		uint32_t		grp_idx;
		uint32_t		cur_grp_size;
		int			tgt_idx;

		cur_grp_size = req_tgts->ort_grp_size;
		head = tgt = req_tgts->ort_shard_tgts + i * grp_size;
		grp_idx = shard_idx / obj_get_grp_size(obj);
		grp_start = grp_idx * obj_get_grp_size(obj);
		if (req_tgts->ort_srv_disp) {
			if (obj_auxi->opc == DAOS_OBJ_RPC_UPDATE &&
			    DAOS_FAIL_CHECK(DAOS_DTX_SPEC_LEADER)) {
				leader_shard = 0;
			} else {
				leader_shard = obj_grp_leader_get(obj, grp_idx, obj_auxi->dkey_hash,
								  obj_auxi->cond_modify,
								  map_ver, bit_map);
			}
			if (leader_shard < 0) {
				D_ERROR(DF_OID" no valid shard %u, grp size %u "
					"grp nr %u, shards %u, reps %u: "DF_RC"\n",
					DP_OID(obj->cob_md.omd_id),
					shard_idx, obj->cob_grp_size,
					obj->cob_grp_nr, obj->cob_shards_nr,
					obj_get_replicas(obj), DP_RC(leader_shard));
				D_GOTO(out, rc = leader_shard);
			}
			rc = obj_shard_tgts_query(obj, map_ver, leader_shard,
						  tgt, obj_auxi, NIL_BITMAP);
			if (rc < 0)
				D_GOTO(out, rc);

			D_ASSERT(rc == 0);
			tgt++;
			cur_grp_size--;
			/* FIXME: check extending shards */
			if (flags & OBJ_TGT_FLAG_LEADER_ONLY) {
				shard_idx = grp_start + obj_get_grp_size(obj);
				continue;
			}
		}

		tgt_idx = shard_idx % obj_get_grp_size(obj);
		D_DEBUG(DB_IO, DF_OID" tgt_idx %d shard_idx %u cur_grp_size %u\n",
			DP_OID(obj->cob_md.omd_id), tgt_idx, shard_idx, cur_grp_size);
		while (cur_grp_size > 0) {
			shard_idx = grp_start + tgt_idx;

			if (req_tgts->ort_srv_disp && shard_idx == leader_shard) {
				tgt_idx = (tgt_idx + 1) % obj_get_grp_size(obj);
				continue;
			}

			rc = obj_shard_tgts_query(obj, map_ver, shard_idx, tgt, obj_auxi, bit_map);
			if (rc < 0) {
				/* NB: -DER_NONEXIST means the shard does not exist, for example
				 * degraded shard or extending shard, since fetch, update and
				 * list_shards_get already check if the shards are enough for
				 * the operation, so let's skip such shard here.  Note: these
				 * non-exist shards will never happen for the leader.
				 */
				D_CDEBUG(rc == -DER_NONEXIST, DB_IO, DLOG_ERR,
					 DF_OID", shard open:" DF_RC"\n",
					 DP_OID(obj->cob_md.omd_id), DP_RC(rc));
				if (rc != -DER_NONEXIST)
					D_GOTO(out, rc);
				rc = 0;
				if (obj_is_modification_opc(obj_auxi->opc))
					tgt_idx = (tgt_idx + 1) % obj_get_grp_size(obj);
				else
					tgt_idx = (tgt_idx + 1) %
						  daos_oclass_grp_size(&obj->cob_oca);
				continue;
			}

			if (req_tgts->ort_srv_disp) {
				struct daos_shard_tgt	*tmp, *last;

				for (tmp = head, last = tgt; tmp != last; tmp++) {
					/* Two shards locate on the same target,
					 * OSA case, will handle it via internal
					 * transaction.
					 */
					if (tmp->st_rank == DAOS_TGT_IGNORE ||
					    tmp->st_tgt_id != last->st_tgt_id)
						continue;

					D_DEBUG(DB_IO, "Modify obj "DF_OID
						" shard %u and shard %d on the"
						" same DAOS target %u/%u, will"
						" handle via CPD RPC.\n",
						DP_OID(obj->cob_md.omd_id),
						tmp->st_shard, last->st_shard,
						tmp->st_rank, tmp->st_tgt_id);
					D_GOTO(out, rc = -DER_NEED_TX);
				}
			}
			if (obj_is_modification_opc(obj_auxi->opc))
				tgt_idx = (tgt_idx + 1) % obj_get_grp_size(obj);
			else
				tgt_idx = (tgt_idx + 1) % daos_oclass_grp_size(&obj->cob_oca);
			cur_grp_size--;
			tgt++;
		}
		shard_idx = grp_start + obj_get_grp_size(obj);
	}

	if (flags & OBJ_TGT_FLAG_FW_LEADER_INFO)
		obj_auxi->flags |= ORF_CONTAIN_LEADER;

	if ((flags == 0 || flags & OBJ_TGT_FLAG_FW_LEADER_INFO) && bit_map == NIL_BITMAP)
		D_ASSERT(tgt == req_tgts->ort_shard_tgts + shard_cnt);

out:
	D_CDEBUG(rc == 0 || rc == -DER_NEED_TX || rc == -DER_TGT_RETRY, DB_TRACE,
		 DLOG_ERR, DF_OID", forward:" DF_RC"\n", DP_OID(obj->cob_md.omd_id), DP_RC(rc));
	return rc;
}

static void
obj_ptr2shards(struct dc_object *obj, uint32_t *start_shard, uint32_t *shard_nr,
	       uint32_t *grp_nr)
{
	*start_shard = 0;
	*shard_nr = obj->cob_shards_nr;
	*grp_nr = obj->cob_shards_nr / obj_get_grp_size(obj);

	D_ASSERTF(*grp_nr == obj->cob_grp_nr, "Unmatched grp nr for "
		  DF_OID": %u/%u\n",
		  DP_OID(obj->cob_md.omd_id), *grp_nr, obj->cob_grp_nr);
}

/* Get pool map version from object handle */
static int
obj_ptr2pm_ver(struct dc_object *obj, unsigned int *map_ver)
{
	*map_ver = obj->cob_version;
	return 0;
}

struct obj_pool_query_arg {
	struct dc_pool		*oqa_pool;
	struct dc_object	*oqa_obj;
};

static int
obj_pool_query_cb(tse_task_t *task, void *data)
{
	struct obj_pool_query_arg *arg = data;

	if (task->dt_result != 0) {
		D_DEBUG(DB_IO, "obj_pool_query_cb task=%p result=%d\n",
			task, task->dt_result);
	} else {
		if (arg->oqa_obj->cob_version <
		    dc_pool_get_version(arg->oqa_pool))
			obj_layout_refresh(arg->oqa_obj);
	}

	obj_decref(arg->oqa_obj);
	return 0;
}

int
obj_pool_query_task(tse_sched_t *sched, struct dc_object *obj,
		    unsigned int map_ver, tse_task_t **taskp)
{
	tse_task_t		       *task;
	struct dc_pool		       *pool;
	struct obj_pool_query_arg	arg;
	int				rc = 0;
	daos_handle_t			ph;

	pool = obj->cob_pool;
	D_ASSERT(pool != NULL);

	dc_pool2hdl_noref(pool, &ph);
	rc = dc_pool_create_map_refresh_task(ph, map_ver, sched, &task);
	if (rc != 0)
		return rc;

	arg.oqa_pool = pool;
	pool = NULL;
	arg.oqa_obj = obj_addref(obj);

	rc = tse_task_register_comp_cb(task, obj_pool_query_cb, &arg,
				       sizeof(arg));
	if (rc != 0) {
		obj_decref(arg.oqa_obj);
		dc_pool_abandon_map_refresh_task(task);
		return rc;
	}

	*taskp = task;
	return 0;
}

int
dc_obj_register_class(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_query_class(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_list_class(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

static int
dc_obj_redun_check(struct dc_object *obj, daos_handle_t coh)
{
	struct daos_oclass_attr	*oca = obj_get_oca(obj);
	int			 obj_tf;	/* obj #tolerate failures */
	uint32_t		 cont_rf;	/* cont redun_fac */
	int			 cont_tf;	/* cont #tolerate failures */
	int			 rc;

	cont_rf = obj->cob_co->dc_props.dcp_redun_fac;
	if (obj_is_ec(obj)) {
		obj_tf = obj_ec_parity_tgt_nr(oca);
	} else {
		D_ASSERT(oca->ca_resil == DAOS_RES_REPL);
		obj_tf = (oca->u.rp.r_num == DAOS_OBJ_REPL_MAX) ?
			 obj->cob_grp_size : oca->u.rp.r_num;
		D_ASSERT(obj_tf >= 1);
		obj_tf -= 1;
	}

	cont_tf = daos_cont_rf2allowedfailures(cont_rf);
	D_ASSERT(cont_tf >= 0);
	if (obj_tf < cont_tf) {
		rc = -DER_INVAL;
		D_ERROR(DF_OID" obj:cont tolerate failures %d:%d, "DF_RC"\n",
			DP_OID(obj->cob_md.omd_id), obj_tf, cont_tf,
			DP_RC(rc));
		return rc;
	}

	return 0;
}

int
dc_obj_open(tse_task_t *task)
{
	daos_obj_open_t		*args;
	struct dc_object	*obj;
	int			 rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_alloc();
	if (obj == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	obj->cob_co = dc_hdl2cont(args->coh);
	if (obj->cob_co == NULL)
		D_GOTO(fail, rc = -DER_NO_HDL);

	obj->cob_pool = dc_hdl2pool(obj->cob_co->dc_pool_hdl);
	if (obj->cob_pool == NULL)
		D_GOTO(fail_put_cont, rc = -DER_NO_HDL);

	obj->cob_mode = args->mode;

	rc = D_SPIN_INIT(&obj->cob_spin, PTHREAD_PROCESS_PRIVATE);
	if (rc != 0)
		D_GOTO(fail_put_pool, rc);

	rc = D_RWLOCK_INIT(&obj->cob_lock, NULL);
	if (rc != 0)
		D_GOTO(fail_spin_created, rc);

	/* it is a local operation for now, does not require event */
	rc = dc_obj_fetch_md(args->oid, &obj->cob_md);
	if (rc != 0)
		D_GOTO(fail_rwlock_created, rc);

	D_ASSERT(obj->cob_co->dc_props.dcp_obj_version < MAX_OBJ_LAYOUT_VERSION);
	obj->cob_layout_version = obj->cob_co->dc_props.dcp_obj_version;
	rc = obj_init_oca(obj);
	if (rc != 0)
		D_GOTO(fail_rwlock_created, rc);

	rc = obj_layout_create(obj, obj->cob_mode, false);
	if (rc != 0)
		D_GOTO(fail_rwlock_created, rc);

	rc = dc_obj_redun_check(obj, args->coh);
	if (rc != 0)
		D_GOTO(fail_layout_created, rc);

	rc = obj_ptr2pm_ver(obj, &obj->cob_md.omd_ver);
	if (rc)
		D_GOTO(fail_layout_created, rc);

	obj_hdl_link(obj);
	*args->oh = obj_ptr2hdl(obj);
	obj_decref(obj);
out:
	tse_task_complete(task, rc);
	return rc;

fail_layout_created:
	obj_layout_free(obj);
fail_rwlock_created:
	D_RWLOCK_DESTROY(&obj->cob_lock);
fail_spin_created:
	D_SPIN_DESTROY(&obj->cob_spin);
fail_put_pool:
	dc_pool_put(obj->cob_pool);
fail_put_cont:
	dc_cont_put(obj->cob_co);
fail:
	D_FREE(obj);
	tse_task_complete(task, rc);
	return rc;
}

int
dc_obj_close_direct(daos_handle_t oh)
{
	struct dc_object        *obj;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;
	obj_hdl_unlink(obj);
	obj_decref(obj);
	return 0;
}

int
dc_obj_close(tse_task_t *task)
{
	daos_obj_close_t	*args;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");
	rc = dc_obj_close_direct(args->oh);
	tse_task_complete(task, rc);
	return 0;
}

int
dc_obj_fetch_md(daos_obj_id_t oid, struct daos_obj_md *md)
{
	/* For predefined object classes, do nothing at here. But for those
	 * customized classes, we need to fetch for the remote OI table.
	 */
	md->omd_id	= oid;
	md->omd_ver	= 0;
	md->omd_pda	= 0;
	return 0;
}

int
daos_obj_layout_free(struct daos_obj_layout *layout)
{
	int i;

	for (i = 0; i < layout->ol_nr; i++) {
		if (layout->ol_shards[i] != NULL) {
			struct daos_obj_shard *shard;

			shard = layout->ol_shards[i];
			D_FREE(shard);
		}
	}

	D_FREE(layout);

	return 0;
}

int
daos_obj_layout_alloc(struct daos_obj_layout **layout, uint32_t grp_nr,
		      uint32_t grp_size)
{
	int rc = 0;
	int i;

	D_ALLOC(*layout, sizeof(struct daos_obj_layout) +
			 grp_nr * sizeof(struct daos_obj_shard *));
	if (*layout == NULL)
		return -DER_NOMEM;

	(*layout)->ol_nr = grp_nr;
	for (i = 0; i < grp_nr; i++) {
		D_ALLOC((*layout)->ol_shards[i],
			sizeof(struct daos_obj_shard) +
			grp_size * sizeof(struct daos_shard_loc));
		if ((*layout)->ol_shards[i] == NULL)
			D_GOTO(free, rc = -DER_NOMEM);

		(*layout)->ol_shards[i]->os_replica_nr = grp_size;
	}
free:
	if (rc != 0) {
		daos_obj_layout_free(*layout);
		*layout = NULL;
	}

	return rc;
}

int
dc_obj_layout_get(daos_handle_t oh, struct daos_obj_layout **p_layout)
{
	struct daos_obj_layout  *layout = NULL;
	struct dc_object	*obj;
	struct daos_oclass_attr *oc_attr;
	unsigned int		grp_size;
	unsigned int		grp_nr;
	int			rc;
	int			i;
	int			j;
	int			k;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	oc_attr = obj_get_oca(obj);
	grp_size = daos_oclass_grp_size(oc_attr);
	grp_nr = daos_oclass_grp_nr(oc_attr, &obj->cob_md);
	if (grp_nr == DAOS_OBJ_GRP_MAX)
		grp_nr = obj->cob_shards_nr / grp_size;

	if (grp_size == DAOS_OBJ_GRP_MAX)
		grp_size = obj->cob_shards_nr;

	rc = daos_obj_layout_alloc(&layout, grp_nr, grp_size);
	if (rc)
		D_GOTO(out, rc);

	for (i = 0, k = 0; i < grp_nr; i++) {
		struct daos_obj_shard *shard;

		shard = layout->ol_shards[i];
		shard->os_replica_nr = grp_size;
		for (j = 0; j < grp_size; j++) {
			struct dc_obj_shard *obj_shard;
			struct pool_target *tgt;

			obj_shard = &obj->cob_shards->do_shards[k++];
			if (obj_shard->do_target_id == -1)
				continue;

			rc = dc_pool_tgt_idx2ptr(obj->cob_pool,
						 obj_shard->do_target_id, &tgt);
			if (rc != 0)
				D_GOTO(out, rc);

			shard->os_shard_loc[j].sd_rank = tgt->ta_comp.co_rank;
			shard->os_shard_loc[j].sd_tgt_idx =
							tgt->ta_comp.co_index;
		}
	}
	*p_layout = layout;
out:
	obj_decref(obj);
	if (rc && layout != NULL)
		daos_obj_layout_free(layout);
	return rc;
}

int
dc_obj_query(tse_task_t *task)
{
	D_ERROR("Unsupported API\n");
	tse_task_complete(task, -DER_NOSYS);
	return 0;
}

int
dc_obj_layout_refresh(daos_handle_t oh)
{
	struct dc_object	*obj;
	int			 rc;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL) {
		D_ERROR("failed by obj_hdl2ptr.\n");
		return -DER_NO_HDL;
	}

	rc = obj_layout_refresh(obj);

	obj_decref(obj);

	return rc;
}

uint32_t
dc_obj_retry_delay(tse_task_t *task, int err, uint16_t *retry_cnt, uint16_t *inprogress_cnt)
{
	uint32_t	delay = 0;

	/*
	 * Randomly delay 5 - 68 us if it is not the first retry for
	 * -DER_INPROGRESS || -DER_UPDATE_AGAIN cases.
	 */
	++(*retry_cnt);
	if (err == -DER_INPROGRESS || err == -DER_UPDATE_AGAIN) {
		if (++(*inprogress_cnt) > 1) {
			delay = (d_rand() & ((1 << 6) - 1)) + 5;
			D_DEBUG(DB_IO, "Try to re-sched task %p for %d/%d times with %u us delay\n",
				task, (int)*inprogress_cnt, (int)*retry_cnt, delay);
		}
	}

	return delay;
}

static int
obj_retry_cb(tse_task_t *task, struct dc_object *obj,
	     struct obj_auxi_args *obj_auxi, bool pmap_stale,
	     bool *io_task_reinited)
{
	tse_sched_t	 *sched = tse_task2sched(task);
	tse_task_t	 *pool_task = NULL;
	uint32_t	  delay;
	int		  result = task->dt_result;
	int		  rc;

	if (pmap_stale) {
		rc = obj_pool_query_task(sched, obj, 0, &pool_task);
		if (rc != 0)
			D_GOTO(err, rc);
	}

	if (obj_auxi->io_retry) {
		if (pool_task != NULL) {
			rc = dc_task_depend(task, 1, &pool_task);
			if (rc != 0) {
				D_ERROR("Failed to add dependency on pool "
					 "query task (%p)\n", pool_task);
				D_GOTO(err, rc);
			}
		}

		delay = dc_obj_retry_delay(task, result, &obj_auxi->retry_cnt,
					   &obj_auxi->inprogress_cnt);
		rc = tse_task_reinit_with_delay(task, delay);
		if (rc != 0)
			D_GOTO(err, rc);

		*io_task_reinited = true;
	}

	if (pool_task != NULL)
		/* ignore returned value, error is reported by comp_cb */
		tse_task_schedule(pool_task, obj_auxi->io_retry);

	D_DEBUG(DB_IO, "Retrying task=%p/%d for err=%d, io_retry=%d\n",
		task, task->dt_result, result, obj_auxi->io_retry);

	return 0;
err:
	if (pool_task)
		dc_pool_abandon_map_refresh_task(pool_task);

	task->dt_result = result; /* restore the original error */
	obj_auxi->io_retry = 0;
	D_ERROR("Failed to retry task=%p(err=%d), io_retry=%d, rc "DF_RC"\n",
		task, result, obj_auxi->io_retry, DP_RC(rc));
	return rc;
}

static void
obj_task_complete(tse_task_t *task, int rc)
{
	/* in tse_task_complete only over-write task->dt_result if it is zero, but for some
	 * cases need to overwrite task->dt_result's retry-able result if get new different
	 * failure to avoid possible dead loop of retry or assertion.
	 */
	if (rc != 0 && task->dt_result != 0 &&
	    (obj_retry_error(task->dt_result) || task->dt_result == -DER_FETCH_AGAIN ||
	     task->dt_result == -DER_TGT_RETRY))
		task->dt_result = rc;

	tse_task_complete(task, rc);
}

static int
recov_task_abort(tse_task_t *task, void *arg)
{
	int	rc = *((int *)arg);

	obj_task_complete(task, rc);
	return 0;
}

static int
recov_task_cb(tse_task_t *task, void *data)
{
	struct obj_ec_recov_task	*recov_task = *((struct obj_ec_recov_task **)data);

	if (task->dt_result != -DER_FETCH_AGAIN)
		return 0;

	/* For the case of EC singv overwritten, in degraded fetch data recovery possibly always
	 * hit conflict case and need fetch again. Should update iod_size to avoid endless retry.
	 */
	recov_task->ert_uiod->iod_size = recov_task->ert_iod.iod_size;
	D_DEBUG(DB_IO, "update iod_size as "DF_U64"\n", recov_task->ert_oiod->iod_size);

	return 0;
}

static inline bool
obj_shard_is_invalid(struct dc_object *obj, uint32_t shard_idx, uint32_t opc)
{
	bool invalid_shard;

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj_is_modification_opc(opc))
		invalid_shard = obj->cob_shards->do_shards[shard_idx].do_target_id == -1 ||
				obj->cob_shards->do_shards[shard_idx].do_shard == -1;
	else
		invalid_shard = obj->cob_shards->do_shards[shard_idx].do_rebuilding ||
				obj->cob_shards->do_shards[shard_idx].do_target_id == -1 ||
				obj->cob_shards->do_shards[shard_idx].do_shard == -1;
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return invalid_shard || (DAOS_FAIL_CHECK(DAOS_FAIL_SHARD_OPEN) &&
				 daos_shard_in_fail_value(shard_idx));
}

/**
 * Check if there are any EC parity shards still alive under the oh/dkey_hash.
 * 1: alive,  0: no alive  < 0: failure.
 * NB: @shard suppose to return real shard from oclass, since it needs to compare
 * with .id_shard to know whether it is right parity shard (see migrate_enum_unpack_cb()),
 * so it has to use daos_oclass_grp_size to get the @shard.
 */
int
obj_ec_parity_alive(daos_handle_t oh, uint64_t dkey_hash, uint32_t *shard)
{
	struct daos_oclass_attr *oca;
	struct dc_object	*obj;
	uint32_t		p_shard;
	int			grp_idx;
	int			i;
	int			rc = 0;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	grp_idx = obj_dkey2grpidx(obj, dkey_hash, obj->cob_version);
	if (grp_idx < 0)
		D_GOTO(out_put, rc = grp_idx);

	oca = obj_get_oca(obj);
	p_shard = obj_ec_parity_start(obj, dkey_hash);
	for (i = 0; i < obj_ec_parity_tgt_nr(oca); i++, p_shard++) {
		uint32_t shard_idx = p_shard % daos_oclass_grp_size(&obj->cob_oca) +
				     grp_idx * obj_get_grp_size(obj);
		D_DEBUG(DB_TRACE, "shard %u %d/%d/%d/%d/%d\n", shard_idx,
			obj->cob_shards->do_shards[shard_idx].do_rebuilding,
			obj->cob_shards->do_shards[shard_idx].do_reintegrating,
			obj->cob_shards->do_shards[shard_idx].do_target_id,
			obj->cob_shards->do_shards[shard_idx].do_shard,
			obj->cob_shards->do_shards[shard_idx].do_shard_idx);
		if (!obj_shard_is_invalid(obj, shard_idx, DAOS_OBJ_RPC_FETCH) &&
		    !obj->cob_shards->do_shards[shard_idx].do_reintegrating) {
			if (shard != NULL)
				*shard = p_shard % daos_oclass_grp_size(&obj->cob_oca) +
					 grp_idx * daos_oclass_grp_size(&obj->cob_oca);
			D_GOTO(out_put, rc = 1);
		}
	}

out_put:
	obj_decref(obj);
	return rc;
}

static int
obj_ec_recov_cb(tse_task_t *task, struct dc_object *obj,
		struct obj_auxi_args *obj_auxi, d_iov_t *csum_iov)
{
	struct obj_reasb_req		*reasb_req = &obj_auxi->reasb_req;
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	daos_obj_fetch_t		*args = dc_task_get_args(task);
	tse_sched_t			*sched = tse_task2sched(task);
	struct obj_ec_recov_task	*recov_task;
	tse_task_t			*sub_task = NULL;
	daos_handle_t			 coh;
	daos_handle_t			 th = DAOS_HDL_INVAL;
	d_list_t			 task_list;
	uint32_t			 extra_flags, i;
	int				 rc;

	D_INIT_LIST_HEAD(&task_list);
	rc = obj_ec_recov_prep(obj, &obj_auxi->reasb_req,
			       obj_auxi->dkey_hash, args->iods, args->nr);
	if (rc) {
		D_ERROR("task %p "DF_OID" obj_ec_recov_prep failed "DF_RC"\n",
			task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
		goto out;
	}

	D_ASSERT(fail_info->efi_recov_ntasks > 0 &&
		 fail_info->efi_recov_tasks != NULL);
	for (i = 0; i < fail_info->efi_recov_ntasks; i++) {
		recov_task = &fail_info->efi_recov_tasks[i];
		/* Set client hlc as recovery epoch only for the case that
		 * singv recovery without fetch from server ahead - when
		 * some targets un-available.
		 */
		if (recov_task->ert_epoch == DAOS_EPOCH_MAX)
			recov_task->ert_epoch = d_hlc_get();
		dc_cont2hdl_noref(obj->cob_co, &coh);
		rc = dc_tx_local_open(coh, recov_task->ert_epoch, 0, &th);
		if (rc) {
			D_ERROR("task %p "DF_OID" dc_tx_local_open failed "
				DF_RC"\n", task, DP_OID(obj->cob_md.omd_id),
				DP_RC(rc));
			goto out;
		}
		recov_task->ert_th = th;
		D_DEBUG(DB_REBUILD, DF_C_OID_DKEY" Fetching to recover epoch "DF_X64"\n",
			DP_C_OID_DKEY(obj->cob_md.omd_id, args->dkey), recov_task->ert_epoch);
		extra_flags = DIOF_EC_RECOV;
		if (recov_task->ert_snapshot)
			extra_flags |= DIOF_EC_RECOV_SNAP;
		if (obj_auxi->flags & ORF_FOR_MIGRATION)
			extra_flags |= DIOF_FOR_MIGRATION;
		rc = dc_obj_fetch_task_create(args->oh, th, 0, args->dkey, 1,
					      extra_flags,
					      &recov_task->ert_iod,
					      &recov_task->ert_sgl, NULL,
					      fail_info, csum_iov,
					      NULL, sched, &sub_task);
		if (rc) {
			D_ERROR("task %p "DF_OID" dc_obj_fetch_task_create failed "DF_RC"\n",
				task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
			goto out;
		}

		tse_task_list_add(sub_task, &task_list);

		rc = tse_task_register_comp_cb(sub_task, recov_task_cb, &recov_task,
					       sizeof(recov_task));
		if (rc) {
			D_ERROR("task %p "DF_OID" tse_task_register_comp_cb failed "DF_RC"\n",
				task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
			goto out;
		}

		rc = dc_task_depend(task, 1, &sub_task);
		if (rc) {
			D_ERROR("task %p "DF_OID" dc_task_depend failed "DF_RC"\n",
				task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
			goto out;
		}
	}

	rc = dc_task_resched(task);
	if (rc != 0) {
		D_ERROR("task %p "DF_OID" dc_task_resched failed "DF_RC"\n",
			task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
		goto out;
	}

out:
	if (rc == 0) {
		obj_auxi->ec_wait_recov = 1;
		D_DEBUG(DB_IO, "scheduling %d recovery tasks for IO task %p.\n",
			fail_info->efi_recov_ntasks, task);
		tse_task_list_sched(&task_list, false);
	} else {
		task->dt_result = rc;
		tse_task_list_traverse(&task_list, recov_task_abort, &rc);
		D_ERROR("task %p "DF_OID" EC recovery failed "DF_RC"\n",
			task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
	}
	return rc;
}

/* prepare the bulk handle(s) for obj request */
int
obj_bulk_prep(d_sg_list_t *sgls, unsigned int nr, bool bulk_bind,
	      crt_bulk_perm_t bulk_perm, tse_task_t *task,
	      crt_bulk_t **p_bulks)
{
	crt_bulk_t	*bulks;
	int		 i = 0;
	int		 rc = 0;

	D_ASSERTF(nr >= 1, "invalid nr %d.\n", nr);
	D_ALLOC_ARRAY(bulks, nr);
	if (bulks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* create bulk handles for sgls */
	for (; sgls != NULL && i < nr; i++) {
		if (sgls[i].sg_iovs != NULL &&
		    sgls[i].sg_iovs[0].iov_buf != NULL) {
			rc = crt_bulk_create(daos_task2ctx(task), &sgls[i],
					     bulk_perm, &bulks[i]);
			if (rc < 0)
				D_GOTO(out, rc);
			if (!bulk_bind)
				continue;
			rc = crt_bulk_bind(bulks[i], daos_task2ctx(task));
			if (rc != 0)
				D_GOTO(out, rc);
		}
	}

out:
	if (rc == 0) {
		*p_bulks = bulks;
	} else {
		int j;

		for (j = 0; j < i; j++)
			crt_bulk_free(bulks[j]);

		D_FREE(bulks);

		D_ERROR("%s failed "DF_RC"\n",
			bulk_perm == CRT_BULK_RO ? "update" : "fetch",
			DP_RC(rc));
	}
	return rc;
}

static void
obj_bulk_fini(struct obj_auxi_args *obj_auxi)
{
	crt_bulk_t	*bulks = obj_auxi->bulks;
	unsigned int	 nr = obj_auxi->iod_nr;
	int		 i;

	if (bulks == NULL)
		return;

	for (i = 0; i < nr; i++)
		if (bulks[i] != CRT_BULK_NULL)
			crt_bulk_free(bulks[i]);

	D_FREE(bulks);
	obj_auxi->bulks = NULL;
}

static int
obj_rw_bulk_prep(struct dc_object *obj, daos_iod_t *iods, d_sg_list_t *sgls,
		 unsigned int nr, bool update, bool bulk_bind,
		 tse_task_t *task, struct obj_auxi_args *obj_auxi)
{
	daos_size_t		sgls_size;
	crt_bulk_perm_t		bulk_perm;
	int			rc = 0;

	if ((obj_auxi->io_retry && !obj_auxi->reasb_req.orr_size_fetched &&
	     obj_auxi->bulks != NULL) || obj_auxi->reasb_req.orr_size_fetch || sgls == NULL)
		return 0;

	/* inline fetch needs to pack sgls buffer into RPC so uses it to check
	 * if need bulk transferring.
	 */
	sgls_size = daos_sgls_packed_size(sgls, nr, NULL);
	if (sgls_size >= DAOS_BULK_LIMIT ||
	    (obj_is_ec(obj) && !obj_auxi->reasb_req.orr_single_tgt)) {
		bulk_perm = update ? CRT_BULK_RO : CRT_BULK_RW;
		rc = obj_bulk_prep(sgls, nr, bulk_bind, bulk_perm, task,
				   &obj_auxi->bulks);
	}
	obj_auxi->reasb_req.orr_size_fetched = 0;

	return rc;
}

static bool
obj_recx_valid(unsigned int nr, daos_recx_t *recxs, bool update)
{
	struct umem_attr	uma;
	daos_handle_t		bth;
	d_iov_t			key;
	int			idx;
	bool			overlapped;
	struct btr_root		broot = { 0 };
	int			rc;

	if (nr == 0 || recxs == NULL)
		return false;
	if (nr == 1) {
		if (recxs[0].rx_nr == 0)
			return false;
		return true;
	}

	switch (nr) {
	case 2:
		overlapped = DAOS_RECX_PTR_OVERLAP(&recxs[0], &recxs[1]);
		break;

	case 3:
		overlapped = DAOS_RECX_PTR_OVERLAP(&recxs[0], &recxs[1]) ||
			     DAOS_RECX_PTR_OVERLAP(&recxs[0], &recxs[2]) ||
			     DAOS_RECX_PTR_OVERLAP(&recxs[1], &recxs[2]);
		break;

	default:
		/* using a btree to detect overlap when nr >= 4 */
		memset(&uma, 0, sizeof(uma));
		uma.uma_id = UMEM_CLASS_VMEM;
		rc = dbtree_create_inplace(DBTREE_CLASS_RECX,
					   BTR_FEAT_DIRECT_KEY, 8,
					   &uma, &broot, &bth);
		if (rc != 0) {
			D_ERROR("failed to create recx tree: "DF_RC"\n",
				DP_RC(rc));
			return false;
		}

		overlapped = false;
		for (idx = 0; idx < nr; idx++) {
			if (recxs[idx].rx_nr == 0) {
				overlapped = true;
				break;
			}
			d_iov_set(&key, &recxs[idx], sizeof(daos_recx_t));
			rc = dbtree_update(bth, &key, NULL);
			if (rc != 0) {
				overlapped = true;
				break;
			}
		}
		dbtree_destroy(bth, NULL);
		break;
	};

	return !overlapped;
}

static int
obj_req_size_valid(daos_size_t iod_size, daos_size_t sgl_size)
{
	if (iod_size > sgl_size) {
		D_ERROR("invalid req - iod size "DF_U64", sgl size "DF_U64"\n",
			iod_size, sgl_size);
		return -DER_REC2BIG;
	}
	return 0;
}

static int
obj_iod_sgl_valid(daos_obj_id_t oid, unsigned int nr, daos_iod_t *iods,
		  d_sg_list_t *sgls, bool update, bool size_fetch,
		  bool spec_shard, bool check_exist)
{
	int i, j;
	int rc;

	if (iods == NULL) {
		if (nr == 0)
			return 0;

		return -DER_INVAL;
	}

	for (i = 0; i < nr; i++) {
		if (iods[i].iod_name.iov_buf == NULL) {
			D_ERROR("Invalid argument of NULL akey\n");
			return -DER_INVAL;
		}
		if (daos_is_akey_uint64(oid) &&
		    iods[i].iod_name.iov_len != sizeof(uint64_t)) {
			D_ERROR("Invalid akey len, expected: %lu, got: "DF_U64"\n",
				sizeof(uint64_t), iods[i].iod_name.iov_len);
			return -DER_INVAL;
		}
		for (j = 0; j < iods[i].iod_nr; j++) {
			if (iods[i].iod_recxs != NULL && (!spec_shard &&
			   (iods[i].iod_recxs[j].rx_idx & PARITY_INDICATOR)
			    != 0)) {
				D_ERROR("Invalid IOD, the bit-63 of rx_idx is "
					"reserved.\n");
				return -DER_INVAL;
			}
		}

		switch (iods[i].iod_type) {
		default:
			D_ERROR("Unknown iod type=%d\n", iods[i].iod_type);
			return -DER_INVAL;

		case DAOS_IOD_NONE:
			if (!iods[i].iod_recxs && iods[i].iod_nr == 0)
				continue;

			D_ERROR("IOD_NONE ignores value iod_nr=%d, recx=%p\n",
				 iods[i].iod_nr, iods[i].iod_recxs);
			return -DER_INVAL;

		case DAOS_IOD_ARRAY:
			if (sgls == NULL) {
				/* size query or punch */
				if ((iods[i].iod_size == DAOS_REC_ANY) ||
				    (!update && check_exist))
					continue;
				D_ERROR("invalid req with NULL sgl\n");
				return -DER_INVAL;
			}
			if (!size_fetch &&
			    !obj_recx_valid(iods[i].iod_nr, iods[i].iod_recxs,
					    update)) {
				D_ERROR("Invalid recxs update %s\n", update ? "yes" : "no");
				for (j = 0; j < iods[i].iod_nr; j++)
					D_ERROR("%d: "DF_RECX"\n", j,
						DP_RECX(iods[i].iod_recxs[j]));

				return -DER_INVAL;
			}
			if (iods[i].iod_size == DAOS_REC_ANY)
				continue;
			rc = obj_req_size_valid(daos_iods_len(&iods[i], 1),
						daos_sgl_buf_size(&sgls[i]));
			if (rc)
				return rc;
			break;

		case DAOS_IOD_SINGLE:
			if (iods[i].iod_nr != 1) {
				D_ERROR("IOD_SINGLE iod_nr %d != 1\n",
					iods[i].iod_nr);
				return -DER_INVAL;
			}
			if (sgls == NULL) {
				/* size query or punch */
				if (!update || iods[i].iod_size == DAOS_REC_ANY)
					continue;
				D_ERROR("invalid update req with NULL sgl\n");
				return -DER_INVAL;
			}
			if (iods[i].iod_size == DAOS_REC_ANY)
				continue;
			rc = obj_req_size_valid(iods[i].iod_size,
						daos_sgl_buf_size(&sgls[i]));
			if (rc)
				return rc;
			break;
		}
	}

	return 0;
}

static int
check_query_flags(daos_obj_id_t oid, uint32_t flags, daos_key_t *dkey,
		  daos_key_t *akey, daos_recx_t *recx)
{
	/** just query max epoch */
	if (flags == 0)
		return 0;

	if (!(flags & (DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX))) {
		D_ERROR("Key type or recx not specified in flags.\n");
		return -DER_INVAL;
	}

	if (!(flags & (DAOS_GET_MIN | DAOS_GET_MAX))) {
		D_ERROR("Query type not specified in flags.\n");
		return -DER_INVAL;
	}

	if ((flags & DAOS_GET_MIN) && (flags & DAOS_GET_MAX)) {
		D_ERROR("Invalid Query.\n");
		return -DER_INVAL;
	}

	if (dkey == NULL) {
		D_ERROR("dkey can't be NULL.\n");
		return -DER_INVAL;
	}

	if (akey == NULL && (flags & (DAOS_GET_AKEY | DAOS_GET_RECX))) {
		D_ERROR("akey can't be NULL with query type.\n");
		return -DER_INVAL;
	}

	if (recx == NULL && flags & DAOS_GET_RECX) {
		D_ERROR("recx can't be NULL with query type.\n");
		return -DER_INVAL;
	}

	if (flags & DAOS_GET_DKEY) {
		if (!daos_is_dkey_uint64(oid)) {
			D_ERROR("Can't query non UINT64 typed Dkeys.\n");
			return -DER_INVAL;
		}
		if (dkey->iov_buf_len < sizeof(uint64_t) ||
		    dkey->iov_buf == NULL) {
			D_ERROR("Invalid Dkey iov.\n");
			return -DER_INVAL;
		}
	}

	if (flags & DAOS_GET_AKEY) {
		if (!(daos_is_akey_uint64(oid))) {
			D_ERROR("Can't query non UINT64 typed Akeys.\n");
			return -DER_INVAL;
		}
		if (akey->iov_buf_len < sizeof(uint64_t) ||
		    akey->iov_buf == NULL) {
			D_ERROR("Invalid Akey iov.\n");
			return -DER_INVAL;
		}
	}

	return 0;
}

static inline bool
obj_key_valid(daos_obj_id_t oid, daos_key_t *key, bool check_dkey)
{
	if (check_dkey) {
		if (daos_is_dkey_uint64(oid) &&
		    key->iov_len != sizeof(uint64_t)) {
			D_ERROR("Invalid dkey len, expected: %lu, got: "DF_U64"\n",
				sizeof(uint64_t), key->iov_len);
			return false;
		}
	} else {
		if (daos_is_akey_uint64(oid) &&
		    key->iov_len != sizeof(uint64_t)) {
			D_ERROR("Invalid akey len, expected: %lu, got: "DF_U64"\n",
				sizeof(uint64_t), key->iov_len);
			return false;
		}
	}

	return key != NULL && key->iov_buf != NULL && key->iov_len != 0;
}

static bool
obj_req_with_cond_flags(uint64_t flags)
{
	return flags & DAOS_COND_MASK;
}

static bool
obj_req_is_ec_cond_fetch(struct obj_auxi_args *obj_auxi)
{
	daos_obj_rw_t	*api_args = dc_task_get_args(obj_auxi->obj_task);

	return obj_auxi->is_ec_obj && obj_is_fetch_opc(obj_auxi->opc) &&
	       obj_req_with_cond_flags(api_args->flags);
}

static bool
obj_req_is_ec_check_exist(struct obj_auxi_args *obj_auxi)
{
	daos_obj_rw_t	*api_args = dc_task_get_args(obj_auxi->obj_task);

	return obj_auxi->is_ec_obj &&
	       (api_args->extra_flags & DIOF_CHECK_EXISTENCE);
}

static bool
obj_ec_req_sent2_all_data_tgts(struct obj_auxi_args *obj_auxi)
{
	struct dc_object	*obj = obj_auxi->obj;
	struct obj_reasb_req	*reasb_req = &obj_auxi->reasb_req;
	struct daos_oclass_attr	*oca;
	uint32_t		 shard, i;

	D_ASSERT(obj_auxi->req_reasbed && reasb_req->tgt_bitmap != NULL);
	oca = obj_get_oca(obj);
	shard = obj_ec_shard_idx(obj, obj_auxi->dkey_hash, 0);
	for (i = 0; i < obj_ec_data_tgt_nr(oca); i++) {
		if (isclr(reasb_req->tgt_bitmap, shard))
			return false;
		shard = (shard + 1) % obj_ec_tgt_nr(oca);
	}

	return true;
}

/* check if the obj request is valid */
static int
obj_req_valid(tse_task_t *task, void *args, int opc, struct dtx_epoch *epoch,
	      uint32_t *pm_ver, struct dc_object **p_obj)
{
	uint32_t		map_ver = *pm_ver;
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj = NULL;
	daos_handle_t		oh;
	daos_handle_t		th = DAOS_HDL_INVAL;
	int			rc = 0;

	obj_auxi = tse_task_stack_push(task, sizeof(*obj_auxi));

	switch (opc) {
	case DAOS_OBJ_RPC_FETCH: {
		daos_obj_fetch_t	*f_args = args;
		uint64_t		 flags = f_args->flags;
		bool			 size_fetch, spec_shard, check_exist;

		spec_shard  = f_args->extra_flags & DIOF_TO_SPEC_SHARD;
		check_exist = f_args->extra_flags & DIOF_CHECK_EXISTENCE;
		size_fetch  = obj_auxi->reasb_req.orr_size_fetch;

		obj = obj_hdl2ptr(f_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		if (obj_req_with_cond_flags(flags)) {
			if (flags & (DAOS_COND_PUNCH | DAOS_COND_DKEY_INSERT |
				     DAOS_COND_DKEY_UPDATE | DAOS_COND_AKEY_INSERT |
				     DAOS_COND_AKEY_UPDATE)) {
				D_ERROR("invalid fetch - with conditional modification flags "
					DF_X64"\n", flags);
				D_GOTO(out, rc = -DER_INVAL);
			}
			if ((flags & DAOS_COND_PER_AKEY) && (flags & (DAOS_COND_AKEY_FETCH))) {
				D_ERROR("cannot with both DAOS_COND_PER_AKEY and "
					"DAOS_COND_AKEY_FETCH\n");
				D_GOTO(out, rc = -DER_INVAL);
			}
		}

		if ((!obj_auxi->io_retry && !obj_auxi->req_reasbed) ||
		    size_fetch) {
			if (!obj_key_valid(obj->cob_md.omd_id, f_args->dkey,
					   true) ||
			    (f_args->nr == 0 && !check_exist)) {
				D_ERROR("Invalid fetch parameter.\n");
				D_GOTO(out, rc = -DER_INVAL);
			}

			rc = obj_iod_sgl_valid(obj->cob_md.omd_id, f_args->nr,
					       f_args->iods, f_args->sgls, false,
					       size_fetch, spec_shard, check_exist);
			if (rc)
				goto out;
		}
		oh = f_args->oh;
		th = f_args->th;
		break;
	}
	case DAOS_OBJ_RPC_UPDATE: {
		daos_obj_update_t	*u_args = args;
		uint64_t		 flags = u_args->flags;

		obj = obj_hdl2ptr(u_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		if (obj_req_with_cond_flags(flags)) {
			if (flags & (DAOS_COND_PUNCH | DAOS_COND_DKEY_FETCH |
				     DAOS_COND_AKEY_FETCH)) {
				D_ERROR("invalid update - with conditional punch/fetch flags "
					DF_X64"\n", flags);
				D_GOTO(out, rc = -DER_INVAL);
			}
			if ((flags & DAOS_COND_PER_AKEY) &&
			    (flags & (DAOS_COND_AKEY_UPDATE | DAOS_COND_AKEY_INSERT))) {
				D_ERROR("cannot with both DAOS_COND_PER_AKEY and "
					"DAOS_COND_AKEY_UPDATE | DAOS_COND_AKEY_INSERT\n");
				D_GOTO(out, rc = -DER_INVAL);
			}
		}

		if (!obj_auxi->io_retry && !obj_auxi->req_reasbed) {
			if (!obj_key_valid(obj->cob_md.omd_id, u_args->dkey,
					   true) || u_args->nr == 0) {
				D_ERROR("Invalid update parameter.\n");
				D_GOTO(out, rc = -DER_INVAL);
			}

			rc = obj_iod_sgl_valid(obj->cob_md.omd_id, u_args->nr,
					       u_args->iods, u_args->sgls, true,
					       false, false, false);
			if (rc)
				D_GOTO(out, rc);
		}

		if (daos_handle_is_valid(u_args->th))
			D_GOTO(out_tx, rc = 0);

		oh = u_args->oh;
		th = u_args->th;
		break;
	}
	case DAOS_OBJ_RPC_PUNCH: {
		daos_obj_punch_t *p_args = args;

		obj = obj_hdl2ptr(p_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		if (daos_handle_is_valid(p_args->th))
			D_GOTO(out_tx, rc = 0);

		oh = p_args->oh;
		th = p_args->th;
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_DKEYS: {
		daos_obj_punch_t *p_args = args;

		obj = obj_hdl2ptr(p_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		if (!obj_key_valid(obj->cob_md.omd_id, p_args->dkey, true)) {
			D_ERROR("invalid punch dkey parameter.\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		if (daos_handle_is_valid(p_args->th))
			D_GOTO(out_tx, rc = 0);

		oh = p_args->oh;
		th = p_args->th;
		break;
	}

	case DAOS_OBJ_RPC_PUNCH_AKEYS: {
		daos_obj_punch_t *p_args = args;
		int		  i;

		obj = obj_hdl2ptr(p_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		if (!obj_key_valid(obj->cob_md.omd_id, p_args->dkey, true) ||
		    p_args->akey_nr == 0) {
			D_ERROR("invalid punch akey parameter.\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		for (i = 0; i < p_args->akey_nr; i++) {
			if (!obj_key_valid(obj->cob_md.omd_id,
					   &p_args->akeys[i], false)) {
				D_ERROR("invalid punch akeys parameter.\n");
				D_GOTO(out, rc = -DER_INVAL);
			}
		}

		if (daos_handle_is_valid(p_args->th))
			D_GOTO(out_tx, rc = 0);

		oh = p_args->oh;
		th = p_args->th;
		break;
	}

	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE: {
		daos_obj_list_t	*l_args = args;

		if (!obj_auxi->io_retry) {
			if (l_args->dkey == NULL &&
			    (opc != DAOS_OBJ_DKEY_RPC_ENUMERATE &&
			     opc != DAOS_OBJ_RPC_ENUMERATE)) {
				D_ERROR("No dkey for opc %x\n", opc);
				D_GOTO(out, rc = -DER_INVAL);
			}

			if (l_args->nr == NULL || *l_args->nr == 0) {
				D_ERROR("Invalid API parameter.\n");
				D_GOTO(out, rc = -DER_INVAL);
			}

			if (opc == DAOS_OBJ_RPC_ENUMERATE &&
			    daos_handle_is_valid(l_args->th) &&
			    l_args->eprs != NULL) {
				D_ERROR("mutually exclusive th and eprs "
					"specified for listing objects\n");
				rc = -DER_INVAL;
				goto out;
			}
		}

		obj = obj_hdl2ptr(l_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		oh = l_args->oh;
		th = l_args->th;
		break;
	}

	case DAOS_OBJ_RPC_QUERY_KEY: {
		daos_obj_query_key_t	*q_args = args;

		obj = obj_hdl2ptr(q_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		rc = check_query_flags(obj->cob_md.omd_id, q_args->flags,
				       q_args->dkey, q_args->akey,
				       q_args->recx);
		if (rc)
			D_GOTO(out, rc);

		th = q_args->th;
		break;
	}
	case DAOS_OBJ_RPC_SYNC: {
		struct daos_obj_sync_args *s_args = args;

		oh = s_args->oh;
		break;
	}
	case DAOS_OBJ_RPC_KEY2ANCHOR: {
		daos_obj_key2anchor_t *k_args = args;

		obj = obj_hdl2ptr(k_args->oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);

		if (k_args->dkey == NULL ||
		    !obj_key_valid(obj->cob_md.omd_id, k_args->dkey, true)) {
			D_ERROR("invalid key2anchor dkey parameter.\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		if (k_args->akey &&
		    !obj_key_valid(obj->cob_md.omd_id, k_args->akey, false)) {
			D_ERROR("invalid key2anchor akey parameter.\n");
			D_GOTO(out, rc = -DER_INVAL);
		}
		break;
	}
	default:
		D_ERROR("bad opc %d.\n", opc);
		D_GOTO(out, rc = -DER_INVAL);
		break;
	};

	if (obj == NULL) {
		obj = obj_hdl2ptr(oh);
		if (obj == NULL)
			D_GOTO(out, rc = -DER_NO_HDL);
	}

	if (obj_is_modification_opc(opc)) {
		if (!(obj->cob_mode & DAOS_OBJ_UPDATE_MODE_MASK)) {
			D_ERROR("object "DF_OID" opc %d is opened with mode 0x%x\n",
				DP_OID(obj->cob_md.omd_id), opc, obj->cob_mode);
			D_GOTO(out, rc = -DER_NO_PERM);
		}
	}

	if (daos_handle_is_valid(th)) {
		if (!obj_is_modification_opc(opc)) {
			rc = dc_tx_hdl2epoch_and_pmv(th, epoch, &map_ver);
			if (rc != 0)
				D_GOTO(out, rc);
		}
	} else {
		dc_io_epoch_set(epoch, opc);
		D_DEBUG(DB_IO, "set fetch epoch "DF_U64"\n", epoch->oe_value);
	}

	if (map_ver == 0) {
		rc = obj_ptr2pm_ver(obj, &map_ver);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	*pm_ver = map_ver;

out_tx:
	D_ASSERT(rc == 0);

	if (p_obj != NULL)
		*p_obj = obj;
	else
		obj_decref(obj);

out:
	if (rc && obj)
		obj_decref(obj);
	tse_task_stack_pop(task, sizeof(*obj_auxi));
	return rc;
}

static void
shard_auxi_set_param(struct shard_auxi_args *shard_arg, uint32_t map_ver,
		     uint32_t shard, uint32_t tgt_id, struct dtx_epoch *epoch,
		     uint16_t ec_tgt_idx)
{
	shard_arg->epoch = *epoch;
	shard_arg->shard = shard;
	shard_arg->target = tgt_id;
	shard_arg->map_ver = map_ver;
	shard_arg->ec_tgt_idx = ec_tgt_idx;
}

struct shard_task_sched_args {
	struct dtx_epoch	tsa_epoch;
	bool			tsa_scheded;
	bool			tsa_prev_scheded; /* previously scheduled */
};

static int
shard_task_sched(tse_task_t *task, void *arg)
{
	tse_task_t			*obj_task;
	struct obj_auxi_args		*obj_auxi;
	struct shard_auxi_args		*shard_auxi;
	struct shard_task_sched_args	*sched_arg = arg;
	uint32_t			 target;
	unsigned int			 map_ver;
	int				 rc = 0;

	shard_auxi = tse_task_buf_embedded(task, sizeof(*shard_auxi));
	obj_auxi = shard_auxi->obj_auxi;
	map_ver = obj_auxi->map_ver_req;
	obj_task = obj_auxi->obj_task;
	if (sched_arg->tsa_prev_scheded && !obj_auxi->new_shard_tasks) {
		/* For retried IO, check if the shard's target changed after
		 * pool map query. If match then need not do anything, if
		 * mismatch then need to re-schedule the shard IO on the new
		 * pool map.
		 * Also retry the shard IO if it got retryable error last time.
		 */
		rc = obj_shard2tgtid(obj_auxi->obj, shard_auxi->shard,
				     map_ver, &target);
		if (rc != 0) {
			D_ERROR("shard %d, obj_shard2tgtid failed "DF_RC"\n",
				shard_auxi->shard, DP_RC(rc));
			goto out;
		}
		if (obj_auxi->req_tgts.ort_srv_disp ||
		    obj_retry_error(task->dt_result) ||
		    !dtx_epoch_equal(&sched_arg->tsa_epoch,
				     &shard_auxi->epoch) ||
		    target != shard_auxi->target) {
			D_DEBUG(DB_IO, "shard %d, dt_result %d, target %d @ "
				"map_ver %d, target %d @ last_map_ver %d, "
				"shard task %p to be re-scheduled.\n",
				shard_auxi->shard, task->dt_result, target,
				map_ver, shard_auxi->target,
				shard_auxi->map_ver, task);

			if (!obj_auxi->req_tgts.ort_srv_disp)
				shard_auxi_set_param(shard_auxi, map_ver,
					shard_auxi->shard, target,
					&sched_arg->tsa_epoch,
					(uint16_t)shard_auxi->ec_tgt_idx);

			rc = tse_task_register_deps(obj_task, 1, &task);
			if (rc != 0)
				goto out;

			rc = tse_task_reinit(task);
			if (rc != 0)
				goto out;

			sched_arg->tsa_scheded = true;
		}
	} else {
		obj_auxi->shards_scheded = 1;
		sched_arg->tsa_scheded = true;
		tse_task_schedule(task, true);
	}

out:
	if (rc != 0)
		obj_task_complete(task, rc);
	return rc;
}

static void
obj_shard_task_sched(struct obj_auxi_args *obj_auxi, struct dtx_epoch *epoch)
{
	struct shard_task_sched_args	sched_arg;

	D_ASSERT(!d_list_empty(&obj_auxi->shard_task_head));
	sched_arg.tsa_scheded = false;
	sched_arg.tsa_prev_scheded = obj_auxi->shards_scheded;
	sched_arg.tsa_epoch = *epoch;
	tse_task_list_traverse_adv(&obj_auxi->shard_task_head, shard_task_sched, &sched_arg);
	/* It is possible that the IO retried by stale pm version found, but
	 * the IO involved shards' targets not changed. No any shard task
	 * re-scheduled for this case, can complete the obj IO task.
	 */
	if (sched_arg.tsa_scheded == false)
		tse_task_complete(obj_auxi->obj_task, 0);
}

static struct shard_auxi_args *
obj_embedded_shard_arg(struct obj_auxi_args *obj_auxi)
{
	switch (obj_auxi->opc) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return &obj_auxi->rw_args.auxi;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		return &obj_auxi->p_args.pa_auxi;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		return &obj_auxi->l_args.la_auxi;
	case DAOS_OBJ_RPC_KEY2ANCHOR:
		return &obj_auxi->k_args.ka_auxi;
	case DAOS_OBJ_RPC_SYNC:
		return &obj_auxi->s_args.sa_auxi;
	case DAOS_OBJ_RPC_QUERY_KEY:
		/*
		 * called from obj_comp_cb_internal() and
		 * checked in obj_shard_comp_cb() correctly
		 */
		return NULL;
	default:
		D_ERROR("bad opc %d.\n", obj_auxi->opc);
		return NULL;
	};
}

static int
shard_io(tse_task_t *task, struct shard_auxi_args *shard_auxi)
{
	struct obj_auxi_args		*obj_auxi = shard_auxi->obj_auxi;
	struct dc_object		*obj = obj_auxi->obj;
	struct dc_obj_shard		*obj_shard;
	struct obj_req_tgts		*req_tgts;
	struct daos_shard_tgt		*fw_shard_tgts;
	uint32_t			 fw_cnt;
	int				 rc;

	D_ASSERT(obj != NULL);
	rc = obj_shard_open(obj, shard_auxi->shard, shard_auxi->map_ver,
			    &obj_shard);
	if (rc != 0) {
		D_ERROR(DF_OID" shard %u open: %d\n",
			DP_OID(obj->cob_md.omd_id), shard_auxi->shard, rc);
		obj_task_complete(task, rc);
		return rc;
	}

	rc = tse_task_register_comp_cb(task, close_shard_cb, &obj_shard, sizeof(obj_shard));
	if (rc != 0) {
		obj_shard_close(obj_shard);
		obj_task_complete(task, rc);
		return rc;
	}

	shard_auxi->flags = shard_auxi->obj_auxi->flags;
	req_tgts = &shard_auxi->obj_auxi->req_tgts;
	D_ASSERT(shard_auxi->grp_idx < req_tgts->ort_grp_nr);

	if (req_tgts->ort_srv_disp) {
		fw_shard_tgts = req_tgts->ort_shard_tgts +
				shard_auxi->grp_idx * req_tgts->ort_grp_size;
		fw_cnt = req_tgts->ort_grp_size;
		if (!(obj_auxi->flags & ORF_CONTAIN_LEADER)) {
			fw_shard_tgts++;
			fw_cnt--;
		}
	} else {
		fw_shard_tgts = NULL;
		fw_cnt = 0;
	}

	rc = shard_auxi->shard_io_cb(obj_shard, obj_auxi->opc, shard_auxi,
				     fw_shard_tgts, fw_cnt, task);
	return rc;
}

static int
shard_io_task(tse_task_t *task)
{
	struct shard_auxi_args	*shard_auxi;
	daos_handle_t		 th;
	int			 rc;

	shard_auxi = tse_task_buf_embedded(task, sizeof(*shard_auxi));

	/*
	 * If this task belongs to a TX, and if the epoch we got earlier
	 * doesn't contain a "chosen" TX epoch, then we may need to reinit the
	 * task via dc_tx_get_epoch. Because tse_task_reinit is less practical
	 * in the middle of a task, we do it here at the beginning of
	 * shard_io_task.
	 */
	th = shard_auxi->obj_auxi->th;
	if (daos_handle_is_valid(th) && !dtx_epoch_chosen(&shard_auxi->epoch)) {
		rc = dc_tx_get_epoch(task, th, &shard_auxi->epoch);
		if (rc < 0) {
			obj_task_complete(task, rc);
			return rc;
		} else if (rc == DC_TX_GE_REINITED) {
			return 0;
		}
	}

	return shard_io(task, shard_auxi);
}

typedef int (*shard_io_prep_cb_t)(struct shard_auxi_args *shard_auxi,
				  struct dc_object *obj,
				  struct obj_auxi_args *obj_auxi,
				  uint32_t grp_idx);

struct shard_task_reset_arg {
	struct obj_req_tgts	*req_tgts;
	struct dtx_epoch	epoch;
	int			index;
};

static int
shard_task_reset_param(tse_task_t *shard_task, void *arg)
{
	struct shard_task_reset_arg	*reset_arg = arg;
	struct obj_req_tgts		*req_tgts = reset_arg->req_tgts;
	struct obj_auxi_args		*obj_auxi;
	struct shard_auxi_args		*shard_arg;
	struct daos_shard_tgt		*tgt;

	shard_arg = tse_task_buf_embedded(shard_task, sizeof(*shard_arg));
	D_ASSERT(shard_arg->grp_idx < req_tgts->ort_grp_nr);
	obj_auxi = container_of(req_tgts, struct obj_auxi_args, req_tgts);
	if (req_tgts->ort_srv_disp) {
		tgt = req_tgts->ort_shard_tgts +
			     shard_arg->grp_idx * req_tgts->ort_grp_size;
	} else {
		tgt = req_tgts->ort_shard_tgts + reset_arg->index;
		reset_arg->index++;
	}
	shard_arg->start_shard = req_tgts->ort_start_shard;
	shard_auxi_set_param(shard_arg, obj_auxi->map_ver_req,
			     tgt->st_shard, tgt->st_tgt_id,
			     &reset_arg->epoch, (uint16_t)tgt->st_ec_tgt);
	return 0;
}

static int
obj_req_fanout(struct dc_object *obj, struct obj_auxi_args *obj_auxi,
	       uint32_t map_ver, struct dtx_epoch *epoch,
	       shard_io_prep_cb_t io_prep_cb, shard_io_cb_t io_cb,
	       tse_task_t *obj_task)
{
	struct obj_req_tgts	*req_tgts = &obj_auxi->req_tgts;
	d_list_t		*task_list = &obj_auxi->shard_task_head;
	tse_task_t		*shard_task;
	struct shard_auxi_args	*shard_auxi;
	struct daos_shard_tgt	*tgt;
	uint32_t		 i, tgts_nr;
	bool			 require_shard_task = false;
	int			 rc = 0;

	tgt = req_tgts->ort_shard_tgts;
	tgts_nr = req_tgts->ort_srv_disp ? req_tgts->ort_grp_nr :
		  req_tgts->ort_grp_nr * req_tgts->ort_grp_size;

	/* See shard_io_task. */
	if (daos_handle_is_valid(obj_auxi->th) && !dtx_epoch_chosen(epoch))
		require_shard_task = true;

	/* for retried obj IO, reuse the previous shard tasks and resched it */
	if (obj_auxi->io_retry) {
		switch (obj_auxi->opc) {
		case DAOS_OBJ_RPC_FETCH:
		case DAOS_OBJ_RPC_UPDATE:
		case DAOS_OBJ_RPC_ENUMERATE:
		case DAOS_OBJ_DKEY_RPC_ENUMERATE:
		case DAOS_OBJ_AKEY_RPC_ENUMERATE:
		case DAOS_OBJ_RECX_RPC_ENUMERATE:
		case DAOS_OBJ_RPC_PUNCH:
		case DAOS_OBJ_RPC_PUNCH_DKEYS:
		case DAOS_OBJ_RPC_PUNCH_AKEYS:
		case DAOS_OBJ_RPC_KEY2ANCHOR:
			/* For distributed transaction, check whether TX pool
			 * map is stale or not, if stale, restart the TX.
			 */
			if (daos_handle_is_valid(obj_auxi->th)) {
				rc = dc_tx_check_pmv(obj_auxi->th);
				if (rc != 0)
					goto out_task;
			}
			break;
		default:
			break;
		}
	}

	/*
	 * We mark the RPC as RESEND although @io_retry does not
	 * guarantee that the RPC has ever been sent. It may cause
	 * some overhead on server side, but no correctness issues.
	 *
	 * On the other hand, the client may resend the RPC to new
	 * shard if leader switched. That is why the resend logic
	 * is handled at object layer rather than shard layer.
	 */
	if (obj_auxi->io_retry && !obj_auxi->tx_renew)
		obj_auxi->flags |= ORF_RESEND;
	obj_auxi->tx_renew = 0;

	/* for retried obj IO, reuse the previous shard tasks and resched it */
	if (obj_auxi->io_retry && obj_auxi->args_initialized &&
	    !obj_auxi->new_shard_tasks) {
		/* if with shard task list, reuse it and re-schedule */
		if (!d_list_empty(task_list)) {
			struct shard_task_reset_arg reset_arg;

			reset_arg.req_tgts = req_tgts;
			reset_arg.epoch = *epoch;
			reset_arg.index = 0;
			/* For srv dispatch, the task_list non-empty is only for
			 * obj punch that with multiple RDG that each with a
			 * leader. Here reset the header for the shard task.
			 */
			if (req_tgts->ort_srv_disp || obj_auxi->reset_param)
				tse_task_list_traverse(task_list,
					shard_task_reset_param, &reset_arg);
			goto task_sched;
		} else if (require_shard_task) {
			/*
			 * The absence of shard tasks indicates that the epoch
			 * was already chosen in the previous attempt. In this
			 * attempt, since an epoch has not been chosen yet, the
			 * TX must have been restarted between the two
			 * attempts. This operation, therefore, is no longer
			 * relevant for the restarted TX.
			 *
			 * This is only a temporary workaround; we will prevent
			 * this case from happening in the first place, by
			 * aborting and waiting for associated operations when
			 * restarting a TX.
			 */
			return -DER_OP_CANCELED;
		} else {
			D_ASSERT(tgts_nr == 1);
			shard_auxi = obj_embedded_shard_arg(obj_auxi);
			D_ASSERT(shard_auxi != NULL);
			shard_auxi_set_param(shard_auxi, map_ver, tgt->st_shard,
					     tgt->st_tgt_id, epoch,
					     (uint16_t)tgt->st_ec_tgt);
			shard_auxi->start_shard = req_tgts->ort_start_shard;
			shard_auxi->shard_io_cb = io_cb;
			rc = shard_io(obj_task, shard_auxi);
			return rc;
		}
	}

	/* if with only one target, need not to create separate shard task */
	if (tgts_nr == 1 && !require_shard_task) {
		shard_auxi = obj_embedded_shard_arg(obj_auxi);
		D_ASSERT(shard_auxi != NULL);
		shard_auxi_set_param(shard_auxi, map_ver, tgt->st_shard,
				     tgt->st_tgt_id, epoch, (uint16_t)tgt->st_ec_tgt);
		shard_auxi->grp_idx = 0;
		shard_auxi->start_shard = req_tgts->ort_start_shard;
		shard_auxi->obj_auxi = obj_auxi;
		shard_auxi->shard_io_cb = io_cb;
		rc = io_prep_cb(shard_auxi, obj, obj_auxi, shard_auxi->grp_idx);
		if (rc)
			goto out_task;

		obj_auxi->args_initialized = 1;
		obj_auxi->shards_scheded = 1;

		/* for fail case the obj_task will be completed in shard_io() */
		rc = shard_io(obj_task, shard_auxi);
		return rc;
	}

	D_ASSERT(d_list_empty(task_list));

	/* for multi-targets, schedule it by tse sub-shard-tasks */
	for (i = 0; i < tgts_nr; i++) {
		if (tgt->st_rank == DAOS_TGT_IGNORE)
			goto next;

		rc = tse_task_create(shard_io_task, tse_task2sched(obj_task),
				     NULL, &shard_task);
		if (rc != 0)
			goto out_task;

		shard_auxi = tse_task_buf_embedded(shard_task,
						   sizeof(*shard_auxi));
		shard_auxi_set_param(shard_auxi, map_ver, tgt->st_shard,
				     tgt->st_tgt_id, epoch, (uint16_t)tgt->st_ec_tgt);
		shard_auxi->grp_idx = req_tgts->ort_srv_disp ? i :
				      (i / req_tgts->ort_grp_size);
		shard_auxi->start_shard = req_tgts->ort_start_shard;
		shard_auxi->obj_auxi = obj_auxi;
		shard_auxi->shard_io_cb = io_cb;
		rc = io_prep_cb(shard_auxi, obj, obj_auxi, shard_auxi->grp_idx);
		if (rc) {
			obj_task_complete(shard_task, rc);
			goto out_task;
		}

		rc = tse_task_register_deps(obj_task, 1, &shard_task);
		if (rc != 0) {
			obj_task_complete(shard_task, rc);
			goto out_task;
		}
		/* decref and delete from head at shard_task_remove */
		tse_task_addref(shard_task);
		tse_task_list_add(shard_task, task_list);
next:
		if (req_tgts->ort_srv_disp)
			tgt += req_tgts->ort_grp_size;
		else
			tgt++;
	}

	obj_auxi->args_initialized = 1;

task_sched:
	if (!d_list_empty(&obj_auxi->shard_task_head))
		obj_shard_task_sched(obj_auxi, epoch);
	else
		obj_task_complete(obj_task, rc);

	return 0;

out_task:
	if (!d_list_empty(task_list)) {
		D_ASSERTF(!obj_retry_error(rc), "unexpected ret "DF_RC"\n",
			DP_RC(rc));

		/* abort/complete sub-tasks will complete obj_task */
		tse_task_list_traverse(task_list, shard_task_abort, &rc);
	} else {
		obj_task_complete(obj_task, rc);
	}

	return rc;
}

static int
shard_task_remove(tse_task_t *task, void *arg)
{
	tse_task_list_del(task);
	tse_task_decref(task);
	return 0;
}

static void
shard_task_list_init(struct obj_auxi_args *auxi)
{
	if (auxi->io_retry == 0)
		D_INIT_LIST_HEAD(&auxi->shard_task_head);
}

static void
obj_rw_csum_destroy(const struct dc_object *obj, struct obj_auxi_args *obj_auxi)
{
	struct daos_csummer	*csummer = obj->cob_co->dc_csummer;

	if (!daos_csummer_initialized(csummer))
		return;
	daos_csummer_free_ci(csummer, &obj_auxi->rw_args.dkey_csum);
	daos_csummer_free_ic(csummer, &obj_auxi->rw_args.iod_csums);
}

static void
obj_shard_list_fini(daos_obj_list_t *obj_args, struct shard_list_args *shard_arg)
{
	if (shard_arg->la_akey_anchor &&
	    shard_arg->la_akey_anchor != obj_args->akey_anchor) {
		D_FREE(shard_arg->la_akey_anchor);
		shard_arg->la_akey_anchor = NULL;
	}

	if (shard_arg->la_dkey_anchor &&
	    shard_arg->la_dkey_anchor != obj_args->dkey_anchor) {
		D_FREE(shard_arg->la_dkey_anchor);
		shard_arg->la_dkey_anchor = NULL;
	}

	if (shard_arg->la_anchor &&
	    shard_arg->la_anchor != obj_args->anchor) {
		D_FREE(shard_arg->la_anchor);
		shard_arg->la_anchor = NULL;
	}

	shard_arg->la_kds = NULL;
	shard_arg->la_recxs = NULL;
}

static int
shard_list_task_fini(tse_task_t *task, void *arg)
{
	struct obj_auxi_args	*obj_auxi = arg;
	daos_obj_list_t		*obj_arg;
	struct shard_auxi_args	*shard_auxi;
	struct shard_list_args	*shard_arg;

	obj_arg = dc_task_get_args(obj_auxi->obj_task);
	shard_auxi = tse_task_buf_embedded(task, sizeof(*shard_auxi));
	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);

	obj_shard_list_fini(obj_arg, shard_arg);
	return 0;
}

static void
obj_auxi_list_fini(struct obj_auxi_args *obj_auxi)
{
	tse_task_list_traverse(&obj_auxi->shard_task_head,
			       shard_list_task_fini, obj_auxi);
}

struct comp_iter_arg {
	d_list_t	*merged_list;
	int		merge_nr;
	daos_off_t	merge_sgl_off;
	bool		cond_fetch_exist; /* cond_fetch got exist from one shard */
	bool		retry;
};

static struct obj_auxi_list_recx *
merge_recx_create_one(d_list_t *prev, uint64_t offset, uint64_t size, daos_epoch_t eph)
{
	struct obj_auxi_list_recx *new;

	D_ALLOC_PTR(new);
	if (new == NULL)
		return NULL;

	new->recx.rx_idx = offset;
	new->recx.rx_nr = size;
	new->recx_eph = eph;
	D_INIT_LIST_HEAD(&new->recx_list);
	d_list_add(&new->recx_list, prev);

	return new;
}

static bool
recx_can_merge_with_boundary(daos_recx_t *recx, uint64_t offset,
			     uint64_t size, uint64_t boundary)
{
	if (!daos_recx_can_merge_with_offset_size(recx, offset, size))
		return false;

	if (boundary == 0)
		return true;

	D_ASSERTF(recx->rx_idx / boundary == (DAOS_RECX_END(*recx) - 1) / boundary,
		  DF_U64"/"DF_U64" boundary "DF_U64"\n", recx->rx_idx, recx->rx_nr,
		  boundary);

	D_ASSERTF(offset / boundary == (offset + size - 1) / boundary,
		  DF_U64"/"DF_U64" boundary "DF_U64"\n", offset, size,
		  boundary);

	if (recx->rx_idx / boundary == (offset + size - 1) / boundary)
		return true;

	return false;
}

static int
merge_recx_insert(struct obj_auxi_list_recx *prev, d_list_t *head, uint64_t offset,
		  uint64_t size, daos_epoch_t eph, uint64_t boundary)
{
	uint64_t end = offset + size;

	while (size > 0) {
		struct obj_auxi_list_recx	*new;
		uint64_t			new_size;
		daos_epoch_t			new_eph;

		/* Split by boundary */
		if (boundary > 0) {
			new_size = min(roundup(offset + 1, boundary), end) - offset;
			if ((offset % boundary) == 0 || prev == NULL)
				new_eph = eph;
			else
				new_eph = max(prev->recx_eph, eph);
		} else {
			new_size = size;
			new_eph = eph;
		}

		/* Check if merging with previous recx or creating new one. */
		if (prev && recx_can_merge_with_boundary(&prev->recx, offset, new_size,
							 boundary)) {
			daos_recx_merge_with_offset_size(&prev->recx, offset, new_size);
			prev->recx_eph = max(prev->recx_eph, new_eph);
		} else {
			new = merge_recx_create_one(prev == NULL ? head : &prev->recx_list,
						    offset, new_size, new_eph);
			if (new == NULL)
				return -DER_NOMEM;
			prev = new;
		}

		offset += new_size;
		size -= new_size;
	}

	return 0;
}

int
merge_recx(d_list_t *head, uint64_t offset, uint64_t size, daos_epoch_t eph, uint64_t boundary)
{
	struct obj_auxi_list_recx	*recx;
	struct obj_auxi_list_recx	*tmp;
	struct obj_auxi_list_recx	*prev = NULL;
	bool				inserted = false;
	daos_off_t			end = offset + size;
	int				rc = 0;

	D_DEBUG(DB_TRACE, "merge "DF_U64"/"DF_U64" "DF_X64", boundary "DF_U64"\n",
		offset, size, eph, boundary);

	d_list_for_each_entry_safe(recx, tmp, head, recx_list) {
		if (end < recx->recx.rx_idx ||
		    daos_recx_can_merge_with_offset_size(&recx->recx, offset, size)) {
			rc = merge_recx_insert(prev, head, offset, size, eph, boundary);
			inserted = true;
			break;
		}
		prev = recx;
	}

	if (!inserted)
		rc = merge_recx_insert(prev, head, offset, size, eph, boundary);
	if (rc)
		return rc;

	prev = NULL;
	/* Check if the recx can be merged. */
	d_list_for_each_entry_safe(recx, tmp, head, recx_list) {
		if (prev == NULL) {
			prev = recx;
			continue;
		}

		if (recx_can_merge_with_boundary(&prev->recx, recx->recx.rx_idx,
						 recx->recx.rx_nr, boundary)) {
			daos_recx_merge(&recx->recx, &prev->recx);
			prev->recx_eph = max(prev->recx_eph, recx->recx_eph);
			d_list_del(&recx->recx_list);
			D_FREE(recx);
		} else {
			prev = recx;
		}
	}

	return rc;
}

static void
obj_recx_parity_to_daos(struct daos_oclass_attr *oca, daos_recx_t *recx)
{
	uint64_t cur_off = recx->rx_idx & ~PARITY_INDICATOR;

	D_ASSERT(recx->rx_idx % obj_ec_cell_rec_nr(oca) == 0);
	D_ASSERT(recx->rx_nr % obj_ec_cell_rec_nr(oca) == 0);
	recx->rx_idx = obj_ec_idx_parity2daos(cur_off,
					      obj_ec_cell_rec_nr(oca),
					      obj_ec_stripe_rec_nr(oca));
	recx->rx_nr *= obj_ec_data_tgt_nr(oca);
}

static int
obj_ec_recxs_convert(d_list_t *merged_list, daos_recx_t *recx,
		     struct shard_auxi_args *shard_auxi)
{
	struct daos_oclass_attr	*oca;
	uint64_t		total_size = recx->rx_nr;
	uint64_t		cur_off = recx->rx_idx & ~PARITY_INDICATOR;
	uint32_t		shard;
	int			rc = 0;
	int			cell_nr;
	int			stripe_nr;

	oca = obj_get_oca(shard_auxi->obj_auxi->obj);
	/* Normally the enumeration is sent to the parity node */
	/* convert the parity off to daos off */
	if (recx->rx_idx & PARITY_INDICATOR) {
		D_DEBUG(DB_IO, "skip parity recx "DF_RECX"\n", DP_RECX(*recx));
		return 0;
	}

	if (merged_list == NULL)
		return 0;

	cell_nr = obj_ec_cell_rec_nr(oca);
	stripe_nr = obj_ec_stripe_rec_nr(oca);
	shard = shard_auxi->shard % obj_get_grp_size(shard_auxi->obj_auxi->obj);
	shard = obj_ec_shard_off(shard_auxi->obj_auxi->obj,
				 shard_auxi->obj_auxi->dkey_hash, shard);
	/* If all parity nodes are down(degraded mode), then
	 * the enumeration is sent to all data nodes.
	 */
	while (total_size > 0) {
		uint64_t daos_off;
		uint64_t data_size;

		daos_off = obj_ec_idx_vos2daos(cur_off, stripe_nr, cell_nr, shard);
		data_size = min(roundup(cur_off + 1, cell_nr) - cur_off,
				total_size);
		rc = merge_recx(merged_list, daos_off, data_size, 0, 0);
		if (rc)
			break;
		D_DEBUG(DB_IO, "total "DF_U64" merge "DF_U64"/"DF_U64"\n",
			total_size, daos_off, data_size);
		total_size -= data_size;
		cur_off += data_size;
	}
	return rc;
}

static int
obj_shard_list_recx_cb(struct shard_auxi_args *shard_auxi,
		       struct obj_auxi_args *obj_auxi, void *arg)
{
	struct comp_iter_arg	*iter_arg = arg;
	struct shard_list_args	*shard_arg;
	int i;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	/* convert recxs for EC object*/
	for (i = 0; i < shard_arg->la_nr; i++) {
		int rc;

		rc = obj_ec_recxs_convert(iter_arg->merged_list,
					  &shard_arg->la_recxs[i],
					  shard_auxi);
		if (rc) {
			if (obj_auxi->obj_task->dt_result == 0)
				obj_auxi->obj_task->dt_result = rc;
			D_ERROR(DF_OID" recx convert failed: %d\n",
				DP_OID(obj_auxi->obj->cob_md.omd_id), rc);
			return rc;
		}
	}

	return 0;
}

static int
obj_shard_list_obj_cb(struct shard_auxi_args *shard_auxi,
		      struct obj_auxi_args *obj_auxi, void *arg)
{
	struct comp_iter_arg	*iter_arg = arg;
	struct shard_list_args	*shard_arg;
	daos_obj_list_t		*obj_arg = dc_task_get_args(obj_auxi->obj_task);
	char			*ptr = obj_arg->sgl->sg_iovs[0].iov_buf;
	daos_key_desc_t		*kds = obj_arg->kds;
	int			i;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	ptr += iter_arg->merge_sgl_off;
	D_ASSERTF(obj_arg->sgl->sg_iovs[0].iov_buf_len >=
		  obj_arg->sgl->sg_iovs[0].iov_len +
		  shard_arg->la_sgl->sg_iovs[0].iov_len,
		  "buf size %zu/%zu shard buf size %zu shard %u "DF_OID" shard_nr %u merge_nr %u\n",
		  obj_arg->sgl->sg_iovs[0].iov_buf_len, obj_arg->sgl->sg_iovs[0].iov_len,
		  shard_arg->la_sgl->sg_iovs[0].iov_len, shard_auxi->shard,
		  DP_OID(obj_auxi->obj->cob_md.omd_id), shard_arg->la_nr, iter_arg->merge_nr);
	memcpy(ptr, shard_arg->la_sgl->sg_iovs[0].iov_buf,
	       shard_arg->la_sgl->sg_iovs[0].iov_len);
	obj_arg->sgl->sg_iovs[0].iov_len +=
		shard_arg->la_sgl->sg_iovs[0].iov_len;
	iter_arg->merge_sgl_off += shard_arg->la_sgl->sg_iovs[0].iov_len;

	kds += iter_arg->merge_nr;
	for (i = 0; i < shard_arg->la_nr; i++)
		kds[i] = shard_arg->la_kds[i];
	iter_arg->merge_nr += shard_arg->la_nr;

	D_DEBUG(DB_TRACE, "shard %u shard nr %u merge_nr %d/"DF_U64"\n", shard_auxi->shard,
		shard_arg->la_nr, iter_arg->merge_nr, obj_arg->sgl->sg_iovs[0].iov_len);
	return 0;
}

static void
enum_hkey_gen(struct dc_object *obj, daos_key_t *key, void *hkey)
{
	if (daos_is_dkey_uint64(obj->cob_md.omd_id)) {
		hkey_int_gen(key, hkey);
		return;
	}

	hkey_common_gen(key, hkey);
}

static int
merge_key(struct dc_object *obj, d_list_t *head, char *key, int key_size)
{
	struct obj_auxi_list_key	*new_key;
	struct obj_auxi_list_key	*key_one;
	bool				inserted = false;

	d_list_for_each_entry(key_one, head, key_list) {
		if (key_size == key_one->key.iov_len &&
		    strncmp(key_one->key.iov_buf, key, key_size) == 0) {
			return 0;
		}
	}

	D_ALLOC_PTR(new_key);
	if (new_key == NULL)
		return -DER_NOMEM;

	D_ALLOC(new_key->key.iov_buf, key_size);
	if (new_key->key.iov_buf == NULL) {
		D_FREE(new_key);
		return -DER_NOMEM;
	}

	memcpy(new_key->key.iov_buf, key, key_size);
	new_key->key.iov_buf_len = key_size;
	new_key->key.iov_len = key_size;
	enum_hkey_gen(obj, &new_key->key, &new_key->hkey);
	D_INIT_LIST_HEAD(&new_key->key_list);

	/* Insert the key into the sorted list */
	d_list_for_each_entry(key_one, head, key_list) {
		if (hkey_common_cmp(&new_key->hkey, &key_one->hkey) == BTR_CMP_LT) {
			d_list_add_tail(&new_key->key_list, &key_one->key_list);
			inserted = true;
			break;
		}
	}

	if (!inserted)
		d_list_add_tail(&new_key->key_list, head);

	return 1;
}

static int
obj_shard_list_key_cb(struct shard_auxi_args *shard_auxi,
		      struct obj_auxi_args *obj_auxi, void *arg)
{
	struct shard_list_args  *shard_arg;
	struct comp_iter_arg	*iter_arg = arg;
	d_sg_list_t		*sgl;
	d_iov_t			*iov;
	int			sgl_off = 0;
	int			iov_off = 0;
	int			i;
	int			rc = 0;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	if (shard_arg->la_sgl == NULL)
		return 0;

	/* If there are several shards do listing all together, then
	 * let's merge the key to get rid of duplicate keys from different
	 * shards.
	 */
	sgl = shard_arg->la_sgl;
	iov = &sgl->sg_iovs[sgl_off];
	for (i = 0; i < shard_arg->la_nr; i++) {
		int key_size = shard_arg->la_kds[i].kd_key_len;
		bool alloc_key = false;
		char *key = NULL;

		if (key_size <= (iov->iov_len - iov_off)) {
			key = (char *)iov->iov_buf + iov_off;
			iov_off += key_size;
			if (iov_off == iov->iov_len) {
				iov_off = 0;
				iov = &sgl->sg_iovs[++sgl_off];
			}
		} else {
			int left = key_size;

			D_ALLOC(key, key_size);
			if (key == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			alloc_key = true;
			while (left > 0) {
				int copy_size = min(left, iov->iov_len - iov_off);
				char *ptr = (char *)iov->iov_buf + iov_off;

				memcpy(key, ptr, copy_size);
				iov_off += copy_size;
				key += copy_size;
				left -= copy_size;
				if (iov_off == iov->iov_len - 1) {
					iov_off = 0;
					iov = &sgl->sg_iovs[++sgl_off];
				}
			}
		}

		rc = merge_key(obj_auxi->obj, iter_arg->merged_list, key, key_size);
		if (alloc_key)
			D_FREE(key);

		if (rc < 0)
			break;

		if (rc == 1) {
			iter_arg->merge_nr++;
			D_DEBUG(DB_TRACE, "merged %.*s cnt %d\n", key_size, key,
				iter_arg->merge_nr);
			rc = 0;
		}
	}

out:
	return rc;
}

static int
obj_shard_list_comp_cb(struct shard_auxi_args *shard_auxi,
		       struct obj_auxi_args *obj_auxi, void *cb_arg)
{
	struct comp_iter_arg	*iter_arg = cb_arg;
	struct shard_list_args	*shard_arg;
	int			rc = 0;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	if (obj_auxi->req_tgts.ort_grp_size == 1) {
		if (obj_is_ec(obj_auxi->obj) &&
		    obj_auxi->opc == DAOS_OBJ_RECX_RPC_ENUMERATE &&
		    shard_arg->la_recxs != NULL) {
			int		i;
			daos_obj_list_t	*obj_args;

			obj_args = dc_task_get_args(obj_auxi->obj_task);
			for (i = 0; i < shard_arg->la_nr; i++) {
				int index;

				index = obj_args->incr_order?i:(shard_arg->la_nr - 1 - i);

				if (shard_arg->la_recxs[index].rx_idx & PARITY_INDICATOR)
					obj_recx_parity_to_daos(obj_get_oca(obj_auxi->obj),
								&shard_arg->la_recxs[index]);

				/* DAOS-9218: The output merged list will latter be reversed.  That
				 * will be done in the function obj_list_recxs_cb(), when the merged
				 * list will be dumped into the output buffer.
				 */
				rc = merge_recx(iter_arg->merged_list,
						shard_arg->la_recxs[index].rx_idx,
						shard_arg->la_recxs[index].rx_nr, 0, 0);
				if (rc)
					return rc;
			}

			return 0;
		}

		iter_arg->merge_nr = shard_arg->la_nr;
		return 0;
	}

	switch (obj_auxi->opc) {
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
		rc = obj_shard_list_key_cb(shard_auxi, obj_auxi, cb_arg);
		break;
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		rc = obj_shard_list_recx_cb(shard_auxi, obj_auxi, cb_arg);
		break;
	case DAOS_OBJ_RPC_ENUMERATE:
		rc = obj_shard_list_obj_cb(shard_auxi, obj_auxi, cb_arg);
		break;
	default:
		D_ASSERTF(0, "opc is %d\n", obj_auxi->opc);
	}

	return rc;
}

static int
obj_shard_comp_cb(tse_task_t *task, struct shard_auxi_args *shard_auxi,
		  struct obj_auxi_args *obj_auxi, void *cb_arg)
{
	struct comp_iter_arg	*iter_arg = cb_arg;
	int			ret = task->dt_result;

	if (shard_auxi == NULL) {
		iter_arg->retry = false;
		return ret;
	}

	/*
	 * Check shard IO task's completion status:
	 * 1) if succeed just stores the highest replied pm version.
	 * 2) if any shard failed, store it in obj_auxi->result, the
	 *    un-retryable error with higher priority.
	 */
	if (ret == 0) {
		if (obj_auxi->map_ver_reply < shard_auxi->map_ver)
			obj_auxi->map_ver_reply = shard_auxi->map_ver;
		if (obj_req_is_ec_cond_fetch(obj_auxi)) {
			iter_arg->cond_fetch_exist = true;
			if (obj_auxi->result == -DER_NONEXIST)
				obj_auxi->result = 0;
			D_DEBUG(DB_IO, "shard %d EC cond_fetch replied 0 - exist.\n",
				shard_auxi->shard);
		}
	} else if (obj_retry_error(ret)) {
		int rc;

		D_DEBUG(DB_IO, "shard %d ret %d.\n", shard_auxi->shard, ret);
		if (obj_auxi->result == 0)
			obj_auxi->result = ret;
		/* If the failure needs to be retried from different redundancy shards,
		 * then let's remember the failure targets to make sure these targets
		 * will be skipped during retry, see obj_ec_valid_shard_get() and
		 * need_retry_redundancy().
		 */
		if ((ret == -DER_TX_UNCERTAIN || ret == -DER_CSUM || ret == -DER_NVME_IO) &&
		    obj_auxi->is_ec_obj) {
			rc = obj_auxi_add_failed_tgt(obj_auxi, shard_auxi->target);
			if (rc != 0) {
				D_ERROR("failed to add tgt %u to failed list: %d\n",
					shard_auxi->target, rc);
				ret = rc;
			}
		}
	} else if (ret == -DER_TGT_RETRY) {
		/* some special handing for DER_TGT_RETRY, as we use that errno for
		 * some retry cases.
		 */
		if (obj_auxi->result == 0 || obj_retry_error(obj_auxi->result))
			obj_auxi->result = ret;
	} else if (ret == -DER_NONEXIST && obj_req_is_ec_cond_fetch(obj_auxi)) {
		D_DEBUG(DB_IO, "shard %d EC cond_fetch replied -DER_NONEXIST.\n",
			shard_auxi->shard);
		if (obj_auxi->result == 0 && !iter_arg->cond_fetch_exist)
			obj_auxi->result = ret;
		ret = 0;
	} else {
		/* for un-retryable failure, set the err to whole obj IO */
		D_DEBUG(DB_IO, "shard %d ret %d.\n", shard_auxi->shard, ret);
		obj_auxi->result = ret;
	}

	if (ret) {
		if (ret == -DER_NONEXIST && obj_is_fetch_opc(obj_auxi->opc)) {
			/** Conditional fetch returns -DER_NONEXIST if the key doesn't exist. We
			 *  do not want to try another replica in this case.
			 */
			D_DEBUG(DB_IO, "shard %d fetch returned -DER_NONEXIST, no retry on "
				"conditional\n", shard_auxi->shard);
			iter_arg->retry = false;
		} else if (ret != -DER_REC2BIG && !obj_retry_error(ret) &&
			   !obj_is_modification_opc(obj_auxi->opc) &&
			   !obj_auxi->is_ec_obj && !obj_auxi->spec_shard &&
			   !obj_auxi->spec_group && !obj_auxi->to_leader &&
			   ret != -DER_TX_RESTART && ret != -DER_RF &&
			   !DAOS_FAIL_CHECK(DAOS_DTX_NO_RETRY)) {
			int new_tgt;
			int rc;

			/* Check if there are other replicas available to
			 * fulfill the request
			 */
			rc = obj_auxi_add_failed_tgt(obj_auxi, shard_auxi->target);
			if (rc != 0) {
				D_ERROR("failed to add tgt %u to failed list: %d\n",
					shard_auxi->target, rc);
				ret = rc;
			}
			new_tgt = obj_shard_find_replica(obj_auxi->obj,
						 shard_auxi->target,
						 obj_auxi->failed_tgt_list);
			if (new_tgt >= 0) {
				D_DEBUG(DB_IO, "failed %d %u --> %u\n",
					ret, shard_auxi->target, new_tgt);
			} else {
				iter_arg->retry = false;
				D_DEBUG(DB_IO, "failed %d no replica %d"
					" new_tgt %d\n", ret,
					shard_auxi->target, new_tgt);
			}
		} else {
			if (ret == -DER_KEY2BIG && obj_is_enum_opc(obj_auxi->opc)) {
				daos_obj_list_t		*obj_arg;
				struct shard_list_args	*shard_arg;

				/* For KEY2BIG case, kds[0] from obj_arg will store the required KDS
				 * size, so let's copy it from shard to object kds.
				 */
				obj_arg = dc_task_get_args(obj_auxi->obj_task);
				shard_arg = container_of(shard_auxi,
							 struct shard_list_args, la_auxi);
				if (obj_arg->kds[0].kd_key_len < shard_arg->la_kds[0].kd_key_len) {
					D_DEBUG(DB_IO, "shard %u size "DF_U64" -> "DF_U64"\n",
						shard_auxi->shard, obj_arg->kds[0].kd_key_len,
						shard_arg->la_kds[0].kd_key_len);
					obj_arg->kds[0] = shard_arg->la_kds[0];
					iter_arg->merge_nr++;
				}
			}

			iter_arg->retry = false;
		}
		return ret;
	}

	/* Then process each shards for enumeration */
	if (obj_is_enum_opc(obj_auxi->opc)) {
		int rc;

		rc = obj_shard_list_comp_cb(shard_auxi, obj_auxi, cb_arg);
		if (rc != 0 && obj_auxi->result == 0)
			obj_auxi->result = rc;
	}

	return ret;
}

typedef int (*shard_comp_cb_t)(tse_task_t *task, struct shard_auxi_args *shard_auxi,
			       struct obj_auxi_args *obj_auxi, void *cb_arg);
struct shard_list_comp_cb_arg {
	shard_comp_cb_t		cb;
	struct obj_auxi_args	*obj_auxi;
	void			*cb_arg;
};

static int
shard_auxi_task_cb(tse_task_t *task, void *data)
{
	struct shard_list_comp_cb_arg	*arg = data;
	struct shard_auxi_args		*shard_auxi;

	shard_auxi = tse_task_buf_embedded(task, sizeof(*shard_auxi));

	return arg->cb(task, shard_auxi, arg->obj_auxi, arg->cb_arg);
}

static int
obj_auxi_shards_iterate(struct obj_auxi_args *obj_auxi, shard_comp_cb_t cb,
			void *cb_arg)
{
	struct shard_list_comp_cb_arg	arg;
	d_list_t			*head;
	int				rc;

	if (!obj_auxi->shards_scheded)
		return 0;

	head = &obj_auxi->shard_task_head;
	if (d_list_empty(head)) {
		struct shard_auxi_args *shard_auxi;

		shard_auxi = obj_embedded_shard_arg(obj_auxi);
		rc = cb(obj_auxi->obj_task, shard_auxi, obj_auxi, cb_arg);
		return rc;
	}

	arg.cb = cb;
	arg.cb_arg = cb_arg;
	arg.obj_auxi = obj_auxi;
	return tse_task_list_traverse(head, shard_auxi_task_cb, &arg);
}

static struct shard_anchors*
obj_get_sub_anchors(daos_obj_list_t *obj_args, int opc)
{
	switch (opc) {
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		return (struct shard_anchors *)obj_args->dkey_anchor->da_sub_anchors;
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
		return (struct shard_anchors *)obj_args->akey_anchor->da_sub_anchors;
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_KEY2ANCHOR:
		return (struct shard_anchors *)obj_args->anchor->da_sub_anchors;
	}
	return NULL;
}

static void
obj_set_sub_anchors(daos_obj_list_t *obj_args, int opc, struct shard_anchors *anchors)
{
	switch (opc) {
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		obj_args->dkey_anchor->da_sub_anchors = (uint64_t)anchors;
		break;
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
		obj_args->akey_anchor->da_sub_anchors = (uint64_t)anchors;
		break;
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_KEY2ANCHOR:
		obj_args->anchor->da_sub_anchors = (uint64_t)anchors;
		break;
	}
}

static int
shard_anchor_lookup(struct shard_anchors *anchors, uint32_t shard)
{
	int i;

	for (i = 0; i < anchors->sa_anchors_nr; i++) {
		if (anchors->sa_anchors[i].ssa_shard == shard)
			return i;
	}

	return -1;
}

static int
update_sub_anchor_cb(tse_task_t *shard_task, struct shard_auxi_args *shard_auxi,
		     struct obj_auxi_args *obj_auxi, void *cb_arg)
{
	tse_task_t		*task = obj_auxi->obj_task;
	daos_obj_list_t		*obj_arg = dc_task_get_args(task);
	struct shard_list_args	*shard_arg;
	struct shard_anchors	*sub_anchors;
	int			shard;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	if (obj_arg->anchor && obj_arg->anchor->da_sub_anchors) {
		sub_anchors = (struct shard_anchors *)obj_arg->anchor->da_sub_anchors;
		shard = shard_anchor_lookup(sub_anchors, shard_auxi->shard);
		D_ASSERT(shard != -1);
		memcpy(&sub_anchors->sa_anchors[shard].ssa_anchor,
		       shard_arg->la_anchor, sizeof(daos_anchor_t));
	}

	if (obj_arg->dkey_anchor && obj_arg->dkey_anchor->da_sub_anchors) {
		sub_anchors = (struct shard_anchors *)obj_arg->dkey_anchor->da_sub_anchors;

		shard = shard_anchor_lookup(sub_anchors, shard_auxi->shard);
		D_ASSERT(shard != -1);
		memcpy(&sub_anchors->sa_anchors[shard].ssa_anchor,
		       shard_arg->la_dkey_anchor, sizeof(daos_anchor_t));

		if (sub_anchors->sa_anchors[shard].ssa_recx_anchor && shard_arg->la_anchor)
			memcpy(sub_anchors->sa_anchors[shard].ssa_recx_anchor,
			       shard_arg->la_anchor, sizeof(daos_anchor_t));
		if (sub_anchors->sa_anchors[shard].ssa_akey_anchor && shard_arg->la_akey_anchor)
			memcpy(sub_anchors->sa_anchors[shard].ssa_akey_anchor,
			       shard_arg->la_akey_anchor, sizeof(daos_anchor_t));
	}

	if (obj_arg->akey_anchor && obj_arg->akey_anchor->da_sub_anchors) {
		sub_anchors = (struct shard_anchors *)obj_arg->akey_anchor->da_sub_anchors;
		shard = shard_anchor_lookup(sub_anchors, shard_auxi->shard);
		D_ASSERT(shard != -1);
		if (shard_arg->la_akey_anchor)
			memcpy(&sub_anchors->sa_anchors[shard].ssa_anchor,
			       shard_arg->la_akey_anchor, sizeof(daos_anchor_t));
	}

	return 0;
}

static void
merged_list_free(d_list_t *merged_list, int opc)
{
	if (opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		struct obj_auxi_list_recx *recx;
		struct obj_auxi_list_recx *tmp;

		d_list_for_each_entry_safe(recx, tmp, merged_list, recx_list) {
			d_list_del(&recx->recx_list);
			D_FREE(recx);
		}
	} else {
		struct obj_auxi_list_key *key;
		struct obj_auxi_list_key *tmp;

		d_list_for_each_entry_safe(key, tmp, merged_list, key_list) {
			d_list_del(&key->key_list);
			daos_iov_free(&key->key);
			D_FREE(key);
		}
	}
}

static void
shard_anchors_free(struct shard_anchors *sub_anchors, int opc)
{
	int i;

	merged_list_free(&sub_anchors->sa_merged_list, opc);
	for (i = 0; i < sub_anchors->sa_anchors_nr; i++) {
		struct shard_sub_anchor *sub;

		sub = &sub_anchors->sa_anchors[i];
		if (sub->ssa_sgl.sg_iovs)
			d_sgl_fini(&sub->ssa_sgl, true);
		if (sub->ssa_kds)
			D_FREE(sub->ssa_kds);
		if (sub->ssa_recxs)
			D_FREE(sub->ssa_recxs);
		if (sub->ssa_recx_anchor)
			D_FREE(sub->ssa_recx_anchor);
		if (sub->ssa_akey_anchor)
			D_FREE(sub->ssa_akey_anchor);
	}
	D_FREE(sub_anchors);
}

static void
sub_anchors_free(daos_obj_list_t *obj_args, int opc)
{
	struct shard_anchors *sub_anchors;

	sub_anchors = obj_get_sub_anchors(obj_args, opc);
	if (sub_anchors == NULL)
		return;

	shard_anchors_free(sub_anchors, opc);
	obj_set_sub_anchors(obj_args, opc, NULL);
}

static bool
sub_anchors_is_eof(struct shard_anchors *sub_anchors)
{
	int i;

	for (i = 0; i < sub_anchors->sa_anchors_nr; i++) {
		daos_anchor_t *sub_anchor;

		sub_anchor = &sub_anchors->sa_anchors[i].ssa_anchor;
		if (!daos_anchor_is_eof(sub_anchor))
			break;
	}

	return i == sub_anchors->sa_anchors_nr;
}

/* Update and Check anchor eof by sub anchors */
static void
anchor_update_check_eof(struct obj_auxi_args *obj_auxi, daos_anchor_t *anchor)
{
	struct shard_anchors	*sub_anchors;

	if (!anchor->da_sub_anchors || !obj_is_ec(obj_auxi->obj))
		return;

	/* update_anchor */
	obj_auxi_shards_iterate(obj_auxi, update_sub_anchor_cb, NULL);

	sub_anchors = (struct shard_anchors *)anchor->da_sub_anchors;
	if (!d_list_empty(&sub_anchors->sa_merged_list))
		return;

	if (sub_anchors_is_eof(sub_anchors)) {
		daos_obj_list_t *obj_args;

		daos_anchor_set_eof(anchor);

		obj_args = dc_task_get_args(obj_auxi->obj_task);
		sub_anchors_free(obj_args, obj_auxi->opc);
	}
}

static int
dump_key_and_anchor_eof_check(struct obj_auxi_args *obj_auxi,
			      daos_anchor_t *anchor,
			      struct comp_iter_arg *arg)
{
	struct obj_auxi_list_key *key;
	struct obj_auxi_list_key *tmp;
	daos_obj_list_t *obj_args;
	d_sg_list_t	*sgl;
	daos_key_desc_t	*kds;
	d_iov_t		*iov;
	int		sgl_off = 0;
	int		iov_off = 0;
	int		cnt = 0;
	int		rc = 0;

	/* 1. Dump the keys from merged_list into user input buffer(@sgl) */
	D_ASSERT(obj_auxi->is_ec_obj);
	obj_args = dc_task_get_args(obj_auxi->obj_task);
	sgl = obj_args->sgl;
	kds = obj_args->kds;
	iov = &sgl->sg_iovs[sgl_off];
	d_list_for_each_entry_safe(key, tmp, arg->merged_list, key_list) {
		int left = key->key.iov_len;

		D_DEBUG(DB_TRACE, DF_OID" opc 0x%x cnt %d key "DF_KEY"\n",
			DP_OID(obj_auxi->obj->cob_md.omd_id), obj_auxi->opc,
			cnt + 1, DP_KEY(&key->key));
		while (left > 0) {
			int copy_size = min(iov->iov_buf_len - iov_off,
					    key->key.iov_len);
			memcpy(iov->iov_buf + iov_off, key->key.iov_buf, copy_size);
			kds[cnt].kd_key_len = copy_size;
			if (obj_auxi->opc == DAOS_OBJ_DKEY_RPC_ENUMERATE)
				kds[cnt].kd_val_type = OBJ_ITER_DKEY;
			else
				kds[cnt].kd_val_type = OBJ_ITER_AKEY;
			left -= copy_size;
			iov_off += copy_size;
			if (iov_off == iov->iov_buf_len) {
				iov_off = 0;
				if (++sgl_off == sgl->sg_nr) {
					if (cnt == 0) {
						kds[0].kd_key_len = key->key.iov_len;
						D_DEBUG(DB_IO, "retry by "DF_U64"\n",
							kds[0].kd_key_len);
						D_GOTO(out, rc = -DER_KEY2BIG);
					}
					D_GOTO(finished, rc);
				}
				iov = &sgl->sg_iovs[sgl_off];
			}
		}
		d_list_del(&key->key_list);
		D_FREE(key->key.iov_buf);
		D_FREE(key);
		if (++cnt >= *obj_args->nr)
			break;
	}

finished:
	*obj_args->nr = cnt;

	/* 2. Check sub anchors to see if anchors is eof */
	anchor_update_check_eof(obj_auxi, anchor);
out:
	return rc;
}

static void
obj_list_akey_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		 struct comp_iter_arg *arg)
{
	daos_obj_list_t	*obj_arg = dc_task_get_args(obj_auxi->obj_task);
	daos_anchor_t	*anchor = obj_arg->akey_anchor;

	if (task->dt_result != 0)
		return;

	if (anchor->da_sub_anchors) {
		task->dt_result = dump_key_and_anchor_eof_check(obj_auxi, anchor, arg);
	} else
		*obj_arg->nr = arg->merge_nr;

	if (daos_anchor_is_eof(anchor))
		D_DEBUG(DB_IO, "Enumerated All shards\n");
}

static void
obj_list_dkey_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		 struct comp_iter_arg *arg)
{
	struct dc_object       *obj;
	daos_obj_list_t	*obj_arg = dc_task_get_args(obj_auxi->obj_task);
	daos_anchor_t	*anchor = obj_arg->dkey_anchor;
	uint32_t	shard = dc_obj_anchor2shard(anchor);
	int		grp_size;

	if (task->dt_result != 0)
		return;

	obj = obj_auxi->obj;
	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	if (anchor->da_sub_anchors)
		task->dt_result = dump_key_and_anchor_eof_check(obj_auxi, anchor, arg);
	else
		*obj_arg->nr = arg->merge_nr;

	if (!daos_anchor_is_eof(anchor)) {
		D_DEBUG(DB_IO, "More keys in shard %d\n", shard);
	} else if (!obj_auxi->spec_shard && !obj_auxi->spec_group &&
		   (shard < obj->cob_shards_nr - grp_size)) {
		shard += grp_size;
		D_DEBUG(DB_IO, "next shard %d grp %d nr %u\n",
			shard, grp_size, obj->cob_shards_nr);

		daos_anchor_set_zero(anchor);
		dc_obj_shard2anchor(anchor, shard);
	} else {
		D_DEBUG(DB_IO, "Enumerated All shards\n");
	}
}

static int
obj_list_recxs_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		  struct comp_iter_arg *arg)
{
	struct obj_auxi_list_recx *recx;
	struct obj_auxi_list_recx *tmp;
	daos_obj_list_t		  *obj_args;
	int			  idx = 0;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	if (d_list_empty(arg->merged_list)) {
		anchor_update_check_eof(obj_auxi, obj_args->anchor);
		*obj_args->nr = arg->merge_nr;
		return 0;
	}

	D_ASSERT(obj_is_ec(obj_auxi->obj));
	if (obj_args->incr_order) {
		d_list_for_each_entry_safe(recx, tmp, arg->merged_list,
					   recx_list) {
			if (idx >= *obj_args->nr)
				break;
			obj_args->recxs[idx++] = recx->recx;
			d_list_del(&recx->recx_list);
			D_FREE(recx);
		}
	} else {
		d_list_for_each_entry_reverse_safe(recx, tmp, arg->merged_list,
						   recx_list) {
			if (idx >= *obj_args->nr)
				break;
			obj_args->recxs[idx++] = recx->recx;
			d_list_del(&recx->recx_list);
			D_FREE(recx);
		}
	}
	anchor_update_check_eof(obj_auxi, obj_args->anchor);
	*obj_args->nr = idx;
	return 0;
}

static void
obj_list_obj_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		struct comp_iter_arg *arg)
{
	daos_obj_list_t *obj_arg = dc_task_get_args(obj_auxi->obj_task);
	daos_anchor_t	*anchor = obj_arg->dkey_anchor;
	uint32_t	grp;

	*obj_arg->nr = arg->merge_nr;
	anchor_update_check_eof(obj_auxi, obj_arg->dkey_anchor);

	grp = dc_obj_anchor2shard(anchor) / obj_get_grp_size(obj_auxi->obj);
	if (!daos_anchor_is_eof(anchor)) {
		D_DEBUG(DB_IO, "More in grp %d\n", grp);
	} else if (!obj_auxi->spec_shard && !obj_auxi->spec_group &&
		   (grp < (obj_auxi->obj->cob_shards_nr / obj_get_grp_size(obj_auxi->obj) - 1))) {
		D_DEBUG(DB_IO, DF_OID" next grp %u total grp %u\n",
			DP_OID(obj_auxi->obj->cob_md.omd_id), grp + 1,
			obj_auxi->obj->cob_shards_nr / obj_get_grp_size(obj_auxi->obj));
		daos_anchor_set_zero(anchor);
		dc_obj_shard2anchor(anchor, (grp + 1) * obj_get_grp_size(obj_auxi->obj));
	} else {
		D_DEBUG(DB_IO, "Enumerated All shards\n");
	}
}

static int
obj_list_comp(struct obj_auxi_args *obj_auxi,
	      struct comp_iter_arg *arg)
{
	tse_task_t *task = obj_auxi->obj_task;

	switch (obj_auxi->opc) {
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
		obj_list_dkey_cb(task, obj_auxi, arg);
		break;
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
		obj_list_akey_cb(task, obj_auxi, arg);
		break;
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		obj_list_recxs_cb(task, obj_auxi, arg);
		break;
	case DAOS_OBJ_RPC_ENUMERATE:
		obj_list_obj_cb(task, obj_auxi, arg);
		break;
	default:
		D_ASSERTF(0, "opc is %d\n", obj_auxi->opc);
	}

	return 0;
}

static int
obj_comp_cb_internal(struct obj_auxi_args *obj_auxi)
{
	daos_obj_list_t		*obj_args;
	struct shard_anchors	*sub_anchors = NULL;
	d_list_t		merged_list;
	struct comp_iter_arg	iter_arg = { 0 };
	int			rc;

	if (obj_auxi->cond_fetch_split)
		return 0;

	iter_arg.retry = true;
	obj_args = dc_task_get_args(obj_auxi->obj_task);
	D_INIT_LIST_HEAD(&merged_list);
	if (obj_is_enum_opc(obj_auxi->opc)) {
		sub_anchors = obj_get_sub_anchors(obj_args, obj_auxi->opc);
		if (sub_anchors == NULL) {
			iter_arg.merged_list = &merged_list;
		} else {
			iter_arg.merged_list = &sub_anchors->sa_merged_list;
		}
	}

	/* Process each shards */
	rc = obj_auxi_shards_iterate(obj_auxi, obj_shard_comp_cb, &iter_arg);
	if (rc != 0) {
		if (iter_arg.retry) {
			D_DEBUG(DB_IO, DF_OID" retry by %d\n",
				DP_OID(obj_auxi->obj->cob_md.omd_id), rc);
			obj_auxi->io_retry = 1;
		}
		D_GOTO(out, rc);
	}

	if (obj_is_enum_opc(obj_auxi->opc))
		rc = obj_list_comp(obj_auxi, &iter_arg);
	else if (obj_auxi->opc == DAOS_OBJ_RPC_KEY2ANCHOR) {
		daos_obj_key2anchor_t *obj_arg = dc_task_get_args(obj_auxi->obj_task);
		int grp_idx;

		grp_idx = obj_dkey2grpidx(obj_auxi->obj, obj_auxi->dkey_hash,
					  obj_auxi->map_ver_req);

		D_ASSERTF(grp_idx >= 0, "grp_idx %d obj_auxi->map_ver_req %u",
			  grp_idx, obj_auxi->map_ver_req);
		obj_arg->anchor->da_shard = grp_idx * obj_get_grp_size(obj_auxi->obj);
		sub_anchors = (struct shard_anchors *)obj_arg->anchor->da_sub_anchors;
		if (sub_anchors) {
			if (sub_anchors_is_eof(sub_anchors))
				daos_anchor_set_eof(obj_arg->anchor);
			else
				daos_anchor_set_zero(obj_arg->anchor);
		}
	}
out:
	if (sub_anchors == NULL && obj_is_enum_opc(obj_auxi->opc))
		merged_list_free(&merged_list, obj_auxi->opc);
	D_DEBUG(DB_TRACE, "exit %d\n", rc);
	return rc;
}

/*
 * Remove current shard tasks (attached to obj_auxi->shard_task_head), and set
 * obj_auxi->new_shard_tasks flag, so when retrying that obj IO task, it will
 * re-create new shard task. This helper function can be used before retry IO
 * and the retried IO possibly with different targets or parameters.
 */
static void
obj_io_set_new_shard_task(struct obj_auxi_args *obj_auxi)
{
	d_list_t	*head;

	head = &obj_auxi->shard_task_head;
	if (head != NULL) {
		tse_task_list_traverse(head, shard_task_remove, NULL);
		D_ASSERT(d_list_empty(head));
	}
	obj_auxi->new_shard_tasks = 1;
}

static void
obj_size_fetch_cb(const struct dc_object *obj, struct obj_auxi_args *obj_auxi)
{
	daos_obj_rw_t	*api_args;
	daos_iod_t	*uiods, *iods;
	d_sg_list_t	*usgls;
	unsigned int     iod_nr, i;
	bool		 size_all_zero = true;

	api_args = dc_task_get_args(obj_auxi->obj_task);
	/* set iod_size to original user IOD */
	uiods = obj_auxi->reasb_req.orr_uiods;
	iods = api_args->iods;
	iod_nr = api_args->nr;
	D_ASSERT(uiods != iods);
	for (i = 0; i < iod_nr; i++) {
		if (uiods[i].iod_size != DAOS_REC_ANY) {
			D_ASSERT(iods[i].iod_size == 0 ||
				 iods[i].iod_size == uiods[i].iod_size);
			size_all_zero = false;
		} else {
			uiods[i].iod_size = iods[i].iod_size;
			D_DEBUG(DB_IO, DF_OID" set iod_size="DF_U64"\n",
				DP_OID(obj->cob_md.omd_id),
				iods[i].iod_size);
			if (uiods[i].iod_size != 0)
				size_all_zero = false;
		}
	}

	obj_auxi->reasb_req.orr_size_fetched = 1;
	usgls = obj_auxi->reasb_req.orr_usgls;
	if (usgls == NULL)
		return;

	if (size_all_zero) {
		for (i = 0; i < iod_nr; i++)
			usgls[i].sg_nr_out = 0;
	} else {
		D_DEBUG(DB_IO, DF_OID" retrying IO after size fetch.\n",
			DP_OID(obj->cob_md.omd_id));
		obj_io_set_new_shard_task(obj_auxi);
		obj_auxi->io_retry = 1;
	}
}

/**
 * User possibly provides sgl with iov_len < iov_buf_len, this may cause some troubles for internal
 * handling, such as crt_bulk_create/daos_iov_left() always use iov_buf_len.
 * For that case, we duplicate the sgls and make its iov_buf_len = iov_len.
 */
static int
obj_update_sgls_dup(struct obj_auxi_args *obj_auxi, daos_obj_update_t *args)
{
	daos_iod_t	*iod;
	d_sg_list_t	*sgls_dup, *sgls;
	d_sg_list_t	*sg, *sg_dup;
	d_iov_t		*iov, *iov_dup;
	bool		 dup = false;
	uint32_t	 i, j;
	int		 rc = 0;

	sgls = args->sgls;
	if (obj_auxi->rw_args.sgls_dup != NULL || sgls == NULL)
		return 0;

	for (i = 0; i < args->nr; i++) {
		sg = &sgls[i];
		iod = &args->iods[i];
		for (j = 0; j < sg->sg_nr; j++) {
			iov = &sg->sg_iovs[j];
			if (iov->iov_len > iov->iov_buf_len ||
			    (iov->iov_len == 0 && iod->iod_size != DAOS_REC_ANY)) {
				D_ERROR("invalid args, iov_len "DF_U64", iov_buf_len "DF_U64"\n",
					iov->iov_len, iov->iov_buf_len);
				return -DER_INVAL;
			} else if (iov->iov_len < iov->iov_buf_len) {
				dup = true;
			}
		}
	}
	if (dup == false)
		return 0;

	D_ALLOC_ARRAY(sgls_dup, args->nr);
	if (sgls_dup == NULL)
		return -DER_NOMEM;

	for (i = 0; i < args->nr; i++) {
		sg_dup = &sgls_dup[i];
		sg = &sgls[i];
		rc = d_sgl_init(sg_dup, sg->sg_nr);
		if (rc)
			goto failed;

		for (j = 0; j < sg_dup->sg_nr; j++) {
			iov_dup = &sg_dup->sg_iovs[j];
			iov = &sg->sg_iovs[j];
			*iov_dup = *iov;
			iov_dup->iov_buf_len = iov_dup->iov_len;
		}
	}
	obj_auxi->reasb_req.orr_usgls = sgls;
	obj_auxi->rw_args.sgls_dup = sgls_dup;
	args->sgls = sgls_dup;
	return 0;

failed:
	if (sgls_dup != NULL) {
		for (i = 0; i < args->nr; i++)
			d_sgl_fini(&sgls_dup[i], false);
		D_FREE(sgls_dup);
	}
	return rc;
}

static void
obj_update_sgls_free(struct obj_auxi_args *obj_auxi)
{
	int	i;

	if (obj_auxi->opc == DAOS_OBJ_RPC_UPDATE && obj_auxi->rw_args.sgls_dup != NULL) {
		daos_obj_rw_t	*api_args;

		for (i = 0; i < obj_auxi->iod_nr; i++)
			d_sgl_fini(&obj_auxi->rw_args.sgls_dup[i], false);
		D_FREE(obj_auxi->rw_args.sgls_dup);
		obj_auxi->rw_args.sgls_dup = NULL;
		api_args = dc_task_get_args(obj_auxi->obj_task);
		api_args->sgls = obj_auxi->reasb_req.orr_usgls;
	}
}

static void
obj_reasb_io_fini(struct obj_auxi_args *obj_auxi, bool retry)
{
	/* "retry" used for DER_FETCH_AGAIN case, that possibly used when iod_size updated
	 * from reply and need to re-assemble the request.
	 */
	if (retry && obj_auxi->reasb_req.orr_args != NULL) {
		D_ASSERT(obj_auxi->reasb_req.orr_uiods != NULL);
		obj_auxi->reasb_req.orr_args->iods = obj_auxi->reasb_req.orr_uiods;
		obj_auxi->reasb_req.orr_args->sgls = obj_auxi->reasb_req.orr_usgls;
	}
	obj_bulk_fini(obj_auxi);
	obj_auxi_free_failed_tgt_list(obj_auxi);
	obj_update_sgls_free(obj_auxi);
	obj_reasb_req_fini(&obj_auxi->reasb_req, obj_auxi->iod_nr);
	obj_auxi->req_reasbed = false;

	/* zero it as user might reuse/resched the task, for
	 * example the usage in dac_array_set_size().
	 */
	if (!retry)
		memset(obj_auxi, 0, sizeof(*obj_auxi));
}

/**
 * Check if need recovery data.
 */
static bool
obj_ec_should_init_recover_cb(struct obj_auxi_args *obj_auxi)
{
	struct obj_ec_fail_info	*fail_info;
	tse_task_t		*task;

	D_ASSERT(obj_auxi->is_ec_obj);

	task = obj_auxi->obj_task;
	if (!obj_auxi->ec_in_recov && task->dt_result == -DER_TGT_RETRY)
		return true;

	fail_info = obj_auxi->reasb_req.orr_fail;
	if (fail_info == NULL)
		return false;

	if (obj_auxi->ec_wait_recov)
		return false;

	if (obj_auxi->result == 0 && !obj_auxi->ec_in_recov && fail_info->efi_nrecx_lists > 0)
		return true;

	return false;
}

static bool
obj_ec_should_recover_data(struct obj_auxi_args *obj_auxi)
{
	if (!obj_auxi->ec_in_recov &&
	    obj_auxi->ec_wait_recov && obj_auxi->obj_task->dt_result == 0)
		return true;

	return false;
}

static void
obj_ec_comp_cb(struct obj_auxi_args *obj_auxi)
{
	tse_task_t	 *task = obj_auxi->obj_task;
	struct dc_object *obj = obj_auxi->obj;
	bool		  data_recov = false;

	D_ASSERT(obj_auxi->is_ec_obj);

	if (obj_is_modification_opc(obj_auxi->opc)) {
		obj_reasb_io_fini(obj_auxi, false);
		return;
	}

	if (obj_ec_should_init_recover_cb(obj_auxi)) {
		int	rc;

		daos_obj_fetch_t *args = dc_task_get_args(task);

		task->dt_result = 0;
		obj_bulk_fini(obj_auxi);
		D_DEBUG(DB_IO, "opc %d init recover task for "DF_OID"\n",
			obj_auxi->opc, DP_OID(obj->cob_md.omd_id));
		rc = obj_ec_recov_cb(task, obj, obj_auxi, args->csum_iov);
		if (rc)
			obj_reasb_io_fini(obj_auxi, false);
		return;
	}

	if (obj_ec_should_recover_data(obj_auxi)) {
		daos_obj_fetch_t *args = dc_task_get_args(task);

		if (!obj_auxi->reasb_req.orr_size_fetch) {
			obj_ec_recov_data(&obj_auxi->reasb_req, args->nr);
			data_recov = true;
		}
	}
	if ((task->dt_result == 0 || task->dt_result == -DER_REC2BIG) &&
	    obj_auxi->opc == DAOS_OBJ_RPC_FETCH && obj_auxi->req_reasbed) {
		daos_obj_fetch_t *args = dc_task_get_args(task);

		obj_ec_update_iod_size(&obj_auxi->reasb_req, args->nr);
		if ((obj_auxi->bulks != NULL && obj_auxi->reasb_req.orr_usgls != NULL) ||
		    data_recov)
			obj_ec_fetch_set_sgl(obj, &obj_auxi->reasb_req,
					     obj_auxi->dkey_hash, args->nr);
	}

	obj_reasb_io_fini(obj_auxi, false);
}

static int
obj_comp_cb(tse_task_t *task, void *data)
{
	struct dc_object	*obj;
	struct obj_auxi_args	*obj_auxi;
	bool			pm_stale = false;
	bool			io_task_reinited = false;
	int			rc;

	obj_auxi = tse_task_stack_pop(task, sizeof(*obj_auxi));
	obj_auxi->io_retry = 0;
	obj_auxi->result = 0;
	obj_auxi->csum_retry = 0;
	obj_auxi->tx_uncertain = 0;
	obj_auxi->nvme_io_err = 0;
	obj = obj_auxi->obj;
	rc = obj_comp_cb_internal(obj_auxi);
	if (rc != 0 || obj_auxi->result) {
		if (task->dt_result == 0)
			task->dt_result = rc ? rc : obj_auxi->result;
	} else if (obj_req_is_ec_cond_fetch(obj_auxi) && task->dt_result == -DER_NONEXIST &&
		   !obj_auxi->cond_fetch_split) {
		/* EC cond_fetch/check_exist task created multiple shard tasks, tse will populate
		 * shard tasks' DER_NONEXIST to parent task, obj_auxi_shards_iterate() zeroed
		 * obj_auxi->result, here should zero task->dt_result.
		 */
		task->dt_result = 0;
	}

	D_DEBUG(DB_IO, "opc %u retry: %d leader %d obj complete callback: %d\n",
		obj_auxi->opc, obj_auxi->io_retry, obj_auxi->to_leader, task->dt_result);

	if (obj->cob_time_fetch_leader != NULL &&
	    obj_auxi->req_tgts.ort_shard_tgts != NULL &&
	    ((!obj_is_modification_opc(obj_auxi->opc) &&
	      task->dt_result == -DER_INPROGRESS) ||
	     (obj_is_modification_opc(obj_auxi->opc) &&
	      task->dt_result == 0))) {
		int	idx;

		idx = obj_auxi->req_tgts.ort_shard_tgts->st_shard /
			obj_get_grp_size(obj);
		obj->cob_time_fetch_leader[idx] = daos_gettime_coarse();
	}

	/* Check if the pool map needs to refresh */
	if (obj_auxi->map_ver_reply > obj_auxi->map_ver_req ||
	    daos_crt_network_error(task->dt_result) ||
	    task->dt_result == -DER_STALE || task->dt_result == -DER_TIMEDOUT ||
	    task->dt_result == -DER_EXCLUDED) {
		D_DEBUG(DB_IO, "map_ver stale (req %d, reply %d). result %d\n",
			obj_auxi->map_ver_req, obj_auxi->map_ver_reply,
			task->dt_result);
		pm_stale = true;
	}

	if (obj_retry_error(task->dt_result)) {
		/* If the RPC sponsor specify shard/group, then means it wants
		 * to fetch data from the specified target. If such shard isn't
		 * ready for read, we should let the caller know that, But there
		 * are some other cases we need to retry the RPC with current
		 * shard, such as -DER_TIMEDOUT or daos_crt_network_error().
		 */
		obj_auxi->io_retry = 1;
		if (obj_auxi->no_retry ||
		    (obj_auxi->spec_shard && (task->dt_result == -DER_INPROGRESS ||
		     task->dt_result == -DER_TX_BUSY || task->dt_result == -DER_EXCLUDED ||
		     task->dt_result == -DER_CSUM)))
			obj_auxi->io_retry = 0;

		if (task->dt_result == -DER_NEED_TX)
			obj_auxi->tx_convert = 1;

		if (task->dt_result == -DER_CSUM || task->dt_result == -DER_TX_UNCERTAIN ||
		    task->dt_result == -DER_NVME_IO) {
			if (!obj_auxi->spec_shard && !obj_auxi->spec_group &&
			    !obj_auxi->no_retry && !obj_auxi->ec_wait_recov) {
				/* Retry fetch on alternative shard */
				if (obj_auxi->opc == DAOS_OBJ_RPC_FETCH) {
					if (task->dt_result == -DER_CSUM)
						obj_auxi->csum_retry = 1;
					else if (task->dt_result == -DER_TX_UNCERTAIN)
						obj_auxi->tx_uncertain = 1;
					else
						obj_auxi->nvme_io_err = 1;
				} else if (task->dt_result != -DER_NVME_IO) {
					/* Don't retry update for CSUM & UNCERTAIN errors */
					obj_auxi->io_retry = 0;
				}
			} else {
				obj_auxi->io_retry = 0;
			}
		}

		if (!obj_auxi->spec_shard && !obj_auxi->spec_group &&
		    task->dt_result == -DER_INPROGRESS)
			obj_auxi->to_leader = 1;
	} else if (!obj_auxi->ec_in_recov &&
		   task->dt_result == -DER_FETCH_AGAIN) {
		/* Remove the original shard fetch tasks and will recreate new shard fetch tasks */
		obj_io_set_new_shard_task(obj_auxi);
		obj_auxi->io_retry = 1;
		pm_stale = 1;
		obj_auxi->ec_wait_recov = 0;
		obj_auxi->ec_in_recov = 0;
		obj_reasb_io_fini(obj_auxi, true);
		D_DEBUG(DB_IO, DF_OID" EC fetch again.\n", DP_OID(obj->cob_md.omd_id));
	} else if (obj_req_is_ec_cond_fetch(obj_auxi) && task->dt_result == -DER_NONEXIST &&
		   !obj_auxi->ec_degrade_fetch && !obj_auxi->cond_fetch_split) {
		daos_obj_fetch_t *args = dc_task_get_args(task);

		if (!(args->extra_flags & DIOF_CHECK_EXISTENCE) &&
		    !obj_ec_req_sent2_all_data_tgts(obj_auxi)) {
			/* retry the original task to check existence */
			args->iods = obj_auxi->reasb_req.orr_uiods;
			args->sgls = obj_auxi->reasb_req.orr_usgls;
			obj_reasb_req_fini(&obj_auxi->reasb_req, obj_auxi->iod_nr);
			obj_auxi->req_reasbed = 0;
			memset(&obj_auxi->rw_args, 0, sizeof(obj_auxi->rw_args));
			args->extra_flags |= DIOF_CHECK_EXISTENCE;
			obj_auxi->io_retry = 1;
		}
	}

	if (!obj_auxi->io_retry && task->dt_result == 0 &&
	    obj_auxi->reasb_req.orr_size_fetch)
		obj_size_fetch_cb(obj, obj_auxi);

	if (task->dt_result == -DER_INPROGRESS &&
	    DAOS_FAIL_CHECK(DAOS_DTX_NO_RETRY))
		obj_auxi->io_retry = 0;

	if (obj_auxi->io_retry) {
		if (obj_auxi->opc == DAOS_OBJ_RPC_FETCH) {
			obj_auxi->reasb_req.orr_iom_tgt_nr = 0;
			obj_io_set_new_shard_task(obj_auxi);
		}

		if (obj_auxi->is_ec_obj && obj_is_enum_opc(obj_auxi->opc)) {
			/**
			 * Since enumeration retry might retry to send multiple
			 * shards, remove the original shard fetch tasks and will
			 * recreate new shard fetch tasks with new parameters.
			 */
			obj_io_set_new_shard_task(obj_auxi);
		}

		if (!obj_auxi->ec_in_recov)
			obj_ec_fail_info_reset(&obj_auxi->reasb_req);
	}

	if (unlikely(task->dt_result == -DER_TX_ID_REUSED || task->dt_result == -DER_EP_OLD)) {
		struct dtx_id	*new_dti;
		struct dtx_id	 old_dti;
		uint64_t	 api_flags;

		D_ASSERT(daos_handle_is_inval(obj_auxi->th));
		D_ASSERT(obj_is_modification_opc(obj_auxi->opc));

		if (task->dt_result == -DER_TX_ID_REUSED && obj_auxi->retry_cnt != 0)
			/* XXX: it is must because miss to set "RESEND" flag, that is bug. */
			D_ASSERTF(0,
				  "Miss 'RESEND' flag (%x) when resend the RPC for task %p: %u\n",
				  obj_auxi->flags, task, obj_auxi->retry_cnt);

		if (obj_auxi->opc == DAOS_OBJ_RPC_UPDATE) {
			daos_obj_rw_t		*api_args = dc_task_get_args(obj_auxi->obj_task);
			struct shard_rw_args	*rw_arg = &obj_auxi->rw_args;

			api_flags = api_args->flags;
			new_dti = &rw_arg->dti;
		} else {
			daos_obj_punch_t	*api_args = dc_task_get_args(obj_auxi->obj_task);
			struct shard_punch_args	*punch_arg = &obj_auxi->p_args;

			api_flags = api_args->flags;
			new_dti = &punch_arg->pa_dti;
		}

		if (task->dt_result == -DER_TX_ID_REUSED || !obj_req_with_cond_flags(api_flags)) {
			daos_dti_copy(&old_dti, new_dti);
			daos_dti_gen(new_dti, false);
			obj_auxi->io_retry = 1;
			obj_auxi->tx_renew = 1;
			D_DEBUG(DB_IO, "refresh DTX ID opc %d (err %d) from "DF_DTI" to "DF_DTI"\n",
				obj_auxi->opc, task->dt_result, DP_DTI(&old_dti), DP_DTI(new_dti));
		}
	}

	if ((!obj_auxi->no_retry || task->dt_result == -DER_FETCH_AGAIN) &&
	     (pm_stale || obj_auxi->io_retry)) {
		rc = obj_retry_cb(task, obj, obj_auxi, pm_stale, &io_task_reinited);
		if (rc) {
			D_ERROR(DF_OID" retry io failed: %d\n", DP_OID(obj->cob_md.omd_id), rc);
			D_ASSERT(obj_auxi->io_retry == 0);
		}
	}

	if (!io_task_reinited) {
		d_list_t *head = &obj_auxi->shard_task_head;

		switch (obj_auxi->opc) {
		case DAOS_OBJ_RPC_SYNC:
			if (task->dt_result != 0) {
				struct daos_obj_sync_args	*sync_args;

				sync_args = dc_task_get_args(task);
				D_ASSERT(sync_args->epochs_p != NULL);

				D_FREE(*sync_args->epochs_p);
				*sync_args->epochs_p = NULL;
				*sync_args->nr = 0;
			}
			break;
		case DAOS_OBJ_RPC_UPDATE:
			D_ASSERT(daos_handle_is_inval(obj_auxi->th));

			obj_rw_csum_destroy(obj, obj_auxi);
			break;
		case DAOS_OBJ_RPC_FETCH: {
			daos_obj_fetch_t	*args = dc_task_get_args(task);

			/** checksums sent and not retrying,
			 * can destroy now
			 */
			obj_rw_csum_destroy(obj, obj_auxi);

			if (daos_handle_is_valid(obj_auxi->th) &&
			    !(args->extra_flags & DIOF_CHECK_EXISTENCE) &&
			    (task->dt_result == 0 || task->dt_result == -DER_NONEXIST))
				/* Cache transactional read if exist or not. */
				dc_tx_attach(obj_auxi->th, obj, DAOS_OBJ_RPC_FETCH, task, 0, false);
			break;
		}
		case DAOS_OBJ_RPC_PUNCH:
		case DAOS_OBJ_RPC_PUNCH_DKEYS:
		case DAOS_OBJ_RPC_PUNCH_AKEYS:
			D_ASSERT(daos_handle_is_inval(obj_auxi->th));
			break;
		case DAOS_OBJ_RPC_QUERY_KEY:
		case DAOS_OBJ_RECX_RPC_ENUMERATE:
		case DAOS_OBJ_AKEY_RPC_ENUMERATE:
		case DAOS_OBJ_DKEY_RPC_ENUMERATE:
		case DAOS_OBJ_RPC_KEY2ANCHOR:
			if (daos_handle_is_valid(obj_auxi->th) &&
			    (task->dt_result == 0 || task->dt_result == -DER_NONEXIST))
				/* Cache transactional read if exist or not. */
				dc_tx_attach(obj_auxi->th, obj, obj_auxi->opc, task, 0, false);
			break;
		case DAOS_OBJ_RPC_ENUMERATE:
			/* XXX: For list dkey recursively, that is mainly used
			 *	by rebuild and object consistency verification,
			 *	currently, we do not have any efficient way to
			 *	trace and spread related read TS to servers.
			 */
			break;
		}

		if (obj_auxi->req_tgts.ort_shard_tgts != obj_auxi->req_tgts.ort_tgts_inline)
			D_FREE(obj_auxi->req_tgts.ort_shard_tgts);

		if (!d_list_empty(head)) {
			if (obj_is_enum_opc(obj_auxi->opc))
				obj_auxi_list_fini(obj_auxi);
			tse_task_list_traverse(head, shard_task_remove, NULL);
			D_ASSERT(d_list_empty(head));
		}

		if (obj_auxi->is_ec_obj)
			obj_ec_comp_cb(obj_auxi);
		else
			obj_reasb_io_fini(obj_auxi, false);
	}

	obj_decref(obj);
	return 0;
}

static void
obj_task_init_common(tse_task_t *task, int opc, uint32_t map_ver,
		     daos_handle_t th, struct obj_auxi_args **auxi,
		     struct dc_object *obj)
{
	struct obj_auxi_args	*obj_auxi;

	obj_auxi = tse_task_stack_push(task, sizeof(*obj_auxi));
	if (obj_is_modification_opc(opc))
		obj_auxi->spec_group = 0;
	obj_auxi->opc = opc;
	obj_auxi->map_ver_req = map_ver;
	obj_auxi->obj_task = task;
	obj_auxi->th = th;
	obj_auxi->obj = obj;
	obj_auxi->dkey_hash = 0;
	obj_auxi->reintegrating = 0;
	obj_auxi->rebuilding = 0;
	shard_task_list_init(obj_auxi);
	obj_auxi->is_ec_obj = obj_is_ec(obj);
	*auxi = obj_auxi;

	D_DEBUG(DB_IO, "client task %p init "DF_OID" opc 0x%x, try %d\n",
		task, DP_OID(obj->cob_md.omd_id), opc, (int)obj_auxi->retry_cnt);
}

/**
 * Init obj_auxi_arg for this object task.
 * Register the completion cb for obj IO request
 */
static int
obj_task_init(tse_task_t *task, int opc, uint32_t map_ver, daos_handle_t th,
	      struct obj_auxi_args **auxi, struct dc_object *obj)
{
	int	rc;

	obj_task_init_common(task, opc, map_ver, th, auxi, obj);
	if ((*auxi)->tx_convert) {
		D_ASSERT((*auxi)->io_retry);
		D_DEBUG(DB_IO, "task %p, convert to dtx opc %d\n", task, opc);
		return 0;
	}
	rc = tse_task_register_comp_cb(task, obj_comp_cb, NULL, 0);
	if (rc) {
		D_ERROR("task %p, register_comp_cb "DF_RC"\n", task, DP_RC(rc));
		tse_task_stack_pop(task, sizeof(struct obj_auxi_args));
	}
	return rc;
}

static int
shard_rw_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
	      struct obj_auxi_args *obj_auxi, uint32_t grp_idx)
{
	struct shard_rw_args	*shard_arg;
	struct obj_reasb_req	*reasb_req;
	struct obj_tgt_oiod	*toiod;

	shard_arg = container_of(shard_auxi, struct shard_rw_args, auxi);

	if (daos_handle_is_inval(obj_auxi->th))
		daos_dti_gen(&shard_arg->dti,
			     obj_auxi->opc == DAOS_OBJ_RPC_FETCH ||
			     srv_io_mode != DIM_DTX_FULL_ENABLED ||
			     daos_obj_is_echo(obj->cob_md.omd_id));
	else
		dc_tx_get_dti(obj_auxi->th, &shard_arg->dti);

	shard_arg->bulks = obj_auxi->bulks;
	if (obj_auxi->req_reasbed) {
		reasb_req = &obj_auxi->reasb_req;
		if (reasb_req->tgt_oiods != NULL) {
			D_ASSERT(obj_auxi->opc == DAOS_OBJ_RPC_FETCH);
			toiod = obj_ec_tgt_oiod_get(
				reasb_req->tgt_oiods, reasb_req->orr_tgt_nr,
				shard_auxi->ec_tgt_idx);
			D_ASSERTF(toiod != NULL, "tgt idx %u\n", shard_auxi->ec_tgt_idx);
			shard_arg->oiods = toiod->oto_oiods;
			shard_arg->offs = toiod->oto_offs;
			D_ASSERT(shard_arg->offs != NULL);
		} else {
			D_ASSERT(obj_auxi->opc == DAOS_OBJ_RPC_UPDATE);
			shard_arg->oiods = reasb_req->orr_oiods;
			shard_arg->offs = NULL;
		}
		if (obj_auxi->is_ec_obj)
			shard_arg->reasb_req = reasb_req;
	} else {
		shard_arg->oiods = NULL;
		shard_arg->offs = NULL;
	}

	/* obj_csum_update/fetch set the dkey_csum/iod_csums to
	 * obj_auxi->rw_args, but it is different than shard task's args
	 * when there are multiple shard tasks (see obj_req_fanout).
	 */
	if (shard_arg != &obj_auxi->rw_args) {
		shard_arg->dkey_csum = obj_auxi->rw_args.dkey_csum;
		shard_arg->iod_csums = obj_auxi->rw_args.iod_csums;
	}

	return 0;
}

bool
obj_csum_dedup_candidate(struct cont_props *props, daos_iod_t *iods,
			 uint32_t iod_nr)
{
	if (!props->dcp_csum_enabled && props->dcp_dedup_enabled) {
		uint32_t	dedup_th = props->dcp_dedup_size;
		int		i;
		bool		candidate = false;

		/**
		 * Checksums are only enabled for dedup purpose.
		 * Verify whether the I/O is a candidate for dedup.
		 * If not, then no need to provide a checksum to the server
		 */

		for (i = 0; i < iod_nr; i++) {
			daos_iod_t	*iod = &iods[i];
			int		 j = 0;

			if (iod->iod_type == DAOS_IOD_SINGLE)
				/** dedup does not support single value yet */
				return false;

			for (j = 0; j < iod->iod_nr; j++) {
				daos_recx_t	*recx = &iod->iod_recxs[j];

				if (recx->rx_nr * iod->iod_size >= dedup_th)
					candidate = true;
			}
		}
		if (!candidate)
			/** not a candidate for dedup, don't compute checksum */
			return false;
	}

	return true;
}

static int
obj_csum_update(struct dc_object *obj, daos_obj_update_t *args, struct obj_auxi_args *obj_auxi)
{
	if (!obj_csum_dedup_candidate(&obj->cob_co->dc_props, args->iods, args->nr))
		return 0;

	return dc_obj_csum_update(obj->cob_co->dc_csummer, obj->cob_co->dc_props,
				  obj->cob_md.omd_id, args->dkey, args->iods, args->sgls, args->nr,
				  obj_auxi->reasb_req.orr_singv_los, &obj_auxi->rw_args.dkey_csum,
				  &obj_auxi->rw_args.iod_csums);
}

static int
obj_csum_fetch(const struct dc_object *obj, daos_obj_fetch_t *args,
	       struct obj_auxi_args *obj_auxi)
{
	return dc_obj_csum_fetch(obj->cob_co->dc_csummer, args->dkey, args->iods, args->sgls,
				 args->nr, obj_auxi->reasb_req.orr_singv_los,
				 &obj_auxi->rw_args.dkey_csum, &obj_auxi->rw_args.iod_csums);
}

static inline char *
retry_errstr(struct obj_auxi_args *obj_auxi)
{
	if (obj_auxi->csum_retry)
		return "csum error";
	else if (obj_auxi->tx_uncertain)
		return "tx uncertainty error";
	else if (obj_auxi->nvme_io_err)
		return "NVMe I/O error";
	else
		return "unknown error";
}

static inline int
retry_errcode(struct obj_auxi_args *obj_auxi, int rc)
{
	if (obj_auxi->csum_retry)
		return -DER_CSUM;
	else if (obj_auxi->tx_uncertain)
		return -DER_TX_UNCERTAIN;
	else if (obj_auxi->nvme_io_err)
		return -DER_NVME_IO;
	else if (!rc)
		return -DER_IO;

	return rc;
}

/* Selects next replica in the object's layout.
 */
static int
obj_retry_next_shard(struct dc_object *obj, struct obj_auxi_args *obj_auxi,
		     unsigned int map_ver, uint32_t *shard)
{
	unsigned int	grp_size;
	unsigned int	start_shard;
	int		rc = 0;

	D_WARN("Retrying replica because of %s.\n", retry_errstr(obj_auxi));

	/* EC retry is done by degraded fetch */
	D_ASSERT(!obj_is_ec(obj));
	rc = obj_dkey2grpmemb(obj, obj_auxi->dkey_hash, map_ver,
			      &start_shard, &grp_size);
	if (rc != 0)
		return rc;

	*shard = (obj_auxi->req_tgts.ort_shard_tgts[0].st_shard + 1) % grp_size + start_shard;
	while (*shard != obj_auxi->initial_shard &&
	       obj_shard_is_invalid(obj, *shard, DAOS_OBJ_RPC_FETCH))
		*shard = (*shard + 1) % grp_size + start_shard;
	if (*shard == obj_auxi->initial_shard) {
		obj_auxi->no_retry = 1;
		return retry_errcode(obj_auxi, 0);
	}

	return rc;
}

static inline bool
need_retry_redundancy(struct obj_auxi_args *obj_auxi)
{
	/* NB: If new failure being added here, then please update failure check in
	 * obj_shard_comp_cb() as well.
	 */
	return (obj_auxi->csum_retry || obj_auxi->tx_uncertain || obj_auxi->nvme_io_err);
}

/* Check if the shard was failed in the previous fetch, so these shards can be skipped */
static inline bool
shard_was_fail(struct obj_auxi_args *obj_auxi, uint32_t shard_idx)
{
	struct obj_auxi_tgt_list *failed_list;
	uint32_t tgt_id;

	if (obj_auxi->force_degraded) {
		D_DEBUG(DB_IO, DF_OID" fail idx %u\n",
			DP_OID(obj_auxi->obj->cob_md.omd_id), shard_idx);
		obj_auxi->force_degraded = 0;
		return true;
	}

	if (obj_auxi->failed_tgt_list == NULL)
		return false;

	failed_list = obj_auxi->failed_tgt_list;
	tgt_id = obj_auxi->obj->cob_shards->do_shards[shard_idx].do_target_id;

	if (tgt_in_failed_tgts_list(tgt_id, failed_list))
		return true;

	return false;
}

static int
obj_ec_valid_shard_get(struct obj_auxi_args *obj_auxi, uint8_t *tgt_bitmap,
		       uint32_t grp_idx, uint32_t *tgt_idx)
{
	struct dc_object	*obj = obj_auxi->obj;
	uint32_t		grp_start = grp_idx * obj_get_grp_size(obj);
	uint32_t		shard_idx = grp_start + *tgt_idx;
	int			rc = 0;

	while (shard_was_fail(obj_auxi, shard_idx) ||
	       obj_shard_is_invalid(obj, shard_idx, DAOS_OBJ_RPC_FETCH)) {
		D_DEBUG(DB_IO, "tried shard %d/%u %d/%d/%d on "DF_OID"\n", shard_idx, *tgt_idx,
			obj->cob_shards->do_shards[shard_idx].do_rebuilding,
			obj->cob_shards->do_shards[shard_idx].do_target_id,
			obj->cob_shards->do_shards[shard_idx].do_shard,
			DP_OID(obj->cob_md.omd_id));
		rc = obj_ec_fail_info_insert(&obj_auxi->reasb_req, (uint16_t)*tgt_idx);
		if (rc)
			break;

		rc = obj_ec_fail_info_parity_get(obj, &obj_auxi->reasb_req, obj_auxi->dkey_hash,
						 tgt_idx, tgt_bitmap);
		if (rc)
			break;
		shard_idx = grp_start + *tgt_idx;
	}

	if (rc) {
		/* Can not find any valid shards anymore, so no need retry, and also to check
		 * if it needs to restore the original failure. */
		obj_auxi->no_retry = 1;
		rc = retry_errcode(obj_auxi, rc);
		D_ERROR(DF_OID" can not get parity shard: "DF_RC"\n",
			DP_OID(obj->cob_md.omd_id), DP_RC(rc));
	}

	return rc;
}

static int
obj_ec_get_parity_or_alldata_shard(struct obj_auxi_args *obj_auxi, unsigned int map_ver,
				   int grp_idx, daos_key_t *dkey, uint32_t *shard_cnt,
				   uint8_t **bitmaps);

static int
obj_ec_fetch_shards_get(struct dc_object *obj, daos_obj_fetch_t *args, unsigned int map_ver,
			struct obj_auxi_args *obj_auxi, uint32_t *shard, uint32_t *shard_cnt)
{
	uint8_t			*tgt_bitmap;
	int			grp_idx;
	uint32_t		grp_start;
	uint32_t		tgt_idx;
	struct daos_oclass_attr	*oca;
	int			rc = 0;
	int			i;

	grp_idx = obj_dkey2grpidx(obj, obj_auxi->dkey_hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	tgt_bitmap = obj_auxi->reasb_req.tgt_bitmap;
	if (obj_req_is_ec_check_exist(obj_auxi)) {
		D_ASSERT(obj_req_is_ec_cond_fetch(obj_auxi));
		D_ASSERT(tgt_bitmap == NULL);
		rc = obj_ec_get_parity_or_alldata_shard(obj_auxi, map_ver, grp_idx, args->dkey,
							shard_cnt, NULL);
		if (rc >= 0) {
			*shard = rc;
			rc = 0;
		}
		return rc;
	}

	oca = obj_get_oca(obj);
	/* Check if it needs to do degraded fetch.*/
	grp_start = grp_idx * obj_get_grp_size(obj);
	tgt_idx = obj_ec_shard_idx(obj, obj_auxi->dkey_hash, 0);
	D_DEBUG(DB_TRACE, DF_OID" grp idx %d shard start %u layout %u\n",
		DP_OID(obj->cob_md.omd_id), grp_idx, tgt_idx, obj->cob_layout_version);
	*shard = tgt_idx + grp_start;
	for (i = 0; i < obj_ec_tgt_nr(oca); i++,
	     tgt_idx = (tgt_idx + 1) % obj_ec_tgt_nr(oca)) {
		struct obj_tgt_oiod	*toiod;
		uint32_t		ec_deg_tgt;

		if (isclr(tgt_bitmap, tgt_idx)) {
			D_DEBUG(DB_IO, "tgt_idx %u clear\n", tgt_idx);
			continue;
		}

		ec_deg_tgt = tgt_idx;
		rc = obj_ec_valid_shard_get(obj_auxi, tgt_bitmap, grp_idx, &ec_deg_tgt);
		if (rc)
			D_GOTO(out, rc);

		/* Normally, no need degraded fetch */
		if (likely(ec_deg_tgt == tgt_idx))
			continue;

		if (obj_auxi->ec_in_recov ||
		    (obj_auxi->reasb_req.orr_singv_only && !obj_auxi->reasb_req.orr_size_fetch)) {
			D_DEBUG(DB_IO, DF_OID" shard %d failed recovery(%d) or singv fetch(%d).\n",
				DP_OID(obj->cob_md.omd_id), grp_start + tgt_idx,
				obj_auxi->ec_in_recov, obj_auxi->reasb_req.orr_singv_only);
			D_GOTO(out, rc = -DER_TGT_RETRY);
		}

		D_DEBUG(DB_IO, DF_OID" shard re-direct %d -> %d for degrade fetch.\n",
			DP_OID(obj->cob_md.omd_id), grp_start + tgt_idx, grp_start + ec_deg_tgt);

		/* Update the tgt map */
		/* Fetch will never from the extending shard */
		D_ASSERT(ec_deg_tgt < obj_ec_tgt_nr(oca));
		D_ASSERT(is_ec_parity_shard(obj_auxi->obj, obj_auxi->dkey_hash, ec_deg_tgt));
		clrbit(tgt_bitmap, tgt_idx);
		toiod = obj_ec_tgt_oiod_get(obj_auxi->reasb_req.tgt_oiods,
					    obj_auxi->reasb_req.orr_tgt_nr, tgt_idx);
		D_ASSERTF(toiod != NULL, "tgt idx %u\n", tgt_idx);

		toiod->oto_tgt_idx = ec_deg_tgt;
		setbit(tgt_bitmap, ec_deg_tgt);

		obj_auxi->reset_param = 1;
		obj_auxi->ec_degrade_fetch = 1;
	}

	/* Then check how many shards needs to fetch */
	*shard_cnt = 0;
	for (i = 0; i < obj_ec_tgt_nr(oca); i++) {
		if (!isclr(tgt_bitmap, i))
			(*shard_cnt)++;
	}

out:
	return rc;
}

static int
obj_replica_fetch_shards_get(struct dc_object *obj, struct obj_auxi_args *obj_auxi,
			     uint32_t map_ver, uint32_t *shard, uint32_t *shard_cnt)
{
	bool	to_leader = obj_auxi->to_leader;
	int	grp_idx;
	int	rc;

	D_ASSERT(!obj_is_ec(obj));
	grp_idx = obj_dkey2grpidx(obj, obj_auxi->dkey_hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	if (!to_leader && obj->cob_time_fetch_leader != NULL &&
	    obj->cob_time_fetch_leader[grp_idx] != 0 &&
	    OBJ_FETCH_LEADER_INTERVAL >=
	    daos_gettime_coarse() - obj->cob_time_fetch_leader[grp_idx])
		to_leader = true;

	if (DAOS_FAIL_CHECK(DAOS_DTX_RESYNC_DELAY))
		rc = obj->cob_shards_nr - 1;
	else if (to_leader)
		rc = obj_replica_leader_select(obj, grp_idx, obj_auxi->dkey_hash, map_ver);
	else
		rc = obj_replica_grp_fetch_valid_shard_get(obj, grp_idx, map_ver,
							   obj_auxi->failed_tgt_list);

	if (rc < 0)
		return rc;

	*shard_cnt = 1;
	*shard = rc;
	return 0;
}

static int
obj_fetch_shards_get(struct dc_object *obj, daos_obj_fetch_t *args, unsigned int map_ver,
		     struct obj_auxi_args *obj_auxi, uint32_t *shard, uint32_t *shard_cnt)
{
	int rc = 0;

	/* Choose the shards to forward the fetch request */
	if (obj_auxi->spec_shard) {  /* special read */
		int grp_idx;

		D_ASSERT(!obj_auxi->to_leader);

		if (args->extra_arg != NULL) {
			*shard = *(int *)args->extra_arg;
		} else if (obj_auxi->io_retry) {
			*shard = obj_auxi->specified_shard;
		} else {
			*shard = daos_fail_value_get();
			obj_auxi->specified_shard = *shard;
		}
		*shard_cnt = 1;

		/* Check if the special shard match the dkey */
		grp_idx = obj_dkey2grpidx(obj, obj_auxi->dkey_hash, map_ver);
		if (grp_idx < 0)
			D_GOTO(out, rc = grp_idx);

		if (*shard < grp_idx * obj->cob_grp_size ||
		    *shard >= (grp_idx + 1) * obj->cob_grp_size) {
			rc = -DER_INVAL;
			D_ERROR("Fetch from invalid shard, grp size %u, shards_nr %u, "
				"grp idx %u, given shard %u, dkey hash %lu: "DF_RC"\n",
				obj->cob_grp_size, obj->cob_shards_nr, grp_idx,
				*shard, obj_auxi->dkey_hash, DP_RC(rc));
			D_GOTO(out, rc);
		}
	} else if (obj_is_ec(obj)) {
		rc = obj_ec_fetch_shards_get(obj, args, map_ver, obj_auxi, shard, shard_cnt);
		if (rc)
			D_GOTO(out, rc);
	} else if (need_retry_redundancy(obj_auxi)) {
		*shard_cnt = 1;
		rc = obj_retry_next_shard(obj, obj_auxi, map_ver, shard);
		if (rc)
			D_GOTO(out, rc);
	} else {
		rc = obj_replica_fetch_shards_get(obj, obj_auxi, map_ver, shard, shard_cnt);
		if (rc < 0)
			D_GOTO(out, rc);
	}
out:
	D_DEBUG(DB_IO, DF_OID" shard/shard_cnt %u/%u special %s leader %s\n",
		DP_OID(obj->cob_md.omd_id), *shard, *shard_cnt, obj_auxi->spec_shard ? "yes" : "no",
		obj_auxi->to_leader ? "yes" : "no");
	return rc;
}

/* pre-process for cond_fetch -
 * for multiple-akeys case, split obj task to multiple sub-tasks each for one akey. For this
 * case return 1 to indicate wait sub-tasks' completion.
 */
static int
obj_cond_fetch_prep(tse_task_t *task, struct obj_auxi_args *obj_auxi)
{
	daos_obj_fetch_t	*args = dc_task_get_args(task);
	d_list_t		*task_list = &obj_auxi->shard_task_head;
	tse_task_t		*sub_task;
	d_sg_list_t		*sgl;
	bool			 per_akey = args->flags & DAOS_COND_PER_AKEY;
	uint64_t		 fetch_flags;
	uint32_t		 i;
	int			 rc = 0;

	if (args->nr <= 1 || (args->flags & (DAOS_COND_AKEY_FETCH | DAOS_COND_PER_AKEY)) == 0)
		return rc;

	/* If cond_fetch include multiple akeys, splits the obj task to multiple sub-tasks, one for
	 * each akey. Because -
	 * 1. for each akey's cond_fetch if any shard returns 0 (exist) then the akey is exist.
	 * 2. for multi-akeys' cond_fetch, should return non-exist if any akey non-exist.
	 * Now one fetch request only with one return code. So creates one sub-task for each akey.
	 */
	D_ASSERT(d_list_empty(task_list));
	D_ASSERT(obj_auxi->cond_fetch_split == 0);
	for (i = 0; i < args->nr; i++) {
		fetch_flags = per_akey ? args->iods[i].iod_flags : args->flags;
		sgl = args->sgls != NULL ? &args->sgls[i] : NULL;
		rc = dc_obj_fetch_task_create(args->oh, obj_auxi->th, fetch_flags, args->dkey, 1,
					      0, &args->iods[i], sgl, NULL, NULL, NULL,
					      NULL, tse_task2sched(task), &sub_task);
		if (rc) {
			D_ERROR("task %p "DF_OID" dc_obj_fetch_task_create failed, "DF_RC"\n",
				task, DP_OID(obj_auxi->obj->cob_md.omd_id), DP_RC(rc));
			goto out;
		}

		tse_task_addref(sub_task);
		tse_task_list_add(sub_task, task_list);

		rc = dc_task_depend(task, 1, &sub_task);
		if (rc) {
			D_ERROR("task %p "DF_OID" dc_task_depend failed "DF_RC"\n",
				task, DP_OID(obj_auxi->obj->cob_md.omd_id), DP_RC(rc));
			goto out;
		}

		D_DEBUG(DB_IO, DF_OID" created sub_task %p for obj task %p\n",
			DP_OID(obj_auxi->obj->cob_md.omd_id), sub_task, task);
	}

out:
	if (rc == 0) {
		D_DEBUG(DB_IO, "scheduling %d sub-tasks for cond_fetch IO task %p.\n",
			args->nr, task);
		obj_auxi->no_retry = 1;
		obj_auxi->cond_fetch_split = 1;
		tse_task_list_sched(task_list, false);
		rc = 1;
	} else {
		if (!d_list_empty(task_list))
			tse_task_list_traverse(task_list, shard_task_abort, &rc);
		task->dt_result = rc;
	}
	return rc;
}

int
dc_obj_fetch_task(tse_task_t *task)
{
	daos_obj_fetch_t	*args = dc_task_get_args(task);
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	uint8_t                 *tgt_bitmap = NIL_BITMAP;
	unsigned int		map_ver = 0;
	struct dtx_epoch	epoch;
	uint32_t		shard = 0;
	uint32_t		shard_cnt = 0;
	int			rc;

	rc = obj_req_valid(task, args, DAOS_OBJ_RPC_FETCH, &epoch, &map_ver,
			   &obj);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = obj_task_init(task, DAOS_OBJ_RPC_FETCH, map_ver, args->th,
			   &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	if (obj_req_with_cond_flags(args->flags)) {
		rc = obj_cond_fetch_prep(task, obj_auxi);
		D_ASSERT(rc <= 1);
		if (rc < 0)
			D_GOTO(out_task, rc);
		if (rc == 1)
			return 0;
	}

	if ((args->extra_flags & DIOF_EC_RECOV) != 0) {
		obj_auxi->ec_in_recov = 1;
		obj_auxi->reasb_req.orr_fail = args->extra_arg;
		obj_auxi->reasb_req.orr_recov = 1;
		if ((args->extra_flags & DIOF_EC_RECOV_SNAP) != 0)
			obj_auxi->reasb_req.orr_recov_snap = 1;
	}
	if (args->extra_flags & DIOF_FOR_MIGRATION) {
		obj_auxi->flags |= ORF_FOR_MIGRATION;
		obj_auxi->no_retry = 1;
	}
	if (args->extra_flags & DIOF_FOR_EC_AGG)
		obj_auxi->flags |= ORF_FOR_EC_AGG;

	if (args->extra_flags & DIOF_EC_RECOV_FROM_PARITY)
		obj_auxi->flags |= ORF_EC_RECOV_FROM_PARITY;

	if (args->extra_flags & DIOF_FOR_FORCE_DEGRADE ||
	    DAOS_FAIL_CHECK(DAOS_OBJ_FORCE_DEGRADE))
		obj_auxi->force_degraded = 1;

	if (args->extra_flags & DIOF_CHECK_EXISTENCE)
		obj_auxi->flags |= ORF_CHECK_EXISTENCE;

	if (args->extra_arg == NULL &&
	    DAOS_FAIL_CHECK(DAOS_OBJ_SPECIAL_SHARD))
		args->extra_flags |= DIOF_TO_SPEC_SHARD;

	if (!obj_auxi->io_retry) {
		obj_auxi->spec_shard = (args->extra_flags & DIOF_TO_SPEC_SHARD) != 0;
		obj_auxi->spec_group = (args->extra_flags & DIOF_TO_SPEC_GROUP) != 0;
		obj_auxi->to_leader = (args->extra_flags & DIOF_TO_LEADER) != 0;
	}

	obj_auxi->dkey_hash = obj_dkey2hash(obj->cob_md.omd_id, args->dkey);
	obj_auxi->iod_nr = args->nr;

	if (obj_auxi->ec_wait_recov)
		goto out_task;

	if (obj_is_ec(obj)) {
		rc = obj_rw_req_reassemb(obj, args, &epoch, obj_auxi);
		if (rc != 0) {
			D_ERROR(DF_OID" obj_req_reassemb failed %d.\n",
				DP_OID(obj->cob_md.omd_id), rc);
			D_GOTO(out_task, rc);
		}
		tgt_bitmap = obj_auxi->reasb_req.tgt_bitmap;
	} else {
		if (args->extra_flags & DIOF_CHECK_EXISTENCE) {
			/*
			 * XXX: Be as tempoary solution, fetch from leader fisrtly, that
			 * always workable for replicated object and will be changed when
			 * support conditional fetch EC object. DAOS-10204.
			 */
			obj_auxi->to_leader = 1;
			tgt_bitmap = NIL_BITMAP;
		}
	}

	rc = obj_fetch_shards_get(obj, args, map_ver, obj_auxi, &shard, &shard_cnt);
	if (rc)
		D_GOTO(out_task, rc);

	/* Map the shard to forward targets */
	rc = obj_shards_2_fwtgts(obj, map_ver, tgt_bitmap, shard, shard_cnt, 1,
				 OBJ_TGT_FLAG_CLI_DISPATCH, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = obj_csum_fetch(obj, args, obj_auxi);
	if (rc != 0) {
		D_ERROR("obj_csum_fetch error: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_task, rc);
	}

	if (!obj_auxi->io_retry && !obj_auxi->is_ec_obj)
		obj_auxi->initial_shard = obj_auxi->req_tgts.ort_shard_tgts[0].st_shard;

	rc = obj_rw_bulk_prep(obj, args->iods, args->sgls, args->nr,
			      false, false, task, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = obj_req_fanout(obj, obj_auxi, map_ver, &epoch,
			    shard_rw_prep, dc_obj_shard_rw, task);
	return rc;

out_task:
	obj_task_complete(task, rc);
	return rc;
}

static int
obj_update_shards_get(struct dc_object *obj, daos_obj_fetch_t *args, unsigned int map_ver,
		      struct obj_auxi_args *obj_auxi, uint32_t *shard, uint32_t *shard_cnt)
{
	uint8_t		*tgt_bitmap;
	uint32_t	failure_cnt = 0;
	int		grp_idx;
	uint32_t	grp_start;
	uint32_t	shard_nr = 0;
	int		i;
	int		rc = 0;

	if (!obj_is_ec(obj))
		return obj_dkey2grpmemb(obj, obj_auxi->dkey_hash, map_ver, shard, shard_cnt);

	grp_idx = obj_dkey2grpidx(obj, obj_auxi->dkey_hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	grp_start = grp_idx * obj_get_grp_size(obj);
	tgt_bitmap = obj_auxi->reasb_req.tgt_bitmap;
	D_RWLOCK_RDLOCK(&obj->cob_lock);
	for (i = 0; i < obj_get_grp_size(obj); i++) {
		struct dc_obj_shard	*obj_shard;
		unsigned int		shard_id;
		int			shard_idx;

		shard_idx = grp_start + i;
		D_ASSERTF(shard_idx < obj->cob_shards_nr, "%d >= %u\n",
			  shard_idx, obj->cob_shards_nr);

		obj_shard = &obj->cob_shards->do_shards[shard_idx];
		if (obj_shard->do_target_id == -1 || obj_shard->do_shard == -1 ||
		    unlikely(DAOS_FAIL_CHECK(DAOS_FAIL_SHARD_NONEXIST))) {
			/* check if the shard is from extending shard */
			if (shard_idx % obj_get_grp_size(obj) >= obj_ec_tgt_nr(obj_get_oca(obj))) {
				D_DEBUG(DB_IO, DF_OID" skip extending shard %d\n",
					DP_OID(obj->cob_md.omd_id), shard_idx);
				continue;
			}

			if (++failure_cnt > obj_ec_parity_tgt_nr(obj_get_oca(obj))) {
				D_ERROR(DF_OID" failures %u is more than parity cnt.\n",
					DP_OID(obj->cob_md.omd_id), failure_cnt);
				D_RWLOCK_UNLOCK(&obj->cob_lock);
				D_GOTO(out, rc = -DER_IO);
			}

			D_DEBUG(DB_IO, DF_OID" skip shard %d\n", DP_OID(obj->cob_md.omd_id),
				shard_idx);
			if (obj_shard->do_shard != -1)
				clrbit(tgt_bitmap, obj_shard->do_shard -
						   grp_idx * obj_ec_tgt_nr(&obj->cob_oca));
			continue;
		}

		/* NB: tgt_bitmap does not include extending shard, so we have to use real
		 * shard id(without extending shards) of each obj_shard to update and
		 * check tgt_bitmap.
		 */
		D_ASSERTF(obj_shard->do_shard >= grp_idx * obj_ec_tgt_nr(&obj->cob_oca),
			  DF_OID" do_shard %u grp_idx %u tgt_nr %u\n", DP_OID(obj->cob_md.omd_id),
			  obj_shard->do_shard, grp_idx, obj_ec_tgt_nr(&obj->cob_oca));
		shard_id = obj_shard->do_shard - grp_idx * obj_ec_tgt_nr(&obj->cob_oca);

		/* Then check if the shard is in this update,
		 */
		if (isclr(tgt_bitmap, shard_id)) {
			D_DEBUG(DB_TRACE, "do shard %u clr i %d\n", shard_id, i);
			continue;
		}
		shard_nr++;
	}
	D_RWLOCK_UNLOCK(&obj->cob_lock);
	*shard = grp_start;
	*shard_cnt = shard_nr;
out:
	return rc;
}

static int
dc_obj_update(tse_task_t *task, struct dtx_epoch *epoch, uint32_t map_ver,
	      daos_obj_update_t *args, struct dc_object *obj)
{
	struct obj_auxi_args	*obj_auxi;
	uint8_t			*tgt_bitmap = NIL_BITMAP;
	uint32_t		shard;
	uint32_t		shard_cnt;
	int			rc;

	rc = obj_task_init(task, DAOS_OBJ_RPC_UPDATE, map_ver, args->th,
			   &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	rc = obj_update_sgls_dup(obj_auxi, args);
	if (rc) {
		D_ERROR(DF_OID" obj_update_sgls_dup failed %d.\n", DP_OID(obj->cob_md.omd_id), rc);
		D_GOTO(out_task, rc);
	}

	if (obj_auxi->tx_convert) {
		if (obj_auxi->is_ec_obj && obj_auxi->req_reasbed) {
			args->iods = obj_auxi->reasb_req.orr_uiods;
			args->sgls = obj_auxi->reasb_req.orr_usgls;
		}

		obj_auxi->tx_convert = 0;
		return dc_tx_convert(obj, DAOS_OBJ_RPC_UPDATE, task);
	}

	obj_auxi->dkey_hash = obj_dkey2hash(obj->cob_md.omd_id, args->dkey);
	obj_auxi->iod_nr = args->nr;
	if (obj_is_ec(obj)) {
		rc = obj_rw_req_reassemb(obj, args, NULL, obj_auxi);
		if (rc) {
			D_ERROR(DF_OID" obj_req_reassemb failed %d.\n",
				DP_OID(obj->cob_md.omd_id), rc);
			D_GOTO(out_task, rc);
		}
		tgt_bitmap = obj_auxi->reasb_req.tgt_bitmap;
	}

	/* The data might be needed to forwarded to other targets (or not forwarded anymore)
	 * after pool map refreshed, especially during online extending or reintegration,
	 * which needs to be binded or unbinded.
	 * So let's free the existent bulk, and recreate the bulk later.
	 */
	if (obj_auxi->io_retry && obj_auxi->bulks != NULL) {
		obj_bulk_fini(obj_auxi);
		obj_io_set_new_shard_task(obj_auxi);
	}

	rc = obj_update_shards_get(obj, args, map_ver, obj_auxi, &shard, &shard_cnt);
	if (rc != 0) {
		D_ERROR(DF_OID" get update shards failure %d\n", DP_OID(obj->cob_md.omd_id), rc);
		D_GOTO(out_task, rc);
	}

	if (args->flags & DAOS_COND_MASK)
		obj_auxi->cond_modify = 1;

	rc = obj_shards_2_fwtgts(obj, map_ver, tgt_bitmap, shard, shard_cnt, 1,
				 OBJ_TGT_FLAG_FW_LEADER_INFO, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);

	if (daos_fail_check(DAOS_FAIL_TX_CONVERT))
		D_GOTO(out_task, rc = -DER_NEED_TX);

	/* For update, based on re-assembled sgl for csum calculate (to match with iod).
	 * Then if with single data target use original user sgl in IO request to avoid
	 * pack the same data multiple times.
	 */
	if (obj_auxi->is_ec_obj && obj_auxi->req_reasbed)
		args->sgls = obj_auxi->reasb_req.orr_sgls;
	rc = obj_csum_update(obj, args, obj_auxi);
	if (rc) {
		D_ERROR("obj_csum_update error: "DF_RC"\n", DP_RC(rc));
		goto out_task;
	}
	if (obj_auxi->is_ec_obj && obj_auxi->req_reasbed && obj_auxi->reasb_req.orr_single_tgt)
		args->sgls = obj_auxi->reasb_req.orr_usgls;

	if (DAOS_FAIL_CHECK(DAOS_DTX_COMMIT_SYNC))
		obj_auxi->flags |= ORF_DTX_SYNC;

	D_DEBUG(DB_IO, "update "DF_OID" dkey_hash "DF_U64"\n",
		DP_OID(obj->cob_md.omd_id), obj_auxi->dkey_hash);

	rc = obj_rw_bulk_prep(obj, args->iods, args->sgls, args->nr, true,
			      obj_auxi->req_tgts.ort_srv_disp, task, obj_auxi);
	if (rc != 0)
		goto out_task;

	rc = obj_req_fanout(obj, obj_auxi, map_ver, epoch,
			    shard_rw_prep, dc_obj_shard_rw, task);
	return rc;

out_task:
	obj_task_complete(task, rc);
	return rc;
}

int
dc_obj_update_task(tse_task_t *task)
{
	daos_obj_update_t	*args = dc_task_get_args(task);
	struct dc_object	*obj = NULL;
	struct dtx_epoch	 epoch = {0};
	unsigned int		 map_ver = 0;
	int			 rc;

	rc = obj_req_valid(task, args, DAOS_OBJ_RPC_UPDATE, &epoch, &map_ver,
			   &obj);
	if (rc != 0)
		goto comp;

	if (daos_handle_is_valid(args->th))
		/* add the operation to DTX and complete immediately */
		return dc_tx_attach(args->th, obj, DAOS_OBJ_RPC_UPDATE, task, 0, true);

	/* submit the update */
	return dc_obj_update(task, &epoch, map_ver, args, obj);

comp:
	obj_task_complete(task, rc);
	return rc;
}

static int
daos_shard_tgt_lookup(struct daos_shard_tgt *tgts, int tgt_nr, uint32_t shard)
{
	int i;

	for (i = 0; i < tgt_nr; i++) {
		if (tgts[i].st_shard == shard)
			return i;
	}

	return -1;
}

/*
 * Check if any sub anchor enumeration reach EOF, then set them to IGNORE_RANK, so
 * to avoid send more RPC.
 */
static int
shard_anchors_eof_check(struct obj_auxi_args *obj_auxi, struct shard_anchors *sub_anchors)
{
	struct daos_shard_tgt	*shard_tgts = obj_auxi->req_tgts.ort_shard_tgts;
	uint32_t		tgt_nr = obj_auxi->req_tgts.ort_grp_nr *
					 obj_auxi->req_tgts.ort_grp_size;
	int			shards_nr = sub_anchors->sa_anchors_nr;
	int			i;

	/*
	 * To avoid complexity of post sgl merge(see obj_shard_list_obj_cb()) and following
	 * rebuild process, let's skip shard eof check for object enumeration, i.e. always
	 * enumerate even for eof shard.
	 */
	if (obj_auxi->opc == DAOS_OBJ_RPC_ENUMERATE) {
		if (tgt_nr != shards_nr) {
			D_ERROR(DF_OID" shards_nr %u tgt_nr %u: "DF_RC"\n",
				DP_OID(obj_auxi->obj->cob_md.omd_id), shards_nr, tgt_nr,
				DP_RC(-DER_IO));
			return -DER_IO;
		}
		return 0;
	}

	/* Check if any shards reach their EOF */
	D_ASSERT(sub_anchors != NULL);
	for (i = 0; i < shards_nr; i++) {
		struct shard_sub_anchor *sub_anchor;

		sub_anchor = &sub_anchors->sa_anchors[i];
		/*
		 * If the shard from sub_anchors does not exist in forward tgts(obj_auxi->req_tgts)
		 * anymore, then it means the shard become invalid, i.e. we do not need enumerate
		 * from this shard anymore, so set it to eof.
		 */
		if (daos_shard_tgt_lookup(shard_tgts, tgt_nr, sub_anchor->ssa_shard) == -1) {
			D_DEBUG(DB_IO, DF_OID" set anchor eof %d/%d/%u\n",
				DP_OID(obj_auxi->obj->cob_md.omd_id), i, shards_nr,
				sub_anchor->ssa_shard);
			daos_anchor_set_eof(&sub_anchor->ssa_anchor);
			continue;
		}

		if (daos_anchor_is_eof(&sub_anchor->ssa_anchor)) {
			int j;

			if (sub_anchor->ssa_sgl.sg_iovs)
				d_sgl_fini(&sub_anchor->ssa_sgl, true);
			if (sub_anchor->ssa_recxs != NULL)
				D_FREE(sub_anchor->ssa_recxs);
			if (sub_anchor->ssa_kds)
				D_FREE(sub_anchor->ssa_kds);
			D_DEBUG(DB_IO, DF_OID" anchor eof %d/%d/%u\n",
				DP_OID(obj_auxi->obj->cob_md.omd_id), i, shards_nr,
				sub_anchor->ssa_shard);
			/* Set the target to IGNORE to skip the shard RPC */
			for (j = 0; j < tgt_nr; j++) {
				if (shard_tgts[j].st_shard == sub_anchor->ssa_shard) {
					shard_tgts[j].st_rank = DAOS_TGT_IGNORE;
					break;
				}
			}
			continue;
		}
	}

	if (tgt_nr <= shards_nr)
		return 0;

	/* More shards are added during enumeration, though to keep the anchor, let's
	 * ignore those new added shards */
	D_DEBUG(DB_IO, DF_OID" shards %u tgt_nr %u ignore tgts not in sub_anchors\n",
		DP_OID(obj_auxi->obj->cob_md.omd_id), shards_nr, tgt_nr);

	for (i = 0; i < tgt_nr; i++) {
		struct daos_shard_tgt *tgt = &shard_tgts[i];

		if (shard_anchor_lookup(sub_anchors, tgt->st_shard) == -1)
			shard_tgts[i].st_rank = DAOS_TGT_IGNORE;
	}

	return 0;
}

static int
shard_anchors_check_alloc_bufs(struct obj_auxi_args *obj_auxi, struct shard_anchors *sub_anchors,
			       int nr, daos_size_t buf_size)
{
	struct obj_req_tgts	*req_tgts = &obj_auxi->req_tgts;
	int			shards_nr = sub_anchors->sa_anchors_nr;
	struct shard_sub_anchor *sub_anchor;
	daos_obj_list_t		*obj_args;
	int			rc = 0;
	int			i;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	for (i = 0; i < shards_nr; i++) {
		sub_anchor = &sub_anchors->sa_anchors[i];
		if (sub_anchor->ssa_shard == (uint32_t)(-1))
			sub_anchor->ssa_shard = req_tgts->ort_shard_tgts[i].st_shard;

		if (daos_anchor_is_eof(&sub_anchor->ssa_anchor))
			continue;

		if (obj_args->sgl != NULL) {
			if (sub_anchor->ssa_sgl.sg_iovs &&
			    sub_anchor->ssa_sgl.sg_iovs->iov_buf_len != buf_size)
				d_sgl_fini(&sub_anchor->ssa_sgl, true);

			if (sub_anchor->ssa_sgl.sg_iovs == NULL) {
				d_sg_list_t *sgl;

				rc = d_sgl_init(&sub_anchor->ssa_sgl, 1);
				if (rc)
					D_GOTO(out, rc);

				sgl = &sub_anchor->ssa_sgl;
				rc = daos_iov_alloc(&sgl->sg_iovs[0], buf_size, false);
				if (rc)
					D_GOTO(out, rc);
			}
		}

		if (obj_args->kds != NULL) {
			if (sub_anchor->ssa_kds != NULL && sub_anchors->sa_nr != nr)
				D_FREE(sub_anchor->ssa_kds);

			if (sub_anchor->ssa_kds == NULL) {
				D_ALLOC_ARRAY(sub_anchor->ssa_kds, nr);
				if (sub_anchor->ssa_kds == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}
		}

		if (obj_args->recxs != NULL) {
			if (sub_anchor->ssa_recxs != NULL && sub_anchors->sa_nr == nr)
				D_FREE(sub_anchor->ssa_recxs);

			if (sub_anchor->ssa_recxs == NULL) {
				D_ALLOC_ARRAY(sub_anchor->ssa_recxs, nr);
				if (sub_anchor->ssa_recxs == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}
		}
	}

	sub_anchors->sa_nr = nr;
out:
	return rc;
}

struct shard_anchors *
shard_anchors_alloc(struct obj_auxi_args *obj_auxi, int shards_nr, int nr,
		    daos_size_t buf_size)
{
	struct shard_anchors	*sub_anchors;
	int			rc;
	int			i;

	D_ALLOC(sub_anchors, sizeof(*sub_anchors) +
			     sizeof(struct shard_sub_anchor) * shards_nr);
	if (sub_anchors == NULL)
		return NULL;

	for (i = 0; i < shards_nr; i++)
		sub_anchors->sa_anchors[i].ssa_shard = -1;

	D_INIT_LIST_HEAD(&sub_anchors->sa_merged_list);
	sub_anchors->sa_anchors_nr = shards_nr;
	rc = shard_anchors_check_alloc_bufs(obj_auxi, sub_anchors, nr, buf_size);
	if (rc)
		D_GOTO(out, rc);

	if (obj_auxi->opc == DAOS_OBJ_RPC_ENUMERATE) {
		for (i = 0; i < shards_nr; i++) {
			D_ALLOC_PTR(sub_anchors->sa_anchors[i].ssa_akey_anchor);
			D_ALLOC_PTR(sub_anchors->sa_anchors[i].ssa_recx_anchor);
			if (sub_anchors->sa_anchors[i].ssa_akey_anchor == NULL ||
			    sub_anchors->sa_anchors[i].ssa_recx_anchor == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}

out:
	if (rc) {
		shard_anchors_free(sub_anchors, obj_auxi->opc);
		sub_anchors = NULL;
	}

	return sub_anchors;
}

/**
 * For migrate enumeration(OBJ_RPC_ENUMERATE), all 3 sub anchors(ssa_anchors, ssa_recx_anchors,
 * ssa_akey_anchors) will be attached to obj_args->dkey_anchors, i.e. anchors and akey_anchors
 * are "useless" here.
 * Though for normal enumeration (no sub anchors), anchors/dkey_anchors/akey_anchors
 * will all be used.
 */
static int
sub_anchors_prep(struct obj_auxi_args *obj_auxi, int shards_nr)
{
	daos_obj_list_t		*obj_args;
	struct shard_anchors	*sub_anchors;
	int			nr = 0;
	daos_size_t		buf_size;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	if (obj_args->nr != NULL)
		nr = *obj_args->nr;
	buf_size = daos_sgl_buf_size(obj_args->sgl);
	if (obj_auxi->opc == DAOS_OBJ_RPC_ENUMERATE) {
		D_ASSERTF(nr >= shards_nr, "nr %d shards_nr %d\n", nr, shards_nr);
		buf_size /= shards_nr;
		nr /= shards_nr;
	}

	obj_auxi->sub_anchors = 1;
	sub_anchors = obj_get_sub_anchors(obj_args, obj_auxi->opc);
	if (sub_anchors != NULL) {
		int rc;

		rc = shard_anchors_eof_check(obj_auxi, sub_anchors);
		if (rc)
			return rc;

		return shard_anchors_check_alloc_bufs(obj_auxi, sub_anchors, nr, buf_size);
	}

	sub_anchors = shard_anchors_alloc(obj_auxi, shards_nr, nr, buf_size);
	if (sub_anchors == NULL)
		return -DER_NOMEM;

	obj_set_sub_anchors(obj_args, obj_auxi->opc, sub_anchors);
	return 0;
}

/* prepare the object enumeration for each shards */
static int
obj_shard_list_prep(struct obj_auxi_args *obj_auxi, struct dc_object *obj,
		    struct shard_list_args *shard_arg)
{
	daos_obj_list_t		*obj_args;
	struct shard_anchors	*sub_anchors;
	int			idx;
	int			rc = 0;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	D_ASSERT(obj_is_ec(obj));

	sub_anchors = obj_get_sub_anchors(obj_args, obj_auxi->opc);
	D_ASSERT(sub_anchors != NULL);
	shard_arg->la_nr = sub_anchors->sa_nr;
	idx = shard_anchor_lookup(sub_anchors, shard_arg->la_auxi.shard);
	D_ASSERT(idx != -1);
	if (shard_arg->la_sgl == NULL && obj_args->sgl != NULL)
		shard_arg->la_sgl = &sub_anchors->sa_anchors[idx].ssa_sgl;
	if (shard_arg->la_kds == NULL && obj_args->kds != NULL)
		shard_arg->la_kds = sub_anchors->sa_anchors[idx].ssa_kds;
	if (shard_arg->la_recxs == NULL && obj_args->recxs)
		shard_arg->la_recxs = sub_anchors->sa_anchors[idx].ssa_recxs;

	D_DEBUG(DB_TRACE, DF_OID" shard %d idx %d kds %p sgl %p\n",
		DP_OID(obj->cob_md.omd_id), shard_arg->la_auxi.shard, idx,
		shard_arg->la_kds, shard_arg->la_sgl);
	if (obj_args->anchor) {
		if (shard_arg->la_anchor == NULL) {
			D_ALLOC_PTR(shard_arg->la_anchor);
			if (shard_arg->la_anchor == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		if (sub_anchors->sa_anchors[idx].ssa_recx_anchor)
			memcpy(shard_arg->la_anchor,
			       sub_anchors->sa_anchors[idx].ssa_recx_anchor, sizeof(daos_anchor_t));
		else
			memcpy(shard_arg->la_anchor, &sub_anchors->sa_anchors[idx].ssa_anchor,
			       sizeof(daos_anchor_t));
	}

	if (obj_args->dkey_anchor) {
		if (shard_arg->la_dkey_anchor == NULL) {
			D_ALLOC_PTR(shard_arg->la_dkey_anchor);
			if (shard_arg->la_dkey_anchor == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
		memcpy(shard_arg->la_dkey_anchor,
		       &sub_anchors->sa_anchors[idx].ssa_anchor, sizeof(daos_anchor_t));
		shard_arg->la_dkey_anchor->da_flags = obj_args->dkey_anchor->da_flags;
	}

	if (obj_args->akey_anchor) {
		if (shard_arg->la_akey_anchor == NULL) {
			D_ALLOC_PTR(shard_arg->la_akey_anchor);
			if (shard_arg->la_akey_anchor == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
		if (sub_anchors->sa_anchors[idx].ssa_akey_anchor)
			memcpy(shard_arg->la_akey_anchor,
			       sub_anchors->sa_anchors[idx].ssa_akey_anchor, sizeof(daos_anchor_t));
		else
			memcpy(shard_arg->la_akey_anchor,
			       &sub_anchors->sa_anchors[idx].ssa_anchor, sizeof(daos_anchor_t));
		shard_arg->la_akey_anchor->da_flags = obj_args->akey_anchor->da_flags;
	}
out:
	return rc;
}

static int
shard_list_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
		struct obj_auxi_args *obj_auxi, uint32_t grp_idx)
{
	daos_obj_list_t		*obj_args;
	struct shard_list_args	*shard_arg;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	if (obj_auxi->sub_anchors) {
		int	rc;

		D_ASSERT(obj_auxi->is_ec_obj);
		rc = obj_shard_list_prep(obj_auxi, obj, shard_arg);
		if (rc) {
			D_ERROR(DF_OID" shard list %d prep: %d\n",
				DP_OID(obj->cob_md.omd_id), grp_idx, rc);
			return rc;
		}
	} else {
		shard_arg->la_nr = *obj_args->nr;
		shard_arg->la_recxs = obj_args->recxs;
		shard_arg->la_anchor = obj_args->anchor;
		shard_arg->la_akey_anchor = obj_args->akey_anchor;
		shard_arg->la_dkey_anchor = obj_args->dkey_anchor;
		shard_arg->la_kds = obj_args->kds;
		shard_arg->la_sgl = obj_args->sgl;
	}

	return 0;
}

/* Get random parity from one group for the EC object */
static int
obj_ec_random_parity_get(struct dc_object *obj, uint64_t dkey_hash, int grp)
{
	struct daos_oclass_attr *oca;
	int			shard = -DER_NONEXIST;
	int			p_size;
	int			grp_size;
	int			idx;
	int			i;

	oca = obj_get_oca(obj);
	D_ASSERT(daos_oclass_is_ec(obj_get_oca(obj)));
	p_size = obj_ec_parity_tgt_nr(oca);
	grp_size = obj_get_grp_size(obj);
	idx = d_rand() % p_size;
	for (i = 0; i < p_size; i++, idx = (idx + 1) % p_size) {
		shard = grp_size * grp + obj_ec_parity_idx(obj, dkey_hash, idx);
		if (obj_shard_is_invalid(obj, shard, DAOS_OBJ_RPC_ENUMERATE))
			continue;

		D_DEBUG(DB_IO, "Choose parity shard %d grp %d\n", shard, grp);
		break;
	}

	if (i == p_size) {
		D_DEBUG(DB_IO, DF_OID" grp %d no parity shard available.\n",
			DP_OID(obj->cob_md.omd_id), grp);
		return -DER_NONEXIST;
	}

	return shard;
}

/**
 * Get parity or all data shards, used for EC enumerate or EC check existence.
 * (dkey == NULL) only possible for the case of EC enumerate - list dkey.
 */
static int
obj_ec_get_parity_or_alldata_shard(struct obj_auxi_args *obj_auxi, unsigned int map_ver,
				   int grp_idx, daos_key_t *dkey, uint32_t *shard_cnt,
				   uint8_t **bitmaps)
{
	struct dc_object	*obj = obj_auxi->obj;
	struct daos_oclass_attr *oca;
	int			i;
	int			grp_start;
	int			shard;
	unsigned int		first;

	oca = obj_get_oca(obj);
	if (dkey == NULL && obj_ec_parity_rotate_enabled(obj)) {
		int fail_cnt = 0;

		/**
		 * Normally, it only needs to enumerate from tgt_nr - parity_nr,
		 * but then if enumeration is shifted to others shards due to
		 * the failure, if might cause duplicate keys, which is not easy
		 * to resolve, so let's enumerate from all shards in this case.
		 */
		*shard_cnt = 0;
		grp_start = grp_idx * obj_get_grp_size(obj);
		/* Check if each shards are in good state */
		D_ASSERT(bitmaps != NULL);
		for (i = 0; i < obj_ec_tgt_nr(oca); i++) {
			int shard_idx;

			shard_idx = grp_start + i;
			if (obj_shard_is_invalid(obj, shard_idx, DAOS_OBJ_RPC_ENUMERATE)) {
				if (++fail_cnt > obj_ec_parity_tgt_nr(oca)) {
					D_ERROR(DF_OID" reach max failure "DF_RC"\n",
						DP_OID(obj->cob_md.omd_id), DP_RC(-DER_DATA_LOSS));
					D_GOTO(out, shard = -DER_DATA_LOSS);
				}
				continue;
			}
			setbit(*bitmaps, i);
			(*shard_cnt)++;
		}
		D_GOTO(out, shard = grp_start);
	}

	if (likely(!DAOS_FAIL_CHECK(DAOS_OBJ_SKIP_PARITY))) {
		*shard_cnt = 1;
		if (obj_auxi->to_leader) {
			shard = obj_ec_leader_select(obj, grp_idx, false, map_ver,
						     obj_auxi->dkey_hash, NIL_BITMAP);
			if (shard < 0)
				D_GOTO(out, shard);

			if (is_ec_data_shard(obj_auxi->obj, obj_auxi->dkey_hash, shard))
				*shard_cnt = obj_ec_data_tgt_nr(oca);
			if (bitmaps != NULL)
				setbit(*bitmaps, shard % obj_get_grp_size(obj));
			D_GOTO(out, shard);
		}

		shard = obj_ec_random_parity_get(obj, obj_auxi->dkey_hash, grp_idx);
		if (shard >= 0) {
			if (bitmaps != NULL)
				setbit(*bitmaps, shard % obj_get_grp_size(obj));
			D_GOTO(out, shard);
		}
	}

	grp_start = grp_idx * obj_get_grp_size(obj);
	first = obj_ec_shard_idx(obj, obj_auxi->dkey_hash, 0);
	D_DEBUG(DB_IO, "let's choose from the data shard %u for "DF_OID"\n",
		first, DP_OID(obj->cob_md.omd_id));

	/* Check if all data shard are in good state */
	for (i = 0; i < obj_ec_data_tgt_nr(oca); i++) {
		int shard_idx;

		shard_idx = grp_start + (first + i) % obj_ec_tgt_nr(oca);
		if (obj_shard_is_invalid(obj, shard_idx, DAOS_OBJ_RPC_ENUMERATE)) {
			shard = -DER_DATA_LOSS;
			D_ERROR("shard %d on "DF_OID" "DF_RC"\n", shard_idx,
				DP_OID(obj->cob_md.omd_id), DP_RC(shard));
			D_GOTO(out, shard);
		}

		if (bitmaps != NULL)
			setbit(*bitmaps, shard_idx % obj_ec_tgt_nr(oca));
	}

	shard = first + grp_start;
	*shard_cnt = obj_ec_data_tgt_nr(oca);

out:
	D_DEBUG(DB_IO, "grp_idx %d, get shard/cnt %d/%d on "DF_OID"\n", grp_idx, shard,
		*shard_cnt, DP_OID(obj->cob_md.omd_id));
	return shard;
}

static int
obj_list_shards_get(struct obj_auxi_args *obj_auxi, unsigned int map_ver,
		    daos_obj_list_t *args, uint32_t *shard, uint32_t *shard_cnt,
		    uint8_t **bitmaps)
{
	struct dc_object	*obj = obj_auxi->obj;
	int			grp_idx = 0;
	int			rc = 0;

	if (DAOS_FAIL_CHECK(DAOS_OBJ_SPECIAL_SHARD)) {
		if (obj_auxi->io_retry) {
			*shard = obj_auxi->specified_shard;
		} else {
			*shard = daos_fail_value_get();
			obj_auxi->specified_shard = *shard;
		}
		*shard_cnt = 1;
		*bitmaps = NULL;
		obj_auxi->spec_shard = 1;
		D_DEBUG(DB_IO, DF_OID" spec shard %u\n", DP_OID(obj->cob_md.omd_id), *shard);
		return 0;
	}

	if (args->dkey_anchor != NULL &&
	    daos_anchor_get_flags(args->dkey_anchor) & DIOF_TO_SPEC_SHARD) {
		*shard = dc_obj_anchor2shard(args->dkey_anchor);
		obj_auxi->specified_shard = *shard;
		*shard_cnt = 1;

		*bitmaps = NULL;
		obj_auxi->spec_shard = 1;
		D_DEBUG(DB_IO, DF_OID" spec shard %u\n", DP_OID(obj->cob_md.omd_id), *shard);
		return 0;
	}

	if (args->dkey_anchor != NULL &&
	    daos_anchor_get_flags(args->dkey_anchor) & DIOF_TO_SPEC_GROUP) {
		*shard = dc_obj_anchor2shard(args->dkey_anchor);
		obj_auxi->spec_group = 1;
		grp_idx = *shard / obj_get_replicas(obj);
	} else {
		if (args->dkey != NULL) {
			grp_idx = obj_dkey2grpidx(obj, obj_auxi->dkey_hash, map_ver);
			if (grp_idx < 0) {
				D_ERROR(DF_OID" can not find grp %d\n",
					DP_OID(obj->cob_md.omd_id), grp_idx);
				D_GOTO(out, rc = grp_idx);
			}
		} else {
			D_ASSERT(args->dkey_anchor != NULL);
			grp_idx = dc_obj_anchor2shard(args->dkey_anchor) /
				  obj_get_grp_size(obj);
		}
	}

	if (obj_auxi->is_ec_obj) {
		rc = obj_ec_get_parity_or_alldata_shard(obj_auxi, map_ver, grp_idx, args->dkey,
							shard_cnt, bitmaps);
	} else {
		*bitmaps = NULL;
		*shard_cnt = 1;
		if (obj_auxi->to_leader) {
			rc = obj_replica_leader_select(obj, grp_idx, obj_auxi->dkey_hash, map_ver);
		} else {
			rc = obj_replica_grp_fetch_valid_shard_get(obj, grp_idx, map_ver,
								   obj_auxi->failed_tgt_list);
			if (rc == -DER_NONEXIST) {
				D_ERROR(DF_OID" can not find any shard %d\n",
					DP_OID(obj->cob_md.omd_id), -DER_DATA_LOSS);
				D_GOTO(out, rc = -DER_DATA_LOSS);
			}
		}
	}

	if (rc < 0) {
		D_ERROR(DF_OID" Can not find shard grp %d: "DF_RC"\n",
			DP_OID(obj->cob_md.omd_id), grp_idx, DP_RC(rc));
		D_GOTO(out, rc);
	}

	*shard = rc;
	D_DEBUG(DB_IO, DF_OID" grp/shard/shard_cnt %d/%u/%u\n", DP_OID(obj->cob_md.omd_id),
		grp_idx, *shard, *shard_cnt);

out:
	D_DEBUG(DB_IO, DF_OID " list on shard %u leader %s: %d\n", DP_OID(obj->cob_md.omd_id),
		*shard, obj_auxi->to_leader ? "yes" : "no", rc);
	return rc;
}

static int
obj_list_common(tse_task_t *task, int opc, daos_obj_list_t *args)
{
	struct dc_object	*obj;
	struct obj_auxi_args	*obj_auxi;
	unsigned int		map_ver = 0;
	struct dtx_epoch	epoch = {0};
	uint32_t		shard = 0;
	uint32_t		shard_cnt = 0;
	uint8_t			bitmaps[OBJ_TGT_BITMAP_LEN] = { 0 };
	uint8_t			*p_bitmaps = bitmaps;
	int			rc;

	rc = obj_req_valid(task, args, opc, &epoch, &map_ver, &obj);
	if (rc)
		goto out_task;

	rc = obj_task_init(task, opc, map_ver, args->th, &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	if (args->dkey_anchor != NULL) {
		if (daos_anchor_get_flags(args->dkey_anchor) & DIOF_FOR_MIGRATION)
			obj_auxi->no_retry = 1;

		if (daos_anchor_get_flags(args->dkey_anchor) & DIOF_FOR_FORCE_DEGRADE ||
		    DAOS_FAIL_CHECK(DAOS_OBJ_FORCE_DEGRADE))
			obj_auxi->force_degraded = 1;

		if (daos_anchor_get_flags(args->dkey_anchor) & DIOF_TO_LEADER)
			obj_auxi->to_leader = 1;
	}

	if (args->dkey)
		obj_auxi->dkey_hash = obj_dkey2hash(obj->cob_md.omd_id, args->dkey);

	/* reset kd_key_len to 0, since it may return the required size, see
	 * obj_shard_comp_cb.
	 */
	if (args->kds)
		args->kds[0].kd_key_len = 0;

	rc = obj_list_shards_get(obj_auxi, map_ver, args, &shard, &shard_cnt, &p_bitmaps);
	if (rc < 0)
		D_GOTO(out_task, rc);

	rc = obj_shards_2_fwtgts(obj, map_ver, p_bitmaps, shard, shard_cnt,
				 1, OBJ_TGT_FLAG_CLI_DISPATCH, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);

	if (shard_cnt > 1) {
		rc = sub_anchors_prep(obj_auxi, shard_cnt);
		if (rc)
			D_GOTO(out_task, rc);
	}

	if (daos_handle_is_valid(args->th)) {
		rc = dc_tx_get_dti(args->th, &obj_auxi->l_args.la_dti);
		/*
		 * The obj_req_valid call above has already verified this
		 * transaction handle.
		 */
		D_ASSERTF(rc == 0, "%d\n", rc);
	} else {
		daos_dti_gen(&obj_auxi->l_args.la_dti, true /* zero */);
	}

	D_DEBUG(DB_IO, "list opc %d "DF_OID" dkey "DF_U64" shard %u/%u\n", opc,
		DP_OID(obj->cob_md.omd_id), obj_auxi->dkey_hash, shard, shard_cnt);

	rc = obj_req_fanout(obj, obj_auxi, map_ver, &epoch,
			    shard_list_prep, dc_obj_shard_list, task);
	return rc;

out_task:
	obj_task_complete(task, rc);
	return rc;
}

int
dc_obj_list_dkey(tse_task_t *task)
{
	daos_obj_list_dkey_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_list_common(task, DAOS_OBJ_DKEY_RPC_ENUMERATE, args);
}

int
dc_obj_list_akey(tse_task_t *task)
{
	daos_obj_list_akey_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_list_common(task, DAOS_OBJ_AKEY_RPC_ENUMERATE, args);
}

int
dc_obj_list_obj(tse_task_t *task)
{
	daos_obj_list_obj_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_list_common(task, DAOS_OBJ_RPC_ENUMERATE, args);
}

int
dc_obj_list_rec(tse_task_t *task)
{
	daos_obj_list_recx_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_list_common(task, DAOS_OBJ_RECX_RPC_ENUMERATE, args);
}

static int
shard_k2a_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
	       struct obj_auxi_args *obj_auxi, uint32_t grp_idx)
{
	daos_obj_key2anchor_t	*obj_args;
	struct shard_k2a_args	*shard_arg;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	shard_arg = container_of(shard_auxi, struct shard_k2a_args, ka_auxi);
	if (obj_args->anchor->da_sub_anchors) {
		struct shard_anchors *sub_anchors;
		int shard;

		sub_anchors = (struct shard_anchors *)obj_args->anchor->da_sub_anchors;
		shard = shard_anchor_lookup(sub_anchors, shard_auxi->shard);
		D_ASSERT(shard != -1);
		shard_arg->ka_anchor = &sub_anchors->sa_anchors[shard].ssa_anchor;
	} else {
		shard_arg->ka_anchor = obj_args->anchor;
	}
	return 0;
}

int
dc_obj_key2anchor(tse_task_t *task)
{
	daos_obj_key2anchor_t	*args;
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	unsigned int		map_ver = 0;
	struct dtx_epoch	epoch;
	uint32_t		shard;
	uint32_t		shard_cnt;
	int			grp_idx = 0;
	int			rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	if (args->anchor == NULL) {
		D_ERROR("Invalid anchor to daos_obj_key2anchor\n");
		D_GOTO(err_task, rc);
	}

	rc = obj_req_valid(task, args, DAOS_OBJ_RPC_KEY2ANCHOR, &epoch, &map_ver, &obj);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = obj_task_init(task, DAOS_OBJ_RPC_KEY2ANCHOR, map_ver, args->th, &obj_auxi, obj);
	if (rc != 0)
		D_GOTO(err_obj, rc);

	obj_auxi->dkey_hash = obj_dkey2hash(obj->cob_md.omd_id, args->dkey);
	grp_idx = obj_dkey2grpidx(obj, obj_auxi->dkey_hash, map_ver);
	if (grp_idx < 0) {
		D_ERROR(DF_OID" can not find grp %d\n", DP_OID(obj->cob_md.omd_id), grp_idx);
		D_GOTO(err_obj, rc = grp_idx);
	}

	if (obj_auxi->is_ec_obj) {
		rc = obj_ec_get_parity_or_alldata_shard(obj_auxi, map_ver, grp_idx,
							args->dkey, &shard_cnt, NULL);
		if (obj_ec_parity_rotate_enabled(obj))
			shard_cnt = obj_get_grp_size(obj);
	} else {
		shard_cnt = 1;
		if (obj_auxi->to_leader) {
			rc = obj_replica_leader_select(obj, grp_idx, obj_auxi->dkey_hash, map_ver);
		} else {
			rc = obj_replica_grp_fetch_valid_shard_get(obj, grp_idx, map_ver,
								   obj_auxi->failed_tgt_list);
			if (rc == -DER_NONEXIST) {
				D_ERROR(DF_OID" can not find any shard %d\n",
					DP_OID(obj->cob_md.omd_id), -DER_DATA_LOSS);
				D_GOTO(err_obj, rc = -DER_DATA_LOSS);
			}
		}
	}
	if (rc < 0) {
		D_ERROR(DF_OID" Can not find shard grp %d: "DF_RC"\n",
			DP_OID(obj->cob_md.omd_id), grp_idx, DP_RC(rc));
		D_GOTO(err_obj, rc);
	}
	shard = rc;

	rc = obj_shards_2_fwtgts(obj, map_ver, NIL_BITMAP, shard, shard_cnt,
				 1, OBJ_TGT_FLAG_CLI_DISPATCH, obj_auxi);
	if (rc != 0)
		D_GOTO(err_obj, rc);

	if (shard_cnt > 1) {
		rc = sub_anchors_prep(obj_auxi, shard_cnt);
		if (rc) {
			D_ERROR(DF_OID" prepare %d anchor fail: %d\n",
				DP_OID(obj->cob_md.omd_id), shard_cnt, rc);
			D_GOTO(err_obj, rc);
		}
	}

	if (daos_handle_is_valid(args->th)) {
		rc = dc_tx_get_dti(args->th, &obj_auxi->k_args.ka_dti);
		D_ASSERTF(rc == 0, "%d\n", rc);
	} else {
		daos_dti_gen(&obj_auxi->k_args.ka_dti, true);
	}

	return obj_req_fanout(obj, obj_auxi, map_ver, &epoch, shard_k2a_prep,
			      dc_obj_shard_key2anchor, task);
err_obj:
	obj_decref(obj);
err_task:
	obj_task_complete(task, rc);
	return rc;
}

static int
shard_punch_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
		 struct obj_auxi_args *obj_auxi, uint32_t grp_idx)
{
	struct shard_punch_args	*shard_arg;
	uuid_t			 coh_uuid;
	uuid_t			 cont_uuid;
	int			 rc;

	rc = dc_cont2uuid(obj->cob_co, &coh_uuid, &cont_uuid);
	if (rc != 0)
		return rc;

	shard_arg = container_of(shard_auxi, struct shard_punch_args, pa_auxi);
	shard_arg->pa_opc		= obj_auxi->opc;
	uuid_copy(shard_arg->pa_coh_uuid, coh_uuid);
	uuid_copy(shard_arg->pa_cont_uuid, cont_uuid);

	if (daos_handle_is_inval(obj_auxi->th))
		daos_dti_gen(&shard_arg->pa_dti,
			     srv_io_mode != DIM_DTX_FULL_ENABLED);
	else
		dc_tx_get_dti(obj_auxi->th, &shard_arg->pa_dti);

	return 0;
}

static int
dc_obj_punch(tse_task_t *task, struct dc_object *obj, struct dtx_epoch *epoch,
	     uint32_t map_ver, enum obj_rpc_opc opc, daos_obj_punch_t *api_args)
{
	struct obj_auxi_args	*obj_auxi;
	uint32_t		shard;
	uint32_t		shard_cnt;
	uint32_t		grp_cnt;
	int			rc;

	if (opc == DAOS_OBJ_RPC_PUNCH && obj->cob_grp_nr > 1)
		/* The object have multiple redundancy groups, use DAOS
		 * internal transaction to handle that to guarantee the
		 * atomicity of punch object.
		 */
		return dc_tx_convert(obj, opc, task);

	rc = obj_task_init(task, opc, map_ver, api_args->th, &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	if (obj_auxi->tx_convert) {
		obj_auxi->tx_convert = 0;
		return dc_tx_convert(obj, opc, task);
	}

	if (opc == DAOS_OBJ_RPC_PUNCH) {
		obj_ptr2shards(obj, &shard, &shard_cnt, &grp_cnt);
	} else {
		grp_cnt = 1;
		obj_auxi->dkey_hash = obj_dkey2hash(obj->cob_md.omd_id, api_args->dkey);
		rc = obj_dkey2grpmemb(obj, obj_auxi->dkey_hash, map_ver, &shard, &shard_cnt);
		if (rc != 0)
			D_GOTO(out_task, rc);
	}

	if (api_args->flags & DAOS_COND_MASK)
		obj_auxi->cond_modify = 1;

	rc = obj_shards_2_fwtgts(obj, map_ver, NIL_BITMAP, shard, shard_cnt, grp_cnt,
				 OBJ_TGT_FLAG_FW_LEADER_INFO, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);

	if (daos_fail_check(DAOS_FAIL_TX_CONVERT))
		D_GOTO(out_task, rc = -DER_NEED_TX);

	if (DAOS_FAIL_CHECK(DAOS_DTX_COMMIT_SYNC))
		obj_auxi->flags |= ORF_DTX_SYNC;
	if (obj_is_ec(obj))
		obj_auxi->flags |= ORF_EC;

	D_DEBUG(DB_IO, "punch "DF_OID" dkey "DF_U64"\n",
		DP_OID(obj->cob_md.omd_id), obj_auxi->dkey_hash);

	rc = obj_req_fanout(obj, obj_auxi, map_ver, epoch,
			    shard_punch_prep, dc_obj_shard_punch, task);
	return rc;

out_task:
	obj_task_complete(task, rc);
	return rc;
}

static int
obj_punch_common(tse_task_t *task, enum obj_rpc_opc opc, daos_obj_punch_t *args)
{
	struct dtx_epoch	 epoch = {0};
	unsigned int		 map_ver = 0;
	struct dc_object	*obj = NULL;
	int			 rc;

	rc = obj_req_valid(task, args, opc, &epoch, &map_ver, &obj);
	if (rc != 0)
		goto comp; /* invalid parameters */

	if (daos_handle_is_valid(args->th))
		/* add the operation to DTX and complete immediately */
		return dc_tx_attach(args->th, obj, opc, task, 0, true);

	/* submit the punch */
	return dc_obj_punch(task, obj, &epoch, map_ver, opc, args);

comp:
	obj_task_complete(task, rc);
	return rc;
}

int
dc_obj_punch_task(tse_task_t *task)
{
	daos_obj_punch_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_punch_common(task, DAOS_OBJ_RPC_PUNCH, args);
}

int
dc_obj_punch_dkeys_task(tse_task_t *task)
{
	daos_obj_punch_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_punch_common(task, DAOS_OBJ_RPC_PUNCH_DKEYS, args);
}

int
dc_obj_punch_akeys_task(tse_task_t *task)
{
	daos_obj_punch_t	*args;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	return obj_punch_common(task, DAOS_OBJ_RPC_PUNCH_AKEYS, args);
}

struct shard_query_key_args {
	/* shard_auxi_args must be the first for shard_task_sched(). */
	struct shard_auxi_args	 kqa_auxi;
	uuid_t			 kqa_coh_uuid;
	uuid_t			 kqa_cont_uuid;
	struct dtx_id		 kqa_dti;
};

static int
shard_query_key_task(tse_task_t *task)
{
	struct shard_query_key_args	*args;
	daos_obj_query_key_t		*api_args;
	struct dc_object		*obj;
	struct dc_obj_shard		*obj_shard;
	daos_handle_t			 th;
	struct dtx_epoch		*epoch;
	int				 rc;

	args = tse_task_buf_embedded(task, sizeof(*args));
	obj = args->kqa_auxi.obj_auxi->obj;
	th = args->kqa_auxi.obj_auxi->th;
	epoch = &args->kqa_auxi.epoch;

	/* See the similar shard_io_task. */
	if (daos_handle_is_valid(th) && !dtx_epoch_chosen(epoch)) {
		rc = dc_tx_get_epoch(task, th, epoch);
		if (rc < 0) {
			obj_task_complete(task, rc);
			return rc;
		}

		if (rc == DC_TX_GE_REINITED)
			return 0;
	}

	rc = obj_shard_open(obj, args->kqa_auxi.shard, args->kqa_auxi.map_ver, &obj_shard);
	if (rc != 0) {
		/* skip a failed target */
		if (rc == -DER_NONEXIST)
			rc = 0;

		obj_task_complete(task, rc);
		return rc;
	}

	rc = tse_task_register_comp_cb(task, close_shard_cb, &obj_shard, sizeof(obj_shard));
	if (rc != 0) {
		obj_shard_close(obj_shard);
		obj_task_complete(task, rc);
		return rc;
	}

	api_args = dc_task_get_args(args->kqa_auxi.obj_auxi->obj_task);
	rc = dc_obj_shard_query_key(obj_shard, epoch, api_args->flags,
				    args->kqa_auxi.obj_auxi->map_ver_req, obj,
				    api_args->dkey, api_args->akey,
				    api_args->recx, api_args->max_epoch, args->kqa_coh_uuid,
				    args->kqa_cont_uuid, &args->kqa_dti,
				    &args->kqa_auxi.obj_auxi->map_ver_reply, th, task);

	return rc;
}

static int
queue_shard_query_key_task(tse_task_t *api_task, struct obj_auxi_args *obj_auxi,
			   struct dtx_epoch *epoch, int shard, unsigned int map_ver,
			   struct dc_object *obj, struct dtx_id *dti,
			   uuid_t coh_uuid, uuid_t cont_uuid)
{
	tse_sched_t			*sched = tse_task2sched(api_task);
	tse_task_t			*task;
	struct shard_query_key_args	*args;
	d_list_t			*head = NULL;
	int				rc;

	rc = tse_task_create(shard_query_key_task, sched, NULL, &task);
	if (rc != 0)
		return rc;

	args = tse_task_buf_embedded(task, sizeof(*args));
	args->kqa_auxi.epoch	= *epoch;
	args->kqa_auxi.shard	= shard;
	args->kqa_auxi.map_ver	= map_ver;
	args->kqa_auxi.obj_auxi	= obj_auxi;
	args->kqa_dti		= *dti;
	uuid_copy(args->kqa_coh_uuid, coh_uuid);
	uuid_copy(args->kqa_cont_uuid, cont_uuid);

	rc = obj_shard2tgtid(obj, shard, map_ver,
			     &args->kqa_auxi.target);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = tse_task_register_deps(api_task, 1, &task);
	if (rc != 0)
		D_GOTO(out_task, rc);

	head = &obj_auxi->shard_task_head;
	/* decref and delete from head at shard_task_remove */
	tse_task_addref(task);
	tse_task_list_add(task, head);

out_task:
	if (rc)
		obj_task_complete(task, rc);
	return rc;
}

int
dc_obj_query_key(tse_task_t *api_task)
{
	daos_obj_query_key_t	*api_args = dc_task_get_args(api_task);
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	d_list_t		*head = NULL;
	uuid_t			coh_uuid;
	uuid_t			cont_uuid;
	int			grp_idx;
	uint32_t		grp_nr;
	unsigned int		map_ver = 0;
	struct dtx_epoch	epoch;
	struct dtx_id		dti;
	int			i = 0;
	int			rc;

	D_ASSERTF(api_args != NULL,
		  "Task Argument OPC does not match DC OPC\n");

	/** for EC need to zero out user recx if passed */
	if (api_args->recx)
		memset(api_args->recx, 0, sizeof(*api_args->recx));

	rc = obj_req_valid(api_task, api_args, DAOS_OBJ_RPC_QUERY_KEY, &epoch, &map_ver, &obj);
	if (rc)
		D_GOTO(out_task, rc);

	if (daos_handle_is_valid(api_args->th)) {
		rc = dc_tx_get_dti(api_args->th, &dti);
		/*
		 * The dc_tx_hdl2epoch_and_pmv call above has already verified
		 * this transaction handle.
		 */
		D_ASSERTF(rc == 0, "%d\n", rc);
	} else {
		daos_dti_gen(&dti, true /* zero */);
	}

	rc = obj_task_init(api_task, DAOS_OBJ_RPC_QUERY_KEY, map_ver, api_args->th, &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	obj_auxi->spec_shard = 0;
	obj_auxi->spec_group = 0;

	rc = dc_cont2uuid(obj->cob_co, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_task, rc);

	if (api_args->flags)
		D_ASSERTF(api_args->dkey != NULL, "dkey should not be NULL\n");
	obj_auxi->dkey_hash = obj_dkey2hash(obj->cob_md.omd_id, api_args->dkey);
	if (api_args->flags & DAOS_GET_DKEY) {
		grp_idx = 0;
		/** set data len to 0 before retrieving dkey. */
		api_args->dkey->iov_len = 0;
		grp_nr = obj_get_grp_nr(obj);
	} else {
		grp_idx = obj_dkey2grpidx(obj, obj_auxi->dkey_hash, map_ver);
		if (grp_idx < 0)
			D_GOTO(out_task, rc = grp_idx);

		grp_nr = 1;
	}

	obj_auxi->map_ver_reply = 0;
	obj_auxi->map_ver_req = map_ver;

	D_DEBUG(DB_IO, "Object Key Query "DF_OID" grp %d/%u map %u\n",
		DP_OID(obj->cob_md.omd_id), grp_idx, grp_nr, map_ver);

	head = &obj_auxi->shard_task_head;

	if (obj_auxi->io_retry && obj_auxi->args_initialized) {
		/* For distributed transaction, check whether TX pool
		 * map is stale or not, if stale, restart the TX.
		 */
		if (daos_handle_is_valid(obj_auxi->th)) {
			rc = dc_tx_check_pmv(obj_auxi->th);
			if (rc != 0)
				goto out_task;
		}

		/* Let's always remove the previous shard tasks for retry, since
		 * the leader status might change.
		 */
		tse_task_list_traverse(head, shard_task_remove, NULL);
		obj_auxi->args_initialized = 0;
		obj_auxi->new_shard_tasks = 1;
	}

	D_ASSERT(!obj_auxi->args_initialized);
	D_ASSERT(d_list_empty(head));

	for (i = grp_idx; i < grp_idx + grp_nr; i++) {
		int start_shard;
		int j;
		int shard_cnt = 0;

		/* Try leader for current group */
		if (!obj_is_ec(obj) || (obj_is_ec(obj) && !obj_ec_parity_rotate_enabled(obj))) {
			int leader;

			leader = obj_grp_leader_get(obj, i, (uint64_t)d_rand(),
						    obj_auxi->cond_modify, map_ver, NULL);
			if (leader >= 0) {
				if (obj_is_ec(obj) &&
				    !is_ec_parity_shard(obj, obj_auxi->dkey_hash, leader))
					goto non_leader;

				rc = queue_shard_query_key_task(api_task, obj_auxi, &epoch, leader,
								map_ver, obj, &dti, coh_uuid,
								cont_uuid);
				if (rc)
					D_GOTO(out_task, rc);

				D_DEBUG(DB_IO, DF_OID" try leader %d for group %d.\n",
					DP_OID(obj->cob_md.omd_id), leader, i);
				continue;
			}

			/* There has to be a leader for non-EC object */
			D_ERROR(DF_OID" no valid shard, rc " DF_RC"\n",
				DP_OID(obj->cob_md.omd_id), DP_RC(leader));
			D_GOTO(out_task, rc = leader);
		}

non_leader:
		/* Then Try non-leader shards */
		D_ASSERT(obj_is_ec(obj));
		start_shard = i * obj_get_grp_size(obj);
		D_DEBUG(DB_IO, DF_OID" EC needs to try all shards for group %d.\n",
			DP_OID(obj->cob_md.omd_id), i);
		for (j = start_shard; j < start_shard + daos_oclass_grp_size(&obj->cob_oca); j++) {
			if (obj_shard_is_invalid(obj, j, DAOS_OBJ_RPC_QUERY_KEY))
				continue;
			rc = queue_shard_query_key_task(api_task, obj_auxi, &epoch, j, map_ver,
							obj, &dti, coh_uuid, cont_uuid);
			if (rc)
				D_GOTO(out_task, rc);

			if (++shard_cnt >= obj_ec_data_tgt_nr(&obj->cob_oca))
				break;
		}

		if (shard_cnt < obj_ec_data_tgt_nr(&obj->cob_oca)) {
			D_ERROR(DF_OID" EC grp %d only have %d shards.\n",
				DP_OID(obj->cob_md.omd_id), i, shard_cnt);
			D_GOTO(out_task, rc = -DER_DATA_LOSS);
		}
	}

	obj_auxi->args_initialized = 1;
	obj_shard_task_sched(obj_auxi, &epoch);

	return 0;

out_task:
	if (head != NULL && !d_list_empty(head)) {
		D_ASSERTF(!obj_retry_error(rc), "unexpected ret "DF_RC"\n", DP_RC(rc));
		/* abort/complete sub-tasks will complete api_task */
		tse_task_list_traverse(head, shard_task_abort, &rc);
	} else {
		obj_task_complete(api_task, rc);
	}

	return rc;
}

static int
shard_sync_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
		struct obj_auxi_args *obj_auxi, uint32_t grp_idx)
{
	struct daos_obj_sync_args	*obj_args;
	struct shard_sync_args		*shard_args;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	shard_args = container_of(shard_auxi, struct shard_sync_args, sa_auxi);
	shard_args->sa_epoch = &(*obj_args->epochs_p)[grp_idx];

	return 0;
}

int
dc_obj_sync(tse_task_t *task)
{
	struct daos_obj_sync_args	*args = dc_task_get_args(task);
	struct obj_auxi_args		*obj_auxi = NULL;
	struct dc_object		*obj = NULL;
	struct dtx_epoch		 epoch;
	uint32_t			 map_ver = 0;
	uint32_t			shard;
	uint32_t			shard_cnt;
	uint32_t			grp_cnt;
	int				 rc;
	int				 i;

	if (srv_io_mode != DIM_DTX_FULL_ENABLED)
		D_GOTO(out_task, rc = 0);

	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC\n");

	rc = obj_req_valid(task, args, DAOS_OBJ_RPC_SYNC, &epoch,
			   &map_ver, &obj);
	if (rc)
		D_GOTO(out_task, rc);

	rc = obj_task_init(task, DAOS_OBJ_RPC_SYNC, map_ver, DAOS_HDL_INVAL,
			   &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	obj_auxi->spec_shard = 0;
	obj_auxi->spec_group = 0;

	epoch.oe_value = args->epoch;
	epoch.oe_first = epoch.oe_value;
	epoch.oe_flags = 0;

	/* Need to mark sync epoch on server, so even if the @replicas is 1,
	 * we still need to send SYNC RPC to the server.
	 */
	if (!obj_auxi->io_retry) {
		daos_epoch_t	*tmp = NULL;

		D_ALLOC_ARRAY(tmp, obj->cob_grp_nr);
		if (tmp == NULL)
			D_GOTO(out_task, rc = -DER_NOMEM);

		*args->nr = obj->cob_grp_nr;
		*args->epochs_p = tmp;
	} else {
		D_ASSERT(*args->epochs_p != NULL);
		D_ASSERTF(*args->nr == obj->cob_grp_nr, "Invalid obj sync args %d/%d\n",
			  *args->nr, obj->cob_grp_nr);

		for (i = 0; i < *args->nr; i++)
			*args->epochs_p[i] = 0;
	}

	obj_auxi->to_leader = 1;
	obj_ptr2shards(obj, &shard, &shard_cnt, &grp_cnt);
	rc = obj_shards_2_fwtgts(obj, map_ver, NIL_BITMAP, shard, shard_cnt,
				 grp_cnt, OBJ_TGT_FLAG_LEADER_ONLY, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);

	D_DEBUG(DB_IO, "sync "DF_OID", %s obj: %d\n",
		DP_OID(obj->cob_md.omd_id), obj_is_ec(obj) ? "EC" : "REP",
		obj_get_replicas(obj));

	return obj_req_fanout(obj, obj_auxi, map_ver, &epoch,
			      shard_sync_prep, dc_obj_shard_sync, task);

out_task:
	obj_task_complete(task, rc);
	return rc;
}

int
dc_obj_verify(daos_handle_t oh, daos_epoch_t *epochs, unsigned int nr)
{
	struct dc_obj_verify_args		*dova = NULL;
	struct dc_object			*obj;
	struct daos_oclass_attr			*oc_attr;
	unsigned int				 reps = 0;
	int					 rc = 0;
	int					 i;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	oc_attr = obj_get_oca(obj);
	if (oc_attr->ca_resil != DAOS_RES_REPL) {
		reps = 1;
	} else {
		if (oc_attr->u.rp.r_num == DAOS_OBJ_REPL_MAX)
			reps = obj->cob_grp_size;
		else
			reps = oc_attr->u.rp.r_num;

		if (reps == 1)
			goto out;
	}

	/* XXX: If we support progressive object layout in the future,
	 *	The "obj->cob_grp_nr" may be different from given @nr.
	 */
	D_ASSERTF(obj->cob_grp_nr == nr, "Invalid grp count %u/%u\n",
		  obj->cob_grp_nr, nr);

	D_ALLOC_ARRAY(dova, reps);
	if (dova == NULL) {
		D_ERROR(DF_OID" no MEM for verify group, reps %u\n",
			DP_OID(obj->cob_md.omd_id), reps);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	for (i = 0; i < reps; i++) {
		dova[i].oh = oh;

		dova[i].list_buf = dova[i].inline_buf;
		dova[i].list_buf_len = sizeof(dova[i].inline_buf);

		dova[i].fetch_buf = NULL;
		dova[i].fetch_buf_len = 0;
	}

	for (i = 0; i < obj->cob_grp_nr && rc == 0; i++) {
		/* Zero epoch means the shards in related redundancy group
		 * have not been created yet.
		 */
		if (epochs[i] != 0)
			rc = dc_obj_verify_rdg(obj, dova, i, reps, epochs[i]);
	}

out:
	if (dova != NULL) {
		for (i = 0; i < reps; i++) {
			if (dova[i].list_buf != dova[i].inline_buf)
				D_FREE(dova[i].list_buf);

			daos_iov_free(&dova[i].cursor.dkey);
			daos_iov_free(&dova[i].cursor.iod.iod_name);
			D_FREE(dova[i].fetch_buf);
		}

		D_FREE(dova);
	}

	obj_decref(obj);

	return rc;
}

void
daos_dc_obj2id(void *ptr, daos_unit_oid_t *id)
{
	struct dc_object *obj = ptr;

	id->id_pub = obj->cob_md.omd_id;
	id->id_layout_ver = obj->cob_layout_version;
	id->id_padding = 0;
}

/**
 * Real latest & greatest implementation of container create.
 * Used by anyone including the daos_obj.h header file.
 */
int
daos_obj_generate_oid(daos_handle_t coh, daos_obj_id_t *oid,
		       enum daos_otype_t type, daos_oclass_id_t cid,
		       daos_oclass_hints_t hints, uint32_t args)
{
	daos_handle_t		poh;
	struct dc_pool		*pool;
	struct pl_map_attr	attr = {0};
	enum daos_obj_redun	ord;
	uint32_t		nr_grp;
	struct cont_props	props;
	int			rc;
	uint32_t		 rf;
	struct dc_cont		*dc;

	if (!daos_otype_t_is_valid(type))
		return -DER_INVAL;

	/** select the oclass */
	poh = dc_cont_hdl2pool_hdl(coh);
	if (daos_handle_is_inval(poh))
		return -DER_NO_HDL;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	pool = dc_hdl2pool(poh);
	if (pool == NULL) {
		dc_cont_put(dc);
		return -DER_NO_HDL;
	}

	props = dc->dc_props;
	attr.pa_domain = props.dcp_redun_lvl;
	rc = pl_map_query(pool->dp_pool, &attr);
	D_ASSERT(rc == 0);
	dc_pool_put(pool);
	rf = dc->dc_props.dcp_redun_fac;

	D_DEBUG(DB_TRACE, "available domain=%d, targets=%d rf:%u\n", attr.pa_domain_nr,
		attr.pa_target_nr, rf);

	if (cid == OC_UNKNOWN) {
		rc = dc_set_oclass(rf, attr.pa_domain_nr, attr.pa_target_nr, type, hints, &ord,
				   &nr_grp);
	} else {
		rc = daos_oclass_fit_max(cid, attr.pa_domain_nr, attr.pa_target_nr, &ord, &nr_grp,
					 rf);
	}
	dc_cont_put(dc);

	if (rc)
		return rc;

	daos_obj_set_oid(oid, type, ord, nr_grp, args);

	return rc;
}

#undef daos_obj_generate_oid

int
daos_obj_generate_oid(daos_handle_t coh, daos_obj_id_t *oid,
		       enum daos_otype_t type, daos_oclass_id_t cid,
		       daos_oclass_hints_t hints, uint32_t args)
		       __attribute__ ((weak, alias("daos_obj_generate_oid2")));

int
daos_obj_generate_oid_by_rf(daos_handle_t poh, uint64_t rf_factor,
			    daos_obj_id_t *oid, enum daos_otype_t type,
			    daos_oclass_id_t cid, daos_oclass_hints_t hints,
			    uint32_t args, uint32_t pa_domain)
{
	struct dc_pool		*pool;
	struct pl_map_attr	attr = {0};
	enum daos_obj_redun	ord;
	uint32_t		nr_grp;
	int			rc;

	if (!daos_otype_t_is_valid(type))
		return -DER_INVAL;

	if (pa_domain == 0)
		pa_domain = DAOS_PROP_CO_REDUN_DEFAULT;
	else if (!daos_pa_domain_is_valid(pa_domain))
		return -DER_INVAL;

	pool = dc_hdl2pool(poh);
	D_ASSERT(pool);

	attr.pa_domain = pa_domain;
	rc = pl_map_query(pool->dp_pool, &attr);
	D_ASSERT(rc == 0);
	dc_pool_put(pool);

	if (cid == OC_UNKNOWN)
		rc = dc_set_oclass(rf_factor, attr.pa_domain_nr,
				   attr.pa_target_nr, type, hints, &ord,
				   &nr_grp);
	else
		rc = daos_oclass_fit_max(cid, attr.pa_domain_nr, attr.pa_target_nr, &ord, &nr_grp,
					 rf_factor);
	if (rc)
		return rc;

	daos_obj_set_oid(oid, type, ord, nr_grp, args);

	return rc;
}

int
daos_obj_get_oclass(daos_handle_t coh, enum daos_otype_t type, daos_oclass_hints_t hints,
		    uint32_t args, daos_oclass_id_t *cid)
{
	daos_handle_t		poh;
	struct dc_pool		*pool;
	struct pl_map_attr	attr = {0};
	uint32_t		rf;
	int			rc;
	enum daos_obj_redun	ord;
	struct cont_props	props;
	uint32_t		nr_grp;
	struct dc_cont		*dc;

	/** select the oclass */
	poh = dc_cont_hdl2pool_hdl(coh);
	if (daos_handle_is_inval(poh))
		return -DER_NO_HDL;

	dc = dc_hdl2cont(coh);
	if (dc == NULL)
		return -DER_NO_HDL;
	pool = dc_hdl2pool(poh);
	if (pool == NULL) {
		dc_cont_put(dc);
		return -DER_NO_HDL;
	}

	props = dc->dc_props;
	attr.pa_domain = props.dcp_redun_lvl;
	rc = pl_map_query(pool->dp_pool, &attr);
	if (rc) {
		D_ERROR("pl_map_query failed, "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	rf = dc->dc_props.dcp_redun_fac;
	dc_cont_put(dc);
	dc_pool_put(pool);
	rc = dc_set_oclass(rf, attr.pa_domain_nr, attr.pa_target_nr, type, hints, &ord, &nr_grp);
	if (rc)
		return rc;

	*cid = (ord << OC_REDUN_SHIFT) | nr_grp;
	return 0;
}

int
dc_obj_hdl2obj_md(daos_handle_t oh, struct daos_obj_md *md)
{
	struct dc_object *obj;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
	{
		return -DER_NO_HDL;
	}
	*md = obj->cob_md;
	obj_decref(obj);
	return 0;
}
