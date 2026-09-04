/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(ddb)

#include <stdbool.h>
#include <unistd.h>

#include <daos/debug.h>
#include <gurt/common.h>
#include <gurt/debug.h>

#include "ddb_common.h"
#include "ddb_spdk_reinit_wa.h"

/**
 * Enforces a "single SPDK/VOS init per process for NVMe-backed pools" rule -- see README.md's
 * Limitations section for the full rationale (SPDK does not support re-initialization within a
 * process, which ddb's multi-command sessions can otherwise trigger).
 *
 * You may remove this code once the underlying issue is properly resolved.
 */

#define VOS_NVME_CONF "daos_nvme.conf"

static int
nvme_conf_exists(const char *nvme_conf_dir, bool *exists)
{
	char *nvme_conf;

	/*
	 * Not unit-tested: D_ASPRINTF's vasprintf() is compiled into libgurt.so, which --wrap
	 * cannot intercept from ddb_ut's own link step (see tests/ddb_spdk_reinit_wa_ut.c).
	 */
	D_ASPRINTF(nvme_conf, "%s/%s", nvme_conf_dir, VOS_NVME_CONF);
	if (nvme_conf == NULL)
		return -DER_NOMEM;

	*exists = (access(nvme_conf, F_OK) == 0);

	D_FREE(nvme_conf);

	return DER_SUCCESS;
}

#define SPDK_REINIT_MSG                                                                            \
	"SPDK cannot be re-initialized for another NVMe-backed pool within the same DDB "          \
	"process. Please restart the DDB process and try again.\n"

#ifndef DAOS_BUILD_RELEASE
/*
 * Diagnostic-only, non-release escape hatch to re-validate the SPDK/DPDK limitation described
 * above against specific hardware or software version; not a supported workflow (see README.md).
 */
static bool
reinit_diagnostic_override(void)
{
	bool allow = false;

	/* The return value is ignored, since the env var is optional. */
	(void)d_getenv_bool(DDB_ALLOW_SPDK_REINIT_ENV, &allow);

	return allow;
}
#endif /* !DAOS_BUILD_RELEASE */

int
dwa_can_proceed(struct ddb_ctx *ctx, const char *nvme_conf_dir, bool *can_proceed)
{
	static bool nvme_used_once = false;

	D_ASSERT(ctx != NULL);
	D_ASSERT(can_proceed != NULL);

#ifndef DAOS_BUILD_RELEASE
	if (reinit_diagnostic_override()) {
		*can_proceed = true;
		return -DER_SUCCESS;
	}
#endif

	if (nvme_conf_dir != NULL) {
		bool nvme_configured;
		int  rc;

		rc = nvme_conf_exists(nvme_conf_dir, &nvme_configured);
		if (!SUCCESS(rc))
			return rc;

		if (!nvme_configured) {
			*can_proceed = true;
			return -DER_SUCCESS;
		}
	}

	*can_proceed = !nvme_used_once;
	if (nvme_used_once)
		ddb_error(ctx, SPDK_REINIT_MSG);
	nvme_used_once = true;

	return -DER_SUCCESS;
}
