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
 * \file
 * GURT Common functions and types.
 */

/** @defgroup GURT GURT */
/** @defgroup GURT_LOG Gurt Log */
/** @defgroup GURT_DEBUG Gurt Debug */
#ifndef __GURT_COMMON_H__
#define __GURT_COMMON_H__

#include <uuid/uuid.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <byteswap.h>
#include <daos_errno.h>

#include <gurt/types.h>
#include <gurt/debug.h>
#include <gurt/fault_inject.h>

/** @addtogroup GURT
 * @{
 */

#if defined(__cplusplus)
extern "C" {
#endif

#define likely(x)       __builtin_expect((x), 1)
#define unlikely(x)     __builtin_expect((x), 0)

/* Check if bit is set in passed val */
#define D_BIT_IS_SET(val, bit) (((val) & bit) ? 1 : 0)


/**
 * Get the current time using a monotonic timer
 * param[out] ts A timespec structure for the result
 */
#define _gurt_gettime(ts) clock_gettime(CLOCK_MONOTONIC, ts)

/* memory allocating macros */

#define D_CHECK_ALLOC(func, cond, ptr, name, size, count, cname,	\
			on_error)					\
	do {								\
		if (D_SHOULD_FAIL(d_fault_attr_mem)) {			\
			free(ptr);					\
			ptr = NULL;					\
		}							\
		if ((cond) && (ptr) != NULL) {				\
			if (count <= 1)					\
				D_DEBUG(DB_MEM,				\
					"alloc(" #func ") '" name "': %i at %p.\n", \
					(int)(size), (ptr));		\
			else						\
				D_DEBUG(DB_MEM,				\
					"alloc(" #func ") '" name "': %i * '" cname "':%i at %p.\n", \
					(int)(size), (int)(count), (ptr)); \
			break;						\
		}							\
		(void)(on_error);					\
		if (count >= 1)						\
			D_ERROR("out of memory (tried to "		\
				#func " '" name "': %i)\n",		\
				(int)((size) * (count)));		\
		else							\
			D_ERROR("out of memory (tried to "		\
				#func " '" name "': %i)\n",		\
				(int)(size));				\
	} while (0)


#define D_ALLOC_CORE(ptr, size, count)					\
	do {								\
		(ptr) = (__typeof__(ptr))calloc(count, (size));		\
		D_CHECK_ALLOC(calloc, true, ptr, #ptr, size,		\
			      count, #count, 0);			\
	} while (0)

#define D_STRNDUP(ptr, s, n)						\
	do {								\
		(ptr) = strndup(s, n);					\
		D_CHECK_ALLOC(strndup, true, ptr, #ptr,			\
			      strnlen(s, n + 1) + 1, 0, #ptr, 0);	\
	} while (0)

#define D_ASPRINTF(ptr, ...)						\
	do {								\
		int _rc;						\
		_rc = asprintf(&(ptr), __VA_ARGS__);			\
		D_CHECK_ALLOC(asprintf, _rc != -1,			\
			      ptr, #ptr, _rc + 1, 0, #ptr,		\
			      (ptr) = NULL);				\
	} while (0)

#define D_REALPATH(ptr, path)						\
	do {								\
		int _size;						\
		(ptr) = realpath((path), NULL);				\
		_size = (ptr) != NULL ?					\
			strnlen((ptr), PATH_MAX + 1) + 1 : 0;		\
		D_CHECK_ALLOC(realpath, true, ptr, #ptr, _size,		\
			      0, #ptr, 0);				\
	} while (0)

/* Requires newptr and oldptr to be different variables.  Otherwise
 * there is no way to tell the difference between successful and
 * failed realloc.
 */
#define D_REALLOC_COMMON(newptr, oldptr, size, cnt)			\
	do {								\
		size_t _esz = (size_t)(size);				\
		size_t _sz = (size_t)(size) * (cnt);			\
		size_t _cnt = (size_t)(cnt);				\
		/* Compiler check to ensure type match */		\
		__typeof__(newptr) optr = oldptr;			\
		D_ASSERT((void *)&(newptr) != &(oldptr));		\
		/*							\
		 * On Linux, realloc(p, 0) may return NULL after	\
		 * freeing p. This behavior has proved tricky, as it is	\
		 * easy to mistake the NULL return value for an error	\
		 * and keep using the dangling p. We therefore allocate	\
		 * a 1-byte object in this case, simulating the BSD	\
		 * behavior.						\
		 */							\
		if (_sz == 0)						\
			_sz = 1;					\
		if (D_SHOULD_FAIL(d_fault_attr_mem))			\
			newptr = NULL;					\
		else							\
			(newptr) = realloc(optr, _sz);			\
		if ((newptr) != NULL) {					\
			if ((_cnt) <= 1)				\
				D_DEBUG(DB_MEM,				\
					"realloc '" #newptr "': %zu at %p (old '" #oldptr "':%p).\n", \
					_esz, (newptr), (oldptr));	\
			else						\
				D_DEBUG(DB_MEM,				\
					"realloc '" #newptr "': %zu * '" #cnt "':%zu at %p (old '" #oldptr "':%p).\n", \
					_esz, (_cnt), (newptr), (oldptr));	\
			(oldptr) = NULL;				\
			break;						\
		}							\
		if ((_cnt) <= 1)					\
			D_ERROR("out of memory (tried to realloc "	\
				"'" #newptr "': size=%zu)\n",		\
				_esz);					\
		else							\
			D_ERROR("out of memory (tried to realloc "	\
				"'" #newptr "': size=%zu count=%zu)\n",	\
				_esz, (_cnt));				\
	} while (0)


#define D_REALLOC(newptr, oldptr, size)	\
	D_REALLOC_COMMON(newptr, oldptr, size, 1)

#define D_REALLOC_ARRAY(newptr, oldptr, count) \
	D_REALLOC_COMMON(newptr, oldptr, sizeof(*(oldptr)), count)


#define D_FREE(ptr)							\
	do {								\
		D_DEBUG(DB_MEM, "free '" #ptr "' at %p.\n", (ptr));	\
		free(ptr);						\
		(ptr) = NULL;						\
	} while (0)

#define D_ALLOC(ptr, size)	D_ALLOC_CORE(ptr, size, 1)
#define D_ALLOC_PTR(ptr)	D_ALLOC(ptr, sizeof(*ptr))
#define D_ALLOC_ARRAY(ptr, count) D_ALLOC_CORE(ptr, sizeof(*ptr), count)
#define D_FREE_PTR(ptr)		D_FREE(ptr)

#define D_GOTO(label, rc)			\
	do {					\
		__typeof__(rc) __rc = (rc);		\
		(void)(__rc);			\
		goto label;			\
	} while (0)

#define D_FPRINTF(...)							\
	({								\
		int	printed = fprintf(__VA_ARGS__);			\
		int	_rc = 0;					\
									\
		if (printed < 0) {					\
			D_ERROR("failed to print to stream\n");		\
			_rc = -DER_IO;					\
		}							\
		_rc;							\
	})

/* Internal helper macros, not to be called directly by the outside caller */
#define __D_PTHREAD(fn, x)						\
	({								\
		int _rc;						\
		_rc = fn(x);						\
		D_ASSERTF(_rc == 0, "%s rc=%d %s\n", #fn, _rc,		\
			  strerror(_rc));				\
		d_errno2der(_rc);					\
	})

#define __D_PTHREAD_INIT(fn, x, y)					\
	({								\
		int _rc;						\
		_rc = fn(x, y);						\
		if (_rc != 0) {						\
			D_ASSERT(_rc != EINVAL);			\
			D_ERROR("%s failed; rc=%d\n", #fn, _rc);	\
		}							\
		d_errno2der(_rc);					\
	})


#define D_SPIN_LOCK(x)		__D_PTHREAD(pthread_spin_lock, x)
#define D_SPIN_UNLOCK(x)	__D_PTHREAD(pthread_spin_unlock, x)
#define D_MUTEX_LOCK(x)		__D_PTHREAD(pthread_mutex_lock, x)
#define D_MUTEX_UNLOCK(x)	__D_PTHREAD(pthread_mutex_unlock, x)
#define D_RWLOCK_RDLOCK(x)	__D_PTHREAD(pthread_rwlock_rdlock, x)
#define D_RWLOCK_WRLOCK(x)	__D_PTHREAD(pthread_rwlock_wrlock, x)
#define D_RWLOCK_UNLOCK(x)	__D_PTHREAD(pthread_rwlock_unlock, x)
#define D_MUTEX_DESTROY(x)	__D_PTHREAD(pthread_mutex_destroy, x)
#define D_SPIN_DESTROY(x)	__D_PTHREAD(pthread_spin_destroy, x)
#define D_RWLOCK_DESTROY(x)	__D_PTHREAD(pthread_rwlock_destroy, x)
#define D_MUTEX_INIT(x, y)	__D_PTHREAD_INIT(pthread_mutex_init, x, y)
#define D_SPIN_INIT(x, y)	__D_PTHREAD_INIT(pthread_spin_init, x, y)
#define D_RWLOCK_INIT(x, y)	__D_PTHREAD_INIT(pthread_rwlock_init, x, y)


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
unsigned int d_hash_srch_u64(uint64_t *hashes, unsigned int nhashes,
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

int d_rank_list_dup(d_rank_list_t **dst, const d_rank_list_t *src);
int d_rank_list_dup_sort_uniq(d_rank_list_t **dst, const d_rank_list_t *src);
void d_rank_list_filter(d_rank_list_t *src_set, d_rank_list_t *dst_set,
			bool exclude);
d_rank_list_t *d_rank_list_alloc(uint32_t size);
d_rank_list_t *d_rank_list_realloc(d_rank_list_t *ptr, uint32_t size);
void d_rank_list_free(d_rank_list_t *rank_list);
int d_rank_list_copy(d_rank_list_t *dst, d_rank_list_t *src);
void d_rank_list_sort(d_rank_list_t *rank_list);
bool d_rank_list_find(d_rank_list_t *rank_list, d_rank_t rank, int *idx);
int d_rank_list_del(d_rank_list_t *rank_list, d_rank_t rank);
bool d_rank_list_identical(d_rank_list_t *rank_list1,
			   d_rank_list_t *rank_list2);
bool d_rank_in_rank_list(d_rank_list_t *rank_list, d_rank_t rank);
int d_idx_in_rank_list(d_rank_list_t *rank_list, d_rank_t rank, uint32_t *idx);
int d_rank_list_append(d_rank_list_t *rank_list, d_rank_t rank);
int d_rank_list_dump(d_rank_list_t *rank_list, d_string_t name, int name_len);
d_rank_list_t *uint32_array_to_rank_list(uint32_t *ints, size_t len);
int rank_list_to_uint32_array(d_rank_list_t *rl, uint32_t **ints, size_t *len);
int d_sgl_init(d_sg_list_t *sgl, unsigned int nr);
void d_sgl_fini(d_sg_list_t *sgl, bool free_iovs);
void d_getenv_bool(const char *env, bool *bool_val);
void d_getenv_int(const char *env, unsigned int *int_val);
int d_write_string_buffer(struct d_string_buffer_t *buf, const char *fmt, ...);
void d_free_string(struct d_string_buffer_t *buf);


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

/**
 * convert system errno to DER_* variaent
 *
 * \param[in] err	System error code
 *
 * \return		Corresponding DER_* error code
 */
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

/**
 * Return current time in timespec form
 *
 * \param[out] t	timespec returned
 *
 * \return		0 on success, negative value on error
 */
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

/**
 * Calculate \a t2 - \a t1 time difference in nanoseconds
 *
 * \param[in] t1	First timespec
 * \param[in] t2	Second timespec
 *
 * \return		Time difference in nanoseconds
 */
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

static inline struct timespec
d_time_elapsed(const struct timespec start)
{
	struct timespec		now;

	d_gettime(&now);

	return d_timediff(start, now);
}

/* calculate the number in us after \param sec_diff second */
static inline uint64_t
d_timeus_secdiff(unsigned int sec_diff)
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

static inline bool
is_on_stack(void *ptr)
{
	int local_var;

	if ((uintptr_t)&local_var < (uintptr_t)ptr &&
	    ((uintptr_t)&local_var + 100) > (uintptr_t)ptr)
		return true;
	return false;
}


static inline void
d_iov_set_safe(d_iov_t *iov, void *buf, size_t size)
{
	D_ASSERTF(!is_on_stack(buf), "buf (%p) is on the stack.\n", buf);

	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}


#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __GURT_COMMON_H__ */
