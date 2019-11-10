/**
 * (C) Copyright 2015-2019 Intel Corporation.
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
#include <daos/placement.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/vos_types.h>

/**
 * Refresh the DTX resync generation.
 *
 * \param coh	[IN]	Container open handle.
 *
 * \return		Zero on success.
 * \return		Negative value if error.
 */
int
vos_dtx_update_resync_gen(daos_handle_t coh);

/**
 * Add the given DTX to the Commit-on-Share (CoS) cache (in DRAM).
 *
 * \param coh		[IN]	Container open handle.
 * \param oid		[IN]	The target object (shard) ID.
 * \param dti		[IN]	The DTX identifier.
 * \param dkey_hash	[IN]	The hashed dkey.
 * \param epoch		[IN]	The DTX epoch.
 * \param gen		[IN]	The DTX generation.
 * \param punch		[IN]	For punch DTX or not.
 *
 * \return		Zero on success.
 * \return		-DER_INPROGRESS	retry with newer epoch.
 * \return		Other negative value if error.
 */
int
vos_dtx_add_cos(daos_handle_t coh, daos_unit_oid_t *oid, struct dtx_id *dti,
		uint64_t dkey_hash, daos_epoch_t epoch, uint64_t gen,
		bool punch);

/**
 * Search the specified DTX is in the CoS cache or not.
 *
 * \param coh		[IN]	Container open handle.
 * \param oid		[IN]	Pointer to the object ID.
 * \param xid		[IN]	Pointer to the DTX identifier.
 * \param dkey_hash	[IN]	The hashed dkey.
 * \param punch		[IN]	For punch DTX or not.
 *
 * \return	0 if the DTX exists in the CoS cache.
 * \return	-DER_NONEXIST if not in the CoS cache.
 * \return	Other negative values on error.
 */
int
vos_dtx_lookup_cos(daos_handle_t coh, daos_unit_oid_t *oid,
		   struct dtx_id *xid, uint64_t dkey_hash, bool punch);

/**
 * Fetch the list of the DTXs to be committed because of (potential) share.
 *
 * \param coh		[IN]	Container open handle.
 * \param oid		[IN]	The target object (shard) ID.
 * \param dkey_hash	[IN]	The hashed dkey.
 * \param types		[IN]	The DTX types to be listed.
 * \param max		[IN]	The max size of the array for DTX entries.
 * \param dtis		[OUT]	The DTX IDs array to be committed for share.
 *
 * \return			The count of DTXs to be committed for share
 *				on success, negative value if error.
 */
int
vos_dtx_list_cos(daos_handle_t coh, daos_unit_oid_t *oid, uint64_t dkey_hash,
		 uint32_t types, int max, struct dtx_id **dtis);

/**
 * Fetch the list of the DTXs that can be committed.
 *
 * \param coh		[IN]	Container open handle.
 * \param max_cnt	[IN]	The max size of the array for DTX entries.
 * \param oid		[IN]	Only return the DTXs belong to the specified
 *				object if it is non-NULL.
 * \param epoch		[IN]	Only return the DTXs that is not newer than
 *				the specified epoch.
 * \param dtes		[OUT]	The array for DTX entries can be committed.
 *
 * \return		Positve value for the @dtes array size.
 *			Negative value on failure.
 */
int
vos_dtx_fetch_committable(daos_handle_t coh, uint32_t max_cnt,
			  daos_unit_oid_t *oid, daos_epoch_t epoch,
			  struct dtx_entry **dtes);

/**
 * Check whether the given DTX is resent one or not.
 *
 * \param coh		[IN]	Container open handle.
 * \param oid		[IN]	Pointer to the object ID.
 * \param xid		[IN]	Pointer to the DTX identifier.
 * \param dkey_hash	[IN]	The hashed dkey.
 * \param punch		[IN]	For punch operation or not.
 * \param epoch		[IN,OUT] Pointer to current epoch, if it is zero and
 *				 if the DTX exists, then the DTX's epoch will
 *				 be saved in it.
 *
 * \return		DTX_ST_PREPARED	means that the DTX has been 'prepared',
 *					so the local modification has been done
 *					on related replica(s).
 *			DTX_ST_COMMITTED means the DTX has been committed.
 *			-DER_MISMATCH	means that the DTX has ever been
 *					processed with different epoch.
 *			Other negative value if error.
 */
int
vos_dtx_check_resend(daos_handle_t coh, daos_unit_oid_t *oid,
		     struct dtx_id *dti, uint64_t dkey_hash,
		     bool punch, daos_epoch_t *epoch);

/**
 * Check the specified DTX's persistent status.
 *
 * \param coh		[IN]	Container open handle.
 * \param xid		[IN]	Pointer to the DTX identifier.
 *
 * \return		DTX_ST_PREPARED	means that the DTX has been 'prepared',
 *					so the local modification has been done
 *					on related replica(s). If all replicas
 *					have 'prepared', then the whole DTX is
 *					committable.
 *			DTX_ST_COMMITTED means the DTX has been committed.
 *			Negative value if error.
 */
int
vos_dtx_check(daos_handle_t coh, struct dtx_id *dti);

/**
 * Commit the specified DTXs.
 *
 * \param coh	[IN]	Container open handle.
 * \param dtis	[IN]	The array for DTX identifiers to be committed.
 * \param count [IN]	The count of DTXs to be committed.
 *
 * \return		Positive value to ask the caller to aggregate
 *			some old DTXs.
 * \return		Zero on success (no additional action required).
 * \return		Negative value if error.
 */
int
vos_dtx_commit(daos_handle_t coh, struct dtx_id *dtis, int count);

/**
 * Abort the specified DTXs.
 *
 * \param coh	[IN]	Container open handle.
 * \param epoch	[IN]	The max epoch for the DTX to be aborted.
 * \param dtis	[IN]	The array for DTX identifiers to be aborted.
 * \param count [IN]	The count of DTXs to be aborted.
 *
 * \return		Zero on success, negative value if error.
 */
int
vos_dtx_abort(daos_handle_t coh, daos_epoch_t epoch, struct dtx_id *dtis,
	      int count);

/**
 * Aggregate the committed DTXs.
 *
 * \param coh	[IN]	Container open handle.
 *
 * \return		Zero on success, negative value if error.
 */
int
vos_dtx_aggregate(daos_handle_t coh);

/**
 * Query the container's DTXs information.
 *
 * \param coh	[IN]	Container open handle.
 * \param stat	[OUT]	The structure to hold the DTXs information.
 */
void
vos_dtx_stat(daos_handle_t coh, struct dtx_stat *stat);

/**
 * Mark the object has been synced at the specified epoch.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	The object ID.
 * \param epoch	[IN]	The epoch against that we sync DTXs for the object.
 *
 * \return	Zero on success, negative value if error.
 */
int
vos_dtx_mark_sync(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch);

/**
 * Establish the indexed committed DTX table in DRAM.
 *
 * \param coh	[IN]		Container open handle.
 * \param hint	[IN,OUT]	Pointer to the address (offset in SCM) that
 *				contains committed DTX entries to be handled.
 *
 * \return	Zero on success, need further re-index.
 *		Positive, re-index is completed.
 *		Negative value if error.
 */
int
vos_dtx_cmt_reindex(daos_handle_t coh, void *hint);

/**
 * Cleanup current DTX handle.
 *
 * \param dth	[IN]	Pointer to the DTX handle.
 */
void
vos_dtx_handle_cleanup(struct dtx_handle *dth);

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
 * Kill a VOS pool before destroy
 * It deletes SPDK blob of this pool and detaches it from VOS GC
 *
 * \param uuid	[IN]	Pool UUID
 * \param force [IN]	Delete blob even if it has open refcount
 *
 * \return		Zero on success, negative value if error
 */
int
vos_pool_kill(uuid_t uuid, bool force);

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
 * \param epr	  [IN]		The epoch range of aggregation
 *
 * \return			Zero on success, negative value if error
 */
int
vos_aggregate(daos_handle_t coh, daos_epoch_range_t *epr);

/**
 * Discards changes in all epochs with the epoch range \a epr
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
 *
 * \param coh		[IN]	Container open handle
 * \param epr		[IN]	The epoch range to discard
 *				keys to discard
 *
 * \return			Zero on success, negative value if error
 */
int
vos_discard(daos_handle_t coh, daos_epoch_range_t *epr);

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
	      d_sg_list_t *sgls);


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
	       uint32_t pm_ver, daos_key_t *dkey, unsigned int iod_nr,
	       daos_iod_t *iods, d_sg_list_t *sgls);

/**
 * Punch an object, or punch a dkey, or punch an array of akeys under a akey.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID, the full object will be punched if \a dkey
 *			and \a akeys are not provided.
 * \param epoch	[IN]	Epoch for the punch.
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param flags [IN]	Object punch flags, VOS_OF_REPLAY_PC is the only
 *			currently supported flag.
 * \param dkey	[IN]	Optional, the dkey will be punched if \a akeys is not
 *			provided.
 * \param akey_nr [IN]	Number of akeys in \a akeys.
 * \param akeys [IN]	Array of akeys to be punched.
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_punch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      uint32_t pm_ver, uint32_t flags, daos_key_t *dkey,
	      unsigned int akey_nr, daos_key_t *akeys, struct dtx_handle *dth);

/**
 * Delete an object, this object is unaccessible at any epoch after deletion.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	ID of the object being deleted
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_delete(daos_handle_t coh, daos_unit_oid_t oid);

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
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		 daos_handle_t *ioh, struct dtx_handle *dth);

/**
 * Finish the current update and release the responding resources.
 *
 * \param ioh	[IN]	The I/O handle created by \a vos_update_begin
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param dkey	[IN]	Distribution key.
 * \param err	[IN]	Errno of the current update, zero if there is no error.
 *			All updates will be dropped if this function is called
 *			for \a vos_update_begin with a non-zero error code.
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_update_end(daos_handle_t ioh, uint32_t pm_ver, daos_key_t *dkey, int err,
	       struct dtx_handle *dth);

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
 * \param type  [IN]	Type of iterator
 * \param param	[IN]	Parameters for the iterator.
 *			For standalone iterator types,  param::ip_ih is
 *			DAOS_HDL_INVAL:
 *			- VOS_ITER_COUUID : param::ip_hdl is pool open handle
 *
 *			- VOS_ITER_OBJ	  : param::ip_hdl is container handle
 *
 *			- VOS_ITER_DKEY	  : param::ip_hdl is container handle,
 *					    param::ip_oid is ID of KV object.
 *					    param::ip_akey is akey condition
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
 *
 *			For nested iterator:
 *			- VOS_ITER_COUUID and VOS_ITER_OBJ are unsupported
 *
 *			- VOS_ITER_DKEY	  : param::ip_ih is VOS_ITER_OBJ handle
 *					    param::ip_oid is ID of KV object.
 *					    param::ip_akey is akey condition
 *
 *			- VOS_ITER_AKEY	  : param::ip_ih is VOS_ITER_DKEY handle
 *
 *			- VOS_ITER_RECX	  : param::ip_ih is VOS_ITER_AKEY handle
 *			- VOS_ITER_SINGLE
 *
 *			Nested iterators use the subtree referenced by the
 *			current cursor of the parent iterator.  Epoch range
 *			is inherited from that entry.
 *
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
 * \param entry [OUT]	Returned data entry for the current cursor
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
 * Copy out the data fetched by vos_iter_fetch()
 *
 * \param ih	[IN]	Iterator handle
 * \param entry [IN]	Data entry for the current cursor
 * \param iov_out [OUT]	Buffer for holding the entry data
 *
 * \return		Zero on success
 *			-DER_NONEXIST if no more entry
 *			negative value if error
 */
int
vos_iter_copy(daos_handle_t ih, vos_iter_entry_t *entry,
	      d_iov_t *iov_out);

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
 * Iterate VOS entries (i.e., containers, objects, dkeys, etc.) and call \a
 * cb(\a arg) for each entry.
 *
 * If \a cb returns a nonzero (either > 0 or < 0) value that is not
 * -DER_NONEXIST, this function stops the iteration and returns that nonzero
 * value from \a cb. If \a cb returns -DER_NONEXIST, this function completes
 * the iteration and returns 0. If \a cb returns 0, the iteration continues.
 *
 * \param[in]		param		iteration parameters
 * \param[in]		type		entry type of starting level
 * \param[in]		recursive	iterate in lower level recursively
 * \param[in]		anchors		array of anchors, one for each
 *					iteration level
 * \param[in]		cb		iteration callback
 * \param[in]		arg		callback argument
 *
 * \retval		0	iteration complete
 * \retval		> 0	callback return value
 * \retval		-DER_*	error (but never -DER_NONEXIST)
 */
int
vos_iterate(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
	    struct vos_iter_anchors *anchors, vos_iter_cb_t cb, void *arg);

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
 * If more than one entity is being queried, the innermost entry must exist.
 * For example, if a user requests DAOS_GET_DKEY and DAOS_GET_RECX and no
 * recx exists for a matched dkey, the search will try the next dkey until
 * a recx is found or no more dkeys exist in which case -DER_NONEXIST is
 * returned.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object id
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
 *
 * \return
 *			0		Success
 *			-DER_NO_HDL	Invalid handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NONEXIST	No suitable key/recx found
 */
int
vos_obj_query_key(daos_handle_t coh, daos_unit_oid_t oid, uint32_t flags,
		  daos_epoch_t epoch, daos_key_t *dkey, daos_key_t *akey,
		  daos_recx_t *recx);

/** Return constants that can be used to estimate the metadata overhead
 *  in persistent memory on-disk format.
 *
 *  \param alloc_overhead[IN]	Expected allocation overhead
 *  \param tclass[IN]		The type of tree to query
 *  \param ofeat[IN]		Relevant object features
 *  \param ovhd[IN,OUT]		Returned overheads
 *
 *  \return 0 on success, error otherwise.
 */
int
vos_tree_get_overhead(int alloc_overhead, enum VOS_TREE_CLASS tclass,
		      uint64_t ofeat, struct daos_tree_overhead *ovhd);

/** Return the size of the pool metadata in persistent memory on-disk format */
int
vos_pool_get_msize(void);

/** Return the cutoff size for SCM allocation.  Larger blocks are allocated to
 *  NVME.
 */
int
vos_pool_get_scm_cutoff(void);

enum vos_pool_opc {
	/** reset pool GC statistics */
	VOS_PO_CTL_RESET_GC,
	/** force VEA flush */
	VOS_PO_CTL_VEA_FLUSH,
};

/**
 * control ephemeral status of pool, see \a vos_pool_opc, this function is
 * mostly for debug & test
 */
int
vos_pool_ctl(daos_handle_t poh, enum vos_pool_opc opc);

int
vos_gc_run(int *credits);
int
vos_gc_pool(daos_handle_t poh, int *credits);

enum vos_cont_opc {
	/** reset HAE (Highest Aggregated Epoch) **/
	VOS_CO_CTL_RESET_HAE,
	/** abort VOS aggregation **/
	VOS_CO_CTL_ABORT_AGG,
};

/**
 * Set various vos container state, see \a vos_cont_opc.
 */
int
vos_cont_ctl(daos_handle_t coh, enum vos_cont_opc opc);

#endif /* __VOS_API_H */
