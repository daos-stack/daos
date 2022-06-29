/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <math.h>
#include <string.h>
#include "pipeline_internal.h"
#include <daos/common.h>

#define NTYPES 13

static filter_func_t *filter_func_ptrs[53] = {
    filter_func_eq_u,   filter_func_eq_i,      filter_func_eq_d,     filter_func_eq_st,
    filter_func_ne_u,   filter_func_ne_i,      filter_func_ne_d,     filter_func_ne_st,
    filter_func_lt_u,   filter_func_lt_i,      filter_func_lt_d,     filter_func_lt_st,
    filter_func_le_u,   filter_func_le_i,      filter_func_le_d,     filter_func_le_st,
    filter_func_ge_u,   filter_func_ge_i,      filter_func_ge_d,     filter_func_ge_st,
    filter_func_gt_u,   filter_func_gt_i,      filter_func_gt_d,     filter_func_gt_st,
    filter_func_add_u,  filter_func_add_i,     filter_func_add_d,    filter_func_sub_u,
    filter_func_sub_i,  filter_func_sub_d,     filter_func_mul_u,    filter_func_mul_i,
    filter_func_mul_d,  filter_func_div_u,     filter_func_div_i,    filter_func_div_d,
    aggr_func_sum_u,    aggr_func_sum_i,       aggr_func_sum_d,      aggr_func_max_u,
    aggr_func_max_i,    aggr_func_max_d,       aggr_func_min_u,      aggr_func_min_i,
    aggr_func_min_d,    filter_func_bitand_u,  filter_func_bitand_i, filter_func_like,
    filter_func_isnull, filter_func_isnotnull, filter_func_not,      filter_func_and,
    filter_func_or};

static filter_func_t *getd_func_ptrs[39] = {
    getdata_func_dkey_u1,   getdata_func_dkey_u2,  getdata_func_dkey_u4,  getdata_func_dkey_u8,
    getdata_func_dkey_i1,   getdata_func_dkey_i2,  getdata_func_dkey_i4,  getdata_func_dkey_i8,
    getdata_func_dkey_r4,   getdata_func_dkey_r8,  getdata_func_dkey_raw, getdata_func_dkey_st,
    getdata_func_dkey_cst,  getdata_func_akey_u1,  getdata_func_akey_u2,  getdata_func_akey_u4,
    getdata_func_akey_u8,   getdata_func_akey_i1,  getdata_func_akey_i2,  getdata_func_akey_i4,
    getdata_func_akey_i8,   getdata_func_akey_r4,  getdata_func_akey_r8,  getdata_func_akey_raw,
    getdata_func_akey_st,   getdata_func_akey_cst, getdata_func_const_u1, getdata_func_const_u2,
    getdata_func_const_u4,  getdata_func_const_u8, getdata_func_const_i1, getdata_func_const_i2,
    getdata_func_const_i4,  getdata_func_const_i8, getdata_func_const_r4, getdata_func_const_r8,
    getdata_func_const_raw, getdata_func_const_st, getdata_func_const_cst};

void
pipeline_aggregations_init(daos_pipeline_t *pipeline, d_sg_list_t *sgl_agg)
{
	uint32_t            i;
	double             *buf;
	daos_filter_part_t *part;
	char               *part_type;
	size_t              part_type_s;

	for (i = 0; i < pipeline->num_aggr_filters; i++) {
		part        = pipeline->aggr_filters[i]->parts[0];
		buf         = (double *)sgl_agg->sg_iovs[i].iov_buf;
		part_type   = (char *)part->part_type.iov_buf;
		part_type_s = part->part_type.iov_len;

		if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
			*buf = -INFINITY;
		else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
			*buf = INFINITY;
		else
			*buf = 0;

		sgl_agg->sg_iovs[i].iov_len = sizeof(double);
	}
	sgl_agg->sg_nr_out = pipeline->num_aggr_filters;
}

static uint32_t
calc_type_nosize_idx(uint32_t idx)
{
	if (idx < 4)
		return 0;
	else if (idx < 8)
		return 1;
	else if (idx < 10)
		return 2;
	return 3;
}

static uint32_t
calc_type_idx(char *type, size_t type_len)
{
	if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER1", type_len))
		return 0;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER2", type_len))
		return 1;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER4", type_len))
		return 2;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER8", type_len))
		return 3;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER1", type_len))
		return 4;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER2", type_len))
		return 5;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER4", type_len))
		return 6;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER8", type_len))
		return 7;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL4", type_len))
		return 8;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL8", type_len))
		return 9;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_BINARY", type_len))
		return 10;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_STRING", type_len))
		return 11;
	else /* DAOS_FILTER_TYPE_CSTRING */
		return 12;
}

static uint32_t
calc_filterfunc_idx(daos_filter_part_t **parts, uint32_t idx)
{
	char  *part_type;
	size_t part_type_s;

	part_type   = (char *)parts[idx]->part_type.iov_buf;
	part_type_s = parts[idx]->part_type.iov_len;

	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s))
		return 0;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s))
		return 4;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s))
		return 8;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s))
		return 12;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s))
		return 16;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s))
		return 20;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s))
		return 24;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s))
		return 27;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s))
		return 30;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s))
		return 33;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
		return 36;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
		return 39;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
		return 42;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_BITAND", part_type_s))
		return 45;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s))
		return 47;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s))
		return 48;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s))
		return 49;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s))
		return 50;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s))
		return 51;
	else /* if (!strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)) */
		return 52;
}

static uint32_t
calc_num_operands(daos_filter_part_t **parts, uint32_t idx)
{
	daos_filter_part_t *child_part;
	char               *part_type;
	size_t              part_type_s;
	uint32_t            nops;

	part_type   = (char *)parts[idx]->part_type.iov_buf;
	part_type_s = parts[idx]->part_type.iov_len;
	nops        = parts[idx]->num_operands;

	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s)) {
		child_part = parts[idx + 2];

		if (!strncmp((char *)child_part->part_type.iov_buf, "DAOS_FILTER_CONST",
			     child_part->part_type.iov_len))
			nops += child_part->num_constants - 1;
	}
	return nops;
}

static int
compile_filter(daos_filter_t *filter, struct filter_compiled_t *comp_filter, uint32_t *part_idx,
	       uint32_t *comp_part_idx, char **type, size_t *type_len)
{
	uint32_t                        nops;
	uint32_t                        func_idx;
	uint32_t                        type_idx;
	uint32_t                        i;
	size_t                          j;
	char                           *part_type;
	size_t                          part_type_s;
	size_t                          comp_size;
	struct filter_part_compiled_t  *comp_part;
	int                             rc = 0;
	uint32_t                        idx;

	part_type   = (char *)filter->parts[*part_idx]->part_type.iov_buf;
	part_type_s = filter->parts[*part_idx]->part_type.iov_len;

	comp_part   = &comp_filter->parts[*comp_part_idx];
	*comp_part  = (struct filter_part_compiled_t){0};

	if (part_type_s < strlen("DAOS_FILTER_FUNC"))
		comp_size = part_type_s;
	else
		comp_size = strlen("DAOS_FILTER_FUNC");

	if (strncmp(part_type, "DAOS_FILTER_FUNC", comp_size)) { /** != FUNC */
		*type     = (char *)filter->parts[*part_idx]->data_type.iov_buf;
		*type_len = filter->parts[*part_idx]->data_type.iov_len;
		func_idx  = calc_type_idx(*type, *type_len);

		if (!strncmp(part_type, "DAOS_FILTER_AKEY", part_type_s)) {
			func_idx += NTYPES;
			comp_part->data_offset = filter->parts[*part_idx]->data_offset;
			comp_part->data_len    = filter->parts[*part_idx]->data_len;
			comp_part->iov         = &filter->parts[*part_idx]->akey;
			comp_part->filter_func = getd_func_ptrs[func_idx];
		} else if (!strncmp(part_type, "DAOS_FILTER_CONST", part_type_s)) {
			func_idx += NTYPES * 2;
			comp_part->data_offset = 0;
			comp_part->iov         = &filter->parts[*part_idx]->constant[0];
			comp_part->data_len    = comp_part->iov->iov_len;
			comp_part->filter_func = getd_func_ptrs[func_idx];

			for (j = 1; j < filter->parts[*part_idx]->num_constants; j++) {
				*comp_part_idx += 1;
				comp_part              = &comp_filter->parts[*comp_part_idx];
				*comp_part             = (struct filter_part_compiled_t){0};

				comp_part->data_offset = 0;
				comp_part->iov         = &filter->parts[*part_idx]->constant[j];
				comp_part->data_len    = comp_part->iov->iov_len;
				comp_part->filter_func = getd_func_ptrs[func_idx];
			}
		} else if (!strncmp(part_type, "DAOS_FILTER_DKEY", part_type_s)) {
			comp_part->data_offset = filter->parts[*part_idx]->data_offset;
			comp_part->data_len    = filter->parts[*part_idx]->data_len;
			comp_part->filter_func = getd_func_ptrs[func_idx];
		}
		D_GOTO(exit, rc = 0);
	}

	nops                    = calc_num_operands(filter->parts, *part_idx);
	comp_part->num_operands = nops;

	/** recursive calls for function parameters */
	idx                     = *part_idx;
	for (i = 0; i < filter->parts[idx]->num_operands; i++) {
		*comp_part_idx += 1;
		*part_idx += 1;
		rc = compile_filter(filter, comp_filter, part_idx, comp_part_idx, type, type_len);
		if (rc != 0)
			D_GOTO(exit, rc);
	}

	func_idx = calc_filterfunc_idx(filter->parts, idx);
	type_idx = calc_type_nosize_idx(calc_type_idx(*type, *type_len));

	if (func_idx < 47)
		func_idx += type_idx;

	comp_part->filter_func     = filter_func_ptrs[func_idx];
	comp_part->idx_end_subtree = *comp_part_idx;
exit:
	return rc;
}

static int
compile_filters(daos_filter_t **ftrs, uint32_t nftrs, struct filter_compiled_t *c_ftrs)
{
	uint32_t            part_idx;
	uint32_t            comp_part_idx;
	uint32_t            comp_num_parts;
	uint32_t            i, j;
	char	       *type;
	size_t              type_len;
	int                 rc = 0;
	daos_filter_part_t *part;

	for (i = 0; i < nftrs; i++) {
		comp_num_parts = ftrs[i]->num_parts;

		for (j = 0; j < ftrs[i]->num_parts; j++) {
			part = ftrs[i]->parts[j];
			if (!strncmp((char *)part->part_type.iov_buf, "DAOS_FILTER_CONST",
				     part->part_type.iov_len))
				comp_num_parts += part->num_constants - 1;
		}

		D_ALLOC_ARRAY(c_ftrs[i].parts, comp_num_parts);
		if (c_ftrs[i].parts == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);

		c_ftrs[i].num_parts = comp_num_parts;
		part_idx            = 0;
		comp_part_idx       = 0;
		type                = NULL;
		type_len            = 0;
		rc = compile_filter(ftrs[i], &c_ftrs[i], &part_idx, &comp_part_idx, &type,
				    &type_len);
		if (rc != 0)
			D_GOTO(exit, rc);
	}
exit:
	return rc;
}

int
pipeline_compile(daos_pipeline_t *pipe, struct pipeline_compiled_t *comp_pipe)
{
	int rc                      = 0;

	comp_pipe->num_filters      = 0;
	comp_pipe->filters          = NULL;
	comp_pipe->num_aggr_filters = 0;
	comp_pipe->aggr_filters     = NULL;

	if (pipe->num_filters > 0) {
		D_ALLOC_ARRAY(comp_pipe->filters, pipe->num_filters);
		if (comp_pipe->filters == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);

		comp_pipe->num_filters = pipe->num_filters;
		rc = compile_filters(pipe->filters, pipe->num_filters, comp_pipe->filters);
		if (rc != 0)
			D_GOTO(exit, rc);
	}
	if (pipe->num_aggr_filters > 0) {
		D_ALLOC_ARRAY(comp_pipe->aggr_filters, pipe->num_aggr_filters);
		if (comp_pipe->aggr_filters == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);

		comp_pipe->num_aggr_filters = pipe->num_aggr_filters;
		rc = compile_filters(pipe->aggr_filters, pipe->num_aggr_filters,
				     comp_pipe->aggr_filters);
		if (rc != 0)
			D_GOTO(exit, rc);
	}
exit:
	return rc;
}

void
pipeline_compile_free(struct pipeline_compiled_t *comp_pipe)
{
	uint32_t i;

	if (comp_pipe->num_filters > 0) {
		for (i = 0; i < comp_pipe->num_filters; i++) {
			if (comp_pipe->filters[i].num_parts > 0)
				D_FREE(comp_pipe->filters[i].parts);
		}
		D_FREE(comp_pipe->filters);
	}
	if (comp_pipe->num_aggr_filters > 0) {
		for (i = 0; i < comp_pipe->num_aggr_filters; i++) {
			if (comp_pipe->aggr_filters[i].num_parts > 0)
				D_FREE(comp_pipe->aggr_filters[i].parts);
		}
		D_FREE(comp_pipe->aggr_filters);
	}
}
