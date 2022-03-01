/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <math.h>
#include <string.h>
#include "pipeline_internal.h"
#include <daos/common.h>

#define NTYPES 9
#define TOTALFUNCS 25

static filter_func_t *
filter_func_ptrs[TOTALFUNCS] = { filter_func_eq,
				 filter_func_eq_st,
				 filter_func_ne,
				 filter_func_ne_st,
				 filter_func_lt,
				 filter_func_lt_st,
				 filter_func_le,
				 filter_func_le_st,
				 filter_func_ge,
				 filter_func_ge_st,
				 filter_func_gt,
				 filter_func_gt_st,
				 filter_func_add,
				 filter_func_sub,
				 filter_func_mul,
				 filter_func_div,
				 aggr_func_sum,
				 aggr_func_max,
				 aggr_func_min,
				 filter_func_like,
				 filter_func_isnull,
				 filter_func_isnotnull,
				 filter_func_not,
				 filter_func_and,
				 filter_func_or };

static filter_func_t *
getd_func_ptrs[TOTALFUNCS] = { getdata_func_dkey_i1,
			       getdata_func_dkey_i2,
			       getdata_func_dkey_i4,
			       getdata_func_dkey_i8,
			       getdata_func_dkey_r4,
			       getdata_func_dkey_r8,
			       getdata_func_dkey_raw,
			       getdata_func_dkey_st,
			       getdata_func_dkey_cst,
			       getdata_func_akey_i1,
			       getdata_func_akey_i2,
			       getdata_func_akey_i4,
			       getdata_func_akey_i8,
			       getdata_func_akey_r4,
			       getdata_func_akey_r8,
			       getdata_func_akey_raw,
			       getdata_func_akey_st,
			       getdata_func_akey_cst,
			       getdata_func_const_i1,
			       getdata_func_const_i2,
			       getdata_func_const_i4,
			       getdata_func_const_i8,
			       getdata_func_const_r4,
			       getdata_func_const_r8,
			       getdata_func_const_raw };

void
pipeline_aggregations_init(daos_pipeline_t *pipeline, d_sg_list_t *sgl_agg)
{
	uint32_t		i;
	double			*buf;
	daos_filter_part_t	*part;
	char			*part_type;
	size_t			part_type_s;

	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part      = pipeline->aggr_filters[i]->parts[0];
		buf       = (double *) sgl_agg[i].sg_iovs->iov_buf;
		part_type   = (char *) part->part_type.iov_buf;
		part_type_s = part->part_type.iov_len;

		if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
		{
			*buf = -INFINITY;
		}
		else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
		{
			*buf = INFINITY;
		}
		else
		{
			*buf = 0;
		}
	}
}

static uint32_t
calc_type_idx(char *type, size_t type_len)
{
	if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER1", type_len))
	{
		return 0;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER2", type_len))
	{
		return 1;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER4", type_len))
	{
		return 2;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_INTEGER8", type_len))
	{
		return 3;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL4", type_len))
	{
		return 4;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_REAL8", type_len))
	{
		return 5;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_BINARY", type_len))
	{
		return 6;
	}
	else if (!strncmp(type, "DAOS_FILTER_TYPE_STRING", type_len))
	{
		return 7;
	}
	else /* DAOS_FILTER_TYPE_CSTRING */
	{
		return 8;
	}
}

static uint32_t
calc_filterfunc_idx(daos_filter_part_t **parts, uint32_t idx)
{
	char		*part_type;
	size_t		part_type_s;

	part_type   = (char *) parts[idx]->part_type.iov_buf;
	part_type_s = parts[idx]->part_type.iov_len;

	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s))
	{
		return 0;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s))
	{
		return 2;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s))
	{
		return 4;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s))
	{
		return 6;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s))
	{
		return 8;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s))
	{
		return 10;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s))
	{
		return 12;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s))
	{
		return 13;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s))
	{
		return 14;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s))
	{
		return 15;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
	{
		return 16;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
	{
		return 17;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
	{
		return 18;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s))
	{
		return 19;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s))
	{
		return 20;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s))
	{
		return 21;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s))
	{
		return 22;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s))
	{
		return 23;
	}
	else /* if (!strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)) */
	{
		return 24;
	}
}

static uint32_t
calc_num_operands(daos_filter_part_t **parts, uint32_t idx)
{
	daos_filter_part_t	*child_part;
	char			*part_type;
	size_t			part_type_s;
	uint32_t		nops = 0;

	part_type   = (char *) parts[idx]->part_type.iov_buf;
	part_type_s = parts[idx]->part_type.iov_len;

	if (!strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s))
	{
		nops = 2;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s))
	{
		nops = 2;
		child_part = parts[idx + 2];

		if (!strncmp((char *) child_part->part_type.iov_buf,
			     "DAOS_FILTER_CONST",
			     child_part->part_type.iov_len))
		{
			nops += child_part->num_constants - 1;
		}
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s)    ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
	{
		nops = 1;
	}

	return nops;
}

static int
compile_filter(daos_filter_t *filter, struct filter_compiled_t *comp_filter,
	       uint32_t *part_idx, uint32_t *comp_part_idx, char **type,
	       size_t *type_len)
{
	uint32_t			nops;
	uint32_t			func_idx;
	uint32_t			i;
	size_t				j;
	char				*part_type;
	size_t				part_type_s;
	size_t				comp_size;
	struct filter_part_compiled_t	*comp_part;
	int				rc;
	uint32_t			idx;

	part_type   = (char *) filter->parts[*part_idx]->part_type.iov_buf;
	part_type_s = filter->parts[*part_idx]->part_type.iov_len;

	comp_part   = &comp_filter->parts[*comp_part_idx];
	*comp_part  = (struct filter_part_compiled_t) { 0 };

	if (part_type_s < strlen("DAOS_FILTER_FUNC"))
	{
		comp_size = part_type_s;
	}
	else
	{
		comp_size = strlen("DAOS_FILTER_FUNC");
	}

	if (strncmp(part_type, "DAOS_FILTER_FUNC", comp_size)) /** != FUNC */
	{
		*type = (char *) filter->parts[*part_idx]->data_type.iov_buf;
		*type_len = filter->parts[*part_idx]->data_type.iov_len;
		func_idx = calc_type_idx(*type, *type_len);

		if (!strncmp(part_type, "DAOS_FILTER_AKEY", part_type_s))
		{
			func_idx += NTYPES;
			comp_part->data_offset =
					filter->parts[*part_idx]->data_offset;
			comp_part->data_len =
					filter->parts[*part_idx]->data_len;

			comp_part->iov = &filter->parts[*part_idx]->akey;
			comp_part->filter_func = getd_func_ptrs[func_idx];
		}
		else if (!strncmp(part_type, "DAOS_FILTER_CONST", part_type_s))
		{
			if (func_idx > 6)
			{
				D_GOTO(exit, rc = -DER_INVAL);
			}
			func_idx += NTYPES*2;
			comp_part->data_offset = 0;
			comp_part->iov = &filter->parts[*part_idx]->constant[0];
			comp_part->data_len = comp_part->iov->iov_len;
			comp_part->filter_func = getd_func_ptrs[func_idx];

			for (j = 1;
			     j < filter->parts[*part_idx]->num_constants;
			     j++)
			{
				*comp_part_idx += 1;
				comp_part = &comp_filter->parts[*comp_part_idx];
				*comp_part =
					(struct filter_part_compiled_t) { 0 };
				comp_part->data_offset = 0;
				comp_part->iov =
					&filter->parts[*part_idx]->constant[j];
				comp_part->data_len = comp_part->iov->iov_len;
				comp_part->filter_func = getd_func_ptrs[func_idx];
			}
		}
		else if (!strncmp(part_type, "DAOS_FILTER_DKEY", part_type_s))
		{
			comp_part->data_offset =
					filter->parts[*part_idx]->data_offset;
			comp_part->data_len =
					filter->parts[*part_idx]->data_len;
			comp_part->filter_func = getd_func_ptrs[func_idx];;
		}
		D_GOTO(exit, rc = 0);
	}

	nops = calc_num_operands(filter->parts, *part_idx);
	comp_part->num_operands = nops;

	/** recursive calls for function parameters */
	idx	= *part_idx;
	for (i = 0; i < filter->parts[idx]->num_operands; i++)
	{
		*comp_part_idx	+= 1;
		*part_idx	+= 1;
		rc = compile_filter(filter, comp_filter, part_idx,
				    comp_part_idx, type, type_len);
		if (rc != 0)
		{
			D_GOTO(exit, rc);
		}
	}

	func_idx  = calc_filterfunc_idx(filter->parts, idx);
	if (func_idx < 12)
	{
		if (calc_type_idx(*type, *type_len) >= 6)
		{
			func_idx++;
		}
	}
	comp_part->filter_func = filter_func_ptrs[func_idx];

exit:
	return rc;
}

static int
compile_filters(daos_filter_t **ftrs, uint32_t nftrs,
		struct filter_compiled_t *c_ftrs)
{
	uint32_t		part_idx;
	uint32_t		comp_part_idx;
	uint32_t		comp_num_parts;
	uint32_t 		i, j;
	char			*type;
	size_t			type_len;
	int			rc = 0;
	daos_filter_part_t	*part;

	for (i = 0; i < nftrs; i++)
	{
		comp_num_parts = ftrs[i]->num_parts;

		for (j = 0; j < ftrs[i]->num_parts; j++)
		{
			part = ftrs[i]->parts[j];

			if (!strncmp((char *) part->part_type.iov_buf,
				     "DAOS_FILTER_CONST",
				     part->part_type.iov_len))
			{
				comp_num_parts += part->num_constants - 1;
			}
		}

		D_ALLOC_ARRAY(c_ftrs[i].parts, comp_num_parts);
		if (c_ftrs[i].parts == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		c_ftrs[i].num_parts = comp_num_parts;

		part_idx	= 0;
		comp_part_idx	= 0;
		type		= NULL;
		type_len	= 0;
		rc = compile_filter(ftrs[i], &c_ftrs[i], &part_idx,
				    &comp_part_idx, &type, &type_len);
		if (rc != 0)
		{
			D_GOTO(exit, rc);
		}
	}
exit:

	return rc;
}

int
pipeline_compile(daos_pipeline_t *pipe, struct pipeline_compiled_t *comp_pipe)
{
	int rc = 0;

	comp_pipe->num_filters		= 0;
	comp_pipe->filters		= NULL;
	comp_pipe->num_aggr_filters	= 0;
	comp_pipe->aggr_filters		= NULL;

	if (pipe->num_filters > 0)
	{
		D_ALLOC_ARRAY(comp_pipe->filters, pipe->num_filters);
		if (comp_pipe->filters == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		comp_pipe->num_filters = pipe->num_filters;

		rc = compile_filters(pipe->filters, pipe->num_filters,
				     comp_pipe->filters);
		if (rc != 0)
		{
			D_GOTO(exit, rc);
		}
	}
	if (pipe->num_aggr_filters > 0)
	{
		D_ALLOC_ARRAY(comp_pipe->aggr_filters, pipe->num_aggr_filters);
		if (comp_pipe->aggr_filters == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		comp_pipe->num_aggr_filters = pipe->num_aggr_filters;

		rc = compile_filters(pipe->aggr_filters, pipe->num_aggr_filters,
				     comp_pipe->aggr_filters);
		if (rc != 0)
		{
			D_GOTO(exit, rc);
		}
	}
exit:
	return rc;
}

void
pipeline_compile_free(struct pipeline_compiled_t *comp_pipe)
{
	uint32_t i;

	if (comp_pipe->num_filters > 0)
	{
		for (i = 0; i < comp_pipe->num_filters; i++)
		{
			if (comp_pipe->filters[i].num_parts > 0)
			{
				D_FREE(comp_pipe->filters[i].parts);
			}
		}
		D_FREE(comp_pipe->filters);
	}
	if (comp_pipe->num_aggr_filters > 0)
	{
		for (i = 0; i < comp_pipe->num_aggr_filters; i++)
		{
			if (comp_pipe->aggr_filters[i].num_parts > 0)
			{
				D_FREE(comp_pipe->aggr_filters[i].parts);
			}
		}
		D_FREE(comp_pipe->aggr_filters);
	}
}

