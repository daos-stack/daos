/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It gives out the data types and function
 * declarations for tree topo.
 */

#ifndef __CRT_TREE_H__
#define __CRT_TREE_H__

/*
 * Query specific tree topo's number of children, child rank number, or parent
 * rank number.
 */
int crt_tree_get_nchildren(struct crt_grp_priv *grp_priv, uint32_t grp_ver,
			   d_rank_list_t *exclude_ranks, int tree_topo,
			   d_rank_t grp_root, d_rank_t grp_self,
			   uint32_t *nchildren);
int crt_tree_get_children(struct crt_grp_priv *grp_priv, uint32_t grp_ver,
			  bool filter_invert, d_rank_list_t *filter_ranks,
			  int tree_topo, d_rank_t grp_root, d_rank_t grp_self,
			  d_rank_list_t **children_rank_list, bool *ver_match);
int crt_tree_get_parent(struct crt_grp_priv *grp_priv, uint32_t grp_ver,
			d_rank_list_t *exclude_ranks, int tree_topo,
			d_rank_t grp_root, d_rank_t grp_self,
			d_rank_t *parent_rank);

/*
 * all specific tree type's calculations are based on group rank number.
 * some different types of rank:
 * 1. primary rank
 *    rank number in primary group, crt_grp_priv::gp_membs
 * 2. group rank
 *    rank number in group created from subset of primary group. group rank
 *    within primary group equals primary rank.
 *    group rank = crt_grp_priv::gp_self
 *    primary rank = crt_grp_priv::gp_membs.rl_ranks[group_rank]
 * 3. tree rank
 *    rank number in tree, one node can have different tree_rank for different
 *    tree topo.
 *    assume group_root is the group rank of the root in the tree topo, then:
 *    tree_rank  = (group_rank - group_root + group_size) % (group_size)
 *    group_rank = (tree_rank + group_root) % (group_size)
 */
typedef int (*crt_topo_get_children_cnt_t)(uint32_t grp_size,
					   uint32_t branch_ratio,
					   uint32_t grp_root,
					   uint32_t grp_self,
					   uint32_t *nchildren);
typedef int (*crt_topo_get_children_t)(uint32_t grp_size, uint32_t branch_ratio,
				       uint32_t grp_root, uint32_t grp_self,
				       uint32_t *children);
typedef int (*crt_topo_get_parent_t)(uint32_t grp_size, uint32_t branch_ratio,
				     uint32_t grp_root, uint32_t grp_self,
				     uint32_t *parent);

struct crt_topo_ops {
	crt_topo_get_children_cnt_t	to_get_children_cnt;
	crt_topo_get_children_t		to_get_children;
	crt_topo_get_parent_t		to_get_parent;
};

extern struct crt_topo_ops	 crt_flat_ops;
extern struct crt_topo_ops	 crt_kary_ops;
extern struct crt_topo_ops	 crt_knomial_ops;

extern struct crt_topo_ops	*crt_tops[];

/* some simple helpers */
static inline int
crt_tree_type(int tree_topo)
{
	return tree_topo >> CRT_TREE_TYPE_SHIFT;
}

static inline int
crt_tree_ratio(int tree_topo)
{
	return tree_topo & ((1U << CRT_TREE_TYPE_SHIFT) - 1);
}

static inline bool
crt_tree_topo_valid(int tree_topo)
{
	int	tree_type, tree_ratio;
	bool	valid;

	tree_type = crt_tree_type(tree_topo);
	tree_ratio = crt_tree_ratio(tree_topo);
	if (tree_type >= CRT_TREE_MIN && tree_type <= CRT_TREE_MAX &&
	    (tree_type == CRT_TREE_FLAT || (tree_ratio >= CRT_TREE_MIN_RATIO &&
					   tree_ratio <= CRT_TREE_MAX_RATIO))) {
		valid = true;
	} else {
		D_ERROR("invalid parameter, tree_type %d, tree_ratio %d.\n",
			tree_type, tree_ratio);
		valid = false;
	}

	return valid;
}

static inline uint32_t
crt_treerank_2_grprank(uint32_t grp_size, uint32_t grp_root, uint32_t tree_rank)
{
	D_ASSERT(grp_size > 0);
	D_ASSERT(grp_root < grp_size);
	D_ASSERT(tree_rank < grp_size);

	return (tree_rank + grp_root) % grp_size;
}

static inline uint32_t
crt_grprank_2_teerank(uint32_t grp_size, uint32_t grp_root, uint32_t grp_rank)
{
	D_ASSERT(grp_size > 0);
	D_ASSERT(grp_root < grp_size);
	D_ASSERT(grp_rank < grp_size);

	return (grp_rank + grp_size - grp_root) % grp_size;
}

#endif /* __CRT_TREE_H__ */
