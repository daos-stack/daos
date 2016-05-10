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

#ifndef __DAOS_COMMON_H__
#define __DAOS_COMMON_H__

#include <sys/time.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <daos_types.h>

#define DAOS_ENV_DEBUG	"DAOS_DEBUG"

/**
 * Debugging flags (32 bits, non-overlapping)
 */
enum {
	DF_UNKNOWN	= (1 << 0),
	DF_VERB_FUNC	= (1 << 1),
	DF_VERB_ALL	= (1 << 2),
	DF_CL		= (1 << 5),
	DF_CL2		= (1 << 6),
	DF_CL3		= (1 << 7),
	DF_PL		= (1 << 8),
	DF_PL2		= (1 << 9),
	DF_PL3		= (1 << 10),
	DF_TP		= (1 << 11),
	DF_VOS1		= (1 << 12),
	DF_VOS2		= (1 << 13),
	DF_VOS3		= (1 << 14),
	DF_SERVER	= (1 << 15),
	DF_MGMT		= (1 << 16),
	DF_DSMC		= (1 << 17),
	DF_DSMS		= (1 << 18),
	DF_MISC		= (1 << 30),
	DF_MEM		= (1 << 31),
};

unsigned int daos_debug_mask(void);

#define D_PRINT(fmt, ...)						\
do {									\
	fprintf(stdout, fmt, ## __VA_ARGS__);				\
	fflush(stdout);							\
} while (0)

#define D_DEBUG(mask, fmt, ...)						\
do {									\
	unsigned int __mask = daos_debug_mask();			\
	if (!((__mask & (mask)) & ~(DF_VERB_FUNC | DF_VERB_ALL)))	\
		break;							\
	if (__mask & DF_VERB_ALL) {					\
		fprintf(stdout, "%s:%d:%d:%s() " fmt, __FILE__,		\
			getpid(), __LINE__, __func__, ## __VA_ARGS__);  \
	} else if (__mask & DF_VERB_FUNC) {				\
		fprintf(stdout, "%s() " fmt,				\
			__func__, ## __VA_ARGS__);			\
	} else {							\
		fprintf(stdout, fmt, ## __VA_ARGS__);			\
	}								\
	fflush(stdout);							\
} while (0)

#define D_ERROR(fmt, ...)						\
do {									\
	fprintf(stderr, "%s:%d:%d:%s() " fmt, __FILE__, getpid(),	\
		__LINE__, __func__, ## __VA_ARGS__);			\
	fflush(stderr);							\
} while (0)

#define D_FATAL(error, fmt, ...)					\
do {									\
	fprintf(stderr, "%s:%d:%s() " fmt, __FILE__, __LINE__,		\
		__func__, ## __VA_ARGS__);				\
	fflush(stderr);							\
	exit(error);							\
} while (0)

#define D_ASSERT(e)	assert(e)

#define D_ASSERTF(cond, fmt, ...)					\
do {									\
	if (!(cond))							\
		D_ERROR(fmt, ## __VA_ARGS__);				\
	assert(cond);							\
} while (0)

#define D_CASSERT(cond)							\
	do {switch (1) {case (cond): case 0: break; } } while (0)

#define DF_U64		"%" PRIu64
#define DF_X64		"%" PRIx64

#define DF_OID		DF_U64"."DF_U64"."DF_U64
#define DP_OID(o)	(o).hi, (o).mid, (o).lo

#define DF_UOID		DF_OID".%u"
#define DP_UOID(uo)	DP_OID((uo).id_pub), (uo).id_shard

/* Only print the first eight bytes. */
#define DF_UUID		DF_X64

static inline uint64_t
DP_UUID(const void *uuid)
{
	const uint64_t *p = (const uint64_t *)uuid;

	return *p;
}

/* memory allocating macros */
#define D_ALLOC(ptr, size)						 \
	do {								 \
		(ptr) = (__typeof__(ptr))calloc(1, size);		 \
		if ((ptr) != NULL)					 \
			break;						 \
		D_ERROR("out of memory (tried to alloc '" #ptr "' = %d)",\
			(int)(size));					 \
	} while (0)

# define D_FREE(ptr, size)						\
	do {								\
		free(ptr);						\
		(ptr) = NULL;						\
	} while ((size) - (size))

#define D_ALLOC_PTR(ptr)        D_ALLOC(ptr, sizeof *(ptr))
#define D_FREE_PTR(ptr)         D_FREE(ptr, sizeof *(ptr))

#define D_GOTO(label, rc)       do { ((void)(rc)); goto label; } while (0)

#define DAOS_GOLDEN_RATIO_PRIME_64	0xcbf29ce484222325ULL
#define DAOS_GOLDEN_RATIO_PRIME_32	0x9e370001UL

static inline uint64_t
daos_u64_hash(uint64_t val, unsigned int bits)
{
	uint64_t hash = val;

	hash *= DAOS_GOLDEN_RATIO_PRIME_64;
	return hash >> (64 - bits);
}

static inline uint32_t
daos_u32_hash(uint64_t key, unsigned int bits)
{
	return (DAOS_GOLDEN_RATIO_PRIME_32 * key) >> (32 - bits);
}

uint64_t daos_hash_mix64(uint64_t key);
uint32_t daos_hash_mix96(uint32_t a, uint32_t b, uint32_t c);

/** consistent hash search */
unsigned int daos_chash_srch_u64(uint64_t *hashes, unsigned int nhashes,
				 uint64_t value);

/** djb2 hash a string to a uint32_t value */
uint32_t daos_hash_string_u32(const char *string);
/** murmur hash (64 bits) */
uint64_t daos_hash_murmur64(const unsigned char *key, unsigned int key_len,
			    unsigned int seed);

#define LOWEST_BIT_SET(x)       ((x) & ~((x) - 1))

static inline unsigned int
daos_power2_nbits(unsigned int val)
{
	unsigned int shift;

	for (shift = 1; (val >> shift) != 0; shift++);

	return val == LOWEST_BIT_SET(val) ? shift - 1 : shift;
}

/** Function table for combsort and binary search */
typedef struct {
	void    (*so_swap)(void *array, int a, int b);
	/**
	 * For ascending order:
	 * 0	array[a] == array[b]
	 * 1	array[a] > array[b]
	 * -1	array[a] < array[b]
	 */
	int     (*so_cmp)(void *array, int a, int b);
	/** for binary search */
	int	(*so_cmp_key)(void *array, int i, uint64_t key);
} daos_sort_ops_t;

int daos_array_sort(void *array, unsigned int len, bool unique,
		    daos_sort_ops_t *ops);
int daos_array_find(void *array, unsigned int len, uint64_t key,
		    daos_sort_ops_t *ops);

#define DAOS_HDL_INVAL	((daos_handle_t){0})

static inline bool
daos_handle_is_inval(daos_handle_t hdl)
{
	return hdl.cookie == 0;
}

static inline void
daos_iov_set(daos_iov_t *iov, void *buf, daos_size_t size)
{
	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}

int
daos_rank_list_dup(daos_rank_list_t **dst, const daos_rank_list_t *src,
		   bool input);
void
daos_rank_list_free(daos_rank_list_t *rank_list);
void
daos_rank_list_copy(daos_rank_list_t *dst, daos_rank_list_t *src, bool input);
bool
daos_rank_list_identical(daos_rank_list_t *rank_list1,
			 daos_rank_list_t *rank_list2, bool input);

#if !defined(container_of)
/* given a pointer @ptr to the field @member embedded into type (usually
 *  * struct) @type, return pointer to the embedding instance of @type. */
# define container_of(ptr, type, member)		\
	        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

#ifndef offsetof
# define offsetof(typ,memb)	((long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
# define MIN(a,b) (((a)<(b)) ? (a): (b))
#endif
#ifndef MAX
# define MAX(a,b) (((a)>(b)) ? (a): (b))
#endif

#ifndef min
#define min(x,y) ((x)<(y) ? (x) : (y))
#endif

#ifndef max
#define max(x,y) ((x)>(y) ? (x) : (y))
#endif

#ifndef min_t
#define min_t(type,x,y) \
	        ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#endif
#ifndef max_t
#define max_t(type,x,y) \
	        ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })
#endif

#define DAOS_UUID_STR_SIZE 36	/* including '\0' */

#endif /* __DAOS_COMMON_H__ */
