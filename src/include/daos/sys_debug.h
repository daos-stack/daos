/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * dc_debug: Client debug API
 */

#ifndef __DC_DEBUG_H__
#define __DC_DEBUG_H__

#include <daos/tse.h>

int dc_debug_set_params(tse_task_t *task);
int dc_debug_add_mark(const char *mark);

#endif
