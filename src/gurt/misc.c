/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#define D_LOGFAC	DD_FAC(misc)

#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>

#include <malloc.h>

#include <gurt/common.h>
#include <gurt/atomic.h>

/* state buffer for DAOS rand and srand calls, NOT thread safe */
static struct drand48_data randBuffer = {0};

void
d_srand(long int seedval)
{
	srand48_r(seedval, &randBuffer);
}

long int
d_rand()
{
	long int result;

	lrand48_r(&randBuffer, &result);
	return result;
}

/* Developer/debug version, poison memory on free.
 * This tries several ways to access the buffer size however none of them are perfect so for now
 * this is no in release builds.
 */

#ifdef DAOS_BUILD_RELEASE
void
d_free(void *ptr)
{
	free(ptr);
}

#else

static size_t
_f_get_alloc_size(void *ptr)
{
	size_t size = malloc_usable_size(ptr);
	size_t obs;

	obs = __builtin_object_size(ptr, 0);
	if (obs != -1 && obs < size)
		size = obs;

#if __USE_FORTIFY_LEVEL > 2
	obs = __builtin_dynamic_object_size(ptr, 0);
	if (obs != -1 && obs < size)
		size = obs;
#endif

	return size;
}

void
d_free(void *ptr)
{
	size_t msize = _f_get_alloc_size(ptr);

	memset(ptr, 0x42, msize);
	free(ptr);
}

#endif

void *
d_calloc(size_t count, size_t eltsize)
{
	return calloc(count, eltsize);
}

void *
d_malloc(size_t size)
{
	return malloc(size);
}

void *
d_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

char *
d_strndup(const char *s, size_t n)
{
	return strndup(s, n);
}

int
d_asprintf(char **strp, const char *fmt, ...)
{
	va_list	ap;
	int	rc;

	va_start(ap, fmt);
	rc = vasprintf(strp, fmt, ap);
	va_end(ap);

	return rc;
}

char *
d_asprintf2(int *_rc, const char *fmt, ...)
{
	char   *str = NULL;
	va_list ap;
	int     rc;

	va_start(ap, fmt);
	rc = vasprintf(&str, fmt, ap);
	va_end(ap);

	*_rc = rc;

	if (rc == -1)
		return NULL;

	return str;
}

char *
d_realpath(const char *path, char *resolved_path)
{
	return realpath(path, resolved_path);
}

void *
d_aligned_alloc(size_t alignment, size_t size, bool zero)
{
	void *buf = aligned_alloc(alignment, size);

	if (!zero || buf == NULL)
		return buf;
	memset(buf, 0, size);
	return buf;
}

int
d_rank_list_dup(d_rank_list_t **dst, const d_rank_list_t *src)
{
	d_rank_list_t	*rank_list = NULL;
	int		 rc = 0;

	if (dst == NULL) {
		D_ERROR("Invalid parameter, dst: %p, src: %p.\n", dst, src);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (src == NULL)
		D_GOTO(out, 0);

	D_ALLOC_PTR(rank_list);
	if (rank_list == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rank_list->rl_nr = src->rl_nr;
	if (rank_list->rl_nr == 0)
		D_GOTO(out, 0);

	D_ALLOC_ARRAY(rank_list->rl_ranks, rank_list->rl_nr);
	if (rank_list->rl_ranks == NULL) {
		D_FREE(rank_list);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	memcpy(rank_list->rl_ranks, src->rl_ranks,
		rank_list->rl_nr * sizeof(*rank_list->rl_ranks));

out:
	if (rc == 0)
		*dst = rank_list;
	return rc;
}

int
d_rank_list_dup_sort_uniq(d_rank_list_t **dst, const d_rank_list_t *src)
{
	d_rank_list_t		*rank_list;
	d_rank_t		rank_tmp;
	uint32_t		rank_num, identical_num;
	int			i, j;
	int			rc = 0;

	rc = d_rank_list_dup(dst, src);
	if (rc != 0) {
		D_ERROR("d_rank_list_dup() failed, "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, 0);
	}

	rank_list = *dst;
	if (rank_list == NULL || rank_list->rl_ranks == NULL)
		D_GOTO(out, 0);

	d_rank_list_sort(rank_list);

	/* uniq - remove same rank number in the list */
	rank_num = src->rl_nr;
	if (rank_num <= 1)
		D_GOTO(out, 0);
	identical_num = 0;
	rank_tmp = rank_list->rl_ranks[0];
	for (i = 1; i < rank_num; i++) {
		if (rank_tmp == rank_list->rl_ranks[i]) {
			identical_num++;
			for (j = i; j < rank_num; j++)
				rank_list->rl_ranks[j - 1] = rank_list->rl_ranks[j];

			i--;
			rank_num--;
		}
		rank_tmp = rank_list->rl_ranks[i];
	}
	if (identical_num != 0)
		rank_list->rl_nr -= identical_num;

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
d_rank_list_filter(d_rank_list_t *src_set, d_rank_list_t *dst_set,
		   bool exclude)
{
	d_rank_t	rank;
	uint32_t	rank_num, filter_num;
	int		i, j;

	if (src_set == NULL || dst_set == NULL)
		return;
	if (src_set->rl_ranks == NULL || dst_set->rl_ranks == NULL)
		return;

	rank_num = dst_set->rl_nr;
	if (rank_num == 0)
		return;

	filter_num = 0;
	for (i = 0; i < rank_num - filter_num; i++) {
		rank = dst_set->rl_ranks[i];
		if (d_rank_in_rank_list(src_set, rank) != exclude)
			continue;
		filter_num++;
		for (j = i; j < rank_num - 1; j++)
			dst_set->rl_ranks[j] = dst_set->rl_ranks[j + 1];
		/* as dst_set moved one item ahead */
		i--;
	}
	if (filter_num != 0)
		dst_set->rl_nr -= filter_num;
}

int
d_rank_list_merge(d_rank_list_t *src_ranks, d_rank_list_t *ranks_merge)
{
	d_rank_t	*rs;
	int		*indexes;
	int		num = 0;
	int		src_num;
	int		i;
	int		j;
	int		rc = 0;

	D_ASSERT(src_ranks != NULL);
	if (ranks_merge == NULL || ranks_merge->rl_nr == 0)
		return 0;

	D_ALLOC_ARRAY(indexes, ranks_merge->rl_nr);
	if (indexes == NULL)
		return -DER_NOMEM;

	for (i = 0; i < ranks_merge->rl_nr; i++) {
		if (!d_rank_list_find(src_ranks, ranks_merge->rl_ranks[i], NULL)) {
			indexes[num] = i;
			num++;
		}
	}

	if (num == 0)
		D_GOTO(free, rc = 0);

	src_num = src_ranks->rl_nr;
	D_ALLOC_ARRAY(rs, (num + src_num));
	if (rs == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	for (i = 0; i < src_num; i++)
		rs[i] = src_ranks->rl_ranks[i];

	for (i = src_num, j = 0; i < src_num + num; i++, j++) {
		int idx = indexes[j];

		rs[i] = ranks_merge->rl_ranks[idx];
	}

	if (src_ranks->rl_ranks)
		D_FREE(src_ranks->rl_ranks);

	src_ranks->rl_nr = num + src_num;
	src_ranks->rl_ranks = rs;

free:
	D_FREE(indexes);
	return rc;
}

d_rank_list_t *
d_rank_list_alloc(uint32_t size)
{
	d_rank_list_t		*rank_list;
	int			 i;

	D_ALLOC_PTR(rank_list);
	if (rank_list == NULL)
		return NULL;

	if (size == 0) {
		rank_list->rl_nr = 0;
		rank_list->rl_ranks = NULL;
		return rank_list;
	}

	D_ALLOC_ARRAY(rank_list->rl_ranks, size);
	if (rank_list->rl_ranks == NULL) {
		D_FREE(rank_list);
		return NULL;
	}

	rank_list->rl_nr = size;
	for (i = 0; i < size; i++)
		rank_list->rl_ranks[i] = i;

	return rank_list;
}

d_rank_list_t *
d_rank_list_realloc(d_rank_list_t *ptr, uint32_t size)
{
	d_rank_t *new_rl_ranks;

	if (ptr == NULL)
		return d_rank_list_alloc(size);
	if (size == 0) {
		d_rank_list_free(ptr);
		return NULL;
	}
	D_REALLOC_ARRAY(new_rl_ranks, ptr->rl_ranks, ptr->rl_nr, size);
	if (new_rl_ranks != NULL) {
		ptr->rl_ranks = new_rl_ranks;
		ptr->rl_nr = size;
	} else {
		ptr = NULL;
	}

	return ptr;
}

void
d_rank_list_free(d_rank_list_t *rank_list)
{
	if (rank_list == NULL)
		return;
	D_FREE(rank_list->rl_ranks);
	D_FREE(rank_list);
}

int
d_rank_list_copy(d_rank_list_t *dst, d_rank_list_t *src)
{
	int		rc = DER_SUCCESS;

	if (dst == NULL || src == NULL) {
		D_ERROR("Nothing to do, dst: %p, src: %p.\n", dst, src);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (dst->rl_nr != src->rl_nr) {
		dst = d_rank_list_realloc(dst, src->rl_nr);
		if (dst == NULL) {
			D_ERROR("d_rank_list_realloc() failed.\n");
			D_GOTO(out, rc = -DER_NOMEM);
		}
		dst->rl_nr = src->rl_nr;
	}

	memcpy(dst->rl_ranks, src->rl_ranks, dst->rl_nr * sizeof(d_rank_t));
out:
	return rc;
}

static inline int
rank_compare(const void *rank1, const void *rank2)
{
	const d_rank_t	*r1 = rank1;
	const d_rank_t	*r2 = rank2;

	D_ASSERT(r1 != NULL && r2 != NULL);
	if (*r1 < *r2)
		return -1;
	else if (*r1 == *r2)
		return 0;
	else /* *r1 > *r2 */
		return 1;
}

void
d_rank_list_sort(d_rank_list_t *rank_list)
{
	if (rank_list == NULL)
		return;
	qsort(rank_list->rl_ranks, rank_list->rl_nr,
	      sizeof(d_rank_t), rank_compare);
}

void
d_rank_list_shuffle(d_rank_list_t *rank_list)
{
	uint32_t	i, j;
	d_rank_t	tmp;

	if (rank_list == NULL)
		return;

	for (i = 0; i < rank_list->rl_nr; i++) {
		j = rand() % rank_list->rl_nr;
		tmp = rank_list->rl_ranks[i];
		rank_list->rl_ranks[i] = rank_list->rl_ranks[j];
		rank_list->rl_ranks[j] = tmp;
	}
}

/**
 * Must be previously sorted or not modified at all in order to guarantee
 * consistent indexes.
 **/
bool
d_rank_list_find(d_rank_list_t *rank_list, d_rank_t rank, int *idx)
{
	int i;

	if (rank_list == NULL)
		return false;
	for (i = 0; i < rank_list->rl_nr; i++) {
		if (rank_list->rl_ranks[i] == rank) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

/**
 * delete the first occurrence of rank, shrink the array storage size in
 * rank_list, and reduce the size of rank_list by 1.
 */
int
d_rank_list_del(d_rank_list_t *rank_list, d_rank_t rank)
{
	uint32_t	 new_num;
	uint32_t	 num_bytes;
	void		*dest;
	void		*src;
	int		 idx;
	int		 rc = 0;

	if (rank_list == NULL) {
		D_ERROR("rank_list cannot be NULL\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (!d_rank_list_find(rank_list, rank, &idx)) {
		D_DEBUG(DB_TRACE, "Rank %d not in the rank list.\n", rank);
		D_GOTO(out, 0);
	}
	new_num = rank_list->rl_nr - 1;
	src = &rank_list->rl_ranks[idx + 1];
	dest = &rank_list->rl_ranks[idx];
	D_ASSERT(idx <= new_num);
	num_bytes = (new_num - idx) * sizeof(d_rank_t);
	memmove(dest, src, num_bytes);
	rank_list = d_rank_list_realloc(rank_list, new_num);
	if (rank_list == NULL) {
		D_ERROR("d_rank_list_realloc() failed.\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
out:
	return rc;
}

int
d_rank_list_append(d_rank_list_t *rank_list, d_rank_t rank)
{
	uint32_t		 old_num = rank_list->rl_nr;
	d_rank_list_t		*new_rank_list;
	int			 rc = 0;

	new_rank_list = d_rank_list_realloc(rank_list, old_num + 1);
	if (new_rank_list == NULL) {
		D_ERROR("d_rank_list_realloc() failed.\n");
		D_GOTO(out, rc = -DER_NOMEM);
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
d_rank_list_identical(d_rank_list_t *rank_list1, d_rank_list_t *rank_list2)
{
	int i;

	if (rank_list1 == rank_list2)
		return true;
	if (rank_list1 == NULL || rank_list2 == NULL)
		return false;
	if (rank_list1->rl_nr != rank_list2->rl_nr)
		return false;
	d_rank_list_sort(rank_list1);
	for (i = 0; i < rank_list1->rl_nr; i++) {
		if (rank_list1->rl_ranks[i] != rank_list2->rl_ranks[i])
			return false;
	}

	return true;
}

/* check whether one rank included in the rank list, all are global ranks. */
bool
d_rank_in_rank_list(d_rank_list_t *rank_list, d_rank_t rank)
{
	uint32_t	rank_num, i;

	if (rank_list == NULL)
		return false;

	rank_num = rank_list->rl_nr;
	for (i = 0; i < rank_num; i++) {
		if (rank_list->rl_ranks[i] == rank)
			return true;
	}
	return false;
}

/*
 * query the idx of rank within the rank_list.
 *
 * return -DER_NONEXIST when rank not belong to rank list.
 */
int
d_idx_in_rank_list(d_rank_list_t *rank_list, d_rank_t rank, uint32_t *idx)
{
	uint32_t	rank_num;
	bool		found = false;
	uint32_t	i;

	if (rank_list == NULL || idx == NULL)
		return -DER_INVAL;

	rank_num = rank_list->rl_nr;
	for (i = 0; i < rank_num; i++) {
		if (rank_list->rl_ranks[i] == rank) {
			found = true;
			*idx = i;
			break;
		}
	}

	return found == true ? 0 : -DER_NONEXIST;
}

/**
 * Print out the content of a rank_list to stderr.
 *
 * \param[in]  rank_list	the rank list to print
 * \param[in]  name		a name to describe the rank list
 * \param[in]  name_len		length of the name string (excluding the
 *				trailing \n);
 *
 * \return			0 on success, a negative value on error
 */
int
d_rank_list_dump(d_rank_list_t *rank_list, d_string_t name, int name_len)
{
	int		 width;
	char		*tmp_str;
	int		 i;
	int		 idx = 0;
	int		 rc = 0;

	width = strnlen(name, name_len + 1);
	if (width > name_len) {
		D_ERROR("name parameter too long.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	width = 0;
	for (i = 0; i < rank_list->rl_nr; i++)
		width += snprintf(NULL, 0, "%d ", rank_list->rl_ranks[i]);
	width++;
	D_ALLOC(tmp_str, width);
	if (tmp_str == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	for (i = 0; i < rank_list->rl_nr; i++)
		idx += sprintf(&tmp_str[idx], "%d ", rank_list->rl_ranks[i]);
	tmp_str[width - 1] = '\0';
	D_DEBUG(DB_TRACE, "%s, %d ranks: %s\n",
		name, rank_list->rl_nr, tmp_str);
	D_FREE(tmp_str);

out:
	return rc;
}

/**
 * Create a ranged string representation of a rank list.
 *
 * \param[in]  rank_list	the rank list to represent
 *
 * \return			a ranged string (caller must free)
 */
char *
d_rank_list_to_str(d_rank_list_t *rank_list)
{
	char			*str;
	bool			 truncated = false;
	d_rank_range_list_t	*range_list;

	range_list = d_rank_range_list_create_from_ranks(rank_list);
	if (range_list == NULL)
		return NULL;
	str = d_rank_range_list_str(range_list, &truncated);

	d_rank_range_list_free(range_list);

	return str;
}

d_rank_list_t *
uint32_array_to_rank_list(uint32_t *ints, size_t len)
{
	d_rank_list_t	*result;
	size_t		i;

	result = d_rank_list_alloc(len);
	if (result == NULL)
		return NULL;

	for (i = 0; i < len; i++)
		result->rl_ranks[i] = (d_rank_t)ints[i];

	return result;
}

int
rank_list_to_uint32_array(d_rank_list_t *rl, uint32_t **ints, size_t *len)
{
	uint32_t i;

	D_ALLOC_ARRAY(*ints, rl->rl_nr);
	if (*ints == NULL)
		return -DER_NOMEM;

	*len = rl->rl_nr;

	for (i = 0; i < rl->rl_nr; i++)
		(*ints)[i] = (uint32_t)rl->rl_ranks[i];

	return 0;
}

d_rank_range_list_t *
d_rank_range_list_alloc(uint32_t size)
{
	d_rank_range_list_t    *range_list;

	D_ALLOC_PTR(range_list);
	if (range_list == NULL)
		return NULL;

	if (size == 0) {
		range_list->rrl_nr = 0;
		range_list->rrl_ranges = NULL;
		return range_list;
	}

	D_ALLOC_ARRAY(range_list->rrl_ranges, size);
	if (range_list->rrl_ranges == NULL) {
		D_FREE(range_list);
		return NULL;
	}
	range_list->rrl_nr = size;

	return range_list;
}

d_rank_range_list_t *
d_rank_range_list_realloc(d_rank_range_list_t *range_list, uint32_t size)
{
	d_rank_range_t		*new_ranges;

	if (range_list == NULL)
		return d_rank_range_list_alloc(size);
	if (size == 0) {
		d_rank_range_list_free(range_list);
		return NULL;
	}
	D_REALLOC_ARRAY(new_ranges, range_list->rrl_ranges, range_list->rrl_nr, size);
	if (new_ranges != NULL) {
		range_list->rrl_ranges = new_ranges;
		range_list->rrl_nr = size;
	} else {
		range_list = NULL;
	}

	return range_list;
}

/* TODO (DAOS-10253) Add unit tests for this function */
d_rank_range_list_t *
d_rank_range_list_create_from_ranks(d_rank_list_t *rank_list)
{
	d_rank_range_list_t	       *range_list;
	uint32_t			alloc_size;
	uint32_t			nranges;
	unsigned int			i;		/* rank index */
	unsigned int			j;		/* rank range index */

	d_rank_list_sort(rank_list);
	if ((rank_list == NULL) || (rank_list->rl_ranks == NULL) || (rank_list->rl_nr == 0))
		return d_rank_range_list_alloc(0);

	alloc_size = nranges = 1;
	range_list = d_rank_range_list_alloc(alloc_size);
	if (range_list == NULL)
		return NULL;

	range_list->rrl_ranges[0].lo = rank_list->rl_ranks[0];
	range_list->rrl_ranges[0].hi = rank_list->rl_ranks[0];
	for (i = 1, j = 0; i < rank_list->rl_nr; i++) {
		if (rank_list->rl_ranks[i] == (rank_list->rl_ranks[i-1] + 1)) {
			/* rank range j continues */
			range_list->rrl_ranges[j].hi = rank_list->rl_ranks[i];
		} else {
			/* New rank range found at position i in rank_list. */
			j++;
			nranges++;	/* j + 1 */
			if (nranges > alloc_size) {
				alloc_size *= 2;
				range_list = d_rank_range_list_realloc(range_list, alloc_size);
				if (range_list == NULL) {
					d_rank_range_list_free(range_list);
					return NULL;
				}
			}
			range_list->rrl_ranges[j].lo = rank_list->rl_ranks[i];
			range_list->rrl_ranges[j].hi = rank_list->rl_ranks[i];
			range_list->rrl_nr = nranges;
		}
	}

	return range_list;
}

/* TODO (DAOS-10253) Add unit tests for this function */
char *
d_rank_range_list_str(d_rank_range_list_t *list, bool *truncated)
{
	const size_t	MAXBYTES = 512;
	char	       *line;
	char	       *linepos;
	int		ret = 0;
	size_t		remaining = MAXBYTES - 2u;
	int		i;
	int		err = 0;

	*truncated = false;
	D_ALLOC(line, MAXBYTES);
	if (line == NULL)
		return NULL;

	*line = '[';
	linepos = line + 1;
	for (i = 0; i < list->rrl_nr; i++) {
		uint32_t	lo = list->rrl_ranges[i].lo;
		uint32_t	hi = list->rrl_ranges[i].hi;
		bool		lastrange = (i == (list->rrl_nr - 1));

		if (lo == hi)
			ret = snprintf(linepos, remaining, "%u%s", lo, lastrange ? "" : ",");
		else
			ret = snprintf(linepos, remaining, "%u-%u%s", lo, hi, lastrange ? "" : ",");

		if (ret < 0) {
			err = errno;
			D_ERROR("rank set could not be serialized: %s (%d)\n", strerror(err), err);
			break;
		}

		if (ret >= remaining) {
			err = EOVERFLOW;
			D_WARN("rank set has been partially serialized\n");
			break;
		}

		remaining -= ret;
		linepos += ret;
	}
	memcpy(linepos, "]", 2u);

	if (err != 0)
		*truncated = true;

	return line;
}

void
d_rank_range_list_free(d_rank_range_list_t *range_list)
{
	if (range_list == NULL)
		return;
	D_FREE(range_list->rrl_ranges);
	D_FREE(range_list);
}

static inline bool
dis_integer_str(char *str)
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

static inline bool
dis_single_char_str(char *str)
{
	return strlen(str) == 1;
}

/**
 * get a bool type environment variables
 *
 * \param[in]		env		name of the environment variable
 * \param[in,out]	bool_val	returned value of the ENV. Will not change the original
 *					value if ENV is not set. Set as false if the env is set to
 *					0, otherwise set as true.
 */
void
d_getenv_bool(const char *env, bool *bool_val)
{
	char *env_val;

	if (env == NULL)
		return;
	D_ASSERT(bool_val != NULL);

	env_val = getenv(env);
	if (!env_val)
		return;

	/* treats any valid non-integer string as true */
	if (!dis_integer_str(env_val))
		*bool_val = true;

	*bool_val = (atoi(env_val) == 0 ? false : true);
}

/**
 * get single character environment variable
 *
 * \param[in]           env     name of the environment variable
 * \param[in,out]       char_val returned value of the ENV. Will not change the original value
 */
void
d_getenv_char(const char *env, char *char_val)
{
	char		*env_val;

	if (env == NULL || char_val == NULL)
		return;

	env_val = getenv(env);
	if (!env_val)
		return;

	if (!dis_single_char_str(env_val)) {
		D_ERROR("ENV %s is not single character.\n", env_val);
		return;
	}
	*char_val = *env_val;
}

/**
 * get an integer type environment variables
 *
 * \param[in]		env	name of the environment variable
 * \param[in,out]	int_val	returned value of the ENV. Will not change the original value if ENV
 *				is not set or set as a
 *				non-integer value.
 */
void
d_getenv_int(const char *env, unsigned *int_val)
{
	char		*env_val;
	unsigned	 value;

	if (env == NULL || int_val == NULL)
		return;

	env_val = getenv(env);
	if (!env_val)
		return;

	if (!dis_integer_str(env_val)) {
		D_ERROR("ENV %s is not integer.\n", env_val);
		return;
	}

	value = atoi(env_val);
	D_DEBUG(DB_TRACE, "get ENV %s as %d.\n", env, value);
	*int_val = value;
}

int
d_getenv_uint64_t(const char *env, uint64_t *val)
{
	char		*env_val;
	size_t		env_len;
	int		matched;
	uint64_t	new_val;
	int		count;

	env_val = getenv(env);
	if (!env_val) {
		D_DEBUG(DB_TRACE, "ENV '%s' unchanged at %"PRId64"\n", env, *val);
		return -DER_NONEXIST;
	}

	env_len = strnlen(env_val, 128);
	if (env_len == 128) {
		D_ERROR("ENV '%s' is invalid\n", env);
		return -DER_INVAL;
	}

	/* Now do scanf, check that the number was matched, and there are no extra unmatched
	 * characters at the end.
	 */
	matched = sscanf(env_val, "%"PRId64"%n", &new_val, &count);
	if (matched == 1 && env_len == count) {
		*val = new_val;
		D_DEBUG(DB_TRACE, "ENV '%s' set to %"PRId64"\n", env, *val);
		return -DER_SUCCESS;
	}

	D_ERROR("ENV '%s' is invalid: '%s'\n", env, env_val);
	return -DER_INVAL;
}

/**
 * Write formatted data to d_string_buffer_t
 *
 * Create a string with the same text that would be created by printf(). The
 * result string is stored as a C string in a buffer pointed by buf. The
 * d_string_buffer_t internal buffer grows as text is written.
 *
 * \param[in,out] buf    string object where the formatted string will be
 *                       stored. Subsequent write operations to the same string
 *                       object will be append at the end of the buffer.
 * \param[in]     format this is the string that contains the text to be
 *                       written to d_string_buffer_t. Please refer to the
 *                       printf() documentation for full details of how to use
 *                       the format tags.
 * \return               0 on success, errno code on failure.
 */
int
d_write_string_buffer(struct d_string_buffer_t *buf, const char *format, ...)
{
	int	n;
	int	size = 64;
	char	*new_buf;
	va_list	ap;

	if (buf == NULL || buf->status != 0) {
		return -DER_NO_PERM;
	}

	if (buf->str == NULL) {
		D_ALLOC(buf->str, size);
		if (buf->str == NULL) {
			buf->status = -DER_NOMEM;
			return -DER_NOMEM;
		}
		buf->str_size = 0;
		buf->buf_size = size;
	}

	while (1) {
		va_start(ap, format);
		size = buf->buf_size - buf->str_size;
		n = vsnprintf(buf->str + buf->str_size, size, format, ap);
		va_end(ap);

		if (n < 0) {
			buf->status = -DER_TRUNC;
			return -DER_TRUNC;
		}

		if ((buf->str_size + n) < buf->buf_size) {
			buf->str_size += n;
			buf->status = DER_SUCCESS;
			return DER_SUCCESS;
		}

		size = buf->buf_size * 2;
		D_REALLOC(new_buf, buf->str, buf->buf_size, size);
		if (new_buf == NULL) {
			buf->status = -DER_NOMEM;
			return -DER_NOMEM;
		}

		buf->str = new_buf;
		buf->buf_size = size;
	}
}

/** Deallocate the memory used by d_string_buffer_t
 *
 * The d_string_buffer_t internal buffer is deallocated, and stats are reset.
 *
 * \param[in] buf string object to be cleaned.
 */
void
d_free_string(struct d_string_buffer_t *buf)
{
	if (buf->str != NULL) {
		D_FREE(buf->str);
		buf->status = 0;
		buf->str_size = 0;
		buf->buf_size = 0;
	}
}

/**
 * Initialize \a seq. The backoff sequence will be generated using an
 * exponential backoff algorithm. The first \a nzeros backoffs will be zero;
 * each of the rest will be a random number in [0, x], where x is initially \a
 * next and multiplied with \a factor for each subsequent backoff. (See
 * d_backoff_seq_next.) For example, with
 *
 *   nzeros = 1,
 *   factor = 4,
 *   next = 16, and
 *   max = 1 << 20,
 *
 * the caller shall get:
 *
 *   Backoff	Range
 *         1	[0,       0]
 *         2	[0,      16]
 *         3	[0,      64]
 *         4	[0,     256]
 *       ...	...
 *        10	[0, 1048576]
 *        11	[0, 1048576]
 *       ...	...
 *
 * \param[in]	seq	backoff sequence
 * \param[in]	nzeros	number of initial zero backoffs
 * \param[in]	factor	backoff factor
 * \param[in]	next	next backoff after all initial zero backoffs
 * \param[in]	max	maximum backoff
 */
int
d_backoff_seq_init(struct d_backoff_seq *seq, uint8_t nzeros, uint16_t factor,
		   uint32_t next, uint32_t max)
{
	if (seq == NULL || factor == 0 || next == 0 || max == 0 || next > max)
		return -DER_INVAL;

	seq->bos_flags = 0;
	seq->bos_nzeros = nzeros;
	seq->bos_factor = factor;
	seq->bos_next = next;
	seq->bos_max = max;
	return 0;
}

/**
 * Finalize \a seq. This offers a clear way to reset a d_backoff_seq object
 * (with potentially different parameters):
 *
 *   d_backoff_seq_fini(&seq);
 *   rc = d_backoff_seq_init(&seq, ...);
 *
 * \param[in]	seq	backoff sequence
 */
void
d_backoff_seq_fini(struct d_backoff_seq *seq)
{
	/* Don't need to do anything at the moment. */
}

/**
 * Compute and return next backoff in \a seq.
 *
 * \param[in]	seq	backoff sequence
 */
uint32_t
d_backoff_seq_next(struct d_backoff_seq *seq)
{
	uint32_t next;

	/* Have we not outputted all the initial zeros yet? */
	if (seq->bos_nzeros != 0) {
		seq->bos_nzeros--;
		return 0;
	}

	/* Save seq->bos_next in next. */
	next = seq->bos_next;

	/* Update seq->bos_next. */
	if (seq->bos_next < seq->bos_max) {
		seq->bos_next *= seq->bos_factor;
		/*
		 * If the new value overflows or is greater than the maximum,
		 * set it to the maximum.
		 */
		if (seq->bos_next / seq->bos_factor != next ||
		    seq->bos_next > seq->bos_max)
			seq->bos_next = seq->bos_max;
	}

	/* Return a random backoff in [0, next]. */
	return (next * ((double)d_rand() / D_RAND_MAX));
}

double
d_stand_div(double *array, int nr)
{
	double		avg = 0;
	double		std = 0;
	int		i;

	for (i = 0; i < nr; i++)
		avg += array[i];

	avg /= nr;
	for (i = 0; i < nr; i++)
		std += pow(array[i] - avg, 2);

	std /= nr;
	return sqrt(std);
}

int
d_vec_pointers_init(struct d_vec_pointers *pointers, uint32_t cap)
{
	void **buf = NULL;

	if (cap > 0) {
		D_ALLOC_ARRAY(buf, cap);
		if (buf == NULL)
			return -DER_NOMEM;
	}

	pointers->p_buf = buf;
	pointers->p_cap = cap;
	pointers->p_len = 0;
	return 0;
}

void
d_vec_pointers_fini(struct d_vec_pointers *pointers)
{
	D_FREE(pointers->p_buf);
	pointers->p_cap = 0;
	pointers->p_len = 0;
}

int
d_vec_pointers_append(struct d_vec_pointers *pointers, void *pointer)
{
	if (pointers->p_len == pointers->p_cap) {
		void		**buf;
		uint32_t	  cap;

		if (pointers->p_cap == 0)
			cap = 1;
		else
			cap = 2 * pointers->p_cap;

		D_REALLOC_ARRAY(buf, pointers->p_buf, pointers->p_cap, cap);
		if (buf == NULL)
			return -DER_NOMEM;

		pointers->p_buf = buf;
		pointers->p_cap = cap;
	}

	D_ASSERTF(pointers->p_len < pointers->p_cap, "%u < %u\n", pointers->p_len, pointers->p_cap);
	pointers->p_buf[pointers->p_len] = pointer;
	pointers->p_len++;
	return 0;
}

/**
 * Overloads to hook the unsafe getenv()/[un]setenv()/putenv()/clearenv()
 * functions from glibc.
 * Libgurt is the preferred place for this as it is the lowest layer in DAOS,
 * so it will be the earliest to be loaded and will ensure the hook to be
 * installed as early as possible and could prevent usage of LD_PRELOAD.
 * The idea is to strengthen all the environment APIs by using a common lock.
 *
 * XXX this will address the main lack of multi-thread protection in the Glibc
 * APIs but do not handle all unsafe use-cases (like the change/removal of an
 * env var when its value address has already been grabbed by a previous
 * getenv(), ...).
 */

static pthread_rwlock_t hook_env_lock = PTHREAD_RWLOCK_INITIALIZER;
static char *(* ATOMIC real_getenv)(const char *);
static int (* ATOMIC real_putenv)(char *);
static int (* ATOMIC real_setenv)(const char *, const char *, int);
static int (* ATOMIC real_unsetenv)(const char *);
static int (* ATOMIC real_clearenv)(void);

static void bind_libc_symbol(void **real_ptr_addr, const char *name)
{
	void *real_temp;

	/* XXX __atomic_*() built-ins are used to avoid the need to cast
	 * each of the ATOMIC pointers of functions, that seems to be
	 * required to make Intel compiler happy ...
	 */
	if (__atomic_load_n(real_ptr_addr, __ATOMIC_RELAXED) == NULL) {
		/* libc should be already loaded ... */
		real_temp = dlsym(RTLD_NEXT, name);
		if (real_temp == NULL) {
			/* try after loading libc now */
			void *handle;

			handle = dlopen("libc.so.6", RTLD_LAZY);
			D_ASSERT(handle != NULL);
			real_temp = dlsym(handle, name);
			D_ASSERT(real_temp != NULL);
		}
		__atomic_store_n(real_ptr_addr, real_temp, __ATOMIC_RELAXED);
	}
}

static pthread_once_t init_real_symbols_flag = PTHREAD_ONCE_INIT;

static void init_real_symbols(void)
{
	bind_libc_symbol((void **)&real_getenv, "getenv");
	bind_libc_symbol((void **)&real_putenv, "putenv");
	bind_libc_symbol((void **)&real_setenv, "setenv");
	bind_libc_symbol((void **)&real_unsetenv, "unsetenv");
	bind_libc_symbol((void **)&real_clearenv, "clearenv");
}

char *getenv(const char *name)
{
	char *p;

	pthread_once(&init_real_symbols_flag, init_real_symbols);
	D_RWLOCK_RDLOCK(&hook_env_lock);
	p = real_getenv(name);
	D_RWLOCK_UNLOCK(&hook_env_lock);

	return p;
}

int putenv(char *name)
{
	int rc;

	pthread_once(&init_real_symbols_flag, init_real_symbols);
	D_RWLOCK_WRLOCK(&hook_env_lock);
	rc = real_putenv(name);
	D_RWLOCK_UNLOCK(&hook_env_lock);

	return rc;
}

int setenv(const char *name, const char *value, int overwrite)
{
	int rc;

	pthread_once(&init_real_symbols_flag, init_real_symbols);
	D_RWLOCK_WRLOCK(&hook_env_lock);
	rc = real_setenv(name, value, overwrite);
	D_RWLOCK_UNLOCK(&hook_env_lock);

	return rc;
}

int unsetenv(const char *name)
{
	int rc;

	pthread_once(&init_real_symbols_flag, init_real_symbols);
	D_RWLOCK_WRLOCK(&hook_env_lock);
	rc = real_unsetenv(name);
	D_RWLOCK_UNLOCK(&hook_env_lock);

	return rc;
}

int clearenv(void)
{
	int rc;

	pthread_once(&init_real_symbols_flag, init_real_symbols);
	D_RWLOCK_WRLOCK(&hook_env_lock);
	rc = real_clearenv();
	D_RWLOCK_UNLOCK(&hook_env_lock);

	return rc;
}
