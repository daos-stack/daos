/*
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
 * \file
 *
 * DAOS API methods
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
 * \param[in]	uuid	UUID to identify a pool.
 * \param[in]	grp	Process set name of the DAOS servers managing the pool
 * \param[in]	svc	Pool service replica ranks, as reported by
 *			daos_pool_create().
 * \param[in]	flags	Connect mode represented by the DAOS_PC_ bits.
 * \param[out]	poh	Returned open handle.
 * \param[out]	info	Returned pool info.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
 * \param[in]	poh	Pool connection handle
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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

/*
 * Handle API
 */

/**
 * Convert a local pool connection to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	poh	Valid local pool connection handle to be shared
 * \param[out]	glob	Pointer to iov of the buffer to store handle information
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
 * \param[in]	glob	Global (shared) representation of a collective handle
 *			to be extracted
 * \param[out]	poh	Returned local pool connection handle
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
 * \param[in]	coh	valid local container handle to be shared
 * \param[out]	glob	pointer to iov of the buffer to store handle information
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
 * \param[in]	poh	Pool connection handle the container belong to
 * \param[in]	glob	Global (shared) representation of a collective handle
 *			to be extracted
 * \param[out]	coh	Returned local container handle
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
 * \param[in]	poh	Pool connection handle.
 * \param[out]	tgts	Optional, returned storage targets in this pool.
 * \param[out]	info	Optional, returned pool information.
 * \param[out]	pool_prop
 *			Optional, returned pool properties.
 *			If it is NULL, then needs not query the properties.
 *			If pool_prop is non-NULL but its dpp_entries is NULL,
 *			will query all pool properties, DAOS internally
 *			allocates the needed buffers and assign pointer to
 *			dpp_entries.
 *			If pool_prop's dpp_nr > 0 and dpp_entries is non-NULL,
 *			will query the properties for specific dpe_type(s), DAOS
 *			internally allocates the needed buffer for dpe_str or
 *			dpe_val_ptr, if the dpe_type with immediate value then
 *			will directly assign it to dpe_val.
 *			User can free the associated buffer by calling
 *			daos_prop_free().
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_pool_query(daos_handle_t poh, d_rank_list_t *tgts, daos_pool_info_t *info,
		daos_prop_t *pool_prop, daos_event_t *ev);

/**
 * Allocate DAOS properties.
 *
 * \param[in]	entries_nr	number of entries
 *
 * \return	allocated daos_prop_t pointer, NULL if failed.
 */
daos_prop_t *
daos_prop_alloc(uint32_t entries_nr);

/**
 * Free the DAOS properties.
 *
 * \param[in]	prop	properties to be freed.
 */
void
daos_prop_free(daos_prop_t *prop);

/**
 * Query information of storage targets within a DAOS pool.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	tgts	A list of targets to query.
 * \param[out]	failed	Optional, buffer to store faulty targets on failure.
 * \param[out]	info_list
 *			Returned storage information of \a tgts, it is an array
 *			and array size must equal to tgts::rl_llen.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_pool_query_target(daos_handle_t poh, d_rank_list_t *tgts,
		       d_rank_list_t *failed, daos_target_info_t *info_list,
		       daos_event_t *ev);

/**
 * List the names of all user-defined pool attributes.
 *
 * \param[in]	poh	Pool handle.
 * \param[out]	buffer	Buffer containing concatenation of all attribute
 *			names, each being null-terminated. No truncation is
 *			performed and only full names will be returned.
 *			NULL is permitted in which case only the aggregate
 *			size will be retrieved.
 * \param[in,out]
 *		size	[in]: Buffer size. [out]: Aggregate size of all
 *			attribute names (excluding terminating null
 *			characters), regardless of the actual buffer
 *			size.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_list_attr(daos_handle_t poh, char *buffer, size_t *size,
		    daos_event_t *ev);

/**
 * Retrieve a list of user-defined pool attribute values.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[out]	buffers	Array of \a n buffers to store attribute values.
 *			Attribute values larger than corresponding buffer sizes
 *			will be truncated. NULL values are permitted and will be
 *			treated identical to zero-length buffers, in which case
 *			only the sizes of attribute values will be retrieved.
 * \param[in,out]
 *		sizes	[in]: Array of \a n buffer sizes. [out]: Array of actual
 *			sizes of \a n attribute values, regardless of given
 *			buffer sizes.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_get_attr(daos_handle_t poh, int n, char const *const names[],
		   void *const buffers[], size_t sizes[], daos_event_t *ev);

/**
 * Create or update a list of user-defined pool attributes.
 *
 * \param[in]	poh	Pool handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[in]	values	Array of \a n attribute values
 * \param[in]	sizes	Array of \a n elements containing the sizes of
 *			respective attribute values.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_pool_set_attr(daos_handle_t poh, int n, char const *const names[],
		   void const *const values[], size_t const sizes[],
		   daos_event_t *ev);

/*
 * Container API
 */

/**
 * Create a new container with uuid \a uuid on the storage pool connected
 * by \a poh.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	uuid	UUID of the new Container.
 * \param[in]	cont_prop
 *			Optional, container properties pointer
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_cont_create(daos_handle_t poh, const uuid_t uuid, daos_prop_t *cont_prop,
		 daos_event_t *ev);

/**
 * Open an existing container identified by UUID \a uuid. Upon successful
 * completion, \a coh and \a info, both of which shall be allocated by the
 * caller, return the container handle and the latest container information
 * respectively. The resulting container handle has an HCE equal to GHCE, an
 * LHE equal to DAOS_EPOCH_MAX, and an LRE equal to GHCE.
 *
 * \param[in]	poh	Pool connection handle.
 * \param[in]	uuid	UUID to identify container.
 * \param[in]	flags	Open mode, represented by the DAOS_COO_ bits.
 * \param[out]	coh	Returned open handle.
 * \param[out]	info	Optional, return container information
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
 * \param[in]	coh	Container open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
 * \param[in]	poh	Pool connection handle.
 * \param[in]	uuid	Container UUID.
 * \param[in]	force	Container destroy will return failure if the container
 *			is still busy (outstanding open handles). This parameter
 *			will force the destroy to proceed even if there is an
 *			outstanding open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
 * \param[in]	coh	Container open handle.
 * \param[out]	info	Returned container information.
 *			If \a info::ci_snapshots is not NULL, epochs of
 *			snapshots will be stored in it.
 *			If \a info::ci_snapshots is NULL, number of snapshots
 *			will be returned by \a info::ci_nsnapshots.
 * \param[out]	cont_prop
 *			Optional, returned container properties
 *			If it is NULL, then needs not query the properties.
 *			If cont_prop is non-NULL but its dpp_entries is NULL,
 *			will query all pool properties, DAOS internally
 *			allocates the needed buffers and assign pointer to
 *			dpp_entries.
 *			If cont_prop's dpp_nr > 0 and dpp_entries is non-NULL,
 *			will query the properties for specific dpe_type(s), DAOS
 *			internally allocates the needed buffer for dpe_str or
 *			dpe_val_ptr, if the dpe_type with immediate value then
 *			will directly assign it to dpe_val.
 *			User can free the associated buffer by calling
 *			daos_prop_free().
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
		daos_prop_t *cont_prop, daos_event_t *ev);

/**
 * List the names of all user-defined container attributes.
 *
 * \param[in]	coh	Container handle.
 * \param[out]	buffer	Buffer containing concatenation of all attribute
 *			names, each being null-terminated. No truncation is
 *			performed and only full names will be returned.
 *			NULL is permitted in which case only the aggregate
 *			size will be retrieved.
 * \param[in,out]
 *		size	[in]: Buffer size. [out]: Aggregate size of all
 *			attribute names (excluding terminating null characters),
 *			regardless of the actual buffer size.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_list_attr(daos_handle_t coh, char *buffer, size_t *size,
		    daos_event_t *ev);

/**
 * Retrieve a list of user-defined container attribute values.
 *
 * \param[in]	coh	Container handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[out]	buffers	Array of \a n buffers to store attribute values.
 *			Attribute values larger than corresponding buffer sizes
 *			will be truncated. NULL values are permitted and will be
 *			treated identical to zero-length buffers, in which case
 *			only the sizes of attribute values will be retrieved.
 * \param[in,out]
 *		sizes	[in]: Array of \a n buffer sizes. [out]: Array of actual
 *			sizes of \a n attribute values, regardless of given
 *			buffer sizes.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_get_attr(daos_handle_t coh, int n, char const *const names[],
		   void *const buffers[], size_t sizes[], daos_event_t *ev);

/**
 * Create or update a list of user-defined container attributes.
 *
 * \param[in]	coh	Container handle
 * \param[in]	n	Number of attributes
 * \param[in]	names	Array of \a n null-terminated attribute names.
 * \param[in]	values	Array of \a n attribute values
 * \param[in]	sizes	Array of \a n elements containing the sizes of
 *			respective attribute values.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_set_attr(daos_handle_t coh, int n, char const *const names[],
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
 * \param[in]	coh	Container open handle.
 * \param[in]	num_oids
 *			Number of unique IDs requested.
 * \param[out]	oid	starting oid that was allocated up to oid + num_oids.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container open handle
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_cont_alloc_oids(daos_handle_t coh, daos_size_t num_oids, uint64_t *oid,
		     daos_event_t *ev);

/**
 * Rollback to a specific persistent snapshot.
 *
 * \param[in]	coh	Container handle
 * \param[in]	epoch	Epoch if persistent snapshot to rollback to.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_rollback(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Subscribe to the container snapshot state. If user specifies a valid epoch,
 * the call will return once a persistent snapshot has been taken at that epoch
 * or a greater one. The epoch value will be updated with that epoch. If
 * multiple snapshots exist at an epoch greater than the one specified, the
 * lowest one will be returned in the epoch value. If the epoch value passed in
 * is 0, this call will return the lowest persistent snapshot on the container,
 * if any exist, otherwise will just wait till a persistent snapshot is created.
 *
 * \param[in]	coh	Container handle
 * \param[in,out]
 *		epoch	[in]: Epoch of snapshot to wait for. [out]: epoch of
 *			persistent snapshot that was taken.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_subscribe(daos_handle_t coh, daos_epoch_t *epoch, daos_event_t *ev);

#define DAOS_SNAPSHOT_MAX_LEN 128

/**
 * Create a persistent snapshot at the current epoch and return it. The epoch
 * that is returned can be used to create a read only transaction to read data
 * from that persistent snapshot. Optionally the snapshot can be given a name as
 * an attribute which can be retrieved with daos_cont_list_snap(). Name length
 * can't exceed DAOS_SNAPSHOT_MAX_LEN.
 *
 * \param[in]	coh	Container handle
 * \param[out]	epoch	returned epoch of persistent snapshot taken.
 * \param[in]	name	Optional null terminated name for snapshot.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_create_snap(daos_handle_t coh, daos_epoch_t *epoch, char *name,
		      daos_event_t *ev);

/**
 * List all the snapshots of a container and optionally retrieve the snapshot
 * name of each one if it was given at create time.
 *
 * \param[in]	coh	Container handle
 * \param[in,out]
 *		nr	[in]: Number of snapshots in epochs and names.
 *			[out]: Actual number of snapshots returned.
 * \param[out]	epochs	preallocated array of epochs to store snapshots.
 * \param[out]	names	preallocated array of names of the snapshots.
 *			DAOS_SNAPSHOT_MAX_LEN can be used for each name
 *			size if not known.
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_list_snap(daos_handle_t coh, int *nr, daos_epoch_t *epochs,
		    char **names, daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Destroy a snapshot. The epoch corresponding to the snapshot is not
 * discarded, but may be aggregated.
 *
 * \param[in]	coh	Container handle
 * \param[in]	epr	Epoch range of snapshots to destroy.
 *			If epr_lo == epr_hi delete 1 snapshot at epr_lo/hi.
 *			If epr_lo == 0, delete all snapshots <= epr_hi.
 *			If epr_hi == DAOS_EPOCH_MAX, delete all snapshots
 *			>= epr_lo.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 */
int
daos_cont_destroy_snap(daos_handle_t coh, daos_epoch_range_t epr,
		       daos_event_t *ev);

/*
 * Transaction API
 */

/**
 * Open a transaction on a container handle. This returns a transaction handle
 * that is tagged with the current epoch. The transaction handle can be used
 * for IOs that need to be committed transactionally.
 *
 * \param[in]	coh	Container handle.
 * \param[out]	th	Returned transaction handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_open(daos_handle_t coh, daos_handle_t *th, daos_event_t *ev);

/**
 * Commit the transaction on the container it was created with. The transaction
 * can't be used for future updates anymore. If -DER_RESTART was returned, the
 * operations that have been done on this transaction need to be redone with a
 * newer transaction since a conflict was detected with another transaction.
 *
 * \param[in]	th	Transaction handle to commit.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 *			Possible error values include:
 *			-DER_NO_HDL     invalid transaction handle.
 *			-DER_INVAL      Invalid parameter
 *			-DER_RESTART	transaction conflict detected.
 */
int
daos_tx_commit(daos_handle_t th, daos_event_t *ev);

/**
 * Create a read-only transaction from a snapshot. This does not create the
 * snapshot, but only a read transaction to be able to read from a persistent
 * snapshot in the container. If the user passes an epoch that is not
 * snapshoted, or the snapshot was deleted, reads using that transaction might
 * fail if the epoch was aggregated.
 *
 * \param[in]	coh	Container handle.
 * \param[in]	epoch	Epoch of snapshot to read from.
 * \param[out]	th	Returned read only transaction handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_open_snap(daos_handle_t coh, daos_epoch_t epoch, daos_handle_t *th,
		  daos_event_t *ev);

/**
 * Abort all updates on the transaction. The transaction can't be used for
 * future updates anymore.
 *
 * \param[in]	th	Transaction handle to abort.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_abort(daos_handle_t th, daos_event_t *ev);

/**
 * Close and free the transaction handle. This is a local operation, no RPC
 * involved.
 *
 * \param[in]	th	Transaction handle to free.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_close(daos_handle_t th, daos_event_t *ev);

/**
 * Return epoch associated with the transaction handle.
 *
 * \param[in]	th	Transaction handle.
 * \param[out]	th	Returned epoch value.
 *
 * \return		0 if Success, negative if failed.
 */
int
daos_tx_hdl2epoch(daos_handle_t th, daos_epoch_t *epoch);

/*
 * Object API
 */

/**
 * Register a new object class in addition to the default ones (see DAOS_OC_*).
 * An object class cannot be unregistered for the time being.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	ID for the new object class.
 * \param[in]	cattr	Attributes for the new object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_register_class(daos_handle_t coh, daos_oclass_id_t cid,
			daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * Query attributes of an object class by its ID.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	cid	Class ID to query.
 * \param[out]	cattr	Returned attributes of the object class.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_query_class(daos_handle_t coh, daos_oclass_id_t cid,
		     daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * List existing object classes.
 *
 * \param[in]	coh	Container open handle.
 * \param[out]	clist	Sink buffer for returned class list.
 * \param[in,out]
 *		anchor	Hash anchor for the next call. It should be set to
 *			zeroes for the first call. It should not be altered
 *			by caller between calls.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_list_class(daos_handle_t coh, daos_oclass_list_t *clist,
		    daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Generate a DAOS object ID by encoding the private DAOS bits of the object
 * address space.
 *
 * \param[in,out]
 *		oid	[in]: Object ID with low 96 bits set and unique inside
 *			the container. [out]: Fully populated DAOS object
 *			identifier with the the low 96 bits untouched and the
 *			DAOS private bits (the high 32 bits) encoded.
 * \param[in]	ofeat	Feature bits specific to object
 * \param[in]	cid	Class Identifier
 */
static inline void
daos_obj_generate_id(daos_obj_id_t *oid, daos_ofeat_t ofeats,
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

/**
 * Generate a rank list from a string with a seprator argument. This is a
 * convenience function to generate the rank list required by
 * daos_pool_connect().
 *
 * \param[in]	str	string with the rank list
 * \param[in]	sep	separator of the ranks in \a str.
 *			dmg uses ":" as the separator.
 *
 * \return		allocated rank list that user is responsible to free
 *			with daos_rank_list_free().
 */
d_rank_list_t *
daos_rank_list_parse(const char *str, const char *sep);

static inline daos_oclass_id_t
daos_obj_id2class(daos_obj_id_t oid)
{
	daos_oclass_id_t ocid;

	ocid = (oid.hi & DAOS_OCLASS_MASK) >> DAOS_OCLASS_SHIFT;
	return ocid;
}

static inline bool
daos_oc_echo_type(daos_oclass_id_t oc)
{
	return oc == DAOS_OC_ECHO_TINY_RW ||
	       oc == DAOS_OC_ECHO_R2S_RW ||
	       oc == DAOS_OC_ECHO_R3S_RW ||
	       oc == DAOS_OC_ECHO_R4S_RW;
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
 * Open an DAOS object.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW/EXCL/IO_RAND/IO_SEQ
 * \param[out]	oh	Returned object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	      daos_handle_t *oh, daos_event_t *ev);

/**
 * Close an opened object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch object in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_punch(daos_handle_t oh, daos_handle_t th, daos_event_t *ev);

/**
 * Punch dkeys (with all akeys) from an object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch dkeys in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	nr	number of dkeys to punch.
 * \param[in]	dkeys	Array of dkeys to punch.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_punch_dkeys(daos_handle_t oh, daos_handle_t th, unsigned int nr,
		     daos_key_t *dkeys, daos_event_t *ev);

/**
 * Punch akeys (with all records) from an object.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to punch akeys in.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	dkey	dkey to punch akeys from.
 * \param[in]	nr	number of akeys to punch.
 * \param[in]	akeys	Array of akeys to punch.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_punch_akeys(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		     unsigned int nr, daos_key_t *akeys, daos_event_t *ev);

/**
 * Query attributes of an object.
 * Caller should provide at least one of the output parameters.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to query with.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[out]	oa	Returned object attributes.
 * \param[out]	ranks	Ordered list of ranks where the object is stored.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_query(daos_handle_t oh, daos_handle_t th, daos_obj_attr_t *oa,
	       d_rank_list_t *ranks, daos_event_t *ev);

/*
 * Object I/O API
 */

/**
 * Fetch object records from co-located arrays.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to fetch with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	Distribution key associated with the fetch operation.
 *
 * \param[in]	nr	Number of I/O descriptor and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param[in,out]
 *		iods	[in]: Array of I/O descriptors. Each descriptor is
 *			associated with a given akey and describes the list of
 *			record extents to fetch from the array.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::iod_eprs[] and in this case, \a epoch will be
 *			ignored. [out]: Checksum of each extent is returned via
 *			\a iods[]::iod_csums[]. If the record size of an
 *			extent is unknown (i.e. set to DAOS_REC_ANY as input),
 *			then the actual record size will be returned in
 *			\a iods[]::iod_size.
 *
 * \param[in]	sgls	Scatter/gather lists (sgl) to store records. Each array
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
 * \param[out]	map	Optional, upper layers can simply pass in NULL.
 *			It is the sink buffer to store the returned actual
 *			index layouts and their epoch validities. The returned
 *			layout covers the record extents as \a iods.
 *			However, the returned extents could be fragmented if
 *			these extents were partially updated in different
 *			epochs. Additionally, the returned extents should also
 *			allow to discriminate punched extents from punched
 *			holes.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_REC2BIG	Record is too large and can't be
 *					fit into output buffer
 *			-DER_EP_OLD	Epoch is too old and has no data
 */
int
daos_obj_fetch(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
	       unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	       daos_iom_t *maps, daos_event_t *ev);

/**
 * Insert or update object records stored in co-located arrays.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to update with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	Distribution key associated with the update operation.
 *
 * \param[in]	nr	Number of descriptors and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param[in]	iods	Array of I/O descriptor. Each descriptor is associated
 *			with an array identified by its akey and describes the
 *			list of record extent to update.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::iod_eprs[] and in this case, \a epoch will be
 *			ignored.
 *			Checksum of each record extent is stored in
 *			\a iods[]::iod_csums[]. If the record size of an extent
 *			is zero, then it is effectively a punch for the
 *			specified index range.
 *
 * \param[in]	sgls	Scatter/gather list (sgl) to store the input data
 *			records. Each I/O descriptor owns a separate sgl in
 *			\a sgls.
 *			Different records of the same extent can either be
 *			stored in separate iod of the sgl, or contiguously
 *			stored in arbitrary iods as long as total buffer size
 *			can match the total extent size.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_REC2BIG	Record is larger than the buffer in
 *					input \a sgls buffer.
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
daos_obj_update(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		daos_event_t *ev);

/**
 * Distribution key enumeration.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to enumerate with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in,out]
 *		nr	[in]: number of key descriptors in \a kds. [out]: number
 *			of returned key descriptors.
 *
 * \param[in,out]
 *		kds	[in]: preallocated array of \nr key descriptors. [out]:
 *			size of each individual key along with checksum type
 *			and size stored just after the key in \a sgl.
 *
 * \param[in]	sgl	Scatter/gather list to store the dkey list.
 *			All dkeys are written contiguously with their checksum,
 *			actual boundaries can be calculated thanks to \a kds.
 *
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be fit into
 *					the \a sgl, the required minimal length
 *					to fit the key is returned by
 *					\a kds[0].kd_key_len. This error code
 *					only returned for the first key in this
 *					enumeration, then user can provide a
 *					larger buffer (for example two or three
 *					times \a kds[0].kd_key_len) and do the
 *					enumerate again.
 */
int
daos_obj_list_dkey(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
		   daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Attribute key enumeration.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to enumerate with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	distribution key for the akey enumeration
 *
 * \param[in,out]
 *		nr	[in]: number of key descriptors in \a kds. [out]: number
 *			of returned key descriptors.
 *
 * \param[in,out]
 *		kds	[in]: preallocated array of \nr key descriptors. [out]:
 *			size of each individual key along with checksum type,
 *			size, and type stored just after the key in \a sgl.
 *
 * \param[in]	sgl	Scatter/gather list to store the akey list.
 *			All akeys are written contiguously with their checksum,
 *			actual boundaries can be calculated thanks to \a kds.
 *
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_KEY2BIG	Key is too large and can't be fit into
 *					the \a sgl, the required minimal length
 *					to fit the key is returned by
 *					\a kds[0].kd_key_len. This error code
 *					only returned for the first key in this
 *					enumeration, then user can provide a
 *					larger buffer (for example two or three
 *					times \a kds[0].kd_key_len) and do the
 *					enumerate again.
 */
int
daos_obj_list_akey(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev);

/**
 * Extent enumeration of valid records in the array.
 *
 * \param[in]	oh	Object open handle.
 *
 * \param[in]	th	Optional transaction handle to enumerate with.
 *			Use DAOS_TX_NONE for an independent transaction.
 *
 * \param[in]	dkey	distribution key for the enumeration
 *
 * \param[in]	akey	attribute key for the enumeration
 *
 * \param[out]	size	record size
 *
 * \param[in,out]
 *		nr	[in]: number of records in \a recxs. [out]: number of
 *			returned recxs.
 *
 * \param[in,out]
 *		recxs	[in]: preallocated array of \nr records. [out]: returned
 *			records.
 *
 * \param[in,out]
 *		eprs	[in]: preallocated array of \nr epoch ranges. [out]:
 *			returned epoch ranges.
 *
 * \param[in,out]
 *		anchor	Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be changed
 *			by caller between calls.
 *
 * \param[in]	incr_order
 *			If this is set to true, extents will be listed in
 *			increasing index order, otherwise if false, they are
 *			listed in decreasing order. Once an anchor is associated
 *			with an order, further calls with that anchor should use
 *			the same order setting.
 *
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_list_recx(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs,
		   daos_anchor_t *anchor, bool incr_order,
		   daos_event_t *ev);

/**
 * Retrieve the largest or smallest integer DKEY, AKEY, and array offset from an
 * object. If object does not have an array value, 0 is returned in extent. User
 * has to specify what is being queried (dkey, akey, and/or recx) along with the
 * query type (max or min) in flags. If one of those is not provided the
 * function will fail. If the dkey or akey are not being queried, there value
 * must be provided by the user.
 *
 * If searching in a particular dkey for the max akey and max offset in that
 * akey, user would supply the dkey value and a flag of: DAOS_GET_MAX |
 * DAOS_GET_AKEY | DAOS_GET_RECX.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	th	Optional transaction handle to query at.
 *			Use DAOS_TX_NONE for an independent transaction.
 * \param[in]	flags	mask with the following options:
 *			DAOS_GET_DKEY, DAOS_GET_AKEY, DAOS_GET_RECX,
 *			DAOS_GET_MAX, DAOS_GET_MIN
 *			User has to indicate whether to query the MAX or MIN, in
 *			addition to what needs to be queried. Providing
 *			(MAX | MIN) in any combination will return an error.
 *			i.e. user can only query MAX or MIN in one call.
 * \param[in,out]
 *		dkey	[in]: allocated integer dkey. User can provide the dkey
 *			if not querying the max or min dkey.
 *			[out]: max or min dkey (if flag includes dkey query).
 * \param[in,out]
 *		akey	[in]: allocated integer akey. User can provide the akey
 *			if not querying the max or min akey.
 *			[out]: max or min akey (if flag includes akey query).
 * \param[out]	recx	max or min offset in dkey/akey, and the size of the
 *			extent at the offset. If there are no visible array
 *			records, the size in the recx returned will be 0.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_query_key(daos_handle_t oh, daos_handle_t th, uint32_t flags,
		   daos_key_t *dkey, daos_key_t *akey, daos_recx_t *recx,
		   daos_event_t *ev);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_API_H__ */
