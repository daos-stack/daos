/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>

#include <gurt/common.h>

#include "ddb_mocks.h"

int
__real_dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed);

static dwa_can_proceed_fn_t mock_dwa_can_proceed = __real_dwa_can_proceed;

void
mock_dwa_can_proceed_set(dwa_can_proceed_fn_t mock_fn)
{
	mock_dwa_can_proceed = __real_dwa_can_proceed;
	if (mock_fn != NULL)
		mock_dwa_can_proceed = mock_fn;
}

int
__wrap_dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed)
{
	D_ASSERT(mock_dwa_can_proceed != NULL);
	return mock_dwa_can_proceed(ctx, nvme_conf_dir, can_proceed);
}
