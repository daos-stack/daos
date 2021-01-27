/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This simulates the array API (array of integers only) on top of VOS only
 *
 * vos/tests/vts_array.h
 */
#ifndef __VTS_ARRAY_H__
#define __VTS_ARRAY_H__

#include <daos_srv/vos.h>
/* This library is convenient for testing daos_array like features on a single
 * VOS standalone target
 */

/** Create a new vos test array object at the specified epoch
 * \param	coh[in]		Container handle
 * \param	epoch[in]	Creation epoch
 * \param	record_size[in]	Size of each record
 * \param	nr_per_key[in]	Records per key (0 for default)
 * \param	akey_size[in]	Size of the akey (0 for default)
 * \param	oid[out]	Generated oid for created array
 *
 * \return 0 or error code
 */
int
vts_array_alloc(daos_handle_t coh, daos_epoch_t epoch, daos_size_t record_size,
		daos_size_t nr_per_key, daos_size_t akey_size,
		daos_unit_oid_t *oid);

/** Calls vos_obj_delete on specified object to remove it from the tree
 * \param	coh[in]		Container handle
 * \param	oid[in]		Object to destroy
 *
 * \return 0 or error code
 */
int
vts_array_free(daos_handle_t coh, daos_unit_oid_t oid);

/** Creates a handle to a vos test array object
 * \param	coh[in]		Container handle
 * \param	oid[in]		Object to open
 * \param	aoh[out]	The open handle
 *
 * \return 0 or error code
 */
int
vts_array_open(daos_handle_t coh, daos_unit_oid_t oid, daos_handle_t *aoh);

/** Set the I/O size of the array.  Reads and writes will be split
 *  into chunks
 *
 *  \param	aoh[in]		Open array handle
 *  \param	io_size[in]	The new io_size (default is record size)
 *
 * \return 0 or error code
 */
int
vts_array_set_iosize(daos_handle_t aoh, uint64_t io_size);

/** Punches the vos test array object and recreates it.  Handle remains open
 * \param	aoh[in,out]		Open array handle, returns new handle
 * \param	punch_epoch[in]		punch epoch
 * \param	create_epoch[in]	creation epoch (must be > punch_epoch)
 * \param	record_size[in]		Size of each record
 * \param	nr_per_key[in]		Records per key (0 for default)
 * \param	akey_size[in]		Size of the akey (0 for default)
 *
 * \return 0 or error code
 */
int
vts_array_reset(daos_handle_t *aoh, daos_epoch_t punch_epoch,
		daos_epoch_t create_epoch, daos_size_t record_size,
		daos_size_t nr_per_key, daos_size_t akey_size);

/** Closes the open vos test array object handle
 * \param	aoh[in]			Open array handle
 */
void
vts_array_close(daos_handle_t aoh);

/** Sets the array size
 * \param	aoh[in]		Open array handle
 * \param	epoch[in]	epoch for size setting
 * \param	new_size[in]	New size
 *
 * \return 0 or error code
 */
int
vts_array_set_size(daos_handle_t aoh, daos_epoch_t epoch, daos_size_t new_size);

/** Gets the array size
 * \param	aoh[in]		Open array handle
 * \param	epoch[in]	epoch for size setting
 * \param	size[out]	Retrieved size
 *
 * \return 0 or error code
 */
int
vts_array_get_size(daos_handle_t aoh, daos_epoch_t epoch, daos_size_t *size);

/** Writes to the array
 * \param	aoh[in]		Open array handle
 * \param	epoch[in]	epoch for update
 * \param	offset[in]	start index
 * \param	count[in]	Number of items
 * \param	elements[in]	data to write
 *
 * \return 0 or error code
 */
int
vts_array_write(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
		uint64_t count, void *elements);

/** Punches extents in the array
 * \param	aoh[in]		Open array handle
 * \param	epoch[in]	epoch for update
 * \param	offset[in]	start index
 * \param	count[in]	Number of items
 *
 * \return 0 or error code
 */
int
vts_array_punch(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
		uint64_t count);

/** Reads from the array
 * \param	aoh[in]		Open array handle
 * \param	epoch[in]	epoch for update
 * \param	offset[in]	start index
 * \param	count[in]	Number of items
 * \param	elements[out]	buffer in which to read
 *
 * \return 0 or error code
 */
int
vts_array_read(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
	       uint64_t count, void *elements);

#endif
