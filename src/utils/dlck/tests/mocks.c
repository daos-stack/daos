/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>
#include <uuid/uuid.h>

#include "../dlck_engine.h"

int
dlck_pool_cont_list(daos_handle_t poh, d_list_t *co_uuids)
{
	return -1;
}

int
dlck_engine_xstream_fini(struct dlck_xstream *xs)
{
	return -1;
}

int
dlck_pool_open(const char *storage_path, uuid_t po_uuid, int tgt_id, daos_handle_t *poh)
{
	return -1;
}
