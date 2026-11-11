/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_CLI_MEM_ATTR_H
#define DAOS_CLI_MEM_ATTR_H

#include <daos/common.h>
#include <daos/object.h>
#include <cart/api.h>

/** A region of daos_mem_register() is a CaRT registered region, see crt_mem_register(). */
static inline crt_mem_handle_t
dc_mem_handle2crt(daos_mem_handle_t mh)
{
	return (crt_mem_handle_t)mh;
}

/* dc_obj_mem_attrs_{valid,dup,free}() are in daos/object.h, the array layer uses them too */

/**
 * Make sure the registered region in \a mem_attr, if any, is registered on the CaRT context
 * \a ctx of the I/O, registering it on first use. The caller holds a reference on the handle.
 */
int
dc_obj_mem_attr_ctx_prep(const daos_mem_attr_t *mem_attr, crt_context_t ctx);

/** Whether any of the \a nr memory attributes carries a registered region. */
bool
dc_obj_mem_attrs_has_reg(const daos_mem_attr_t *mem_attrs, unsigned int nr);

#endif /* DAOS_CLI_MEM_ATTR_H */
