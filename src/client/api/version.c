/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/client/api/version.c
 */
#define D_LOGFAC DD_FAC(client)

#include <daos/common.h>
#include <daos_version.h>

/**
 * Retrieve DAOS client API version.
 */
int
daos_version_get(int *major, int *minor, int *fix)
{
	if (major == NULL || minor == NULL || fix == NULL) {
		D_ERROR("major, minor, fix must not be NULL\n");
		return -DER_INVAL;
	}
	*major = DAOS_API_VERSION_MAJOR;
	*minor = DAOS_API_VERSION_MINOR;
	*fix   = DAOS_API_VERSION_FIX;

	return 0;
}