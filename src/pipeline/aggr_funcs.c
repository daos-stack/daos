
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

#define aggr_func_sum(type)\
int aggr_func_sum_##type(struct filter_part_run_t *args)\
{\
	double  *aggr;\
	int     rc;\
\
	args->part_idx += 1;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		return rc;\
	}\
	if (args->iov_out == NULL)\
	{\
		return 0;\
	}\
\
	aggr   = (double *) args->iov_aggr->iov_buf;\
	*aggr += (double) args->value_##type##_out;\
	return 0;\
}

aggr_func_sum(u)
aggr_func_sum(i)
aggr_func_sum(d)

#define aggr_func_max(type)\
int aggr_func_max_##type(struct filter_part_run_t *args)\
{\
	double  val;\
	double  *aggr;\
	int     rc;\
\
	args->part_idx += 1;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		return rc;\
	}\
	if (args->iov_out == NULL)\
	{\
		return 0;\
	}\
\
	aggr   = (double *) args->iov_aggr->iov_buf;\
	val    = (double) args->value_##type##_out;\
	if (val > *aggr)\
	{\
		*aggr = val;\
	}\
	return 0;\
}

aggr_func_max(u)
aggr_func_max(i)
aggr_func_max(d)

#define aggr_func_min(type)\
int aggr_func_min_##type(struct filter_part_run_t *args)\
{\
	double  val;\
	double  *aggr;\
	int     rc;\
\
	args->part_idx += 1;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		return rc;\
	}\
	if (args->iov_out == NULL)\
	{\
		return 0;\
	}\
\
	aggr   = (double *) args->iov_aggr->iov_buf;\
	val    = (double) args->value_##type##_out;\
	if (val < *aggr)\
	{\
		*aggr = val;\
	}\
	return 0;\
}

aggr_func_min(u)
aggr_func_min(i)
aggr_func_min(d)

