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
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015, 2016 Intel Corporation.
 */
/**
 * DAOS Sharding & Resilience APIs
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 *
 * Version 0.3
 */

#ifndef __DSR_API_H__
#define __DSR_API_H__

#include <daos_m.h>

/**
 * DAOS APIs can run either in non-blocking mode or in blocking mode:
 *
 * - Non-blocking mode
 *   If input event(daos_event_t) of API is not NULL, it will run in
 *   non-blocking mode and return immediately after submitting API request
 *   to underlying stack.
 *   Returned value of API is zero on success, or negative error code only if
 *   there is an invalid parameter or other failure which can be detected
 *   without calling into server stack.
 *   Error codes for all other failures will be returned by event::ev_error.
 *
 * - Blocking mode
 *   If input event of API is NULL, it will run in blocking mode and return
 *   after completing of operation. Error codes for all failure cases should
 *   be returned by return value of API.
 */

/**
 * Initialize the DAOS-SR library.
 */
int
dsr_init(void);

/**
 * Finalize the DAOS-SR library.
 */
int
dsr_fini(void);

/**
 * Pool API is shared with DAOS-M
 */

#define dsr_pool_connect	dsm_pool_connect
#define dsr_pool_disconnect	dsm_pool_disconnect
#define dsr_pool_exclude	dsm_pool_exclude
#define dsr_pool_query		dsm_pool_query
#define dsr_pool_target_query	dsm_pool_target_query

/**
 * Container API is shared with DAOS-M
 */

#define dsr_co_create		dsm_co_create
#define dsr_co_open		dsm_co_open
#define dsr_co_close		dsm_co_close
#define dsr_co_destroy		dsm_co_destroy
#define dsr_co_query		dsm_co_query
#define dsr_co_attr_list	dsm_co_attr_list
#define dsr_co_attr_get		dsm_co_attr_get
#define dsr_co_attr_set		dsm_co_attr_set

/**
 * Epoch API is shared with DAOS-M
 */

#define dsr_epoch_flush			dsm_epoch_flush
#define dsr_epoch_flush_target		dsm_epoch_flush_target
#define dsr_epoch_discard		dsm_epoch_discard
#define dsr_epoch_discard_target	dsm_epoch_discard_target
#define dsr_epoch_query			dsm_epoch_query
#define dsr_epoch_hold			dsm_epoch_hold
#define dsr_epoch_slip			dsm_epoch_slip
#define dsr_epoch_commit		dsm_epoch_commit
#define dsr_epoch_wait			dsm_epoch_wait

/**
 * Snapshot API is shared with DAOS-M
 */

#define dsr_snap_list		dsm_snap_list
#define dsr_snap_create		dsm_snap_create
#define dsr_snap_destroy	dsm_snap_destroy

/**
 * Object API
 */

/**
 * Register a new object class.
 * A object class cannot be unregistered for the time being.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	ID for the new object class.
 * \param cattr	[IN]	Attributes for the new object class.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EXIST	Object class ID already existed
 */
int
dsr_oclass_register(daos_handle_t coh, daos_oclass_id_t id,
		    daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * Query attributes of object class by its ID.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN]	Class ID to query.
 * \param cattr	[OUT]	Returned attributes of the object class.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_NONEXIST	nonexistent class ID
 */
int
dsr_oclass_query(daos_handle_t coh, daos_oclass_id_t id,
		 daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * List existing object class.
 *
 * \param coh	[IN]	Container open handle.
 * \param clist	[OUT]	Sink buffer for returned class list.
 * \param anchor [IN/OUT]
 *			Hash anchor for the next call, it should be set to
 *			zeroes for the first call, it should not be altered
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
dsr_oclass_list(daos_handle_t coh, daos_oclass_list_t *clist,
		daos_hash_out_t *anchor, daos_event_t *ev);

/**
 * Object attributes (metadata).
 * \a oa_class and \a oa_oa are mutually exclusive.
 */
typedef struct {
	/** Pre-defined class ID */
	daos_oclass_id_t	 oa_class;
	/** Optional, affinity target for the object */
	dtp_rank_t		 oa_rank;
	/** Optional, class attributes of object with private class */
	daos_oclass_attr_t	*oa_oa;
} dsr_obj_attr_t;

/**
 * Declare a new object based on attributes \a oa.
 *
 * \param coh	[IN]	Container open handle.
 * \param id	[IN/OUT]
 *			object ID, daos may fill reserved bits of object ID.
 * \param epoch	[IN]	Epoch to create object.
 * \param oa	[IN]	Object creation parameters.
 * \param oh	[OUT]	Returned object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_UNREACH	Network is unreachable
 *			-DER_PERM	Permission denied
 *			-DER_EXIST	Object ID has been used for another
 *					object.
 *			-DER_NONEXIST	Cannot find container on specified
 *					storage target
 *			-DER_NOTYPE	Unknown object type
 *			-DER_NOSCHEMA	Unknown object schema
 *			-DER_EP_RO	Epoch is read-only
 */
int
dsr_obj_declare(daos_handle_t coh, daos_obj_id_t *id, daos_epoch_t epoch,
		dsr_obj_attr_t *oa, daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an declared DAOS-SR object.
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
dsr_obj_open(daos_handle_t coh, daos_obj_id_t id, daos_epoch_t epoch,
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
dsr_obj_close(daos_handle_t oh, daos_event_t *ev);

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
dsr_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Query attributes of an object.
 * Caller should provide at least one of output parameters.
 *
 * \param oh	[IN]	Object open handle.
 * \param epoch	[IN]	Epoch to query.
 * \param oa	[OUT]	Returned object attributes.
 * \param ranks	[OUT]	Ordered list of ranks where the object is stored.
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
dsr_obj_query(daos_handle_t oh, daos_epoch_t epoch, dsr_obj_attr_t *oa,
	      daos_rank_list_t *ranks, daos_event_t *ev);

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
dsr_rec_fetch(daos_handle_t oh, daos_epoch_t epoch, unsigned int rec_array_nr,
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
dsr_rec_update(daos_handle_t oh, daos_epoch_t epoch, unsigned int rec_array_nr,
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
dsr_rec_list(daos_handle_t oh, daos_epoch_range_t *epr,
	     daos_list_filter_t *filter, daos_nr_t *rec_array_nr,
	     daos_rec_array_t *rec_array, daos_hash_out_t *anchor,
	     daos_event_t *ev);
#endif /* __DSR_API_H__ */
