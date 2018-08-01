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
 * This file describes the API for a versioning object store.
 * These APIs will help build a versioned store with key value objects.
 * The KV index is composed of a {distribution-key, attribute-key, record}.
 * These APIs provide ways to create, delete, search and enumerate
 * such multiversion objects over PMEM.
 */

#ifndef __VOS_API_H__
#define __VOS_API_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos_srv/vos_types.h>

/**
 * Initialize the environment for a VOS instance
 * Must be called once before starting a VOS instance
 *
 * NB: Required only when using VOS as a standalone
 * library.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_init(void);

/**
 * Finalize the environment for a VOS instance
 * Must be called for clean up at the end of using a vos instance
 *
 * NB: Needs to be called only when VOS is used as a
 * standalone library.
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
 * \param scm_sz [IN]	Size of SCM for the pool
 * \param blob_sz[IN]	Size of blob for the pool
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t scm_sz,
		daos_size_t blob_sz);

/**
 * Destroy SPDK blob.
 *
 * \param uuid	[IN]	Pool UUID
 *
 * \return		Zero on success, negative value if error
 */
int
vos_blob_destroy(void *uuid);

/**
 * Destroy a Versioned Object Storage Pool (VOSP)
 *
 * \param path	[IN]	Path of the memory pool
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
vos_cont_create(daos_handle_t poh, uuid_t co_uuid);

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
vos_cont_destroy(daos_handle_t poh, uuid_t co_uuid);

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
vos_cont_open(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh);

/**
 * Release container open handle
 *
 * \param coh	[IN]	container open handle
 *
 * \return		Zero on success, negative value if error
 */
int
vos_cont_close(daos_handle_t coh);

/**
 * Query container information.
 *
 * \param coh	[IN]	Container open handle.
 * \param cinfo	[OUT]	Returned container attributes and other information.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_cont_query(daos_handle_t coh, vos_cont_info_t *cinfo);

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
 * \param coh	  [IN]		Container open handle
 * \param oid	  [IN]		Object handle for aggregation
 * \param epr	  [IN]		The epoch range of aggregation
 * \param credits [IN/OUT]	credits for probing object tree
 * \param anchor  [IN/OUT]	anchor returned for preemption.
 * \param finished
 *		  [OUT]		flag returned to notify completion
 *				of aggregation to caller.
 *
 * \return			Zero on success, negative value if error
 */
int
vos_epoch_aggregate(daos_handle_t coh, daos_unit_oid_t oid,
		    daos_epoch_range_t *epr, unsigned int *credits,
		    vos_purge_anchor_t *anchor, bool *finished);

/**
 * Discards changes in all epochs with the epoch range \a epr
 * and \a cookie id.
 *
 * If a single epoch needs to be discarded then \a epr::epr_lo
 * and \a epr::hi must be set to the same epoch.
 * If all epochs from a certain epoch needs to be discarded then
 * \a epr::epr_hi must be set to DAOS_EPOCH_MAX.
 * If a epochs within a range must be discarded then
 * \a epr::epr_hi must be set to a meaningful epoch value less
 * than DAOS_EPOCH_MAX.
 *
 * Note: \a epr::epr_lo must never be set to DAOS_EPOCH_MAX by
 * the caller.
 * \a cookie is a uuid assigned by the user during each update
 * call to tag updates that have to be grouped together.
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
		  uuid_t cookie);

/**
 * VOS object API
 */
/**
 * Fetch records from the specified object.
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
 *			is provided by \a iods.
 * \param dkey	[IN]	Distribution key.
 * \param iod_nr [IN]	Number of I/O descriptors in \a iods.
 * \param iods	[IN/OUT]
 *			Array of I/O descriptors. The returned record
 *			sizes are also stored in this parameter.
 * \param sgls	[OUT]	Scatter/gather list to store the returned record values
 *			or value addresses.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_fetch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      daos_key_t *dkey, unsigned int iod_nr, daos_iod_t *iods,
	      daos_sg_list_t *sgls);


/**
 * Update records for the specfied object.
 * If input buffer is not provided in \a sgl, then this function returns
 * the new allocated addresses to store the records, upper layer can
 * directly write data into these addresses (rdma mode).
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the update. It will be ignored if epoch
 *			range is provided by \a iods (kvl::kv_epr).
 * \param cookie [IN]	Cookie ID to tag this update to identify during
 *			discard. This tag is used to group all updates
 *			that might in future be discarded together.
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param dkey	[IN]	Distribution key.
 * \param iod_nr [IN]	Number of I/O descriptors in \a iods.
 * \param iods [IN]	Array of I/O descriptors.
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
	       uuid_t cookie, uint32_t pm_ver, daos_key_t *dkey,
	       unsigned int iod_nr, daos_iod_t *iods, daos_sg_list_t *sgls);

/**
 * Punch an object, or punch a dkey, or punch an array of akeys under a akey.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID, the full object will be punched if \a dkey
 *			and \a akeys are not provided.
 * \param epoch	[IN]	Epoch for the punch.
 * \param cookie [IN]	Cookie ID to tag this punch to identify during
 *			discard. This tag is used to group all updates
 *			that might in future be discarded together.
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param flags [IN]	Object punch flags, VOS_OF_REPLAY_PC is the only
 *			currently supported flag.
 * \param dkey	[IN]	Optional, the dkey will be punched if \a akeys is not
 *			provided.
 * \param akey_nr [IN]	Number of akeys in \a akeys.
 * \param akeys [IN]	Array of akeys to be punched.

 */
int
vos_obj_punch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      uuid_t cookie, uint32_t pm_ver, uint32_t flags,
	      daos_key_t *dkey, unsigned int akey_nr, daos_key_t *akeys);

/**
 * I/O APIs
 */
/**
 *
 * Find and return I/O source buffers for the data of the specified
 * arrays of the given object. The caller can directly use these buffers
 * for RMA read.
 *
 * The upper layer must explicitly call \a vos_fetch_end to finalise the
 * I/O and release resources.
 *
 * TODO: add more detail descriptions for punched or missing records.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the fetch. It will be ignored if epoch range
 *			is provided by \a iods.
 * \param dkey	[IN]	Distribution key.
 * \param nr	[IN]	Number of I/O descriptors in \a ios.
 * \param iods	[IN/OUT]
 *			Array of I/O descriptors. The returned record
 *			sizes are also restored in this parameter.
 * \param size_fetch[IN]
 *			Fetch size only
 * \param ioh	[OUT]	The returned handle for the I/O.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		bool size_fetch, daos_handle_t *ioh);

/**
 * Finish the fetch operation and release the responding resources.
 *
 * \param ioh	[IN]	The I/O handle created by \a vos_fetch_begin
 * \param err	[IN]	Errno of the current fetch, zero if there is no error.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_fetch_end(daos_handle_t ioh, int err);

/**
 * Prepare IO sink buffers for the specified arrays of the given
 * object. The caller can directly use thse buffers for RMA write.
 *
 * The upper layer must explicitly call \a vos_fetch_end to finalise the
 * ZC I/O and release resources.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the update. It will be ignored if epoch
 *			range is provided by \a iods (kvl::kv_epr).
 * \param dkey	[IN]	Distribution key.
 * \param nr	[IN]	Number of I/O descriptors in \a iods.
 * \param iods	[IN]	Array of I/O descriptors.
 * \param ioh	[OUT]	The returned handle for the I/O.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		 daos_handle_t *ioh);

/**
 * Finish the current update and release the responding resources.
 *
 * \param ioh	[IN]	The I/O handle created by \a vos_update_begin
 * \param cookie [IN]	Cookie ID to tag this update to identify during
 *			discard. This tag is used to group all updates
 *			that might in future be discarded together.
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param dkey	[IN]	Distribution key.
 * \param err	[IN]	Errno of the current update, zero if there is no error.
 *			All updates will be dropped if this function is called
 *			for \a vos_update_begin with a non-zero error code.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_update_end(daos_handle_t ioh, uuid_t cookie, uint32_t pm_ver,
	       daos_key_t *dkey, int err);

/**
 * Get the I/O descriptor.
 *
 * \param ioh	[IN]	The I/O handle.
 *
 * \return		BIO IO descriptor
 */
struct bio_desc *
vos_ioh2desc(daos_handle_t ioh);

/**
 * Get the scatter/gather list associated with a given I/O descriptor.
 *
 * \param ioh	[IN]	The I/O handle.
 * \param idx	[IN]	SGL index.
 *
 * \return		BIO SGL
 */
struct bio_sglist *
vos_iod_sgl_at(daos_handle_t ioh, unsigned int idx);

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
 *			- VOS_ITER_SINGLE   param::ip_oid is ID of byte array
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
vos_iter_probe(daos_handle_t ih, daos_anchor_t *anchor);

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
	       daos_anchor_t *anchor);

/**
 * Delete the current data entry of the iterator
 *
 * \param ih	[IN]	Iterator handle
 * \param args	[IN/OUT]
 *			Optional, Provide additional hints while
 *			deletion to handle special cases.
 *			for example return record of this node
 *			to user instead of deleting.
 *
 * \return		Zero on Success
 *			-DER_NONEXIST if cursor does
 *			not exist. negative value if error
 *
 */
int
vos_iter_delete(daos_handle_t ih, void *args);

/**
 * If the iterator has any element. The condition provided to vos_iter_prepare
 * will not be taken into account, so even if there is no element can match
 * the iterator condition, but the function still returns false if there is
 * any other element.
 *
 * \param ih	[IN]	Iterator handle
 *
 * \return		1 it is empty
 *			0 it is not empty
 *			-ve error code
 */
int
vos_iter_empty(daos_handle_t ih);

/**
 * VOS object index set attributes
 * Add a new object ID entry in the object index table
 * Creates an empty tree for the object
 *
 * \param coh   [IN]    Container handle
 * \param oid   [IN]    DAOS object ID
 * \param epoch [IN]    Epoch to set
 * \param attr	[IN]	Attributes bitmask
 *
 * \return              0 on success and negative on
 *                      failure
 */
int
vos_oi_set_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t attr);

/**
 * VOS object index clear attributes
 * Add a new object ID entry in the object index table
 * Creates an empty tree for the object
 *
 * \param coh   [IN]    Container handle
 * \param oid   [IN]    DAOS object ID
 * \param epoch [IN]    Epoch to set
 * \param attr	[IN]	Attributes bitmask
 *
 * \return              0 on success and negative on
 *                      failure
 */
int
vos_oi_clear_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		  uint64_t attr);

/**
 * VOS object index get attributes
 *
 * \param coh   [IN]		Container handle
 * \param oid   [IN]		DAOS object ID
 * \param epoch [IN]		Epoch to get
 * \param attr	[IN, OUT]	Attributes bitmask
 *
 * \return			0 on success and negative on
 *				failure
 */
int
vos_oi_get_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t *attr);
#endif /* __VOS_API_H */
