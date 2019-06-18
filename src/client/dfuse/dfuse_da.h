/**
 * (C) Copyright 2017-2019 Intel Corporation.
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

#ifndef __DFUSE_DA_H__
#define __DFUSE_DA_H__

#include <pthread.h>
#include <gurt/list.h>

/* A datastructure used to describe and register a type */
struct dfuse_da_reg {
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
struct dfuse_da_type {
	struct dfuse_da_reg	reg;
	d_list_t		type_list;
	d_list_t		free_list;
	d_list_t		pending_list;
	pthread_mutex_t		lock;
	struct dfuse_da		*da;

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

struct dfuse_da {
	d_list_t	list;
	void		*arg;
	pthread_mutex_t	lock;
	bool		init;
};

/* Create a new da, called once at startup
 *
 * Returns a CaRT error code.
 */
int
dfuse_da_init(struct dfuse_da *, void *arg)
	__attribute((warn_unused_result, nonnull(1)));

/* Destroy a da, called once at shutdown */
void
dfuse_da_destroy(struct dfuse_da *);

/* Register a new type to a da, called multiple times after init */
struct dfuse_da_type *
dfuse_da_register(struct dfuse_da *, struct dfuse_da_reg *);

/* Allocate a datastructure in performant way */
void *
dfuse_da_acquire(struct dfuse_da_type *);

/* Release a datastructure in a performant way */
void
dfuse_da_release(struct dfuse_da_type *, void *);

/* Pre-allocate datastructures
 * This should be called off the critical path, after previous acquire/release
 * calls and will do memory allocation as required.  Only 1 call is needed after
 * transitions so it does not need calling in progress loops.
 */
void
dfuse_da_restock(struct dfuse_da_type *);

/* Reclaim any memory possible across all types
 *
 * Returns true of there are any descriptors in use.
 */
bool
dfuse_da_reclaim(struct dfuse_da *)
	__attribute((warn_unused_result, nonnull));

#endif /*  __DFUSE_DA_H__ */
