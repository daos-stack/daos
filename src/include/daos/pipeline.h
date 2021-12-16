
/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DD_PIPE_H__
#define __DD_PIPE_H__

#include <daos/common.h>
#include <daos/tse.h>

#if defined(__cplusplus)
extern "C" {
#endif


#include <daos_pipeline.h>

int
dc_pipeline_init(void);

void
dc_pipeline_fini(void);

int
dc_pipeline_check(daos_pipeline_t *pipeline);

int
dc_pipeline_run(tse_task_t *api_task);

/*
int
dc_pipeline_run(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t pipeline,
		daos_handle_t th, uint64_t flags, daos_key_t *dkey,
		uint32_t *nr_iods, daos_iod_t *iods, daos_anchor_t *anchor,
		uint32_t *nr_kds, daos_key_desc_t *kds, d_sg_list_t *sgl_keys,
		d_sg_list_t *sgl_recx, d_sg_list_t *sgl_agg, daos_event_t *ev);
*/

#if defined(__cplusplus)
}
#endif

#endif /* __DD_PIPE_H__ */
