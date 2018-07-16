/*
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
 * \file
 *
 * DAOS addons
 *
 * The DAOS addons include APIs that are built on top of the existing DAOS
 * API. No internal library functionality is used. The addons include a
 * simplified DAOS object API and a DAOS Array object abstraction on top of the
 * DAOS Key-Array object.
 */

#ifndef __DAOS_ADDONS_H__
#define __DAOS_ADDONS_H__

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Insert or update a single object KV pair. The key specified will be mapped to
 * a dkey in DAOS. The object akey will be the same as the dkey. If a value
 * existed before it will be overwritten (punched first if not previously an
 * atomic value) with the new atomic value described by the sgl.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	epoch	Epoch for the update.
 * \param[in]	key	Key associated with the update operation.
 * \param[in]	size	Size of the buffer to be inserted as an atomic val.
 * \param[in]	buf	Pointer to user buffer of the atomic value.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_kv_put(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	    daos_size_t size, const void *buf, daos_event_t *ev);

/**
 * Fetch value of a key.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	epoch	Epoch for the read.
 * \param[in]	key	key associated with the update operation.
 * \param[in,out]
 *		size	[in]: Size of the user buf. if the size is unknown, set
 *			to DAOS_REC_ANY). [out]: The actual size of the value.
 * \param[in]	buf	Pointer to user buffer. If NULL, only size is returned.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_kv_get(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	    daos_size_t *size, void *buf, daos_event_t *ev);

/**
 * Remove a Key and it's value from the KV store
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	epoch	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. \a iods[]::vd_eprs[]).
 * \param[in]	key	Key to be punched/removed.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_kv_remove(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	       daos_event_t *ev);

typedef struct {
	daos_key_t	*ioa_dkey;
	unsigned int	ioa_nr;
	daos_iod_t	*ioa_iods;
	daos_sg_list_t	*ioa_sgls;
	daos_iom_t	*ioa_maps;
} daos_dkey_io_t;

/**
 * Fetch Multiple Dkeys in a single call. Behaves the same as daos_obj_fetch but
 * for multiple dkeys.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	epoch	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. \a iods[]::vd_eprs[]).
 * \param[in]	nr	Number of dkeys in \a io_array.
 * \param[in,out]
 *		io_array
 *			Array of io descriptors for all dkeys, which describes
 *			another array of iods for akeys within each dkey.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_fetch_multi(daos_handle_t oh, daos_epoch_t epoch,
		     unsigned int nr, daos_dkey_io_t *io_array,
		     daos_event_t *ev);

/**
 * Update/Insert/Punch Multiple Dkeys in a single call. Behaves the same as
 * daos_obj_fetch but for multiple dkeys.
 *
 * \param[in]	oh	Object open handle.
 * \param[in]	epoch	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. \a iods[]::vd_eprs[]).
 * \param[in]	nr	Number of dkeys in \a io_array.
 * \param[in]	io_array
 *			Array of io descriptors for all dkeys, which describes
 *			another array of iods for akeys within each dkey.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_obj_update_multi(daos_handle_t oh, daos_epoch_t epoch,
		      unsigned int nr, daos_dkey_io_t *io_array,
		      daos_event_t *ev);

/** Range of contiguous records */
typedef struct {
	/** Index of the first record in the range */
	daos_off_t		rg_idx;
	/** Number of records in the range */
	daos_size_t		rg_len;
} daos_range_t;

/** IO descriptor of ranges in a DAOS array object to access */
typedef struct {
	/** Number of entries in arr_rgs */
	daos_size_t		arr_nr;
	/** Array of ranges; each range defines a starting index and length. */
	daos_range_t	       *arr_rgs;
} daos_array_iod_t;

/**
 * Create an Array object. This creates a DAOS KV object and adds metadata to
 * define the cell size and chunk size. Further access to that object using the
 * handle will use that metadata to store the array elements.
 *
 * The metadata are just entries in the KV object, meaning that any user can
 * open the object and overwrite that metadata. The user can recreate the array;
 * This will not punch the existing raw data; just overwrite the metadata.
 * However changing the metadata will cause undefined access issues. (MSC - we
 * can force an error in this case by checking for object existence by reading
 * the metadata. But this adds extra overhead).
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID.
 * \param[in]	epoch	Epoch to open object.
 * \param[in]	cell_size
 *			Record size of the array.
 * \param[in]	chunk_size
 *			Contiguous bytes to store per DKey before moving to a
 *			different dkey.
 * \param[out]	oh	Returned array object open handle.
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
daos_array_create(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		  daos_size_t cell_size, daos_size_t chunk_size,
		  daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an Array object. If the array has not been created before (no array
 * metadata exists), this will fail.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID.
 * \param[in]	epoch	Epoch to open object.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW/EXCL/IO_RAND/IO_SEQ
 * \param[out]	cell_size
 *			Record size of the array.
 * \param[out]	chunk_size
 *			Contiguous bytes to store per DKey before moving to a
 *			differen dkey.
 * \param[out]	oh	Returned array object open handle.
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
daos_array_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		unsigned int mode, daos_size_t *elem_size,
		daos_size_t *chunk_size, daos_handle_t *oh, daos_event_t *ev);

/**
 * Close an opened array object.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 */
int
daos_array_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Read data from an array object.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	epoch	Epoch for the read.
 * \param[in]	iod	IO descriptor of ranges to read from the array.
 * \param[in]	sgl	A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does. User allocates the
 *			buffer(s) and sets the length of each buffer.
 * \param[in]	csums	Array of checksums for each buffer in the sgl.
 *			This is optional (pass NULL to ignore).
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
daos_array_read(daos_handle_t oh, daos_epoch_t epoch,
		daos_array_iod_t *iod, daos_sg_list_t *sgl,
		daos_csum_buf_t *csums, daos_event_t *ev);

/**
 * Write data to an array object.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	epoch	Epoch for the write.
 * \param[in]	iod	IO descriptor of ranges to write to the array.
 * \param[in]	sgl	A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 * \param[in]	csums	Array of checksums for each buffer in the sgl.
 *			This is optional (pass NULL to ignore).
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
daos_array_write(daos_handle_t oh, daos_epoch_t epoch,
		 daos_array_iod_t *iod, daos_sg_list_t *sgl,
		 daos_csum_buf_t *csums, daos_event_t *ev);

/**
 * Query the number of records in the array object.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	epoch	Epoch for the query.
 * \param[out]	size	Returned array size (number of records).
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on Success, negative on failure.
 */
int
daos_array_get_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t *size,
		    daos_event_t *ev);

/**
 * Set the array size (truncate) in records. If array is shrinking, we punch
 * dkeys/records above the required size. If the array is epxanding, we insert 1
 * record at the corresponding size. This is NOT equivalent to an allocate.
 *
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	epoch	Epoch for the set_size.
 * \param[in]	size	Size (number of records) to set array to.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \return		0 on Success, negative on failure.
 */
int
daos_array_set_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t size,
		    daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_ADDONS_H__ */
