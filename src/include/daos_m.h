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
/**
 * DAOS-M API
 *
 * A pool handle is required to create, open, and destroy containers (belonging
 * to the pool). Operations requiring container handles do not ask for pool
 * handles at the same time, for the pool handles can be inferred from the
 * container handles.
 */

#ifndef __DSM_API_H__
#define __DSM_API_H__

#include <uuid/uuid.h>
#include <daos_event.h>
#include <daos_types.h>
#include <daos_errno.h>

/**
 * Initialize the DAOS-M library.
 */
int
dsm_init(void);

/**
 * Finalize the DAOS-M library.
 */
int
dsm_fini(void);

/**
 * Handle API
 */

/**
 * Convert a local pool connection to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param poh	[IN]	valid local pool connection handle to be shared
 * \param glob	[OUT]	pointer to iov of the buffer to store handle information
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Pool  handle is nonexistent
 *			-DER_TRUNC	Buffer in \a glob is too short, larger
 *					buffer required. In this case the
 *					required buffer size is returned through
 *					glob->iov_buf_len.
 */
int
dsm_pool_local2global(daos_handle_t poh, daos_iov_t *glob);

/**
 * Create a local pool connection for global representation data.
 *
 * \param glob	[IN]	global (shared) representation of a collective handle
 *			to be extracted
 * \param poh	[OUT]	returned local pool connection handle
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 */
int
dsm_pool_global2local(daos_iov_t glob, daos_handle_t *poh);

/**
 * Convert a local container handle to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param coh	[IN]	valid local container handle to be shared
 * \param glob	[OUT]	pointer to iov of the buffer to store handle information
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Container handle is nonexistent
 *			-DER_TRUNC	Buffer in \a glob is too short, larger
 *					buffer required. In this case the
 *					required buffer size is returned through
 *					glob->iov_buf_len.
 */
int
dsm_co_local2global(daos_handle_t coh, daos_iov_t *glob);

/**
 * Create a local container handle for global representation data.
 *
 * \param poh	[IN]	Pool connection handle the container belong to
 * \param glob	[IN]	global (shared) representation of a collective handle
 *			to be extracted
 * \param coh	[OUT]	returned local container handle
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Pool handle is nonexistent
 */
int
dsm_co_global2local(daos_handle_t poh, daos_iov_t glob, daos_handle_t *coh);

/**
 * Pool API
 */

/**
 * Connect to the DAOS pool identified by UUID \a uuid. Upon a successful
 * completion, \a poh returns the pool handle, and \a info return the latest
 * pool information.
 *
 * \param uuid	[IN]	UUID to identify a pool.
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param svc	[IN]	Optional, indicate potential targets of the pool service
 *			replicas. If not aware of the ranks of the pool service
 *			replicas, the caller may pass in NULL.
 * \param flags	[IN]	Connect mode represented by the DAOS_PC_ bits.
 * \param poh	[OUT]	Returned open handle.
 * \param info	[OUT]	Returned pool info.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Pool is nonexistent
 */
int
dsm_pool_connect(const uuid_t uuid, const char *grp,
		 const daos_rank_list_t *svc, unsigned int flags,
		 daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev);

/**
 * Disconnect from the DAOS pool. It should revoke all the container open
 * handles of this pool.
 *
 * \param poh	[IN]	Pool connection handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
dsm_pool_disconnect(daos_handle_t poh, daos_event_t *ev);

/**
 * Exclude a set of storage targets from a pool.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param tgts	[IN]	Target rank array to be excluded from the pool.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsm_pool_exclude(daos_handle_t poh, daos_rank_list_t *tgts, daos_event_t *ev);

/**
 * Query pool information. User should provide at least one of \a info and
 * \a tgts as output buffer.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param tgts	[OUT]	Optional, returned storage targets in this pool.
 * \param info	[OUT]	Optional, returned pool information.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
dsm_pool_query(daos_handle_t poh, daos_rank_list_t *tgts,
	       daos_pool_info_t *info, daos_event_t *ev);

/**
 * Query information of storage targets within a DAOS pool.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param tgts	[IN]	A list of targets to query.
 * \param failed
 *		[OUT]	Optional, buffer to store faulty targets on failure.
 * \param info_list
 *		[OUT]	Returned storage information of \a tgts, it is an array
 *			and array size must equal to tgts::rl_llen.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	No pool on specified targets
 */
int
dsm_pool_target_query(daos_handle_t poh, daos_rank_list_t *tgts,
		      daos_rank_list_t *failed, daos_target_info_t *info_list,
		      daos_event_t *ev);

/**
 * Container API
 */

/**
 * Create a new container with uuid \a uuid on the storage pool connected
 * by \a poh.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param uuid	[IN]	UUID of the new Container.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	network is unreachable
 *			-DER_EXIST	Container uuid already existed
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsm_co_create(daos_handle_t poh, const uuid_t uuid, daos_event_t *ev);

/**
 * Open an existing container identified by UUID \a uuid. Upon a successful
 * completion, \a coh and \a info, both of which shall be allocated by the
 * caller, return the container handle and the latest container information
 * respectively.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param uuid	[IN]	UUID to identify container.
 * \param flags	[IN]	Open mode, represented by the DAOS_COO_ bits.
 * \param coh	[OUT]	Returned open handle.
 * \param info	[OUT]	Optional, return container information
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Container is nonexistent
 */
int
dsm_co_open(daos_handle_t poh, const uuid_t uuid, unsigned int flags,
	    daos_handle_t *coh, daos_co_info_t *info, daos_event_t *ev);

/**
 * Close an opened container.
 *
 * \param coh	[IN]	Container open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid container handle
 */
int
dsm_co_close(daos_handle_t coh, daos_event_t *ev);

/**
 * Destroy a container identfied by \a uuid, all objects within this
 * container will be destroyed as well.
 * If there is at least one container opener, and \a force is set to zero, then
 * the operation completes with DER_BUSY. Otherwise, the container is destroyed
 * when the operation completes.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param uuid	[IN]	Container UUID.
 * \param force	[IN]	Container destroy will return failure is the container
 *			is still busy (still have openers), this parameter will
 *			force the destroy to proceed even there is opener.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Container is nonexistent
 *			-DER_BUSY	Pool is busy
 */
int
dsm_co_destroy(daos_handle_t poh, const uuid_t uuid, int force,
	       daos_event_t *ev);

/**
 * Query container information.
 *
 * \param coh	[IN]	Container open handle.
 * \param info	[OUT]	Returned container information.
 *			If \a info::ci_snapshots is not NULL, epochs of
 *			snapshots will be stored in it.
 *			If \a info::ci_snapshots is NULL, number of snaphots
 *			will be returned by \a info::ci_nsnapshots.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid container handle
 */
int
dsm_co_query(daos_handle_t container, daos_co_info_t *info,
	     daos_event_t *ev);

/**
 * List all attribute names in a buffer, with each name terminated by a '\0'.
 *
 * \param coh	[IN]	container handle
 * \param buf	[OUT]	buffer
 * \param size	[IN]	buffer size
 *		[OUT]	total size of all names (regardless of actual buffer
 *			size)
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_co_attr_list(daos_handle_t coh, char *buf, size_t *size, daos_event_t *ev);

/**
 * Get a set of attributes.
 *
 * \param coh	[IN]	container handle
 * \param n	[IN]	number of attributes
 * \param names	[IN]	array of attribute names
 * \param bufs	[OUT]	array of attribute values
 * \param sizes	[IN]	array of buffer sizes
 *		[OUT]	array of value sizes (regardless of actual buffer sizes)
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_co_attr_get(daos_handle_t coh, int n, const char *const names[],
		void *bufs[], size_t *sizes[], daos_event_t *ev);

/**
 * Set a set of attributes.
 *
 * \param coh	[IN]	container handle
 * \param n	[IN]	number of attributes
 * \param names	[IN]	array of attribute names
 * \param values
 *		[IN]	array of attribute values
 * \param sizes	[IN]	array of value sizes
 * \param ev	[IN]	Completion event, it is optional and can be
 *				NULL. Function will run in blocking mode if
 *				\a ev is NULL.
 */
int
dsm_co_attr_set(daos_handle_t coh, int n, const char *const names[],
		const void *const values[], const size_t sizes[],
		daos_event_t *ev);

/**
 * Epoch API
 */

/**
 * Flush an epoch of a container handle.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to flush
 * \param state	[OUT]	latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_flush(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Flush an epoch of a container handle on a target.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to flush
 * \param tgt	[IN]	Target to flush.
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_flush_target(daos_handle_t coh, daos_epoch_t epoch, daos_rank_t tgt,
		       daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Discard an epoch of a container handle.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to discard
 * \param state	[OUT]	Optiona, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_discard(daos_handle_t coh, daos_epoch_t epoch,
		  daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Discard an epoch of a container handle on a target.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to discard
 * \param state	[OUT]	Opiontal, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_discard_target(daos_handle_t coh, daos_epoch_t epoch, daos_rank_t tgt,
			 daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Query latest epoch state.
 *
 * \param coh	[IN]	container handle
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_query(daos_handle_t coh, daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Propose a new lowest held epoch (LHE) of a container handle. The resulting
 * LHE may be higher than the one proposed. The owner of the container handle
 * is responsible for releasing its held epochs by either committing them or
 * setting LHE to DAOS_EPOCH_MAX.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	minimum requested LHE, set to 0 if no requirement
 *		[OUT]	returned LHE of the container handle
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_hold(daos_handle_t coh, daos_epoch_t *epoch,
	       daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Increase the lowest referenced epoch (LRE) of a container handle.
 * The resulting LRE' is determined like this:
 *
 *	LRE' = min(container HCE, max(LRE, epoch))
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to increase LRE to
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_slip(daos_handle_t coh, daos_epoch_t epoch, daos_epoch_state_t *state,
	       daos_event_t *ev);

/**
 * Commit to an epoch for a container handle. Unless already committed, in
 * which case the epoch state of the container handle is unchanged, epoch must
 * be equal to or higher than the LHE. Otherwise, an error is returned. Once
 * the commit succeeds, the HCE, LHE, and LRE (unless DAOS_COO_NOSLIP was
 * specified when opening this container handle) of the container handle
 * becomes epoch, epoch + 1, and epoch, respectively.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to commit
 * \param state	[OUT]	Opitional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_commit(daos_handle_t coh, daos_epoch_t epoch,
		 daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Wait for an epoch to be committed.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to commit
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_wait(daos_handle_t coh, daos_epoch_t epoch, daos_epoch_state_t *state,
	       daos_event_t *ev);

/**
 * Snapshot API
 */

/**
 * List epochs of all snapshot of a container.
 *
 * \param coh	[IN]	container coh
 * \param buf	[IN]	buffer to epochs
 *		[OUT]	array of epochs of snapshots
 * \param n	[IN]	number of epochs buffer can hold
 *		[OUT]	number of all snapshots (regardless of buffer size)
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_snap_list(daos_handle_t coh, daos_epoch_t *buf, int *n, daos_event_t *ev);

/**
 * Take a snapshot of an epoch.
 *
 * \param coh	[IN]	container coh
 * \param epoch	[IN]	epoch to snapshot
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_snap_create(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Destroy a snapshot. The epoch corresponding to the snapshot is not
 * discarded, but may be aggregated.
 *
 * \param coh	[IN]	container coh
 * \param epoch	[IN]	epoch to snapshot
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_snap_destroy(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Object API
 */

/**
 * Open an DAOS-M object.
 *
 * \param coh	[IN]	Container open handle.
 * \param tgt	[IN]	Target ID.
 * \param id	[IN]	Object ID.
 * \param mode	[IN]	Open mode: read-only, read-write.
 * \param oh	[OUT]	Returned object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Cannot find object
 *			-DER_EP_OLD	Epoch is too old and has no data for
 *					this object
 */
int
dsm_obj_open(daos_handle_t coh, uint32_t tgt, daos_unit_oid_t id,
	     unsigned int mode, daos_handle_t *oh, daos_event_t *ev);

/**
 * Close an opened object.
 *
 * \param oh	[IN]	Object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 */
int
dsm_obj_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Punch all records for all vectos in an object.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to punch records.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Permission denied
 *			-DER_NOEXIST	Nonexistent object ID
 */
int
dsm_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev);

#endif /* __DSM_API_H__ */
