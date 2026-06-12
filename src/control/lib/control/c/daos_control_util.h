/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __DAOS_CONTROL_C_UTIL_H__
#define __DAOS_CONTROL_C_UTIL_H__

#include <daos_prop.h>

/*
 * cgo is unable to work directly with unions, so we have
 * to provide these glue helpers.
 */
static inline char *
get_dpe_str(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_str;
}

static inline uint64_t
get_dpe_val(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return 0;

	return dpe->dpe_val;
}

static inline void *
get_dpe_val_ptr(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_val_ptr;
}

#endif /* __DAOS_CONTROL_C_UTIL_H__ */
