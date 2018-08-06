/* Copyright (C) 2016 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
	if (CRT_PMIX_ENABLED())
		D_ASSERT(grp_root < grp_size && grp_self < grp_size);
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
	if (CRT_PMIX_ENABLED())
		D_ASSERT(grp_root < grp_size && grp_self < grp_size);
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
	if (CRT_PMIX_ENABLED())
		D_ASSERT(grp_root < grp_size && grp_self < grp_size);
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
