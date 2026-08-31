/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_SPDK_REINIT_WA_H
#define DAOS_DDB_SPDK_REINIT_WA_H

#include <stdbool.h>

/**
 * "Single SPDK/VOS init per process for NVMe-backed pools" guard -- see ddb_spdk_reinit_wa.c
 * for the full rationale.
 *
 * @param ctx		DDB context, used to report an actionable error if the guard refuses.
 * @param nvme_conf_dir	Directory to check for a daos_nvme.conf, i.e. whether this operation
 *			touches SPDK at all. Pass NULL if the caller's SPDK use isn't tied to
 *			any directory (currently just smd_sync) -- always treated as configured.
 * @param can_proceed	Set on success (rc == 0): true if the operation may proceed, false if
 *			the guard refuses it (an error has already been reported via ctx).
 *
 * @return		0 on success (see can_proceed for the actual decision); a negative
 *			DAOS error if the internal daos_nvme.conf check itself failed.
 */
int
dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed);

#endif /* DAOS_DDB_SPDK_REINIT_WA_H */
