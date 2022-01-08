/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_PIPE_INTERNAL_H__
#define __DAOS_PIPE_INTERNAL_H__

#include <daos_pipeline.h>


typedef struct {
	uint32_t	nr;
	daos_iod_t	*iods;
} daos_pipeline_iods_t;

typedef struct {
	uint32_t	nr;
	d_sg_list_t	*sgls;
} daos_pipeline_sgls_t;

void ds_pipeline_run_handler(crt_rpc_t *rpc);

int d_pipeline_check(daos_pipeline_t *pipeline);

#endif /* __DAOS_PIPE_INTERNAL_H__ */
