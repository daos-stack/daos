/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
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
	/** only support rim map for the time being */
	PL_TYPE_RIM,
	/** reserved */
	PL_TYPE_PETALS,
} pl_map_type_t;

/** a target on the rim */
typedef struct pl_target {
	/** offset within cluster map */
	unsigned int		pt_pos;
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
	uint32_t		oa_start;
	/** number of stripes, it is optional */
	uint32_t		oa_nstripes;
	/** size of redundancy group */
	uint16_t		oa_rd_grp;
	/** number of spare nodes between redundancy groups */
	uint8_t			oa_nspares;
	/**
	 * While choosing a spare target, an object can skip from zero to
	 * \a oa_spare_skip redundancy groups between the failed target
	 * and spare target.
	 * The value is decided by hash result of object ID.
	 */
	uint8_t			oa_spare_skip;
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

/** metadata for daos-m object */
typedef struct pl_obj_shard {
	/** SR object ID */
	daos_obj_id_t		 os_id;
	/** object shard index */
	uint32_t		 os_sid;
	/** target rank to store this object */
	daos_rank_t		 os_rank;
	/** hash stride */
	uint64_t		 os_stride;
} pl_obj_shard_t;

typedef enum {
	PL_SEL_CUR,
	PL_SEL_GRP_CUR,
	PL_SEL_GRP_NEXT,
	PL_SEL_GRP_PREV,
	/** for KV object only */
	PL_SEL_GRP_SPLIT,
	PL_SEL_ALL,
} pl_select_opc_t;

/**
 * Function table for placement map.
 */
typedef struct pl_map_ops {
	/** create a placement map */
	int	(*o_create)(cl_map_t *cl_map, pl_map_attr_t *ma,
			    pl_map_t **mapp);
	/** destroy a placement map */
	void	(*o_destroy)(pl_map_t *map);
	/** print a placement map */
	void	(*o_print)(pl_map_t *map);

	/** object methods */
	/** see \a pl_map_obj_select and \a pl_map_obj_rebalance */
	int	(*o_obj_select)(pl_map_t *map, pl_obj_shard_t *obs,
				pl_obj_attr_t *oa, pl_select_opc_t select,
				unsigned int obs_arr_len,
				pl_obj_shard_t *obs_arr);
	/** see \a pl_map_obj_rebuild */
	bool	(*o_obj_rebuild)(pl_map_t *map, pl_obj_shard_t *obs,
				 pl_obj_attr_t *oa, daos_rank_t failed,
				 pl_obj_shard_t *obs_rbd);
	/** see \a pl_map_obj_recover */
	bool	(*o_obj_recover)(pl_map_t *map, pl_obj_shard_t *obs,
				 pl_obj_attr_t *oa, daos_rank_t recovered);
} pl_map_ops_t;

int  pl_map_create(cl_map_t *cl_map, pl_map_attr_t *ma, pl_map_t **mapp);
void pl_map_destroy(pl_map_t *map);
void pl_map_print(pl_map_t *map);
int  pl_map_obj_select(pl_map_t *map, pl_obj_shard_t *obs,
		       pl_obj_attr_t *oa, pl_select_opc_t select,
		       unsigned int obs_arr_len, pl_obj_shard_t *obs_arr);
int  pl_map_obj_rebalance(pl_map_t *map, pl_obj_shard_t *obs,
			  pl_obj_attr_t *oa, daos_rank_t *rank_rebal);
bool pl_map_obj_rebuild(pl_map_t *map, pl_obj_shard_t *obs,
			pl_obj_attr_t *oa, daos_rank_t failed,
			pl_obj_shard_t *obs_rbd);
bool pl_map_obj_recover(pl_map_t *map, pl_obj_shard_t *obs,
			pl_obj_attr_t *oa, daos_rank_t recovered);

#endif /* __PLACEMENT_MAP_H__ */
