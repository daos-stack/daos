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
		fprintf(stdout, "%s:%d:%s() " fmt, __FILE__, __LINE__,	\
			__FUNCTION__, ## __VA_ARGS__);			\
	} else if (__mask & DF_VERB_FUNC) {				\
		fprintf(stdout, "%s() " fmt,				\
			__FUNCTION__, ## __VA_ARGS__);			\
	} else {							\
		fprintf(stdout, fmt, ## __VA_ARGS__);			\
	}								\
	fflush(stdout);							\
} while (0)

#define D_ERROR(fmt, ...)						\
do {									\
	fprintf(stderr, fmt, ## __VA_ARGS__);				\
	fflush(stderr);							\
} while (0)

#define D_FATAL(error, fmt, ...)					\
do {									\
	fprintf(stderr, "%s:%d:%s() " fmt, __FILE__, __LINE__,		\
		__FUNCTION__, ## __VA_ARGS__);				\
	fflush(stderr);							\
	exit(error);							\
} while (0)

#define D_ASSERT(e)	assert(e)

#define D_ASSERTF(cond, ...)						\
do {									\
	if (!(cond))							\
		D_ERROR(__VA_ARGS__);					\
	assert(cond);							\
} while (0)

#define D_CASSERT(cond)							\
	do {switch (1) {case (cond): case 0: break; } } while (0)

#define DF_U64		"%"PRIu64
#define DF_X64		"%"PRIx64

#if !defined(container_of)
/* given a pointer @ptr to the field @member embedded into type (usually
 * struct) @type, return pointer to the embedding instance of @type. */
# define container_of(ptr, type, member)				\
	((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

#ifndef offsetof
# define offsetof(typ,memb)	((long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
# define MIN(a,b) (((a)<(b)) ? (a): (b))
#endif
#ifndef MAX
# define MAX(a,b) (((a)>(b)) ? (a): (b))
#endif

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

#endif /* __DAOS_COMMON_H__ */
