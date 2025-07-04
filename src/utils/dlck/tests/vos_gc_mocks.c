/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos_errno.h>
#include <daos/mem.h>
#include <vos_layout.h>

int
__wrap_gc_init_cont(struct umem_instance *umm, struct vos_cont_df *cd)
{
	return DER_SUCCESS;
}
