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
 * This file is part of daos. It implements some miscellaneous functions which
 * not belong to other parts.
 */

#include <daos/common.h>

int
daos_rank_list_dup(dtp_rank_list_t **dst, const dtp_rank_list_t *src,
		   bool input)
{
	dtp_rank_list_t	*rank_list;
	uint32_t		rank_num;
	dtp_size_t		size;
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

	size = rank_num * sizeof(dtp_rank_t);
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
daos_rank_list_free(dtp_rank_list_t *rank_list)
{
	if (rank_list == NULL)
		return;
	if (rank_list->rl_ranks != NULL)
		D_FREE(rank_list->rl_ranks,
		       rank_list->rl_nr.num * sizeof(dtp_rank_t));
	D_FREE_PTR(rank_list);
}

void
daos_rank_list_copy(dtp_rank_list_t *dst, dtp_rank_list_t *src, bool input)
{
	if (dst == NULL || src == NULL) {
		D_DEBUG(DF_MISC, "daos_rank_list_copy do nothing, dst: %p, "
			"src: %p.\n", dst, src);
		return;
	}

	if (input == true) {
		dst->rl_nr.num = src->rl_nr.num;
		memcpy(dst->rl_ranks, src->rl_ranks,
		       dst->rl_nr.num * sizeof(dtp_rank_t));
	} else {
		dst->rl_nr.num_out = src->rl_nr.num_out;
		memcpy(dst->rl_ranks, src->rl_ranks,
		       dst->rl_nr.num_out * sizeof(dtp_rank_t));
	}
}

static inline int
rank_compare(const void *rank1, const void *rank2)
{
	const dtp_rank_t	*r1 = rank1;
	const dtp_rank_t	*r2 = rank2;

	D_ASSERT(r1 != NULL && r2 != NULL);
	if (*r1 < *r2)
		return -1;
	else if (*r1 == *r2)
		return 0;
	else /* *r1 > *r2 */
		return 1;
}

void
daos_rank_list_sort(dtp_rank_list_t *rank_list)
{
	if (rank_list == NULL)
		return;
	qsort(rank_list->rl_ranks, rank_list->rl_nr.num,
	      sizeof(dtp_rank_t), rank_compare);
}

/**
 * Must be previously sorted or not modified at all in order to guarantee
 * consistent indexes.
 **/
bool
daos_rank_list_find(dtp_rank_list_t *rank_list, dtp_rank_t rank, int *idx)
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
daos_rank_list_identical(dtp_rank_list_t *rank_list1,
			 dtp_rank_list_t *rank_list2, bool input)
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

/* check whether one rank included in the rank list, all are global ranks. */
bool
daos_rank_in_rank_list(dtp_rank_list_t *rank_list, dtp_rank_t rank)
{
	int i;

	if (rank_list == NULL)
		return false;

	for (i = 0; i < rank_list->rl_nr.num; i++) {
		if (rank_list->rl_ranks[i] == rank)
			return true;
	}
	return false;
}

/**
 * Initialise a scatter/gather list, create an array to store @nr iovecs.
 */
int
daos_sgl_init(dtp_sg_list_t *sgl, unsigned int nr)
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
daos_sgl_fini(dtp_sg_list_t *sgl, bool free_iovs)
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
