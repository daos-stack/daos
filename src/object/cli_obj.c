/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

#define CLI_OBJ_IO_PARMS	8
#define NIL_BITMAP		(NULL)

#define OBJ_TGT_INLINE_NR	(18)
struct obj_req_tgts {
	/* to save memory allocation if #targets <= OBJ_TGT_INLINE_NR */
	struct daos_shard_tgt	 ort_tgts_inline[OBJ_TGT_INLINE_NR];
	/* Shard target array, with (ort_grp_nr * ort_grp_size) targets.
	 * If #targets <= OBJ_TGT_INLINE_NR then it points to ort_tgts_inline.
	 * Within the array, [0, ort_grp_size - 1] is for the first group,
	 * [ort_grp_size, ort_grp_size * 2 - 1] is the 2nd group and so on.
	 * If (ort_srv_disp == 1) then within each group the first target is the
	 * leader shard and following (ort_grp_size - 1) targets are the forward
	 * non-leader shards.
	 * Now there is only one case for (ort_grp_nr > 1) that for object
	 * punch, all other cases with (ort_grp_nr == 1).
	 */
	struct daos_shard_tgt	*ort_shard_tgts;
	uint32_t		 ort_grp_nr;
	/* ort_grp_size is the size of the group that is sent as forwarded
	 * shards
	 */
	uint32_t		 ort_grp_size;
	/* ort_start_shard is only for EC object, it is the start shard number
	 * of the EC stripe. To facilitate calculate the offset of different
	 * shards in the stripe.
	 */
	uint32_t		 ort_start_shard;
	/* flag of server dispatch */
	uint32_t		 ort_srv_disp:1;
};

/* Auxiliary args for object I/O */
struct obj_auxi_args {
	tse_task_t			*obj_task;
	daos_handle_t			 th;
	struct dc_object		*obj;
	int				 opc;
	int				 result;
	uint32_t			 map_ver_req;
	uint32_t			 map_ver_reply;
	/* flags for the obj IO task.
	 * ec_wait_recov -- obj fetch wait another EC recovery task,
	 * ec_in_recov -- a EC recovery task
	 */
	uint32_t			 io_retry:1,
					 args_initialized:1,
					 to_leader:1,
					 spec_shard:1,
					 req_reasbed:1,
					 is_ec_obj:1,
					 csum_retry:1,
					 csum_report:1,
					 no_retry:1,
					 ec_wait_recov:1,
					 ec_in_recov:1,
					 new_shard_tasks:1;
	/* request flags. currently only: ORF_RESEND, ORF_CSUM_REPORT */
	uint32_t			 flags;
	uint32_t			 specified_shard;
	struct obj_req_tgts		 req_tgts;
	crt_bulk_t			*bulks;
	uint32_t			 iod_nr;
	uint32_t			 initial_shard;
	d_list_t			 shard_task_head;
	struct obj_reasb_req		 reasb_req;
	/* one shard_args embedded to save one memory allocation if the obj
	 * request only targets for one shard.
	 */
	union {
		struct shard_rw_args	 rw_args;
		struct shard_punch_args	 p_args;
		struct shard_list_args	 l_args;
		struct shard_sync_args	 s_args;
	};
};

/**
 * task memory space should enough to use -
 * obj API task with daos_task_args + obj_auxi_args,
 * shard sub-task with shard_auxi_args + obj_auxi_args.
 * When it exceed the limit, can reduce OBJ_TGT_INLINE_NR or enlarge tse_task.
 */
D_CASSERT(sizeof(struct obj_auxi_args) + sizeof(struct shard_auxi_args) <=
	  TSE_TASK_ARG_LEN);
D_CASSERT(sizeof(struct obj_auxi_args) + sizeof(struct daos_task_args) <=
	  TSE_TASK_ARG_LEN);

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
	if (obj_shard->do_shard == -1 ||
	    obj_shard->do_target_id == -1) {
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

		memset(&oid, 0, sizeof(oid));
		oid.id_shard = shard;
		oid.id_pub   = obj->cob_md.omd_id;
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
	D_SPIN_UNLOCK(&obj->cob_spin);

	if (layout != NULL)
		D_FREE(layout);
}

static void
obj_free(struct d_hlink *hlink)
{
	struct dc_object *obj;

	obj = container_of(hlink, struct dc_object, cob_hlink);
	D_ASSERT(daos_hhash_link_empty(&obj->cob_hlink));
	obj_layout_free(obj);
	if (obj->cob_time_fetch_leader != NULL)
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
	daos_hhash_link_putref(&obj->cob_hlink);
}

void
obj_addref(struct dc_object *obj)
{
	daos_hhash_link_getref(&obj->cob_hlink);
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

daos_handle_t
dc_obj_hdl2cont_hdl(daos_handle_t oh)
{
	struct dc_object *obj;
	daos_handle_t hdl;

	obj = obj_hdl2ptr(oh);
	if (obj == NULL)
		return DAOS_HDL_INVAL;

	hdl = obj->cob_coh;
	obj_decref(obj);
	return hdl;
}

static int
obj_layout_create(struct dc_object *obj, bool refresh)
{
	struct pl_obj_layout	*layout = NULL;
	struct dc_pool		*pool;
	struct pl_map		*map;
	int			i;
	int			rc;

	pool = dc_hdl2pool(dc_cont_hdl2pool_hdl(obj->cob_coh));
	D_ASSERT(pool != NULL);

	map = pl_map_find(pool->dp_pool, obj->cob_md.omd_id);
	dc_pool_put(pool);

	if (map == NULL) {
		D_DEBUG(DB_PL, "Cannot find valid placement map\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_place(map, &obj->cob_md, NULL, &layout);
	pl_map_decref(map);
	if (rc != 0) {
		D_DEBUG(DB_PL, "Failed to generate object layout\n");
		D_GOTO(out, rc);
	}
	D_DEBUG(DB_PL, "Place object on %d targets ver %d\n", layout->ol_nr,
		layout->ol_ver);
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

	if (obj->cob_grp_size > 1 && srv_io_mode == DIM_DTX_FULL_ENABLED &&
	    obj->cob_grp_nr < obj->cob_shards_nr / obj->cob_grp_size) {
		if (obj->cob_time_fetch_leader != NULL)
			D_FREE(obj->cob_time_fetch_leader);

		obj->cob_grp_nr = obj->cob_shards_nr / obj->cob_grp_size;
		D_ALLOC_ARRAY(obj->cob_time_fetch_leader, obj->cob_grp_nr);
		if (obj->cob_time_fetch_leader == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	for (i = 0; i < layout->ol_nr; i++) {
		struct dc_obj_shard *obj_shard;

		obj_shard = &obj->cob_shards->do_shards[i];
		obj_shard->do_shard = layout->ol_shards[i].po_shard;
		obj_shard->do_target_id = layout->ol_shards[i].po_target;
		obj_shard->do_fseq = layout->ol_shards[i].po_fseq;
		obj_shard->do_rebuilding = layout->ol_shards[i].po_rebuilding;
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
	rc = obj_layout_create(obj, true);
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return rc;
}

int
obj_get_replicas(struct dc_object *obj)
{
	struct daos_oclass_attr *oc_attr;

	oc_attr = daos_oclass_attr_find(obj->cob_md.omd_id);
	D_ASSERT(oc_attr != NULL);

	if (oc_attr->ca_resil != DAOS_RES_REPL)
		return 1;

	if (oc_attr->u.rp.r_num == DAOS_OBJ_REPL_MAX)
		return obj->cob_grp_size;

	return oc_attr->u.rp.r_num;
}

int
obj_get_grp_size(struct dc_object *obj)
{
	return obj->cob_grp_size;
}

/* Get a valid shard from an object group */
static int
obj_grp_valid_shard_get(struct dc_object *obj, int idx,
			unsigned int map_ver, uint32_t op)
{
	int idx_first;
	int grp_size;
	int i = 0;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	D_ASSERT(obj->cob_shards_nr > 0);

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_version != map_ver) {
		/* Sigh, someone else change the pool map */
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_STALE;
	}

	idx_first = (idx / grp_size) * grp_size;
	idx = idx_first + random() % grp_size;

	for (i = 0; i < grp_size;
	     i++, idx = idx_first + (idx + 1 - idx_first) % grp_size) {
		/* let's skip the rebuild shard for non-update op */
		if (op != DAOS_OBJ_RPC_UPDATE &&
		    obj->cob_shards->do_shards[idx].do_rebuilding)
			continue;

		if (obj->cob_shards->do_shards[idx].do_target_id != -1)
			break;
	}

	D_RWLOCK_UNLOCK(&obj->cob_lock);

	if (i == grp_size)
		return -DER_NONEXIST;

	return idx;
}

static inline struct pl_obj_shard*
obj_get_shard(void *data, int idx)
{
	struct dc_object	*obj = data;

	return &obj->cob_shards->do_shards[idx].do_pl_shard;
}

static int
obj_grp_leader_get(struct dc_object *obj, int idx, unsigned int map_ver)
{
	int	rc = -DER_STALE;

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_version == map_ver)
		rc = pl_select_leader(obj->cob_md.omd_id, idx,
				      obj->cob_grp_size, false,
				      obj_get_shard, obj);
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return rc;
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
	int		grp_size;
	uint64_t	grp_idx;

	grp_size = obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	D_RWLOCK_RDLOCK(&obj->cob_lock);
	if (obj->cob_version != map_ver) {
		D_RWLOCK_UNLOCK(&obj->cob_lock);
		return -DER_STALE;
	}

	D_ASSERT(obj->cob_shards_nr >= grp_size);

	/* XXX, consistent hash? */
	grp_idx = hash % (obj->cob_shards_nr / grp_size);
	D_RWLOCK_UNLOCK(&obj->cob_lock);

	return grp_idx;
}

static int
obj_dkey2shard(struct dc_object *obj, uint64_t hash, unsigned int map_ver,
	       uint32_t op, bool to_leader)
{
	uint64_t	time = 0;
	int		grp_idx;
	int		grp_size;
	int		idx;

	grp_idx = obj_dkey2grpidx(obj, hash, map_ver);
	if (grp_idx < 0)
		return grp_idx;

	grp_size = obj_get_grp_size(obj);
	idx = hash % grp_size + grp_idx * grp_size;

	if (!to_leader && obj->cob_time_fetch_leader != NULL &&
	    obj->cob_time_fetch_leader[grp_idx] != 0 &&
	    daos_gettime_coarse(&time) == 0 && OBJ_FETCH_LEADER_INTERVAL >=
	    time - obj->cob_time_fetch_leader[grp_idx])
		to_leader = true;

	if (to_leader)
		return obj_grp_leader_get(obj, idx, map_ver);

	return obj_grp_valid_shard_get(obj, idx, map_ver, op);
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
obj_reasb_req_init(struct obj_reasb_req *reasb_req, daos_iod_t *iods,
		   uint32_t iod_nr, struct daos_oclass_attr *oca)
{
	daos_size_t			 size_iod, size_sgl, size_oiod;
	daos_size_t			 size_recx, size_tgt_nr, size_singv;
	daos_size_t			 size_sorter, size_array, buf_size;
	daos_iod_t			*uiod, *riod;
	struct obj_ec_recx_array	*ec_recx;
	void				*buf;
	uint8_t				*tmp_ptr;
	int				 i;

	reasb_req->orr_oca = oca;
	size_iod = roundup(sizeof(daos_iod_t) * iod_nr, 8);
	size_sgl = roundup(sizeof(d_sg_list_t) * iod_nr, 8);
	size_oiod = roundup(sizeof(struct obj_io_desc) * iod_nr, 8);
	size_recx = roundup(sizeof(struct obj_ec_recx_array) * iod_nr, 8);
	size_sorter = roundup(sizeof(struct obj_ec_seg_sorter) * iod_nr, 8);
	size_singv = roundup(sizeof(struct dcs_layout) * iod_nr, 8);
	size_array = sizeof(daos_size_t) * obj_ec_tgt_nr(oca) * iod_nr;
	/* for oer_tgt_recx_nrs/_idxs */
	size_tgt_nr = roundup(sizeof(uint32_t) * obj_ec_tgt_nr(oca), 8);
	buf_size = size_iod + size_sgl + size_oiod + size_recx + size_sorter +
		   size_singv + size_array +
		   size_tgt_nr * iod_nr * 2 + OBJ_TGT_BITMAP_LEN;
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

	return 0;
}

void
obj_reasb_req_fini(struct obj_reasb_req *reasb_req, uint32_t iod_nr)
{
	daos_iod_t			*iod;
	int				 i;

	for (i = 0; i < iod_nr; i++) {
		iod = reasb_req->orr_iods + i;
		if (iod == NULL)
			return;
		if (iod->iod_recxs != NULL)
			D_FREE(iod->iod_recxs);
		daos_sgl_fini(reasb_req->orr_sgls + i, false);
		obj_io_desc_fini(reasb_req->orr_oiods + i);
		obj_ec_recxs_fini(&reasb_req->orr_recxs[i]);
		obj_ec_seg_sorter_fini(&reasb_req->orr_sorters[i]);
		obj_ec_tgt_oiod_fini(reasb_req->tgt_oiods);
		reasb_req->tgt_oiods = NULL;
	}
	obj_ec_fail_info_free(reasb_req);
	D_FREE(reasb_req->orr_iods);
}

static int
obj_rw_req_reassemb(struct dc_object *obj, daos_obj_rw_t *args,
		    struct dtx_epoch *epoch, struct obj_auxi_args *obj_auxi)
{
	struct obj_reasb_req	*reasb_req = &obj_auxi->reasb_req;
	daos_obj_id_t		 oid = obj->cob_md.omd_id;
	struct daos_oclass_attr	*oca;
	int			 rc = 0;

	if (epoch != NULL)
		reasb_req->orr_epoch = *epoch;
	if (obj_auxi->req_reasbed && !reasb_req->orr_size_fetch) {
		D_DEBUG(DB_TRACE, DF_OID" req reassembled (retry case).\n",
			DP_OID(oid));
		return 0;
	}
	obj_auxi->iod_nr = args->nr;

	if (args->extra_flags & DIOF_CHECK_EXISTENCE)
		return 0;

	/** XXX possible re-order/merge for both replica and EC */
	if (!daos_oclass_is_ec(oid, &oca))
		return 0;

	if (!obj_auxi->req_reasbed) {
		obj_auxi->is_ec_obj = 1;
		rc = obj_reasb_req_init(&obj_auxi->reasb_req, args->iods,
					args->nr, oca);
		if (rc) {
			D_ERROR(DF_OID" obj_reasb_req_init failed %d.\n",
				DP_OID(oid), rc);
			return rc;
		}
	}

	rc = obj_ec_req_reasb(args->iods, args->sgls, oid, oca, reasb_req,
			      args->nr, obj_auxi->opc == DAOS_OBJ_RPC_UPDATE);
	if (rc == 0) {
		obj_auxi->flags |= ORF_DTX_SYNC;
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
		     uint16_t ec_tgt_idx, struct daos_shard_tgt *shard_tgt,
		     struct obj_auxi_args *obj_auxi)
{
	struct dc_obj_shard	*obj_shard;
	bool			 ec_degrade = false;
	uint32_t		 ec_deg_tgt = 0, start_shard;
	int			 rc;

	shard_tgt->st_ec_tgt = ec_tgt_idx;
	start_shard = shard - ec_tgt_idx;
shard_open:
	rc = obj_shard_open(obj, shard, map_ver, &obj_shard);
	if (obj_auxi->opc == DAOS_OBJ_RPC_FETCH &&
	    DAOS_FAIL_CHECK(DAOS_FAIL_SHARD_FETCH) &&
	    daos_shard_in_fail_value(shard + 1)) {
		rc = -DER_NONEXIST;
		D_ERROR("obj_shard_open failed on shard %d, "DF_RC"\n",
			shard, DP_RC(rc));
	}

	if (rc == 0) {
		if (obj_op_is_ec_fetch(obj_auxi) && obj_shard->do_rebuilding) {
			ec_degrade = true;
			obj_shard_close(obj_shard);
		}
	} else {
		if (rc == -DER_NONEXIST) {
			if (!obj_auxi->is_ec_obj) {
				shard_tgt->st_rank = DAOS_TGT_IGNORE;
				rc = 0;
			} else {
				if (obj_auxi->opc == DAOS_OBJ_RPC_FETCH)
					ec_degrade = true;
			}
		} else {
			D_ERROR(DF_OID" obj_shard_open %u, rc "DF_RC".\n",
				DP_OID(obj->cob_md.omd_id), shard, DP_RC(rc));
		}
		if (!ec_degrade)
			D_GOTO(out, rc);
	}

	if (ec_degrade) {
		rc = obj_ec_get_degrade(&obj_auxi->reasb_req,
					shard - start_shard, &ec_deg_tgt,
					false);
		if (rc) {
			D_ERROR(DF_OID" obj_ec_get_degrade failed, rc "
				DF_RC".\n", DP_OID(obj->cob_md.omd_id),
				DP_RC(rc));
			D_GOTO(out, rc);
		}
		D_ASSERT(ec_deg_tgt >=
			 obj_ec_data_tgt_nr(obj_auxi->reasb_req.orr_oca));
		if (obj_auxi->ec_in_recov ||
		    obj_auxi->reasb_req.orr_singv_only) {
			D_DEBUG(DB_IO, DF_OID" shard %d failed in recovery(%d) "
				"or singv fetch(%d).\n",
				DP_OID(obj->cob_md.omd_id), shard,
				obj_auxi->ec_in_recov,
				obj_auxi->reasb_req.orr_singv_only);
			D_GOTO(out, rc = -DER_BAD_TARGET);
		}
		shard = start_shard + ec_deg_tgt;
		D_DEBUG(DB_IO, DF_OID" shard %d fetch re-direct to shard %d.\n",
			DP_OID(obj->cob_md.omd_id), start_shard + ec_tgt_idx,
			start_shard + ec_deg_tgt);
		ec_degrade = false;
		goto shard_open;
	}
	if (rc != 0)
		D_GOTO(out, rc);

	shard_tgt->st_rank	= obj_shard->do_target_rank;
	shard_tgt->st_shard	= shard,
	shard_tgt->st_tgt_idx	= obj_shard->do_target_idx;
	rc = obj_shard2tgtid(obj, shard, map_ver, &shard_tgt->st_tgt_id);
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

static int
obj_shards_2_fwtgts(struct dc_object *obj, uint32_t map_ver, uint8_t *bit_map,
		    uint32_t start_shard, uint32_t shard_cnt, uint32_t grp_nr,
		    uint32_t flags, struct obj_auxi_args *obj_auxi)
{
	struct obj_req_tgts	*req_tgts = &obj_auxi->req_tgts;
	struct daos_shard_tgt	*tgt = NULL;
	uint32_t		 leader_shard = -1;
	uint32_t		 i, j;
	uint32_t		 shard_idx, shard_nr, grp_size;
	bool			 cli_disp = flags & OBJ_TGT_FLAG_CLI_DISPATCH;
	int			 rc;

	D_ASSERT(shard_cnt >= 1);
	grp_size = shard_cnt / grp_nr;
	D_ASSERT(grp_size * grp_nr == shard_cnt);
	if (cli_disp || bit_map != NIL_BITMAP)
		D_ASSERT(grp_nr == 1);
	req_tgts->ort_start_shard = start_shard;
	req_tgts->ort_srv_disp = (srv_io_mode != DIM_CLIENT_DISPATCH) &&
				  !cli_disp && grp_size > 1;

	if (bit_map != NIL_BITMAP) {
		shard_nr = 0;
		for (i = 0; i < shard_cnt; i++)
			if (isset(bit_map, i))
				shard_nr++;
	} else {
		shard_nr = shard_cnt;
	}

	if (shard_nr > OBJ_TGT_INLINE_NR) {
		if (req_tgts->ort_shard_tgts != NULL &&
		    req_tgts->ort_grp_nr * req_tgts->ort_grp_size != shard_nr) {
			/* shard_nr possibly changed per progressive layout */
			if (req_tgts->ort_shard_tgts !=
				req_tgts->ort_tgts_inline)
				D_FREE(req_tgts->ort_shard_tgts);
			req_tgts->ort_shard_tgts = NULL;
		}
		if (req_tgts->ort_shard_tgts == NULL) {
			D_ALLOC_ARRAY(req_tgts->ort_shard_tgts, shard_nr);
			if (req_tgts->ort_shard_tgts == NULL)
				return -DER_NOMEM;
		}
	} else {
		if (req_tgts->ort_shard_tgts != NULL &&
		    req_tgts->ort_shard_tgts != req_tgts->ort_tgts_inline)
			D_FREE(req_tgts->ort_shard_tgts);
		req_tgts->ort_shard_tgts = req_tgts->ort_tgts_inline;
	}
	req_tgts->ort_grp_nr = grp_nr;
	req_tgts->ort_grp_size = (shard_nr == shard_cnt) ? grp_size : shard_nr;
	for (i = 0; i < grp_nr; i++) {
		shard_idx = start_shard + i * grp_size;
		tgt = req_tgts->ort_shard_tgts + i * grp_size;
		if (req_tgts->ort_srv_disp) {
			rc = obj_grp_leader_get(obj, shard_idx, map_ver);
			if (rc < 0) {
				D_ERROR(DF_OID" no valid shard, rc "DF_RC".\n",
					DP_OID(obj->cob_md.omd_id),
					DP_RC(rc));
				return rc;
			}
			leader_shard = rc;
			rc = obj_shard_tgts_query(obj, map_ver, leader_shard,
						  0, tgt++, obj_auxi);
			if (rc != 0)
				return rc;

			if (flags & OBJ_TGT_FLAG_LEADER_ONLY)
				continue;
		}

		for (j = 0; j < grp_size; j++, shard_idx++) {
			if (shard_idx == leader_shard ||
			    (bit_map != NIL_BITMAP && isclr(bit_map, j)))
				continue;
			rc = obj_shard_tgts_query(obj, map_ver, shard_idx,
						  shard_idx - start_shard,
						  tgt++, obj_auxi);
			if (rc != 0)
				return rc;
		}
	}

	if (flags == 0 && bit_map == NIL_BITMAP)
		D_ASSERT(tgt == req_tgts->ort_shard_tgts + shard_nr);

	return 0;
}

static void
obj_ptr2shards(struct dc_object *obj, uint32_t *start_shard, uint32_t *shard_nr,
	       uint32_t *grp_nr)
{
	*start_shard = 0;
	*shard_nr = obj->cob_shards_nr;
	*grp_nr = obj->cob_shards_nr / obj_get_grp_size(obj);
}

static int
obj_ptr2poh(struct dc_object *obj, daos_handle_t *ph)
{
	daos_handle_t   coh;

	coh = obj->cob_coh;
	if (daos_handle_is_inval(coh))
		return -DER_NO_HDL;

	*ph = dc_cont_hdl2pool_hdl(coh);
	if (daos_handle_is_inval(*ph))
		return -DER_NO_HDL;

	return 0;
}

/* Get pool map version from object handle */
static int
obj_ptr2pm_ver(struct dc_object *obj, unsigned int *map_ver)
{
	*map_ver = obj->cob_version;
	return 0;
}

static int
obj_pool_query_cb(tse_task_t *task, void *data)
{
	struct dc_object	*obj = *((struct dc_object **)data);
	daos_pool_query_t	*args;

	if (task->dt_result != 0)
		D_DEBUG(DB_IO, "obj_pool_query_cb task=%p result=%d\n",
			task, task->dt_result);

	obj_layout_refresh(obj);
	obj_decref(obj);

	args = dc_task_get_args(task);
	D_FREE(args->info);
	return 0;
}

int
obj_pool_query_task(tse_sched_t *sched, struct dc_object *obj,
		    tse_task_t **taskp)
{
	tse_task_t		*task;
	daos_pool_query_t	*args;
	daos_handle_t		 ph;
	int			 rc = 0;

	rc = obj_ptr2poh(obj, &ph);
	if (rc != 0)
		return rc;

	rc = dc_task_create(dc_pool_query, sched, NULL, &task);
	if (rc != 0)
		return rc;

	args = dc_task_get_args(task);
	args->poh = ph;
	D_ALLOC_PTR(args->info);
	if (args->info == NULL)
		D_GOTO(err, rc = -DER_NOMEM);

	obj_addref(obj);
	rc = dc_task_reg_comp_cb(task, obj_pool_query_cb, &obj, sizeof(obj));
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(err, rc);
	}

	*taskp = task;
	return 0;
err:
	dc_task_decref(task);
	if (args->info)
		D_FREE(args->info);

	return rc;
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

int
dc_obj_open(tse_task_t *task)
{
	daos_obj_open_t		*args;
	struct dc_object	*obj;
	int			rc;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_alloc();
	if (obj == NULL)
		return -DER_NOMEM;

	obj->cob_coh  = args->coh;
	obj->cob_mode = args->mode;

	rc = D_SPIN_INIT(&obj->cob_spin, PTHREAD_PROCESS_PRIVATE);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = D_RWLOCK_INIT(&obj->cob_lock, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	/* it is a local operation for now, does not require event */
	rc = dc_obj_fetch_md(args->oid, &obj->cob_md);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = obj_ptr2pm_ver(obj, &obj->cob_md.omd_ver);
	if (rc)
		D_GOTO(out, rc);

	rc = obj_layout_create(obj, false);
	if (rc != 0)
		D_GOTO(out, rc);

	obj_hdl_link(obj);
	*args->oh = obj_ptr2hdl(obj);

out:
	obj_decref(obj);
	tse_task_complete(task, rc);
	return rc;
}

int
dc_obj_close(tse_task_t *task)
{
	daos_obj_close_t	*args;
	struct dc_object	*obj;
	int			 rc = 0;

	args = dc_task_get_args(task);
	D_ASSERTF(args != NULL, "Task Argument OPC does not match DC OPC\n");

	obj = obj_hdl2ptr(args->oh);
	if (obj == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	obj_hdl_unlink(obj);
	obj_decref(obj);

out:
	tse_task_complete(task, rc);
	return 0;
}

int
dc_obj_fetch_md(daos_obj_id_t oid, struct daos_obj_md *md)
{
	/* For predefined object classes, do nothing at here. But for those
	 * customized classes, we need to fetch for the remote OI table.
	 */
	memset(md, 0, sizeof(*md));
	md->omd_id = oid;
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
			grp_size * sizeof(uint32_t));
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

	oc_attr = daos_oclass_attr_find(obj->cob_md.omd_id);
	D_ASSERT(oc_attr != NULL);
	grp_size = daos_oclass_grp_size(oc_attr);
	grp_nr = daos_oclass_grp_nr(oc_attr, &obj->cob_md);
	if (grp_nr == DAOS_OBJ_GRP_MAX)
		grp_nr = obj->cob_shards_nr / grp_size;

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

			rc = dc_cont_tgt_idx2ptr(obj->cob_coh,
						 obj_shard->do_target_id, &tgt);
			if (rc != 0)
				D_GOTO(out, rc);

			shard->os_ranks[j] = tgt->ta_comp.co_rank;
		}
	}
	*p_layout = layout;
out:
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

static int
obj_retry_cb(tse_task_t *task, struct dc_object *obj,
	     struct obj_auxi_args *obj_auxi, bool pmap_stale)
{
	tse_sched_t	 *sched = tse_task2sched(task);
	tse_task_t	 *pool_task = NULL;
	int		  result = task->dt_result;
	int		  rc;
	bool		  keep_result = false;

	if (pmap_stale) {
		rc = obj_pool_query_task(sched, obj, &pool_task);
		if (rc != 0)
			D_GOTO(err, rc);
	}

	if (obj_auxi->io_retry) {
		/* Let's reset task result before retry */
		rc = dc_task_resched(task);
		if (rc != 0) {
			D_ERROR("Failed to re-init task (%p)\n", task);
			D_GOTO(err, rc);
		}

		if (pool_task != NULL) {
			rc = dc_task_depend(task, 1, &pool_task);
			if (rc != 0) {
				D_ERROR("Failed to add dependency on pool "
					 "query task (%p)\n", pool_task);
				D_GOTO(err, rc);
			}
		}
	} else if (obj_auxi->spec_shard) {
		/* If the RPC sponsor has specifies the shard, we will NOT
		 * reschedule the IO, but not prevent the pool map refresh.
		 * Restore the original RPC (task) result, the RPC sponsor
		 * will handle that by itself.
		 */
		keep_result = true;
	}

	D_DEBUG(DB_IO, "Retrying task=%p for err=%d, io_retry=%d\n",
		 task, result, obj_auxi->io_retry);

	if (pool_task != NULL)
		/* ignore returned value, error is reported by comp_cb */
		dc_task_schedule(pool_task, obj_auxi->io_retry);

	if (keep_result)
		task->dt_result = result;

	return 0;
err:
	if (pool_task)
		dc_task_decref(pool_task);

	task->dt_result = result; /* restore the original error */
	obj_auxi->io_retry = 0;
	D_ERROR("Failed to retry task=%p(err=%d), io_retry=%d, rc "DF_RC".\n",
		task, result, obj_auxi->io_retry, DP_RC(rc));
	return rc;
}

static int
recov_task_abort(tse_task_t *task, void *arg)
{
	int	rc = *((int *)arg);

	tse_task_complete(task, rc);
	return 0;
}

static void
obj_ec_recov_cb(tse_task_t *task, struct dc_object *obj,
		struct obj_auxi_args *obj_auxi)
{
	struct obj_reasb_req		*reasb_req = &obj_auxi->reasb_req;
	struct obj_ec_fail_info		*fail_info = reasb_req->orr_fail;
	daos_obj_fetch_t		*args = dc_task_get_args(task);
	tse_sched_t			*sched = tse_task2sched(task);
	struct obj_ec_recov_task	*recov_task;
	tse_task_t			*sub_task = NULL;
	daos_handle_t			 coh = obj->cob_coh;
	daos_handle_t			 th = DAOS_HDL_INVAL;
	d_list_t			 task_list;
	uint32_t			 i;
	int				 rc;

	rc = obj_ec_recov_prep(&obj_auxi->reasb_req, obj->cob_md.omd_id,
			       args->iods, args->nr);
	if (rc) {
		D_ERROR("task %p "DF_OID" obj_ec_recov_prep failed "DF_RC".\n",
			task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
		goto out;
	}

	D_ASSERT(fail_info->efi_recov_ntasks > 0 &&
		 fail_info->efi_recov_tasks != NULL);
	D_INIT_LIST_HEAD(&task_list);
	for (i = 0; i < fail_info->efi_recov_ntasks; i++) {
		recov_task = &fail_info->efi_recov_tasks[i];
		rc = dc_tx_local_open(coh, recov_task->ert_epoch, 0, &th);
		if (rc) {
			D_ERROR("task %p "DF_OID" dc_tx_local_open failed "
				DF_RC".\n", task, DP_OID(obj->cob_md.omd_id),
				DP_RC(rc));
			goto out;
		}
		recov_task->ert_th = th;

		rc = dc_obj_fetch_task_create(args->oh, th, 0, args->dkey, 1,
					DIOF_EC_RECOV, &recov_task->ert_iod,
					&recov_task->ert_sgl, NULL, fail_info,
					NULL, sched, &sub_task);
		if (rc) {
			D_ERROR("task %p "DF_OID" dc_obj_fetch_task_create "
				"failed "DF_RC".\n", task,
				DP_OID(obj->cob_md.omd_id), DP_RC(rc));
			goto out;
		}

		tse_task_list_add(sub_task, &task_list);

		rc = dc_task_depend(task, 1, &sub_task);
		if (rc != 0) {
			D_ERROR("task %p "DF_OID" dc_task_depend failed "DF_RC
				".\n", task, DP_OID(obj->cob_md.omd_id),
				DP_RC(rc));
			goto out;
		}
	}

	rc = dc_task_resched(task);
	if (rc != 0) {
		D_ERROR("task %p "DF_OID" dc_task_resched failed "DF_RC".\n",
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
		D_ERROR("task %p "DF_OID" EC recovery failed "DF_RC".\n",
			task, DP_OID(obj->cob_md.omd_id), DP_RC(rc));
	}
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

		D_ERROR("%s obj_bulk_prep failed "DF_RC".\n",
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

	if ((obj_auxi->io_retry && !obj_auxi->reasb_req.orr_size_fetched) ||
	    obj_auxi->reasb_req.orr_size_fetch)
		return 0;

	/* inline fetch needs to pack sgls buffer into RPC so uses it to check
	 * if need bulk transferring.
	 */
	sgls_size = daos_sgls_packed_size(sgls, nr, NULL);
	if (sgls_size >= OBJ_BULK_LIMIT || obj_auxi->reasb_req.orr_tgt_nr > 1) {
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
	d_iov_t		key;
	int			idx;
	bool			overlapped;
	struct btr_root		broot = { 0 };
	int			rc;

	if (nr == 0 || recxs == NULL)
		return false;
	if (nr == 1)
		return true;

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
obj_iod_sgl_valid(unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
		  bool update, bool size_fetch)
{
	int	i, j;
	int	rc;

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
		for (j = 0; j < iods[i].iod_nr; j++) {
			if (iods[i].iod_recxs != NULL &&
			   (iods[i].iod_recxs[j].rx_idx & PARITY_INDICATOR)
			    != 0) {
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
				if (!update || iods[i].iod_size == DAOS_REC_ANY)
					continue;
				D_ERROR("invalid update req with NULL sgl\n");
				return -DER_INVAL;
			}
			if (!size_fetch &&
			    !obj_recx_valid(iods[i].iod_nr, iods[i].iod_recxs,
					    update)) {
				D_ERROR("IOD_ARRAY should have valid recxs\n");
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
	daos_ofeat_t ofeat = daos_obj_id2feat(oid);

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
		if (!(ofeat & DAOS_OF_DKEY_UINT64)) {
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
		if (!(ofeat & DAOS_OF_AKEY_UINT64)) {
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

/* check if the obj request is valid */
static int
obj_req_valid(tse_task_t *task, void *args, int opc, struct dtx_epoch *epoch,
	      uint32_t *pm_ver, struct dc_object **p_obj, bool check_existence)
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
		bool			 size_fetch;

		size_fetch = obj_auxi->reasb_req.orr_size_fetch;
		if ((!obj_auxi->io_retry && !obj_auxi->req_reasbed) ||
		    size_fetch) {
			if (f_args->dkey == NULL ||
			    f_args->dkey->iov_buf == NULL ||
			    (f_args->nr == 0 && !check_existence)) {
				D_ERROR("Invalid fetch parameter.\n");
				D_GOTO(out, rc = -DER_INVAL);
			}

			rc = obj_iod_sgl_valid(f_args->nr, f_args->iods,
					       f_args->sgls, false, size_fetch);
			if (rc)
				goto out;
		}
		oh = f_args->oh;
		th = f_args->th;
		break;
	}
	case DAOS_OBJ_RPC_UPDATE: {
		daos_obj_update_t	*u_args = args;

		if (!obj_auxi->io_retry && !obj_auxi->req_reasbed) {
			if (u_args->dkey == NULL ||
			    u_args->dkey->iov_buf == NULL || u_args->nr == 0) {
				D_ERROR("Invalid update parameter.\n");
				D_GOTO(out, rc = -DER_INVAL);
			}

			rc = obj_iod_sgl_valid(u_args->nr, u_args->iods,
					       u_args->sgls, true, false);
			if (rc)
				D_GOTO(out, rc);
		}

		oh = u_args->oh;
		th = u_args->th;
		break;
	}
	case DAOS_OBJ_RPC_PUNCH: {
		daos_obj_punch_t *p_args = args;

		oh = p_args->oh;
		th = p_args->th;
		break;
	}
	case DAOS_OBJ_RPC_PUNCH_DKEYS: {
		daos_obj_punch_t *p_args = args;

		if (p_args->dkey == NULL) {
			D_ERROR("NULL dkeys\n");
			D_GOTO(out, rc = -DER_INVAL);
		} else if (p_args->dkey[0].iov_buf == NULL ||
			   p_args->dkey[0].iov_len == 0) {
			D_ERROR("invalid dkey (NULL iov_buf or iov_len.\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		oh = p_args->oh;
		th = p_args->th;
		break;
	}

	case DAOS_OBJ_RPC_PUNCH_AKEYS: {
		daos_obj_punch_t *p_args = args;

		if (p_args->dkey == NULL || p_args->dkey->iov_buf == NULL ||
		    p_args->dkey->iov_len == 0) {
			D_ERROR("NULL or invalid dkey\n");
			D_GOTO(out, rc = -DER_INVAL);
		}
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

	if (daos_handle_is_valid(th)) {
		if (!obj_is_modification_opc(opc)) {
			rc = dc_tx_hdl2epoch_and_pmv(th, epoch, &map_ver);
			if (rc != 0)
				D_GOTO(out, rc);
		}
	} else {
		dc_io_epoch_set(epoch);
		D_DEBUG(DB_IO, "set fetch epoch "DF_U64"\n", epoch->oe_value);
	}

	if (map_ver == 0) {
		rc = obj_ptr2pm_ver(obj, &map_ver);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	if (p_obj != NULL)
		*p_obj = obj;
	else
		obj_decref(obj);
	*pm_ver = map_ver;
out:
	if (rc && obj)
		obj_decref(obj);
	tse_task_stack_pop(task, sizeof(*obj_auxi));
	return rc;
}

/* Query the obj request's targets */
static int
obj_req_get_tgts(struct dc_object *obj, int *shard, daos_key_t *dkey,
		 uint64_t dkey_hash, uint8_t *bit_map, uint32_t map_ver,
		 bool to_leader, bool spec_shard,
		 struct obj_auxi_args *obj_auxi)
{
	struct obj_req_tgts	*req_tgts = &obj_auxi->req_tgts;
	enum obj_rpc_opc	opc = obj_auxi->opc;
	uint32_t		shard_idx;
	uint32_t		shard_cnt = 0;
	uint32_t		grp_nr;
	uint32_t		flags = 0;
	int			rc;

	switch (opc) {
	case DAOS_OBJ_RPC_FETCH:
		if (bit_map == NIL_BITMAP) {
			if (spec_shard) {
				D_ASSERT(shard != NULL);

				rc = obj_dkey2grpidx(obj, dkey_hash, map_ver);
				if (rc < 0)
					goto out;

				rc *= obj->cob_grp_size;
				if (*shard < rc ||
				    *shard >= rc + obj->cob_grp_size) {
					D_ERROR("Fetch from invalid shard, "
						"grp size %u, shard cnt %u, "
						"grp idx %u, given shard %u, "
						"dkey hash %lu, dkey %s\n",
						obj->cob_grp_size,
						obj->cob_shards_nr,
						rc / obj->cob_grp_size, *shard,
						dkey_hash,
						(char *)dkey->iov_buf);
					D_GOTO(out, rc = -DER_INVAL);
				}

				rc = *shard;
			} else {
				rc = obj_dkey2shard(obj, dkey_hash, map_ver,
						    opc, to_leader);
				if (rc < 0)
					goto out;
			}
			shard_idx = rc;
			shard_cnt = 1;
		} else {
			rc = obj_dkey2grpmemb(obj, dkey_hash, map_ver,
					      &shard_idx, &shard_cnt);
			if (rc != 0)
				goto out;
		}
		grp_nr = 1;
		flags = OBJ_TGT_FLAG_CLI_DISPATCH;
		break;
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		if (opc == DAOS_OBJ_RPC_PUNCH) {
			obj_ptr2shards(obj, &shard_idx, &shard_cnt, &grp_nr);
		} else {
			grp_nr = 1;
			rc = obj_dkey2grpmemb(obj, dkey_hash, map_ver,
					      &shard_idx, &shard_cnt);
			if (rc != 0)
				goto out;
			D_DEBUG(DB_TRACE, "shard_idx %d shard_cnt %d\n",
				(int)shard_idx, (int)shard_cnt);
		}
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
		shard_cnt = 1;
		grp_nr = 1;
		D_ASSERT(shard != NULL);
		D_ASSERT((int)*shard != -1);
		shard_idx = *shard;
		if (obj_auxi->is_ec_obj && !to_leader) {
			struct daos_oclass_attr	*oca;

			/* Client dispatch for EC recx enumeration */
			flags = OBJ_TGT_FLAG_CLI_DISPATCH;
			oca = daos_oclass_attr_find(obj->cob_md.omd_id);
			D_ASSERT(oca != NULL);
			shard_cnt = obj_ec_data_tgt_nr(oca);
		}
		req_tgts->ort_shard_tgts = req_tgts->ort_tgts_inline;
		req_tgts->ort_srv_disp = 0;
		rc = obj_shard_tgts_query(obj, map_ver, *shard, 0,
					  req_tgts->ort_shard_tgts, obj_auxi);
		if (rc != 0)
			goto out;
		break;
	case DAOS_OBJ_RPC_SYNC:
		obj_ptr2shards(obj, &shard_idx, &shard_cnt, &grp_nr);
		flags = OBJ_TGT_FLAG_LEADER_ONLY;
		break;
	default:
		D_ERROR("bad opc %d.\n", opc);
		rc = -DER_INVAL;
		D_GOTO(out, rc);
	}

	rc = obj_shards_2_fwtgts(obj, map_ver, bit_map, shard_idx,
				 shard_cnt, grp_nr, flags, obj_auxi);
	if (rc != 0)
		D_ERROR("opc %d "DF_OID", obj_shards_2_fwtgts failed "
			""DF_RC".\n", opc, DP_OID(obj->cob_md.omd_id),
			DP_RC(rc));
out:
	return rc;
}

static int
shard_task_abort(tse_task_t *task, void *arg)
{
	int	rc = *((int *)arg);

	tse_task_complete(task, rc);
	tse_task_list_del(task);
	tse_task_decref(task);
	return 0;
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
	if (obj_auxi->io_retry && !obj_auxi->new_shard_tasks) {
		/* For retried IO, check if the shard's target changed after
		 * pool map query. If match then need not do anything, if
		 * mismatch then need to re-schedule the shard IO on the new
		 * pool map.
		 * Also retry the shard IO if it got retryable error last time.
		 */
		rc = obj_shard2tgtid(shard_auxi->obj, shard_auxi->shard,
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
			rc = tse_task_reinit(task);
			if (rc != 0)
				goto out;

			rc = tse_task_register_deps(obj_task, 1, &task);
			if (rc != 0)
				goto out;

			if (!obj_auxi->req_tgts.ort_srv_disp)
				shard_auxi_set_param(shard_auxi, map_ver,
					shard_auxi->shard, target,
					&sched_arg->tsa_epoch,
					shard_auxi->ec_tgt_idx);
			sched_arg->tsa_scheded = true;
		}
	} else {
		tse_task_schedule(task, true);
		sched_arg->tsa_scheded = true;
	}

out:
	if (rc != 0)
		tse_task_complete(task, rc);
	return rc;
}

static void
obj_shard_task_sched(struct obj_auxi_args *obj_auxi, struct dtx_epoch *epoch)
{
	struct shard_task_sched_args	sched_arg;

	D_ASSERT(!d_list_empty(&obj_auxi->shard_task_head));
	sched_arg.tsa_scheded = false;
	sched_arg.tsa_epoch = *epoch;
	tse_task_list_traverse(&obj_auxi->shard_task_head, shard_task_sched,
			       &sched_arg);
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
	case DAOS_OBJ_RPC_SYNC:
		return &obj_auxi->s_args.sa_auxi;
	default:
		D_ERROR("bad opc %d.\n", obj_auxi->opc);
		return NULL;
	};
}

static int
shard_io(tse_task_t *task, struct shard_auxi_args *shard_auxi)
{
	struct dc_object		*obj = shard_auxi->obj;
	struct obj_auxi_args		*obj_auxi = shard_auxi->obj_auxi;
	struct dc_obj_shard		*obj_shard;
	struct obj_req_tgts		*req_tgts;
	struct daos_shard_tgt		*fw_shard_tgts;
	uint32_t			 fw_cnt;
	int				 rc;

	D_ASSERT(obj != NULL);
	rc = obj_shard_open(obj, shard_auxi->shard, shard_auxi->map_ver,
			    &obj_shard);
	if (rc != 0) {
		/* skip a failed target */
		if (rc == -DER_NONEXIST)
			rc = 0;

		tse_task_complete(task, rc);
		return rc;
	}

	shard_auxi->flags = shard_auxi->obj_auxi->flags;
	req_tgts = &shard_auxi->obj_auxi->req_tgts;
	D_ASSERT(shard_auxi->grp_idx < req_tgts->ort_grp_nr);
	fw_shard_tgts = req_tgts->ort_srv_disp ?
			(req_tgts->ort_shard_tgts +
			 shard_auxi->grp_idx * req_tgts->ort_grp_size + 1) :
			 NULL;
	fw_cnt = req_tgts->ort_srv_disp ? (req_tgts->ort_grp_size - 1) : 0;
	rc = shard_auxi->shard_io_cb(obj_shard, obj_auxi->opc, shard_auxi,
				     fw_shard_tgts, fw_cnt, task);
	obj_shard_close(obj_shard);

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
	 * task. (See dc_tx_get_epoch.) Because tse_task_reinit is less
	 * practical in the middle of a task, we do it here at the beginning of
	 * shard_io_task.
	 */
	th = shard_auxi->obj_auxi->th;
	if (daos_handle_is_valid(th) && !dtx_epoch_chosen(&shard_auxi->epoch)) {
		rc = dc_tx_get_epoch(task, th, &shard_auxi->epoch);
		if (rc < 0)
			return rc;
		if (rc == DC_TX_GE_REINIT)
			return tse_task_reinit(task);
	}

	return shard_io(task, shard_auxi);
}

typedef int (*shard_io_prep_cb_t)(struct shard_auxi_args *shard_auxi,
				  struct dc_object *obj,
				  struct obj_auxi_args *obj_auxi,
				  uint64_t dkey_hash, uint32_t grp_idx);

struct shard_task_reset_arg {
	struct obj_req_tgts	*req_tgts;
	struct dtx_epoch	 epoch;
};

static int
shard_task_reset_param(tse_task_t *shard_task, void *arg)
{
	struct shard_task_reset_arg	*reset_arg = arg;
	struct obj_req_tgts		*req_tgts = reset_arg->req_tgts;
	struct obj_auxi_args		*obj_auxi;
	struct shard_auxi_args		*shard_arg;
	struct daos_shard_tgt		*leader_tgt;

	shard_arg = tse_task_buf_embedded(shard_task, sizeof(*shard_arg));
	D_ASSERT(shard_arg->grp_idx < req_tgts->ort_grp_nr);
	obj_auxi = container_of(req_tgts, struct obj_auxi_args, req_tgts);
	leader_tgt = req_tgts->ort_shard_tgts +
		     shard_arg->grp_idx * req_tgts->ort_grp_size;
	shard_auxi_set_param(shard_arg, obj_auxi->map_ver_req,
			     leader_tgt->st_shard, leader_tgt->st_tgt_id,
			     &reset_arg->epoch, leader_tgt->st_ec_tgt);
	return 0;
}

static int
obj_req_fanout(struct dc_object *obj, struct obj_auxi_args *obj_auxi,
	       uint64_t dkey_hash, uint32_t map_ver, struct dtx_epoch *epoch,
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

	/* for retried obj IO, reuse the previous shard tasks and resched it */
	if (obj_auxi->io_retry && obj_auxi->args_initialized &&
	    !obj_auxi->new_shard_tasks) {
		/* We mark the RPC as RESEND although @io_retry does not
		 * guarantee that the RPC has ever been sent. It may cause
		 * some overhead on server side, but no correctness issues.
		 *
		 * On the other hand, the client may resend the RPC to new
		 * shard if leader switched. That is why the resend logic
		 * is handled at object layer rather than shard layer.
		 */
		obj_auxi->flags |= ORF_RESEND;

		/* if with shard task list, reuse it and re-schedule */
		if (!d_list_empty(task_list)) {
			struct shard_task_reset_arg reset_arg;

			reset_arg.req_tgts = req_tgts;
			reset_arg.epoch = *epoch;
			/* For srv dispatch, the task_list non-empty is only for
			 * obj punch that with multiple RDG that each with a
			 * leader. Here reset the header for the shard task.
			 */
			if (req_tgts->ort_srv_disp) {
				D_ASSERT(obj_auxi->opc == DAOS_OBJ_RPC_PUNCH);
				tse_task_list_traverse(task_list,
					shard_task_reset_param, &reset_arg);
			}
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
					     tgt->st_ec_tgt);
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
				     tgt->st_tgt_id, epoch, tgt->st_ec_tgt);
		shard_auxi->grp_idx = 0;
		shard_auxi->start_shard = req_tgts->ort_start_shard;
		shard_auxi->obj = obj;
		shard_auxi->obj_auxi = obj_auxi;
		shard_auxi->shard_io_cb = io_cb;
		rc = io_prep_cb(shard_auxi, obj, obj_auxi, dkey_hash,
				shard_auxi->grp_idx);
		if (rc)
			goto out_task;

		obj_auxi->args_initialized = 1;

		/* for fail case the obj_task will be completed in shard_io() */
		rc = shard_io(obj_task, shard_auxi);
		return rc;
	}

	D_ASSERT(d_list_empty(task_list));

	/* for multi-targets, schedule it by tse sub-shard-tasks */
	for (i = 0; i < tgts_nr; i++) {
		rc = tse_task_create(shard_io_task, tse_task2sched(obj_task),
				     NULL, &shard_task);
		if (rc != 0)
			goto out_task;

		shard_auxi = tse_task_buf_embedded(shard_task,
						   sizeof(*shard_auxi));
		shard_auxi_set_param(shard_auxi, map_ver, tgt->st_shard,
				     tgt->st_tgt_id, epoch, tgt->st_ec_tgt);
		shard_auxi->grp_idx = req_tgts->ort_srv_disp ? i :
				      (i / req_tgts->ort_grp_size);
		shard_auxi->start_shard = req_tgts->ort_start_shard;
		shard_auxi->obj = obj;
		shard_auxi->obj_auxi = obj_auxi;
		shard_auxi->shard_io_cb = io_cb;
		rc = io_prep_cb(shard_auxi, obj, obj_auxi, dkey_hash,
				shard_auxi->grp_idx);
		if (rc) {
			tse_task_complete(shard_task, rc);
			goto out_task;
		}

		rc = tse_task_register_deps(obj_task, 1, &shard_task);
		if (rc != 0) {
			tse_task_complete(shard_task, rc);
			goto out_task;
		}
		/* decref and delete from head at shard_task_remove */
		tse_task_addref(shard_task);
		tse_task_list_add(shard_task, task_list);
		if (req_tgts->ort_srv_disp)
			tgt += req_tgts->ort_grp_size;
		else
			tgt++;
	}

	obj_auxi->args_initialized = 1;

task_sched:
	obj_shard_task_sched(obj_auxi, epoch);
	return 0;

out_task:
	if (!d_list_empty(task_list)) {
		D_ASSERTF(!obj_retry_error(rc), "unexpected ret "DF_RC"\n",
			DP_RC(rc));

		tse_task_list_traverse(task_list, shard_task_abort, &rc);
	}

	tse_task_complete(obj_task, rc);

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
	struct daos_csummer	*csummer = dc_cont_hdl2csummer(obj->cob_coh);

	if (!daos_csummer_initialized(csummer))
		return;
	daos_csummer_free_ci(csummer, &obj_auxi->rw_args.dkey_csum);
	daos_csummer_free_ic(csummer, &obj_auxi->rw_args.iod_csums);
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
	shard_arg->la_recxs = NULL;
	shard_arg->la_kds = NULL;
	if (shard_arg->la_sgl != NULL && shard_arg->la_sgl != obj_arg->sgl)
		daos_sgl_fini(shard_arg->la_sgl, false);

	return 0;
}

static void
obj_auxi_list_fini(struct obj_auxi_args *obj_auxi)
{
	tse_task_list_traverse(&obj_auxi->shard_task_head,
			       shard_list_task_fini, obj_auxi);
}

struct comp_cb_merge_arg {
	d_list_t	merge_list;
	int		merge_nr;
	daos_off_t	merge_sgl_off;
};

static int
merge_recx_insert(d_list_t *prev, uint64_t offset, uint64_t size)
{
	struct obj_auxi_list_recx *new;

	D_ALLOC_PTR(new);
	if (new == NULL)
		return -DER_NOMEM;

	new->recx.rx_idx = offset;
	new->recx.rx_nr = size;
	D_INIT_LIST_HEAD(&new->recx_list);
	d_list_add(&new->recx_list, prev);
	return 0;
}

int
merge_recx(d_list_t *head, uint64_t offset, uint64_t size)
{
	struct obj_auxi_list_recx	*recx;
	struct obj_auxi_list_recx	*new_recx = NULL;
	struct obj_auxi_list_recx	*tmp;
	struct obj_auxi_list_recx	*prev = NULL;
	bool				inserted = false;
	daos_off_t			end = offset + size;
	int				rc = 0;

	d_list_for_each_entry_safe(recx, tmp, head, recx_list) {
		daos_off_t recx_start = recx->recx.rx_idx;
		daos_off_t recx_end = recx->recx.rx_idx + recx->recx.rx_nr;

		D_DEBUG(DB_TRACE, "current "DF_U64"/"DF_U64"\n", recx_start, recx_end);
		if (end < recx_start) {
			if (!inserted) {
				rc = merge_recx_insert(prev == NULL ?
						       head : &prev->recx_list,
						       offset, size);
				inserted = true;
			}
			break;
		}

		/* merge with current recx, and try to merge with next recxs */
		if (max(recx_start, offset) <= min(recx_end, end)) {
			if (new_recx == NULL)
				new_recx = recx;

			new_recx->recx.rx_idx = min(recx_start, offset);
			new_recx->recx.rx_nr = max(recx_end, end) -
					       new_recx->recx.rx_idx;
			D_DEBUG(DB_TRACE, "new "DF_U64"/"DF_U64"\n",
				new_recx->recx.rx_idx, new_recx->recx.rx_nr);
			offset = new_recx->recx.rx_idx;
			end = offset + new_recx->recx.rx_nr;
			D_DEBUG(DB_TRACE, "offset "DF_U64"/"DF_U64"\n", offset, end);
			inserted = true;
			if (recx != new_recx) {
				d_list_del(&recx->recx_list);
				D_FREE_PTR(recx);
			}

		}
		prev = recx;
	}

	if (!inserted)
		rc = merge_recx_insert(prev == NULL ? head : &prev->recx_list,
				       offset, size);

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
obj_ec_recxs_convert(d_list_t *merge_list, daos_recx_t *recx,
		     struct shard_auxi_args *shard_auxi)
{
	struct daos_oclass_attr	*oca;
	uint64_t		total_size = recx->rx_nr;
	uint64_t		cur_off = recx->rx_idx & ~PARITY_INDICATOR;
	int			rc = 0;
	int			cell_nr;
	int			stripe_nr;

	oca = daos_oclass_attr_find(shard_auxi->obj->cob_md.omd_id);
	D_ASSERT(oca != NULL);
	/* Normally the enumeration is sent to the parity node */
	/* convert the parity off to daos off */
	if (recx->rx_idx & PARITY_INDICATOR) {
		obj_recx_parity_to_daos(oca, recx);
		return 0;
	}

	if (merge_list == NULL)
		return 0;

	cell_nr = obj_ec_cell_rec_nr(oca);
	stripe_nr = obj_ec_stripe_rec_nr(oca);
	/* If all parity nodes are down(degraded mode), then
	 * the enumeration is sent to all data nodes.
	 */
	while (total_size > 0) {
		uint64_t daos_off;
		uint64_t data_size;

		daos_off = obj_ec_idx_vos2daos(cur_off, stripe_nr, cell_nr,
					       shard_auxi->shard);
		data_size = min(roundup(cur_off + 1, cell_nr) - cur_off,
				total_size);
		rc = merge_recx(merge_list, daos_off, data_size);
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
	struct comp_cb_merge_arg	*merge_arg = arg;
	struct shard_list_args		*shard_arg;
	int i;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	/* convert recxs for EC object*/
	for (i = 0; i < shard_arg->la_nr; i++) {
		int rc;

		rc = obj_ec_recxs_convert(&merge_arg->merge_list,
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
obj_enum_shard_process_cb(daos_key_desc_t *kds, void *ptr,
			  unsigned int size, void *arg)
{
	struct obj_enum_rec	*rec = ptr;
	struct shard_auxi_args	*shard_auxi = arg;
	int			rc;

	D_ASSERT(size >= sizeof(struct obj_enum_rec));

	rc = obj_ec_recxs_convert(NULL, &rec->rec_recx, shard_auxi);
	if (rc)
		return rc;

	return rc;
}

static int
obj_shard_list_obj_cb(struct shard_auxi_args *shard_auxi,
		      struct obj_auxi_args *obj_auxi, void *arg)
{
	struct comp_cb_merge_arg *merge_arg = arg;
	struct shard_list_args	*shard_arg;
	daos_obj_list_t		*obj_arg = dc_task_get_args(obj_auxi->obj_task);
	char			*ptr = obj_arg->sgl->sg_iovs[0].iov_buf;
	daos_key_desc_t		*kds = obj_arg->kds;
	int			rc;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	rc = obj_enum_iterate(shard_arg->la_kds, shard_arg->la_sgl,
			      shard_arg->la_nr, OBJ_ITER_RECX,
			      obj_enum_shard_process_cb, shard_auxi);
	ptr += merge_arg->merge_sgl_off;
	if (ptr != shard_arg->la_sgl->sg_iovs[0].iov_buf)
		memmove(ptr, shard_arg->la_sgl->sg_iovs[0].iov_buf,
			shard_arg->la_sgl->sg_iovs[0].iov_len);
	obj_arg->sgl->sg_iovs[0].iov_len +=
			shard_arg->la_sgl->sg_iovs[0].iov_len;
	merge_arg->merge_sgl_off += shard_arg->la_sgl->sg_iovs[0].iov_len;

	kds += merge_arg->merge_nr;
	if (kds != shard_arg->la_kds) {
		int i;

		for (i = 0; i < shard_arg->la_nr; i++)
			kds[i] = shard_arg->la_kds[i];
	}
	merge_arg->merge_nr += shard_arg->la_nr;

	D_DEBUG(DB_TRACE, "merge_nr %d\n", merge_arg->merge_nr);
	return rc;
}

static int
merge_key(d_list_t *head, char *key, int key_size)
{
	struct obj_auxi_list_key *key_one;

	d_list_for_each_entry(key_one, head, key_list) {
		if (key_size == key_one->key.iov_len &&
		    strncmp(key_one->key.iov_buf, key, key_size) == 0) {
			return 0;
		}
	}

	D_ALLOC_PTR(key_one);
	if (key_one == NULL)
		return -DER_NOMEM;

	D_ALLOC(key_one->key.iov_buf, key_size);
	if (key_one->key.iov_buf == NULL) {
		D_FREE(key);
		return -DER_NOMEM;
	}

	memcpy(key_one->key.iov_buf, key, key_size);
	key_one->key.iov_buf_len = key_size;
	key_one->key.iov_len = key_size;
	D_INIT_LIST_HEAD(&key_one->key_list);
	d_list_add_tail(head, &key_one->key_list);
	return 0;
}

static int
obj_shard_list_key_cb(struct shard_auxi_args *shard_auxi,
		      struct obj_auxi_args *obj_auxi, void *arg)
{
	daos_obj_list_t		*obj_args;
	struct shard_list_args  *shard_arg;
	struct comp_cb_merge_arg *merge_arg = arg;
	d_sg_list_t		*sgl;
	d_iov_t			*iov;
	int			sgl_off = 0;
	int			iov_off = 0;
	int			i;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	/* If there are several shards do listing all together, then
	 * let's merge the key to get rid of duplicate keys from different
	 * shards.
	 */
	obj_args = dc_task_get_args(obj_auxi->obj_task);
	sgl = shard_arg->la_sgl;
	iov = &sgl->sg_iovs[sgl_off];
	for (i = 0; i < shard_arg->la_nr; i++) {
		int key_size = obj_args->kds[i].kd_key_len;
		bool alloc_key = false;
		char *key = NULL;
		int rc;

		if (key_size <= (iov->iov_len - iov_off)) {
			key = (char *)iov->iov_buf + iov_off;
			iov_off += key_size;
			if (iov_off == iov->iov_len - 1) {
				iov_off = 0;
				iov = &sgl->sg_iovs[++sgl_off];
			}
		} else {
			int left = key_size;

			D_ALLOC(key, key_size);
			if (key == NULL)
				return -DER_NOMEM;

			alloc_key = true;
			while (left > 0) {
				int copy_size = min(left,
						    iov->iov_len - iov_off);
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

		rc = merge_key(&merge_arg->merge_list, key, key_size);
		if (rc)
			return rc;

		if (alloc_key)
			D_FREE(key);
	}

	return 0;
}

static int
obj_shard_list_comp_cb(struct shard_auxi_args *shard_auxi,
		       struct obj_auxi_args *obj_auxi, void *cb_arg)
{
	struct comp_cb_merge_arg	*merge_arg = cb_arg;
	struct shard_list_args		*shard_arg;
	int				rc = 0;

	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	if (obj_auxi->req_tgts.ort_grp_size == 1) {
		merge_arg->merge_nr = shard_arg->la_nr;
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
obj_shard_comp_cb(struct shard_auxi_args *shard_auxi,
		  struct obj_auxi_args *obj_auxi, void *cb_arg)
{
	tse_task_t	*task = obj_auxi->obj_task;
	int		ret = task->dt_result;

	if (task->dt_result != 0)
		return task->dt_result;
	/*
	 * Check shard IO task's completion status:
	 * 1) if succeed just stores the highest replied pm version.
	 * 2) if any shard failed, store it in obj_auxi->result, the
	 *    un-retryable error with higher priority.
	 */
	if (ret == 0) {
		if (obj_auxi->map_ver_reply < shard_auxi->map_ver)
			obj_auxi->map_ver_reply = shard_auxi->map_ver;
	} else if (obj_retry_error(ret)) {
		D_DEBUG(DB_IO, "shard %d ret %d.\n", shard_auxi->shard, ret);
		if (obj_auxi->result == 0)
			obj_auxi->result = ret;
	} else {
		/* for un-retryable failure, set the err to whole obj IO */
		D_DEBUG(DB_IO, "shard %d ret %d.\n", shard_auxi->shard, ret);
		obj_auxi->result = ret;
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

typedef int (*shard_comp_cb_t)(struct shard_auxi_args *shard_auxi,
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

	return arg->cb(shard_auxi, arg->obj_auxi, arg->cb_arg);
}

static int
obj_auxi_shards_iterate(struct obj_auxi_args *obj_auxi, shard_comp_cb_t cb,
			void *cb_arg)
{
	struct shard_list_comp_cb_arg	arg;
	d_list_t			*head;
	int				rc;

	head = &obj_auxi->shard_task_head;
	if (d_list_empty(head)) {
		struct shard_auxi_args *shard_auxi;

		shard_auxi = obj_embedded_shard_arg(obj_auxi);
		rc = cb(shard_auxi, obj_auxi, cb_arg);
		return rc;
	}

	arg.cb = cb;
	arg.cb_arg = cb_arg;
	arg.obj_auxi = obj_auxi;
	return tse_task_list_traverse(head, shard_auxi_task_cb, &arg);
}

static int
obj_list_recxs_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		  struct comp_cb_merge_arg *arg)
{
	struct obj_auxi_list_recx *recx;
	struct obj_auxi_list_recx *tmp;
	daos_obj_list_t		  *obj_args;
	int			  idx = 0;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	if (d_list_empty(&arg->merge_list)) {
		*obj_args->nr = arg->merge_nr;
		return 0;
	}

	D_ASSERT(daos_oclass_is_ec(obj_auxi->obj->cob_md.omd_id, NULL));
	d_list_for_each_entry_safe(recx, tmp, &arg->merge_list,
				   recx_list) {
		D_ASSERTF(idx <= *obj_args->nr, "more recx %d max_num %d\n",
			  idx, *obj_args->nr);
		obj_args->recxs[idx++] = recx->recx;
		d_list_del(&recx->recx_list);
		D_FREE(recx);
	}
	*obj_args->nr = idx;
	return 0;
}

/* Check anchor eof by sub anchors */
static void
anchor_check_eof(struct dc_object *obj, daos_anchor_t *anchor)
{
	struct daos_oclass_attr	*oca;
	int i;

	if (!anchor->da_sub_anchors ||
	    !daos_oclass_is_ec(obj->cob_md.omd_id, &oca))
		return;

	for (i = 0; i < obj_ec_data_tgt_nr(oca); i++) {
		daos_anchor_t *sub_anchor =
			&((daos_anchor_t *)anchor->da_sub_anchors)[i];
		if (!daos_anchor_is_eof(sub_anchor))
			break;
	}

	if (i == obj_ec_data_tgt_nr(oca)) {
		void *ptr = (void *)anchor->da_sub_anchors;

		daos_anchor_set_eof(anchor);
		D_FREE(ptr);
		anchor->da_sub_anchors = 0;
	}
}

static void
dump_key_and_anchor_eof_check(struct obj_auxi_args *obj_auxi,
			      daos_anchor_t *anchor,
			      struct comp_cb_merge_arg *arg)
{
	struct dc_object *obj = obj_auxi->obj;
	struct obj_auxi_list_key *key;
	struct obj_auxi_list_key *tmp;
	daos_obj_list_t *obj_args;
	d_sg_list_t *sgl;
	int sgl_off = 0;
	int iov_off = 0;
	int cnt = 0;
	d_iov_t *iov;

	/* 1. Dump the keys from merge_list into user input buffer(@sgl) */
	D_ASSERT(obj_auxi->is_ec_obj);
	obj_args = dc_task_get_args(obj_auxi->obj_task);
	sgl = obj_args->sgl;
	iov = &sgl->sg_iovs[sgl_off];
	d_list_for_each_entry_safe(key, tmp, &arg->merge_list,
				   key_list) {
		int left = key->key.iov_len;

		while (left > 0) {
			int copy_size = min(iov->iov_buf_len - iov_off,
					    key->key.iov_len);
			memcpy(iov->iov_buf + iov_off, key->key.iov_buf,
			       copy_size);
			left -= copy_size;
			iov_off += copy_size;
			if (iov_off == iov->iov_buf_len - 1) {
				iov_off = 0;
				sgl_off++;
			}
		}
		d_list_del(&key->key_list);
		D_FREE(key->key.iov_buf);
		D_FREE(key);
		cnt++;
	}
	*obj_args->nr = cnt;

	/* 2. Check sub anchors to see if anchors is eof */
	anchor_check_eof(obj, anchor);
}

static void
obj_list_akey_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		 struct comp_cb_merge_arg *arg)
{
	daos_obj_list_t	*obj_arg = dc_task_get_args(obj_auxi->obj_task);
	daos_anchor_t	*anchor = obj_arg->akey_anchor;

	if (task->dt_result != 0)
		return;

	if (anchor->da_sub_anchors)
		dump_key_and_anchor_eof_check(obj_auxi, anchor, arg);
	else
		*obj_arg->nr = arg->merge_nr;

	if (daos_anchor_is_eof(anchor))
		D_DEBUG(DB_IO, "Enumerated All shards\n");
}

static void
obj_list_dkey_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		 struct comp_cb_merge_arg *arg)
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
		dump_key_and_anchor_eof_check(obj_auxi, anchor, arg);
	else
		*obj_arg->nr = arg->merge_nr;

	if (!daos_anchor_is_eof(anchor)) {
		D_DEBUG(DB_IO, "More keys in shard %d\n", shard);
	} else if (!(daos_anchor_get_flags(anchor) & DIOF_TO_SPEC_SHARD) &&
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

static void
obj_list_obj_cb(tse_task_t *task, struct obj_auxi_args *obj_auxi,
		struct comp_cb_merge_arg *arg)
{
	daos_obj_list_t *obj_arg = dc_task_get_args(obj_auxi->obj_task);

	*obj_arg->nr = arg->merge_nr;
	anchor_check_eof(obj_auxi->obj, obj_arg->dkey_anchor);
}

static int
obj_list_comp(struct obj_auxi_args *obj_auxi,
	      struct comp_cb_merge_arg *arg)
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
	struct comp_cb_merge_arg merge_arg = { 0 };
	int rc;

	D_INIT_LIST_HEAD(&merge_arg.merge_list);
	/* Process each shards */
	rc = obj_auxi_shards_iterate(obj_auxi, obj_shard_comp_cb, &merge_arg);
	if (rc != 0)
		D_GOTO(out, rc);

	if (obj_is_enum_opc(obj_auxi->opc))
		rc = obj_list_comp(obj_auxi, &merge_arg);
out:
	if (obj_auxi->opc == DAOS_OBJ_DKEY_RPC_ENUMERATE ||
	    obj_auxi->opc == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		struct obj_auxi_list_key *key;
		struct obj_auxi_list_key *tmp;

		d_list_for_each_entry_safe(key, tmp, &merge_arg.merge_list,
					   key_list) {
			d_list_del(&key->key_list);
			D_FREE_PTR(key);
		}
	} else if (obj_auxi->opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		struct obj_auxi_list_recx *recx;
		struct obj_auxi_list_recx *tmp;

		d_list_for_each_entry_safe(recx, tmp, &merge_arg.merge_list,
					   recx_list) {
			d_list_del(&recx->recx_list);
			D_FREE_PTR(recx);
		}
	}

	return rc;
}

static void
obj_size_fetch_cb(const struct dc_object *obj, struct obj_auxi_args *obj_auxi)
{
	daos_iod_t	*uiods, *iods;
	d_sg_list_t	*usgls;
	d_list_t	*head;
	unsigned int     iod_nr, i;
	bool		 size_all_zero = true;

	/* set iod_size to original user IOD */
	uiods = obj_auxi->reasb_req.orr_uiods;
	iods = obj_auxi->rw_args.api_args->iods;
	iod_nr = obj_auxi->rw_args.api_args->nr;
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

	if (size_all_zero) {
		usgls = obj_auxi->reasb_req.orr_usgls;
		for (i = 0; i < iod_nr; i++)
			usgls[i].sg_nr_out = 0;
	} else {
		D_DEBUG(DB_IO, DF_OID" retrying IO after size fetch.\n",
			DP_OID(obj->cob_md.omd_id));
		/* remove the original shard fetch tasks and will
		 * recreate new shard fetch tasks with new parameters.
		 */
		head = &obj_auxi->shard_task_head;
		if (head != NULL) {
			tse_task_list_traverse(head, shard_task_remove, NULL);
			D_ASSERT(d_list_empty(head));
		}
		obj_auxi->new_shard_tasks = 1;
		obj_auxi->io_retry = 1;
	}
}

static int
obj_comp_cb(tse_task_t *task, void *data)
{
	struct dc_object	*obj;
	struct obj_auxi_args	*obj_auxi;
	bool			pm_stale = false;
	int			rc;

	obj_auxi = tse_task_stack_pop(task, sizeof(*obj_auxi));
	obj_auxi->to_leader = 0;
	obj_auxi->io_retry = 0;
	obj = obj_auxi->obj;
	rc = obj_comp_cb_internal(obj_auxi);
	if (rc != 0 || obj_auxi->result) {
		if (task->dt_result == 0)
			task->dt_result = rc ? rc : obj_auxi->result;
		D_DEBUG(DB_IO, "obj complete callback: %d\n", task->dt_result);
	}

	if (obj->cob_time_fetch_leader != NULL &&
	    obj_auxi->req_tgts.ort_shard_tgts != NULL &&
	    ((!obj_is_modification_opc(obj_auxi->opc) &&
	      task->dt_result == -DER_INPROGRESS) ||
	     (obj_is_modification_opc(obj_auxi->opc) &&
	      task->dt_result == 0))) {
		int	idx;

		idx = obj_auxi->req_tgts.ort_shard_tgts->st_shard /
			obj_get_grp_size(obj);
		daos_gettime_coarse(&obj->cob_time_fetch_leader[idx]);
	}

	/* Check if the pool map needs to refresh */
	if (obj_auxi->map_ver_reply > obj_auxi->map_ver_req ||
	    daos_crt_network_error(task->dt_result) ||
	    task->dt_result == -DER_STALE || task->dt_result == -DER_TIMEDOUT ||
	    task->dt_result == -DER_EVICTED) {
		D_DEBUG(DB_IO, "map_ver stale (req %d, reply %d). result %d\n",
			obj_auxi->map_ver_req, obj_auxi->map_ver_reply,
			task->dt_result);
		pm_stale = true;
	}

	if (obj_retry_error(task->dt_result)) {
		/* If the RPC sponsor set the @spec_shard, then means it wants
		 * to fetch data from the specified shard. If such shard isn't
		 * ready for read, we should let the caller know that, But there
		 * are some other cases we need to retry the RPC with current
		 * shard, such as -DER_TIMEDOUT or daos_crt_network_error().
		 */

		if ((!obj_auxi->spec_shard && !obj_auxi->no_retry) ||
		    (task->dt_result != -DER_INPROGRESS &&
		     task->dt_result != -DER_TX_BUSY)) {
			if (obj_auxi->is_ec_obj &&
			    obj_is_enum_opc(obj_auxi->opc))
				obj_auxi->to_leader = 0;

			obj_auxi->io_retry = 1;
		}

		if (task->dt_result == -DER_CSUM) {
			if (!obj_auxi->spec_shard && !obj_auxi->no_retry &&
			    obj_auxi->opc == DAOS_OBJ_RPC_FETCH) {
				if (!obj_auxi->csum_retry &&
				    !obj_auxi->csum_report) {
					obj_auxi->csum_report = 1;
				} else if (obj_auxi->csum_report) {
					obj_auxi->csum_report = 0;
					obj_auxi->csum_retry = 1;
				}
			} else {
				/* not retrying updates yet */
				obj_auxi->io_retry = 0;
			}
		}

		if (!obj_auxi->spec_shard && task->dt_result == -DER_INPROGRESS)
			obj_auxi->to_leader = 1;
	}

	if (!obj_auxi->io_retry && task->dt_result == 0 &&
	    obj_auxi->reasb_req.orr_size_fetch)
		obj_size_fetch_cb(obj, obj_auxi);

	if (pm_stale || obj_auxi->io_retry)
		obj_retry_cb(task, obj, obj_auxi, pm_stale);

	if (!obj_auxi->io_retry) {
		struct obj_ec_fail_info	*fail_info;
		bool new_tgt_fail = false;
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
			D_ASSERT(!daos_handle_is_valid(obj_auxi->th));

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
				 (task->dt_result == 0 ||
				  task->dt_result == -DER_NONEXIST))
				/* Cache transactional read if exist or not. */
				dc_tx_attach(obj_auxi->th, DAOS_OBJ_RPC_FETCH,
					     task);
			break;
		}
		case DAOS_OBJ_RPC_PUNCH:
		case DAOS_OBJ_RPC_PUNCH_DKEYS:
		case DAOS_OBJ_RPC_PUNCH_AKEYS:
			D_ASSERT(!daos_handle_is_valid(obj_auxi->th));
			break;
		case DAOS_OBJ_RPC_QUERY_KEY:
		case DAOS_OBJ_RECX_RPC_ENUMERATE:
		case DAOS_OBJ_AKEY_RPC_ENUMERATE:
		case DAOS_OBJ_DKEY_RPC_ENUMERATE:
			if (daos_handle_is_valid(obj_auxi->th) &&
			    (task->dt_result == 0 ||
			     task->dt_result == -DER_NONEXIST))
				/* Cache transactional read if exist or not. */
				dc_tx_attach(obj_auxi->th,
					     obj_auxi->opc, task);
			break;
		case DAOS_OBJ_RPC_ENUMERATE:
			/* XXX: For list dkey recursively, that is mainly used
			 *	by rebuild and object consistency verification,
			 *	currently, we do not have any efficient way to
			 *	trace and spread related read TS to servers.
			 */
			break;
		}

		if (obj_auxi->req_tgts.ort_shard_tgts !=
		    obj_auxi->req_tgts.ort_tgts_inline)
			D_FREE(obj_auxi->req_tgts.ort_shard_tgts);

		if (!d_list_empty(head)) {
			if (obj_is_enum_opc(obj_auxi->opc))
				obj_auxi_list_fini(obj_auxi);
			tse_task_list_traverse(head, shard_task_remove, NULL);
			D_ASSERT(d_list_empty(head));
		}

		fail_info = obj_auxi->reasb_req.orr_fail;
		new_tgt_fail = obj_auxi->ec_wait_recov &&
			       task->dt_result == -DER_BAD_TARGET;
		if (fail_info != NULL &&
		    ((obj_auxi->reasb_req.orr_singv_only &&
		     (task->dt_result == -DER_BAD_TARGET ||
		      task->dt_result == 0)) ||
		     (fail_info->efi_nrecx_lists > 0 &&
		      (task->dt_result == 0 ||  new_tgt_fail)))) {
			if (obj_auxi->ec_wait_recov && task->dt_result == 0) {
				daos_obj_fetch_t *args = dc_task_get_args(task);

				obj_ec_recov_data(&obj_auxi->reasb_req,
					obj->cob_md.omd_id, args->nr);

				obj_reasb_req_fini(&obj_auxi->reasb_req,
						   obj_auxi->iod_nr);
				memset(obj_auxi, 0, sizeof(*obj_auxi));
			} else if (!obj_auxi->ec_in_recov || new_tgt_fail) {
				task->dt_result = 0;
				obj_ec_recov_cb(task, obj, obj_auxi);
			}
		} else {
			if (obj_auxi->is_ec_obj && task->dt_result == 0 &&
			    obj_auxi->opc == DAOS_OBJ_RPC_FETCH &&
			    obj_auxi->bulks != NULL) {
				daos_obj_fetch_t *args = dc_task_get_args(task);

				obj_ec_fetch_set_sgl(&obj_auxi->reasb_req,
						     args->nr);
			}

			obj_bulk_fini(obj_auxi);
			obj_reasb_req_fini(&obj_auxi->reasb_req,
					   obj_auxi->iod_nr);
			/* zero it as user might reuse/resched the task, for
			 * example the usage in dac_array_set_size().
			 */
			memset(obj_auxi, 0, sizeof(*obj_auxi));
		}
	} else {
		obj_ec_fail_info_reset(&obj_auxi->reasb_req);
	}

	obj_decref(obj);
	return 0;
}

/**
 * Init obj_auxi_arg for this object task.
 * Register the completion cb for obj IO request
 */
static int
obj_task_init(tse_task_t *task, int opc, uint32_t map_ver, daos_handle_t th,
	      struct obj_auxi_args **auxi, struct dc_object *obj)
{
	struct obj_auxi_args	*obj_auxi;

	obj_auxi = tse_task_stack_push(task, sizeof(*obj_auxi));
	obj_auxi->opc = opc;
	obj_auxi->map_ver_req = map_ver;
	obj_auxi->obj_task = task;
	obj_auxi->th = th;
	obj_auxi->obj = obj;
	shard_task_list_init(obj_auxi);
	*auxi = obj_auxi;
	return tse_task_register_comp_cb(task, obj_comp_cb, NULL, 0);
}

static int
shard_rw_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
	      struct obj_auxi_args *obj_auxi, uint64_t dkey_hash,
	      uint32_t grp_idx)
{
	daos_obj_rw_t		*obj_args;
	struct shard_rw_args	*shard_arg;
	struct obj_reasb_req	*reasb_req;
	struct obj_tgt_oiod	*toiod;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	shard_arg = container_of(shard_auxi, struct shard_rw_args, auxi);

	if (daos_handle_is_inval(obj_auxi->th))
		daos_dti_gen(&shard_arg->dti,
			     obj_auxi->opc == DAOS_OBJ_RPC_FETCH ||
			     srv_io_mode != DIM_DTX_FULL_ENABLED ||
			     daos_obj_is_echo(obj->cob_md.omd_id));
	else
		dc_tx_get_dti(obj_auxi->th, &shard_arg->dti);

	obj_auxi->rw_args.api_args	= obj_args;
	shard_arg->api_args		= obj_args;
	shard_arg->dkey_hash		= dkey_hash;
	shard_arg->bulks		= obj_auxi->bulks;
	if (obj_auxi->req_reasbed) {
		reasb_req = &obj_auxi->reasb_req;
		if (reasb_req->tgt_oiods != NULL) {
			D_ASSERT(obj_auxi->opc == DAOS_OBJ_RPC_FETCH);
			toiod = obj_ec_tgt_oiod_get(
				reasb_req->tgt_oiods, reasb_req->orr_tgt_nr,
				shard_auxi->ec_tgt_idx);
			D_ASSERT(toiod != NULL);
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

static int
obj_csum_update(struct dc_object *obj, daos_obj_update_t *args,
		struct obj_auxi_args *obj_auxi)
{
	struct daos_csummer	*csummer = dc_cont_hdl2csummer(obj->cob_coh);
	struct daos_csummer	*csummer_copy = NULL;
	struct cont_props	 cont_props = dc_cont_hdl2props(obj->cob_coh);
	struct dcs_csum_info	*dkey_csum = NULL;
	struct dcs_iod_csums	*iod_csums = NULL;
	int			 rc;

	if (!daos_csummer_initialized(csummer)) /** Not configured */
		return 0;

	if (!cont_props.dcp_csum_enabled && cont_props.dcp_dedup_enabled) {
		uint32_t	dedup_th = cont_props.dcp_dedup_size;
		int		i;
		bool		candidate = false;

		/**
		 * Checksums are only enabled for dedup purpose.
		 * Verify whether the I/O is a candidate for dedup.
		 * If not, then no need to provide a checksum to the server
		 */

		for (i = 0; i < args->nr; i++) {
			daos_iod_t	*iod = &args->iods[i];
			int		 j = 0;

			if (iod->iod_type == DAOS_IOD_SINGLE)
				/** dedup does not support single value yet */
				return 0;

			for (j = 0; j < iod->iod_nr; j++) {
				daos_recx_t	*recx = &iod->iod_recxs[j];

				if (recx->rx_nr * iod->iod_size >= dedup_th)
					candidate = true;
			}
		}
		if (!candidate)
			/** not a candidate for dedup, don't compute checksum */
			return 0;
	}

	/** Used to do actual checksum calculations. This prevents conflicts
	 * between tasks
	 */
	csummer_copy = daos_csummer_copy(csummer);
	if (csummer_copy == NULL)
		return -DER_NOMEM;


	/** Calc 'd' key checksum */
	rc = daos_csummer_calc_key(csummer_copy, args->dkey, &dkey_csum);
	if (rc != 0) {
		daos_csummer_destroy(&csummer_copy);
		return rc;
	}

	/** Calc 'a' key checksum and value checksum */
	rc = daos_csummer_calc_iods(csummer_copy, args->sgls, args->iods, NULL,
				    args->nr,
				    false, obj_auxi->reasb_req.orr_singv_los,
				    -1, &iod_csums);
	if (rc != 0) {
		daos_csummer_free_ci(csummer_copy, &dkey_csum);
		daos_csummer_destroy(&csummer_copy);
		D_ERROR("daos_csummer_calc_iods error: %d", rc);
		return rc;
	}
	daos_csummer_destroy(&csummer_copy);

	/** fault injection - corrupt data and/or keys after calculating
	 * checksum - simulates corruption over network
	 */
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE_DKEY))
		((char *) args->dkey->iov_buf)[0]++;
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE_AKEY))
		((char *)iod_csums[0].ic_akey.cs_csum)[0]++;
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE))
		dcf_corrupt(args->sgls, args->nr);

	obj_auxi->rw_args.iod_csums = iod_csums;
	obj_auxi->rw_args.dkey_csum = dkey_csum;

	return 0;
}

static int
obj_csum_fetch(const struct dc_object *obj, daos_obj_fetch_t *args,
	       struct obj_auxi_args *obj_auxi)
{
	struct daos_csummer	*csummer = dc_cont_hdl2csummer(obj->cob_coh);
	struct daos_csummer	*csummer_copy;
	struct dcs_csum_info	*dkey_csum = NULL;
	struct dcs_iod_csums	*iod_csums = NULL;
	int			 rc;

	if (!daos_csummer_initialized(csummer) ||
	    csummer->dcs_skip_data_verify)
		/** csummer might be initialized by dedup, but checksum
		 * feature is turned off ...
		 */
		return 0;

	if (obj_auxi->rw_args.dkey_csum != NULL) {
		/** already calculated - don't need to do it again */
		return 0;
	}

	/** Used to do actual checksum calculations. This prevents conflicts
	 * between tasks
	 */
	csummer_copy = daos_csummer_copy(csummer);
	if (csummer_copy == NULL)
		return -DER_NOMEM;

	/** dkey */
	rc = daos_csummer_calc_key(csummer_copy, args->dkey, &dkey_csum);
	if (rc != 0) {
		daos_csummer_destroy(&csummer_copy);
		return rc;
	}

	/** akeys (1 for each iod) */
	rc = daos_csummer_calc_iods(csummer_copy, args->sgls, args->iods, NULL,
				    args->nr,
				    true, obj_auxi->reasb_req.orr_singv_los,
				    -1, &iod_csums);
	if (rc != 0) {
		D_ERROR("daos_csummer_calc_iods error: %d", rc);
		daos_csummer_free_ci(csummer_copy, &dkey_csum);
		daos_csummer_destroy(&csummer_copy);
		return rc;
	}

	daos_csummer_destroy(&csummer_copy);

	/**
	 * fault injection - corrupt keys after calculating checksum -
	 * simulates corruption over network
	 */
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_FETCH_DKEY))
		dkey_csum->cs_csum[0]++;
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_FETCH_AKEY))
		iod_csums[0].ic_akey.cs_csum[0]++;

	obj_auxi->rw_args.iod_csums = iod_csums;
	obj_auxi->rw_args.dkey_csum = dkey_csum;

	return 0;
}

/* Selects next replica in the object's layout.
 */
static int
obj_retry_csum_err(struct dc_object *obj, struct obj_auxi_args *obj_auxi,
		     uint64_t dkey_hash, unsigned int map_ver, uint8_t *bitmap)
{
	D_WARN("Retrying replica because of checksum error.\n");
	struct daos_oclass_attr	*oca;
	unsigned int		 next_shard, retry_size, shard_cnt, shard_idx;
	int			 rc = 0;

	/* CSUM retry for EC object is not supported yet */
	if (daos_oclass_is_ec(obj->cob_md.omd_id, &oca)) {
		obj_auxi->no_retry = 1;
		rc = -DER_CSUM;
		goto out;
	}
	rc = obj_dkey2grpmemb(obj, dkey_hash, map_ver,
			      &shard_idx, &shard_cnt);
	if (rc != 0)
		goto out;

	/* bitmap has only 8 bits, so retry to the first eight replicas */
	retry_size = shard_cnt <= 8 ? shard_cnt : 8;
	next_shard = (obj_auxi->req_tgts.ort_shard_tgts[0].st_shard + 1) %
		retry_size + shard_idx;

	/* all replicas have csum error for this fetch request */
	if (next_shard == obj_auxi->initial_shard) {
		obj_auxi->no_retry = 1;
		rc = -DER_CSUM;
		goto out;
	}
	setbit(bitmap, next_shard - shard_idx);
	obj_auxi->flags &= ~ORF_CSUM_REPORT;
out:
	return rc;
}

int
dc_obj_fetch_task(tse_task_t *task)
{
	daos_obj_fetch_t	*args = dc_task_get_args(task);
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	uint8_t                 *tgt_bitmap = NIL_BITMAP;
	unsigned int		 map_ver = 0;
	uint64_t		 dkey_hash;
	struct dtx_epoch	 epoch;
	uint32_t		 shard = 0;
	int			 rc;
	uint8_t                  csum_bitmap = 0;

	rc = obj_req_valid(task, args, DAOS_OBJ_RPC_FETCH, &epoch, &map_ver,
		&obj, args->extra_flags & DIOF_CHECK_EXISTENCE ? true : false);
	if (rc != 0)
		D_GOTO(out_task, rc);

	rc = obj_task_init(task, DAOS_OBJ_RPC_FETCH, map_ver, args->th,
			   &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	if ((args->extra_flags & DIOF_EC_RECOV) != 0) {
		obj_auxi->ec_in_recov = 1;
		obj_auxi->reasb_req.orr_fail = args->extra_arg;
		obj_auxi->reasb_req.orr_recov = 1;
	}
	if (obj_auxi->ec_wait_recov)
		goto out_task;

	if (args->extra_flags & DIOF_CHECK_EXISTENCE) {
		obj_auxi->flags |= DRF_CHECK_EXISTENCE;
	} else {
		rc = obj_rw_req_reassemb(obj, args, &epoch, obj_auxi);
		if (rc != 0) {
			D_ERROR(DF_OID" obj_req_reassemb failed %d.\n",
				DP_OID(obj->cob_md.omd_id), rc);
			goto out_task;
		}
	}

	dkey_hash = obj_dkey2hash(args->dkey);

	if (args->extra_arg == NULL &&
	    DAOS_FAIL_CHECK(DAOS_OBJ_SPECIAL_SHARD))
		args->extra_flags |= DIOF_TO_SPEC_SHARD;

	obj_auxi->spec_shard = (args->extra_flags & DIOF_TO_SPEC_SHARD) != 0;
	if (obj_auxi->spec_shard) {
		D_ASSERT(!obj_auxi->to_leader);

		if (args->extra_arg != NULL) {
			shard = *(int *)args->extra_arg;
		} else if (obj_auxi->io_retry) {
			shard = obj_auxi->specified_shard;
		} else {
			shard = daos_fail_value_get();
			obj_auxi->specified_shard = shard;
		}
	} else {
		obj_auxi->to_leader = (args->extra_flags & DIOF_TO_LEADER) != 0;
	}

	/* for CSUM error, build a bitmap with only the next target set,
	 * based current obj->auxi.req_tgt.ort_shart_tgts[0].st_shard.
	 * (increment shard mod rdg-size, set appropriate bit).
	 */
	if (obj_auxi->csum_report) {
		obj_auxi->flags |= ORF_CSUM_REPORT;
		D_ASSERT(!obj_auxi->csum_retry);
		/* the spec_shard case will not cause csum_report, so just
		 * reuse it to make sure the csum_report send to correct tgt.
		 */
		shard = obj_auxi->req_tgts.ort_shard_tgts[0].st_shard;
		obj_auxi->spec_shard = 1;
	} else if (obj_auxi->csum_retry) {
		rc = obj_retry_csum_err(obj, obj_auxi, dkey_hash, map_ver,
					&csum_bitmap);
		if (rc)
			goto out_task;
		tgt_bitmap = &csum_bitmap;
	} else {
		if (obj_auxi->is_ec_obj)
			tgt_bitmap = obj_auxi->reasb_req.tgt_bitmap;
	}

	if (args->extra_flags & DIOF_CHECK_EXISTENCE)
		tgt_bitmap = NIL_BITMAP;

	rc = obj_req_get_tgts(obj, (int *)&shard, args->dkey, dkey_hash,
			      tgt_bitmap, map_ver, obj_auxi->to_leader,
			      obj_auxi->spec_shard, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);
	if (obj_auxi->csum_report)
		obj_auxi->spec_shard = 0;

	rc = obj_csum_fetch(obj, args, obj_auxi);
	if (rc != 0) {
		D_ERROR("obj_csum_fetch error: %d", rc);
		D_GOTO(out_task, rc);
	}

	if (!obj_auxi->io_retry) {
		if (!obj_auxi->is_ec_obj)
			obj_auxi->initial_shard =
				obj_auxi->req_tgts.ort_shard_tgts[0].st_shard;

	}

	rc = obj_rw_bulk_prep(obj, args->iods, args->sgls, args->nr,
			      false, false, task, obj_auxi);
	if (rc != 0)
		goto out_task;

	rc = obj_req_fanout(obj, obj_auxi, dkey_hash, map_ver, &epoch,
			    shard_rw_prep, dc_obj_shard_rw, task);
	return rc;

out_task:
	tse_task_complete(task, rc);
	return rc;
}

static int
dc_obj_update(tse_task_t *task, struct dtx_epoch *epoch, uint32_t map_ver,
	      daos_obj_update_t *args)
{
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	uint64_t		 dkey_hash;
	int			 rc;

	rc = obj_req_valid(task, args, DAOS_OBJ_RPC_UPDATE, epoch, &map_ver,
			   &obj, false);
	if (rc != 0)
		goto out_task; /* invalid parameter */

	rc = obj_task_init(task, DAOS_OBJ_RPC_UPDATE, map_ver, args->th,
			   &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		goto out_task;
	}

	rc = obj_rw_req_reassemb(obj, args, NULL, obj_auxi);
	if (rc) {
		D_ERROR(DF_OID" obj_req_reassemb failed %d.\n",
			DP_OID(obj->cob_md.omd_id), rc);
		goto out_task;
	}

	dkey_hash = obj_dkey2hash(args->dkey);
	rc = obj_req_get_tgts(obj, NULL, args->dkey, dkey_hash,
			      obj_auxi->reasb_req.tgt_bitmap, map_ver, false,
			      false, obj_auxi);
	if (rc)
		goto out_task;

	if (!obj_auxi->io_retry) {
		rc = obj_csum_update(obj, args, obj_auxi);
		if (rc) {
			D_ERROR("obj_csum_update error: %d", rc);
			goto out_task;
		}
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_COMMIT_SYNC))
		obj_auxi->flags |= ORF_DTX_SYNC;

	D_DEBUG(DB_IO, "update "DF_OID" dkey_hash "DF_U64"\n",
		DP_OID(obj->cob_md.omd_id), dkey_hash);

	rc = obj_rw_bulk_prep(obj, args->iods, args->sgls, args->nr, true,
			      obj_auxi->req_tgts.ort_srv_disp, task, obj_auxi);
	if (rc != 0)
		goto out_task;

	rc = obj_req_fanout(obj, obj_auxi, dkey_hash, map_ver, epoch,
			    shard_rw_prep, dc_obj_shard_rw, task);
	return rc;

out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_obj_update_task(tse_task_t *task)
{
	daos_obj_update_t	*args = dc_task_get_args(task);
	struct dtx_epoch	 epoch = {0};
	unsigned int		 map_ver = 0;
	int			 rc;

	if (daos_handle_is_valid(args->th)) {
		/* add the operation to DTX and complete immediately */
		rc = dc_tx_attach(args->th, DAOS_OBJ_RPC_UPDATE, task);
		goto comp;
	}

	/* submit the update */
	return dc_obj_update(task, &epoch, map_ver, args);
comp:
	if (rc <= 0)
		tse_task_complete(task, rc);
	return rc > 0 ? 0 : rc;
}

static void
shard_anchors_free(daos_anchor_t *anchor)
{
	void *tmp = (void *)anchor->da_sub_anchors;

	if (anchor == NULL)
		return;

	if (anchor->da_sub_anchors == 0)
		return;

	D_FREE(tmp);
	anchor->da_sub_anchors = 0;
}

static int
shard_anchor_prep(daos_anchor_t *anchor, int nr)
{
	daos_anchor_t	*sub_anchors;
	int		i;

	D_ASSERT(anchor->da_sub_anchors == 0);
	D_ALLOC_ARRAY(sub_anchors, nr);
	if (sub_anchors == NULL)
		return -DER_NOMEM;

	for (i = 0; i < nr; i++)
		sub_anchors[i] = *anchor;

	anchor->da_sub_anchors = (uint64_t)sub_anchors;
	return 0;
}

static int
shard_anchors_prep(daos_obj_list_t *obj_args, int nr)
{
	int rc = 0;

	if (obj_args->anchor &&
	    obj_args->anchor->da_sub_anchors == 0) {
		rc = shard_anchor_prep(obj_args->anchor, nr);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	if (obj_args->dkey_anchor &&
	    obj_args->dkey_anchor->da_sub_anchors == 0) {
		rc = shard_anchor_prep(obj_args->dkey_anchor, nr);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	if (obj_args->akey_anchor &&
	    obj_args->akey_anchor->da_sub_anchors == 0) {
		rc = shard_anchor_prep(obj_args->akey_anchor, nr);
		if (rc != 0)
			D_GOTO(out, rc);
	}

out:
	if (rc) {
		shard_anchors_free(obj_args->akey_anchor);
		shard_anchors_free(obj_args->dkey_anchor);
		shard_anchors_free(obj_args->anchor);
	}

	return rc;
}

static void
obj_shard_list_fini(daos_obj_list_t *obj_args,
		    struct shard_list_args *shard_arg)
{
	if (shard_arg->la_sgl && shard_arg->la_sgl != obj_args->sgl) {
		d_sgl_fini(shard_arg->la_sgl, false);
		D_FREE(shard_arg->la_sgl);
	}
	shard_arg->la_kds = NULL;
	shard_arg->la_recxs = NULL;
	shard_anchors_free(obj_args->akey_anchor);
	shard_anchors_free(obj_args->dkey_anchor);
	shard_anchors_free(obj_args->anchor);
}

/* prepare the object enumeration for each shards */
static int
obj_shard_list_prep(daos_obj_list_t *obj_args,
		    struct shard_list_args *shard_arg, int shard_nr,
		    int opc)
{
	int idx;
	int rc = 0;

	shard_arg->la_nr = (*obj_args->nr + shard_nr - 1) / shard_nr;
	idx = shard_arg->la_auxi.shard;
	if (shard_arg->la_kds == NULL && obj_args->kds != NULL)
		shard_arg->la_kds = &obj_args->kds[idx * shard_arg->la_nr];

	if (shard_arg->la_recxs == NULL && obj_args->recxs)
		shard_arg->la_recxs = &obj_args->recxs[idx * shard_arg->la_nr];

	if (shard_arg->la_sgl == NULL && obj_args->sgl) {
		d_iov_t	*shard_iov;
		char	*ptr;
		int	seg_size;

		seg_size = obj_args->sgl->sg_iovs[0].iov_buf_len / shard_nr;

		D_ALLOC_PTR(shard_arg->la_sgl);
		if (shard_arg->la_sgl == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_ALLOC_PTR(shard_iov);
		if (shard_iov == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		ptr = obj_args->sgl->sg_iovs[0].iov_buf + idx * seg_size;

		d_iov_set(shard_iov, ptr, seg_size);
		shard_arg->la_sgl->sg_nr = 1;
		shard_arg->la_sgl->sg_nr_out = 1;
		shard_arg->la_sgl->sg_iovs = shard_iov;
	}
out:
	if (rc)
		obj_shard_list_fini(obj_args, shard_arg);
	return rc;
}

static int
shard_list_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
		struct obj_auxi_args *obj_auxi, uint64_t dkey_hash,
		uint32_t grp_idx)
{
	daos_obj_list_t		*obj_args;
	struct shard_list_args	*shard_arg;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	shard_arg = container_of(shard_auxi, struct shard_list_args, la_auxi);
	shard_arg->la_api_args = obj_args;
	if (daos_oclass_is_ec(obj->cob_md.omd_id, NULL) &&
	    !obj_auxi->to_leader) {
		uint32_t		idx = shard_auxi->shard;
		daos_anchor_t		*sub_anchors;
		struct daos_oclass_attr *oca;
		int			shard_nr;
		int			rc;

		oca = daos_oclass_attr_find(obj->cob_md.omd_id);
		D_ASSERT(oca != NULL);
		D_ASSERT(oca->ca_resil == DAOS_RES_EC);
		shard_nr = obj_ec_data_tgt_nr(oca);

		/* check and allocate the sub_anchors */
		rc = shard_anchors_prep(obj_args, shard_nr);
		if (rc) {
			D_ERROR(DF_OID" shard list %d prep: %d\n",
				DP_OID(obj->cob_md.omd_id), grp_idx, rc);
			return rc;
		}

		rc = obj_shard_list_prep(obj_args, shard_arg, shard_nr,
					 obj_auxi->opc);
		if (rc) {
			D_ERROR(DF_OID" shard list %d prep: %d\n",
				DP_OID(obj->cob_md.omd_id), grp_idx, rc);
			return rc;
		}

		if (obj_args->anchor) {
			sub_anchors =
			(daos_anchor_t *)obj_args->anchor->da_sub_anchors;
			shard_arg->la_anchor = &sub_anchors[idx];
		}

		if (obj_args->dkey_anchor) {
			sub_anchors =
			(daos_anchor_t *)obj_args->dkey_anchor->da_sub_anchors;
			shard_arg->la_dkey_anchor = &sub_anchors[idx];
		}
		if (obj_args->akey_anchor) {
			sub_anchors =
			(daos_anchor_t *)obj_args->akey_anchor->da_sub_anchors;
			shard_arg->la_akey_anchor = &sub_anchors[idx];
		}
	} else {
		shard_arg->la_nr = *obj_args->nr;
		shard_arg->la_recxs = obj_args->recxs;
		shard_arg->la_anchor = obj_args->anchor;
		shard_arg->la_akey_anchor = obj_args->akey_anchor;
		shard_arg->la_dkey_anchor = obj_args->dkey_anchor;
		shard_arg->la_nr = *obj_args->nr;
		shard_arg->la_kds = obj_args->kds;
		shard_arg->la_sgl = obj_args->sgl;
	}

	return 0;
}

static int
obj_list_common(tse_task_t *task, int opc, daos_obj_list_t *args)
{
	struct dc_object	*obj;
	struct obj_auxi_args	*obj_auxi;
	unsigned int		 map_ver = 0;
	uint64_t		 dkey_hash = 0;
	struct dtx_epoch	 epoch = {0};
	int			 shard = -1;
	int			 rc;

	rc = obj_req_valid(task, args, opc, &epoch, &map_ver, &obj, false);
	if (rc)
		goto out_task;

	rc = obj_task_init(task, opc, map_ver, args->th, &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	if (daos_oclass_is_ec(obj->cob_md.omd_id, NULL))
		obj_auxi->is_ec_obj = 1;

	if ((args->dkey_anchor != NULL &&
	     daos_anchor_get_flags(args->dkey_anchor) & DIOF_TO_LEADER) ||
	     obj_auxi->is_ec_obj)
		obj_auxi->to_leader = 1;

	if (args->dkey_anchor != NULL &&
	    (daos_anchor_get_flags(args->dkey_anchor) & DIOF_TO_SPEC_SHARD)) {
		shard = dc_obj_anchor2shard(args->dkey_anchor);
		obj_auxi->spec_shard = 1;
	} else if (DAOS_FAIL_CHECK(DAOS_OBJ_SPECIAL_SHARD)) {
		if (obj_auxi->io_retry) {
			shard = obj_auxi->specified_shard;
		} else {
			shard = daos_fail_value_get();
			obj_auxi->specified_shard = shard;
		}
		obj_auxi->spec_shard = 1;
	} else if (args->dkey == NULL) {
		D_ASSERT(args->dkey_anchor != NULL);

		shard = dc_obj_anchor2shard(args->dkey_anchor);
		if (obj_auxi->to_leader) {
			struct pl_obj_shard *p_shard;
			int leader;

			leader = obj_grp_leader_get(obj, shard, map_ver);
			p_shard = obj_get_shard(obj, leader);
			if (p_shard->po_rebuilding ||
			    p_shard->po_target == -1) {
				D_DEBUG(DB_IO, DF_OID".%d is being rebuilt\n",
					DP_OID(obj->cob_md.omd_id), shard);
				obj_auxi->to_leader = 0;
				if (obj_auxi->is_ec_obj) {
					/* For EC object, if the leader is
					 * is in rebuilt status, let's list
					 * from 1st data shards.
					 */
					shard = rounddown(shard,
							  obj->cob_grp_size);
				} else {
					D_ERROR(DF_OID".%d leader is being"
						" rebuilt\n",
						DP_OID(obj->cob_md.omd_id),
						shard);
					D_GOTO(out_task, rc = -DER_NONEXIST);
				}
			} else {
				shard = leader;
			}
			D_DEBUG(DB_IO, "list on shard %d\n", shard);
		} else if (!obj_auxi->is_ec_obj) {
			shard = obj_grp_valid_shard_get(obj, shard, map_ver,
							opc);
			if (shard < 0)
				D_GOTO(out_task, rc = shard);
		}
	} else {
		dkey_hash = obj_dkey2hash(args->dkey);
		shard = obj_dkey2shard(obj, dkey_hash, map_ver, opc,
				       obj_auxi->to_leader);
		if (shard < 0)
			D_GOTO(out_task, rc = shard);
	}

	rc = obj_req_get_tgts(obj, &shard, args->dkey, dkey_hash, NIL_BITMAP,
			      map_ver, obj_auxi->to_leader,
			      obj_auxi->spec_shard, obj_auxi);
	if (rc != 0)
		goto out_task;

	if (args->dkey == NULL)
		dc_obj_shard2anchor(args->dkey_anchor, shard);

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

	D_DEBUG(DB_IO, "list opc %d "DF_OID" dkey %llu\n", opc,
		DP_OID(obj->cob_md.omd_id), (unsigned long long)dkey_hash);

	rc = obj_req_fanout(obj, obj_auxi, dkey_hash, map_ver, &epoch,
			    shard_list_prep, dc_obj_shard_list, task);
	return rc;

out_task:
	tse_task_complete(task, rc);
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
shard_punch_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
		 struct obj_auxi_args *obj_auxi, uint64_t dkey_hash,
		 uint32_t grp_idx)
{
	daos_obj_punch_t	*obj_args;
	struct shard_punch_args	*shard_arg;
	daos_handle_t		 coh;
	uuid_t			 coh_uuid;
	uuid_t			 cont_uuid;
	int			 rc;

	coh = obj->cob_coh;
	if (daos_handle_is_inval(coh))
		return -DER_NO_HDL;

	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		return rc;

	obj_args = dc_task_get_args(obj_auxi->obj_task);
	shard_arg = container_of(shard_auxi, struct shard_punch_args, pa_auxi);
	shard_arg->pa_api_args		= obj_args;
	shard_arg->pa_opc		= obj_auxi->opc;
	shard_arg->pa_dkey_hash		= dkey_hash;
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
dc_obj_punch(tse_task_t *task, struct dtx_epoch *epoch, uint32_t map_ver,
	     enum obj_rpc_opc opc, daos_obj_punch_t *api_args)
{
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	uint64_t		 dkey_hash;
	int			 rc;

	obj = obj_hdl2ptr(api_args->oh);
	D_ASSERT(obj != NULL);
	rc = obj_task_init(task, opc, map_ver, api_args->th, &obj_auxi, obj);
	if (rc) {
		obj_decref(obj);
		goto out_task;
	}

	dkey_hash = obj_dkey2hash(api_args->dkey);
	rc = obj_req_get_tgts(obj, NULL, api_args->dkey, dkey_hash, NIL_BITMAP,
			      map_ver, false, false, obj_auxi);
	if (rc != 0)
		goto out_task;

	if (daos_oclass_is_ec(obj->cob_md.omd_id, NULL) ||
	    DAOS_FAIL_CHECK(DAOS_DTX_COMMIT_SYNC))
		obj_auxi->flags |= ORF_DTX_SYNC;

	D_DEBUG(DB_IO, "punch "DF_OID" dkey %llu\n",
		DP_OID(obj->cob_md.omd_id), (unsigned long long)dkey_hash);

	rc = obj_req_fanout(obj, obj_auxi, dkey_hash, map_ver, epoch,
			    shard_punch_prep, dc_obj_shard_punch, task);
	return rc;

out_task:
	tse_task_complete(task, rc);
	return rc;
}

static int
obj_punch_common(tse_task_t *task, enum obj_rpc_opc opc, daos_obj_punch_t *args)
{
	struct dtx_epoch	 epoch = {0};
	unsigned int		 map_ver = 0;
	int			 rc;

	rc = obj_req_valid(task, args, opc, &epoch, &map_ver, NULL, false);
	if (rc != 0)
		goto comp; /* invalid parameters */

	if (daos_handle_is_valid(args->th)) {

		/* add the operation to DTX and complete immediately */
		rc = dc_tx_attach(args->th, opc, task);
		goto comp;
	}
	/* submit the punch */
	return dc_obj_punch(task, &epoch, map_ver, opc, args);
comp:
	if (rc <= 0)
		tse_task_complete(task, rc);
	return rc > 0 ? 0 : rc;
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
	struct shard_auxi_args	 kqa_auxi;
	uuid_t			 kqa_coh_uuid;
	uuid_t			 kqa_cont_uuid;
	daos_obj_query_key_t	*kqa_api_args;
	uint64_t		 kqa_dkey_hash;
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
	obj = args->kqa_auxi.obj;
	th = args->kqa_auxi.obj_auxi->th;
	epoch = &args->kqa_auxi.epoch;

	/* See the similar shard_io_task. */
	if (daos_handle_is_valid(th) && !dtx_epoch_chosen(epoch)) {
		rc = dc_tx_get_epoch(task, th, epoch);
		if (rc < 0)
			return rc;
		if (rc == DC_TX_GE_REINIT)
			return tse_task_reinit(task);
	}

	rc = obj_shard_open(obj, args->kqa_auxi.shard, args->kqa_auxi.map_ver,
			    &obj_shard);
	if (rc != 0) {
		/* skip a failed target */
		if (rc == -DER_NONEXIST)
			rc = 0;

		tse_task_complete(task, rc);
		return rc;
	}

	tse_task_stack_push_data(task, &args->kqa_dkey_hash,
				 sizeof(args->kqa_dkey_hash));
	api_args = args->kqa_api_args;
	/* let's set the current pool map version in the req to
	 * avoid ESTALE.
	 */
	args->kqa_auxi.obj_auxi->map_ver_reply =
			args->kqa_auxi.obj_auxi->map_ver_req;
	rc = dc_obj_shard_query_key(obj_shard, epoch, api_args->flags, obj,
				    api_args->dkey, api_args->akey,
				    api_args->recx, args->kqa_coh_uuid,
				    args->kqa_cont_uuid, &args->kqa_dti,
				    &args->kqa_auxi.obj_auxi->map_ver_reply,
				    th, task);

	obj_shard_close(obj_shard);
	return rc;
}

struct shard_task_reset_query_target_args {
	uint32_t	shard;
	uint32_t	replicas;
	uint32_t	version;
	uint32_t	flags;
};

static int
shard_task_reset_query_target(tse_task_t *shard_task, void *arg)
{
	struct shard_task_reset_query_target_args	*reset_arg = arg;
	struct shard_auxi_args				*auxi_arg;
	int						 rc;

	auxi_arg = tse_task_buf_embedded(shard_task, sizeof(*auxi_arg));
	if (reset_arg->flags & DAOS_GET_DKEY) {
		rc = obj_grp_leader_get(auxi_arg->obj, reset_arg->shard,
					reset_arg->version);
		if (rc < 0)
			return rc;
		auxi_arg->shard = rc;
	} else {
		auxi_arg->shard = reset_arg->shard;
	}
	rc = obj_shard2tgtid(auxi_arg->obj, auxi_arg->shard, reset_arg->version,
			     &auxi_arg->target);
	reset_arg->shard += reset_arg->replicas;

	return rc;
}

int
dc_obj_query_key(tse_task_t *api_task)
{
	daos_obj_query_key_t	*api_args = dc_task_get_args(api_task);
	tse_sched_t		*sched = tse_task2sched(api_task);
	struct obj_auxi_args	*obj_auxi;
	struct dc_object	*obj;
	d_list_t		*head = NULL;
	daos_handle_t		coh;
	uuid_t			coh_uuid;
	uuid_t			cont_uuid;
	int			shard_first;
	unsigned int		replicas;
	unsigned int		map_ver = 0;
	uint64_t		dkey_hash;
	struct dtx_epoch	epoch;
	struct dtx_id		dti;
	int			i = 0;
	int			rc;

	D_ASSERTF(api_args != NULL,
		  "Task Argument OPC does not match DC OPC\n");

	rc = obj_req_valid(api_task, api_args, DAOS_OBJ_RPC_QUERY_KEY, &epoch,
			   &map_ver, &obj, false);
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

	rc = obj_task_init(api_task, DAOS_OBJ_RPC_QUERY_KEY, map_ver,
			   api_args->th, &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

	coh = dc_obj_hdl2cont_hdl(api_args->oh);
	if (daos_handle_is_inval(coh))
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = dc_cont_hdl2uuid(coh, &coh_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_task, rc);

	D_ASSERTF(api_args->dkey != NULL, "dkey should not be NULL\n");
	dkey_hash = obj_dkey2hash(api_args->dkey);
	if (api_args->flags & DAOS_GET_DKEY) {
		replicas = obj_get_replicas(obj);
		shard_first = 0;
		/** set data len to 0 before retrieving dkey. */
		api_args->dkey->iov_len = 0;
	} else {
		replicas = obj->cob_shards_nr;
		/* For the specified dkey, only need to query one replica. */
		shard_first = obj_dkey2shard(obj, dkey_hash, map_ver,
					     DAOS_OPC_OBJ_QUERY,
					     obj_auxi->to_leader);
		if (shard_first < 0)
			D_GOTO(out_task, rc = shard_first);
	}

	obj_auxi->map_ver_req = map_ver;
	obj_auxi->obj_task = api_task;

	D_DEBUG(DB_IO, "Object Key Query "DF_OID" start %u\n",
		DP_OID(obj->cob_md.omd_id), shard_first);

	head = &obj_auxi->shard_task_head;

	/* for retried obj IO, reuse the previous shard tasks and resched it */
	if (obj_auxi->io_retry && obj_auxi->args_initialized) {
		/* For distributed transaction, check whether TX pool
		 * map is stale or not, if stale, restart the TX.
		 */
		if (daos_handle_is_valid(obj_auxi->th)) {
			rc = dc_tx_check_pmv(obj_auxi->th);
			if (rc != 0)
				goto out_task;
		}

		/* The RPC may need to be resent to (new) leader. */
		if (srv_io_mode != DIM_CLIENT_DISPATCH) {
			struct shard_task_reset_query_target_args	arg;

			arg.shard = shard_first;
			arg.replicas = replicas;
			arg.version = map_ver;
			arg.flags = api_args->flags;
			tse_task_list_traverse(head,
					shard_task_reset_query_target, &arg);
		}

		goto task_sched;
	}

	D_ASSERT(!obj_auxi->args_initialized);
	D_ASSERT(d_list_empty(head));

	/* In each redundancy group, the QUERY RPC only needs to be sent
	 * to one replica: i += replicas
	 */
	for (i = 0; i < obj->cob_shards_nr; i += replicas) {
		tse_task_t			*task;
		struct shard_query_key_args	*args;
		int				 shard;

		if (api_args->flags & DAOS_GET_DKEY) {
			/* Send to leader replica directly for reducing
			 * retry because some potential 'prepared' DTX.
			 */
			shard = obj_grp_leader_get(obj, i, map_ver);
			if (shard < 0) {
				rc = shard;
				D_ERROR(DF_OID" no valid shard, rc "DF_RC".\n",
					DP_OID(obj->cob_md.omd_id),
					DP_RC(rc));
				D_GOTO(out_task, rc);
			}
		} else {
			shard = shard_first + i;
		}

		rc = tse_task_create(shard_query_key_task, sched, NULL, &task);
		if (rc != 0)
			D_GOTO(out_task, rc);

		args = tse_task_buf_embedded(task, sizeof(*args));
		args->kqa_api_args	= api_args;
		args->kqa_auxi.epoch	= epoch;
		args->kqa_auxi.shard	= shard;
		args->kqa_auxi.map_ver	= map_ver;
		args->kqa_auxi.obj	= obj;
		args->kqa_auxi.obj_auxi	= obj_auxi;
		args->kqa_dkey_hash	= dkey_hash;
		args->kqa_dti		= dti;
		uuid_copy(args->kqa_coh_uuid, coh_uuid);
		uuid_copy(args->kqa_cont_uuid, cont_uuid);

		rc = obj_shard2tgtid(obj, shard, map_ver,
				     &args->kqa_auxi.target);
		if (rc != 0) {
			tse_task_complete(task, rc);
			D_GOTO(out_task, rc);
		}

		rc = tse_task_register_deps(api_task, 1, &task);
		if (rc != 0) {
			tse_task_complete(task, rc);
			D_GOTO(out_task, rc);
		}

		/* decref and delete from head at shard_task_remove */
		tse_task_addref(task);
		tse_task_list_add(task, head);
	}

	obj_auxi->args_initialized = 1;

task_sched:
	obj_shard_task_sched(obj_auxi, &epoch);
	return rc;

out_task:
	if (head != NULL && !d_list_empty(head)) {
		D_ASSERTF(!obj_retry_error(rc), "unexpected ret "DF_RC"\n",
			DP_RC(rc));

		tse_task_list_traverse(head, shard_task_abort, &rc);
	}

	tse_task_complete(api_task, rc);

	return rc;
}

static int
shard_sync_prep(struct shard_auxi_args *shard_auxi, struct dc_object *obj,
		struct obj_auxi_args *obj_auxi, uint64_t dkey_hash,
		uint32_t grp_idx)
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
	int				 rc;
	int				 i;

	if (srv_io_mode != DIM_DTX_FULL_ENABLED)
		D_GOTO(out_task, rc = 0);

	D_ASSERTF(args != NULL,
		  "Task Argument OPC does not match DC OPC\n");

	rc = obj_req_valid(task, args, DAOS_OBJ_RPC_SYNC, &epoch,
			   &map_ver, &obj, false);
	if (rc)
		D_GOTO(out_task, rc);

	rc = obj_task_init(task, DAOS_OBJ_RPC_SYNC, map_ver, DAOS_HDL_INVAL,
			   &obj_auxi, obj);
	if (rc != 0) {
		obj_decref(obj);
		D_GOTO(out_task, rc);
	}

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

		for (i = 0; i < *args->nr; i++)
			*args->epochs_p[i] = 0;
	}

	rc = obj_req_get_tgts(obj, NULL, NULL, 0, NIL_BITMAP, map_ver, true,
			      false, obj_auxi);
	if (rc != 0)
		D_GOTO(out_task, rc);

	D_DEBUG(DB_IO, "sync "DF_OID", reps %d\n",
		DP_OID(obj->cob_md.omd_id), obj_get_replicas(obj));

	rc = obj_req_fanout(obj, obj_auxi, 0, map_ver, &epoch, shard_sync_prep,
			    dc_obj_shard_sync, task);
	return rc;

out_task:
	tse_task_complete(task, rc);
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

	oc_attr = daos_oclass_attr_find(obj->cob_md.omd_id);
	D_ASSERT(oc_attr != NULL);

	/* XXX: Currently, only support to verify replicated object.
	 *	Need more work for EC object in the future.
	 */
	if (oc_attr->ca_resil != DAOS_RES_REPL)
		D_GOTO(out, rc = -DER_NOSYS);

	if (oc_attr->u.rp.r_num == DAOS_OBJ_REPL_MAX)
		reps = obj->cob_grp_size;
	else
		reps = oc_attr->u.rp.r_num;

	if (reps == 1)
		goto out;

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

			D_FREE(dova[i].fetch_buf);
		}

		D_FREE(dova);
	}

	obj_decref(obj);

	return rc;
}

void
daos_dc_obj2id(void *ptr, daos_obj_id_t *id)
{
	struct dc_object *obj = ptr;

	*id = obj->cob_md.omd_id;
}
