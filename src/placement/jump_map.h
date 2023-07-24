/**
 * (C) Copyright 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * src/placement/jump_map.h
 */

#ifndef __JUMP_MAP_H__
#define __JUMP_MAP_H__

#include <daos/placement.h>

#define JMOP_PD_INLINE	(8)
/**
 * Contains information related to object layout size.
 */
struct jm_obj_placement {
	/* root domain, used when no PD defined */
	struct pool_domain	 *jmop_root;
	unsigned int		  jmop_grp_size;
	unsigned int		  jmop_grp_nr;
	pool_comp_type_t	  jmop_fdom_lvl;
	uint32_t		  jmop_dom_nr;
	/* #PDs to-be-used for the obj */
	unsigned int		  jmop_pd_nr;
	/* For zero jmop_pd_nr, non-sense for below fields */
	/* PD group size (min(pda, doms_per_pd)) */
	unsigned int		  jmop_pd_grp_size;
	/* PD domain pointers array */
	struct pool_domain	**jmop_pd_ptrs;
	struct pool_domain	 *jmop_pd_ptrs_inline[JMOP_PD_INLINE];
};

/**
 * jump_map Placement map structure used to place objects.
 * The map is returned as a struct pl_map and then converted back into a
 * pl_jump_map once passed from the caller into the object placement
 * functions.
 */
struct pl_jump_map {
	/** placement map interface */
	struct pl_map		jmp_map;
	/** Number of performance domains (can be zero) */
	unsigned int		jmp_pd_nr;
	/* Total size of domain type specified during map creation */
	unsigned int		jmp_domain_nr;
	/* # UPIN targets */
	unsigned int		jmp_target_nr;
	/* The dom that will contain no colocated shards */
	pool_comp_type_t	jmp_redundant_dom;
};

struct pool_domain *
jm_obj_shard_pd(struct jm_obj_placement *jmop, uint32_t shard);

void
get_target(struct pool_domain *root, struct pool_domain *curr_pd, uint32_t layout_ver,
	   struct pool_target **target, struct pool_domain **dom, uint64_t key,
	   uint8_t *dom_used, uint8_t *dom_full, uint8_t *dom_cur_grp_used,
	   uint8_t *dom_cur_grp_real, uint8_t *tgts_used, int shard_num,
	   uint32_t allow_status, uint32_t allow_version, pool_comp_type_t fdom_lvl,
	   uint32_t grp_size, uint32_t *spare_left, bool *spare_avail);
#endif /* __JUMP_MAP_H__ */
