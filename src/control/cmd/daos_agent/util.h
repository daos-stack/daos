/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CMD_DAOS_AGENT_UTIL_H__
#define __CMD_DAOS_AGENT_UTIL_H__

#define D_LOGFAC	DD_FAC(client)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <daos/common.h>
#include <daos.h>

/* client shmem cache operations */
int setup_client_cache(size_t size);
int destroy_client_cache(size_t size);

#endif /* __CMD_DAOS_AGENT_UTIL_H__ */
