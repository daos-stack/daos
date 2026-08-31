/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdbool.h>

#include "ddb_mocks.h"

int
__real_dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed);

static dwa_can_proceed_fn_t mock_dwa_can_proceed = NULL;

int
mock_dwa_can_proceed_setup(dwa_can_proceed_fn_t mock_fn)
{
	mock_dwa_can_proceed = mock_fn;

	return 0;
}

int
mock_dwa_can_proceed_teardown(void)
{
	mock_dwa_can_proceed = NULL;

	return 0;
}

int
__wrap_dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed)
{
	if (mock_dwa_can_proceed == NULL)
		return __real_dwa_can_proceed(ctx, nvme_conf_dir, can_proceed);

	return mock_dwa_can_proceed(ctx, nvme_conf_dir, can_proceed);
}
