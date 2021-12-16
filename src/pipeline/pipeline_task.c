/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/pipeline.h>
#include <daos_task.h>
#include <daos/task.h>
#include <daos/event.h>


int
dc_pipeline_run_task_create(daos_handle_t coh, daos_handle_t oh,
			    daos_handle_t th, daos_pipeline_t pipeline,
			    daos_event_t *ev, tse_sched_t *tse,
			    tse_task_t **task)
{
	daos_pipeline_run_t	*args;
	int			rc;

	DAOS_API_ARG_ASSERT(*args, PIPELINE_RUN);
	rc = dc_task_create(dc_pipeline_run, tse, ev, task);
	if (rc)
	{
		return rc;
	}

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->th	= th;
	args->pipeline	= pipeline;

	return 0;
}
