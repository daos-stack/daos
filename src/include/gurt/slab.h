/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __SLAB_H__
#define __SLAB_H__

#include <pthread.h>
#include <stdbool.h>
#include <gurt/list.h>

/* A data structure used to describe and register a type */
struct d_slab_reg {
	/* Perform any one-time setup or assigning constants.
	 */
	void	(*sr_init)(void *, void *);

	/* Prepare an object for use by freeing any old data
	 * and allocating new data.
	 * Returns true on success.
	 */
	bool	(*sr_reset)(void *);

	/* Called once at teardown */
	void	(*sr_release)(void *);
	char	*sr_name;
	int	sr_size;
	int	sr_offset;

	/* Maximum number of descriptors to exist concurrently */
	int	sr_max_desc;
	/* Maximum number of descriptors to exist on the free_list */
	int	sr_max_free_desc;
};

/* If max_desc is non-zero then at most max_desc descriptors can exist
 * simultaneously.  In this case restock() will not allocate new descriptors
 * so all descriptors after startup will be created on the critical path,
 * however once max_desc is reached no more descriptors will be created.
 */

#define POOL_TYPE_INIT(itype, imember) .sr_size = sizeof(struct itype),	\
		.sr_offset = offsetof(struct itype, imember),		\
		.sr_name = #itype,

/* A data structure used to manage a type.  Includes both the
 * registration data and any live state
 */
struct d_slab_type {
	struct d_slab_reg	st_reg;
	d_list_t		st_type_list;
	d_list_t		st_free_list;
	d_list_t		st_pending_list;
	pthread_mutex_t		st_lock;
	struct d_slab		*st_slab;

	/* Counters for current number of objects */
	int			st_count; /* Total currently created */
	int			st_free_count; /* Number currently free */
	int			st_pending_count; /* Number currently created */

	/* Statistics counters */
	int			st_init_count;
	int			st_reset_count;
	int			st_release_count;

	/* Performance metrics */
	int			st_op_init; /* Number of on-path init calls */
	int			st_op_reset; /* Number of on-path reset calls */
	/* Number of sequental calls to acquire() without a call to restock() */
	int			st_no_restock; /* Current count */
	int			st_no_restock_hwm; /* High water mark */
};

struct d_slab {
	d_list_t	slab_list;
	void		*slab_arg;
	pthread_mutex_t	slab_lock;
	bool		slab_init;
};

/* Create a new data slab manager, called once at startup
 *
 * Returns a CaRT error code.
 */
int
d_slab_init(struct d_slab *, void *arg)
	__attribute((warn_unused_result, nonnull(1)));

/* Destroy a data slab manager, called once at shutdown */
void
d_slab_destroy(struct d_slab *);

/* Register a new type to a manager, called multiple times after init */
struct d_slab_type *
d_slab_register(struct d_slab *, struct d_slab_reg *);

/* Allocate a data structure in performant way */
void *
d_slab_acquire(struct d_slab_type *);

/* Release a data structure in a performant way */
void
d_slab_release(struct d_slab_type *, void *);

/* Pre-allocate data structures
 * This should be called off the critical path, after previous acquire/release
 * calls and will do memory allocation as required.  Only 1 call is needed after
 * transitions so it does not need calling in progress loops.
 */
void
d_slab_restock(struct d_slab_type *);

/* Reclaim any memory possible across all types
 *
 * Returns true of there are any descriptors in use.
 */
bool
d_slab_reclaim(struct d_slab *)
	__attribute((warn_unused_result, nonnull));

#endif /*  __SLAB_H__ */
