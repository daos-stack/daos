/**
 * (C) Copyright 2015 Intel Corporation.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */
/**
 * This file is part of daos_sr
 *
 * dsr/placement/pl_map.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include "pl_map_internal.h"

extern pl_map_ops_t		rim_map_ops;

struct pl_map_table {
	pl_map_type_t		 mt_type;
	pl_map_ops_t		*mt_ops;
	char			*mt_name;
};

/** array of defined placement maps */
static struct pl_map_table pl_maps[] = {
	{
		.mt_type	= PL_TYPE_RIM,
		.mt_ops		= &rim_map_ops,
		.mt_name	= "rim",
	},
	{
		.mt_type	= PL_TYPE_UNKNOWN,
		.mt_name	= "unknown",
	},
};

/**
 * Create a placement map based on attributes in \a ma
 */
int
pl_map_create(cl_map_t *cl_map, pl_map_attr_t *ma, pl_map_t **mapp)
{
	struct pl_map_table	*mt = pl_maps;
	int			 rc;

	for (mt = &pl_maps[0]; mt->mt_type != PL_TYPE_UNKNOWN; mt++) {
		if (mt->mt_type == ma->ma_type)
			break;
	}

	if (mt->mt_type == PL_TYPE_UNKNOWN) {
		D_DEBUG(DF_PL, "Unknown placement map type %d\n", mt->mt_type);
		return -EINVAL;
	}

	D_DEBUG(DF_PL, "Create %s placement map\n", mt->mt_name);
	rc = mt->mt_ops->o_create(cl_map, ma, mapp);
	if (rc != 0)
		return rc;

	(*mapp)->pm_ops = mt->mt_ops;
	return 0;
}

/**
 * Destroy a placement map
 */
void
pl_map_destroy(pl_map_t *map)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_destroy != NULL);

	map->pm_ops->o_destroy(map);
}

/** Print a placement map, it's optional and for debug only */
void pl_map_print(pl_map_t *map)
{
	D_ASSERT(map->pm_ops != NULL);

	if (map->pm_ops->o_print != NULL)
		map->pm_ops->o_print(map);
}

/**
 * (Re)compute distribution for the input object shard:
 *
 * PL_SEL_CUR	it is used when placement map has been changed, this function
 *		may return a different target rank for the input object shard
 *
 * PL_SEL_ALL	returns all object shards belonging to the SR object of the
 *		input object shard
 *
 * PL_SEL_CUR	returns all object shards in the same redundancy group of the
 *		input object shard
 *
 * PL_SEL_NEXT	returns all object shards in the next redundancy group of the
 *		input object shard
 *
 * PL_SEL_PREV	returns all object shards in the next redundancy group of the
 *		input object shard
 */
int
pl_map_obj_select(pl_map_t *map, pl_obj_shard_t *obs, pl_obj_attr_t *oa,
		  pl_select_opc_t opc, unsigned int obs_arr_len,
		  pl_obj_shard_t *obs_arr)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_select != NULL);

	memset(obs_arr, -1, obs_arr_len * sizeof(*obs_arr));
	return map->pm_ops->o_obj_select(map, obs, oa, opc, obs_arr_len,
					 obs_arr);
}

/**
 * Check if the object shard \a obs needs to be rebalanced.
 * If returned \a rank_rebal is different with the input \a obs::os_rank,
 * this object should be moved to target identified by \a rank_rebal
 */
int
pl_map_obj_rebalance(pl_map_t *map, pl_obj_shard_t *obs, pl_obj_attr_t *oa,
		     daos_rank_t *rank_rebal)
{
	pl_obj_shard_t os;
	int	       rc;

	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_select != NULL);

	*rank_rebal = -1;
	D_DEBUG(DF_CL, "Rebalance object "DF_U64"."DF_U64".%u\n",
		obs->os_id.body[1], obs->os_id.body[0], obs->os_sid);

	rc = map->pm_ops->o_obj_select(map, obs, oa, PL_SEL_CUR, 1, &os);
	if (rc < 0)
		return rc;

	D_ASSERT(rc > 0);
	*rank_rebal = os.os_rank;
	return 0;
}

/**
 * Check if object rebuilding should be triggered for the failed target
 * identified by \a failed.
 *
 * It returns true only if:
 * - there is an object shard living in the failed target \a failed
 * - the input object shard \obs is the coordinator of the redundancy group of
 *   the failed object shard.
 */
bool
pl_map_obj_rebuild(pl_map_t *map, pl_obj_shard_t *obs, pl_obj_attr_t *oa,
		   daos_rank_t failed, pl_obj_shard_t *obs_rbd)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_rebuild != NULL);

	memset(obs_rbd, -1, sizeof(*obs_rbd));
	return map->pm_ops->o_obj_rebuild(map, obs, oa, failed, obs_rbd);
}

/**
 * Check if an object shard \a obs should be recovered for (moved back to)
 * the recovered target identified by \a recovered.
 */
bool
pl_map_obj_recover(pl_map_t *map, pl_obj_shard_t *obs,
		   pl_obj_attr_t *oa, daos_rank_t recovered)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_recover != NULL);

	return map->pm_ops->o_obj_recover(map, obs, oa, recovered);
}
