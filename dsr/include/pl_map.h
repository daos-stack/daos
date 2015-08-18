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
 * dsr/include/pl_map.h
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#ifndef __PLACEMENT_MAP_H__
#define __PLACEMENT_MAP_H__

#include <daos_common.h>
#include <daos_types.h>
#include <cl_map.h>

/** type of placement map, only support RIM for now */
typedef enum {
	PL_TYPE_UNKNOWN,
	PL_TYPE_RIM,
	PL_TYPE_PETALS,
} pl_map_type_t;

/** a target on the rim */
typedef struct pl_target {
	/** offset within cluster map */
	uint32_t		pt_pos;
} pl_target_t;

typedef struct pl_map_attr {
	pl_map_type_t		ma_type;
	unsigned int		ma_version;
	union {
		struct rim_map_attr {
			cl_comp_type_t	ra_domain;
			unsigned int	ra_nrims;
		} rim;
	} u;
} pl_map_attr_t;

/** object placement attributes */
typedef struct pl_obj_attr {
	uint64_t		oa_start;
	uint32_t		oa_nstripes;
	uint16_t		oa_rd_grp;
	uint16_t		oa_nspares;
	uint32_t		oa_cookie;
} pl_obj_attr_t;

struct pl_map_ops;

/** common header of all placement map */
typedef struct pl_map {
	/** type of placement map */
	pl_map_type_t		 pm_type;
	/** version of cluster map when this placement is created */
	uint32_t		 pm_ver;
	/** placement map operations */
	struct pl_map_ops	*pm_ops;
	/**
	 * TODO: add members
	 *
	 * struct list_head	ph_link;
	 */
} pl_map_t;

typedef struct pl_map_ops {
	int	(*o_create)(cl_map_t *cl_map, pl_map_attr_t *ma,
			    pl_map_t **mapp);
	void	(*o_destroy)(pl_map_t *map);
	void	(*o_print)(pl_map_t *map);

	int	(*o_obj_select)(pl_map_t *rimap, daos_obj_id_t id,
				pl_obj_attr_t *oa, unsigned int nranks,
				daos_rank_t *ranks);
	bool	(*o_obj_failover)(pl_map_t *map, daos_obj_id_t id,
				  pl_obj_attr_t *oa, daos_rank_t current,
				  daos_rank_t failed, daos_rank_t *failover);
	bool	(*o_obj_recover)(pl_map_t *map, daos_obj_id_t id,
				 pl_obj_attr_t *oa, daos_rank_t current,
				 daos_rank_t recovered);
} pl_map_ops_t;

int  pl_map_create(cl_map_t *cl_map, pl_map_attr_t *ma, pl_map_t **mapp);
void pl_map_destroy(pl_map_t *map);
void pl_map_print(pl_map_t *map);
int  pl_map_obj_select(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
		       unsigned int nranks, daos_rank_t *ranks);
int  pl_map_obj_rebuild(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
			daos_rank_t *target);
bool pl_map_obj_failover(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
			 daos_rank_t current, daos_rank_t failed,
			 daos_rank_t *failover);
bool pl_map_obj_recover(pl_map_t *map, daos_obj_id_t id, pl_obj_attr_t *oa,
			daos_rank_t current, daos_rank_t recovered);

#endif /* __PLACEMENT_MAP_H__ */
