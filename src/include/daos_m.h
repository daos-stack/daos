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
/**
 * DSM API
 *
 * A pool handle is required to create, open, and destroy containers (belonging
 * to the pool). Operations requiring container handles do not ask for pool
 * handles at the same time, for the pool handles can be inferred from the
 * container handles.
 */

#ifndef __DSM_API_H__
#define __DSM_API_H__

#include <daos_types.h>
#include <daos_errno.h>
#include <daos_ev.h>

/**
 * Create a pool with "uuid", "mode", and "ntargets".
 *
 * "device" identifies target devices. "size" specifies target sizes (i.e.,
 * maximum amounts of storage space targets can consume) in bytes. Targets are
 * assumed to share the same "path" and "size".
 *
 * "ranks" passes in the rank of each target. The fault domains among the
 * targets are retrieved automatically from external sources.
 *
 * This is a privileged operation.
 */
int
dsm_pool_create(const uuid_t uuid, unsigned int mode, int ntargets,
		const daos_rank_t *ranks, const char *device, uint64_t size,
		daos_event_t *event);

/**
 * Destroy a pool with "uuid". Upon successful completion, no new connections
 * can be made to the pool. The pool data will be destroyed once all existing
 * connections disappear.
 *
 * This is a privileged operation.
 */
int
dsm_pool_destroy(const uuid_t uuid, daos_event_t *event);

/**
 * Connect to a pool with "uuid". "ranks" indicates potential ranks of the pool
 * service replicas. "flags" contains flags such as whether the resulting
 * connection should be read-only or read-write.
 */
int
dsm_pool_connect(const uuid_t uuid, const daos_rank_group_t *ranks,
		 unsigned int flags, daos_handle_t *pool,
		 daos_event_t *event);

/**
 * Disconnect "pool".
 */
int
dsm_pool_disconnect(daos_handle_t pool, daos_event_t *event);

/**
 * Add new targets to a pool with "uuid".
 *
 * This is a privileged operation.
 */
int
dsm_pool_grow(daos_handle_t pool, daos_rank_group_t *ranks,
	      daos_event_t *event);

/**
 * Exclude existing targets in a pool with "uuid".
 */
int
dsm_pool_exclude(daos_handle_t pool, daos_rank_group_t *ranks,
		 daos_event_t *event);

typedef struct dsm_pool_info {
	uuid_t		mps_uuid;
	unsigned int	mps_mode;
	int		mps_ntargets;
	uint64_t	mps_size;
	uint64_t	mps_free;
	/* ... */
} dsm_pool_info_t;

/**
 * Query "pool" info.
 */
int
dsm_pool_query(daos_handle_t pool, dsm_pool_info_t *info, daos_event_t *event);

/**
 * TODO: Pool Map Operations
 *
 * One option: Hide the pool map data structure from upper layers and export
 * these methods instead:
 *
 *   dsm_pool_map_traverse()
 *   dsm_pool_map_get(rank, &target)
 *   dsm_pool_map_put(rank, target)
 *   ...?
 */

/**
 * Create a container with "uuid" and optional "name" in "pool".
 *
 * "name" is only valid in this pool and may be NULL. When it is NULL, the
 * container can only be found by its UUID.
 */
int
dsm_co_create(daos_handle_t pool, const uuid_t uuid, const char *name,
	      daos_event_t *event);

/**
 * DSM_COO_RO opens the container for reading only. This flag conflicts with
 * DSM_COO_RW.
 *
 * DSM_COO_RW opens the container for reading and writing. This flag conflicts
 * with DSM_COO_RO.
 *
 * DSM_COO_CREATE creates the container before opening it. This flag must be
 * set together with either DSM_COO_RO or DSM_COO_RW.
 */
#define DSM_COO_RO	0x0U
#define DSM_COO_RW	0x1U
#define DSM_COO_CREATE	0x2U

/**
 * Open a container with "uuid", optionally creating it first. See also
 * dsm_co_create(). "flags" comprises of DSM_COO_ bits.
 */
int
dsm_co_open(daos_handle_t pool, const uuid_t uuid, unsigned int flags,
	    daos_handle_t *container, daos_co_info_t *info,
	    daos_event_t *event);

/**
 * Close "container".
 */
int
dsm_co_close(daos_handle_t container, daos_event_t *event);

/**
 * Destroy a container with "uuid" in "pool". Upon successful completion, the
 * container can no longer be opened and will be destroyed once all existing
 * container handles disappear.
 */
int
dsm_co_destroy(daos_handle_t pool, const uuid_t uuid, daos_event_t *event);

/**
 * Query "container" info.
 */
int
dsm_co_query(daos_handle_t container, daos_co_info_t *info,
	     daos_event_t *event);

/**
 * List all attribute names in a buffer, with each name terminated by a '\0'.
 *
 *	container	IN  container handle
 *	buffer		OUT buffer
 *	size		IN  buffer size
 *			OUT total size of all names (regardless of actual buffer
 *			    size)
 *	event		IN  completion event
 */
int
dsm_co_xattr_list(daos_handle_t container, char *buffer, size_t *size,
		  daos_event_t *event);

/**
 * Get a set of attributes.
 *
 *	container	IN  container handle
 *	n		IN  number of attributes
 *	names		IN  array of attribute names
 *	buffers		OUT array of attribute values
 *	sizes		IN  array of buffer sizes
 *			OUT array of value sizes (regardless of actual buffer
 *			    sizes)
 *	event		IN  completion event
 */
int
dsm_co_xattr_get(daos_handle_t container, int n, const char *const names[],
		 void *buffers[], size_t *sizes[], daos_event_t *event);

/**
 * Set a set of attributes.
 *
 *	container	IN  container handle
 *	n		IN  number of attributes
 *	names		IN  array of attribute names
 *	values		IN  array of attribute values
 *	sizes		IN  array of value sizes
 *	event		IN  completion event
 */
int
dsm_co_xattr_set(daos_handle_t container, int n, const char *const names[],
		 const void *const values[], const size_t sizes[],
		 daos_event_t *event);

/**
 * Query latest epoch state.
 *
 *	container	IN  container handle
 *	state		OUT latest epoch state
 *	event		IN  completion event
 */
int
dsm_epoch_query(daos_handle_t container, daos_epoch_state_t *state,
		daos_event_t *event);

/**
 * Set the lowest held epoch (LHE) of a container handle.
 *
 *	container	IN  container handle
 *	epoch		IN  epoch to set LHE to
 *	state		OUT latest epoch state
 *	event		IN  completion event
 *
 * The resulting LHE' is determined like this:
 *
 *	LHE' = max(HCE + 1, epoch))
 *
 * The owner of the container handle is responsible for releasing its held
 * epochs by either committing/aborting them or setting LHE to DAOS_EPOCH_MAX.
 */
int
dsm_epoch_hold(daos_handle_t container, daos_epoch_t epoch,
	       daos_epoch_state_t *state, daos_event_t *event);

/**
 * Increase the lowest referenced epoch (LRE) of a container handle.
 *
 *	container	IN  container handle
 *	epoch		IN  epoch to increase LRE to
 *	state		OUT latest epoch state
 *	event		IN  completion event
 *
 * The resulting LRE' is determined like this:
 *
 *	LRE' = min(HCE, max(LRE, epoch))
 */
int
dsm_epoch_slip(daos_handle_t container, daos_epoch_t epoch,
	       daos_epoch_state_t *state, daos_event_t *event);

/**
 * Commit an epoch.
 *
 *	container	IN  container handle
 *	epoch		IN  epoch to commit
 *	depends		IN  epochs "epoch" depends on
 *	ndepends	IN  number of epochs in "depends"
 *	state		OUT latest epoch state
 *	event		IN  completion event
 *
 * "depends" is an array of epochs on which "epoch" depends.  A NULL value
 * indicates that "epoch" is independent.
 */
int
dsm_epoch_commit(daos_handle_t container, daos_epoch_t epoch,
		 const daos_epoch_t *depends, int ndepends,
		 daos_epoch_state_t *state, daos_event_t *event);

/**
 * Abort an epoch.
 *
 *	container	IN  container handle
 *	epoch		IN  epoch to abort
 *	state		OUT latest epoch state
 *	event		IN  completion event
 */
int
dsm_epoch_abort(daos_handle_t container, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *event);

/**
 * Wait for an epoch to be committed.
 *
 *	container	IN  container handle
 *	epoch		IN  epoch to wait for
 *	state		OUT latest epoch state
 *	event		IN  completion event
 */
int
dsm_epoch_wait(daos_handle_t container, daos_epoch_t epoch,
	       daos_epoch_state_t *state, daos_event_t *event);

/**
 * List epochs of all snapshot of a container.
 *
 *	container	IN  container coh
 *	buffer		IN  buffer to epochs
 *			OUT array of epochs of snapshots
 *	n		IN  number of epochs buffer can hold
 *			OUT number of all snapshots (regardless of buffer size)
 *	event		IN  completion event
 */
int
dsm_snap_list(daos_handle_t container, daos_epoch_t *buffer, int *n,
	      daos_event_t *event);

/**
 * Take a snapshot of an epoch.
 *
 *	container	IN  container handle
 *	epoch		IN  epoch to snapshot
 *	event		IN  completion event
 */
int
dsm_snap_create(daos_handle_t container, daos_epoch_t epoch,
		daos_event_t *event);

/**
 * Destroy a snapshot.  The epoch corresponding to the snapshot is not
 * discarded, but may be aggregated.
 *
 *	container	IN  container handle
 *	epoch		IN  snapshot to destory
 *	event		IN  completion event
 */
int
dsm_snap_destroy(daos_handle_t container, daos_epoch_t epoch,
		 daos_event_t *event);

#endif /* __DSM_API_H__ */
