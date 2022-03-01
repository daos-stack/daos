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
	size_t				data_offset_out;
	size_t				data_len_out;
	bool				log_out;
	d_iov_t				*iov_aggr;
	double				value_out;
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

typedef int8_t _int8_t;
typedef int16_t _int16_t;
typedef int32_t _int32_t;
typedef int64_t _int64_t;
typedef float _float;
typedef double _double;

filter_func_t filter_func_eq;
filter_func_t filter_func_eq_st;

filter_func_t filter_func_ne;
filter_func_t filter_func_ne_st;

filter_func_t filter_func_lt;
filter_func_t filter_func_lt_st;

filter_func_t filter_func_le;
filter_func_t filter_func_le_st;

filter_func_t filter_func_ge;
filter_func_t filter_func_ge_st;

filter_func_t filter_func_gt;
filter_func_t filter_func_gt_st;

filter_func_t filter_func_add;
filter_func_t filter_func_sub;
filter_func_t filter_func_mul;
filter_func_t filter_func_div;

filter_func_t aggr_func_sum;
filter_func_t aggr_func_max;
filter_func_t aggr_func_min;

filter_func_t filter_func_like;
filter_func_t filter_func_isnull;
filter_func_t filter_func_isnotnull;
filter_func_t filter_func_not;
filter_func_t filter_func_and;
filter_func_t filter_func_or;

filter_func_t getdata_func_dkey_i1;
filter_func_t getdata_func_dkey_i2;
filter_func_t getdata_func_dkey_i4;
filter_func_t getdata_func_dkey_i8;
filter_func_t getdata_func_dkey_r4;
filter_func_t getdata_func_dkey_r8;
filter_func_t getdata_func_dkey_raw;
filter_func_t getdata_func_dkey_st;
filter_func_t getdata_func_dkey_cst;

filter_func_t getdata_func_akey_i1;
filter_func_t getdata_func_akey_i2;
filter_func_t getdata_func_akey_i4;
filter_func_t getdata_func_akey_i8;
filter_func_t getdata_func_akey_r4;
filter_func_t getdata_func_akey_r8;
filter_func_t getdata_func_akey_raw;
filter_func_t getdata_func_akey_st;
filter_func_t getdata_func_akey_cst;

filter_func_t getdata_func_const_i1;
filter_func_t getdata_func_const_i2;
filter_func_t getdata_func_const_i4;
filter_func_t getdata_func_const_i8;
filter_func_t getdata_func_const_r4;
filter_func_t getdata_func_const_r8;
filter_func_t getdata_func_const_raw;

#endif /* __DAOS_PIPE_INTERNAL_H__ */
