/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_POOL__
#define __DLCK_POOL__

#include <daos_types.h>

#include "dlck_args.h"

#define DLCK_POOL_OPEN_FLAGS (VOS_POF_EXCL | VOS_POF_FOR_FEATURE_FLAG)

/**
 * Create a directory for the pool.
 *
 * \param[in]	storage_path	Storage path.
 * \param[in]	po_uuid		Pool UUID.
 * \param[in]	ck		Checker.
 *
 * \retval DER_SUCCESS		Success.
 * \retval -DER_NOMEM		Out of memory.
 * \retval -DER_NO_PERM		Permission problem. Please see mkdir(2).
 * \retval -DER_NONEXIST	A component of the \p storage_path does not exist.
 * \retval -DER_*		Possibly other errors.
 */
int
dlck_pool_mkdir(const char *storage_path, uuid_t po_uuid, struct checker *ck);

/**
 * Create pool directories for all \p files provided.
 *
 * \param[in]	storage_path	Engine the ULT is about to be run in.
 * \param[in]	files		List of files.
 * \param[in]	ck		Checker.
 *
 * \retval DER_SUCCESS		Success.
 * \retval -DER_NOMEM		Out of memory.
 * \retval -DER_NO_PERM		Permission problem. Please see mkdir(2).
 * \retval -DER_NONEXIST	A component of the \p storage_path does not exist.
 * \retval -DER_*		Possibly other errors but not -DER_EXIST.
 */
int
dlck_pool_mkdir_all(const char *storage_path, d_list_t *files, struct checker *ck);

/**
 * Allocate the pool file if necessary (MD-on-SSD).
 *
 * \param[in]	storage_path	Storage path.
 * \param[in]	po_uuid		Pool UUID.
 * \param[in]	tgt_id		Target ID.
 *
 * \retval DER_SUCCESS		Success.
 * \retval -DER_NOMEM		Out of memory.
 * \retval -DER_NO_PERM		Permission problem. Please see open(3) and fallocate(2).
 * \retval -DER_EXIST		The file already exists. Please see open(3).
 * \retval -DER_NONEXIST	The file does not exist. Please see open(3).
 * \retval -DER_NOSPACE		There is not enough space left on the device.
 * \retval -DER_*		Possibly other errors.
 */
int
dlck_pool_file_preallocate(const char *storage_path, uuid_t po_uuid, int tgt_id);

/**
 * Open a pool.
 *
 * It allocates the pool file if necessary (MD-on-SSD).
 *
 * \param[in]	storage_path	Storage path.
 * \param[in]	po_uuid		Pool UUID.
 * \param[in]	tgt_id		Target ID.
 * \param[out]	poh		Pool handle.
 *
 * \retval DER_SUCCESS		Success.
 * \retval -DER_NOMEM		Out of memory.
 * \retval -DER_NO_PERM		Permission problem. Please see open(3) and fallocate(2).
 * \retval -DER_EXIST		The file already exists. Please see open(3).
 * \retval -DER_NONEXIST	The file does not exist. Please see open(3).
 * \retval -DER_NOSPACE		There is not enough space left on the device.
 * \retval -DER_*		Possibly other errors.
 */
int
dlck_pool_open(const char *storage_path, uuid_t po_uuid, int tgt_id, daos_handle_t *poh);

struct co_uuid_list_elem {
	d_list_t link;
	uuid_t   uuid;
};

/**
 * Add all container UUIDs of the \p poh pool to \p co_uuids.
 *
 * \param[in]	poh		Pool handle.
 * \param[out]	co_uuids	List of containers' UUIDs.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Possibly other errors. Please see vos_iterate().
 */
int
dlck_pool_cont_list(daos_handle_t poh, d_list_t *co_uuids);

/**
 * Add all files (pool UUIDs + all targets bitmap) to \p file_list.
 *
 * \param[out]	file_list	List of all files belonging to the given DAOS engine.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_NOMEM	Out of memory.
 * \retval -DER_*	Possibly other errors.
 */
int
dlck_pool_list(d_list_t *file_list);

#endif /** __DLCK_POOL__ */
