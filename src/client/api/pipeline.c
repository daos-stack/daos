/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/pipeline.h>
#include <daos/task.h>
#include "client_internal.h"
#include "task_internal.h"


void
daos_pipeline_init(daos_pipeline_t *pipeline)
{
	pipeline->version		= 1;
	pipeline->num_filters		= 0;
	pipeline->filters		= NULL;
	pipeline->num_aggr_filters	= 0;
	pipeline->aggr_filters		= NULL;
}

void
daos_filter_init(daos_filter_t *filter)
{
	bzero((void *) filter, sizeof(daos_filter_t));
}

int
daos_pipeline_add(daos_pipeline_t *pipeline, daos_filter_t *filter)
{
	if (!strncmp((char *) filter->filter_type.iov_buf,
		     "DAOS_FILTER_CONDITION",
		     filter->filter_type.iov_len))
	{
		pipeline->filters = (daos_filter_t **)
			realloc
			(
			   (void *) pipeline->filters,
			   (pipeline->num_filters+1)*sizeof(daos_filter_t *)
			);
		if (pipeline->filters == NULL)
			return -DER_NOMEM;

		pipeline->filters[pipeline->num_filters] = filter;
		pipeline->num_filters                   += 1;
	}
	else if (!strncmp((char *) filter->filter_type.iov_buf,
			  "DAOS_FILTER_AGGREGATION",
			  filter->filter_type.iov_len))
	{
		pipeline->aggr_filters = (daos_filter_t **)
			realloc
			(
			  (void *) pipeline->aggr_filters,
			  (pipeline->num_aggr_filters+1)*sizeof(daos_filter_t *)
			);
		if (pipeline->aggr_filters == NULL)
			return -DER_NOMEM;

		pipeline->aggr_filters[pipeline->num_aggr_filters] = filter;
		pipeline->num_aggr_filters                        += 1;
	}
	else
	{
		return -DER_INVAL;
	}

	return 0;
}

int
daos_filter_add(daos_filter_t *filter, daos_filter_part_t *part)
{
	filter->parts = (daos_filter_part_t **)
			realloc
			(
			   (void *) filter->parts,
			   (filter->num_parts + 1) * sizeof(daos_filter_part_t)
			);
	if (filter->parts == NULL)
	{
		return -DER_NOMEM;
	}

	filter->parts[filter->num_parts] = part;
	filter->num_parts += 1;

	return 0;
}

int
daos_pipeline_check(daos_pipeline_t *pipeline)
{
	return dc_pipeline_check(pipeline);
}

int
daos_pipeline_run(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t pipeline,
		  daos_handle_t th, uint64_t flags, daos_key_t *dkey,
		  uint32_t *nr_iods, daos_iod_t *iods, daos_anchor_t *anchor,
		  uint32_t *nr_kds, daos_key_desc_t *kds, d_sg_list_t *sgl_keys,
		  d_sg_list_t *sgl_recx, daos_iom_t *ioms, d_sg_list_t *sgl_agg,
		  daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_pipeline_run_task_create(coh, oh, th, pipeline, flags, dkey,
					 nr_iods, iods, anchor, nr_kds, kds,
					 sgl_keys, sgl_recx, sgl_agg, NULL,
					 NULL, &task); 
	if (rc)
	{
		return rc;
	}
	return dc_task_schedule(task, true);
}
