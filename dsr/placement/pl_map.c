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

void
pl_map_destroy(pl_map_t *map)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_destroy != NULL);

	map->pm_ops->o_destroy(map);
}

void pl_map_print(pl_map_t *map)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_print != NULL);

	return map->pm_ops->o_print(map);
}

int
pl_map_obj_select(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
		  unsigned int nranks, daos_rank_t *ranks)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_select != NULL);

	memset(ranks, -1, nranks * sizeof(*ranks));
	return map->pm_ops->o_obj_select(map, id, oa, nranks, ranks);
}

int
pl_map_obj_rebuild(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
		   daos_rank_t *target)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_select != NULL);

	*target = -1;
	return map->pm_ops->o_obj_select(map, id, oa, 1, target);
}

bool
pl_map_obj_failover(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
		    daos_rank_t current, daos_rank_t failed,
		    daos_rank_t *failover)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_failover != NULL);

	*failover = -1;
	return map->pm_ops->o_obj_failover(map, id, oa, current,
					   failed, failover);
}

bool pl_map_obj_recover(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
			daos_rank_t current, daos_rank_t recovered)
{
	D_ASSERT(map->pm_ops != NULL);
	D_ASSERT(map->pm_ops->o_obj_recover != NULL);

	return map->pm_ops->o_obj_recover(map, id, oa, current, recovered);
}
