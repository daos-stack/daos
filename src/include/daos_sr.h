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
 * Handle API
 */

#define dsr_local2global	dsm_local2global
#define dsr_global2local	dsm_global2local

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
dsr_oclass_register(daos_handle_t coh, daos_oclass_id_t cid,
		    daos_oclass_attr_t *cattr, daos_event_t *ev);

/**
 * Query attributes of object class by its ID.
 *
 * \param coh	[IN]	Container open handle.
 * \param cid	[IN]	Class ID to query.
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
dsr_oclass_query(daos_handle_t coh, daos_oclass_id_t cid,
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
 * Generate a DAOS object ID by encoding the private DAOS bits of the object
 * address space.
 *
 * \param oid	[IN]	Object ID with low 160 bits set and unique inside the
 *			container.
 *		[OUT]	Fully populated DAOS object identifier with the the low
 *			160 bits untouched and the DAOS private	bits (the high
 *			32 bits) encoded.
 * \param cid	[IN]	Class Identifier
 */
static inline void
dsr_objid_generate(daos_obj_id_t *oid, daos_oclass_id_t cid)
{
	uint64_t hdr = cid;

	oid->hi &= 0x00000000ffffffff;
	/**
	 * | 8-bit version | 8-bit unused |
	 * | 16-bit object class          |
	 * | 160-bit for upper layer ...  |
	 */
	hdr <<= 32;
	hdr |= 0x1ULL << 56;
	oid->hi |= hdr;
}

/**
 * Object attributes (metadata).
 * \a oa_class and \a oa_oa are mutually exclusive.
 */
typedef struct {
	/** Optional, affinity target for the object */
	daos_rank_t		 oa_rank;
	/** Optional, class attributes of object with private class */
	daos_oclass_attr_t	*oa_oa;
} dsr_obj_attr_t;

/**
 * Declare a new object based on attributes \a oa.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID generated by dsr_objid_generate().
 * \param epoch	[IN]	Epoch to create object.
 * \param oa	[IN]	Optional, object creation parameters.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
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
dsr_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		dsr_obj_attr_t *oa, daos_event_t *ev);

/**
 * Open an declared DAOS-SR object.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to open object.
 * \param mode	[IN]	Open mode: DAOS_OO_RO/RW/EXCL/IO_RAND/IO_SEQ
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
dsr_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
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
 * Object I/O API
 */

/**
 * Fetch object records from co-located vectors.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the fetch. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. through \a iods[]::vd_eprs[]).
 *
 * \param dkey	[IN]	Distribution key associated with the fetch operation.
 *
 * \param nr	[IN]	Number of I/O descriptor and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param iods	[IN]	Array of vector I/O descriptors. Each descriptor is
 *			associated with	a given akey and describes the list of
 *			record extents to fetch from the vector.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::vd_eprs[] and in this case, \a epoch will be
 *			ignored.
 *		[OUT]	Checksum of each extent is returned via
 *			\a iods[]::vd_csums[]. If the record size of an
 *			extent is unknown (i.e. set to -1 as input), then the
 *			actual record size will be returned in
 *			\a iods[]::vd_recxs[]::rx_rsize.
 *
 * \param sgls	[IN]	Scatter/gather lists (sgl) to store records. Each vector
 *			is associated with a separate sgl in \a sgls.
 *			Iovecs in each sgl can be arbitrary as long as their
 *			total size is sufficient to fill in all returned data.
 *			For example, extents with records of different sizes can
 *			be adjacently stored in the same iovec of the sgl of the
 *			vector: iovec start offset of an extent is the end
 *			offset of the prevous extent.
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
 *			epochs.	In additition, the returned extents should also
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
dsr_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	      unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	      daos_vec_map_t *maps, daos_event_t *ev);

/**
 * Insert or udpate object records stored in co-located vectors.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. \a iods[]::vd_eprs[]).
 *
 * \param dkey	[IN]	Distribution key associated with the update operation.
 *
 * \param nr	[IN]	Number of descriptors and scatter/gather lists in
 *			respectively \a iods and \a sgls.
 *
 * \param iods	[IN]	Array of vector I/O descriptor. Each descriptor is
 *			associated with a vector identified by its akey and
 *			describes the list of record extent to update.
 *			A different epoch can be passed for each extent via
 *			\a iods[]::vd_eprs[] and in this case, \a epoch will be
 *			ignored.
 *			Checksum of each record extent is stored in
 *			\a iods[]::vd_csums[]. If the record size of an extent
 *			is zero, then it is effectively a punch	for the
 *			specified index range.
 *
 * \param sgls	[IN]	Scatter/gather list (sgl) to store the input data
 *			records. Each vector I/O descriptor owns a separate sgl
 *			in \a sgls.
 *			Different records of the same extent can either be
 *			stored in separate iovec of the sgl, or contiguously
 *			stored in arbitrary iovecs as long as total buffer size
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
dsr_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	       unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
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
dsr_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, daos_nr_t *nr,
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
 * \param nr	[IN]    number of key descriptors in \a kds
 *		[OUT]   number of returned key descriptors.
 *
 * \param sgl	[IN]	Scatter/gather list to store the dkey list.
 *			All dkeys are written continuously, actual limits can be
 *			found in \a kds.
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
dsr_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
		  daos_nr_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		  daos_hash_out_t *anchor, daos_event_t *ev);
#endif /* __DSR_API_H__ */
