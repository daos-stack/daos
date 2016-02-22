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
 * (C) Copyright 2015, 2016 Intel Corporation.
 */
/**
 * This file describes the API for a versioning object store.
 * These APIs will help build a versioned store with
 * key-value and byte-array object types.
 * These APIs provide ways to create, delete, search and enumerate
 * multiversion concurrent key-value and byte-array objects.
 *
 * Author :  Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef __VOS_API_H
#define __VOS_API_H

#include <daos/daos_ev.h>
#include <daos/daos_types.h>
#include <daos_srv/vos_types.h>

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
 * \param ev	[IN]    Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_create(const char *path, uuid_t uuid, daos_size_t size,
		daos_handle_t *poh, daos_event_t *ev);

/**
 * Destroy a Versioned Object Storage Pool (VOSP)
 * The open handle will be invalidated after the destroy.
 *
 * \param poh	[IN]	Pool open handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_pool_destroy(daos_handle_t poh, daos_event_t *ev);

/**
 * Open a Versioning Object Storage Pool (VOSP), load its root object
 * and other internal data structures.
 *
 * \param path	[IN]	Path of the memory pool
 * \param uuid	[IN]    Pool UUID
 * \param poh	[OUT]	Returned pool handle
 * \param ev	[IN]    Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_open(const char *path, uuid_t uuid, daos_handle_t *poh,
	      daos_event_t *ev);

/**
 * Close a VOSP, all opened containers sharing this pool handle
 * will be revoked.
 *
 * \param poh	[IN]	Pool open handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return              Zero on success, negative value if error
 */
int
vos_pool_close(daos_handle_t poh, daos_event_t *ev);

/**
 * Query attributes and statistics of the current pool
 *
 * \param poh	[IN]	Pool open handle
 * \param pinfo	[OUT]	Returned pool attributes and stats info
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_pool_query(daos_handle_t poh, vos_pool_info_t *pinfo, daos_event_t *ev);

/**
 * Create a container within a VOSP
 *
 * \param poh	[IN]	Pool open handle
 * \param co_uuid
 *		[IN]	UUID for the new container
 * \param coh	[OUT]	Returned container handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_create(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh,
	      daos_event_t *ev);

/**
 * Destroy a container
 *
 * \param coh	[IN]	Container open handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_destroy(daos_handle_t coh, daos_event_t *ev);

/**
 * Open a container within a VOSP
 *
 * \param poh	[IN]	Pool open handle
 * \param co_uuid
 *		[IN]	Container uuid
 * \param mode	[IN]	open mode: rd-only, rdwr...
 * \param coh	[OUT]	Returned container handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_open(daos_handle_t poh, uuid_t co_uuid, daos_handle_t *coh,
	    daos_event_t *ev);

/**
 * Release container open handle
 *
 * \param coh	[IN]	container open handle
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_close(daos_handle_t coh, daos_event_t *ev);

/**
 * Query container information.
 *
 * \param coh	[IN]	Container open handle.
 * \param cinfo	[OUT]	Returned container attributes and other information.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_co_query(daos_handle_t coh, vos_co_info_t *cinfo, daos_event_t *ev);

/**
 * Flush changes in the specified epoch to storage
 *
 * \param coh	[IN]	Container open handle
 * \param epoch	[IN]	Epoch to flush
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_epoch_flush(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev);

/**
 * Aggregates all epochs within the epoch range \a epr.
 * Data in all these epochs will be aggregated to the last epoch
 * \a epr::epr_hi, aggregated epochs will be discarded except the last one,
 * which is kept as aggregation result.
 *
 * \param coh	[IN]	Container open handle
 * \param epr	[IN]	The epoch range of aggregation
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_epoch_aggregate(daos_handle_t coh, daos_epoch_range_t *epr,
		    daos_event_t *ev);

/**
 * Discards changes in all epochs with the epoch range \a epr
 *
 * \param coh	[IN]	Container open handle
 * \param epr	[IN]	The epoch range to discard
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_epoch_discard(daos_handle_t coh, daos_epoch_range_t *epr, daos_event_t *ev);

/**
 * Byte Array API
 */
/**
 * Prepare RMA read buffers for byte-array object.
 * If \a sgl is not sufficient to store all returned buffer descriptors,
 * then the required size will be returned to sgl::sg_iovn.
 * If \a exl_layout is not sufficient to store all returned extent layouts,
 * then the required size will be returned to ext_layout::el_extn.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID.
 * \param epoch	[IN]	Epoch for the read. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]	Read source extents, it may also carry epoch ranges
 *			for each individual extent.
 * \param exl_layout [OUT]
 *			Optional, returned physical extent layouts and
 *			their epoch ranges.
 * \param sgl	[OUT]	Returned scatter/gather list.
 * \param ioh	[OUT]	Returned I/O handle, it should be released by
 *			\a vos_ba_rd_finish().
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_ba_rd_prepare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		  daos_ext_list_t *exl, daos_ext_layout_t *exl_layout,
		  daos_sg_list_t *sgl, daos_handle_t *ioh, daos_event_t *ev);

/**
 * Finish current RMA read operation.
 *
 * \param ioh	[IN]	I/O handle to finalise
 * \param errno	[IN]	errno of current read, zero if there is no error.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_ba_rd_finish(daos_handle_t ioh, int errno, daos_event_t *ev);

/**
 * Prepare RMA write buffers for byte-array object.
 * If \a sgl is not sufficient to store all returned buffer descriptors,
 * then the required size will be returned to sgl::sg_iovn.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID.
 * \param epoch	[IN]	Epoch for the write. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]	Write destination extents, it may also carry epoch
 *			ranges for each individual extent.
 * \param sgl	[OUT]	Returned scatter/gather list.
 * \param ioh	[OUT]	Returned I/O handle, it should be released by
 *			\a vos_ba_wr_finish()
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_ba_wr_prepare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		  daos_ext_list_t *exl, daos_sg_list_t *sgl,
		  daos_handle_t *ioh, daos_event_t *ev);

/**
 * Finish current write operation.
 *
 * \param ioh	[IN]	I/O handle to finalise
 * \param errno	[IN]	errno of current I/O, zero if there is no error.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_ba_wr_finish(daos_handle_t ioh, int errno, daos_event_t *ev);

/**
 * Read from a byte-array object.
 * Object data extents in \a exl will be copied to \a sgl.
 * If \a exl_layout is not sufficient to store all returned extent layouts,
 * then the required size will be returned to ext_layout::el_extn.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the read. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]	Object extents for read
 * \param exl_layout [IN/OUT]
 *			Optional, returned physical extent layouts and
 *			their epoch ranges.
 * \param sgl	[IN/OUT]
 *			Buffer list to store returned object data
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_ba_read(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	    daos_ext_list_t *exl, daos_ext_layout_t *exl_layout,
	    daos_sg_list_t *sgl, daos_event_t *ev);

/**
 * Write to a byte-array object
 * Data blobs in \a sgl will be written to object extents specified by \a exl.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the write. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]	Object extents for read
 * \param sgl	[OUT]	Buffer list to store returned object data
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_ba_write(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	     daos_ext_list_t *exl, daos_sg_list_t *sgl, daos_event_t *ev);

/**
 * Punch will zero specified extent list of a byte-array object
 *
 * \param coh	[IN]	container open handle
 * \param oid	[IN]	object ID
 * \param epoch	[IN]	Epoch for the punch. It will be ignored if epoch range
 *			is provided by \a exl (exl::el_epr).
 * \param exl	[IN]	extents to punch
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_ba_punch(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	     daos_ext_list_t *exl, daos_event_t *ev);

/**
 * KV object API
 */

/**
 * Lookup and return values for given keys in \a kvl.
 * Value length of unfound key will be set to zero.
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	Object ID
 * \param epoch	[IN]	Epoch for the lookup. It will be ignored if epoch range
 *			is provided by \a kvl (kvl::kv_epr).
 * \param kvl	[IN/OUT]
 *			Key list to lookup, if value buffers of \a kvl are NULL,
 *			only value lengths will be returned, otherwise found
 *			values will be copied into these buffers.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_kv_lookup(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	      daos_kv_list_t *kvl, daos_event_t *ev);

/**
 * Update or insert KV pairs in \a kvl
 *
 * \param coh	[IN]	Container open handle
 * \param oid	[IN]	KV object ID
 * \param epoch	[IN]	Epoch for the KV update. It will be ignored if epoch
 *			range is provided by \a kvl (kvl::kv_epr).
 * \param kvl	[IN/OUT]
 *			KV list to update
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_kv_update(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	      daos_kv_list_t *kvl, daos_event_t *ev);

/**
 * Locate and punch key-value pairs for specified keys in \a kvl.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	KV object ID.
 * \param epoch	[IN]	Epoch for the KV punch. It will be ignored if epoch
 *			range is provided by \a kvl (kvl::kv_epr).
 * \param kvl	[IN/OUT]
 *			KV list, only keys are required.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on success, negative value if error.
 */
int
vos_kv_punch(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	     daos_kv_list_t *kvl, daos_event_t *ev);

/**
 * VOS iterator APIs
 */
/**
 * Initialise an iterator for VOS
 *
 * \param cond	[IN]	Conditions for initialising the iterator.
 *			For different iterator types (param::ic_type):
 *			- VOS_ITER_COUUID : param::ic_hdl is pool open handle
 *			- VOS_ITER_OBJ	  : param::ic_hdl is container handle
 *			- VOS_ITER_KV	  : param::ic_hdl is container handle,
 *					    param::ic_oid is ID of KV object.
 *			- VOS_ITER_BA	  : param::ic_hdl is container handle,
 *					    param::ic_oid is ID of byte array
 *					    object.
 * \param ih	[OUT]	Returned iterator handle
 *
 * \return		Zero on success, negative value if error
 */
int
vos_iter_prepare(vos_iter_cond_t *cond, daos_handle_t *ih);

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
 * Move the iterator cursor to the specified position anchor \a pos if it is
 * not NULL, otherwise move the cursor to the next entry of current cursor.
 *
 * \param ih	[IN]	Iterator handle.
 * \param pos	[IN]	Optional, position cursor to move to.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		Zero on if no more entry
 *			1 if there is an entry
 *			negative value if error
 */
int
vos_iter_move(daos_handle_t ih, vos_iter_pos_t *pos, daos_event_t *ev);

/**
 * Return the current data entry of the iterator.
 *
 * \param ih	[IN]	Iterator handle
 * \param entry [OUT]	Optional, returned data entry fo the current cursor
 * \param next	[OUT]	Optional, position anchor for the next entry,
 *			pos::ip_type will be set to VOS_ITER_NONE if there
 *			is no more entries.
 *
 * \return		Zero on success, negative value if error
 */
int
vos_iter_current(daos_handle_t ih, vos_iter_entry_t *entry,
		 vos_iter_pos_t *next);

#endif /* __VOS_API_H */
