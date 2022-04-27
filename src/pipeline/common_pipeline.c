/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

static bool
pipeline_part_chk_type(const char *part_type, size_t part_type_s, bool is_aggr)
{
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_BITAND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_DKEY", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_AKEY", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_CONST", part_type_s))
		return true;
	if (is_aggr && (!strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
			!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s) ||
			!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s) ||
			!strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s)))
		return true;
	if (!is_aggr && (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s) ||
			 !strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s)))
		return true;
	return false;
}

static int
pipeline_part_nops(const char *part_type, size_t part_type_s)
{
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s))
		return -1;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_BITAND", part_type_s))
		return 2;
	else if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s) ||
		 !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s))
		return 1;
	return 0; /** Everything else has zero operands */
}

static bool
pipeline_part_checkop(const char *part_type, size_t part_type_s, const char *operand_type,
		      size_t operand_type_s)
{
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_NOT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)) { /* only logical funcs */
		return !strncmp(operand_type, "DAOS_FILTER_FUNC_EQ", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_IN", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_NE", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_LT", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_LE", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_GE", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_GT", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_LIKE", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_NOT", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_AND", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_OR", operand_type_s);
	} else if (!strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s) ||
		   !strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s) ||
		   !strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL",
			    part_type_s)) /* no functions */
		return strncmp(operand_type, "DAOS_FILTER_FUN", strlen("DAOS_FILTER_FUN"));
	else { /* arithmetic functions or keys and constants (no functions) */
		return strncmp(operand_type, "DAOS_FILTER_FUN", strlen("DAOS_FILTER_FUN")) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_BITAND", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_ADD", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_SUB", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_MUL", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_DIV", operand_type_s);
	}
}

static bool
pipeline_filter_checkops(daos_filter_t *ftr, size_t *p)
{
	uint32_t i;
	uint32_t num_operands;
	bool     res;
	char    *part_type;
	size_t   part_type_s;
	char    *child_part_type;
	size_t   child_part_type_s;

	num_operands = ftr->parts[*p]->num_operands;
	part_type    = (char *)ftr->parts[*p]->part_type.iov_buf;
	part_type_s  = ftr->parts[*p]->part_type.iov_len;
	for (i = 0; i < num_operands; i++) {
		child_part_type   = (char *)ftr->parts[*p + 1]->part_type.iov_buf;
		child_part_type_s = ftr->parts[*p + 1]->part_type.iov_len;
		res               = pipeline_part_checkop(part_type, part_type_s, child_part_type,
							  child_part_type_s);
		if (!res) {
			D_ERROR("part %zu: wrong child part type %.*s for part type %.*s\n", *p,
				(int)child_part_type_s, child_part_type, (int)part_type_s,
				part_type);
			return res;
		}

		/** recursive call */
		*p += 1;
		res = pipeline_filter_checkops(ftr, p);
		if (!res)
			return res;
	}
	return true;
}

int
d_pipeline_check(daos_pipeline_t *pipeline)
{
	size_t  i;
	int     rc;

	/**
	 * TODO: Check that functions' operands always have the right type
	 * TODO: Check that constants that are arrays are always on the right
	 * TODO: Check that arithmetic functions only support number types
	 * TODO: Check that isnull and isnotnull operands are always akeys
	 * TODO: Check that offsets and sizes are correct (i.e, offset <= size)
	 * TODO: Check that parts of type CTRING always have at least one '\0'
	 * TODO: Check that parts of type STRING have a sane size
	 */

	/** -- Check 0: Check that pipeline is not NULL. */

	if (pipeline == NULL) {
		D_ERROR("pipeline object is NULL\n");
		return -DER_INVAL;
	}

	/** -- Check 1: Check that filters are chained together correctly. */

	for (i = 0; i < pipeline->num_filters; i++) {
		if (strncmp((char *)pipeline->filters[i]->filter_type.iov_buf,
			    "DAOS_FILTER_CONDITION", pipeline->filters[i]->filter_type.iov_len)) {
			D_ERROR("filter %zu: filter type is not DAOS_FILTER_CONDITION\n", i);
			return -DER_INVAL;
		}
	}
	for (i = 0; i < pipeline->num_aggr_filters; i++) {
		if (strncmp((char *)pipeline->aggr_filters[i]->filter_type.iov_buf,
			    "DAOS_FILTER_AGGREGATION",
			    pipeline->aggr_filters[i]->filter_type.iov_len)) {
			D_ERROR("aggr_filter %zu: filter type is not DAOS_FILTER_AGGREGATION\n", i);
			return -DER_INVAL;
		}
	}

	/** -- Rest of the checks are done for each filter */

	for (i = 0; i < pipeline->num_filters + pipeline->num_aggr_filters; i++) {
		daos_filter_t *ftr;
		size_t         p;
		uint32_t       num_parts = 0;
		int            num_operands;
		bool           res;
		bool           is_aggr;

		if (i < pipeline->num_filters) {
			ftr     = pipeline->filters[i];
			is_aggr = false;
		} else {
			ftr     = pipeline->aggr_filters[i - pipeline->num_filters];
			is_aggr = true;
		}
		if (ftr->num_parts)
			num_parts = 1;

		/** -- Checks 2 and 3 */

		/**
		 * -- Check 2: Check that all parts have a correct type.
		 *
		 * -- Check 3: Check that all parts have a correct number of operands and also that
		 *             the number of total parts is correct.
		 */

		for (p = 0; p < ftr->num_parts; p++) {
			daos_filter_part_t *part = ftr->parts[p];

			/** 2 */

			res = pipeline_part_chk_type((char *)part->part_type.iov_buf,
						     part->part_type.iov_len, is_aggr);
			if (!res) {
				D_ERROR("filter %zu, part %zu: part type %.*s is not supported\n",
					i, p, (int)part->part_type.iov_len,
					(char *)part->part_type.iov_buf);
				return -DER_NOSYS;
			}

			/** 3 */

			num_operands = pipeline_part_nops((char *)part->part_type.iov_buf,
							  part->part_type.iov_len);

			if (num_operands < 0) { /** special cases for AND and OR */
				if (part->num_operands < 2)
					rc = -DER_INVAL;
			} else if (((uint32_t)num_operands) != part->num_operands) {
				rc = -DER_INVAL;
			}
			if (rc != 0) {
				D_ERROR("filter %zu, part %zu: part has an incorrect number of "
					"operands\n", i, p);
				return rc;
			}
			num_parts += part->num_operands;
		}
		/** 3 */

		if (num_parts != ftr->num_parts) {
			D_ERROR("filter %zu: mismatch between counted parts %u and .num_parts %u\n",
				i, num_parts, ftr->num_parts);
			return -DER_INVAL;
		}

		/**
		 * -- Check 4: Check that all parts have the right type of operands.
		 */

		p   = 0;
		res = pipeline_filter_checkops(ftr, &p);
		if (!res) {
			D_ERROR("filter %zu: wrong type for some part operands\n", i);
			return -DER_INVAL;
		}
	}

	return 0;
}

void
pipeline_aggregations_fixavgs(daos_pipeline_t *pipeline, double total, d_sg_list_t *sgl_agg)
{
	uint32_t i;
	double  *buf;
	char    *part_type;
	size_t   part_type_s;

	D_ASSERT(total > 0.0);

	for (i = 0; i < pipeline->num_aggr_filters; i++) {
		part_type   = (char *)pipeline->aggr_filters[i]->parts[0]->part_type.iov_buf;
		part_type_s = pipeline->aggr_filters[i]->parts[0]->part_type.iov_len;
		if (!strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s)) {
			buf  = (double *)sgl_agg->sg_iovs[i].iov_buf;
			*buf = *buf / total;
		}
	}
}
