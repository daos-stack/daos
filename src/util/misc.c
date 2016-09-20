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
 * This file is part of CaRT. It implements some miscellaneous functions which
 * not belong to other parts.
 */

#include <crt_util/common.h>

int
crt_rank_list_dup(crt_rank_list_t **dst, const crt_rank_list_t *src,
		   bool input)
{
	crt_rank_list_t		*rank_list;
	uint32_t		rank_num;
	crt_size_t		size;
	int			rc = 0;

	if (dst == NULL) {
		C_ERROR("Invalid parameter, dst: %p, src: %p.\n", dst, src);
		C_GOTO(out, rc = -CER_INVAL);
	}

	if (src == NULL) {
		*dst = NULL;
		C_GOTO(out, rc);
	}

	C_ALLOC_PTR(rank_list);
	if (rank_list == NULL) {
		C_ERROR("Cannot allocate memory for rank list.\n");
		C_GOTO(out, rc = -CER_NOMEM);
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
		C_GOTO(out, rc);
	}

	size = rank_num * sizeof(crt_rank_t);
	C_ALLOC(rank_list->rl_ranks, size);
	if (rank_list->rl_ranks == NULL) {
		C_ERROR("Cannot allocate memory for rl_ranks.\n");
		C_FREE_PTR(rank_list);
		C_GOTO(out, rc = -CER_NOMEM);
	}

	memcpy(rank_list->rl_ranks, src->rl_ranks, size);
	*dst = rank_list;
out:
	return rc;
}

void
crt_rank_list_free(crt_rank_list_t *rank_list)
{
	if (rank_list == NULL)
		return;
	if (rank_list->rl_ranks != NULL)
		C_FREE(rank_list->rl_ranks,
		       rank_list->rl_nr.num * sizeof(crt_rank_t));
	C_FREE_PTR(rank_list);
}

void
crt_rank_list_copy(crt_rank_list_t *dst, crt_rank_list_t *src, bool input)
{
	if (dst == NULL || src == NULL) {
		crt_log(MISC_DBG, "crt_rank_list_copy do nothing, dst: %p, "
			"src: %p.\n", dst, src);
		return;
	}

	if (input == true) {
		dst->rl_nr.num = src->rl_nr.num;
		memcpy(dst->rl_ranks, src->rl_ranks,
		       dst->rl_nr.num * sizeof(crt_rank_t));
	} else {
		dst->rl_nr.num_out = src->rl_nr.num_out;
		memcpy(dst->rl_ranks, src->rl_ranks,
		       dst->rl_nr.num_out * sizeof(crt_rank_t));
	}
}

static inline int
rank_compare(const void *rank1, const void *rank2)
{
	const crt_rank_t	*r1 = rank1;
	const crt_rank_t	*r2 = rank2;

	C_ASSERT(r1 != NULL && r2 != NULL);
	if (*r1 < *r2)
		return -1;
	else if (*r1 == *r2)
		return 0;
	else /* *r1 > *r2 */
		return 1;
}

void
crt_rank_list_sort(crt_rank_list_t *rank_list)
{
	if (rank_list == NULL)
		return;
	qsort(rank_list->rl_ranks, rank_list->rl_nr.num,
	      sizeof(crt_rank_t), rank_compare);
}

/**
 * Must be previously sorted or not modified at all in order to guarantee
 * consistent indexes.
 **/
bool
crt_rank_list_find(crt_rank_list_t *rank_list, crt_rank_t rank, int *idx)
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
crt_rank_list_identical(crt_rank_list_t *rank_list1,
			 crt_rank_list_t *rank_list2, bool input)
{
	int i;

	if (rank_list1 == rank_list2)
		return true;
	if (rank_list1 == NULL || rank_list2 == NULL)
		return false;
	if (input == true) {
		if (rank_list1->rl_nr.num != rank_list2->rl_nr.num)
			return false;
		crt_rank_list_sort(rank_list1);
		for (i = 0; i < rank_list1->rl_nr.num; i++) {
			if (rank_list1->rl_ranks[i] != rank_list2->rl_ranks[i])
				return false;
		}
	} else {
		if (rank_list1->rl_nr.num_out != rank_list2->rl_nr.num_out)
			return false;
		crt_rank_list_sort(rank_list1);
		for (i = 0; i < rank_list1->rl_nr.num_out; i++) {
			if (rank_list1->rl_ranks[i] != rank_list2->rl_ranks[i])
				return false;
		}
	}
	return true;
}

/* check whether one rank included in the rank list, all are global ranks. */
bool
crt_rank_in_rank_list(crt_rank_list_t *rank_list, crt_rank_t rank)
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
crt_sgl_init(crt_sg_list_t *sgl, unsigned int nr)
{
	memset(sgl, 0, sizeof(*sgl));

	sgl->sg_nr.num = sgl->sg_nr.num_out = nr;
	C_ALLOC(sgl->sg_iovs, nr * sizeof(*sgl->sg_iovs));

	return sgl->sg_iovs == NULL ? -CER_NOMEM : 0;
}

/**
 * Finalise a scatter/gather list, it can also free iovecs if @free_iovs
 * is true.
 */
void
crt_sgl_fini(crt_sg_list_t *sgl, bool free_iovs)
{
	int	i;

	if (sgl->sg_iovs == NULL)
		return;

	for (i = 0; free_iovs && i < sgl->sg_nr.num; i++) {
		if (sgl->sg_iovs[i].iov_buf != NULL) {
			C_FREE(sgl->sg_iovs[i].iov_buf,
			       sgl->sg_iovs[i].iov_buf_len);
		}
	}

	C_FREE(sgl->sg_iovs, sgl->sg_nr.num * sizeof(*sgl->sg_iovs));
	memset(sgl, 0, sizeof(*sgl));
}
