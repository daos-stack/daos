/**
 * (C) Copyright 2016-2023 Intel Corporation.
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

static bool
pipeline_part_chk_data_type(const char *data_type, size_t data_type_s)
{
	if (!data_type_s)
		return true; /** some parts do not need to declare type */

	if (data_type_s < strlen("DAOS_FILTER_TYPE") ||
	    strncmp(data_type, "DAOS_FILTER_TYPE", strlen("DAOS_FILTER_TYPE")))
		return false; /** not a type */

	if (!strncmp(data_type, "DAOS_FILTER_TYPE_BINARY", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_STRING", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_CSTRING", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_UINTEGER1", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_UINTEGER2", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_UINTEGER4", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_UINTEGER8", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER1", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER2", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER4", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_INTEGER8", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_REAL4", data_type_s) ||
	    !strncmp(data_type, "DAOS_FILTER_TYPE_REAL8", data_type_s))
		return true;

	return false; /** type not recognized */
}

static int
pipeline_part_nops(const char *part_type, size_t part_type_s)
{
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_AND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s))
		return -1;
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
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
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s) ||
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
	    !strncmp(part_type, "DAOS_FILTER_FUNC_OR", part_type_s)) {
		/* only logical funcs */
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
	}
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s)) {
		/* no functions */
		return strncmp(operand_type, "DAOS_FILTER_FUN", strlen("DAOS_FILTER_FUN"));
	}
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_ISNULL", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_ISNOTNULL", part_type_s)) {
		/* only akeys */
		return !strncmp(operand_type, "DAOS_FILTER_AKEY", operand_type_s);
	}
	if (!strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_BITAND", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_SUM", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s) ||
	    !strncmp(part_type, "DAOS_FILTER_FUNC_AVG", part_type_s)) {
		/* arithmetic functions or keys and constants */
		return strncmp(operand_type, "DAOS_FILTER_FUN", strlen("DAOS_FILTER_FUN")) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_BITAND", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_ADD", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_SUB", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_MUL", operand_type_s) ||
		       !strncmp(operand_type, "DAOS_FILTER_FUNC_DIV", operand_type_s);
	}
	return false;
}

static bool
is_comp_logical_func(const char *part_type, size_t part_type_s)
{
	return !strncmp(part_type, "DAOS_FILTER_FUNC_EQ", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_IN", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_NE", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_LT", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_LE", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_GE", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_GT", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s);
}

static bool
is_arith_func(const char *part_type, size_t part_type_s)
{
	return !strncmp(part_type, "DAOS_FILTER_FUNC_BITAND", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_ADD", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_SUB", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_MUL", part_type_s) ||
	       !strncmp(part_type, "DAOS_FILTER_FUNC_DIV", part_type_s);
}

static bool
type_is_uint(const char *type, size_t type_s)
{
	size_t len = strlen("DAOS_FILTER_TYPE_UINTEGER");

	if (type_s != len + 1)
		return false;
	return !strncmp(type, "DAOS_FILTER_TYPE_UINTEGER", len);
}

static bool
type_is_int(const char *type, size_t type_s)
{
	size_t len = strlen("DAOS_FILTER_TYPE_INTEGER");

	if (type_s != len + 1)
		return false;
	return !strncmp(type, "DAOS_FILTER_TYPE_INTEGER", len);
}

static bool
type_is_real(const char *type, size_t type_s)
{
	size_t len = strlen("DAOS_FILTER_TYPE_REAL");

	if (type_s != len + 1)
		return false;
	return !strncmp(type, "DAOS_FILTER_TYPE_REAL", len);
}

static bool
type_is_string(const char *type, size_t type_s)
{
	return !strncmp(type, "DAOS_FILTER_TYPE_BINARY", type_s) ||
	       !strncmp(type, "DAOS_FILTER_TYPE_STRING", type_s) ||
	       !strncmp(type, "DAOS_FILTER_TYPE_CSTRING", type_s);
}

static bool
type_is_numeric(const char *data_type, size_t data_type_s)
{
	if (!data_type_s)
		return false;
	return !type_is_string(data_type, data_type_s);
}

static bool
types_are_the_same(const char *type1, size_t type1_s, const char *type2, size_t type2_s)
{
	if (type_is_uint(type1, type1_s) && type_is_uint(type2, type2_s))
		return true;
	if (type_is_int(type1, type1_s) && type_is_int(type2, type2_s))
		return true;
	if (type_is_real(type1, type1_s) && type_is_real(type2, type2_s))
		return true;
	if (type_is_string(type1, type1_s) && type_is_string(type2, type2_s))
		return true;
	return false;
}

static bool
pipeline_filter_checkops(daos_filter_t *ftr, size_t *p, char **data_type, size_t *data_type_s)
{
	uint32_t  i;
	uint32_t  num_operands;
	size_t    child_num_constants;
	bool      res;
	char     *part_type;
	size_t    part_type_s;
	char     *part_data_type;
	size_t    part_data_type_s;
	char     *child_part_type;
	size_t    child_part_type_s;
	char     *child_data_type;
	size_t    child_data_type_s;

	num_operands     = ftr->parts[*p]->num_operands;
	part_type        = (char *)ftr->parts[*p]->part_type.iov_buf;
	part_type_s      = ftr->parts[*p]->part_type.iov_len;
	part_data_type   = (char *)ftr->parts[*p]->data_type.iov_buf;
	part_data_type_s = ftr->parts[*p]->data_type.iov_len;

	*data_type       = part_data_type;
	*data_type_s     = part_data_type_s;

	for (i = 0; i < num_operands; i++) {
		child_part_type     = (char *)ftr->parts[*p + 1]->part_type.iov_buf;
		child_part_type_s   = ftr->parts[*p + 1]->part_type.iov_len;
		child_data_type     = (char *)ftr->parts[*p + 1]->data_type.iov_buf;
		child_data_type_s   = ftr->parts[*p + 1]->data_type.iov_len;
		child_num_constants = ftr->parts[*p + 1]->num_constants;

		res                 = pipeline_part_checkop(part_type, part_type_s, child_part_type,
							    child_part_type_s);
		if (!res) {
			D_ERROR("part %zu: wrong part type %.*s operand for part type %.*s\n", *p,
				(int)child_part_type_s, child_part_type, (int)part_type_s,
				part_type);
			return res;
		}

		if (!strncmp(child_part_type, "DAOS_FILTER_CONST", child_part_type_s) &&
		    child_num_constants > 1 &&
		    (!is_comp_logical_func(part_type, part_type_s) ||
		     !strncmp(part_type, "DAOS_FILTER_FUNC_LIKE", part_type_s))) {
			D_ERROR("part %zu: CONST array can't be operand of part type %.*s\n", *p,
				(int)part_type_s, part_type);
			return false;
		}

		if (is_arith_func(part_type, part_type_s) &&
		    strncmp(child_part_type, "DAOS_FILTER_FUN", strlen("DAOS_FILTER_FUN")) &&
		    !type_is_numeric(child_data_type, child_data_type_s)) {
			/**
			 * we can use child_data_type here because we make sure the child is not a
			 * function
			 */
			D_ERROR("part %zu: wrong data type %.*s operand for part type %.*s\n",
				*p, (int)child_data_type_s, child_data_type, (int)part_type_s,
				part_type);
			return false;
		}

		/** recursive call */
		*p                += 1;
		child_data_type    = NULL;
		child_data_type_s  = 0;
		res = pipeline_filter_checkops(ftr, p, &child_data_type, &child_data_type_s);
		if (!res)
			return res;

		if (is_comp_logical_func(part_type, part_type_s) ||
		    is_arith_func(part_type, part_type_s)) {
			if (!child_data_type_s) {
				D_ERROR("part %zu: no data type for operand of part type %.*s\n",
					*p, (int)part_type_s, part_type);
				return false;
			}
			if (!*data_type_s) {
				/* simply adopting child type for now */
				*data_type   = child_data_type;
				*data_type_s = child_data_type_s;
			} else {
				/* types must be the same */
				if (!types_are_the_same(*data_type, *data_type_s,
							child_data_type, child_data_type_s)) {
					D_ERROR("part %zu: data type mismatch (%.*s vs %.*s) for "
						"operand of part type %.*s\n", *p,
						(int)child_data_type_s, child_data_type,
						(int)*data_type_s, *data_type,
						(int)part_type_s, part_type);
					return false;
				}
			}
		}
	}
	if (!strncmp(part_type, "DAOS_FILTER_FUN", strlen("DAOS_FILTER_FUN")) &&
	    !is_arith_func(part_type, part_type_s)) {
		/**
		 * If part is not an arithmetic function, then there is no need to return the data
		 * type (the type is either boolean or it is an aggregation function which is at the
		 * top of the tree).
		 */
		*data_type   = NULL;
		*data_type_s = 0;
	}
	return true;
}

static bool
pipeline_filter_check_array_constants(daos_filter_t *ftr, size_t *p)
{
	uint32_t  i;
	uint32_t  num_operands;
	size_t    child_num_constants;
	bool      res;
	char     *child_part_type;
	size_t    child_part_type_s;

	num_operands = ftr->parts[*p]->num_operands;
	for (i = 0; i < num_operands; i++) {
		child_part_type     = (char *)ftr->parts[*p + 1]->part_type.iov_buf;
		child_part_type_s   = ftr->parts[*p + 1]->part_type.iov_len;
		child_num_constants = ftr->parts[*p + 1]->num_constants;

		if (!strncmp(child_part_type, "DAOS_FILTER_CONST", child_part_type_s) &&
		    child_num_constants > 1 && i < num_operands - 1) {
			D_ERROR("part %zu: CONST array should always be the last operand\n", *p);
			return false;
		}

		/** recursive call */
		*p += 1;
		res = pipeline_filter_check_array_constants(ftr, p);
		if (!res)
			return res;
	}
	return true;
}

static int
do_checks_for_string_constants(size_t ft, size_t pa, daos_filter_part_t *part)
{
	size_t   k;
	char    *string;

	if (part->data_type.iov_len == strlen("DAOS_FILTER_TYPE_CSTRING") &&
	    !strncmp((char *)part->data_type.iov_buf, "DAOS_FILTER_TYPE_CSTRING",
		     strlen("DAOS_FILTER_TYPE_CSTRING"))) {
		/** constants are CSTRING */
		size_t  c;
		bool    eos_found;

		for (k = 0; k < part->num_constants; k++) {
			eos_found = false;
			string    = (char *)part->constant[k].iov_buf;
			for (c = 0; c < part->constant[k].iov_len; c++) {
				if (string[c] == '\0') {
					eos_found = true;
					break;
				}
			}
			if (!eos_found) {
				D_ERROR("filter %zu, part %zu, const %zu: CSTRING constant does "
					"not terminate in \\0\n", ft, pa, k);
				return -DER_INVAL;
			}
		}
	} else if (part->data_type.iov_len == strlen("DAOS_FILTER_TYPE_STRING") &&
		   !strncmp((char *)part->data_type.iov_buf, "DAOS_FILTER_TYPE_STRING",
			    strlen("DAOS_FILTER_TYPE_STRING"))) {
		/** constants are STRING */
		size_t  *string_size;

		for (k = 0; k < part->num_constants; k++) {
			string_size = (size_t *)part->constant[k].iov_buf;
			if (*string_size > part->constant[k].iov_len - sizeof(size_t)) {
				D_ERROR("filter %zu, part %zu, const %zu: size of STRING constant "
					"%zu is larger than (.iov_len - %zu) %zu\n", ft,
					pa, k, *string_size, sizeof(size_t),
					part->constant[k].iov_len - sizeof(size_t));
				return -DER_INVAL;
			}
		}
	}
	return 0;
}

int
d_pipeline_check(daos_pipeline_t *pipeline)
{
	size_t  i;
	int     rc = 0;

	/**
	 * TOTAL: 9 checks:
	 *
	 *      -- Check 0: Check that pipeline is not NULL.
	 *      -- Check 1: Check that filters are chained together correctly.
	 *      -- Check 2: Check that all parts have a correct type.
	 *      -- Check 3: Check that all parts have a correct number of operands and also that
	 *                  the number of total parts is correct.
	 *      -- Check 4: Check that parts that are not functions have a data type.
	 *      -- Check 5: Check that constants of type CSTRING always end in '\0'.
	 *      -- Check 6: Check that constants of type STRING have a sane size.
	 *      -- Check 7: Check that all parts have a correct data type.
	 *      -- Check 8: Check that all parts have the right type of operands.
	 *      -- Check 9: Check that arrays of constants are always on the right operand.
	 */

	/** 0 */

	if (pipeline == NULL) {
		D_ERROR("pipeline object is NULL\n");
		return -DER_INVAL;
	}

	/** 1 */

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
		daos_filter_t  *ftr;
		size_t          p;
		uint32_t        num_parts     = 0;
		int             num_operands;
		bool            res;
		bool            is_aggr;
		char           *data_type;
		size_t          data_type_s;

		if (i < pipeline->num_filters) {
			ftr     = pipeline->filters[i];
			is_aggr = false;
		} else {
			ftr     = pipeline->aggr_filters[i - pipeline->num_filters];
			is_aggr = true;
		}
		if (ftr->num_parts)
			num_parts = 1;

		/** -- Checks 2 ... 7 */

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

			/** 4 */

			if (strncmp((char *)part->part_type.iov_buf, "DAOS_FILTER_FUN",
				    strlen("DAOS_FILTER_FUN")) &&
			    !part->data_type.iov_len) {
				D_ERROR("filter %zu, part %zu: no data type defined\n", i, p);
				return -DER_INVAL;
			}

			if (part->part_type.iov_len == strlen("DAOS_FILTER_CONST") &&
			    !strncmp((char *)part->part_type.iov_buf, "DAOS_FILTER_CONST",
				    strlen("DAOS_FILTER_CONST"))) {

				/** 5 and 6 */

				rc = do_checks_for_string_constants(i, p, part);
				if (rc != 0)
					return rc;
			}

			/** 7 */

			res = pipeline_part_chk_data_type((char *)part->data_type.iov_buf,
							  part->data_type.iov_len);
			if (!res) {
				D_ERROR("filter %zu, part %zu: data type %.*s is not supported\n",
					i, p, (int)part->data_type.iov_len,
					(char *)part->data_type.iov_buf);
				return -DER_NOSYS;
			}
		}
		/** 3 (continued) */

		if (num_parts != ftr->num_parts) {
			D_ERROR("filter %zu: mismatch between counted parts %u and .num_parts %u\n",
				i, num_parts, ftr->num_parts);
			return -DER_INVAL;
		}

		/** 8 */

		p            = 0;
		data_type    = NULL;
		data_type_s  = 0;
		if (ftr->num_parts > 0) {
			res = pipeline_filter_checkops(ftr, &p, &data_type, &data_type_s);
			if (!res) {
				D_ERROR("filter %zu: wrong type for some part operands\n", i);
				return -DER_INVAL;
			}
		}

		/** 9 */

		p = 0;
		if (ftr->num_parts > 0) {
			res = pipeline_filter_check_array_constants(ftr, &p);
			if (!res) {
				D_ERROR("filter %zu: array of constants placed in wrong operand\n",
					i);
				return -DER_INVAL;
			}
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
