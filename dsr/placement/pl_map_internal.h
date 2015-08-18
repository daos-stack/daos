/**
 * This file is part of daos_sr
 *
 * dsr/placement/pl_map_internal.h
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#ifndef __PL_MAP_INTERNAL_H__
#define __PL_MAP_INTERNAL_H__

#include <pl_map.h>

/** placement rim */
typedef struct pl_rim {
	/** self pointer, quick reference to myself */
	pl_target_t		*rim_self;
	/** all targets on the rim */
	pl_target_t		*rim_targets;
} pl_rim_t;

/** rim placement map, it can have multiple rims */
typedef struct pl_rim_map {
	/** common body */
	pl_map_t		 rmp_map;
	/** reference to cluster map */
	cl_map_t		*rmp_clmap;
	/** fault domain */
	cl_comp_type_t		 rmp_domain;
	/** number of domains */
	unsigned int		 rmp_ndomains;
	/** total number of targets, consistent hash ring size */
	unsigned int		 rmp_ntargets;
	/** number of rims, consistent hash ring size */
	unsigned int		 rmp_nrims;
	/** */
	unsigned int		 rmp_target_hbits;
	/** hash stride */
	double			 rmp_stride;
	/** array of rims */
	pl_rim_t		*rmp_rims;
	/** consistent hash ring of rims */
	uint64_t		*rmp_rim_hashes;
	/** consistent hash ring of targets */
	uint64_t		*rmp_target_hashes;
} pl_rim_map_t;

#endif /* __PL_MAP_INTERNAL_H__ */
