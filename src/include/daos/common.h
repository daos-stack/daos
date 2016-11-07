/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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
#include <byteswap.h>

#include <daos_types.h>
#include <crt_util/common.h>

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
	DF_SR		= (1 << 19),
	DF_SRC		= (1 << 20),
	DF_SRS		= (1 << 21),
	DF_TIER		= (1 << 22),
	DF_TIERC	= (1 << 23),
	DF_TIERS	= (1 << 24),
	DF_MISC		= (1 << 30),
	DF_MEM		= (1 << 31),
};

unsigned int daos_debug_mask(void);
void daos_debug_set(unsigned int mask);

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

/*
 * Each thread has DF_UUID_MAX number of thread-local buffers for UUID strings.
 * Each debug message can have at most this many CP_UUIDs.
 *
 * DF_UUID prints the first eight characters of the string representation,
 * while DF_UUIDF prints the full 36-character string representation. CP_UUID()
 * matches both DF_UUID and DF_UUIDF.
 */
#define DF_UUID_MAX	8
#define DF_UUID		"%.8s"
#define DF_UUIDF	"%s"

/* For prefixes of error messages about a container */
#define DF_CONT			DF_UUID"/"DF_UUID
#define DP_CONT(puuid, cuuid)	CP_UUID(puuid), CP_UUID(cuuid)

/* memory allocating macros */
#define D_ALLOC(ptr, size)	C_ALLOC(ptr, size)
#define D_FREE(ptr, size)	C_FREE(ptr, size)
#define D_ALLOC_PTR(ptr)        D_ALLOC(ptr, sizeof *(ptr))
#define D_FREE_PTR(ptr)         D_FREE(ptr, sizeof *(ptr))

#define D_GOTO(label, rc)       do { ((void)(rc)); goto label; } while (0)

#define DAOS_GOLDEN_RATIO_PRIME_64	0xcbf29ce484222325ULL
#define DAOS_GOLDEN_RATIO_PRIME_32	0x9e370001UL


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


struct daos_oper_grp;
typedef int (*daos_oper_grp_comp_t)(void *args, int rc);

int  daos_oper_grp_create(daos_event_t *ev_up, daos_oper_grp_comp_t comp,
			  void *args, struct daos_oper_grp **grpp);
void daos_oper_grp_destroy(struct daos_oper_grp *grp, int rc);
int  daos_oper_grp_launch(struct daos_oper_grp *grp);
int  daos_oper_grp_new_ev(struct daos_oper_grp *grp,
			  struct daos_event **evpp);

static inline int
daos_errno2der(int err)
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
	default:	return -DER_INVAL;
	}
}

int
daos_fail_loc_init();

#endif /* __DAOS_COMMON_H__ */
