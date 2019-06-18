/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#ifndef __DAOS_OBJECT_H__
#define __DAOS_OBJECT_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_types.h>
#include <daos_api.h>

enum daos_io_mode {
	DIM_DTX_FULL_ENABLED	= 0,	/* by default */
	DIM_SERVER_DISPATCH	= 1,
	DIM_CLIENT_DISPATCH	= 2,
};

/** object metadata stored in the global OI table of container */
struct daos_obj_md {
	daos_obj_id_t		omd_id;
	uint32_t		omd_ver;
	uint32_t		omd_padding;
	union {
		uint32_t	omd_split;
		uint64_t	omd_loff;
	};
};

/** object shard metadata stored in each contianer shard */
struct daos_obj_shard_md {
	/** ID of the object shard */
	daos_unit_oid_t		smd_id;
	uint64_t		smd_attr;
	uint32_t		smd_po_ver;
	uint32_t		smd_padding;
};

/**
 * object layout information.
 **/
struct daos_obj_shard {
	uint32_t	os_replica_nr;
	uint32_t	os_ranks[0];
};

struct daos_obj_layout {
	uint32_t	ol_ver;
	uint32_t	ol_class;
	uint32_t	ol_nr;
	struct daos_obj_shard	*ol_shards[0];
};

#define TGTS_IGNORE		((d_rank_t)-1)
/** to identify each obj shard's target */
struct daos_shard_tgt {
	uint32_t		st_rank;	/* rank of the shard */
	uint32_t		st_shard;	/* shard index */
	uint32_t		st_tgt_idx;	/* target xstream index */
	uint32_t		st_tgt_id;	/* target id */
};

static inline bool
daos_obj_id_equal(daos_obj_id_t oid1, daos_obj_id_t oid2)
{
	return oid1.lo == oid2.lo && oid1.hi == oid2.hi;
}

static inline bool
daos_unit_obj_id_equal(daos_unit_oid_t oid1, daos_unit_oid_t oid2)
{
	return daos_obj_id_equal(oid1.id_pub, oid2.id_pub) &&
	       oid1.id_shard == oid2.id_shard;
}

struct pl_obj_layout;

struct daos_oclass_attr *daos_oclass_attr_find(daos_obj_id_t oid);
unsigned int daos_oclass_grp_size(struct daos_oclass_attr *oc_attr);
unsigned int daos_oclass_grp_nr(struct daos_oclass_attr *oc_attr,
				struct daos_obj_md *md);
int daos_oclass_name2id(const char *name);

/** bits for the specified rank */
#define DAOS_OC_SR_SHIFT	24
#define DAOS_OC_SR_BITS		8
#define DAOS_OC_SR_MASK		\
	(((1ULL << DAOS_OC_SR_BITS) - 1) << DAOS_OC_SR_SHIFT)

/** bits for the specified target, Note: the target here means the target
 * index inside the rank, and it only reserve 4 bits, so only specify 16th
 * target maximum.
 */
#define DAOS_OC_ST_SHIFT	20
#define DAOS_OC_ST_BITS		4
#define DAOS_OC_ST_MASK		\
	(((1ULL << DAOS_OC_ST_BITS) - 1) << DAOS_OC_ST_SHIFT)

static inline d_rank_t
daos_oclass_sr_get_rank(daos_obj_id_t oid)
{
	D_ASSERT(daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK);

	return ((oid.hi & DAOS_OC_SR_MASK) >> DAOS_OC_SR_SHIFT);
}

static inline daos_obj_id_t
daos_oclass_sr_set_rank(daos_obj_id_t oid, d_rank_t rank)
{
	D_ASSERT(daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK);
	D_ASSERT(rank < (1 << DAOS_OC_SR_SHIFT));
	D_ASSERT((oid.hi & DAOS_OC_SR_MASK) == 0);

	oid.hi |= (uint64_t)rank << DAOS_OC_SR_SHIFT;
	return oid;
}

static inline int
daos_oclass_st_get_tgt(daos_obj_id_t oid)
{
	D_ASSERT(daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK);

	return ((oid.hi & DAOS_OC_ST_MASK) >> DAOS_OC_ST_SHIFT);
}

static inline daos_obj_id_t
daos_oclass_st_set_tgt(daos_obj_id_t oid, int tgt)
{
	D_ASSERT(daos_obj_id2class(oid) == DAOS_OC_R3S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R1S_SPEC_RANK ||
		 daos_obj_id2class(oid) == DAOS_OC_R2S_SPEC_RANK);
	D_ASSERT(tgt < (1 << DAOS_OC_ST_SHIFT));
	D_ASSERT((oid.hi & DAOS_OC_ST_MASK) == 0);

	oid.hi |= (uint64_t)tgt << DAOS_OC_ST_SHIFT;
	return oid;
}

static inline bool
daos_unit_oid_is_null(daos_unit_oid_t oid)
{
	return oid.id_shard == 0 && daos_obj_is_null_id(oid.id_pub);
}

static inline int
daos_unit_oid_compare(daos_unit_oid_t a, daos_unit_oid_t b)
{
	int rc;

	rc = daos_obj_compare_id(a.id_pub, b.id_pub);
	if (rc != 0)
		return rc;

	if (a.id_shard < b.id_shard)
		return -1;
	else if (a.id_shard > b.id_shard)
		return 1;

	return 0;
}

int daos_obj_layout_free(struct daos_obj_layout *layout);
int daos_obj_layout_alloc(struct daos_obj_layout **layout, uint32_t grp_nr,
			  uint32_t grp_size);
int daos_obj_layout_get(daos_handle_t coh, daos_obj_id_t oid,
			struct daos_obj_layout **layout);

int dc_obj_init(void);
void dc_obj_fini(void);

int dc_obj_register_class(tse_task_t *task);
int dc_obj_query_class(tse_task_t *task);
int dc_obj_list_class(tse_task_t *task);
int dc_obj_open(tse_task_t *task);
int dc_obj_close(tse_task_t *task);
int dc_obj_punch(tse_task_t *task);
int dc_obj_punch_dkeys(tse_task_t *task);
int dc_obj_punch_akeys(tse_task_t *task);
int dc_obj_query(tse_task_t *task);
int dc_obj_query_key(tse_task_t *task);
int dc_obj_fetch(tse_task_t *task);
int dc_obj_update(tse_task_t *task);
int dc_obj_list_dkey(tse_task_t *task);
int dc_obj_list_akey(tse_task_t *task);
int dc_obj_list_rec(tse_task_t *task);
int dc_obj_list_obj(tse_task_t *task);
int dc_obj_fetch_md(daos_obj_id_t oid, struct daos_obj_md *md);
int dc_obj_layout_get(daos_handle_t oh, struct daos_obj_layout **p_layout);
int dc_obj_layout_refresh(daos_handle_t oh);
daos_handle_t dc_obj_hdl2cont_hdl(daos_handle_t oh);

/** Decode shard number from enumeration anchor */
static inline uint32_t
dc_obj_anchor2shard(daos_anchor_t *anchor)
{
	return anchor->da_shard;
}

/** Encode shard into enumeration anchor. */
static inline void
dc_obj_shard2anchor(daos_anchor_t *anchor, uint32_t shard)
{
	anchor->da_shard = shard;
}

#endif /* __DAOS_OBJECT_H__ */
