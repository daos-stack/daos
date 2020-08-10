/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
