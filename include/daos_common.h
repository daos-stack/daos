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
# define MAX(a,b) (((a)>(b))
#endif

/* 2^63 + 2^61 - 2^57 + 2^54 - 2^51 - 2^18 + 1 */
#define DAOS_GOLDEN_RATIO_PRIME_64	0x9e37fffffffc0001ULL

static inline uint64_t
daos_u64_hash(uint64_t val, unsigned int bits)
{
	uint64_t hash = val;
#if 1
	/*  gcc can't optimise this alone like it does for 32 bits. */
	uint64_t n = hash;

	n <<= 18;
	hash -= n;
	n <<= 33;
	hash -= n;
	n <<= 3;
	hash += n;
	n <<= 3;
	hash -= n;
	n <<= 4;
	hash += n;
	n <<= 2;
	hash += n;
#else
	/* On some cpus multiply is faster, on others gcc will do shifts */
	hash *= CFS_GOLDEN_RATIO_PRIME;
#endif
	/* High bits are more random, so use them. */
	return hash >> (64 - bits);
}

static inline uint32_t
daos_u32_hash(uint64_t key, unsigned int bits)
{
	key = (~key) + (key << 18);
	key = key ^ (key >> 31);
	key = key * 21;
	key = key ^ (key >> 11);
	key = key + (key << 6);
	key = key ^ (key >> 22);

	return (uint32_t)key & ((1UL << bits) - 1);
}

/** consistent hash search */
static inline unsigned int
daos_chash_srch_u64(uint64_t *hashes, unsigned int nhashes, uint64_t value)
{
	int	high = nhashes - 1;
	int	low = 0;
	int     i;

        for (i = high / 2; high - low > 1; i = (low + high) / 2) {
                if (value >= hashes[i])
			low = i;
		else /* value < hashes[i] */
			high = i;
	}
	return value >= hashes[high] ? high : low;
}

#define LOWEST_BIT_SET(x)       ((x) & ~((x) - 1))

static inline unsigned int
daos_power2_nbits(unsigned int val)
{
	unsigned int shift;

	for (shift = 1; (val >> shift) != 0; shift++);

	return val == LOWEST_BIT_SET(val) ? shift - 1 : shift;
}

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
