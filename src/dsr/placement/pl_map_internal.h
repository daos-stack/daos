/**
 * (C) Copyright 2016 Intel Corporation.
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
