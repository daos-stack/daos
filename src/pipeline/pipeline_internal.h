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

void pipeline_aggregations_init(daos_pipeline_t *pipeline,
				d_sg_list_t *sgl_agg);

void pipeline_aggregations_fixavgs(daos_pipeline_t *pipeline, double total,
				   d_sg_list_t *sgl_agg);

int pipeline_aggregations(daos_pipeline_t *pipeline, d_iov_t *dkey,
			  uint32_t *nr_iods, daos_iod_t *iods,
			  d_sg_list_t *akeys, d_sg_list_t *sgl_agg);

int pipeline_filters(daos_pipeline_t *pipeline, d_iov_t *dkey,
		     uint32_t *nr_iods, daos_iod_t *iods, d_sg_list_t *akeys);

#endif /* __DAOS_PIPE_INTERNAL_H__ */
