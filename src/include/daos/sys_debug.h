/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_debug: Client debug API
 */

#ifndef __DC_DEBUG_H__
#define __DC_DEBUG_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_types.h>

int
dc_debug_set_params(tse_task_t *task);
int
dc_debug_add_mark(const char *mark);

#endif
