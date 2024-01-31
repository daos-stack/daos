/**
 * (C) Copyright 2015-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/pipeline.h>
#include <daos_task.h>
#include <daos/task.h>
#include <daos/event.h>

int
dc_pipeline_run_task_create(daos_handle_t coh, daos_handle_t oh, daos_handle_t th,
			    daos_pipeline_t *pipeline, uint64_t flags, daos_key_t *dkey,
			    uint32_t *nr_iods, daos_iod_t *iods, daos_anchor_t *anchor,
			    uint32_t *nr_kds, daos_key_desc_t *kds, d_sg_list_t *sgl_keys,
			    d_sg_list_t *sgl_recx, daos_size_t *recx_size, d_sg_list_t *sgl_agg,
			    daos_pipeline_stats_t *stats, daos_event_t *ev, tse_sched_t *tse,
			    tse_task_t **task)
{
	daos_pipeline_run_t *args;
	int                  rc;

	DAOS_API_ARG_ASSERT(*args, PIPELINE_RUN);
	rc = dc_task_create(dc_pipeline_run, tse, ev, task);
	if (rc)
		return rc;

	args               = dc_task_get_args(*task);
	args->oh           = oh;
	args->th           = th;
	args->pipeline     = pipeline;
	args->flags        = flags;
	args->dkey         = dkey;
	args->nr_iods      = nr_iods;
	args->iods         = iods;
	args->anchor       = anchor;
	args->nr_kds       = nr_kds;
	args->kds          = kds;
	args->sgl_keys     = sgl_keys;
	args->sgl_recx     = sgl_recx;
	args->recx_size    = recx_size;
	args->sgl_agg      = sgl_agg;
	args->stats        = stats;

	return 0;
}
