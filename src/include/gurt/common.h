/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#ifdef D_HAS_VALGRIND
#include <valgrind/valgrind.h>
#define D_ON_VALGRIND RUNNING_ON_VALGRIND
#else
#define D_ON_VALGRIND 0
#endif

#include <gurt/types.h>
#include <gurt/debug.h>
#include <gurt/fault_inject.h>

/** @addtogroup GURT
 * @{
 */

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef likely
#define likely(x)	__builtin_expect((uintptr_t)(x), 1)
#endif
#ifndef unlikely
#define unlikely(x)	__builtin_expect((uintptr_t)(x), 0)
#endif

/* Check if bit is set in passed val */
#define D_BIT_IS_SET(val, bit) (((val) & bit) ? 1 : 0)

/**
 * Get the current time using a monotonic timer
 * param[out] ts A timespec structure for the result
 */
#define _gurt_gettime(ts) clock_gettime(CLOCK_MONOTONIC, ts)

/* rand and srand macros */

#define D_RAND_MAX 0x7fffffff

void d_srand(long int);
long int d_rand(void);

/* memory allocating macros */
void  d_free(void *);
void *d_calloc(size_t, size_t);
void *d_malloc(size_t);
void *d_realloc(void *, size_t);
char *d_strndup(const char *s, size_t n);
int d_asprintf(char **strp, const char *fmt, ...);
void *d_aligned_alloc(size_t alignment, size_t size);
char *d_realpath(const char *path, char *resolved_path);

#define D_CHECK_ALLOC(func, cond, ptr, name, size, count, cname,	\
			on_error)					\
	do {								\
		if (D_SHOULD_FAIL(d_fault_attr_mem)) {			\
			d_free(ptr);					\
			(ptr) = NULL;					\
		}							\
		if ((cond) && (ptr) != NULL) {				\
			if ((count) <= 1)				\
				D_DEBUG(DB_MEM,				\
					"alloc(" #func ") '" name	\
					"':%i at %p.\n",		\
					(int)(size), (ptr));		\
			else						\
				D_DEBUG(DB_MEM,				\
					"alloc(" #func ") '" name	\
					"':%i * '" cname "':%i at %p.\n", \
					(int)(size), (int)(count), (ptr)); \
			break;						\
		}							\
		(void)(on_error);					\
		if ((count) >= 1)					\
			D_ERROR("out of memory (tried to "		\
				#func " '" name "':%i)\n",		\
				(int)((size) * (count)));		\
		else							\
			D_ERROR("out of memory (tried to "		\
				#func " '" name "':%i)\n",		\
				(int)(size));				\
	} while (0)

#define D_ALLOC_CORE(ptr, size, count)					\
	do {								\
		(ptr) = (__typeof__(ptr))d_calloc((count), (size));	\
		D_CHECK_ALLOC(calloc, true, ptr, #ptr, size,		\
			      count, #count, 0);			\
	} while (0)

#define D_ALLOC_CORE_NZ(ptr, size, count)				\
	do {								\
		(ptr) = (__typeof__(ptr))d_malloc((count) * (size));	\
		D_CHECK_ALLOC(malloc, true, ptr, #ptr, size,		\
			      count, #count, 0);			\
	} while (0)

#define D_STRNDUP(ptr, s, n)						\
	do {								\
		(ptr) = d_strndup(s, n);				\
		D_CHECK_ALLOC(strndup, true, ptr, #ptr,			\
			      strnlen(s, n) + 1, 0, #ptr, 0);		\
	} while (0)

/* This can be used for duplicating static strings, it will work for strings
 * which are defined in-place or through #define but it will not work with
 * strings which are defined as a char * variable as in that case it'll copy
 * the first 8 bytes.  To avoid this case add a static assert on the size,
 * so code that tries to mis-use this macro will fail at compile time.
 */

#define D_STRNDUP_S(ptr, s)						\
	do {								\
		_Static_assert(sizeof(s) != sizeof(void *) ||		\
			__builtin_types_compatible_p(__typeof__(s), __typeof__("1234567")), \
	"D_STRNDUP_S cannot be used with this type");			\
		(ptr) = d_strndup(s, sizeof(s));			\
		D_CHECK_ALLOC(strndup, true, ptr, #ptr,			\
			sizeof(s), 0, #ptr, 0);				\
	} while (0)

#define D_ASPRINTF(ptr, ...)						\
	do {								\
		int _rc;						\
		_rc = d_asprintf(&(ptr), __VA_ARGS__);			\
		D_CHECK_ALLOC(asprintf, _rc != -1,			\
			      ptr, #ptr, _rc + 1, 0, #ptr,		\
			      (ptr) = NULL);				\
	} while (0)

/* d_realpath() can fail with genuine errors, in which case we want to keep the errno from
 * realpath, however if it doesn't fail then we want to preserve the previous errno, in
 * addition the fault injection code could insert an error in the D_CHECK_ALLOC() macro
 * so if that happens then we want to set ENOMEM there.
 */
#define D_REALPATH(ptr, path)						\
	do {								\
		(ptr) = d_realpath((path), NULL);			\
		if ((ptr) != NULL) {					\
			int _size = strnlen(ptr, PATH_MAX + 1) + 1 ;	\
			D_CHECK_ALLOC(realpath, true, ptr, #ptr, _size,	0, #ptr, 0); \
			if (((ptr) == NULL))				\
				errno = ENOMEM;				\
		}							\
	} while (0)

#define D_ALIGNED_ALLOC(ptr, alignment, size)				\
	do {								\
		(ptr) = (__typeof__(ptr))d_aligned_alloc(alignment,	\
							 size);		\
		D_CHECK_ALLOC(aligned_alloc, true, ptr, #ptr,		\
			      size, 0, #ptr, 0);			\
	} while (0)

/* Requires newptr and oldptr to be different variables.  Otherwise
 * there is no way to tell the difference between successful and
 * failed realloc.
 */
#define D_REALLOC_COMMON(newptr, oldptr, oldsize, size, cnt)		\
	do {								\
		size_t _esz = (size_t)(size);				\
		size_t _sz = (size_t)(size) * (cnt);			\
		size_t _oldsz = (size_t)(oldsize);			\
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
			(newptr) = NULL;				\
		else							\
			(newptr) = d_realloc(optr, _sz);		\
		if ((newptr) != NULL) {					\
			if (_cnt <= 1)					\
				D_DEBUG(DB_MEM,				\
					"realloc '" #newptr		\
					"':%zu at %p old '" #oldptr	\
					"':%zu at %p.\n",		\
					_esz, (newptr), _oldsz,		\
					(oldptr));			\
			else						\
				D_DEBUG(DB_MEM,				\
					"realloc '" #newptr		\
					"':%zu * '" #cnt		\
					"':%zu at %p old '" #oldptr	\
					"':%zu at %p.\n",		\
					_esz, _cnt, (newptr), _oldsz,	\
					(oldptr));			\
			(oldptr) = NULL;				\
			if (_oldsz < _sz)				\
				memset((char *)(newptr) + _oldsz, 0,	\
				       _sz - _oldsz);			\
			break;						\
		}							\
		if (_cnt <= 1)						\
			D_ERROR("out of memory (tried to realloc "	\
				"'" #newptr "': size=%zu)\n",		\
				_esz);					\
		else							\
			D_ERROR("out of memory (tried to realloc "	\
				"'" #newptr "': size=%zu count=%zu)\n",	\
				_esz, _cnt);				\
	} while (0)

#define D_REALLOC(newptr, oldptr, oldsize, size)			\
	D_REALLOC_COMMON(newptr, oldptr, oldsize, size, 1)

#define D_REALLOC_ARRAY(newptr, oldptr, oldcount, count)		\
	D_REALLOC_COMMON(newptr, oldptr,				\
			 (oldcount) * sizeof(*(oldptr)),		\
					     sizeof(*(oldptr)), count)

#define D_REALLOC_NZ(newptr, oldptr, size)				\
	D_REALLOC_COMMON(newptr, oldptr, size, size, 1)

#define D_REALLOC_ARRAY_NZ(newptr, oldptr, count)			\
	D_REALLOC_COMMON(newptr, oldptr,				\
			 (count) * sizeof(*(oldptr)),			\
			 sizeof(*(oldptr)), count)

/** realloc macros that do not clear the new memory */
#define D_REALLOC_NZ(newptr, oldptr, size)				\
	D_REALLOC_COMMON(newptr, oldptr, size, size, 1)

#define D_REALLOC_ARRAY_NZ(newptr, oldptr, count)			\
	D_REALLOC_COMMON(newptr, oldptr,				\
			 (count) * sizeof(*(oldptr)),			\
			 sizeof(*(oldptr)), count)

/** realloc macros that clear the whole allocation */
#define D_REALLOC_Z(newptr, oldptr, size)				\
	D_REALLOC_COMMON(newptr, oldptr, 0, size, 1)

#define D_REALLOC_ARRAY_Z(newptr, oldptr, count)			\
	D_REALLOC_COMMON(newptr, oldptr, 0, sizeof(*(oldptr)), count)

#define D_FREE(ptr)							\
	do {								\
		D_DEBUG(DB_MEM, "free '" #ptr "' at %p.\n", (ptr));	\
		d_free(ptr);						\
		(ptr) = NULL;						\
	} while (0)

#define D_ALLOC(ptr, size)	D_ALLOC_CORE(ptr, size, 1)
#define D_ALLOC_PTR(ptr)	D_ALLOC(ptr, sizeof(*ptr))
#define D_ALLOC_ARRAY(ptr, count) D_ALLOC_CORE(ptr, sizeof(*ptr), count)
#define D_ALLOC_NZ(ptr, size)	D_ALLOC_CORE_NZ(ptr, size, 1)
#define D_ALLOC_PTR_NZ(ptr)	D_ALLOC_NZ(ptr, sizeof(*ptr))
#define D_ALLOC_ARRAY_NZ(ptr, count) D_ALLOC_CORE_NZ(ptr, sizeof(*ptr), count)
#define D_FREE_PTR(ptr)		D_FREE(ptr)

/* _Generic() is part of the c11 standard but isn't yet supported by all
 * compilers so there's a configure check for this in scons.
 */

#ifdef USE_GOTO_LOGGING
#ifdef HAVE_GENERIC
#define VERBOSE_GOTO 1
#endif
#endif /* USE_GOTO_LOGGING */

#ifdef VERBOSE_GOTO

#define maybe_derrcode(x) _Generic((x), int : true, default : false)

#define DG_FMT(x)                                                                                  \
	_Generic(__rc,					\
int : "%d",				\
				unsigned int: "%u",			\
				unsigned long: "%lu",			\
				bool : "%d",				\
				int64_t : "%ld",			\
				struct swim_context *  : "%p",		\
				struct crt_hg_hdl *  : "%p",		\
				struct crt_ivns_internal *  : "%p",	\
				d_rank_list_t *  : "%p")

#define GOTO_MSG_LEN 21

#define D_GOTO(label, rc)                                                                          \
	do {                                                                                       \
		__typeof__(rc) __rc = (rc);                                                        \
		if (D_LOG_ENABLED(DB_GOTO)) {                                                      \
			char  __result[GOTO_MSG_LEN] = {0};                                        \
			char *__errcode              = NULL;                                       \
			int   __rcs                  = -DER_INVAL;                                 \
			if (maybe_derrcode(__rc))                                                  \
				__rcs = d_get_errstr((intptr_t)__rc, &__errcode);                  \
			snprintf(__result, GOTO_MSG_LEN, DG_FMT(__rc), __rc);                      \
			if (__rcs == -DER_SUCCESS) {                                               \
				D_DEBUG(DB_GOTO,                                                   \
					"Jumping to " #label " '" #rc "' result '%s': %s\n",       \
					__result, __errcode);                                      \
			} else {                                                                   \
				D_DEBUG(DB_GOTO, "Jumping to " #label " '" #rc "' result '%s'\n",  \
					__result);                                                 \
			}                                                                          \
		}                                                                                  \
		(void)(__rc);                                                                      \
		goto label;                                                                        \
	} while (0)

#else /* VERBOSE_GOTO */

#define D_GOTO(label, rc)                                                                          \
	do {                                                                                       \
		__typeof__(rc) __rc = (rc);                                                        \
		(void)(__rc);                                                                      \
		goto label;                                                                        \
	} while (0)

#endif /* VERBOSE_GOTO */

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
uint32_t d_hash_jump(uint64_t key, uint32_t num_buckets);

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
int d_rank_list_merge(d_rank_list_t *src_set, d_rank_list_t *merge_set);
d_rank_list_t *d_rank_list_alloc(uint32_t size);
d_rank_list_t *d_rank_list_realloc(d_rank_list_t *ptr, uint32_t size);
void d_rank_list_free(d_rank_list_t *rank_list);
int d_rank_list_copy(d_rank_list_t *dst, d_rank_list_t *src);
void d_rank_list_shuffle(d_rank_list_t *rank_list);
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
char *d_rank_list_to_str(d_rank_list_t *rank_list);

d_rank_range_list_t *d_rank_range_list_alloc(uint32_t size);
d_rank_range_list_t *d_rank_range_list_realloc(d_rank_range_list_t *range_list, uint32_t size);
d_rank_range_list_t *d_rank_range_list_create_from_ranks(d_rank_list_t *rank_list);
char *d_rank_range_list_str(d_rank_range_list_t *list, bool *truncated);
void d_rank_range_list_free(d_rank_range_list_t *range_list);

static inline int
d_sgl_init(d_sg_list_t *sgl, unsigned int nr)
{
	sgl->sg_nr_out = 0;
	sgl->sg_nr = nr;

	if (unlikely(nr == 0)) {
		sgl->sg_iovs = NULL;
		return 0;
	}

	D_ALLOC_ARRAY(sgl->sg_iovs, nr);

	return sgl->sg_iovs == NULL ? -DER_NOMEM : 0;
}

static inline void
d_sgl_fini(d_sg_list_t *sgl, bool free_iovs)
{
	uint32_t i;

	if (unlikely(sgl == NULL || sgl->sg_iovs == NULL))
		return;

	if (free_iovs)
		for (i = 0; i < sgl->sg_nr; i++)
			D_FREE(sgl->sg_iovs[i].iov_buf);

	D_FREE(sgl->sg_iovs);
	sgl->sg_nr_out = 0;
	sgl->sg_nr = 0;
}

void d_getenv_bool(const char *env, bool *bool_val);
void d_getenv_int(const char *env, unsigned int *int_val);
int d_getenv_uint64_t(const char *env, uint64_t *val);
int  d_write_string_buffer(struct d_string_buffer_t *buf, const char *fmt, ...);
void d_free_string(struct d_string_buffer_t *buf);

#if !defined(container_of)
/* given a pointer @ptr to the field @member embedded into type (usually
 * struct) @type, return pointer to the embedding instance of @type.
 */
# define container_of(ptr, type, member)		\
	((type *)((char *)(ptr) - (char *)(&((type *)0)->member)))
#endif

#ifndef offsetof
# define offsetof(typ, memb)	((long)((char *)&(((typ *)0)->memb)))
#endif

#define D_ALIGNUP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

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
	return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

static inline double
d_time2us(struct timespec t)
{
	return (double)t.tv_sec * 1e6 + (double)t.tv_nsec / 1e3;
}

static inline double
d_time2s(struct timespec t)
{
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/**
 * Backoff sequence (opaque)
 *
 * Used to generate a sequence of uint32_t backoffs with user-defined semantics
 * (e.g., numbers of microseconds for delaying RPC retries). See
 * d_backoff_seq_init and d_backoff_seq_next for the algorithm.
 */
struct d_backoff_seq {
	uint8_t		bos_flags;	/* unused */
	uint8_t		bos_nzeros;
	uint16_t	bos_factor;
	uint32_t	bos_max;
	uint32_t	bos_next;
};

int d_backoff_seq_init(struct d_backoff_seq *seq, uint8_t nzeros,
		       uint16_t factor, uint32_t next, uint32_t max);
void d_backoff_seq_fini(struct d_backoff_seq *seq);
uint32_t d_backoff_seq_next(struct d_backoff_seq *seq);

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

double d_stand_div(double *array, int nr);

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __GURT_COMMON_H__ */
