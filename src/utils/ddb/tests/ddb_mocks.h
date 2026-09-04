/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_MOCKS_H
#define DAOS_DDB_MOCKS_H

#include <stdbool.h>

#include "ddb.h"

/** Signature of dwa_can_proceed(), for installing a test-provided fake implementation. */
typedef int (*dwa_can_proceed_fn_t)(struct ddb_ctx *ctx, const char *nvme_conf_dir,
				    bool *can_proceed);

/**
 * Install mock_fn as __wrap_dwa_can_proceed()'s implementation. Pass NULL to restore the real
 * dwa_can_proceed() passthrough.
 */
void
mock_dwa_can_proceed_set(dwa_can_proceed_fn_t mock_fn);

#endif /* DAOS_DDB_MOCKS_H */
