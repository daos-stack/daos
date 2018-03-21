/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
 * DAOS APIs
 */

#ifndef __DAOS_API_H__
#define __DAOS_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Initialize the DAOS library.
 */
int
daos_init(void);

/**
 * Finalize the DAOS library.
 */
int
daos_fini(void);

/**
 * Connect to the DAOS pool identified by UUID \a uuid. Upon a successful
 * completion, \a poh returns the pool handle, and \a info returns the latest
 * pool information.
 *
 * \param uuid	[IN]	UUID to identify a pool.
 * \param grp	[IN]	Process set name of the DAOS servers managing the pool
 * \param svc	[IN]	Pool service replica ranks, as reported by
 *			daos_pool_create().
 * \param flags	[IN]	Connect mode represented by the DAOS_PC_ bits.
 * \param poh	[OUT]	Returned open handle.
 * \param info	[OUT]	Returned pool info.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
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
daos_pool_connect(const uuid_t uuid, const char *grp,
		  const d_rank_list_t *svc, unsigned int flags,
		  daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev);

/**
 * Disconnect from the DAOS pool. It should revoke all the container open
 * handles of this pool.
 *
 * \param poh	[IN]	Pool connection handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
daos_pool_disconnect(daos_handle_t poh, daos_event_t *ev);

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
 * \param poh	[IN]	Valid local pool connection handle to be shared
 * \param glob	[OUT]	Pointer to iov of the buffer to store handle information
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Pool  handle is nonexistent
 *			-DER_TRUNC	Buffer in \a glob is too short, a larger
 *					buffer is required. In this case the
 *					required buffer size is returned through
 *					glob->iov_buf_len.
 */
int
daos_pool_local2global(daos_handle_t poh, daos_iov_t *glob);

/**
 * Create a local pool connection for global representation data.
 *
 * \param glob	[IN]	Global (shared) representation of a collective handle
 *			to be extracted
 * \param poh	[OUT]	Returned local pool connection handle
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 */
int
daos_pool_global2local(daos_iov_t glob, daos_handle_t *poh);

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
daos_cont_local2global(daos_handle_t coh, daos_iov_t *glob);

/**
 * Create a local container handle for global representation data.
 *
 * \param poh	[IN]	Pool connection handle the container belong to
 * \param glob	[IN]	Global (shared) representation of a collective handle
 *			to be extracted
 * \param coh	[OUT]	Returned local container handle
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Pool handle is nonexistent
 */
int
daos_cont_global2local(daos_handle_t poh, daos_iov_t glob, daos_handle_t *coh);

/**
 * Query pool information. User should provide at least one of \a info and
 * \a tgts as output buffer.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param tgts	[OUT]	Optional, returned storage targets in this pool.
 * \param info	[OUT]	Optional, returned pool information.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid pool handle
 */
int
daos_pool_query(daos_handle_t poh, d_rank_list_t *tgts,
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
 *			The function will run in blocking mode if \a ev is NULL.
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
daos_pool_target_query(daos_handle_t poh, d_rank_list_t *tgts,
		       d_rank_list_t *failed, daos_target_info_t *info_list,
		       daos_event_t *ev);

/**
 * List the names of all user-defined pool attributes.
 *
 * \param poh	 [IN]	Pool handle.
 * \param buffer [OUT]	Buffer containing concatenation of all attribute
 *			names, each being null-terminated. No truncation is
 *			performed and only full names will be returned.
 *			NULL is permitted in which case only the aggregate
 *			size will be retrieved.
 * \param size	 [IN]	Buffer size.
 *		 [OUT]	Aggregate size of all attribute names (excluding
 *			terminating null characters), regardless of the
 *			actual buffer size.
 * \param ev	 [IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_attr_list(daos_handle_t poh, char *buffer, size_t *size,
		    daos_event_t *ev);

/**
 * Retrieve a list of user-defined pool attribute values.
 *
 * \param poh	  [IN]	Pool handle
 * \param n	  [IN]	Number of attributes
 * \param names	  [IN]	Array of \a n null-terminated attribute names.
 * \param buffers [OUT]	Array of \a n buffers to store attribute values.
 *			Attribute values larger than corresponding
 *			buffer sizes will be truncated. NULL values are
 *			permitted and will be treated identical to
 *			zero-length buffers, in which case only the
 *			sizes of attribute values will be retrieved.
 * \param sizes	  [IN]	Array of \a n buffer sizes.
 *		  [OUT]	Array of actual sizes of \a n attribute values,
 *			regardless of given buffer sizes.
 * \param ev	  [IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_attr_get(daos_handle_t poh, int n, char const *const names[],
		   void *const buffers[], size_t sizes[], daos_event_t *ev);

/**
 * Create or update a list of user-defined pool attributes.
 *
 * \param poh	  [IN]	Pool handle
 * \param n	  [IN]	Number of attributes
 * \param names	  [IN]	Array of \a n null-terminated attribute names.
 * \param values  [IN]	Array of \a n attribute values
 * \param sizes	  [IN]	Array of \a n elements containing the
 *			sizes of respective attribute values.
 * \param ev	  [IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_attr_set(daos_handle_t poh, int n, char const *const names[],
		   void const *const values[], size_t const sizes[],
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
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Invalid pool handle
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	network is unreachable
 */
int
daos_cont_create(daos_handle_t poh, const uuid_t uuid, daos_event_t *ev);

/**
 * Open an existing container identified by UUID \a uuid. Upon successful
 * completion, \a coh and \a info, both of which shall be allocated by the
 * caller, return the container handle and the latest container information
 * respectively. The resulting container handle has an HCE equal to GHCE, an
 * LHE equal to DAOS_EPOCH_MAX, and an LRE equal to GHCE.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param uuid	[IN]	UUID to identify container.
 * \param flags	[IN]	Open mode, represented by the DAOS_COO_ bits.
 * \param coh	[OUT]	Returned open handle.
 * \param info	[OUT]	Optional, return container information
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
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
daos_cont_open(daos_handle_t poh, const uuid_t uuid, unsigned int flags,
	       daos_handle_t *coh, daos_cont_info_t *info, daos_event_t *ev);

/**
 * Close a container handle. Upon successful completion, the container handle's
 * epoch hold (i.e., if LHE < DAOS_EPOCH_MAX) is released, and any uncommitted
 * updates from the container handle are discarded.
 *
 * \param coh	[IN]	Container open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid container handle
 */
int
daos_cont_close(daos_handle_t coh, daos_event_t *ev);

/**
 * Destroy a container identified by \a uuid, all objects within this
 * container will be destroyed as well.
 * If there is at least one container opener, and \a force is set to zero, then
 * the operation completes with DER_BUSY. Otherwise, the container is destroyed
 * when the operation completes.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param uuid	[IN]	Container UUID.
 * \param force	[IN]	Container destroy will return failure if the container
 *			is still busy (outstanding open handles). This parameter
 *			will force the destroy to proceed even if there is an
 *			outstanding open handle.
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
daos_cont_destroy(daos_handle_t poh, const uuid_t uuid, int force,
		  daos_event_t *ev);

/**
 * Query container information.
 *
 * \param coh	[IN]	Container open handle.
 * \param info	[OUT]	Returned container information.
 *			If \a info::ci_snapshots is not NULL, epochs of
 *			snapshots will be stored in it.
 *			If \a info::ci_snapshots is NULL, number of snapshots
 *			will be returned by \a info::ci_nsnapshots.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_HDL	Invalid container handle
 */
int
daos_cont_query(daos_handle_t container, daos_cont_info_t *info,
		daos_event_t *ev);

/**
 * List the names of all user-defined container attributes.
 *
 * \param coh	 [IN]	Container handle.
 * \param buffer [OUT]	Buffer containing concatenation of all attribute
 *			names, each being null-terminated. No truncation is
 *			performed and only full names will be returned.
 *			NULL is permitted in which case only the aggregate
 *			size will be retrieved.
 * \param size	 [IN]	Buffer size.
 *		 [OUT]	Aggregate size of all attribute names (excluding
 *			terminating null characters), regardless of the
 *			actual buffer size.
 * \param ev	 [IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_attr_list(daos_handle_t coh, char *buffer, size_t *size,
		    daos_event_t *ev);

/**
 * Retrieve a list of user-defined container attribute values.
 *
 * \param coh	  [IN]	Container handle
 * \param n	  [IN]	Number of attributes
 * \param names	  [IN]	Array of \a n null-terminated attribute names.
 * \param buffers [OUT]	Array of \a n buffers to store attribute values.
 *			Attribute values larger than corresponding
 *			buffer sizes will be truncated. NULL values are
 *			permitted and will be treated identical to
 *			zero-length buffers, in which case only the
 *			sizes of attribute values will be retrieved.
 * \param sizes	  [IN]	Array of \a n buffer sizes.
 *		  [OUT]	Array of actual sizes of \a n attribute values,
 *			regardless of given buffer sizes.
 * \param ev	  [IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_attr_get(daos_handle_t coh, int n, char const *const names[],
		   void *const buffers[], size_t sizes[], daos_event_t *ev);

/**
 * Create or update a list of user-defined container attributes.
 *
 * \param coh	  [IN]	Container handle
 * \param n	  [IN]	Number of attributes
 * \param names	  [IN]	Array of \a n null-terminated attribute names.
 * \param values  [IN]	Array of \a n attribute values
 * \param sizes	  [IN]	Array of \a n elements containing the
 *			sizes of respective attribute values.
 * \param ev	  [IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_attr_set(daos_handle_t coh, int n, char const *const names[],
		   void const *const values[], size_t const sizes[],
		   daos_event_t *ev);

/**
 * Allocate a unique set of 64 bit unsigned integers to be used for object ID
 * generation for that container. This is an optional helper function for
 * applications to use to guarantee unique object IDs on the container when more
 * than 1 client are accessing objects on the container. The highest used ID is
 * tracked in the container metadata for future access to that container. This
 * doesn't guarantee that the IDs allocated are sequential; and several ID
 * ranges could be discarded at container close.
 *
 * \param coh   [IN]    Container open handle.
 * \param num_oids [IN]	Number of unique IDs requested.
 * \param oid	[OUT]	starting oid that was allocated up to oid + num_oids.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container open handle
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_cont_oid_alloc(daos_handle_t coh, daos_size_t num_oids, uint64_t *oid,
		    daos_event_t *ev);

/**
 * Epoch API
 */

/**
 * Flush an epoch of a container handle.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Epoch to flush
 * \param state	[OUT]	Latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_epoch_flush(daos_handle_t coh, daos_epoch_t epoch,
		 daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Discard an epoch of a container handle.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Epoch to discard
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_epoch_discard(daos_handle_t coh, daos_epoch_t epoch,
		   daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Query latest epoch state.
 *
 * \param coh	[IN]	Container handle
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_epoch_query(daos_handle_t coh, daos_epoch_state_t *state,
		 daos_event_t *ev);

/**
 * Propose a new lowest held epoch (LHE) on a container handle. The resulting
 * LHE may be higher than the one proposed. The owner of the container handle
 * is responsible for releasing its held epochs by either committing them or
 * setting LHE to DAOS_EPOCH_MAX.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Minimum requested LHE, set to 0 if no requirement
 *		[OUT]	returned LHE of the container handle
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_epoch_hold(daos_handle_t coh, daos_epoch_t *epoch,
		daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Increase the lowest referenced epoch (LRE) of a container handle. If
 * \a epoch is lower than current LRE, then the epoch state of the container
 * handle is unchanged.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Epoch to increase LRE to
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_epoch_slip(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Commit to an epoch for a container handle. Unless already committed, in
 * which case the epoch state of the container handle is unchanged, epoch must
 * be equal to or higher than the LHE. Otherwise, an error is returned. Once
 * the commit succeeds, the HCE, LHE, and LRE (unless DAOS_COO_NOSLIP was
 * specified when opening this container handle) of the container handle
 * becomes epoch, epoch + 1, and epoch, respectively.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Epoch to commit
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_epoch_commit(daos_handle_t coh, daos_epoch_t epoch,
		  daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Wait for an epoch to be committed. This function is typically used by a
 * consumer application waiting for the producer to commit a specific epoch.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Epoch to wait on
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_epoch_wait(daos_handle_t coh, daos_epoch_t epoch,
		daos_epoch_state_t *state, daos_event_t *ev);

/**
 * Snapshot API
 */

/**
 * List epochs of all the snapshots of a container.
 *
 * \param coh	[IN]	Container handle
 * \param buf	[IN]	Buffer to hold the epochs
 *		[OUT]	Array of epochs of snapshots
 * \param n	[IN]	Number of epochs the buffer can hold
 *		[OUT]	Number of all snapshots (regardless of buffer size)
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_snap_list(daos_handle_t coh, daos_epoch_t *buf, int *n, daos_event_t *ev);

/**
 * Take a snapshot of a container at an epoch.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Epoch to snapshot
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_snap_create(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Destroy a snapshot. The epoch corresponding to the snapshot is not
 * discarded, but may be aggregated.
 *
 * \param coh	[IN]	Container handle
 * \param epoch	[IN]	Epoch of snapshot to destroy
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_snap_destroy(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Object API
 */

/**
 * Register a new object class in addition to the default ones (see DAOS_OC_*).
 * An object class cannot be unregistered for the time being.
 *
 * \param coh	[IN]	Container open handle.
 * \param cid	[IN]	ID for the new object class.
 * \param cattr	[IN]	Attributes for the new object class.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EXIST	Object class ID already existed
 */
int
daos_obj_class_register(daos_handle_t coh, daos_oclass_id_t cid,
			daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * Query attributes of an object class by its ID.
 *
 * \param coh	[IN]	Container open handle.
 * \param cid	[IN]	Class ID to query.
 * \param cattr	[OUT]	Returned attributes of the object class.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	Nonexistent class ID
 */
int
daos_obj_class_query(daos_handle_t coh, daos_oclass_id_t cid,
		     daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * List existing object classes.
 *
 * \param coh	[IN]	Container open handle.
 * \param clist	[OUT]	Sink buffer for returned class list.
 * \param anchor [IN/OUT]
 *			Hash anchor for the next call. It should be set to
 *			zeroes for the first call. It should not be altered
 *			by caller between calls.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_class_list(daos_handle_t coh, daos_oclass_list_t *clist,
		    daos_hash_out_t *anchor, daos_event_t *ev);

/**
 * Generate a DAOS object ID by encoding the private DAOS bits of the object
 * address space.
 *
 * \param oid	[IN]	Object ID with low 160 bits set and unique inside the
 *			container.
 *		[OUT]	Fully populated DAOS object identifier with the the low
 *			96 bits untouched and the DAOS private	bits (the high
 *			32 bits) encoded.
 * \param ofeat	[IN]	Feature bits specific to object
 * \param cid	[IN]	Class Identifier
 */
static inline void
daos_obj_id_generate(daos_obj_id_t *oid, daos_ofeat_t ofeats,
		     daos_oclass_id_t cid)
{
	uint64_t hdr = cid;

	oid->hi &= 0x00000000ffffffff;
	/**
	 * | Upper bits contain
	 * | DAOS_OVERSION_BITS version        |
	 * | DAOS_OFEAT_BITS object features   |
	 * | DAOS_OCLASS_BITS object class     |
	 * | 96-bit for upper layer ...        |
	 */
	hdr <<= DAOS_OCLASS_SHIFT;
	hdr |= 0x1ULL << DAOS_OVERSION_SHIFT;
	hdr |= ((uint64_t)ofeats << DAOS_OFEAT_SHIFT);
	oid->hi |= hdr;
}

static inline daos_oclass_id_t
daos_obj_id2class(daos_obj_id_t oid)
{
	daos_oclass_id_t ocid;

	ocid = (oid.hi & DAOS_OCLASS_MASK) >> DAOS_OCLASS_SHIFT;
	return ocid;
}

static inline daos_ofeat_t
daos_obj_id2feat(daos_obj_id_t oid)
{
	daos_ofeat_t ofeat;

	ofeat = (oid.hi & DAOS_OFEAT_MASK) >> DAOS_OFEAT_SHIFT;
	return ofeat;
}

static inline uint8_t
daos_obj_id2version(daos_obj_id_t oid)
{
	uint8_t version;

	version = (oid.hi & DAOS_OVERSION_MASK) >> DAOS_OVERSION_SHIFT;
	return version;
}

/**
 * Declare a new object based on attributes \a oa.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID generated by daos_objid_generate().
 * \param epoch	[IN]	Epoch to create object.
 * \param oa	[IN]	Optional, object creation parameters.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NO_PERM	Permission denied
 *			-DER_EXIST	Object ID has been used for another
 *					object.
 *			-DER_NONEXIST	Cannot find container on specified
 *					storage target
 *			-DER_NOTYPE	Unknown object type
 *			-DER_NOSCHEMA	Unknown object schema
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		 daos_obj_attr_t *oa, daos_event_t *ev);

/**
 * Open an declared DAOS-SR object.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to open object.
 * \param mode	[IN]	Open mode: DAOS_OO_RO/RW/EXCL/IO_RAND/IO_SEQ
 * \param oh	[OUT]	Returned object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
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
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
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
daos_obj_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Punch an entire object with all keys associated with it.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to punch the object in.
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
daos_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Punch dkeys (with all akeys) from an object.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to punch records.
 * \param nr	[IN]	number of dkeys to punch.
 * \param dkeys	[IN]	Array of dkeys to punch.
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
daos_obj_punch_dkeys(daos_handle_t oh, daos_epoch_t epoch, unsigned int nr,
		     daos_key_t *dkeys, daos_event_t *ev);

/**
 * Punch akeys (with all records) from an object.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to punch records.
 * \param dkey	[IN]	dkey to punch akeys from.
 * \param nr	[IN]	number of akeys to punch.
 * \param akeys	[IN]	Array of akeys to punch.
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
daos_obj_punch_akeys(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		     unsigned int nr, daos_key_t *akeys, daos_event_t *ev);

/**
 * Query attributes of an object.
 * Caller should provide at least one of the output parameters.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to query.
 * \param oa	[OUT]	Returned object attributes.
 * \param ranks	[OUT]	Ordered list of ranks where the object is stored.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	       d_rank_list_t *ranks, daos_event_t *ev);

/**
 * Object I/O API
 */

/**
 * Fetch object records from co-located arrays.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the fetch. It is ignored if epoch range is
 *			provided for each extent through the I/O descriptor
 *			\a iods[]::iod_eprs[]).
 *
 * \param dkey	[IN]	Distribution key associated with the fetch operation.
 *
 * \param nr	[IN]	Number of I/O descriptor and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param iods	[IN]	Array of I/O descriptors. Each descriptor is associated
 *			with a given akey and describes the list of record
 *			extents to fetch from the array.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::iod_eprs[] and in this case, \a epoch will be
 *			ignored.
 *		[OUT]	Checksum of each extent is returned via
 *			\a iods[]::iod_csums[]. If the record size of an
 *			extent is unknown (i.e. set to DAOS_REC_ANY as input),
 *			then the actual record size will be returned in
 *			\a iods[]::iod_size.
 *
 * \param sgls	[IN]	Scatter/gather lists (sgl) to store records. Each array
 *			is associated with a separate sgl in \a sgls.
 *			I/O descs in each sgl can be arbitrary as long as their
 *			total size is sufficient to fill in all returned data.
 *			For example, extents with records of different sizes can
 *			be adjacently stored in the same iod of the sgl of the
 *			I/O descriptor start offset of an extent is the end
 *			offset of the previous extent.
 *			For an unfound record, the output length of the
 *			corresponding sgl is set to zero.
 *
 * \param map	[OUT]	Optional, this parameter is mostly for the cache and
 *			tiering layer, other upper layers can simply pass in
 *			NULL.
 *			It is the sink buffer to store the returned actual
 *			index layouts and their epoch validities. The returned
 *			layout covers the record extents as \a iods.
 *			However, the returned extents could be fragmented if
 *			these extents were partially updated in	different
 *			epochs.	Additionally, the returned extents should also
 *			allow to discriminate punched extents from punched
 *			holes.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be
 *					fit into output buffer
 *			-DER_REC2BIG	Record is too large and can't be
 *					fit into output buffer
 *			-DER_EP_OLD	Epoch is too old and has no data
 */
int
daos_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	       unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	       daos_iom_t *maps, daos_event_t *ev);

/**
 * Insert or update object records stored in co-located arrays.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the I/O descriptor
 *			\a iods[]::iod_eprs[]).
 *
 * \param dkey	[IN]	Distribution key associated with the update operation.
 *
 * \param nr	[IN]	Number of descriptors and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param iods	[IN]	Array of I/O descriptor. Each descriptor is associated
 *			with an array identified by its akey and describes the
 *			list of record extent to update.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::iod_eprs[] and in this case, \a epoch will be
 *			ignored.
 *			Checksum of each record extent is stored in
 *			\a iods[]::iod_csums[]. If the record size of an extent
 *			is zero, then it is effectively a punch	for the
 *			specified index range.
 *
 * \param sgls	[IN]	Scatter/gather list (sgl) to store the input data
 *			records. Each I/O descriptor owns a separate sgl in
 *			\a sgls.
 *			Different records of the same extent can either be
 *			stored in separate iod of the sgl, or contiguously
 *			stored in arbitrary iods as long as total buffer size
 *			can match the total extent size.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		daos_event_t *ev);

/**
 * Distribution key enumeration.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the enumeration.
 *
 * \param nr	[IN]    number of key descriptors in \a kds
 *		[OUT]   number of returned key descriptors.
 *
 * \param kds	[IN]	preallocated array of \nr key descriptors.
 *		[OUT]	size of each individual key along with checksum type
 *			and size stored just after the key in \a sgl.
 *
 * \param sgl	[IN]	Scatter/gather list to store the dkey list.
 *			All dkeys are written contiguously with their checksum,
 *			actual boundaries can be calculated thanks to \a kds.
 *
 * \param anchor [IN/OUT]
 *			Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be
 *					fit into the \a sgl
 */
int
daos_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		   daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev);

/**
 * Attribute key enumeration.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the enumeration.
 *
 * \param dkey	[IN]	distribution key for the akey enumeration
 *
 * \param nr	[IN]	number of key descriptors in \a kds
 *		[OUT]	number of returned key descriptors.
 *
 * \param kds	[IN]	preallocated array of \nr key descriptors.
 *		[OUT]	size of each individual key along with checksum type,
 *			size, and type stored just after the key in \a sgl.
 *
 * \param sgl	[IN]	Scatter/gather list to store the akey list.
 *			All akeys are written contiguously with their checksum,
 *			actual boundaries can be calculated thanks to \a kds.
 *
 * \param anchor [IN/OUT]
 *			Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be
 *					fit into the \a sgl
 */
int
daos_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev);

/**
 * Extent enumeration of valid records in the array.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the enumeration.
 *
 * \param dkey	[IN]	distribution key for the enumeration
 *
 * \param akey	[IN]	attribute key for the enumeration
 *
 * \param size	[OUT]	record size
 *
 * \param nr	[IN]	number of records in \a recxs
 *		[OUT]	number of returned recxs.
 *
 * \param recxs	[IN]	preallocated array of \nr records.
 *		[OUT]	returned records.
 *
 * \param eprs	[IN]	preallocated array of \nr epoch ranges.
 *		[OUT]	returned epoch ranges.
 *
 * \param anchor [IN/OUT]
 *			Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param incr_order [IN]
 *			If this is set to true, extents will be listed in
 *			increasing index order, otherwise if false, they are
 *			listed in decreasing order. Once an anchor is associated
 *			with an order, further calls with that anchor should use
 *			the same order setting.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_obj_list_recx(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs,
		   daos_hash_out_t *anchor, bool incr_order,
		   daos_event_t *ev);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_API_H__ */
