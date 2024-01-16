/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <math.h>
#include <string.h>
#include "pipeline_internal.h"
#include <daos/common.h>

#define NTYPES              13
#define NTYPES_NOSIZE        4
#define N_FILTER_FUNC_PTRS  53
#define N_GETD_FUNC_PTRS    39

#define SUBIDX_UINTEGER1  0
#define SUBIDX_UINTEGER2  1
#define SUBIDX_UINTEGER4  2
#define SUBIDX_UINTEGER8  3
#define SUBIDX_INTEGER1   4
#define SUBIDX_INTEGER2   5
#define SUBIDX_INTEGER4   6
#define SUBIDX_INTEGER8   7
#define SUBIDX_REAL4      8
#define SUBIDX_REAL8      9
#define SUBIDX_BINARY    10
#define SUBIDX_STRING    11
#define SUBIDX_CSTRING   12

#define SUBIDX_UINTEGER   0
#define SUBIDX_INTEGER    1
#define SUBIDX_DOUBLE     2
#define SUBIDX_STR        3

#define SUBIDX_FUNC_EQ        0
#define SUBIDX_FUNC_NE        NTYPES_NOSIZE
#define SUBIDX_FUNC_LT        (NTYPES_NOSIZE * 2)
#define SUBIDX_FUNC_LE        (NTYPES_NOSIZE * 3)
#define SUBIDX_FUNC_GE        (NTYPES_NOSIZE * 4)
#define SUBIDX_FUNC_GT        (NTYPES_NOSIZE * 5)

#define SUBIDX_FUNC_ADD       (NTYPES_NOSIZE * 6) /** these do not work with strings */
#define SUBIDX_FUNC_SUB       (SUBIDX_FUNC_ADD + (NTYPES_NOSIZE - 1))
#define SUBIDX_FUNC_MUL       (SUBIDX_FUNC_ADD + (NTYPES_NOSIZE - 1) * 2)
#define SUBIDX_FUNC_DIV       (SUBIDX_FUNC_ADD + (NTYPES_NOSIZE - 1) * 3)
#define SUBIDX_FUNC_SUM       (SUBIDX_FUNC_ADD + (NTYPES_NOSIZE - 1) * 4)
#define SUBIDX_FUNC_MAX       (SUBIDX_FUNC_ADD + (NTYPES_NOSIZE - 1) * 5)
#define SUBIDX_FUNC_MIN       (SUBIDX_FUNC_ADD + (NTYPES_NOSIZE - 1) * 6)

#define SUBIDX_FUNC_BITAND    (SUBIDX_FUNC_ADD + (NTYPES_NOSIZE - 1) * 7) /** only works w/ int */

#define SUBIDX_FUNC_LIKE      (SUBIDX_FUNC_BITAND + (NTYPES_NOSIZE - 2))  /** only works w/ str */
#define SUBIDX_FUNC_ISNULL    (SUBIDX_FUNC_LIKE + 1)/** type is N/A */
#define SUBIDX_FUNC_ISNOTNULL (SUBIDX_FUNC_LIKE + 2)
#define SUBIDX_FUNC_NOT       (SUBIDX_FUNC_LIKE + 3)
#define SUBIDX_FUNC_AND       (SUBIDX_FUNC_LIKE + 4)
#define SUBIDX_FUNC_OR        (SUBIDX_FUNC_LIKE + 5)

#define SUBIDX_FUNCS_WITH_ONE_TYPE_ONLY SUBIDX_FUNC_LIKE

static filter_func_t *filter_func_ptrs[N_FILTER_FUNC_PTRS] = {
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

static filter_func_t *getd_func_ptrs[N_GETD_FUNC_PTRS] = {
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

/**
 * calculates the subindex of a function in filter_func_ptrs[] given its index calculated with the
 * function calc_type_idx(). There is only 4 types if we don't consider the size: unsigned int,
 * signed int, double and string.
 */
static uint32_t
calc_type_nosize_idx(uint32_t idx)
{
	/** TODO: This could probably be done better with a FOREACH macro. */
	if (idx <= SUBIDX_UINTEGER8)
		return SUBIDX_UINTEGER;
	else if (idx <= SUBIDX_INTEGER8)
		return SUBIDX_INTEGER;
	else if (idx <= SUBIDX_REAL8)
		return SUBIDX_DOUBLE;
	return SUBIDX_STR;
}

/**
 * calculates the index of a type: this is used to point to the right function in the get data func
 * ptrs defined above.
 */
static uint32_t
calc_type_idx(char *type, size_t type_len)
{
	/** TODO: This could probably be done better with a FOREACH macro. */
	if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER1", type_len))
		return SUBIDX_UINTEGER1;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER2", type_len))
		return SUBIDX_UINTEGER2;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER4", type_len))
		return SUBIDX_UINTEGER4;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_UINTEGER8", type_len))
		return SUBIDX_UINTEGER8;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER1", type_len))
		return SUBIDX_INTEGER1;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER2", type_len))
		return SUBIDX_INTEGER2;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER4", type_len))
		return SUBIDX_INTEGER4;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER8", type_len))
		return SUBIDX_INTEGER8;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL4", type_len))
		return SUBIDX_REAL4;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL8", type_len))
		return SUBIDX_REAL8;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_BINARY", type_len))
		return SUBIDX_BINARY;
	else if (!strncmp(type, "DAOS_FILTER_TYPE_STRING", type_len))
		return SUBIDX_STRING;
	else /* DAOS_FILTER_TYPE_CSTRING */
		return SUBIDX_CSTRING;
}

/**
 * calculates the index of function class: this is used to point to the right function in the filter
 * func ptrs defined above. The space between function classes is there for the different types.
 * For example, there is 4 EQ functions (unsigned int, signed int, doubles, and strings).
 */
static uint32_t
calc_filterfunc_idx(daos_filter_part_t **parts, uint32_t idx)
{
	char  *part_type;
	size_t part_type_s;

	part_type   = (char *)parts[idx]->part_type.iov_buf;
	part_type_s = parts[idx]->part_type.iov_len;

	/** TODO: This could probably be done better with a FOREACH macro. */
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s))
		return SUBIDX_FUNC_EQ;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s))
		return SUBIDX_FUNC_NE;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s))
		return SUBIDX_FUNC_LT;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s))
		return SUBIDX_FUNC_LE;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s))
		return SUBIDX_FUNC_GE;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s))
		return SUBIDX_FUNC_GT;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s))
		return SUBIDX_FUNC_ADD;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s))
		return SUBIDX_FUNC_SUB;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s))
		return SUBIDX_FUNC_MUL;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s))
		return SUBIDX_FUNC_DIV;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
		return SUBIDX_FUNC_SUM;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
		return SUBIDX_FUNC_MAX;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
		return SUBIDX_FUNC_MIN;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_BITAND", part_type_s))
		return SUBIDX_FUNC_BITAND;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s))
		return SUBIDX_FUNC_LIKE;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s))
		return SUBIDX_FUNC_ISNULL;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s))
		return SUBIDX_FUNC_ISNOTNULL;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s))
		return SUBIDX_FUNC_NOT;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s))
		return SUBIDX_FUNC_AND;
	else /* if (!strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)) */
		return SUBIDX_FUNC_OR;
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

	if (func_idx < SUBIDX_FUNCS_WITH_ONE_TYPE_ONLY)
		func_idx += type_idx;

	comp_part->filter_func     = filter_func_ptrs[func_idx];
	comp_part->idx_end_subtree = *comp_part_idx;
exit:
	return rc;
}

static int
compile_filters(daos_filter_t **ftrs, uint32_t nftrs, struct filter_compiled_t *c_ftrs)
{
	uint32_t             part_idx;
	uint32_t             comp_part_idx;
	uint32_t             comp_num_parts;
	uint32_t             i               = 0;
	uint32_t             j, k;
	char                *type;
	size_t               type_len;
	int                  rc;
	daos_filter_part_t  *part;

	for (; i < nftrs; i++) {
		comp_num_parts = ftrs[i]->num_parts;

		for (j = 0; j < ftrs[i]->num_parts; j++) {
			part = ftrs[i]->parts[j];
			if (!strncmp((char *)part->part_type.iov_buf, "DAOS_FILTER_CONST",
				     part->part_type.iov_len))
				comp_num_parts += part->num_constants - 1;
		}

		D_ALLOC_ARRAY(c_ftrs[i].parts, comp_num_parts);
		if (c_ftrs[i].parts == NULL)
			D_GOTO(error, rc = -DER_NOMEM);

		c_ftrs[i].num_parts = comp_num_parts;
		part_idx            = 0;
		comp_part_idx       = 0;
		type                = NULL;
		type_len            = 0;
		rc = compile_filter(ftrs[i], &c_ftrs[i], &part_idx, &comp_part_idx, &type,
				    &type_len);
		if (rc != 0)
			D_GOTO(error, rc);
	}
	return 0;
error:
	for (k = 0; k <= i; k++) {
		if (c_ftrs[k].parts != NULL)
			D_FREE(c_ftrs[k].parts);
	}
	return rc;
}

int
pipeline_compile(daos_pipeline_t *pipe, struct pipeline_compiled_t *comp_pipe)
{
	int rc;

	comp_pipe->num_filters      = 0;
	comp_pipe->filters          = NULL;
	comp_pipe->num_aggr_filters = 0;
	comp_pipe->aggr_filters     = NULL;

	if (pipe->num_filters > 0) {
		D_ALLOC_ARRAY(comp_pipe->filters, pipe->num_filters);
		if (comp_pipe->filters == NULL)
			D_GOTO(error, rc = -DER_NOMEM);

		comp_pipe->num_filters = pipe->num_filters;
		rc = compile_filters(pipe->filters, pipe->num_filters, comp_pipe->filters);
		if (rc != 0)
			D_GOTO(error, rc);
	}
	if (pipe->num_aggr_filters > 0) {
		D_ALLOC_ARRAY(comp_pipe->aggr_filters, pipe->num_aggr_filters);
		if (comp_pipe->aggr_filters == NULL)
			D_GOTO(error, rc = -DER_NOMEM);

		comp_pipe->num_aggr_filters = pipe->num_aggr_filters;
		rc = compile_filters(pipe->aggr_filters, pipe->num_aggr_filters,
				     comp_pipe->aggr_filters);
		if (rc != 0)
			D_GOTO(error, rc);
	}
	return 0;
error:
	if (comp_pipe->filters != NULL)
		D_FREE(comp_pipe->filters);
	if (comp_pipe->aggr_filters != NULL)
		D_FREE(comp_pipe->aggr_filters);
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
