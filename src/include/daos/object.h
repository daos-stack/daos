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

#if 1 /* TODO: Move to dss. These will become private. */
#define RECX_INLINE	(1U << 0)

struct obj_enum_rec {
	daos_recx_t		rec_recx;
	daos_epoch_range_t	rec_epr;
	uuid_t			rec_cookie;
	uint64_t		rec_size;
	uint32_t		rec_version;
	uint32_t		rec_flags;
};
#endif

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

int dc_obj_init(void);
void dc_obj_fini(void);

int dc_obj_class_register(tse_task_t *task);
int dc_obj_class_query(tse_task_t *task);
int dc_obj_class_list(tse_task_t *task);
int dc_obj_declare(tse_task_t *task);
int dc_obj_open(tse_task_t *task);
int dc_obj_close(tse_task_t *task);
int dc_obj_punch(tse_task_t *task);
int dc_obj_punch_dkeys(tse_task_t *task);
int dc_obj_punch_akeys(tse_task_t *task);
int dc_obj_query(tse_task_t *task);
int dc_obj_fetch(tse_task_t *task);
int dc_obj_update(tse_task_t *task);
int dc_obj_list_dkey(tse_task_t *task);
int dc_obj_list_akey(tse_task_t *task);
int dc_obj_list_rec(tse_task_t *task);
int dc_obj_list_obj(tse_task_t *task);
int dc_obj_fetch_md(daos_obj_id_t oid, struct daos_obj_md *md);
int dc_obj_layout_get(daos_handle_t oh, struct pl_obj_layout **layout,
		      unsigned int *grp_nr, unsigned int *grp_size);
int dc_obj_layout_refresh(daos_handle_t oh);

#define ENUM_ANCHOR_SHARD_OFF		28
#define ENUM_ANCHOR_SHARD_LENGTH	4

/** Decode shard number from enumeration anchor */
static inline uint32_t
dc_obj_anchor2shard(daos_anchor_t *anchor)
{
	uint32_t tag;

	memcpy(&tag, &anchor->da_hkey[ENUM_ANCHOR_SHARD_OFF],
	       ENUM_ANCHOR_SHARD_LENGTH);

	return tag;
}

/** Encode shard into enumeration anchor. */
static inline void
dc_obj_shard2anchor(daos_anchor_t *anchor, uint32_t shard)
{
	memcpy(&anchor->da_hkey[ENUM_ANCHOR_SHARD_OFF], &shard,
	       ENUM_ANCHOR_SHARD_LENGTH);
}

#endif /* __DAOS_OBJECT_H__ */
