/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * DAOS Array
 *
 * The DAOS Array API provides a 1-D array implementation over the DAOS object
 * data model.
 */

#ifndef __DAOS_ARRAY_H__
#define __DAOS_ARRAY_H__

#if defined(__cplusplus)
extern "C" {
#endif

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
	/*
	 * On read only: return the number of records that are short fetched
	 * from the largest dkey(s). This helps for checking for short reads. If
	 * this values is not zero, then a short read is possible and should be
	 * checked with daos_array_get_size() compared with the indexes being
	 * read.
	 */
	daos_size_t		arr_nr_short_read;
} daos_array_iod_t;

/**
 * Convenience function to generate a DAOS object ID by encoding the private
 * DAOS bits of the object address space.
 *
 * \param[in,out]
 *		oid	[in]: Object ID with low 96 bits set and unique inside
 *			the container. [out]: Fully populated DAOS object
 *			identifier with the the low 96 bits untouched and the
 *			DAOS private bits (the high 32 bits) encoded.
 * \param[in]	cid	Class Identifier
 * \param[in]	add_attr
 *			Indicate whether the user would maintain the array
 *			cell and chunk size (false), or the metadata should
 *			be stored in the obj (true).
 * \param[in]	args	Reserved.
 */
static inline int
daos_array_generate_id(daos_obj_id_t *oid, daos_oclass_id_t cid, bool add_attr,
		       uint32_t args)
{
	static daos_ofeat_t	feat;
	uint64_t		hdr;

	feat = DAOS_OF_DKEY_UINT64 | DAOS_OF_KV_FLAT;

	if (add_attr)
		feat = feat | DAOS_OF_ARRAY;

	/* TODO: add check at here, it should return error if user specified
	 * bits reserved by DAOS
	 */
	oid->hi &= (1ULL << OID_FMT_INTR_BITS) - 1;
	/**
	 * | Upper bits contain
	 * | OID_FMT_VER_BITS (version)		 |
	 * | OID_FMT_FEAT_BITS (object features) |
	 * | OID_FMT_CLASS_BITS (object class)	 |
	 * | 96-bit for upper layer ...		 |
	 */
	hdr  = ((uint64_t)OID_FMT_VER << OID_FMT_VER_SHIFT);
	hdr |= ((uint64_t)feat << OID_FMT_FEAT_SHIFT);
	hdr |= ((uint64_t)cid << OID_FMT_CLASS_SHIFT);
	oid->hi |= hdr;

	return 0;
}

/**
 * Create an Array object. This opens a DAOS KV object and adds metadata to
 * define the cell size and chunk size. Further access to that object using the
 * handle will use that metadata to store the array elements.
 *
 * The metadata of the array is stored under a special AKEY in DKEY 0. This
 * means that this is a generic array object with it's metadata tracked in the
 * DAOS object. The feat bits in the oid must set DAOS_OF_DKEY_UINT64 |
 * DAOS_OF_ARRAY.  If the feat bits does not set DAOS_OF_ARRAY but sets
 * DAOS_OF_KV_FLAT, the user would be responsible in remembering the array
 * metadata since DAOS will not store those, and should not call this API since
 * nothing will be written to the array object. daos_array_open_with_attrs() can
 * be used to get an array OH in that case to access with the Array APIs.
 *
 * The metadata are just entries in the KV object, meaning that any user can
 * open the object and overwrite that metadata. The user can recreate the array;
 * This will not punch the existing raw data; just overwrite the metadata.
 * However changing the metadata will cause undefined access issues. (MSC - we
 * can force an error in this case by checking for object existence by reading
 * the metadata. But this adds extra overhead).
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID. It is required that the feat for dkey type
 *			be set to DAOS_OF_DKEY_UINT64 | DAOS_OF_ARRAY.
 * \param[in]	th	Transaction handle.
 * \param[in]	cell_size
 *			Record size of the array.
 * \param[in]	chunk_size
 *			Number of contiguous records to store per DKey before
 *			moving to a different dkey.
 * \param[out]	oh	Returned array object open handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
 *			The function will run in blocking mode if \a ev is NULL.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid container handle
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_PERM	Permission denied
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_array_create(daos_handle_t coh, daos_obj_id_t oid, daos_handle_t th,
		  daos_size_t cell_size, daos_size_t chunk_size,
		  daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an Array object. If the array has not been created before (no array
 * metadata exists), this will fail.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID. It is required that the feat for dkey type
 *			be set to DAOS_OF_DKEY_UINT64 | DAOS_OF_ARRAY.
 * \param[in]	th	Transaction handle.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW
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
 *			-DER_NO_PERM	Permission denied
 *			-DER_NONEXIST	Cannot find object
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_array_open(daos_handle_t coh, daos_obj_id_t oid, daos_handle_t th,
		unsigned int mode, daos_size_t *cell_size,
		daos_size_t *chunk_size, daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an Array object with the array attributes specified by the user. This is
 * the same as the create call if the object does not exist, except that nothing
 * is updated in the object, and the API just returns an OH to the user. If the
 * array was accessed with different cell_size and chunk_size before, accessing
 * it again will introduce corruption in the array data.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID. It is required that the feat for dkey type
 *			be set to DAOS_OF_DKEY_UINT64 | DAOS_OF_KV_FLAT.
 * \param[in]	th	Transaction handle.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW
 * \param[in]	cell_size
 *			Record size of the array.
 * \param[in]	chunk_size
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
 *			-DER_NO_PERM	Permission denied
 */
int
daos_array_open_with_attr(daos_handle_t coh, daos_obj_id_t oid,
			  daos_handle_t th, unsigned int mode,
			  daos_size_t cell_size, daos_size_t chunk_size,
			  daos_handle_t *oh, daos_event_t *ev);

/**
 * Convert a local array handle to global representation data which can be
 * shared with peer processes.
 * If glob->iov_buf is set to NULL, the actual size of the global handle is
 * returned through glob->iov_buf_len.
 * This function does not involve any communication and does not block.
 *
 * \param[in]	oh	valid local array object open handle to be shared
 * \param[out]	glob	pointer to iov of the buffer to store handle information
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Array handle is nonexistent
 *			-DER_TRUNC	Buffer in \a glob is too short, larger
 *					buffer required. In this case the
 *					required buffer size is returned through
 *					glob->iov_buf_len.
 */
int
daos_array_local2global(daos_handle_t oh, d_iov_t *glob);

/**
 * Create a local array open handle for global representation data. This handle
 * has to be closed with daos_array_close().
 *
 * \param[in]	coh	Container open handle the array belongs to
 * \param[in]	glob	Global (shared) representation of a collective handle
 *			to be extracted
 * \param[in]	mode	Option to change the object open mode.
 *			Pass 0 to inherit the global mode.
 * \param[out]	oh	Returned local array open handle
 *
 * \return		These values will be returned:
 *			non-blocking mode:
 *			0		Success
 *			-DER_INVAL	Invalid parameter
 *			-DER_NO_HDL	Container handle is nonexistent
 */
int
daos_array_global2local(daos_handle_t coh, d_iov_t glob, unsigned int mode,
			daos_handle_t *oh);

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
 * \param[in]	th	Transaction handle.
 * \param[in]	iod	IO descriptor of ranges to read from the array.
 * \param[in]	sgl	A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does. User allocates the
 *			buffer(s) and sets the length of each buffer.
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
 */
int
daos_array_read(daos_handle_t oh, daos_handle_t th, daos_array_iod_t *iod,
		d_sg_list_t *sgl, daos_event_t *ev);

/**
 * Write data to an array object.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	iod	IO descriptor of ranges to write to the array.
 * \param[in]	sgl	A scatter/gather list (sgl) to the store array data.
 *			Buffer sizes do not have to match the indiviual range
 *			sizes as long as the total size does.
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
 */
int
daos_array_write(daos_handle_t oh, daos_handle_t th, daos_array_iod_t *iod,
		 d_sg_list_t *sgl, daos_event_t *ev);

/**
 * Query the number of records in the array object.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	th	Transaction handle.
 * \param[out]	size	Returned array size (number of records).
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_array_get_size(daos_handle_t oh, daos_handle_t th, daos_size_t *size,
		    daos_event_t *ev);

/**
 * Set the array size (truncate) in records. If array is shrinking, we punch
 * dkeys/records above the required size. If the array is epxanding, we insert 1
 * record at the corresponding size. This is NOT equivalent to an allocate.
 *
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	size	Size (number of records) to set array to.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_array_set_size(daos_handle_t oh, daos_handle_t th, daos_size_t size,
		    daos_event_t *ev);

/**
 * Destroy the array object by punching all data (keys) in the array object
 * including the metadata associated with the array. daos_obj_punch() is called
 * underneath. The oh still needs to be closed with a call to
 * daos_array_close(), but any other access with that handle, or other array
 * open handles, will fail. The destroy will happen regardless of any open
 * handle, so it's the user responsibility to ensure that there is no further
 * access to the array before the destory is called.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_array_destroy(daos_handle_t oh, daos_handle_t th, daos_event_t *ev);

/**
 * Punch a hole in the array indicated by the range in the iod.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	th	Transaction handle.
 * \param[in]	iod	IO descriptor of ranges to punch in the array.
 * \param[in]	ev	Completion event, it is optional and can be NULL.
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
daos_array_punch(daos_handle_t oh, daos_handle_t th, daos_array_iod_t *iod,
		 daos_event_t *ev);

/**
 * Retrieve array cell and chunk size from an open handle.
 *
 * \param[in]	oh	Array object open handle.
 * \param[out]	chunk_size
 *			Chunk size of the array.
 * \param[out]	cell_size
 *			Cell size of the array.
 *
 * \return		These values will be returned by \a ev::ev_error in
 *			non-blocking mode:
 *			0		Success
 *			-DER_NO_HDL	Invalid object open handle
 *			-DER_INVAL	Invalid parameter
 */
int
daos_array_get_attr(daos_handle_t oh, daos_size_t *chunk_size,
		    daos_size_t *cell_size);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_ARRAY_H__ */
