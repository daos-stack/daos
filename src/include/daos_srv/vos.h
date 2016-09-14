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
 * This file describes the API for a versioning object store.
 * These APIs will help build a versioned store with
 * key-value and byte-array object types.
 * These APIs provide ways to create, delete, search and enumerate
 * multiversion concurrent key-value and byte-array objects.
 */

#ifndef __VOS_API_H__
#define __VOS_API_H__

#include <daos_types.h>
#include <daos_srv/vos_types.h>

/**
 *  Initialize and Finalize Routines
 *  (required for VOS standalone mode)
 */
/**
 * Initialize the environment for a VOS instance
 * Must be called once before starting a VOS instance
 *
 * \return		Zero on success, negative value if error
 */
int
vos_init(void);

/**
 * Finalize the environment for a VOS instance
 * Must be called for clean up at the end of using a vos instance
 *
 */
void
vos_fini(void);


/**
 * Versioning Object Storage Pool (VOSP)
 * A VOSP creates and manages a versioned object store on a local
 * storage device. The capacity of an OSP is determined
 * by the capacity of the underlying storage device
 */

/**
 * Create a Versioning Object Storage Pool (VOSP) and its root object.
 *
 * \param path	[IN]	Path of the memory pool
 * \param uuid	[IN]    Pool UUID
 * \param size	[IN]	Size of the pool
 * \param poh	[OUT]	Returned pool open handle
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t size);

/**
 * Destroy a Versioned Object Storage Pool (VOSP)
 * The open handle will be invalidated after the destroy.
 *
 * \param poh	[IN]	Pool open handle
 * \param uuid	[IN]	Pool UUID
 *
 * \return		Zero on success, negative value if error
 */
int
vos_pool_destroy(const char *path, uuid_t uuid);

/**
 * Open a Versioning Object Storage Pool (VOSP), load its root object
 * and other internal data structures.
 *
 * \param path	[IN]	Path of the memory pool
 * \param uuid	[IN]    Pool UUID
 * \param poh	[OUT]	Returned pool handle
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_open(const char *path, uuid_t uuid, daos_handle_t *poh);

/**
 * Close a VOSP, all opened containers sharing this pool handle
 * will be revoked.
 *
 * \param poh	[IN]	Pool open handle
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_close(daos_handle_t poh);

/**
 * Query attributes and statistics of the current pool
 *
 * \param poh	[IN]	Pool open handle
 * \param pinfo	[OUT]	Returned pool attributes and stats info
 *
 * \return		Zero on success, negative value if error
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo);

/**
 * Create a container within a VOSP
 *
 * \param poh	[IN]	Pool open handle
 * \param co_uuid
 *		[IN]	UUID for the new container
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_create(daos_handle_t poh, uuid_t co_uuid);

/**
 * Destroy a container
 *
 * \param poh	[IN]	Pool open handle
 * \param co_uuid
 *		[IN]	UUID for the container to be destroyed
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_destroy(daos_handle_t poh, uuid_t co_uuid);

/**
 * Open a container within a VOSP
 *
 * \param poh	[IN]	Pool open handle
 * \param co_uuid
 *		[IN]	Container uuid
 * \param mode	[IN]	open mode: rd-only, rdwr...
 * \param coh	[OUT]	Returned container handle
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_open(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh);

/**
 * Release container open handle
 *
 * \param coh	[IN]	container open handle
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_close(daos_handle_t coh);

/**
 * Query container information.
 *
 * \param coh	[IN]	Container open handle.
 * \param cinfo	[OUT]	Returned container attributes and other information.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_query(daos_handle_t coh, vos_co_info_t *cinfo);

/**
 * Flush changes in the specified epoch to storage
 *
 * \param coh	[IN]	Container open handle
 * \param epoch	[IN]	Epoch to flush
 *
 * \return		Zero on success, negative value if error
 */
int
vos_epoch_flush(daos_handle_t coh, daos_epoch_t epoch);

/**
 * Aggregates all epochs within the epoch range \a epr.
 * Data in all these epochs will be aggregated to the last epoch
 * \a epr::epr_hi, aggregated epochs will be discarded except the last one,
 * which is kept as aggregation result.
 *
 * \param coh	[IN]	Container open handle
 * \param epr	[IN]	The epoch range of aggregation
 *
 * \return		Zero on success, negative value if error
 */
int
vos_epoch_aggregate(daos_handle_t coh, daos_epoch_range_t *epr);

/**
 * Discards changes in all epochs with the epoch range \a epr
 * and \a cookie id. The cookie is assigned by the user
 * during each update call to tag updates that have to be
 * grouped together.
 *
 * \param coh		[IN]	Container open handle
 * \param epr		[IN]	The epoch range to discard
 * \param cookie	[IN]	Cookie ID to identify records,
 *				keys to discard
 *
 * \return			Zero on success, negative value if error
 */
int
vos_epoch_discard(daos_handle_t coh, daos_epoch_range_t *epr,
		  uint64_t cookie);

/**
 * VOS object API
 */
/**
 * Fetch an array of vectors from the specfied object.
 */
/**
 * Fetch values for the given keys and their indices.
 * If output buffer is not provided in \a sgl, then this function returns
 * the directly accessible addresses of record data, upper layer can directly
 * read from these addresses (rdma mode).
 *
 * TODO: add more detail descriptions for punched or missing records.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the fetch. It will be ignored if epoch range
 *			is provided by \a vios.
 * \param dkey	[IN]	Distribution key.
 * \param viod_nr [IN]	Number of vector descriptors in \a viods.
 * \param viods	[IN/OUT]
 *			Array of vector IO descriptors. The returned record
 *			sizes are also stored in this parameter.
 * \param sgls	[OUT]	Scatter/gather list to store the returned record values
 *			or value addresses.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_dkey_t *dkey, unsigned int viod_nr, daos_vec_iod_t *viods,
	      daos_sg_list_t *sgls);


/**
 * Update an array of vectors for the specfied object.
 * If input buffer is not provided in \a sgl, then this function returns
 * the new allocated addresses to store the records, upper layer can
 * directly write data into these addresses (rdma mode).
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the update. It will be ignored if epoch
 *			range is provided by \a vios (kvl::kv_epr).
 * \param cookie [IN]	Cookie ID to tag this update to identify during
 *			discard. This tag is used to group all updates
 *			that might in future be discarded together.
 * \param dkey	[IN]	Distribution key.
 * \param viod_nr [IN]	Number of vector IO descriptors in \a viods.
 * \param viods [IN]	Array of vector IO descriptors.
 * \param sgls	[IN/OUT]
 *			Scatter/gather list to pass in record value buffers,
 *			if caller sets the input buffer size only without
 *			providing input buffers, then VOS will allocate spaces
 *			for the records and return addresses of them, so upper
 *			layer stack can transfer data via rdma.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_update(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	       uint64_t cookie, daos_dkey_t *dkey, unsigned int viod_nr,
	       daos_vec_iod_t *viods, daos_sg_list_t *sgls);

/**
 * Zero-Copy I/O APIs
 */
/**
 *
 * Find and return zero-copy source buffers for the data of the specified
 * vectors of the given object. The caller can directly use these buffers
 * for RMA read.
 *
 * The upper layer must explicitly call \a vos_obj_zc_end to finalise the
 * ZC I/O and release resources.
 *
 * TODO: add more detail descriptions for punched or missing records.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the fetch. It will be ignored if epoch range
 *			is provided by \a vios.
 * \param dkey	[IN]	Distribution key.
 * \param nr	[IN]	Number of vector descriptors in \a vios.
 * \param vios	[IN/OUT]
 *			Array of vector IO descriptors. The returned record
 *			sizes are also restored in this parameter.
 * \param ioh	[OUT]	The returned handle for the ZC I/O.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_zc_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid,
		       daos_epoch_t epoch, daos_dkey_t *dkey, unsigned int nr,
		       daos_vec_iod_t *vios, daos_handle_t *ioh);

/**
 * Finish the zero-copy fetch operation and release the responding resources.
 *
 * \param ioh	[IN]	The ZC I/O handle created by \a vos_obj_zc_fetch_begin
 * \param dkey	[IN]	Distribution key.
 * \param nr	[IN]	Number of vector IO descriptors in \a vios.
 * \param vios	[IN]	Array of vector IO descriptors.
 * \param err	[IN]	Errno of the current fetch, zero if there is no error.
 *			All updates will be dropped if this function is called
 *			for \a vos_obj_zc_update with a non-zero error code.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_zc_fetch_end(daos_handle_t ioh, daos_dkey_t *dkey, unsigned int nr,
		     daos_vec_iod_t *vios, int err);

/**
 * Prepare zero-copy sink buffers for the specified vectors of the given
 * object. The caller can directly use thse buffers for RMA write.
 *
 * The upper layer must explicitly call \a vos_obj_zc_fetch_end to finalise the
 * ZC I/O and release resources.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the update. It will be ignored if epoch
 *			range is provided by \a vios (kvl::kv_epr).
 * \param dkey	[IN]	Distribution key.
 * \param nr	[IN]	Number of vector IO descriptors in \a vios.
 * \param vios	[IN]	Array of vector IO descriptors.
 * \param ioh	[OUT]	The returned handle for the ZC I/O.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_zc_update_begin(daos_handle_t coh, daos_unit_oid_t oid,
			daos_epoch_t epoch, daos_dkey_t *dkey,
			unsigned int nr, daos_vec_iod_t *vios,
			daos_handle_t *ioh);

/**
 * Finish the current zero-copy update and release the responding resources.
 *
 * \param ioh	[IN]	The ZC I/O handle created by \a vos_obj_zc_update_begin
 * \param cookie [IN]	Cookie ID to tag this update to identify during
 *			discard. This tag is used to group all updates
 *			that might in future be discarded together.
 * \param dkey	[IN]	Distribution key.
 * \param nr	[IN]	Number of vector IO descriptors in \a vios.
 * \param vios	[IN]	Array of vector IO descriptors.
 * \param err	[IN]	Errno of the current update, zero if there is no error.
 *			All updates will be dropped if this function is called
 *			for \a vos_obj_zc_update with a non-zero error code.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_zc_update_end(daos_handle_t ioh, uint64_t cookie,
		      daos_dkey_t *dkey, unsigned int nr,
		      daos_vec_iod_t *vios, int err);

/**
 * Get the zero-copy scatter/gather list for the specified vector.
 *
 * \param ioh	[IN]	The ZC I/O handle.
 * \param vec_at [IN]	Index of the vector within @vios while creating the
 *			ZC I/O handle \a ioh.
 * \param sgl_pp [OUT]	The returned scatter/gather list.
 */
int
vos_obj_zc_vec2sgl(daos_handle_t ioh, unsigned int vec_at,
		   daos_sg_list_t **sgl_pp);

/**
 * VOS iterator APIs
 */
/**
 * Initialise an iterator for VOS
 *
 * \param param	[IN]	Parameters for the iterator.
 *			For different iterator types:
 *			- VOS_ITER_COUUID : param::ip_hdl is pool open handle
 *
 *			- VOS_ITER_OBJ	  : param::ip_hdl is container handle
 *
 *			- VOS_ITER_DKEY	  : param::ip_hdl is container handle,
 *					    param::ip_oid is ID of KV object.
 *
 *			- VOS_ITER_AKEY	  : param::ip_hdl is container handle,
 *					    param::ip_oid is ID of KV object.
 *					    param::ip_dkey is the distribution
 *					    key of the akeys to be iterated.
 *					    (NB: a-key is unsupported for now)
 *
 *			- VOS_ITER_RECX	  : param::ip_hdl is container handle,
 *					    param::ip_oid is ID of byte array
 *					    param::ip_dkey is the distribution
 *					    key of the akeys to be iterated.
 *					    param::ip_akey is the attribute key
 *					    key of the records to be iterated.
 * \param ih	[OUT]	Returned iterator handle
 *
 * \return		Zero on success, negative value if error
 */
int
vos_iter_prepare(vos_iter_type_t type, vos_iter_param_t *param,
		 daos_handle_t *ih);

/**
 * Release a iterator
 *
 * \param ih	[IN]	Iterator handle to release
 *
 * \return		Zero on success, negative value if error
 */
int
vos_iter_finish(daos_handle_t ih);

/**
 * Set the iterator cursor to the specified position \a anchor if it is
 * not NULL, otherwise move the cursor to the begin of the iterator.
 * This function must be called before using vos_iter_next or vos_iter_fetch.
 *
 * \param ih	[IN]	Iterator handle.
 * \param pos	[IN]	Optional, position cursor to move to.
 *
 * \return		zero if there is an entry at/after @anchor
 *			-DER_NONEXIST if no more entry
 *			negative value if error
 */
int
vos_iter_probe(daos_handle_t ih, daos_hash_out_t *anchor);

/**
 * Move forward the iterator cursor.
 *
 * \param ih	[IN]	Iterator handle.
 *
 * \return		Zero if there is an available entry
 *			-DER_NONEXIST if no more entry
 *			negative value if error
 */
int
vos_iter_next(daos_handle_t ih);

/**
 * Return the current data entry of the iterator.
 *
 * \param ih	[IN]	Iterator handle
 * \param entry [OUT]	Optional, returned data entry fo the current cursor
 * \param anchor [OUT]	Optional, position anchor for this entry
 *
 * \return		Zero on success
 *			-DER_NONEXIST if no more entry
 *			negative value if error
 */
int
vos_iter_fetch(daos_handle_t ih, vos_iter_entry_t *entry,
	       daos_hash_out_t *anchor);

#endif /* __VOS_API_H */
