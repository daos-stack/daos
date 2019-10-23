/**
 * (C) Copyright 2019 Intel Corporation.
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
 * \param	oid[out]	Generated oid for created array
 *
 * \return 0 or error code
 */
int
vts_array_alloc(daos_handle_t coh, daos_epoch_t epoch, daos_unit_oid_t *oid);

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

/** Punches the vos test array object and recreates it.  Handle remains open
 * \param	aoh[in]			Open array handle
 * \param	punch_epoch[in]		punch epoch
 * \param	create_epoch[in]	creation epoch (must be > punch_epoch)
 *
 * \return 0 or error code
 */
int
vts_array_reset(daos_handle_t aoh, daos_epoch_t punch_epoch,
		daos_epoch_t create_epoch);

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
 * \param	elements[in]	data to write (int32_t data only)
 *
 * \return 0 or error code
 */
int
vts_array_write(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
		uint64_t count, int *elements);

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
 * \param	elements[out]	buffer in which to read (must be large enough
 *				to store count int32_t)
 *
 * \return 0 or error code
 */
int
vts_array_read(daos_handle_t aoh, daos_epoch_t epoch, uint64_t offset,
	       uint64_t count, int *elements);

#endif
