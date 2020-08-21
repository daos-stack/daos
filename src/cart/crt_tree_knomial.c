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
 * This file is part of CaRT. It gives out the knomial tree topo related
 * function implementation.
 */
#define D_LOGFAC	DD_FAC(grp)

#include "crt_internal.h"

struct knomial_number {
	uint32_t	digits[CRT_TREE_MAX_RATIO];
	uint32_t	ndigits;
	uint32_t	ratio;
};

static uint32_t
knomial_number_2_int(struct knomial_number *n)
{
	uint32_t i, index = 0;

	if (n->ndigits == 0)
		return 0;

	D_ASSERT(n->ndigits >= 1);
	i = n->ndigits - 1;
	while (1) {
		index = index * n->ratio + n->digits[i];
		if (i == 0)
			break;
		i--;
	}

	return index;
}

static void
int_2_knomial_number(uint32_t x, uint32_t ratio, struct knomial_number *n)
{
	n->ratio = ratio;
	n->ndigits = 0;
	memset((void *)n->digits, 0, sizeof(n->digits));

	while (x > 0) {
		D_ASSERT(n->ndigits < CRT_TREE_MAX_RATIO);
		n->digits[n->ndigits++] = x % ratio;
		x /= ratio;
	}
}

static uint32_t
knomial_get_children(uint32_t *children, uint32_t self, uint32_t size,
		     uint32_t ratio)
{
	struct knomial_number	n;
	uint32_t		inc = 1;
	uint32_t		nchildren = 0;
	uint32_t		i, digit;

	D_ASSERT(self < size);

	int_2_knomial_number(self, ratio, &n);
	for (digit = 0; (digit < CRT_TREE_MAX_RATIO) &&
		(digit > n.ndigits || n.digits[digit] == 0); digit++) {
		for (i = 1; i < ratio; i++) {
			uint32_t child = self + i*inc;

			if (child >= size)
				return nchildren;

			if (children != NULL)
				children[nchildren++] = child;
			else
				nchildren++;
		}
		inc *= ratio;
	}
	return nchildren;
}

static uint32_t
knomial_get_parent(uint32_t self, uint32_t ratio)
{
	struct knomial_number	n;
	uint32_t		i;

	int_2_knomial_number(self, ratio, &n);

	for (i = 0; i < n.ndigits; i++) {
		if (n.digits[i] != 0) {
			n.digits[i] = 0;
			break;
		}
	}

	return knomial_number_2_int(&n);
}

int
crt_knomial_get_children_cnt(uint32_t grp_size, uint32_t tree_ratio,
			     uint32_t grp_root, uint32_t grp_self,
			     uint32_t *nchildren)
{
	uint32_t	tree_self;

	D_ASSERT(grp_size > 0);
	D_ASSERT(nchildren != NULL);
	D_ASSERT(tree_ratio >= CRT_TREE_MIN_RATIO &&
		 tree_ratio <= CRT_TREE_MAX_RATIO);

	tree_self = crt_grprank_2_teerank(grp_size, grp_root, grp_self);

	*nchildren = knomial_get_children(NULL, tree_self, grp_size,
					  tree_ratio);

	return 0;
}

int
crt_knomial_get_children(uint32_t grp_size, uint32_t tree_ratio,
			 uint32_t grp_root, uint32_t grp_self,
			 uint32_t *children)
{
	uint32_t	nchildren;
	uint32_t	tree_self;
	uint32_t	i;

	D_ASSERT(grp_size > 0);
	D_ASSERT(children != NULL);
	D_ASSERT(tree_ratio >= CRT_TREE_MIN_RATIO &&
		 tree_ratio <= CRT_TREE_MAX_RATIO);

	tree_self = crt_grprank_2_teerank(grp_size, grp_root, grp_self);

	nchildren = knomial_get_children(children, tree_self, grp_size,
					 tree_ratio);
	for (i = 0; i < nchildren; i++)
		children[i] = crt_treerank_2_grprank(grp_size, grp_root,
						     children[i]);

	return 0;
}

int
crt_knomial_get_parent(uint32_t grp_size, uint32_t tree_ratio,
		       uint32_t grp_root, uint32_t grp_self, uint32_t *parent)
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

	tree_parent = knomial_get_parent(tree_self, tree_ratio);

	*parent = crt_treerank_2_grprank(grp_size, grp_root, tree_parent);

	return 0;
}

struct crt_topo_ops crt_knomial_ops = {
	.to_get_children_cnt	= crt_knomial_get_children_cnt,
	.to_get_children	= crt_knomial_get_children,
	.to_get_parent		= crt_knomial_get_parent
};
