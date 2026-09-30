/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_MOCKS_H
#define DAOS_DDB_MOCKS_H

#include <stdbool.h>

#include "ddb.h"
#include "ddb_parse.h"

/** Signature of vos_dtx_discard_invalid(), for installing a test-provided fake implementation. */
typedef int (*vos_dtx_discard_invalid_fn_t)(daos_handle_t coh, struct dtx_id *dti, int *discarded);

/**
 * Install mock_fn as __wrap_vos_dtx_discard_invalid()'s implementation. Pass NULL to restore the
 * real vos_dtx_discard_invalid() passthrough.
 */
void
mock_vos_dtx_discard_invalid_set(vos_dtx_discard_invalid_fn_t mock_fn);

/** Signature of dwa_can_proceed(), for installing a test-provided fake implementation. */
typedef int (*dwa_can_proceed_fn_t)(struct ddb_ctx *ctx, const char *nvme_conf_dir,
				    bool *can_proceed);

/**
 * Install mock_fn as __wrap_dwa_can_proceed()'s implementation. Pass NULL to restore the real
 * dwa_can_proceed() passthrough.
 */
void
mock_dwa_can_proceed_set(dwa_can_proceed_fn_t mock_fn);

/** Signature of ddb_parse_vos_file_parts(), for installing a test-provided fake implementation. */
typedef int (*ddb_parse_vos_file_parts_fn_t)(const char *vos_path, const char *db_path,
					     struct vos_file_parts *vos_file_parts);

/**
 * Install mock_fn as __wrap_ddb_parse_vos_file_parts()'s implementation. Pass NULL to restore the
 * real ddb_parse_vos_file_parts() passthrough.
 */
void
mock_ddb_parse_vos_file_parts_set(ddb_parse_vos_file_parts_fn_t mock_fn);

/**
 * Signature of d_asprintf2(), for installing a test-provided fake implementation of D_ASPRINTF.
 */
typedef char *(*d_asprintf2_fn_t)(int *rc, const char *fmt, ...);

/**
 * Install mock_fn as __wrap_d_asprintf2()'s implementation. Pass NULL to restore the real
 * d_asprintf2() passthrough.
 */
void
mock_d_asprintf2_set(d_asprintf2_fn_t mock_fn);

#endif /* DAOS_DDB_MOCKS_H */
