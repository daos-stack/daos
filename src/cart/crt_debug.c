/*
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <crt_internal.h>

#define CART_FAC_MAX_LEN (128)

CRT_FOREACH_LOG_FAC(D_LOG_INSTANTIATE_FAC, D_NOOP)

int
crt_setup_log_fac(void)
{
	int rc = D_LOG_REGISTER_FAC(CRT_FOREACH_LOG_FAC);

	if (rc != 0)
		return rc;

	d_log_sync_mask();

	return 0;
}
