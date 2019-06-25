/**
 *
 *
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * src/placement/pl_map_common.h
 */

#ifndef __PL_MAP_COMMON_H__
#define __PL_MAP_COMMON_H__

#include <daos/placement.h>

/**
 * This struct is used to to hold information while
 * finding rebuild targets for shards located on unavailable
 * targets.
 */
struct failed_shard {
	d_list_t        fs_list;
	uint32_t        fs_shard_idx;
	uint32_t        fs_fseq;
	uint32_t        fs_tgt_id;
	uint8_t         fs_status;
};

void
remap_add_one(d_list_t *remap_list, struct failed_shard *f_new);

int
remap_alloc_one(d_list_t *remap_list, unsigned int shard_idx,
		struct pool_target *tgt);

void
remap_list_free_all(d_list_t *remap_list);

void
remap_dump(d_list_t *remap_list, struct daos_obj_md *md,
		char *comment);

int
op_get_grp_size(unsigned int domain_nr, unsigned int *grp_size,
		daos_obj_id_t oid);

int
remap_list_fill(struct pl_map *map, struct daos_obj_md *md,
		struct daos_obj_shard_md *shard_md, uint32_t rebuild_ver,
		uint32_t *tgt_id, uint32_t *shard_idx,
		unsigned int array_size, int myrank, int *idx,
		struct pl_obj_layout *layout, d_list_t *remap_list);

void
determine_valid_spares(struct pool_target *spare_tgt, struct daos_obj_md *md,
		bool spare_avail, d_list_t **current, d_list_t *remap_list,
		struct failed_shard *f_shard, struct pl_obj_shard *l_shard);

int
spec_place_rank_get(unsigned int *pos, daos_obj_id_t oid,
		struct pool_map *pl_poolmap);

#endif /* __PL_MAP_COMMON_H__ */
