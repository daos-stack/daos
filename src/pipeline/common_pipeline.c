/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"


static bool
pipeline_part_chk_type(const char *part_type, size_t part_type_s, bool is_aggr)
{
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_DKEY", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_AKEY", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_CONST", part_type_s))
	{
		return true;
	}
	if (is_aggr == true &&
		(!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s)))
	{
		return true;
	}
	if (is_aggr == false &&
		(!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s)))
	{
		return true;
	}
	return false;
}

static uint32_t
pipeline_part_nops(const char *part_type, size_t part_type_s)
{
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)  ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s))
	{
		return 2;
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s)      ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s)    ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s)       ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
	{
		return 1;
	}
	return 0; /** Everything else has zero operands */
}

static bool
pipeline_part_checkop(const char *part_type, size_t part_type_s,
		      const char *operand_type, size_t operand_type_s)
{
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s))
	{
		return !strncmp(operand_type, "DAOS_FILTER_FUNC",
				strlen("DAOS_FILTER_FUNC"));
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s)  ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s))
	{
		return strncmp(operand_type, "DAOS_FILTER_FUNC",
				strlen("DAOS_FILTER_FUNC"));
	}
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s))
	{
		return strncmp(operand_type, "DAOS_FILTER_FUNC",
				strlen("DAOS_FILTER_FUNC"))           ||
			!strncmp(operand_type, "DAOS_FILTER_FUNC_ADD", operand_type_s) ||
			!strncmp(operand_type, "DAOS_FILTER_FUNC_SUB", operand_type_s) ||
			!strncmp(operand_type, "DAOS_FILTER_FUNC_MUL", operand_type_s) ||
			!strncmp(operand_type, "DAOS_FILTER_FUNC_DIV", operand_type_s);
	}
	return false; /** we should not reach this ever */
}

static bool
pipeline_filter_checkops(daos_filter_t *ftr, size_t *p)
{
	uint32_t	i;
	uint32_t	num_operands;
	bool		res;
	char		*part_type;
	size_t		part_type_s;
	char		*child_part_type;
	size_t		child_part_type_s;

	num_operands 	= ftr->parts[*p]->num_operands;
	part_type	= (char *) ftr->parts[*p]->part_type.iov_buf;
	part_type_s	= ftr->parts[*p]->part_type.iov_len;
	for (i = 0; i < num_operands; i++)
	{
		*p		+= 1;
		child_part_type	= (char *) ftr->parts[*p]->part_type.iov_buf;
		child_part_type_s = ftr->parts[*p]->part_type.iov_len;
		res = pipeline_part_checkop(part_type, part_type_s,
					    child_part_type, child_part_type_s);
		if (res == false)
		{
			return res;
		}
		res = pipeline_filter_checkops(ftr, p);
		if (res == false)
		{
			return res;
		}
	}
	return true;
}

int d_pipeline_check(daos_pipeline_t *pipeline)
{
	size_t i;

	// TODO: Check that only function 'IN' has array of constants
	// TODO: Check that functions' operands always have the same type
	// TODO: Check that functions 'like', 'null', and 'notnull' don't have
	//       functions as parameters
	// TODO: Check that arithmetic functions only support number types
	// TODO: Check that constants have offset set to zero

	/** -- Check 0: Check that pipeline is not NULL. */

	if (pipeline == NULL)
	{
		return -DER_INVAL;
	}

	/** -- Check 1: Check that filters are chained together correctly. */

	{
		for (i = 0; i < pipeline->num_filters; i++)
		{
			if (strncmp((char *) pipeline->filters[i]->filter_type.iov_buf,
				    "DAOS_FILTER_CONDITION",
				    pipeline->filters[i]->filter_type.iov_len))
			{
				return -DER_INVAL;
			}
		}
		for (i = 0; i < pipeline->num_aggr_filters; i++)
		{
			if (strncmp((char *) pipeline->aggr_filters[i]->filter_type.iov_buf,
				    "DAOS_FILTER_AGGREGATION",
				    pipeline->aggr_filters[i]->filter_type.iov_len))
			{
				return -DER_INVAL;
			}
		}
	}

	/** -- Rest of the checks are done for each filter */

	for (i = 0;
	     i < pipeline->num_filters + pipeline->num_aggr_filters;
	     i++)
	{
		daos_filter_t *ftr;
		size_t p;
		uint32_t num_parts = 0;
		uint32_t num_operands;
		bool res;
		bool is_aggr;

		if (i < pipeline->num_filters)
		{
			ftr = pipeline->filters[i];
			is_aggr = false;
		}
		else
		{
			ftr = pipeline->aggr_filters[i-pipeline->num_filters];
			is_aggr = true;
		}
		if (ftr->num_parts)
		{
			num_parts = 1;
		}

		/** -- Checks 2 and 3 */

		/**
		 * -- Check 2: Check that all parts have a correct
		 *             type.
		 *
		 * -- Check 3: Check that all parts have a correct
		 *             number of operands and also that the
		 *             number of total parts is correct.
		 */

		for (p = 0; p < ftr->num_parts; p++)
		{
			daos_filter_part_t *part = ftr->parts[p];

			/** 2 */
			res = pipeline_part_chk_type((char *) part->part_type.iov_buf,
						     part->part_type.iov_len,
						     is_aggr);
			if (res == false)
			{
				return -DER_NOSYS;
			}

			/** 3 */
			num_operands = pipeline_part_nops((char *) part->part_type.iov_buf,
							  part->part_type.iov_len);

			if (num_operands != part->num_operands)
			{
				return -DER_INVAL;
			}
			num_parts += part->num_operands;
		}
		/** 3 */
		if (num_parts != ftr->num_parts)
		{
			return -DER_INVAL;
		}

		/**
		 * -- Check 4: Check that all parts have the right
		 *             type of operands.
		 */

		p = 0;
		res = pipeline_filter_checkops(ftr, &p);
		if (res == false)
		{
			return -DER_INVAL;
		}
	}

	return 0;
}

void
pipeline_aggregations_fixavgs(daos_pipeline_t *pipeline, double total,
			      d_sg_list_t *sgl_agg)
{
	uint32_t		i;
	double			*buf;
	char			*part_type;
	size_t			part_type_s;

	D_ASSERT(total > 0.0);

	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part_type = (char *) pipeline->aggr_filters[i]->parts[0]->part_type.iov_buf;
		part_type_s = pipeline->aggr_filters[i]->parts[0]->part_type.iov_len;
		if (!strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
		{
			buf = (double *) sgl_agg[i].sg_iovs->iov_buf;
			*buf = *buf / total;
		}
	}
}
