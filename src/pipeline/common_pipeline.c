/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include "pipeline_internal.h"


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

		if (i < pipeline->num_filters)
		{
			ftr = pipeline->filters[i];
		}
		else
		{
			ftr = pipeline->aggr_filters[i-pipeline->num_filters];
		}
		if (ftr->num_parts)
		{
			num_parts = 1;
		}

		/**
		 * -- Check 2: Check that all parts have a correct
		 *             number of operands and also that the
		 *             number of total parts is correct.
		 */

		for (p = 0; p < ftr->num_parts; p++)
		{
			daos_filter_part_t *part = ftr->parts[p];
			num_operands = pipeline_part_nops((char *) part->part_type.iov_buf,
							  part->part_type.iov_len);

			if (num_operands != part->num_operands)
			{
				return -DER_INVAL;
			}
			num_parts += part->num_operands;
		}
		if (num_parts != ftr->num_parts)
		{
			return -DER_INVAL;
		}

		/**
		 * -- Check 3: Check that all parts have the right
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
