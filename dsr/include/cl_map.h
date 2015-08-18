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
 * dsr/include/cl_map.h
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#ifndef __CLUSTER_MAP_H__
#define __CLUSTER_MAP_H__

#include <daos_common.h>
#include <daos_types.h>

/**
 * cluster component types
 * sparse values in case we want to add more types
 */
typedef enum {
	/* dummy type for extending clustre map */
	CL_COMP_DUMMY		= 0,
	CL_COMP_ROOT		= 1,
	CL_COMP_RACK		= 10,
	CL_COMP_BLADE		= 20,
	CL_COMP_BOARD		= 30,
	CL_COMP_NODE		= 40,
	CL_COMP_TARGET		= 50,
	/* TODO: more types */
} cl_comp_type_t;

/** cluster component status */
typedef enum {
	CL_COMP_ST_UNKNOWN,
	/* intermediate state for cluster map change */
	CL_COMP_ST_NEW,
	/* component is heathy */
	CL_COMP_ST_UP,
	/* component is dead */
	CL_COMP_ST_DOWN,
} cl_comp_state_t;

typedef struct {
	/** cl_comp_type_t */
	uint32_t		co_type:8;
	/** cl_comp_state_t */
	uint32_t		co_status:3;
	/** version it's been added */
	uint32_t		co_ver;
	/** failure sequence */
	uint32_t		co_fseq;
	/** identifier of component */
	uint32_t		co_rank;
} cl_component_t;

/** a leaf of cluster map */
typedef cl_component_t		cl_target_t;

static inline bool
cl_comp_is_unknown(cl_component_t *comp)
{
	return comp->co_status == CL_COMP_ST_UNKNOWN;
}

static inline bool
cl_comp_is_new(cl_component_t *comp)
{
	return comp->co_status == CL_COMP_ST_NEW;
}

static inline bool
cl_comp_is_up(cl_component_t *comp)
{
	return comp->co_status == CL_COMP_ST_UP;
}

static inline bool
cl_comp_is_down(cl_component_t *comp)
{
	return comp->co_status == CL_COMP_ST_DOWN;
}

/**
 * an intermediate component in cluster map, a domain can either contains low
 * level domains or just leaf targets.
 */
typedef struct cl_domain {
	/** embedded component for myself */
	cl_component_t		 cd_comp;
	/** # all targets within this domain */
	unsigned int		 cd_ntargets;
	/** # direct child domains */
	unsigned int		 cd_nchildren;
	/**
	 * all targets within this domain
	 * for the last level domain, it points to the first direct targets
	 * for the intermediate domain, it ponts to the first indirect targets
	 */
	cl_target_t		*cd_targets;
	/**
	 * child domains within current domain, it is NULL for the last
	 * level domain.
	 */
	struct cl_domain	*cd_children;
} cl_domain_t;

/** cluster component buffer */
typedef cl_domain_t		 cl_buf_t;

typedef struct {
	/** # of domains in the top level */
	unsigned int		 cc_ndoms_top;;
	/** # of all domains */
	unsigned int		 cc_ndoms;
	/** # of targets */
	unsigned int		 cc_ntargets;
	/** # of buffer layers */
	unsigned int		 cc_nlayers;
} cl_buf_count_t;

/** Cluster map */
typedef struct {
	/** Current version of cluster map */
	unsigned int		   clm_ver;
	/** the oldest Version of cluster map */
	unsigned int		   clm_ver_old;
	/**
	 * Tree root of all components.
	 * NB: All components must be stored in contigunous buffer.
	 */
	cl_domain_t		  *clm_root;
	/** # of targets in cluster map */
	unsigned int		   clm_ntargets;
	/** targets in ascending order for binary search */
	cl_target_t		 **clm_targets;
	/** # domain layers */
	unsigned int		   clm_nlayers;
	/** summary of all domains */
	unsigned int		   clm_ndoms_sum;
	/** domains in ascending order for binary search */
	unsigned int		  *clm_ndoms;
	cl_domain_t		***clm_doms;
} cl_map_t;

char *cl_comp_state2name(cl_comp_state_t state);
cl_comp_state_t cl_comp_name2state(char *name);

cl_comp_type_t cl_comp_name2type(char *name);
cl_comp_type_t cl_comp_abbr2type(char abbr);
char *cl_comp_type2name(cl_comp_type_t type);

static inline char *cl_comp_name(cl_component_t *comp)
{
	return cl_comp_type2name(comp->co_type);
}

static inline char *cl_domain_name(cl_domain_t *dom)
{
	return cl_comp_name(&dom->cd_comp);
}

bool cl_buf_sane(cl_buf_t *buf);
bool cl_buf_compat(cl_buf_t *buf, cl_map_t *map);
void cl_buf_count(cl_buf_t *buf, cl_buf_count_t *cntr);
void cl_buf_rebuild(cl_buf_t *buf, cl_buf_count_t *cntr);
void cl_buf_copy(cl_buf_t *dst, cl_buf_t *src);
unsigned int cl_buf_size(cl_buf_t *buf);
cl_buf_t *cl_buf_dup(cl_buf_t *buf);

int  cl_map_create(cl_buf_t *buf, cl_map_t **mapp);
void cl_map_destroy(cl_map_t *map);
int  cl_map_extend(cl_map_t *map, cl_buf_t *buf);
int  cl_map_find_buf(cl_map_t *map, cl_comp_type_t type, cl_buf_t **buf_p);
void cl_map_print(cl_map_t *map);

static inline bool
cl_map_empty(cl_map_t *map)
{
	return map->clm_root == NULL;
}

static inline unsigned int
cl_map_version(cl_map_t *map)
{
	return map->clm_ver;
}

static inline cl_buf_t *
cl_map_buf(cl_map_t *map)
{
	return (cl_buf_t *)map->clm_root;
}

static inline cl_target_t *
cl_map_targets(cl_map_t *map)
{
	if (cl_map_empty(map))
		return NULL;

	return map->clm_root->cd_targets;
}

static inline int
cl_map_ntargets(cl_map_t *map)
{
	return map->clm_ntargets;
}

cl_target_t *cl_target_find(cl_map_t *map, daos_rank_t rank);
cl_domain_t *cl_domain_find(cl_map_t *map, cl_comp_type_t type,
			    daos_rank_t rank);

int  cl_comp_set_state(cl_map_t *map, cl_comp_type_t type,
			   daos_rank_t rank, cl_comp_state_t state);
int  cl_comp_get_state(cl_map_t *map, cl_comp_type_t type,
			   daos_rank_t rank);

#define cl_target_set_state(map, rank, state)	\
	cl_comp_set_state(map, CL_COMP_TARGET, rank, state)

#define cl_target_get_state(map, rank)		\
	cl_comp_get_state(map, CL_COMP_TARGET, rank)

#endif /* __CLUSTER_MAP_H__ */
