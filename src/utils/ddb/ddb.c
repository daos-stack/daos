/**
 * (C) Copyright 2022-2024 Intel Corporation.
 * (C) Copyright 2025 Vdura Inc.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <daos_errno.h>
#include <daos_srv/vos.h>

#include "ddb_common.h"

int
ddb_feature_string2flags(struct ddb_ctx *ctx, const char *string, uint64_t *compat_flags,
			 uint64_t *incompat_flags)
{
	char    *tmp;
	char    *tok;
	int      rc = 0;
	uint64_t flag;
	uint64_t ret_compat_flags   = 0;
	uint64_t ret_incompat_flags = 0;
	bool     compat_feature;

	tmp = strndup(string, PATH_MAX);
	if (tmp == NULL)
		return -DER_NOMEM;
	tok = strtok(tmp, ",");
	while (tok != NULL) {
		flag = vos_pool_name2flag(tok, &compat_feature);
		if (flag == 0) {
			ddb_printf(ctx, "Unknown flag: '%s'\n", tok);
			rc = -DER_INVAL;
			break;
		}
		if (compat_feature)
			ret_compat_flags |= flag;
		else
			ret_incompat_flags |= flag;
		tok = strtok(NULL, ",");
	}

	free(tmp);
	if (rc == 0) {
		*compat_flags   = ret_compat_flags;
		*incompat_flags = ret_incompat_flags;
	}

	return rc;
}
