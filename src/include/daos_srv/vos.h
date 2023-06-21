/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

/** Initialize the vos reserve/cancel related fields in dtx handle
 *
 * \param dth	[IN]	The dtx handle
 *
 * \return	0 on success
 *		-DER_NOMEM on failure
 */
int
vos_dtx_rsrvd_init(struct dtx_handle *dth);

/** Finalize the vos reserve/cancel related fields in dtx handle
 *
 * \param dth	[IN]	The dtx handle
 */
void
vos_dtx_rsrvd_fini(struct dtx_handle *dth);

/**
 * Generate DTX entry for the given DTX, and attach it to the DTX handle.
 *
 * \param dth		[IN]	The dtx handle
 * \param persistent	[IN]	Save the DTX entry in persistent storage if set.
 * \param exist		[IN]	Related DTX entry exists or not.
 */
int
vos_dtx_attach(struct dtx_handle *dth, bool persistent, bool exist);

/**
 * Detach the DTX entry from the DTX handle.
 *
 * \param dth		[IN]	The dtx handle
 */
void
vos_dtx_detach(struct dtx_handle *dth);

/**
 * Check whether DTX entry attached to the DTX handle is still valid or not.
 *
 * \param dth		[IN]	The dtx handle
 *
 * \return		The DTX entry status.
 */
int
vos_dtx_validation(struct dtx_handle *dth);

/**
 * Check the specified DTX's status, and related epoch, pool map version
 * information if required.
 *
 * \param coh		[IN]	Container open handle.
 * \param dti		[IN]	Pointer to the DTX identifier.
 * \param epoch		[IN,OUT] Pointer to current epoch, if it is zero and
 *				 if the DTX exists, then the DTX's epoch will
 *				 be saved in it.
 * \param pm_ver	[OUT]	Hold the DTX's pool map version.
 * \param mbs		[OUT]	Pointer to the DTX participants information.
 * \param dck		[OUT]	Pointer to the key for CoS cache.
 * \param for_refresh	[IN]	It is for DTX_REFRESH or not.
 *
 * \return		DTX_ST_PREPARED	means that the DTX has been 'prepared',
 *					so the local modification has been done
 *					on related replica(s).
 *			DTX_ST_COMMITTED means the DTX has been committed.
 *			DTX_ST_COMMITTABLE means that the DTX is committable,
 *					   but not real committed.
 *			DTX_ST_CORRUPTED means the DTX entry is corrupted.
 *			-DER_MISMATCH	means that the DTX has ever been
 *					processed with different epoch.
 *			-DER_AGAIN means DTX re-index is in processing, not sure
 *				   about the existence of the DTX entry, need to
 *				   retry sometime later.
 *			DTX_ST_INITED	means that the DTX is just initialized but not prepared.
 *			Other negative value if error.
 */
int
vos_dtx_check(daos_handle_t coh, struct dtx_id *dti, daos_epoch_t *epoch,
	      uint32_t *pm_ver, struct dtx_memberships **mbs, struct dtx_cos_key *dck,
	      bool for_refresh);

/**
 * Load participants information for the given DTX.
 *
 * \param coh		[IN]	Container open handle.
 * \param dti		[IN]	Pointer to the DTX identifier.
 * \param mbs		[OUT]	Pointer to the DTX participants information.
 *
 * \return		Zero on success, negative value if error.
 */
int
vos_dtx_load_mbs(daos_handle_t coh, struct dtx_id *dti, struct dtx_memberships **mbs);

/**
 * Commit the specified DTXs.
 *
 * \param coh	[IN]	Container open handle.
 * \param dtis	[IN]	The array for DTX identifiers to be committed.
 * \param count [IN]	The count of DTXs to be committed.
 * \param rm_cos [OUT]	The array for whether remove entry from CoS cache.
 *
 * \return		Negative value if error.
 * \return		Others are for the count of committed DTXs.
 */
int
vos_dtx_commit(daos_handle_t coh, struct dtx_id dtis[], int count, bool rm_cos[]);

/**
 * Abort the specified DTXs.
 *
 * \param coh	[IN]	Container open handle.
 * \param dti	[IN]	The DTX identifiers to be aborted.
 * \param epoch	[IN]	The max epoch for the DTX to be aborted.
 *
 * \return		Zero on success, negative value if error.
 */
int
vos_dtx_abort(daos_handle_t coh, struct dtx_id *dti, daos_epoch_t epoch);

/**
 * Set flags on the active DTXs.
 *
 * \param coh	[IN]	Container open handle.
 * \param dtis	[IN]	The array for DTX identifiers to be set.
 * \param count [IN]	The count of DTXs to be set.
 * \param flags [IN]	The flags for the DTXs.
 *
 * \return		Zero on success, negative value if error.
 */
int
vos_dtx_set_flags(daos_handle_t coh, struct dtx_id dtis[], int count, uint32_t flags);

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
 * Query the container's DTXs statistics information.
 *
 * \param coh	[IN]	Container open handle.
 * \param stat	[OUT]	The structure to hold the DTXs information.
 * \param flags	[IN]	The flags for stat operation.
 */
void
vos_dtx_stat(daos_handle_t coh, struct dtx_stat *stat, uint32_t flags);

/**
 * Set the DTX committable as committable.
 *
 * \param dth	[IN]	Pointer to the DTX handle.
 */
void
vos_dtx_mark_committable(struct dtx_handle *dth);

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
 *
 * \return	Zero on success, need further re-index.
 *		Positive, re-index is completed.
 *		Negative value if error.
 */
int
vos_dtx_cmt_reindex(daos_handle_t coh);

/**
 * Cleanup local DTX when local modification failed.
 *
 * \param dth	[IN]	The DTX handle.
 * \param unpin	[IN]	unpin the DTX entry or not.
 */
void
vos_dtx_cleanup(struct dtx_handle *dth, bool unpin);

/**
 * Reset DTX related cached information in VOS.
 *
 * \param coh	[IN]	Container open handle.
 * \param force	[IN]	Reset all DTX tables by force.
 *
 * \return	Zero on success, negative value if error.
 */
int
vos_dtx_cache_reset(daos_handle_t coh, bool force);

/**
 * Initialize the environment for a VOS instance
 * Must be called once before starting a VOS instance
 *
 * NB: Required only when using VOS as a standalone library.
 *
 * \param db_path [IN]	path for system DB that stores NVMe metadata
 *
 * \return		Zero on success, negative value if error
 */
int
vos_self_init(const char *db_path, bool use_sys_db, int tgt_id);

/**
 * Finalize the environment for a VOS instance
 * Must be called for clean up at the end of using a vos instance
 *
 * NB: Needs to be called only when VOS is used as a standalone library.
 */
void
vos_self_fini(void);

/**
 * Versioning Object Storage Pool (VOSP)
 * A VOSP creates and manages a versioned object store on a local
 * storage device. The capacity of an OSP is determined
 * by the capacity of the underlying storage device
 */

/**
 * Create a Versioning Object Storage Pool (VOSP), and open it if \a poh is not
 * NULL
 *
 * \param path	[IN]	Path of the memory pool
 * \param uuid	[IN]    Pool UUID
 * \param scm_sz [IN]	Size of SCM for the pool
 * \param blob_sz[IN]	Size of blob for the pool
 * \param wal_sz [IN]	Size of WAL blob for the pool
 * \param flags [IN]	Pool open flags (see vos_pool_open_flags)
 * \param poh	[OUT]	Returned pool handle if not NULL
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_create_ex(const char *path, uuid_t uuid, daos_size_t scm_sz,
		   daos_size_t blob_sz, daos_size_t wal_sz,
		   unsigned int flags, daos_handle_t *poh);
/**
 * Create a Versioning Object Storage Pool (VOSP), and open it if \a poh is not
 * NULL
 *
 * \param path	[IN]	Path of the memory pool
 * \param uuid	[IN]    Pool UUID
 * \param scm_sz [IN]	Size of SCM for the pool
 * \param blob_sz[IN]	Size of blob for the pool
 * \param flags [IN]	Pool open flags (see vos_pool_open_flags)
 * \param poh	[OUT]	Returned pool handle if not NULL
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t scm_sz,
		daos_size_t blob_sz, unsigned int flags, daos_handle_t *poh);

/**
 * Kill a VOS pool before destroy
 * It deletes SPDK blob of this pool and detaches it from VOS GC
 *
 * \param uuid		[IN]	Pool UUID
 * \param flags [IN]	Pool open flags (see vos_pool_open_flags)
 *
 * \return		Zero on success, negative value if error
 */
int
vos_pool_kill(uuid_t uuid, unsigned int flags);

/**
 * Destroy a Versioned Object Storage Pool (VOSP)
 *
 * \param path	[IN]	Path of the memory pool
 * \param uuid	[IN]	Pool UUID
 * \param flags [IN]	Pool open flags (see vos_pool_open_flags)
 *
 * \return		Zero on success, negative value if error
 */
int
vos_pool_destroy_ex(const char *path, uuid_t uuid, unsigned int flags);

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
 * Open a Versioning Object Storage Pool (VOSP)
 *
 * \param path	 [IN]	Path of the memory pool
 * \param uuid	 [IN]	Pool UUID
 * \param flags  [IN]	Pool open flags (see vos_pool_open_flags)
 * \param poh	 [OUT]	Returned pool handle
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_open(const char *path, uuid_t uuid, unsigned int flags,
	      daos_handle_t *poh);

/** Upgrade the vos pool version
 *
 * \param poh		[IN]	Container open handle
 * \param version	[IN]	pool version
 *
 * \return	0 on success, error otherwise
 */
int
vos_pool_upgrade(daos_handle_t poh, uint32_t version);

/**
 * Extended vos_pool_open() with an additional 'metrics' parameter to VOS telemetry.
 */
int
vos_pool_open_metrics(const char *path, uuid_t uuid, unsigned int flags, void *metrics,
		      daos_handle_t *poh);

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
 * Query pool space by pool UUID
 *
 * \param pool_id [IN]	Pool UUID
 * \param vps     [OUT]	Returned pool space info
 *
 * \return		Zero		: success
 *			-DER_NONEXIST	: pool isn't opened
 *			-ve		: error
 */
int
vos_pool_query_space(uuid_t pool_id, struct vos_pool_space *vps);

/**
 * Set aside additional "system reserved" space in pool SCM and NVMe
 * (additive to any existing reserved space by vos)
 *
 * \param poh		[IN]	Pool open handle
 * \param space_sys	[IN]	Array of sizes in bytes, indexed by media type
 *				(DAOS_MEDIA_SCM and DAOS_MEDIA_NVME)
 *
 * \return		Zero		: success
 *			-DER_NO_HDL	: invalid pool handle
 *			-DER_INVAL	: space_sys is NULL
 */
int
vos_pool_space_sys_set(daos_handle_t poh, daos_size_t *space_sys);

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

enum {
	VOS_AGG_FL_FORCE_SCAN	= (1UL << 0),	/* Scan all obj/dkey/akeys */
	VOS_AGG_FL_FORCE_MERGE	= (1UL << 1),	/* Merge all coalesce-able EV records */
};

/**
 * Aggregates all epochs within the epoch range \a epr.
 * Data in all these epochs will be aggregated to the last epoch
 * \a epr::epr_hi, aggregated epochs will be discarded except the last one,
 * which is kept as aggregation result.
 *
 * \param coh	  [IN]		Container open handle
 * \param epr	  [IN]		The epoch range of aggregation
 * \param yield_func [IN]	Pointer to customized yield function
 * \param yield_arg  [IN]	Argument of yield function
 * \param flags      [IN]	Aggregation flags
 *
 * \return			Zero on success, negative value if error
 */
int
vos_aggregate(daos_handle_t coh, daos_epoch_range_t *epr,
	      int (*yield_func)(void *arg), void *yield_arg, uint32_t flags);

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
 * \param oidp		[IN]	Optional oid for oid specific discard.  Aggregation should be
 *				disabled before calling this function in this mode.  This simply
 *				removes values in the specified epoch range.  It doesn't discard
 *				any ilog entries. The expectation is the removed data will
 *				be replaced at the same epoch (or snapshot).  If that is not the
 *				case, any orphaned entries will be inaccessible.
 * \param epr		[IN]	The epoch range to discard keys to discard
 * \param yield_func	[IN]	Pointer to customized yield function
 * \param yield_arg	[IN]	Argument of yield function
 *
 * \return			Zero on success, negative value if error
 */
int
vos_discard(daos_handle_t coh, daos_unit_oid_t *oidp, daos_epoch_range_t *epr,
	    int (*yield_func)(void *arg), void *yield_arg);

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
 * \param epoch	[IN]	Epoch for the fetch.
 * \param flags	[IN]	VOS flags
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
	      uint64_t flags, daos_key_t *dkey, unsigned int iod_nr,
	      daos_iod_t *iods, d_sg_list_t *sgls);

/**
 * Fetch values for the given keys and their indices.
 * Output buffer must be provided in \a sgl.  For zero copy, use
 * vos_fetch_begin/end.
 *
 * TODO: add more detail descriptions for punched or missing records.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the fetch.  Ignored if a valid DTX handle
 *			is provided.
 * \param flags	[IN]	Fetch flags
 * \param dkey	[IN]	Distribution key.
 * \param iod_nr [IN]	Number of I/O descriptors in \a iods.
 * \param iods	[IN/OUT]
 *			Array of I/O descriptors. The returned record
 *			sizes are also stored in this parameter.
 * \param sgls	[OUT]	Scatter/gather list to store the returned record values
 *			or value addresses.
 * \param dth	[IN]	Optional dtx handle
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_fetch_ex(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 uint64_t flags, daos_key_t *dkey, unsigned int iod_nr,
		 daos_iod_t *iods, d_sg_list_t *sgls, struct dtx_handle *dth);

/**
 * Update records for the specified object.
 * If input buffer is not provided in \a sgl, then this function returns
 * the new allocated addresses to store the records, upper layer can
 * directly write data into these addresses (rdma mode).
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the update. Ignored if a DTX handle
 *			is provided.
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param flags	[IN]	Update flags
 * \param dkey	[IN]	Distribution key.
 * \param iod_nr [IN]	Number of I/O descriptors in \a iods.
 * \param iods	[IN]	Array of I/O descriptors. If \a flags includes
 *			VOS_OF_EC, then the iod_recxs field of every single
 *			value iod in \a iods must contain the "gsize" instead
 *			of a memory address.
 * \param iods_csums [IN]
 *			Array of iod_csums (1 for each iod). Will be NULL
 *			if csums are disabled.
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
	       uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
	       unsigned int iod_nr, daos_iod_t *iods,
	       struct dcs_iod_csums *iods_csums, d_sg_list_t *sgls);


/**
 * Update records for the specified object.
 * If input buffer is not provided in \a sgl, then this function returns
 * the new allocated addresses to store the records, upper layer can
 * directly write data into these addresses (rdma mode).
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the update. Ignored if a DTX handle
 *			is provided.
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param flags	[IN]	Update flags
 * \param dkey	[IN]	Distribution key.
 * \param iod_nr [IN]	Number of I/O descriptors in \a iods.
 * \param iods	[IN]	Array of I/O descriptors. If \a flags includes
 *			VOS_OF_EC, then the iod_recxs field of every single
 *			value iod in \a iods must contain the "gsize" instead
 *			of a memory address.
 * \param iods_csums [IN]
 *			Array of iod_csums (1 for each iod). Will be NULL
 *			if csums are disabled.
 * \param sgls	[IN/OUT]
 *			Scatter/gather list to pass in record value buffers,
 *			if caller sets the input buffer size only without
 *			providing input buffers, then VOS will allocate spaces
 *			for the records and return addresses of them, so upper
 *			layer stack can transfer data via rdma.
 * \param dth	[IN]	Optional transaction handle
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_update_ex(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		  uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
		  unsigned int iod_nr, daos_iod_t *iods,
		  struct dcs_iod_csums *iods_csums, d_sg_list_t *sgls,
		  struct dtx_handle *dth);

/**
 * Remove all array values within the specified range.  If the specified
 * extent and epoch range includes partial extents, the function will
 * fail and no changes will be made.
 *
 * \param[in]	coh	Container open handle
 * \param[in]	oid	object ID
 * \param[in]	epr	Epoch range
 * \param[in]	dkey	Distribution key
 * \param[in]	akey	Attribute key
 * \param[in]	recx	Extent range to remove
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_array_remove(daos_handle_t coh, daos_unit_oid_t oid,
		     const daos_epoch_range_t *epr, const daos_key_t *dkey,
		     const daos_key_t *akey, const daos_recx_t *recx);

/**
 * Punch an object, or punch a dkey, or punch an array of akeys under a akey.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID, the full object will be punched if \a dkey
 *			and \a akeys are not provided.
 * \param epoch	[IN]	Epoch for the punch. Ignored if a DTX handle
 *			is provided.
 * \param pm_ver [IN]   Pool map version for this update, which will be
 *			used during rebuild.
 * \param flags [IN]	Object punch flags, including VOS_OF_REPLAY_PC and
 *			conditional flags
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
	      uint32_t pm_ver, uint64_t flags, daos_key_t *dkey,
	      unsigned int akey_nr, daos_key_t *akeys, struct dtx_handle *dth);

/**
 * Delete an object, this object is unaccessible at any epoch after deletion.
 * This function is not part of DAOS data model API, it is only used by data
 * migration protocol.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	ID of the object being deleted
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_delete(daos_handle_t coh, daos_unit_oid_t oid);

/**
 * Delete an object in OI table, this object is unaccessible at any epoch after deletion.
 * This function is not part of DAOS data model API, it is only used by data migration protocol.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	ID of the object being deleted
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_delete_ent(daos_handle_t coh, daos_unit_oid_t oid);

/**
 * Delete a dkey or akey, the key is unaccessible at any epoch after deletion.
 * This function is not part of DAOS data model API, it is only used by data
 * migration protocol and system database.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	ID of the object
 * \param dkey	[IN]	dkey being deleted if \a akey is NULL
 * \param akey	[IN]	Optional, akey being deleted
 *
 * \return		Zero on success, negative value if error
 */
int
vos_obj_del_key(daos_handle_t coh, daos_unit_oid_t oid, daos_key_t *dkey,
		daos_key_t *akey);

/**
 * I/O APIs
 */
/**
 * Find and return I/O source buffers for the data of the specified
 * arrays of the given object. The caller can directly use these buffers
 * for RMA read.
 *
 * The upper layer must explicitly call \a vos_fetch_end to finalize the
 * I/O and release resources.
 *
 * TODO: add more detail descriptions for punched or missing records.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the fetch. Ignored if a DTX handle
 *			is provided.
 * \param dkey	[IN]	Distribution key.
 * \param nr	[IN]	Number of I/O descriptors in \a ios.
 * \param iods	[IN/OUT]
 *			Array of I/O descriptors. The returned record
 *			sizes are also restored in this parameter.
 * \param vos_flags [IN]
 *			VOS fetch flags, VOS cond flags, VOS_OF_FETCH_SIZE_ONLY
 *			or VOS_OF_FETCH_RECX_LIST.
 * \param shadows [IN]	Optional shadow recx/epoch lists, one for each iod.
 *			data of extents covered by these should not be returned
 *			by fetch function. Only used for EC obj degraded fetch.
 * \param ioh	[OUT]	The returned handle for the I/O.
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_fetch_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		daos_key_t *dkey, unsigned int nr,
		daos_iod_t *iods, uint32_t vos_flags,
		struct daos_recx_ep_list *shadows, daos_handle_t *ioh,
		struct dtx_handle *dth);

/**
 * Finish the fetch operation and release the responding resources.
 *
 * \param ioh	[IN]	The I/O handle created by \a vos_fetch_begin
 * \param size	[OUT]	The total IO size for the fetch.
 * \param err	[IN]	Errno of the current fetch, zero if there is no error.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_fetch_end(daos_handle_t ioh, daos_size_t *size, int err);

/**
 * Prepare IO sink buffers for the specified arrays of the given
 * object. The caller can directly use those buffers for RMA write.
 *
 * The upper layer must explicitly call \a vos_update_end to finalize the
 * ZC I/O and release resources.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the update. Ignored if a DTX handle
 *			is provided.
 * \param flags [IN]	conditional flags
 * \param dkey	[IN]	Distribution key.
 * \param iod_nr	[IN]	Number of I/O descriptors in \a iods.
 * \param iods	[IN]	Array of I/O descriptors.
 * \param iods	[IN]	Array of I/O descriptors. If \a flags includes
 *			VOS_OF_EC, then the iod_recxs field of every single
 *			value iod in \a iods must contain the "gsize" instead
 *			of a memory address.
 * \param iods_csums [IN]
 *			Array of iod_csums (1 for each iod). Will be NULL
 *			if csums are disabled.
 * \param dedup_th [IN]	Deduplication threshold size
 * \param ioh	[OUT]	The returned handle for the I/O.
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_update_begin(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		 uint64_t flags, daos_key_t *dkey, unsigned int iod_nr,
		 daos_iod_t *iods, struct dcs_iod_csums *iods_csums,
		 uint32_t dedup_th, daos_handle_t *ioh, struct dtx_handle *dth);

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
 * \param size	[OUT]	The total IO size for the update.
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_update_end(daos_handle_t ioh, uint32_t pm_ver, daos_key_t *dkey, int err,
	       daos_size_t *size, struct dtx_handle *dth);

/**
 * Renew the epoch for the update handle.
 *
 * \param ioh	[IN]	The I/O handle for update.
 * \param dth	[IN]	Pointer to the DTX handle.
 */
void
vos_update_renew_epoch(daos_handle_t ioh, struct dtx_handle *dth);

/**
 * Get the recx/epoch list.
 *
 * \param ioh	[IN]	The I/O handle.
 *
 * \return		recx/epoch list.
 */
struct daos_recx_ep_list *
vos_ioh2recx_list(daos_handle_t ioh);

/**
 * Get the I/O descriptor.
 *
 * \param ioh	[IN]	The I/O handle.
 *
 * \return		BIO IO descriptor
 */
struct bio_desc *
vos_ioh2desc(daos_handle_t ioh);

struct dcs_ci_list *
vos_ioh2ci(daos_handle_t ioh);

uint32_t
vos_ioh2ci_nr(daos_handle_t ioh);

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
 * Get the bulk handle associated with a given I/O descriptor.
 *
 * \param ioh		[IN]	The I/O handle
 * \param sgl_idx	[IN]	SGL index
 * \param iov_idx	[IN]	IOV index within the SGL
 * \param bulk_off	[OUT]	Bulk offset
 *
 * \return			Bulk handle
 */
void *
vos_iod_bulk_at(daos_handle_t ioh, unsigned int sgl_idx, unsigned int iov_idx,
		unsigned int *bulk_off);

void
vos_set_io_csum(daos_handle_t ioh, struct dcs_iod_csums *csums);

/**
 * VOS iterator APIs
 */
/**
 * Initialize an iterator for VOS
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
 * \param dth	[IN]	Pointer to the DTX handle.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_iter_prepare(vos_iter_type_t type, vos_iter_param_t *param,
		 daos_handle_t *ih, struct dtx_handle *dth);

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
 * \param ih	[IN]		Iterator handle.
 * \param anchor[IN,OUT]	Optional, position cursor to move to. May
 *				be modified if entries are skipped during
 *				probe.
 *
 * \return		zero if there is an entry at/after @anchor
 *			-DER_NONEXIST if no more entry
 *			negative value if error
 */
int
vos_iter_probe(daos_handle_t ih, daos_anchor_t *anchor);

/**
 * Set the iterator cursor to the specified position \a anchor if it is
 * not NULL, otherwise move the cursor to the begin of the iterator.
 * This function must be called before using vos_iter_next or vos_iter_fetch.
 *
 * \param ih	[IN]		Iterator handle.
 * \param anchor[IN,OUT]	Optional, position cursor to move to. May
 *				be modified if entries are skipped during
 *				probe.
 * \param flags	[IN]		Probe flags (See VOS_ITER_PROBE*)
 *
 * \return		zero if there is an entry at/after @anchor
 *			-DER_NONEXIST if no more entry
 *			negative value if error
 */
int
vos_iter_probe_ex(daos_handle_t ih, daos_anchor_t *anchor, uint32_t flags);

/**
 * Move forward the iterator cursor.
 *
 * \param ih	[IN]	Iterator handle.
 * \param anchor[OUT]	Optional, current anchor. May be modified if entries
 *			are skipped.
 *
 * \return		Zero if there is an available entry
 *			-DER_NONEXIST if no more entry
 *			negative value if error
 */
int
vos_iter_next(daos_handle_t ih, daos_anchor_t *anchor);

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
 * Process the operation for the current cursor.
 *
 * \param ih	[IN]	Iterator open handle.
 * \param op	[IN]	op code to process. See vos_iter_proc_op_t
 * \param args	[IN/OUT]
 *			Optional, Provide additional hints while
 *			deletion to handle special cases.
 *			for example return record of this node
 *			to user instead of deleting.
 *
 * \return		Zero on Success
 *			-DER_NONEXIST if cursor does
 *			not exist. negative value if error
 */
int
vos_iter_process(daos_handle_t ih, vos_iter_proc_op_t op, void *args);

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
 * When the callback executes code that may yield, in some cases, it needs to
 * verify the value or key it is operating on still exists before continuing.
 * This function will revalidate from the object layer down.   This is only
 * supported in conjunction with recursive vos_iterate from VOS_ITER_OBJ.
 *
 * \param[in]	ih	The iterator handle
 *
 * \return		0 if iterator value is valid
 *			VOS_ITER_* level that needs probe
 *			<0 on error
 */
int
vos_iter_validate(daos_handle_t ih);

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
 * \param[in]		pre_cb		pre subtree iteration callback
 * \param[in]		post_cb		post subtree iteration callback
 * \param[in]		arg		callback argument
 * \param[in]		dth		DTX handle
 *
 * \retval		0	iteration complete
 * \retval		> 0	callback return value
 * \retval		-DER_*	error (but never -DER_NONEXIST)
 */
int
vos_iterate(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
	    struct vos_iter_anchors *anchors, vos_iter_cb_t pre_cb,
	    vos_iter_cb_t post_cb, void *arg, struct dtx_handle *dth);

/**
 * Retrieve the largest or smallest integer DKEY, AKEY, and array offset from an
 * object. If object does not have an array value, 0 is returned in extent. User
 * has to specify what is being queried (dkey, akey, and/or recx) along with the
 * query type (max or min) in flags. If one of those is not provided the
 * function will fail. If the dkey or akey are not being queried, their values
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
 * The maximum write epoch for the object can be returned at the same time
 * as the min or max query. Alternatively, this API can be used to only query
 * the maximum write for an object if no flags are given.
 *
 * \param[in]	coh		Container open handle.
 * \param[in]	oid		Object id
 * \param[in]	flags		mask with the following options:
 *				DAOS_GET_DKEY, DAOS_GET_AKEY, DAOS_GET_RECX,
 *				DAOS_GET_MAX, DAOS_GET_MIN
 *				If the user isn't querying max_write, they must specify either
 *				DAOS_GET_MAX or DAOS_GET_MIN, exclusively. Anything else is
 *				an error.
 * \param[in,out]
 *		dkey		[in]: allocated integer dkey. User can provide the dkey
 *				if not querying the max or min dkey.
 *				[out]: max or min dkey (if flag includes dkey query).
 * \param[in,out]
 *		akey		[in]: allocated integer akey. User can provide the akey
 *				if not querying the max or min akey.
 *				[out]: max or min akey (if flag includes akey query).
 * \param[out]	recx		max or min offset in dkey/akey, and the size of the
 *				extent at the offset. If there are no visible array
 *				records, the size in the recx returned will be 0.
 * \param[out]	max_write	Optional: Returns max write epoch for object.
 * \param[in]	cell_size cell size for EC object, used to calculated the replicated
 *                      space address on parity shard.
 * \param[in]	stripe_size stripe size for EC object, used to calculated the replicated
 *                      space address on parity shard.
 * \param[in]	dth	Pointer to the DTX handle.
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
		  daos_recx_t *recx, daos_epoch_t *max_write, unsigned int cell_size,
		  uint64_t stripe_size, struct dtx_handle *dth);

/** Return constants that can be used to estimate the metadata overhead
 *  in persistent memory on-disk format.
 *
 *  \param alloc_overhead[IN]	Expected allocation overhead
 *  \param tclass[IN]		The type of tree to query
 *  \param otype[IN]		Relevant object features
 *  \param ovhd[IN,OUT]		Returned overheads
 *
 *  \return 0 on success, error otherwise.
 */
int
vos_tree_get_overhead(int alloc_overhead, enum VOS_TREE_CLASS tclass,
		      uint64_t otype, struct daos_tree_overhead *ovhd);

/** Return the size of the pool metadata in persistent memory on-disk format */
int
vos_pool_get_msize(void);

/** Return the size of the container metadata in persistent memory on-disk
 *  format
 */
int
vos_container_get_msize(void);

/** Return the cutoff size for SCM allocation.  Larger blocks are allocated to
 *  NVME.
 */
int
vos_pool_get_scm_cutoff(void);

enum vos_pool_opc {
	/** Reset pool GC statistics */
	VOS_PO_CTL_RESET_GC,
	/** Set pool tiering policy */
	VOS_PO_CTL_SET_POLICY,
	/** Set space reserve ratio for rebuild */
	VOS_PO_CTL_SET_SPACE_RB,
};

/**
 * control ephemeral status of pool, see \a vos_pool_opc, this function is
 * mostly for debug & test
 */
int
vos_pool_ctl(daos_handle_t poh, enum vos_pool_opc opc, void *param);

int
vos_gc_pool(daos_handle_t poh, int credits, int (*yield_func)(void *arg),
	    void *yield_arg);
int
vos_flush_pool(daos_handle_t poh, bool force, uint32_t nr_flush, uint32_t *nr_flushed);

bool
vos_gc_pool_idle(daos_handle_t poh);


enum vos_cont_opc {
	VOS_CO_CTL_DUMMY,
};

/**
 * Set various vos container state, see \a vos_cont_opc.
 */
int
vos_cont_ctl(daos_handle_t coh, enum vos_cont_opc opc);

uint64_t
vos_get_io_size(daos_handle_t ioh);

/**
 * Helper functions for dedup verify.
 */
int
vos_dedup_verify_init(daos_handle_t ioh, void *bulk_ctxt,
		      unsigned int bulk_perm);
int
vos_dedup_verify(daos_handle_t ioh);

struct sys_db *vos_db_get(void);

/* return sysdb pool uuid */
uuid_t *
vos_db_pool_uuid(void);

/**
 * Create the system DB in VOS
 * System DB is KV store that can support insert/delete/traverse
 * See \a sys_db for more details.
 */
int vos_db_init(const char *db_path);
int vos_db_init_ex(const char *db_path, const char *db_name, bool force_create,
		   bool destroy_db_on_fini);
void vos_db_fini(void);

typedef void (*vos_chkpt_update_cb_t)(void *arg, uint64_t commit_id, uint32_t used_blocks,
				      uint32_t total_blocks);
typedef void (*vos_chkpt_wait_cb_t)(void *arg, uint64_t chkpt_id, uint64_t *committed_id);
/**
 * Initialize checkpointing callbacks, retrieve the store.  Function will invoke commit_cb and
 * reserve_cb to initialize values.
 *
 * \param[in]	poh		Open pool handle
 * \param[in]	update_cb	Callback to invoke after wal changes
 * \param[in]	arg		Callback argument
 * \param[out]	store		Return the umem_store associated with the pool
 */
void
vos_pool_checkpoint_init(daos_handle_t poh, vos_chkpt_update_cb_t update_cb,
			 vos_chkpt_wait_cb_t wait_cb, void *arg, struct umem_store **store);

/**
 * Clears saved checkpoint callbacks to avoid any race on shutdown
 *
 * \param[in]	poh	Open pool handle
 */
void
vos_pool_checkpoint_fini(daos_handle_t poh);

/**
 * Returns true if checkpointing is needed for the pool
 *
 * \param[in]	poh	Pool open handle
 */
bool
vos_pool_needs_checkpoint(daos_handle_t poh);

/** Checkpoint the VOS pool
 *
 * \param[in] poh		Open vos pool handle
 * \param[in] chkpt_wait	Callback to wait for tx_id to commit
 * \param[in] arg		Argument to callback
 */
int
vos_pool_checkpoint(daos_handle_t poh);

/**
 * The following declarations are for checksum scrubbing functions. The function
 * types provide an interface for injecting dependencies into the
 * scrubber (srv_pool_scrub.c) from the schedule/ult management
 * so that it can be more easily tested without depending on the entire daos
 * engine to be running or waiting for schedules to run.
 */

struct cont_scrub {
	struct daos_csummer	*scs_cont_csummer;
	void			*scs_cont_src;
	daos_handle_t		 scs_cont_hdl;
	uuid_t			 scs_cont_uuid;
	bool			 scs_props_fetched;
};

/*
 * Because the scrubber operates at the pool level, it will need a way to
 * get some info for each container within the pool as it's scrubbed.
 */
typedef int(*sc_get_cont_fn_t)(uuid_t pool_uuid, uuid_t cont_uuid, void *arg,
			       struct cont_scrub *cont);
typedef void(*sc_put_cont_fn_t)(void *cont);
typedef bool(*sc_cont_is_stopping_fn_t)(void *cont);

typedef bool (*sc_is_idle_fn_t)();
typedef int (*sc_sleep_fn_t)(void *, uint32_t msec);
typedef int (*sc_yield_fn_t)(void *);
typedef int (*ds_pool_tgt_drain)(struct ds_pool *pool);

enum scrub_status {
	SCRUB_STATUS_UNKNOWN = 0,
	SCRUB_STATUS_RUNNING = 1,
	SCRUB_STATUS_NOT_RUNNING = 2,
};

struct scrub_ctx_metrics {
	struct d_tm_node_t	*scm_next_csum_scrub;
	struct d_tm_node_t	*scm_next_tree_scrub;
	struct d_tm_node_t	*scm_busy_time;
	struct d_tm_node_t	*scm_start;
	struct d_tm_node_t	*scm_last_duration;
	struct d_tm_node_t	*scm_csum_calcs;
	struct d_tm_node_t	*scm_csum_calcs_last;
	struct d_tm_node_t	*scm_csum_calcs_total;
	struct d_tm_node_t	*scm_bytes_scrubbed;
	struct d_tm_node_t	*scm_bytes_scrubbed_last;
	struct d_tm_node_t	*scm_bytes_scrubbed_total;
	struct d_tm_node_t	*scm_corruption;
	struct d_tm_node_t	*scm_corruption_total;
	struct d_tm_node_t	*scm_scrub_count;
	struct timespec		 scm_busy_start;

};

/* Scrub the pool */
struct scrub_ctx {
	/**
	 * Metrics gathered during scrubbing process
	 */
	struct scrub_ctx_metrics sc_metrics;

	struct dss_module_info	*sc_dmi;

	/**
	 * Pool
	 **/
	uuid_t			 sc_pool_uuid;
	daos_handle_t		 sc_vos_pool_hdl;
	struct ds_pool		*sc_pool; /* Used to get properties */
	uint32_t		 sc_pool_scrub_count;
	struct timespec		 sc_pool_start_scrub;
	int			 sc_pool_last_csum_calcs;
	int			 sc_pool_csum_calcs;
	uint64_t		 sc_bytes_scrubbed;
	uint32_t		 sc_pool_tgt_corrupted_detected;
	ds_pool_tgt_drain	 sc_drain_pool_tgt_fn;

	/**
	 * Container
	 **/
	/* callback function that will provide the csummer for the container */
	sc_get_cont_fn_t	 sc_cont_lookup_fn;
	sc_put_cont_fn_t	 sc_cont_put_fn;
	sc_cont_is_stopping_fn_t sc_cont_is_stopping_fn;
	struct cont_scrub	 sc_cont;
	uuid_t			 sc_cont_uuid;

	/**
	 * Object
	 */
	daos_unit_oid_t		 sc_cur_oid;
	daos_key_t		 sc_dkey;
	struct dcs_csum_info	*sc_csum_to_verify;
	daos_epoch_t		 sc_epoch;
	uint16_t		 sc_minor_epoch;
	daos_iod_t		 sc_iod;
	struct bio_iov		*sc_cur_biov;

	/* Current vos object iterator */
	daos_handle_t		 sc_vos_iter_handle;

	/* Schedule controlling function pointers and arg */
	sc_is_idle_fn_t		 sc_is_idle_fn;
	sc_sleep_fn_t		 sc_sleep_fn;
	sc_yield_fn_t		 sc_yield_fn;
	void			*sc_sched_arg;

	enum scrub_status        sc_status;
	uint8_t                  sc_cont_loaded : 1, /* Have all the containers been loaded */
	    sc_first_pass_done                  : 1; /* Is this the first pass of the scrubber */
};

/*
 * It is expected that the pool uuid/handle and any functional dependencies are
 * set in the scrubbing context. The container/object info should not be set.
 * This function will iterate over all of the containers in the pool and if
 * checksums are enabled on the pool, each object in the container will be
 * scrubbed.
 */
int vos_scrub_pool(struct scrub_ctx *ctx);

/*
 * A generic utility function that, given a start time, duration, number of
 * periods that can be processed, and the current period index, calculate how
 * long the caller should wait/sleep in order to fill the space of the current
 * period.
 *
 * Returns number of milliseconds to wait
 */
uint64_t
get_ms_between_periods(struct timespec start_time, struct timespec cur_time,
		       uint64_t duration_seconds, uint64_t periods_nr,
		       uint64_t per_idx);

/** Set the VOS portion of the anchor for a given dkey or akey
 *
 * \param[in]	coh	Container open handle
 * \param[in]	oid	Object ID
 * \param[in]	dkey	Distribution key
 * \param[in]	akey	Optional attribute key. If NULL, anchor is set using dkey
 * \param[out]	anchor	The anchor to set.
 *
 * \return 0 on success, error otherwise.
 *
 * If the tree containing the key doesn't exist, the anchor will be set anchor to EOF.
 */
int
vos_obj_key2anchor(daos_handle_t coh, daos_unit_oid_t oid, daos_key_t *dkey, daos_key_t *akey,
		   daos_anchor_t *anchor);

/**
 * Upgrade object layout version for the object
 * \param[in]	coh	Container open handle
 * \param[in]	oid	Object ID
 * \param[in]	layout_ver	the new layout version of the object.
 *
 * \return 0 on success, error otherwise.
 *
 */
int
vos_obj_layout_upgrade(daos_handle_t hdl, daos_unit_oid_t oid, uint32_t layout_ver);

/**
 * Init standalone VOS TLS.
 * \param[in]	tags
 */
int
vos_standalone_tls_init(int tags);

/**
 * Finish standalone VOS TLS.
 */
void
vos_standalone_tls_fini(void);

#endif /* __VOS_API_H */
