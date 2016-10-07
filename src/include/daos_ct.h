/**
 * (C) Copyright 2016 Intel Corporation.
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
 * @file
 * @brief DAOS Caching and Tiering APIs
 *
 * Author: John Keys <john.keys@intel.com>
 * Author: Ian F. Adams <ian.f.adams@intel.com>
 * Version 0.1
 */

#ifndef __DCT_API_H__
#define __DCT_API_H__

#include <daos_types.h>
#include <daos_api.h>

/**
 * CT Specific Structs
 */

/**
 * Place Holder for caching policy
 */
typedef struct {
	/*Choice of eviction alg*/
	uint32_t	dct_cp_evict;
	/*Choice of persistence policy*/
	uint32_t	dct_cp_persist;
	/*Choice of read ahead policy*/
	uint32_t	dct_cp_read_ahead;
	/*hi-water mark for eviction*/
	uint64_t	dct_cp_hi_water;
	/*lo-water for eviction*/
	uint64_t	dct_cp_lo_water;
} daos_cache_pol_t;

/**
 * Type of pool/tier
 */
typedef enum {
	/*A regular caching tier*/
	DAOS_TR_CACHE,
	/*A parking tier*/
	DAOS_TR_PARKING,
} daos_tier_type_t;

/**
 * Summarize a pool and its policies for caching
 */
typedef struct {
	/*What is the primary media of the pool*/
	daos_target_type_t	dct_ti_media;
	/* Describe the caching policy*/
	daos_cache_pol_t	dct_ti_policy;
	/*What type of tier (currently only cache or parking)*/
	daos_tier_type_t	dct_ti_type;
	/*temperature of the tier-pool, used to set up a hierarchy*/
	uint32_t		dct_ti_tmpr;
	/*Open handle affiliated with this pool tier*/
	daos_handle_t		dct_ti_poh;
	/*UUID of the pool*/
	uuid_t			dct_ti_pool_id;
} daos_tier_info_t;

/*Convenient struct for moving all tier info together*/
typedef struct {
	/* Number of tiers*/
	daos_nr_t		tl_nr;
	/*refernce to tier list*/
	daos_tier_info_t	*tl_tiers;
} daos_tier_list_t;

/**
 * Initialize the DAOS-CT library.
 */
int
dct_init(void);

/**
 * Finalize the DAOS-CT library.
 */
int
dct_fini(void);

/**
 * CT (Pre)Fetch API
 */

/**
 * Move an entire containers content at a specified highest committed epoch
 * HCE to the target pool. This is sourced from the coldest tier of the
 * tier hierarchy
 *
 * \param poh	[IN]	Pool connection handle of the target pool
 * \param cont_id
 *		[IN]	UUID of the container to fetch
 * \param fetch_ep
 *		[IN]	Epoch to fetch. To retrieve HCE pass in 0.
 * \param obj_list	List of objects to fetch, if NULL, all objects in the
 *			container will be retrieved
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::error in
 *			non-blocking mode:
 *			-0	      Success
 *		      -DER_NO_HDL     Invalid pool handle
 *		      -DER_INVAL      Invalid parameter
 *			-DER_NONEXIST   Container is nonexistent on lower tier
 *			-DER_UNREACH    Network is unreachable
 *			-DER_NO_PERM	Permission denied
 */
int
dct_fetch_container(daos_handle_t poh, const uuid_t cont_id,
		    daos_epoch_t fetch_ep, daos_oid_list_t obj_list,
		    daos_event_t *ev);

/**
 * CT Tier Mapping API
 */

 /**
 * Registers one (or more) pools as tiers
 * \param local_pl_id
 *		[IN]	The ID of the pool that is local. This is used
 *			in figuring out which tiers are warmer
 *			and colder than self.
 * \param local_temp
 *		[IN]	temperature of the local tier, used to figure
 *			out who is warmer and colder than the local tier
 * \param tier_list
 *		[IN]	list of all tiers for a particular workflow
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *		      Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::error in
 *			non-blocking mode:
 *			-0		Success
 *			-DER_NO_HDL     Invalid pool handle
 *		      -DER_INVAL      Invalid parameter
 *		      -DER_UNREACH    Network is unreachable
 */
int
dct_tier_register(uuid_t local_pl_id, uint32_t local_temp,
		      daos_tier_list_t tier_list, daos_event_t *ev);

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
dct_co_create(daos_handle_t poh, const uuid_t uuid, daos_event_t *ev);

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
dct_co_open(daos_handle_t poh, const uuid_t uuid, unsigned int flags,
	    daos_rank_list_t *failed, daos_handle_t *coh,
	    daos_cont_info_t *info, daos_event_t *ev);

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
dct_co_close(daos_handle_t coh, daos_event_t *ev);

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
dct_co_destroy(daos_handle_t poh, const uuid_t uuid, int force,
	       daos_event_t *ev);


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
dct_obj_declare(daos_handle_t coh, daos_obj_id_t *id, daos_epoch_t epoch,
		daos_obj_attr_t *oa, daos_handle_t *oh, daos_event_t *ev);

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
dct_obj_open(daos_handle_t coh, daos_obj_id_t id, daos_epoch_t epoch,
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
dct_obj_close(daos_handle_t oh, daos_event_t *ev);

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
dct_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev);

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
dct_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
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
 * \param maps	[OUT]	Optional, this parameter is mostly for the cache and
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
dct_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
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
 *			-DER_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 *			-DER_EP_RO	Epoch is read-only
 */
int
dct_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
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
 * \param kds	[IN]	preallocated array of \a nr key descriptors.
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
dct_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, daos_nr_t *nr,
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
 * \param kds	[IN]	preallocated array of \a nr key descriptors.
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
dct_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
		  daos_nr_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		  daos_hash_out_t *anchor, daos_event_t *ev);



/**
 * PING client call, mostly for testing and playing around
 * TODO add actual docstring if we decide to keep it
 */
int
dct_ping(uint32_t ping_val, daos_event_t *ev);


/**
 * Pool APIs from DAOS-SR
 */

#define dct_pool_connect	daos_pool_connect
#define dct_pool_disconnect	daos_pool_disconnect
#define dct_pool_exclude	daos_pool_exclude
#define dct_pool_query		daos_pool_query
#define dct_pool_target_query	daos_pool_target_query

/**
 * Container APIs from DAOS-SR
 */

#define dct_co_query		daos_cont_query
#define dct_co_attr_list	daos_cont_attr_list
#define dct_co_attr_get		daos_cont_attr_get
#define dct_co_attr_set		daos_cont_attr_set

/**
 * Epoch APIs from DAOS-SR
 */

#define dct_epoch_flush			daos_epoch_flush
#define dct_epoch_flush_target		daos_epoch_flush_target
#define dct_epoch_discard		daos_epoch_discard
#define dct_epoch_discard_target	daos_epoch_discard_target
#define dct_epoch_query			daos_epoch_query
#define dct_epoch_hold			daos_epoch_hold
#define dct_epoch_slip			daos_epoch_slip
#define dct_epoch_commit		daos_epoch_commit
#define dct_epoch_wait			daos_epoch_wait

/**
 * Snapshot APIs from DAOS-SR
 */

#define dct_snap_list		daos_snap_list
#define dct_snap_create		daos_snap_create
#define dct_snap_destroy	daos_snap_destroy

/**
 * Object Class APIs from DAOS-SR
 */

#define dct_oclass_register	daos_oclass_register
#define dct_oclass_query	daos_oclass_query
#define dct_oclass_list		daos_oclass_list

#endif /* __DCT_API_H__ */
