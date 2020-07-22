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
 * GURT types.
 */

/** @defgroup GURT GURT */
/** @defgroup GURT_LOG Gurt Log */
/** @defgroup GURT_DEBUG Gurt Debug */
#ifndef __GURT_TYPES_H__
#define __GURT_TYPES_H__

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

/** @addtogroup GURT
 * @{
 */

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__has_warning)
#define D_HAS_WARNING(gcc_version, warning)	__has_warning(warning)
#else  /* !defined(__has_warning) */
#define D_HAS_WARNING(gcc_version, warning) ((gcc_version) <= __GNUC__)
#endif /* defined(__has_warning) */

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
	/** list of ranks */
	d_rank_t	*rl_ranks;
	/** number of ranks */
	uint32_t	rl_nr;
} d_rank_list_t;

typedef d_rank_list_t	*d_rank_list_ptr_t;

typedef char		*d_string_t;
typedef const char	*d_const_string_t;

/** Scatter/gather list for memory buffers */
typedef struct {
	uint32_t	sg_nr;
	uint32_t	sg_nr_out;
	d_iov_t		*sg_iovs;
} d_sg_list_t;

static inline void
d_iov_set(d_iov_t *iov, void *buf, size_t size)
{
	iov->iov_buf = buf;
	iov->iov_len = iov->iov_buf_len = size;
}

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __GURT_TYPES_H__ */
