/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define M_TAG		DM_TAG(CLI)

#include <daos/common.h>
#include "dfs_internal.h"

void
dfs_free_sb_layout(daos_iod_t *iods[])
{
	D_FREE(*iods);
}
