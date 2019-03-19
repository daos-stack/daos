/* Copyright (C) 2017-2018 Intel Corporation
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
 *
 * A simple, efficient pool for allocating small objects of equal size.
 */
#ifndef __IOF_POOL_H__
#define __IOF_POOL_H__

#include <pthread.h>
#include <gurt/list.h>

/* A datastructure used to describe and register a type */
struct iof_pool_reg {
	/* Perform any one-time setup or assigning constants.
	 */
	void	(*init)(void *, void *);

	/* Prepare an object for use by freeing any old data
	 * and allocating new data.
	 * Returns true on success.
	 */
	bool	(*reset)(void *);

	/* Called once at teardown */
	void	(*release)(void *);
	char	*name;
	int	size;
	int	offset;

	/* Maximum number of descriptors to exist concurrently */
	int	max_desc;
	/* Maximum number of descriptors to exist on the free_list */
	int	max_free_desc;
};

/* If max_desc is non-zero then at most max_desc descriptors can exist
 * simultaneously.  In this case restock() will not allocate new descriptors
 * so all descriptors after startup will be created on the critical path,
 * however once max_desc is reached no more descriptors will be created.
 */

#define POOL_TYPE_INIT(itype, imember) .size = sizeof(struct itype),	\
		.offset = offsetof(struct itype, imember),		\
		.name = #itype,

/* A datastructure used to manage a type.  Includes both the
 * registration data and any live state
 */
struct iof_pool_type {
	struct iof_pool_reg	reg;
	d_list_t		type_list;
	d_list_t		free_list;
	d_list_t		pending_list;
	pthread_mutex_t		lock;
	struct iof_pool		*pool;

	/* Counters for current number of objects */
	int			count; /* Total currently created */
	int			free_count; /* Number currently free */
	int			pending_count; /* Number currently created */

	/* Statistics counters */
	int			init_count;
	int			reset_count;
	int			release_count;

	/* Performance metrics */
	int			op_init; /* Number of on-path init calls */
	int			op_reset; /* Number of on-path reset calls */
	/* Number of sequental calls to acquire() without a call to restock() */
	int			no_restock; /* Current count */
	int			no_restock_hwm; /* High water mark */
};

struct iof_pool {
	d_list_t	list;
	void		*arg;
	pthread_mutex_t	lock;
	bool		init;
};

/* Create a new pool, called once at startup
 *
 * Returns a CaRT error code.
 */
int iof_pool_init(struct iof_pool *, void *arg)
	__attribute((warn_unused_result, nonnull(1)));

/* Destroy a pool, called once at shutdown */
void iof_pool_destroy(struct iof_pool *);

/* Register a new type to a pool, called multiple times after init */
struct iof_pool_type *
iof_pool_register(struct iof_pool *, struct iof_pool_reg *);

/* Allocate a datastructure in performant way */
void *iof_pool_acquire(struct iof_pool_type *);

/* Release a datastructure in a performant way */
void iof_pool_release(struct iof_pool_type *, void *);

/* Pre-allocate datastructures
 * This should be called off the critical path, after previous acquire/release
 * calls and will do memory allocation as required.  Only 1 call is needed after
 * transitions so it does not need calling in progress loops.
 */
void iof_pool_restock(struct iof_pool_type *);

/* Reclaim any memory possible across all types
 *
 * Returns true of there are any descriptors in use.
 */
bool iof_pool_reclaim(struct iof_pool *)
	__attribute((warn_unused_result, nonnull));

#endif /*  __IOF_POOL_H__ */
