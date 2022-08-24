/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It gives out the flat tree topo related
 * function implementation.
 */
#define D_LOGFAC	DD_FAC(grp)

#include "crt_internal.h"

int
crt_flat_get_children_cnt(uint32_t grp_size, uint32_t branch_ratio,
			  uint32_t grp_root, uint32_t grp_self,
			  uint32_t *nchildren)
{
	D_ASSERT(grp_size > 0);
	D_ASSERT(nchildren != NULL);

	if (grp_self == grp_root)
		*nchildren = grp_size - 1;
	else
		*nchildren = 0;

	return 0;
}

int
crt_flat_get_children(uint32_t grp_size, uint32_t branch_ratio,
		      uint32_t grp_root, uint32_t grp_self, uint32_t *children)
{
	int	i, j;

	D_ASSERT(grp_size > 0);
	D_ASSERT(children != NULL);

	if (grp_self != grp_root)
		return -DER_INVAL;

	for (i = 0, j = 0; i < grp_size; i++) {
		if (grp_root != i)
			children[j++] = i;
	}

	return 0;
}

int
crt_flat_get_parent(uint32_t grp_size, uint32_t branch_ratio, uint32_t grp_root,
		    uint32_t grp_self, uint32_t *parent)
{
	D_ASSERT(grp_size > 0);
	D_ASSERT(parent != NULL);

	if (grp_self == grp_root)
		return -DER_INVAL;

	*parent = grp_root;
	return 0;
}

struct crt_topo_ops crt_flat_ops = {
	.to_get_children_cnt	= crt_flat_get_children_cnt,
	.to_get_children	= crt_flat_get_children,
	.to_get_parent		= crt_flat_get_parent
};
