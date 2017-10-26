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

#ifndef __GURT_COMMON_H__
#define __GURT_COMMON_H__

#include <uuid/uuid.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <byteswap.h>

#include <gurt/errno.h>
#include <gurt/debug.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Get the current time using a monotonic timer
 * param[out] ts A timespec structure for the result
 */
#define _gurt_gettime(ts) clock_gettime(CLOCK_MONOTONIC, ts)

/**
 * hide the dark secret that uuid_t is an array not a structure.
 */
struct d_uuid {
	uuid_t		uuid;
};

/** iovec for memory buffer */
typedef struct {
	/** buffer address */
	void		*iov_buf;
	/** buffer length */
	size_t		iov_buf_len;
	/** data length */
	size_t		iov_len;
} d_iov_t;

/** Server identification */
typedef uint32_t	d_rank_t;

typedef struct {
	/** input number */
	uint32_t	num;
	/** output/returned number */
	uint32_t	num_out;
} d_nr_t;

typedef struct {
	/** number of ranks */
	d_nr_t	 rl_nr;
	d_rank_t	*rl_ranks;
} d_rank_list_t;

typedef char		*d_string_t;
typedef const char	*d_const_string_t;

/** Scatter/gather list for memory buffers */
typedef struct {
	d_nr_t	 sg_nr;
	d_iov_t	*sg_iovs;
} d_sg_list_t;

static inline void
d_iov_set(d_iov_t *iov, void *buf, size_t size)
{
	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}

/* memory allocating macros */

#define MEM_DBG		(d_mem_logfac | DLOG_DBG)

#define D_ALLOC_CORE(ptr, size, count)					\
	do {								\
		(ptr) = (__typeof__(ptr))calloc(count, (size));		\
		if ((ptr) != NULL) {					\
			if (count == 1)					\
				d_log(MEM_DBG, "%s:%d, alloc '" #ptr	\
					"': %i at %p.\n",		\
					__FILE__, __LINE__,		\
					(int)(size), ptr);		\
			else						\
				d_log(MEM_DBG, "%s:%d, alloc '" #ptr	\
					"': %i * '" #count " ': %i at %p.\n", \
					__FILE__, __LINE__,		\
					(int)(size), (count), ptr);	\
			break;						\
		}							\
		D_ERROR("%s:%d, out of memory (tried to alloc '" #ptr	\
			"': %i)",					\
			__FILE__, __LINE__, (int)(size) * (count));	\
	} while (0)

/* TODO: Correct use of #ptr in this function */
static inline void *
d_realloc(void *ptrptr, size_t size)
{
	void *tmp_ptr;

	tmp_ptr = realloc(ptrptr, size);
	if (size == 0 || tmp_ptr != NULL) {
		d_log(MEM_DBG, "realloc #ptr : %d at %p.\n",
			(int) size, ptrptr);

		return tmp_ptr;
	}
	D_ERROR("out of memory (tried to realloc \" #ptr \" = %d)",
		(int)(size));

	return tmp_ptr;
}
/* memory reallocation */
#define D_REALLOC(ptr, size)						\
	(__typeof__(ptr)) d_realloc((ptr), (size))

# define D_FREE(ptr)						\
	do {								\
		d_log(MEM_DBG, "%s:%d, free '" #ptr "' at %p.\n",	\
			__FILE__, __LINE__, (ptr));	\
		free(ptr);						\
		(ptr) = NULL;						\
	} while (0)

#define D_ALLOC(ptr, size)	D_ALLOC_CORE(ptr, size, 1)
#define D_ALLOC_PTR(ptr)	D_ALLOC(ptr, sizeof(*ptr))
#define D_ALLOC_ARRAY(ptr, count) D_ALLOC_CORE(ptr, sizeof(*ptr), count)
#define D_FREE_PTR(ptr)		D_FREE(ptr)

#define D_GOTO(label, rc)	do { ((void)(rc)); goto label; } while (0)

#define DGOLDEN_RATIO_PRIME_64	0xcbf29ce484222325ULL
#define DGOLDEN_RATIO_PRIME_32	0x9e370001UL

static inline uint64_t
d_u64_hash(uint64_t val, unsigned int bits)
{
	uint64_t hash = val;

	hash *= DGOLDEN_RATIO_PRIME_64;
	return hash >> (64 - bits);
}

static inline uint32_t
d_u32_hash(uint64_t key, unsigned int bits)
{
	return (DGOLDEN_RATIO_PRIME_32 * key) >> (32 - bits);
}

uint64_t d_hash_mix64(uint64_t key);
uint32_t d_hash_mix96(uint32_t a, uint32_t b, uint32_t c);

/** consistent hash search */
unsigned int d_chash_srch_u64(uint64_t *hashes, unsigned int nhashes,
				 uint64_t value);

/** djb2 hash a string to a uint32_t value */
uint32_t d_hash_string_u32(const char *string, unsigned int len);
/** murmur hash (64 bits) */
uint64_t d_hash_murmur64(const unsigned char *key, unsigned int key_len,
			    unsigned int seed);

#define LOWEST_BIT_SET(x)       ((x) & ~((x) - 1))

static inline unsigned int
d_power2_nbits(unsigned int val)
{
	unsigned int shift;

	for (shift = 1; (val >> shift) != 0; shift++);

	return val == LOWEST_BIT_SET(val) ? shift - 1 : shift;
}

int d_rank_list_dup(d_rank_list_t **dst, const d_rank_list_t *src, bool input);
int d_rank_list_dup_sort_uniq(d_rank_list_t **dst, const d_rank_list_t *src,
			      bool input);
void d_rank_list_filter(d_rank_list_t *src_set, d_rank_list_t *dst_set,
			bool input, bool exclude);
d_rank_list_t *d_rank_list_alloc(uint32_t size);
d_rank_list_t *d_rank_list_realloc(d_rank_list_t *ptr, uint32_t size);
void d_rank_list_free(d_rank_list_t *rank_list);
void d_rank_list_copy(d_rank_list_t *dst, d_rank_list_t *src, bool input);
void d_rank_list_sort(d_rank_list_t *rank_list);
bool d_rank_list_find(d_rank_list_t *rank_list, d_rank_t rank, int *idx);
int d_rank_list_del(d_rank_list_t *rank_list, d_rank_t rank);
bool d_rank_list_identical(d_rank_list_t *rank_list1,
			   d_rank_list_t *rank_list2, bool input);
bool d_rank_in_rank_list(d_rank_list_t *rank_list, d_rank_t rank, bool input);
int d_idx_in_rank_list(d_rank_list_t *rank_list, d_rank_t rank,
			uint32_t *idx, bool input);
int d_rank_list_append(d_rank_list_t *rank_list, d_rank_t rank);
int d_rank_list_dump(d_rank_list_t *rank_list, d_string_t name, int name_len);
int d_sgl_init(d_sg_list_t *sgl, unsigned int nr);
void d_sgl_fini(d_sg_list_t *sgl, bool free_iovs);
void d_getenv_bool(const char *env, bool *bool_val);
void d_getenv_int(const char *env, unsigned *int_val);


#if !defined(container_of)
/* given a pointer @ptr to the field @member embedded into type (usually
 * struct) @type, return pointer to the embedding instance of @type.
 */
# define container_of(ptr, type, member)		\
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

#ifndef offsetof
# define offsetof(typ, memb)	((long)((char *)&(((typ *)0)->memb)))
#endif

#define D_ALIGNUP(x, a) (((x) + (a - 1)) & ~(a - 1))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
# define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
# define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef max
#define max(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef min_t
#define min_t(type, x, y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x : __y; })
#endif
#ifndef max_t
#define max_t(type, x, y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x : __y; })
#endif

/* byte swapper */
#define D_SWAP16(x)	bswap_16(x)
#define D_SWAP32(x)	bswap_32(x)
#define D_SWAP64(x)	bswap_64(x)
#define D_SWAP16S(x)	do { *(x) = D_SWAP16(*(x)); } while (0)
#define D_SWAP32S(x)	do { *(x) = D_SWAP32(*(x)); } while (0)
#define D_SWAP64S(x)	do { *(x) = D_SWAP64(*(x)); } while (0)

static inline int
d_errno2der(int err)
{
	switch (err) {
	case 0:		return 0;
	case EPERM:
	case EACCES:	return -DER_NO_PERM;
	case ENOMEM:	return -DER_NOMEM;
	case EDQUOT:
	case ENOSPC:	return -DER_NOSPACE;
	case EEXIST:	return -DER_EXIST;
	case ENOENT:	return -DER_NONEXIST;
	case ECANCELED:	return -DER_CANCELED;
	default:	return -DER_MISC;
	}
	return 0;
}

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC  1000000000
#endif
#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC 1000000
#endif
#ifndef NSEC_PER_USEC
#define NSEC_PER_USEC 1000
#endif

/* timing utilities */
static inline int
d_gettime(struct timespec *t)
{
	int	rc;

	rc = _gurt_gettime(t);
	if (rc != 0) {
		D_ERROR("clock_gettime failed, rc: %d, errno %d(%s).\n",
			rc, errno, strerror(errno));
		rc = d_errno2der(errno);
	}

	return rc;
}

/* Calculate t2 - t1 in nanoseconds */
static inline int64_t
d_timediff_ns(const struct timespec *t1, const struct timespec *t2)
{
	return ((t2->tv_sec - t1->tv_sec) * NSEC_PER_SEC) +
		t2->tv_nsec - t1->tv_nsec;
}

/* Calculate end - start as timespec. */
static inline struct timespec
d_timediff(struct timespec start, struct timespec end)
{
	struct timespec		temp;

	if ((end.tv_nsec - start.tv_nsec) < 0) {
		temp.tv_sec = end.tv_sec - start.tv_sec - 1;
		temp.tv_nsec = NSEC_PER_SEC + end.tv_nsec - start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec - start.tv_sec;
		temp.tv_nsec = end.tv_nsec - start.tv_nsec;
	}


	return temp;
}

/* Calculate remaining time in ns */
static inline int64_t
d_timeleft_ns(const struct timespec *expiration)
{
	struct timespec		now;
	int64_t			ns;

	d_gettime(&now);

	ns = d_timediff_ns(&now, expiration);

	if (ns <= 0)
		return 0;

	return ns;
}

/* calculate the number in us after \param sec_diff second */
static inline uint64_t
d_timeus_secdiff(unsigned sec_diff)
{
	struct timespec		now;
	uint64_t		us;

	d_gettime(&now);
	us = (now.tv_sec + sec_diff) * 1e6 + now.tv_nsec / 1e3;

	return us;
}

/* Increment time by ns nanoseconds */
static inline void
d_timeinc(struct timespec *now, uint64_t ns)
{
	now->tv_nsec += ns;
	now->tv_sec += now->tv_nsec / NSEC_PER_SEC;
	now->tv_nsec = now->tv_nsec % NSEC_PER_SEC;
}

static inline double
d_time2ms(struct timespec t)
{
	return (double) t.tv_sec * 1e3 + (double) t.tv_nsec / 1e6;
}

static inline double
d_time2us(struct timespec t)
{
	return (double) t.tv_sec * 1e6 + (double) t.tv_nsec / 1e3;
}

static inline double
d_time2s(struct timespec t)
{
	return (double) t.tv_sec + (double) t.tv_nsec / 1e9;
}

#if defined(__cplusplus)
}
#endif

#endif /* __GURT_COMMON_H__ */
