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
 *
 * A pool handle is required to create, open, and destroy containers (belonging
 * to the pool). Operations requiring container handles do not ask for pool
 * handles at the same time, for the pool handles can be inferred from the
 * container handles.
 */

#ifndef __DSM_API_H__
#define __DSM_API_H__

#include <uuid/uuid.h>
#include <daos/daos_ev.h>
#include <daos/daos_types.h>
#include <daos/daos_errno.h>

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
 * Pool API
 */

/**
 * Connect to the DAOS pool identified by UUID \a uuid.
 * Upon a successful completion, \a poh returns the pool handle and \a failed,
 * which shall be allocated by the caller, returns the targets that failed to
 * establish this connection.
 *
 * \param uuid	[IN]	UUID to identify a pool.
 * \param grp	[IN]	Process group descriptor.
 * \param tgts	[IN]	Optional, indicate potential targets of the pool service
 *			replicas. If not aware of the ranks of the pool service
 *			replicas, the caller may pass in NULL.
 * \param flags	[IN]	Connect mode represented by the DAOS_PC_ bits.
 * \param failed
 *		[OUT]	Optional, buffer to store faulty targets on failure.
 * \param poh	[OUT]	Returned open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Pool is nonexistent
 */
int
dsm_pool_connect(const uuid_t uuid, const daos_group_t *grp,
		 const daos_rank_list_t *tgts, unsigned int flags,
		 daos_rank_list_t *failed, daos_handle_t *poh,
		 daos_event_t *ev);

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
 *			-DER_PERM	Permission denied
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
 * by \a grp.
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
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	network is unreachable
 *			-DER_EXIST	Container uuid already existed
 *			-DER_NONEXIST	Storage target is nonexistent
 */
int
dsm_co_create(daos_handle_t poh, const uuid_t uuid, daos_event_t *ev);

/**
 * Open an existent container identified by UUID \a uuid.
 * Upon a successful completion, \a coh and \a info, both of which shall be
 * allocated by the caller, return the container handle and the container
 * information respectively.
 *
 * \param poh	[IN]	Pool connection handle.
 * \param uuid	[IN]	UUID to identify container.
 * \param flags	[IN]	Open mode, represented by the DAOS_COO_ bits.
 * \param failed
 *		[OUT]	Optional, buffer to store faulty targets on failure.
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
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Container is nonexistent
 */
int
dsm_co_open(daos_handle_t poh, const uuid_t uuid, unsigned int flags,
	    daos_rank_list_t *failed, daos_handle_t *coh, daos_co_info_t *info,
	    daos_event_t *ev);

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
 *			-DER_PERM	Permission denied
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
 * Set the lowest held epoch (LHE) of a container handle.
 * The resulting LHE becomes max(max(handle HCEs) + 1, epoch). The owner of the
 * container handle is responsible for releasing its held epochs by either
 * committing them or setting LHE to DAOS_EPOCH_MAX.
 *
 * \param coh	[IN]	container handle
 * \param epoch	[IN]	epoch to set LHE to
 * \param state	[OUT]	Optional, latest epoch state
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 */
int
dsm_epoch_hold(daos_handle_t coh, daos_epoch_t epoch, daos_epoch_state_t *state,
	       daos_event_t *ev);

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
 * Commit an epoch of a container handle.
 * Unless already committed, the epoch must be higher than the LHE. Otherwise,
 * an error is returned. Once the epoch is committed successfully, the
 * resulting LHE becomes handle HCE + 1.
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
 * \param id	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to open object.
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
 *			-DER_PERM	Permission denied
 *			-DER_NONEXIST	Cannot find object
 *			-DER_EP_OLD	Epoch is too old and has no data for
 *					this object
 */
int
dsm_obj_open(daos_handle_t coh, daos_unit_oid_t id, daos_epoch_t epoch,
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
 * Punch all records in an object.
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

/**
 * Record API
 *
 * A record is identified by daos_key_t, a record can range in index from zero
 * to infinity, each index can own an atomic "data unit", which is arbitrary
 * length. The upper layer stack can fetch or update any number of data units
 * of a record by providing the record key and indices/index ranges of these
 * data units.
 */

/**
 * Fetch data units of the records listed in an explicit array.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the fetch. It is ignored if epoch range is
 *			provided by \a rec_array::rd_eprs.
 *
 * \param rec_array_nr	[IN]
 *			Array size of \a rec_array and \a sgls.
 *
 * \param rec_array	[IN/OUT]
 *			Descriptors for the records to fetch. Checksum of each
 *			data unit, or each range of units is returned in
 *			\a rec_array[i]::rd_rcsums[j]. If the unit size of an
 *			index or range is unknown, which is set to -1 as input,
 *			then the actual unit size of it will be returned in
 *			\a rec_array[i]::rd_indices[j]::ir_usize. In addition,
 *			caller can provide individual epoch for each index or
 *			range in \a rec_array[i]::rd_eprs[j].
 *
 * \param sgls [IN/OUT] Scatter/gather lists (sgl) to store data units of
 *			records. Each record has a separate sgl in \a sgls.
 *
 *			Iovecs in each sgl can be arbitrary as long as their
 *			total size is sufficient to fill in all returned data.
 *			For example, data units of different indices or ranges
 *			of the same record can be adjacently stored in the same
 *			iovec of the sgl of the record: iovec start offset of
 *			an index or range is the end offset of the prevous
 *			index or range.
 *			For an unfound record, the output length of the
 *			corresponding sgl is set to zero.
 *
 * \param rec_layouts [OUT]
 *			Optional, this parameter is mostly for the cache and
 *			tiering layer, other upper layers can simply pass in
 *			NULL.
 *
 *			It is the sink buffer to store the returned actual
 *			index layouts and their epoch validities. The returned
 *			layout covers the same set of indices or index ranges
 *			as \a rec_array. However, the returned ranges could be
 *			fragmented if these ranges were partially updated in
 *			different epochs.
 *			In additition, the returned ranges should also allow
 *			to discriminate punched ranges from punched holes.
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
dsm_rec_fetch(daos_handle_t oh, daos_epoch_t epoch, unsigned int rec_array_nr,
	      daos_rec_array_t *rec_array, daos_sg_list_t *sgls,
	      daos_rec_array_t *rec_layouts, daos_event_t *ev);

/**
 * Insert or udpate data units of the records listed in an explicit array.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the update. It will be ignored if epoch range
 *			is provided by \a rec_array::rd_eprs.
 *
 * \param rec_array_nr	[IN]
 *			Array size of \a rec_array and \a sgls.
 *
 * \param rec_array	[IN]
 *			Array for the records to update. Checksum of each unit,
 *			or each range of units is stored in
 *			\a rec_array[i]::rd_rcsums[j]. If the unit size of an
 *			index or range is zero, then it is effectively a punch
 *			for the specified index/range. In addition, caller can
 *			provide individual epoch for each index or range in
 *			\a rec_array[i]::rd_eprs[j].
 *
 * \param sgls	[IN]	Scatter/gather list (sgl) to store the input data
 *			units. Each record of \a rec_array owns a separate sgl
 *			in \a sgls. Different data units of a record can either
 *			be stored in separate iovec of the sgl, or contiguously
 *			stored in arbitrary iovecs as long as total buffer size
 *			can match the total data units size.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsm_rec_update(daos_handle_t oh, daos_epoch_t epoch, unsigned int rec_array_nr,
	       daos_rec_array_t *rec_array, daos_sg_list_t *rec_sgls,
	       daos_event_t *ev);

/**
 * Enumerate keys or records descriptors.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epr	[IN]	Epoch range for the enumeration.
 *
 * \param filter [IN]	Specific filter for the iteration. When the enumeration
 *			is limited to dkeys, no records or checksums are
 *			returned. \a filter can be set to NULL, in this case,
 *			all keys will be enumeration (i.e. iterate over all
 *			distribution keys and enumerate all attribute keys
 *			available for each distribution key)
 *
 * \param rec_array_nr [IN/OUT]
 *			Input  : array size of \a rec_array
 *			Output : number of returned record descriptors
 *
 * \param rec_array [OUT]
 *			Sink buffer for returned keys or record array.
 *			Unless it is dkey enumeration(see \a filter), otherwise
 *			\a rec_array::rd_indices and \a rec_array::rd_eprs
 *			must be allocated, \a rec_array::rd_nr should be set
 *			to the number of entries of these arrays.
 *
 *			The same record may be returned into multiple entries
 *			of \a rec_array if the indices/ranges of this record
 *			cannot be filled in one entry.
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
 *					fit into output buffer
 *			-DER_REC2BIG	Record is too large and can't be
 *					fit into output buffer
 */
int
dsm_rec_list(daos_handle_t oh, daos_epoch_range_t *epr,
	     daos_list_filter_t *filter, daos_nr_t *rec_array_nr,
	     daos_rec_array_t *rec_array, daos_hash_out_t *anchor,
	     daos_event_t *ev);
#endif /* __DSM_API_H__ */
