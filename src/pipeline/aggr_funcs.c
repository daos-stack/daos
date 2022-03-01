
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

int aggr_func_sum(struct filter_part_run_t *args)
{
	double  *aggr;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	if (args->iov_out == NULL)
	{
		return 0;
	}

	aggr   = (double *) args->iov_aggr->iov_buf;
	*aggr += args->value_out;
	return 0;
}

int aggr_func_max(struct filter_part_run_t *args)
{
	double  *aggr;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	if (args->iov_out == NULL)
	{
		return 0;
	}

	aggr   = (double *) args->iov_aggr->iov_buf;
	if (args->value_out > *aggr)
	{
		*aggr = args->value_out;
	}
	return 0;
}

int aggr_func_min(struct filter_part_run_t *args)
{
	double  *aggr;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	if (args->iov_out == NULL)
	{
		return 0;
	}

	aggr   = (double *) args->iov_aggr->iov_buf;
	if (args->value_out < *aggr)
	{
		*aggr = args->value_out;
	}
	return 0;
}

