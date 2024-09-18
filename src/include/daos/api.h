/**
 * (C) Copyright 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Miscellaneous Client API Functions
 */

#ifndef __DAOS_API_H__
#define __DAOS_API_H__

#include <sys/types.h>

/**
 * Retrieve the daos API version.
 *
 * \param[out]	major	Pointer to an int that will be set to DAOS_API_VERSION_MAJOR.
 * \param[out]	minor	Pointer to an int that will be set to DAOS_API_VERSION_MINOR.
 * \param[out]	fix	    Pointer to an int that will be set to DAOS_API_VERSION_FIX.
 *
 * \return		0 if Success, negative if failed.
 * \retval -DER_INVAL	Invalid parameter.
 */
int
daos_version_get(int *major, int *minor, int *fix);

#endif