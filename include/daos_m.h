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
 * DAOS-M API
 */

#ifndef __DSM_API_H__
#define __DSM_API_H__

#include <daos_types.h>
#include <daos_errno.h>
#include <daos_ev.h>

/**
 * Create a container, without opening it.  For cases in which a container only
 * needs to be created but not opened.
 *
 *	uuid	IN  container UUID
 *	shards	IN  set of shards to create container on
 *	cshards	IN  consensus subset of shards
 *	event	IN  completion event
 */
int
dsm_co_create(uuid_t uuid, daos_rank_group_t *shards,
	      daos_rank_group_t *cshards, daos_event_t *event);

/**
 * Open a container, optionally creating it first.  See also dsm_co_create().
 *
 *	uuid	IN  container UUID
 *	shards	IN  hint of shards, or if mode contains create, set of
 *		    shards to create
 *	cshards	IN  unused, or if mode contains create, consensus subset of
 *		    shards
 *	mode	IN  read-only, read-write, and optionally also create
 *	coh	OUT container handle
 *	status	OUT container status
 *	event	IN  completion event
 */
int
dsm_co_open(uuid_t uuid, daos_rank_group_t *shards,
	    daos_rank_group_t *cshards, unsigned int mode,
	    daos_handle_t *coh, daos_co_status_t *status,
	    daos_event_t *event);

/**
 * Close a container handle.
 *
 *	coh	IN  container handle
 *	event	IN  completion event
 */
int
dsm_co_close(daos_handle_t coh, daos_event_t *event);

/**
 * Destroy a container.
 *
 *	uuid	IN  container UUID
 *	shards	IN  hint of shards
 *	event	IN  completion event
 */
int
dsm_co_destroy(uuid_t uuid, daos_rank_group_t *shards, daos_event_t *event);

/**
 * Query a container's various information.
 *
 *	coh		IN  container handle
 *	info		OUT container information
 *	shards		OUT list of all shards
 *	disabled	OUT list of indices of disabled shards
 *	n		OUT number of indices in disabled
 *	event		IN  completion event
 */
int
dsm_co_query(daos_handle_t coh, daos_co_info_t *info,
	     daos_rank_group_t *shards, unsigned int *disabled,
	     unsigned int *n, daos_event_t *event);

/**
 * Container Layout
 */

/**
 * Modify a container's layout.  Existing shards in disable are disabled and
 * new shards in add are appended to the list of shards in the order they
 * appear in add.  Disabling an unexistent shard or adding an existing shard
 * gets an error, with the layout left intact.
 *
 * XXX: Whether shard indices can be reused...
 *
 *	coh	IN  container handle
 *	disable	IN  set of existing shards to disable
 *	add	IN  list of new shards to add
 *	cadd	IN  subset of add that should be consensus shards
 *	event	IN  completion event
 */
int
dsm_co_reconfig(daos_handle_t coh, daos_rank_group_t *disable,
		daos_rank_group_t *add, daos_rank_group_t *cadd,
		daos_event_t *event);

/*
 * Container extended attribute
 *
 * An attribute is a name-value pair.  A name must be a '\0'-terminated string.
 * These attributes are not versioned.
 */

/**
 * List all attribute names in a buffer, with each name terminated by a '\0'.
 *
 *	coh	IN  container handle
 *	buffer	OUT buffer
 *	size	IN  buffer size
 *		OUT total size of all names (regardless of actual buffer size)
 *	event	IN  completion event
 */
int
dsm_co_xattr_list(daos_handle_t coh, char *buffer, size_t *size,
		  daos_event_t *event);

/**
 * Get a set of attributes.
 *
 *	coh	IN  container handle
 *	n	IN  number of attributes
 *	names	IN  array of attribute names
 *	buffers	OUT array of attribute values
 *	sizes	IN  array of buffer sizes
 *		OUT array of value sizes (regardless of actual buffer sizes)
 *	event	IN  completion event
 */
int
dsm_co_xattr_get(daos_handle_t coh, unsigned int n, char **names,
		 void **buffers, size_t **sizes, daos_event_t *event);

/**
 * Set a set of attributes.
 *
 *	coh	IN  container handle
 *	n	IN  number of attributes
 *	names	IN  array of attribute names
 *	values	IN  array of attribute values
 *	sizes	IN  array of value sizes
 *	event	IN  completion event
 */
int
dsm_co_xattr_set(daos_handle_t coh, unsigned int n, char **names,
		 void **values, size_t *sizes, daos_event_t *event);

/**
 * Query latest epoch state.
 *
 *	coh   [IN]	container handle
 *	state [OUT]	latest epoch state
 *	event [IN]	completion event
 */
int
dsm_epoch_query(daos_handle_t coh, daos_epoch_state_t *state,
		daos_event_t *event);

/**
 * Set the lowest held epoch (LHE) of a container handle.
 *
 *	coh   [IN]	container handle
 *	epoch [IN]	epoch to set LHE to
 *	state [OUT]	latest epoch state
 *	event [IN]	completion event
 *
 * The resulting LHE' is determined like this:
 *
 *	LHE' = max(HCE + 1, epoch))
 *
 * The owner of the container handle is responsible for releasing its held
 * epochs by either committing/aborting them or setting LHE to DAOS_EPOCH_MAX.
 */
int
dsm_epoch_hold(daos_handle_t coh, daos_epoch_t epoch,
	       daos_epoch_state_t *state, daos_event_t *event);

/**
 * Increase the lowest referenced epoch (LRE) of a container handle.
 *
 *	coh   [IN]	container handle
 *	epoch [IN]	epoch to increase LRE to
 *	state [OUT]	latest epoch state
 *	event [IN]	completion event
 *
 * The resulting LRE' is determined like this:
 *
 *	LRE' = min(HCE, max(LRE, epoch))
 */
int
dsm_epoch_slip(daos_handle_t coh, daos_epoch_t epoch,
	       daos_epoch_state_t *state, daos_event_t *event);

/**
 * Commit an epoch.
 *
 *	coh   [IN]	container handle
 *	epoch [IN]	epoch to commit
 *	depends [IN]	epochs "epoch" depends on
 *	ndepends [IN]	number of epochs in "depends"
 *	state [OUT]	latest epoch state
 *	event [IN]	completion event
 *
 * "depends" is an array of epochs on which "epoch" depends.  A NULL value
 * indicates that "epoch" is independent.
 */
int
dsm_epoch_commit(daos_handle_t coh, daos_epoch_t epoch,
		 const daos_epoch_t *depends, int ndepends,
		 daos_epoch_state_t *state, daos_event_t *event);

/**
 * Abort an epoch.
 *
 *	coh   [IN]	container handle
 *	epoch [IN]	epoch to abort
 *	state [OUT]	latest epoch state
 *	event [IN]	completion event
 */
int
dsm_epoch_abort(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *event);

/** TODO: Batch commit/abort; easy way to specify FF-like dependency */

/**
 * Wait for an epoch to be committed.
 *
 *	coh [IN]	container handle
 *	epoch [IN]	epoch to wait for
 *	state [OUT]	latest epoch state
 *	event [IN]	completion event
 */
int
dsm_epoch_wait(daos_handle_t coh, daos_epoch_t epoch,
	       daos_epoch_state_t *state, daos_event_t *event);

/**
 * Snapshot
 *
 * Snapshots are assumed to be nameless; they can only be referred to be the
 * epochs they correspond to.
 */

/**
 * List epochs of all snapshot of a container.
 *
 *	coh	IN  container coh
 *	buffer	IN  buffer to epochs
 *		OUT array of epochs of snapshots
 *	n	IN  number of epochs buffer can hold
 *		OUT number of all snapshots (regardless of buffer size)
 *	event	IN  completion event
 */
int
dsm_snap_list(daos_handle_t coh, daos_epoch_t *buffer, unsigned int *n,
	      daos_event_t *event);

/**
 * Take a snapshot of an epoch.
 *
 *	coh	IN  container handle
 *	epoch	IN  epoch to snapshot
 *	event	IN  completion event
 */
int
dsm_snap_create(daos_handle_t coh, daos_epoch_t epoch,
		daos_event_t *event);

/**
 * Destroy a snapshot.  The epoch corresponding to the snapshot is not
 * discarded, but may be aggregated.
 *
 *	coh	IN  container handle
 *	epoch	IN  snapshot to destory
 *	event	IN  completion event
 */
int
dsm_snap_destroy(daos_handle_t coh, daos_epoch_t epoch,
		 daos_event_t *event);

#endif /* __DSM_API_H__ */
