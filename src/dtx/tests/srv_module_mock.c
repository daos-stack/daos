/**
 * (C) Copyright 2023-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/container.h>

static void *
mock_init(int tags, int xs_id, int tgt_id)
{
	assert_true(false);
	return NULL;
}

static void
mock_fini(int tags, void *data)
{
	assert_true(false);
}

struct dss_module_key daos_srv_modkey = {
    .dmk_tags  = DAOS_SERVER_TAG,
    .dmk_index = -1,
    .dmk_init  = mock_init,
    .dmk_fini  = mock_fini,
};
