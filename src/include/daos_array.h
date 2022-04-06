/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
	/** (on read only) the number of records that are short fetched from the largest dkey(s).
	 * Helps for checking short reads. If nonzero, a short read is possible and should be
	 * checked with daos_array_get_size() compared with the indexes being read.
	 */
	daos_size_t		arr_nr_short_read;
	/** (on read only) the number of records that were actually read from the array */
	daos_size_t		arr_nr_read;
} daos_array_iod_t;

/** DAOS array stat (size, modification time) information */
typedef struct {
	/** Array size (in records) */
	daos_size_t	st_size;
	/** Max epoch of array modification (mtime) */
	daos_epoch_t	st_max_epoch;
} daos_array_stbuf_t;

/**
 * Convenience function to generate a DAOS Array object ID by encoding the private DAOS bits of the
 * object address space.
 *
 * \param[in]	coh	Container open handle.
 * \param[in,out]
 *		oid	[in]: Object ID with low 96 bits set and unique inside the container.
 *			[out]: Fully populated DAOS object identifier with the low 96 bits untouched
 *			and the DAOS private bits (the high 32 bits) encoded.
 * \param[in]	add_attr
 *			Indicate whether the user would maintain the array cell and chunk size
 *			(false), or the metadata should be stored in the obj (true).
 * \param[in]	cid	Class Identifier. This setting is for advanced users who are knowledgeable
 *			on the specific oclass being set and what that means for the object in the
 *			current system and the container it's in. Setting this to 0 (unknown) will
 *			check if there are any hints specified and use an oclass accordingly. If
 *			there are no hints specified we use the container properties to select the
 *			object class.
 * \param[in]   hints	Optional hints to select oclass with redundancy type and sharding. This will
 *			be ignored if cid is not OC_UNKNOWN (0).
 * \param[in]	args	Reserved.
 */
static inline int
daos_array_generate_oid(daos_handle_t coh, daos_obj_id_t *oid, bool add_attr, daos_oclass_id_t cid,
			daos_oclass_hints_t hints, uint32_t args)
{
	enum daos_otype_t type;

	type = DAOS_OT_ARRAY_ATTR;

	if (add_attr)
		type = DAOS_OT_ARRAY;

	return daos_obj_generate_oid(coh, oid, type, cid, hints, args);
}

/**
 * Create an Array object. This opens a DAOS object and adds metadata under a special akey to define
 * the cell size and chunk size. Further access to that object using the handle will use that
 * metadata to store the array elements.
 *
 * The metadata of the array is stored under a special AKEY in DKEY 0. This means that this is a
 * generic array object with it's metadata tracked in the DAOS object. The feat bits in the oid must
 * set DAOS_OT_ARRAY,DAOS_OT_ARRAY_ATTR or DAOS_OT_ARRAY_BYTE. If the feat bits does not set
 * DAOS_OF_ARRAY, the user would be responsible for remembering the array metadata since DAOS will
 * not store those, and should not call this API since nothing will be written to the array
 * object. daos_array_open_with_attrs() can be used to get an array OH in that case to access with
 * the Array APIs.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID. It is required that the object type
 *			be set to DAOS_OT_ARRAY.
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
 *			-DER_EXIST	Array already exists
 *			-DER_UNREACH	Network is unreachable
 */
int
daos_array_create(daos_handle_t coh, daos_obj_id_t oid, daos_handle_t th,
		  daos_size_t cell_size, daos_size_t chunk_size,
		  daos_handle_t *oh, daos_event_t *ev);

/**
 * Open an Array object. If the array has not been created before (no array metadata exists), this
 * will fail.
 *
 * \param[in]	coh	Container open handle.
 * \param[in]	oid	Object ID. It is required that the feat for dkey type
 *			be set to DAOS_OF_KV_FLAT | DAOS_OF_DKEY_UINT64 |
 *			DAOS_OF_ARRAY.
 * \param[in]	th	Transaction handle.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW
 * \param[out]	cell_size
 *			Record size of the array.
 * \param[out]	chunk_size
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
 * \param[in]	oid	Object ID. It is required that the object type to be
 *			be set to DAOS_OT_ARRAY_ATTR or DAOS_OT_ARRAY_BYTE.
 * \param[in]	th	Transaction handle.
 * \param[in]	mode	Open mode: DAOS_OO_RO/RW
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
 *			Buffer sizes do not have to match the individual range
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
 *			Buffer sizes do not have to match the individual range
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
daos_array_get_size(daos_handle_t oh, daos_handle_t th, daos_size_t *size, daos_event_t *ev);

/**
 * Stat array to retrieve size and mtime.
 *
 * \param[in]	oh	Array object open handle.
 * \param[in]	th	Transaction handle.
 * \param[out]	stbuf	Returned stat info.
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
daos_array_stat(daos_handle_t oh, daos_handle_t th, daos_array_stbuf_t *stbuf, daos_event_t *ev);

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
 * access to the array before the destroy is called.
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
