/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_COMMON__
#define __DLCK_COMMON__

#include <daos_types.h>

/**
 * XXX
 */
int
dlck_pool_mkdir(const char *storage_path, struct dlck_file *file);

/**
 * XXX
 */
int
dlck_pool_open(const char *storage_path, struct dlck_file *file, int tgt_id, daos_handle_t *poh);

struct co_uuid_list_elem {
	d_list_t link;
	uuid_t   uuid;
};

/**
 * XXX
 */
int
dlck_pool_cont_list(daos_handle_t poh, d_list_t *co_uuids);

#endif /** __DLCK_COMMON__ */