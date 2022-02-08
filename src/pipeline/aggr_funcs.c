
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

int
aggr_func_sum_i1(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int8_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	*aggr += value;

	return 0;
}

int
aggr_func_sum_i2(struct filter_part_run_t *args)
{
	double  *aggr;
	double  value;
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
	value  = (double) *((int16_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	*aggr += value;

	return 0;
}

int
aggr_func_sum_i4(struct filter_part_run_t *args)
{
	double  *aggr;
	double  value;
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
	value  = (double) *((int32_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	*aggr += value;

	return 0;
}

int
aggr_func_sum_i8(struct filter_part_run_t *args)
{
	double  *aggr;
	double  value;
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
	value  = (double) *((int64_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	*aggr += value;

	return 0;
}

int
aggr_func_sum_r4(struct filter_part_run_t *args)
{
	double *aggr;
	double  value;
	int    rc;

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
	value  = (double) *((float *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	*aggr += value;

	return 0;
}

int
aggr_func_sum_r8(struct filter_part_run_t *args)
{
	double  *aggr;
	double  value;
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
	value  = *((double *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	*aggr += value;

	return 0;
}

int
aggr_func_max_i1(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int8_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value > *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_max_i2(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int16_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value > *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_max_i4(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int32_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value > *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_max_i8(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int64_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value > *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_max_r4(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((float *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value > *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_max_r8(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = *((double *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value > *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_min_i1(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int8_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value < *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_min_i2(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int16_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value < *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_min_i4(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int32_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value < *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_min_i8(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((int64_t *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value < *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_min_r4(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = (double) *((float *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value < *aggr)
	{
		*aggr = value;
	}

	return 0;
}

int
aggr_func_min_r8(struct filter_part_run_t *args)
{
	double *aggr;
	double value;
	int    rc;

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
	value  = *((double *) args->iov_out->iov_buf);
	aggr   = (double *) args->iov_aggr->iov_buf;
	if (value < *aggr)
	{
		*aggr = value;
	}

	return 0;
}

