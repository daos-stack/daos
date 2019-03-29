/**
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
	int	(*o_create)(struct pool_map *poolmap,
			    struct pl_map_init_attr *mia,
			    struct pl_map **mapp);
	/** destroy a placement map */
	void	(*o_destroy)(struct pl_map *map);
	/** print debug information of a placement map */
	void	(*o_print)(struct pl_map *map);

	/** object methods */
	/** see \a pl_map_obj_select and \a pl_map_obj_rebalance */
	int	(*o_obj_place)(struct pl_map *map,
			       struct daos_obj_md *md,
			       struct daos_obj_shard_md *shard_md,
			       struct pl_obj_layout **layout_pp);
	/** see \a pl_map_obj_rebuild */
	int	(*o_obj_find_rebuild)(struct pl_map *map,
				      struct daos_obj_md *md,
				      struct daos_obj_shard_md *shard_md,
				      uint32_t rebuild_ver,
				      uint32_t *tgt_rank,
				      uint32_t *shard_id,
				      unsigned int array_size, int myrank);
	int	(*o_obj_find_reint)(struct pl_map *map,
				    struct daos_obj_md *md,
				    struct daos_obj_shard_md *shard_md,
				    struct pl_target_grp *tgp_reint,
				    uint32_t *tgt_reint);
};

unsigned int pl_obj_shard2grp_head(struct daos_obj_shard_md *shard_md,
				   struct daos_oclass_attr *oc_attr);
unsigned int pl_obj_shard2grp_index(struct daos_obj_shard_md *shard_md,
				    struct daos_oclass_attr *oc_attr);

#endif /* __PL_MAP_H__ */
