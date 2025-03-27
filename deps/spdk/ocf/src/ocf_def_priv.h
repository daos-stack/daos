/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __OCF_DEF_PRIV_H__
#define __OCF_DEF_PRIV_H__

#include "ocf/ocf.h"
#include "ocf_env.h"

#define BYTES_TO_SECTORS(x) ((x) >> ENV_SECTOR_SHIFT)
#define SECTORS_TO_BYTES(x) ((x) << ENV_SECTOR_SHIFT)

#define BYTES_TO_PAGES(x)	((((uint64_t)x) + (PAGE_SIZE - 1)) / PAGE_SIZE)
#define PAGES_TO_BYTES(x)	(((uint64_t)x) * PAGE_SIZE)

#define OCF_DIV_ROUND_UP(x, y)			\
	({					\
		__typeof__ (x) __x = (x);	\
		__typeof__ (y) __y = (y);	\
		(__x + __y - 1) / __y;		\
	})

#define OCF_MAX(x,y)				\
	({					\
		__typeof__ (x) __x = (x);	\
		__typeof__ (y) __y = (y);	\
		__x > __y ? __x : __y;		\
	})

#define OCF_MIN(x,y)				\
	({					\
		__typeof__ (x) __x = (x);	\
		__typeof__ (y) __y = (y);	\
		__x < __y ? __x : __y;		\
	})

#define METADATA_VERSION() ((OCF_VERSION_MAIN << 16) + \
		(OCF_VERSION_MAJOR << 8) + OCF_VERSION_MINOR)

/* call conditional reschedule every 'iterations' calls */
#define OCF_COND_RESCHED(cnt, iterations) \
	if (unlikely(++(cnt) == (iterations))) { \
		env_cond_resched(); \
		(cnt) = 0; \
	}

/* call conditional reschedule with default interval */
#define OCF_COND_RESCHED_DEFAULT(cnt) OCF_COND_RESCHED(cnt, 1000000)

static inline unsigned long long
ocf_rotate_right(unsigned long long bits, unsigned shift, unsigned width)
{
	return ((bits >> shift) | (bits << (width - shift))) &
		((1ULL << width) - 1);
}

#endif
