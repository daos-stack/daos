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
	if (dst == NULL || src == NULL) {
		D_DEBUG(DF_MISC, "daos_rank_list_copy do nothing, dst: %p, "
			"src: %p.\n", dst, src);
		return;
	}

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

static inline int
rank_compare(const void *rank1, const void *rank2)
{
	const daos_rank_t	*r1 = rank1;
	const daos_rank_t	*r2 = rank2;

	D_ASSERT(r1 != NULL && r2 != NULL);
	if (*r1 < *r2)
		return -1;
	else if (*r1 == *r2)
		return 0;
	else /* *r1 > *r2 */
		return 1;
}

void
daos_rank_list_sort(daos_rank_list_t *rank_list)
{
	qsort(rank_list->rl_ranks, rank_list->rl_nr.num,
	      sizeof(daos_rank_t), rank_compare);
}

/**
 * Must be previously sorted or not modified at all in order to guarantee
 * consistent indexes.
 **/
bool
daos_rank_list_find(daos_rank_list_t *rank_list, daos_rank_t rank, int *idx)
{
	int i;

	if (rank_list == NULL)
		return false;
	for (i = 0; i < rank_list->rl_nr.num; i++) {
		if (rank_list->rl_ranks[i] == rank) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

/*
 * Compare whether or not the two rank lists are identical.
 * This function possibly will change the order of the passed in rank list, it
 * will sort the rank list in order.
 */
bool
daos_rank_list_identical(daos_rank_list_t *rank_list1,
			 daos_rank_list_t *rank_list2, bool input)
{
	int i;

	if (rank_list1 == rank_list2)
		return true;
	if (rank_list1 == NULL || rank_list2 == NULL)
		return false;
	if (input == true) {
		if (rank_list1->rl_nr.num != rank_list2->rl_nr.num)
			return false;
		daos_rank_list_sort(rank_list1);
		for (i = 0; i < rank_list1->rl_nr.num; i++) {
			if (rank_list1->rl_ranks[i] != rank_list2->rl_ranks[i])
				return false;
		}
	} else {
		if (rank_list1->rl_nr.num_out != rank_list2->rl_nr.num_out)
			return false;
		daos_rank_list_sort(rank_list1);
		for (i = 0; i < rank_list1->rl_nr.num_out; i++) {
			if (rank_list1->rl_ranks[i] != rank_list2->rl_ranks[i])
				return false;
		}
	}
	return true;
}

/**
 * Initialise a scatter/gather list, create an array to store @nr iovecs.
 */
int
daos_sgl_init(daos_sg_list_t *sgl, unsigned int nr)
{
	memset(sgl, 0, sizeof(*sgl));

	sgl->sg_nr.num = sgl->sg_nr.num_out = nr;
	D_ALLOC(sgl->sg_iovs, nr * sizeof(*sgl->sg_iovs));

	return sgl->sg_iovs == NULL ? -DER_NOMEM : 0;
}

/**
 * Finalise a scatter/gather list, it can also free iovecs if @free_iovs
 * is true.
 */
void
daos_sgl_fini(daos_sg_list_t *sgl, bool free_iovs)
{
	int	i;

	if (sgl->sg_iovs == NULL)
		return;

	for (i = 0; free_iovs && i < sgl->sg_nr.num; i++) {
		if (sgl->sg_iovs[i].iov_buf != NULL) {
			D_FREE(sgl->sg_iovs[i].iov_buf,
			       sgl->sg_iovs[i].iov_buf_len);
		}
	}

	D_FREE(sgl->sg_iovs, sgl->sg_nr.num * sizeof(*sgl->sg_iovs));
	memset(sgl, 0, sizeof(*sgl));
}
