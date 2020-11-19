/**
 * (C) Copyright 2017-2020 Intel Corporation.
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

#ifndef __DTM_H__
#define __DTM_H__

#include <pthread.h>
#include <stdbool.h>
#include <gurt/list.h>

/* A data structure used to describe and register a type */
struct d_dtm_reg {
	/* Perform any one-time setup or assigning constants.
	 */
	void	(*dr_init)(void *, void *);

	/* Prepare an object for use by freeing any old data
	 * and allocating new data.
	 * Returns true on success.
	 */
	bool	(*dr_reset)(void *);

	/* Called once at teardown */
	void	(*dr_release)(void *);
	char	*dr_name;
	int	dr_size;
	int	dr_offset;

	/* Maximum number of descriptors to exist concurrently */
	int	dr_max_desc;
	/* Maximum number of descriptors to exist on the free_list */
	int	dr_max_free_desc;
};

/* If max_desc is non-zero then at most max_desc descriptors can exist
 * simultaneously.  In this case restock() will not allocate new descriptors
 * so all descriptors after startup will be created on the critical path,
 * however once max_desc is reached no more descriptors will be created.
 */

#define POOL_TYPE_INIT(itype, imember) .dr_size = sizeof(struct itype),	\
		.dr_offset = offsetof(struct itype, imember),		\
		.dr_name = #itype,

/* A data structure used to manage a type.  Includes both the
 * registration data and any live state
 */
struct d_dtm_type {
	struct d_dtm_reg	dt_reg;
	d_list_t		dt_type_list;
	d_list_t		dt_free_list;
	d_list_t		dt_pending_list;
	pthread_mutex_t		dt_lock;
	struct d_dtm		*dt_dtm;

	/* Counters for current number of objects */
	int			dt_count; /* Total currently created */
	int			dt_free_count; /* Number currently free */
	int			dt_pending_count; /* Number currently created */

	/* Statistics counters */
	int			dt_init_count;
	int			dt_reset_count;
	int			dt_release_count;

	/* Performance metrics */
	int			dt_op_init; /* Number of on-path init calls */
	int			dt_op_reset; /* Number of on-path reset calls */
	/* Number of sequental calls to acquire() without a call to restock() */
	int			dt_no_restock; /* Current count */
	int			dt_no_restock_hwm; /* High water mark */
};

struct d_dtm {
	d_list_t	dtm_list;
	void		*dtm_arg;
	pthread_mutex_t	dtm_lock;
	bool		dtm_init;
};

/* Create a new data type manager, called once at startup
 *
 * Returns a CaRT error code.
 */
int
d_dtm_init(struct d_dtm *, void *arg)
	__attribute((warn_unused_result, nonnull(1)));

/* Destroy a data type manager, called once at shutdown */
void
d_dtm_destroy(struct d_dtm *);

/* Register a new type to a manager, called multiple times after init */
struct d_dtm_type *
d_dtm_register(struct d_dtm *, struct d_dtm_reg *);

/* Allocate a data structure in performant way */
void *
d_dtm_acquire(struct d_dtm_type *);

/* Release a data structure in a performant way */
void
d_dtm_release(struct d_dtm_type *, void *);

/* Pre-allocate data structures
 * This should be called off the critical path, after previous acquire/release
 * calls and will do memory allocation as required.  Only 1 call is needed after
 * transitions so it does not need calling in progress loops.
 */
void
d_dtm_restock(struct d_dtm_type *);

/* Reclaim any memory possible across all types
 *
 * Returns true of there are any descriptors in use.
 */
bool
d_dtm_reclaim(struct d_dtm *)
	__attribute((warn_unused_result, nonnull));

#endif /*  __DTM_H__ */
