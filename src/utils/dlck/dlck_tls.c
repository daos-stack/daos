/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos_srv/daos_engine.h>

static void *
dlck_srv_tls_init(int tags, int xs_id, int tgt_id)
{
	struct dss_module_info *info;

	D_ALLOC_PTR(info);

	return info;
}

static void
dlck_srv_tls_fini(int tags, void *data)
{
	D_FREE(data);
}

struct dss_module_key daos_srv_modkey = {
    .dmk_tags  = DAOS_SERVER_TAG,
    .dmk_index = -1,
    .dmk_init  = dlck_srv_tls_init,
    .dmk_fini  = dlck_srv_tls_fini,
};
