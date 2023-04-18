
/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

/**
 * These macros define the SUM(), MAX(), and MIN() functions for the three main types: unsigned
 * int, integer, and double.
 */

#define DEFINE_AGGR_FUNC_SUM(type)                                                                 \
	int aggr_func_sum_##type(struct filter_part_run_t *args)                            \
	{                                                                                          \
		double *aggr;                                                                      \
		int     rc;                                                                        \
		args->part_idx += 1;                                                               \
		rc = args->parts[args->part_idx].filter_func(args);                                \
		if (unlikely(rc != 0))                                                             \
			return rc > 0 ? 0 : rc;                                                    \
		if (args->data_out == NULL)                                                        \
			return 0;                                                                  \
		aggr = (double *)args->iov_aggr->iov_buf;                                          \
		*aggr += (double)args->value_##type##_out;                                         \
		return 0;                                                                          \
	}

DEFINE_AGGR_FUNC_SUM(u)
DEFINE_AGGR_FUNC_SUM(i)
DEFINE_AGGR_FUNC_SUM(d)

#define DEFINE_AGGR_FUNC_MAX(type)                                                                 \
	int aggr_func_max_##type(struct filter_part_run_t *args)                                   \
	{                                                                                          \
		double  val;                                                                       \
		double *aggr;                                                                      \
		int     rc;                                                                        \
		args->part_idx += 1;                                                               \
		rc = args->parts[args->part_idx].filter_func(args);                                \
		if (unlikely(rc != 0))                                                             \
			return rc > 0 ? 0 : rc;                                                    \
		if (args->data_out == NULL)                                                        \
			return 0;                                                                  \
		aggr = (double *)args->iov_aggr->iov_buf;                                          \
		val  = (double)args->value_##type##_out;                                           \
		if (val > *aggr)                                                                   \
			*aggr = val;                                                               \
		return 0;                                                                          \
	}

DEFINE_AGGR_FUNC_MAX(u)
DEFINE_AGGR_FUNC_MAX(i)
DEFINE_AGGR_FUNC_MAX(d)

#define DEFINE_AGGR_FUNC_MIN(type)                                                                 \
	int aggr_func_min_##type(struct filter_part_run_t *args)                                   \
	{                                                                                          \
		double  val;                                                                       \
		double *aggr;                                                                      \
		int     rc;                                                                        \
		args->part_idx += 1;                                                               \
		rc = args->parts[args->part_idx].filter_func(args);                                \
		if (unlikely(rc != 0))                                                             \
			return rc > 0 ? 0 : rc;                                                    \
		if (args->data_out == NULL)                                                        \
			return 0;                                                                  \
		aggr = (double *)args->iov_aggr->iov_buf;                                          \
		val  = (double)args->value_##type##_out;                                           \
		if (val < *aggr)                                                                   \
			*aggr = val;                                                               \
		return 0;                                                                          \
	}

DEFINE_AGGR_FUNC_MIN(u)
DEFINE_AGGR_FUNC_MIN(i)
DEFINE_AGGR_FUNC_MIN(d)
