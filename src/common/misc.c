/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * This file is part of daos. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#include <daos/common.h>

int
daos_rank_list_dup(daos_rank_list_t **dst, const daos_rank_list_t *src,
		   bool input)
{
	daos_rank_list_t	*rank_list;
	uint32_t		rank_num;
	daos_size_t		size;
	int			rc = 0;

	if (dst == NULL) {
		D_ERROR("Invalid parameter, dst: %p, src: %p.\n", dst, src);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (src == NULL) {
		*dst = NULL;
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(rank_list);
	if (rank_list == NULL) {
		D_ERROR("Cannot allocate memory for rank list.\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	if (input == true) {
		rank_num = src->rl_nr.num;
		rank_list->rl_nr.num = rank_num;
	} else {
		rank_num = src->rl_nr.num_out;
		rank_list->rl_nr.num_out = rank_num;
	}
	if (rank_num == 0) {
		rank_list->rl_ranks = NULL;
		*dst = rank_list;
		D_GOTO(out, rc);
	}

	size = rank_num * sizeof(daos_rank_t);
	D_ALLOC(rank_list->rl_ranks, size);
	if (rank_list->rl_ranks == NULL) {
		D_ERROR("Cannot allocate memory for rl_ranks.\n");
		D_FREE_PTR(rank_list);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	memcpy(rank_list->rl_ranks, src->rl_ranks, size);
	*dst = rank_list;
out:
	return rc;
}

void
daos_rank_list_free(daos_rank_list_t *rank_list)
{
	if (rank_list == NULL)
		return;
	if (rank_list->rl_ranks != NULL)
		D_FREE(rank_list->rl_ranks,
		       rank_list->rl_nr.num * sizeof(daos_rank_t));
	D_FREE_PTR(rank_list);
}

void
daos_rank_list_copy(daos_rank_list_t *dst, daos_rank_list_t *src, bool input)
{
	D_ASSERT(dst != NULL && src != NULL);
	if (input == true) {
		dst->rl_nr.num = src->rl_nr.num;
		memcpy(dst->rl_ranks, src->rl_ranks,
		       dst->rl_nr.num * sizeof(daos_rank_t));
	} else {
		dst->rl_nr.num_out = src->rl_nr.num_out;
		memcpy(dst->rl_ranks, src->rl_ranks,
		       dst->rl_nr.num_out * sizeof(daos_rank_t));
	}
}
