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
 * DAOS ARRAY APIs
 */

#ifndef __DAOS_ARRAY_API_H__
#define __DAOS_ARRAY_API_H__

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
	daos_size_t		rg_len;
	daos_off_t		rg_idx;
} daos_range_t;

/** describe ranges of an array object to access */
typedef struct {
	/** Number of ranges to access */
	daos_size_t		arr_nr;
	/** Array of index/len pairs */
	daos_range_t	       *arr_rgs;
} daos_array_ranges_t;

/**
 * Create an Array object. This creates a DAOS KV object and adds metadata to
 * define the cell size and block size. Further access to that object using the
 * handle will use that metadata to store the array elements.
 *
 * The metadata are just entries in the KV object, meaning that any user can
 * open the object and overwrite that metadata. The user can recreate the array;
 * however changing the metadata will cause undefined access issues later. (MSC
 * - we can force an error in this case once we can query the value of the the
 * metadata entries and determine they are holes/unwritten and the array size is
 * 0).
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to open object.
 * \param cell_size [IN]
 *			Record size of the array.
 * \param block_size [IN]
 *			Contiguous bytes to store per DKey before moving to a
 *			differen dkey.
 * \param oh	[OUT]	Returned object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
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
		  daos_size_t elem_size, daos_size_t block_size,
		  daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an Array object. If the array has not been created before, this will be
 * equivalent to a DAOS KV object, and the metadata will be set to default
 * values, but not written to the KV.
 *
 * \param coh	[IN]	Container open handle.
 * \param oid	[IN]	Object ID.
 * \param epoch	[IN]	Epoch to open object.
 * \param mode	[IN]	Open mode: DAOS_OO_RO/RW/EXCL/IO_RAND/IO_SEQ
 * \param cell_size [OUT]
 *			Record size of the array.
 * \param block_size [OUT]
 *			Contiguous bytes to store per DKey before moving to a
 *			differen dkey.
 * \param oh	[OUT]	Returned object open handle.
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
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
		daos_size_t *block_size, daos_handle_t *oh, daos_event_t *ev);

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
daos_array_close(daos_handle_t oh, daos_event_t *ev);

/**
 * Read data from an array object.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the read.
 *
 * \param range	[IN]	Ranges to read from the array.
 *
 * \param sgl   [IN/OUT]
 *			A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does. User allocates the
 *			buffer(s) and sets the length of each buffer.
 *
 * \param csums	[OUT]	Array of checksums for each buffer in the sgl.
 *			This is optional (pass NULL to ignore).
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
 *			-DER_REC2BIG	Record is too large and can't be
 *					fit into output buffer
 *			-DER_EP_OLD	Epoch is too old and has no data
 */
int
daos_array_read(daos_handle_t oh, daos_epoch_t epoch,
		daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
		daos_csum_buf_t *csums, daos_event_t *ev);

/**
 * Write data to an array object.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the write.
 *
 * \param range	[IN]	Ranges to write to the array.
 *
 * \param sgl   [IN]	A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does.
 *
 * \param ev	[IN]	Completion event, it is optional and can be NULL.
 *			Function will run in blocking mode if \a ev is NULL.
 *
 * \param csums	[IN]	Array of checksums for each buffer in the sgl.
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
		 daos_array_ranges_t *ranges, daos_sg_list_t *sgl,
		 daos_csum_buf_t *csums, daos_event_t *ev);

int
daos_array_get_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t *size,
		    daos_event_t *ev);
int
daos_array_set_size(daos_handle_t oh, daos_epoch_t epoch, daos_size_t size,
		    daos_event_t *ev);

/**
 * Insert or update a single object KV pair. The key specified will be mapped to
 * a dkey in DAOS. The object akey will be the same as the dkey. If a value
 * existed before it will be overwritten (punched first if not previously an
 * atomic value) with the new atomic value described by the sgl.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the update.
 *
 * \param key	[IN]	key associated with the update operation.
 *
 * \param buf_size [IN] Size of the buffer to be inserted as an atomic val.
 *
 * \param buf	[IN]	pointer to user buffer of the atomic value.
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
daos_kv_put(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	    daos_size_t buf_size, const void *buf, daos_event_t *ev);

/**
 * Fetch value of a key.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the read.
 *
 * \param key	[IN]	key associated with the update operation.
 *
 * \param buf_size [IN/OUT]
 *			Size of the user buf. if the size is unknown (set to
 *			DAOS_REC_ANY), the actual size of the value is returned.
 *
 * \param buf	[IN]	pointer to user buffer. If NULL, only size is returned.
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
daos_kv_get(daos_handle_t oh, daos_epoch_t epoch, const char *key,
	    daos_size_t *buf_size, void *buf, daos_event_t *ev);

/**
 * Remove a Key and it's value from the KV store
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. \a iods[]::vd_eprs[]).
 *
 * \param key	[IN]	key to be punched/removed.
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
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. \a iods[]::vd_eprs[]).
 *
 * \param num_dkeys
 *		[IN]	Number of dkeys in \a io_array.
 *
 * \param daos_dkey_io_t *io_array [IN\OUT]
 *			Array of io descriptors for all dkeys, which describes
 *			another array of iods for akeys within each dkey.
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
daos_obj_fetch_multi(daos_handle_t oh, daos_epoch_t epoch,
		     unsigned int num_dkeys, daos_dkey_io_t *io_array,
		     daos_event_t *ev);

/**
 * Update/Insert/Punch Multiple Dkeys in a single call. Behaves the same as
 * daos_obj_fetch but for multiple dkeys.
 *
 * \param oh	[IN]	Object open handle.
 *
 * \param epoch	[IN]	Epoch for the update. It is ignored if epoch range is
 *			provided for each extent through the vector I/O
 *			descriptor (i.e. \a iods[]::vd_eprs[]).
 *
 * \param num_dkeys
 *		[IN]	Number of dkeys in \a io_array.
 *
 * \param daos_dkey_io_t *io_array [IN\OUT]
 *			Array of io descriptors for all dkeys, which describes
 *			another array of iods for akeys within each dkey.
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
daos_obj_update_multi(daos_handle_t oh, daos_epoch_t epoch,
		      unsigned int num_dkeys, daos_dkey_io_t *io_array,
		      daos_event_t *ev);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_ARRAY_API_H__ */
