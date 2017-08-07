/* Copyright (C) 2016-2017 Intel Corporation
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

#include <pouch/common.h>

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

int
crt_rank_list_dup_sort_uniq(crt_rank_list_t **dst, const crt_rank_list_t *src,
			    bool input)
{
	crt_rank_list_t		*rank_list;
	crt_rank_t		 rank_tmp;
	uint32_t		 rank_num, identical_num;
	int			 i, j;
	int			 rc = 0;

	rc = crt_rank_list_dup(dst, src, input);
	if (rc != 0) {
		C_ERROR("crt_rank_list_dup failed, rc: %d.\n", rc);
		C_GOTO(out, rc);
	}

	rank_list = *dst;
	if (rank_list == NULL || rank_list->rl_ranks == NULL)
		C_GOTO(out, rc);

	crt_rank_list_sort(rank_list);

	/* uniq - remove same rank number in the list */
	rank_num = (input == true) ? src->rl_nr.num : src->rl_nr.num_out;
	if (rank_num <= 1)
		C_GOTO(out, rc);
	identical_num = 0;
	rank_tmp = rank_list->rl_ranks[0];
	for (i = 1; i < rank_num - identical_num; i++) {
		if (rank_tmp == rank_list->rl_ranks[i]) {
			identical_num++;
			for (j = i; j < rank_num; j++)
				rank_list->rl_ranks[j - 1] =
					rank_list->rl_ranks[j];
			C_DEBUG("%s:%d, rank_list %p, removed identical "
				"rank[%d](%d).\n", __FILE__, __LINE__,
				rank_list, i, rank_tmp);
		}
		rank_tmp = rank_list->rl_ranks[i];
	}
	if (identical_num != 0) {
		if (input == true)
			rank_list->rl_nr.num -= identical_num;
		else
			rank_list->rl_nr.num_out -= identical_num;
		C_DEBUG("%s:%d, rank_list %p, removed %d ranks.\n",
			__FILE__, __LINE__, rank_list, identical_num);
	}

out:
	return rc;
}

/*
 * Filter the rank list:
 * 1) exclude == true, the result dst_set does not has any rank belong to src_
 *    set, i.e. the ranks belong to src_set will be filtered out from dst_set
 * 2) exclude == false, the result dst_set does not has any rank not belong to
 *    src_set, i.e. the ranks not belong to src_set will be filtered out
 *    from dst_set
 */
void
crt_rank_list_filter(crt_rank_list_t *src_set, crt_rank_list_t *dst_set,
		     bool input, bool exclude)
{
	crt_rank_t	rank;
	uint32_t	rank_num, filter_num;
	int		i, j;

	if (src_set == NULL || dst_set == NULL)
		return;
	if (src_set->rl_ranks == NULL || dst_set->rl_ranks == NULL)
		return;

	rank_num = (input == true) ? dst_set->rl_nr.num :
				     dst_set->rl_nr.num_out;
	if (rank_num == 0)
		return;

	filter_num = 0;
	for (i = 0; i < rank_num - filter_num; i++) {
		rank = dst_set->rl_ranks[i];
		if (crt_rank_in_rank_list(src_set, rank, input) != exclude)
			continue;
		filter_num++;
		for (j = i; j < rank_num - 1; j++)
			dst_set->rl_ranks[j] =
				dst_set->rl_ranks[j + 1];
		C_DEBUG("%s:%d, rank_list %p, filter rank[%d](%d).\n",
			__FILE__, __LINE__, dst_set, i, rank);
		/* as dst_set moved one item ahead */
		i--;
	}
	if (filter_num != 0) {
		if (input == true)
			dst_set->rl_nr.num -= filter_num;
		else
			dst_set->rl_nr.num_out -= filter_num;
		C_DEBUG("%s:%d, rank_list %p, filter %d ranks.\n",
			__FILE__, __LINE__, dst_set, filter_num);
	}
}

crt_rank_list_t *
crt_rank_list_alloc(uint32_t size)
{
	crt_rank_list_t		*rank_list;
	int			 i;

	C_ALLOC_PTR(rank_list);
	if (rank_list == NULL)
		return NULL;

	if (size == 0) {
		rank_list->rl_nr.num = 0;
		rank_list->rl_nr.num_out = 0;
		rank_list->rl_ranks = NULL;
		return rank_list;
	}

	C_ALLOC(rank_list->rl_ranks, size * sizeof(crt_rank_t));
	if (rank_list->rl_ranks == NULL) {
		C_FREE_PTR(rank_list);
		return NULL;
	}

	rank_list->rl_nr.num = size;
	rank_list->rl_nr.num_out = size;
	for (i = 0; i < size; i++)
		rank_list->rl_ranks[i] = i;

	return rank_list;
}

crt_rank_list_t *
crt_rank_list_realloc(crt_rank_list_t *ptr, uint32_t size)
{
	crt_rank_t *new_rl_ranks;

	if (ptr == NULL)
		return crt_rank_list_alloc(size);
	if (size == 0) {
		crt_rank_list_free(ptr);
		return NULL;
	}
	new_rl_ranks = C_REALLOC(ptr->rl_ranks, size * sizeof(crt_rank_t));
	if (new_rl_ranks != NULL) {
		ptr->rl_ranks = new_rl_ranks;
		ptr->rl_nr.num = size;
	} else {
		C_ERROR("crt_rank_list_realloc() failed.\n");
		ptr = NULL;
	}

	return ptr;
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
		C_DEBUG("crt_rank_list_copy do nothing, dst: %p, src: %p.\n",
			dst, src);
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

/**
 * delete the first occurance of rank, shrink the arrray storage size in
 * rank_list, and reduce the size of rank_list by 1.
 */
int
crt_rank_list_del(crt_rank_list_t *rank_list, crt_rank_t rank)
{
	uint32_t	 new_num;
	uint32_t	 num_bytes;
	void		*dest;
	void		*src;
	int		 idx;
	int		 rc = 0;

	if (rank_list == NULL) {
		C_ERROR("rank_list cannot be NULL\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	if (!crt_rank_list_find(rank_list, rank, &idx)) {
		C_DEBUG("Rank %d not in the rank list.\n", rank);
		C_GOTO(out, rc);
	}
	new_num = rank_list->rl_nr.num - 1;
	src = &rank_list->rl_ranks[idx + 1];
	dest = &rank_list->rl_ranks[idx];
	num_bytes = (new_num - idx) * sizeof(crt_rank_t);
	if (num_bytes == 0)
		C_GOTO(out, rc);
	memmove(dest, src, num_bytes);
	rank_list = crt_rank_list_realloc(rank_list, new_num);
	if (rank_list == NULL) {
		C_ERROR("crt_rank_list_realloc() failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
out:
	return rc;
}

int
crt_rank_list_append(crt_rank_list_t *rank_list, crt_rank_t rank)
{
	uint32_t		 old_num;
	crt_rank_list_t		*new_rank_list;
	int			 rc = 0;

	old_num = rank_list->rl_nr.num;
	new_rank_list = crt_rank_list_realloc(rank_list, old_num + 1);
	if (new_rank_list == NULL) {
		C_ERROR("crt_rank_list_realloc() failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
	new_rank_list->rl_ranks[old_num] = rank;

out:
	return rc;
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
crt_rank_in_rank_list(crt_rank_list_t *rank_list, crt_rank_t rank, bool input)
{
	uint32_t	rank_num, i;

	if (rank_list == NULL)
		return false;

	rank_num = (input == true) ? rank_list->rl_nr.num :
				     rank_list->rl_nr.num_out;
	for (i = 0; i < rank_num; i++) {
		if (rank_list->rl_ranks[i] == rank)
			return true;
	}
	return false;
}

/*
 * query the idx of rank within the rank_list.
 *
 * return -CER_OOG when rank not belong to rank list.
 */
int
crt_idx_in_rank_list(crt_rank_list_t *rank_list, crt_rank_t rank, uint32_t *idx,
		     bool input)
{
	uint32_t	rank_num;
	bool		found = false;
	uint32_t	i;

	if (rank_list == NULL || idx == NULL)
		return -CER_INVAL;

	rank_num = (input == true) ? rank_list->rl_nr.num :
				     rank_list->rl_nr.num_out;
	for (i = 0; i < rank_num; i++) {
		if (rank_list->rl_ranks[i] == rank) {
			found = true;
			*idx = i;
			break;
		}
	}

	return found == true ? 0 : -CER_OOG;
}

/**
 * Print out the content of a rank_list to stderr.
 *
 * \param  rank_list [IN]	the rank list to print
 * \param  name      [IN]	a name to describe the rank list
 *
 * \return			0 on success, a negative value on error
 */
int
crt_rank_list_dump(crt_rank_list_t *rank_list, crt_string_t name)
{
	int		 width;
	char		*tmp_str;
	int		 i;
	int		 idx = 0;
	int		 rc = 0;

	width = strnlen(name, CRT_GROUP_ID_MAX_LEN + 1);
	if (width > CRT_GROUP_ID_MAX_LEN) {
		C_ERROR("name parameter too long.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	width = 0;
	for (i = 0; i < rank_list->rl_nr.num; i++)
		width += snprintf(NULL, 0, "%d ", rank_list->rl_ranks[i]);
	width++;
	C_ALLOC(tmp_str, width);
	if (tmp_str == NULL) {
		C_ERROR("memory allocation failed.\n");
		C_GOTO(out, rc = -CER_NOMEM);
	}
	for (i = 0; i < rank_list->rl_nr.num; i++)
		idx += sprintf(&tmp_str[idx], "%d ", rank_list->rl_ranks[i]);
	tmp_str[width - 1] = '\0';
	C_DEBUG("%s, %d ranks: %s\n", name, rank_list->rl_nr.num, tmp_str);
	C_FREE(tmp_str, width);

out:
	return rc;
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

static inline bool
crt_is_integer_str(char *str)
{
	char	*p;

	p = str;
	if (p == NULL || strlen(p) == 0)
		return false;

	while (*p != '\0') {
		if (*p <= '9' && *p >= '0') {
			p++;
			continue;
		} else {
			return false;
		}
	}

	return true;
}

/**
 * get a bool type environment variables
 *
 * \param env	[IN]		name of the environment variable
 * \param bool_val [IN/OUT]	returned value of the ENV. Will not change the
 *				original value if ENV is not set. Set as false
 *				if the env is set to 0, otherwise set as true.
 */
void crt_getenv_bool(const char *env, bool *bool_val)
{
	char *env_val;

	if (env == NULL)
		return;
	C_ASSERT(bool_val != NULL);

	env_val = getenv(env);
	if (!env_val)
		return;

	/* treats any valid non-integer string as true */
	if (!crt_is_integer_str(env_val))
		*bool_val = true;

	*bool_val = (atoi(env_val) == 0 ? false : true);
}

/**
 * get an integer type environment variables
 *
 * \param env	[IN]		name of the environment variable
 * \param int_val [IN/OUT]	returned value of the ENV. Will not change the
 *				original value if ENV is not set or set as a
 *				non-integer value.
 */
void crt_getenv_int(const char *env, unsigned *int_val)
{
	char		*env_val;
	unsigned	 value;

	if (env == NULL || int_val == NULL)
		return;

	env_val = getenv(env);
	if (!env_val)
		return;

	if (!crt_is_integer_str(env_val)) {
		C_ERROR("ENV %s is not integer.\n", env_val);
		return;
	}

	value = atoi(env_val);
	C_DEBUG("crt_getenv_int(), get ENV %s as %d.\n", env, value);
	*int_val = value;
}
