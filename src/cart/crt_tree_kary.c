/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It gives out the kary tree topo related
 * function implementation.
 */
#define D_LOGFAC	DD_FAC(grp)

#include "crt_internal.h"

static uint32_t
kary_get_children(uint32_t *children, uint32_t self, uint32_t size,
		  uint32_t ratio)
{
	uint32_t	rank, nchildren = 0;
	uint32_t	i;

	D_ASSERT(self < size);

	for (i = 0; i < ratio; i++) {
		rank = self * ratio + i + 1;
		if (rank >= size)
			break;
		if (children != NULL)
			children[nchildren] = rank;
		nchildren++;
	}

	return nchildren;
}

int
crt_kary_get_children_cnt(uint32_t grp_size, uint32_t tree_ratio,
			  uint32_t grp_root, uint32_t grp_self,
			  uint32_t *nchildren)
{
	uint32_t	tree_self;

	D_ASSERT(grp_size > 0);
	D_ASSERT(nchildren != NULL);
	D_ASSERT(tree_ratio >= CRT_TREE_MIN_RATIO &&
		 tree_ratio <= CRT_TREE_MAX_RATIO);

	tree_self = crt_grprank_2_teerank(grp_size, grp_root, grp_self);

	*nchildren = kary_get_children(NULL, tree_self, grp_size, tree_ratio);

	return 0;
}

int
crt_kary_get_children(uint32_t grp_size, uint32_t tree_ratio,
		      uint32_t grp_root, uint32_t grp_self, uint32_t *children)
{
	uint32_t	nchildren;
	uint32_t	tree_self;
	uint32_t	i;

	D_ASSERT(grp_size > 0);
	D_ASSERT(children != NULL);
	D_ASSERT(tree_ratio >= CRT_TREE_MIN_RATIO &&
		 tree_ratio <= CRT_TREE_MAX_RATIO);

	tree_self = crt_grprank_2_teerank(grp_size, grp_root, grp_self);

	nchildren = kary_get_children(children, tree_self, grp_size,
				      tree_ratio);

	for (i = 0; i < nchildren; i++)
		children[i] = crt_treerank_2_grprank(grp_size, grp_root,
						     children[i]);

	return 0;
}

int
crt_kary_get_parent(uint32_t grp_size, uint32_t tree_ratio, uint32_t grp_root,
		    uint32_t grp_self, uint32_t *parent)
{
	uint32_t	tree_self, tree_parent;

	D_ASSERT(grp_size > 0);
	D_ASSERT(parent != NULL);
	D_ASSERT(tree_ratio >= CRT_TREE_MIN_RATIO &&
		 tree_ratio <= CRT_TREE_MAX_RATIO);

	if (grp_self == grp_root)
		return -DER_INVAL;

	tree_self = crt_grprank_2_teerank(grp_size, grp_root, grp_self);
	D_ASSERT(tree_self != 0);

	tree_parent = (tree_self - 1) / tree_ratio;

	*parent = crt_treerank_2_grprank(grp_size, grp_root, tree_parent);

	return 0;
}

struct crt_topo_ops crt_kary_ops = {
	.to_get_children_cnt	= crt_kary_get_children_cnt,
	.to_get_children	= crt_kary_get_children,
	.to_get_parent		= crt_kary_get_parent
};
