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
 * src/placement/pl_map.h
 */

#ifndef __PL_MAP_H__
#define __PL_MAP_H__

#include <daos/placement.h>

struct pl_map_ops;

/**
 * Function table for placement map.
 */
struct pl_map_ops {
	/** create a placement map */
	int (*o_create)(struct pool_map *poolmap,
			struct pl_map_init_attr *mia,
			struct pl_map **mapp);
	/** destroy a placement map */
	void (*o_destroy)(struct pl_map *map);
	/** print debug information of a placement map */
	void (*o_print)(struct pl_map *map);

	/** object methods */
	/** see \a pl_map_obj_select and \a pl_map_obj_rebalance */
	int (*o_obj_place)(struct pl_map *map,
			   struct daos_obj_md *md,
			   struct daos_obj_shard_md *shard_md,
			   struct pl_obj_layout **layout_pp);
	/** see \a pl_map_obj_rebuild */
	int (*o_obj_find_rebuild)(struct pl_map *map,
				  struct daos_obj_md *md,
				  struct daos_obj_shard_md *shard_md,
				  uint32_t rebuild_ver,
				  uint32_t *tgt_rank,
				  uint32_t *shard_id,
				  unsigned int array_size);
	int (*o_obj_find_reint)(struct pl_map *map,
				  struct daos_obj_md *md,
				  struct daos_obj_shard_md *shard_md,
				  uint32_t reint_ver,
				  uint32_t *tgt_rank,
				  uint32_t *shard_id,
				  unsigned int array_size);
	int (*o_obj_find_addition)(struct pl_map *map,
				   struct daos_obj_md *md,
				  struct daos_obj_shard_md *shard_md,
				  uint32_t reint_ver,
				  uint32_t *tgt_rank,
				  uint32_t *shard_id,
				  unsigned int array_size);

};

unsigned int pl_obj_shard2grp_head(struct daos_obj_shard_md *shard_md,
				   struct daos_oclass_attr *oc_attr);
unsigned int pl_obj_shard2grp_index(struct daos_obj_shard_md *shard_md,
				    struct daos_oclass_attr *oc_attr);

/** Common placement map functions */

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
void
reint_add_one(d_list_t *remap_list, struct failed_shard *f_new);

int
remap_alloc_one(d_list_t *remap_list, unsigned int shard_idx,
		struct pool_target *tgt, bool for_reint);

int
remap_insert_copy_one(d_list_t *remap_list, struct failed_shard *original);

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
		unsigned int array_size, int *idx,
		struct pl_obj_layout *layout, d_list_t *remap_list,
		bool fill_addition);

void
determine_valid_spares(struct pool_target *spare_tgt, struct daos_obj_md *md,
		       bool spare_avail, d_list_t **current,
		       d_list_t *remap_list, bool for_reint,
		       struct failed_shard *f_shard,
		       struct pl_obj_shard *l_shard);

int
spec_place_rank_get(unsigned int *pos, daos_obj_id_t oid,
		    struct pool_map *pl_poolmap);

int
pl_map_extend(struct pl_obj_layout *layout, d_list_t *extended_list);

bool
is_pool_adding(struct pool_domain *dom);

#endif /* __PL_MAP_H__ */
