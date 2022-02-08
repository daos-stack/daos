/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_PIPE_INTERNAL_H__
#define __DAOS_PIPE_INTERNAL_H__

#include <daos_pipeline.h>


struct filter_part_run_t {
	d_iov_t				*dkey;
	uint32_t			nr_iods;
	daos_iod_t			*iods;
	d_sg_list_t			*akeys;
	struct filter_part_compiled_t	*parts;
	uint32_t			part_idx;
	d_iov_t				*iov_out;
	bool				log_out;
	d_iov_t				*iov_aggr;
	d_iov_t				iov_extra;
};

typedef int filter_func_t(struct filter_part_run_t *args);

struct filter_part_compiled_t {
	uint32_t	num_operands;
	d_iov_t		*iov;
	size_t		data_offset;
	size_t		data_len;
	filter_func_t	*filter_func;
};

struct filter_compiled_t {
	uint32_t			num_parts;
	struct filter_part_compiled_t	*parts;
};

struct pipeline_compiled_t {
	uint32_t			num_filters;
	struct filter_compiled_t	*filters;
	uint32_t			num_aggr_filters;
	struct filter_compiled_t	*aggr_filters;
};

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

int pipeline_compile(daos_pipeline_t *pipe,
		     struct pipeline_compiled_t *comp_pipe);

void pipeline_compile_free(struct pipeline_compiled_t *comp_pipe);


filter_func_t filter_func_eq_i1;
filter_func_t filter_func_eq_i2;
filter_func_t filter_func_eq_i4;
filter_func_t filter_func_eq_i8;
filter_func_t filter_func_eq_r4;
filter_func_t filter_func_eq_r8;
filter_func_t filter_func_eq_st;
filter_func_t filter_func_eq_raw;

filter_func_t filter_func_in_i1;
filter_func_t filter_func_in_i2;
filter_func_t filter_func_in_i4;
filter_func_t filter_func_in_i8;
filter_func_t filter_func_in_r4;
filter_func_t filter_func_in_r8;
filter_func_t filter_func_in_st;
filter_func_t filter_func_in_raw;

filter_func_t filter_func_ne_i1;
filter_func_t filter_func_ne_i2;
filter_func_t filter_func_ne_i4;
filter_func_t filter_func_ne_i8;
filter_func_t filter_func_ne_r4;
filter_func_t filter_func_ne_r8;
filter_func_t filter_func_ne_st;
filter_func_t filter_func_ne_raw;

filter_func_t filter_func_lt_i1;
filter_func_t filter_func_lt_i2;
filter_func_t filter_func_lt_i4;
filter_func_t filter_func_lt_i8;
filter_func_t filter_func_lt_r4;
filter_func_t filter_func_lt_r8;
filter_func_t filter_func_lt_st;
filter_func_t filter_func_lt_raw;

filter_func_t filter_func_le_i1;
filter_func_t filter_func_le_i2;
filter_func_t filter_func_le_i4;
filter_func_t filter_func_le_i8;
filter_func_t filter_func_le_r4;
filter_func_t filter_func_le_r8;
filter_func_t filter_func_le_st;
filter_func_t filter_func_le_raw;

filter_func_t filter_func_ge_i1;
filter_func_t filter_func_ge_i2;
filter_func_t filter_func_ge_i4;
filter_func_t filter_func_ge_i8;
filter_func_t filter_func_ge_r4;
filter_func_t filter_func_ge_r8;
filter_func_t filter_func_ge_st;
filter_func_t filter_func_ge_raw;

filter_func_t filter_func_gt_i1;
filter_func_t filter_func_gt_i2;
filter_func_t filter_func_gt_i4;
filter_func_t filter_func_gt_i8;
filter_func_t filter_func_gt_r4;
filter_func_t filter_func_gt_r8;
filter_func_t filter_func_gt_st;
filter_func_t filter_func_gt_raw;

filter_func_t filter_func_add_i1;
filter_func_t filter_func_add_i2;
filter_func_t filter_func_add_i4;
filter_func_t filter_func_add_i8;
filter_func_t filter_func_add_r4;
filter_func_t filter_func_add_r8;

filter_func_t filter_func_sub_i1;
filter_func_t filter_func_sub_i2;
filter_func_t filter_func_sub_i4;
filter_func_t filter_func_sub_i8;
filter_func_t filter_func_sub_r4;
filter_func_t filter_func_sub_r8;

filter_func_t filter_func_mul_i1;
filter_func_t filter_func_mul_i2;
filter_func_t filter_func_mul_i4;
filter_func_t filter_func_mul_i8;
filter_func_t filter_func_mul_r4;
filter_func_t filter_func_mul_r8;

filter_func_t filter_func_div_i1;
filter_func_t filter_func_div_i2;
filter_func_t filter_func_div_i4;
filter_func_t filter_func_div_i8;
filter_func_t filter_func_div_r4;
filter_func_t filter_func_div_r8;

filter_func_t aggr_func_sum_i1;
filter_func_t aggr_func_sum_i2;
filter_func_t aggr_func_sum_i4;
filter_func_t aggr_func_sum_i8;
filter_func_t aggr_func_sum_r4;
filter_func_t aggr_func_sum_r8;

filter_func_t aggr_func_max_i1;
filter_func_t aggr_func_max_i2;
filter_func_t aggr_func_max_i4;
filter_func_t aggr_func_max_i8;
filter_func_t aggr_func_max_r4;
filter_func_t aggr_func_max_r8;

filter_func_t aggr_func_min_i1;
filter_func_t aggr_func_min_i2;
filter_func_t aggr_func_min_i4;
filter_func_t aggr_func_min_i8;
filter_func_t aggr_func_min_r4;
filter_func_t aggr_func_min_r8;

filter_func_t filter_func_like_st;
filter_func_t filter_func_isnull_raw;
filter_func_t filter_func_isnotnull_raw;
filter_func_t filter_func_not;
filter_func_t filter_func_and;
filter_func_t filter_func_or;

filter_func_t getdata_func_dkey;
filter_func_t getdata_func_akey;
filter_func_t getdata_func_const;

#endif /* __DAOS_PIPE_INTERNAL_H__ */
