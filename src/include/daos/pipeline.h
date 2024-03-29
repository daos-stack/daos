
/**
 * (C) Copyright 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DD_PIPE_H__
#define __DD_PIPE_H__

#include <daos/common.h>
#include <daos/tse.h>
#include <daos_pipeline.h>

int
dc_pipeline_init(void);

void
dc_pipeline_fini(void);

int
dc_pipeline_check(daos_pipeline_t *pipeline);

int
dc_pipeline_run(tse_task_t *api_task);

#endif /* __DD_PIPE_H__ */
