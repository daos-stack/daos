
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

#define aggr_func_sum(typename, typec) \
int aggr_func_sum_##typename(struct filter_part_run_t *args)\
{\
	double  *aggr;\
	char    *ptr;\
	size_t  offset;\
	double  value;\
	int     rc;\
\
	args->part_idx += 1;\
	offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		return rc;\
	}\
	if (args->iov_out == NULL ||\
		offset + sizeof(_##typec) > args->iov_out->iov_len)\
	{\
		return 0;\
	}\
	ptr = args->iov_out->iov_buf;\
	ptr = &ptr[offset];\
	value  = (double) *((_##typec *) ptr);\
	aggr   = (double *) args->iov_aggr->iov_buf;\
	*aggr += value;\
\
	return 0;\
}

aggr_func_sum(i1, int8_t)
aggr_func_sum(i2, int16_t)
aggr_func_sum(i4, int32_t)
aggr_func_sum(i8, int64_t)
aggr_func_sum(r4, float)
aggr_func_sum(r8, double)

#define aggr_func_max(typename, typec) \
int aggr_func_max_##typename(struct filter_part_run_t *args)\
{\
	double  *aggr;\
	char    *ptr;\
	size_t  offset;\
	double  value;\
	int     rc;\
\
	args->part_idx += 1;\
	offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		return rc;\
	}\
	if (args->iov_out == NULL ||\
		offset + sizeof(_##typec) > args->iov_out->iov_len)\
	{\
		return 0;\
	}\
	ptr = args->iov_out->iov_buf;\
	ptr = &ptr[offset];\
	value  = (double) *((_##typec *) ptr);\
	aggr   = (double *) args->iov_aggr->iov_buf;\
	if (value > *aggr)\
	{\
		*aggr = value;\
	}\
\
	return 0;\
}

aggr_func_max(i1, int8_t)
aggr_func_max(i2, int16_t)
aggr_func_max(i4, int32_t)
aggr_func_max(i8, int64_t)
aggr_func_max(r4, float)
aggr_func_max(r8, double)

#define aggr_func_min(typename, typec) \
int aggr_func_min_##typename(struct filter_part_run_t *args)\
{\
	double  *aggr;\
	char    *ptr;\
	size_t  offset;\
	double  value;\
	int     rc;\
\
	args->part_idx += 1;\
	offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		return rc;\
	}\
	if (args->iov_out == NULL ||\
		offset + sizeof(_##typec) > args->iov_out->iov_len)\
	{\
		return 0;\
	}\
	ptr = args->iov_out->iov_buf;\
	ptr = &ptr[offset];\
	value  = (double) *((_##typec *) ptr);\
	aggr   = (double *) args->iov_aggr->iov_buf;\
	if (value < *aggr)\
	{\
		*aggr = value;\
	}\
\
	return 0;\
}

aggr_func_min(i1, int8_t)
aggr_func_min(i2, int16_t)
aggr_func_min(i4, int32_t)
aggr_func_min(i8, int64_t)
aggr_func_min(r4, float)
aggr_func_min(r8, double)
