
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

static int filter_func_getdata(struct filter_part_run_t *args, double *data)
{
	int     rc;
	d_iov_t *iov;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	iov    = args->iov_out;
	if (iov == NULL)
	{
		return 1;
	}
	*data = args->value_out;

	return 0;
}

static bool logfunc_eq(double left, double right)
{
	return left == right;
}

static bool logfunc_ne(double left, double right)
{
	return left != right;
}

static bool logfunc_lt(double left, double right)
{
	return left < right;
}

static bool logfunc_le(double left, double right)
{
	return left <= right;
}

static bool logfunc_ge(double left, double right)
{
	return left >= right;
}

static bool logfunc_gt(double left, double right)
{
	return left > right;
}

#define filter_func_log(op)\
int filter_func_##op(struct filter_part_run_t *args)\
{\
	double left;\
	double right;\
	uint32_t comparisons;\
	uint32_t i = 0;\
	int    rc;\
\
	comparisons = args->parts[args->part_idx].num_operands - 1;\
\
	rc = filter_func_getdata(args, &left);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
	for (; i < comparisons; i++)\
	{\
		rc = filter_func_getdata(args, &right);\
		if (unlikely(rc != 0))\
		{\
			return rc;\
		}\
		if ((args->log_out = logfunc_##op(left, right)))\
		{\
			i++;\
			D_GOTO(exit, rc = 0);\
		}\
	}\
exit:\
	args->part_idx += comparisons - i;\
	if (rc < 0)\
	{\
		args->log_out = false;\
	}\
	return 0;\
}

filter_func_log(eq)
filter_func_log(ne)
filter_func_log(lt)
filter_func_log(le)
filter_func_log(ge)
filter_func_log(gt)

static bool logfunc_eq_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl)
	{
		return false;
	}
	return (memcmp(l, r, rl) == 0);
}

static bool logfunc_ne_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl)
	{
		return true;
	}
	return (memcmp(l, r, rl) != 0);
}

static bool logfunc_lt_st(char *l, size_t ll, char *r, size_t rl)
{
	size_t len = ll <= rl ? ll : rl;
	return (memcmp(l, r, len) < 0);
}

static bool logfunc_le_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl)
	{
		size_t len = ll <= rl ? ll : rl;
		return (memcmp(l, r, len) < 0);
	}
	return (memcmp(l, r, rl) <= 0);
}

static bool logfunc_ge_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl)
	{
		size_t len = ll <= rl ? ll : rl;
		return (memcmp(l, r, len) > 0);
	}
	return (memcmp(l, r, rl) >= 0);
}

static bool logfunc_gt_st(char *l, size_t ll, char *r, size_t rl)
{
	size_t len = ll <= rl ? ll : rl;
	return (memcmp(l, r, len) > 0);
}

static int filter_func_getdata_st(struct filter_part_run_t *args,
				  char **st, size_t *st_len)
{
	int     rc;
	d_iov_t *iov;
	char    *buf;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	iov    = args->iov_out;
	if (iov == NULL)
	{
		return 1;
	}
	buf     = (char *) iov->iov_buf;
	buf     = &buf[args->data_offset_out];
	*st     = buf;
	*st_len = args->data_len_out;

	return 0;
}

#define filter_func_log_st(op)\
int filter_func_##op##_st(struct filter_part_run_t *args)\
{\
	char   *left;\
	size_t left_size;\
	char   *right;\
	size_t right_size;\
	uint32_t comparisons;\
	uint32_t i = 0;\
	int    rc;\
\
	comparisons = args->parts[args->part_idx].num_operands - 1;\
\
	rc = filter_func_getdata_st(args, &left, &left_size);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
	for (; i < comparisons; i++)\
	{\
		rc = filter_func_getdata_st(args, &right, &right_size);\
		if (unlikely(rc != 0))\
		{\
			return rc;\
		}\
\
		if ((args->log_out = logfunc_##op##_st(left, left_size,\
						       right, right_size)))\
		{\
			i++;\
			D_GOTO(exit, rc = 0);\
		}\
	}\
exit:\
	args->part_idx += comparisons - i;\
	if (rc < 0)\
	{\
		args->log_out = false;\
	}\
	return 0;\
}

filter_func_log_st(eq)
filter_func_log_st(ne)
filter_func_log_st(lt)
filter_func_log_st(le)
filter_func_log_st(ge)
filter_func_log_st(gt)

static int arithfunc_add(double left, double right, double *res)
{
	*res = left + right;
	return 0;
}

static int arithfunc_sub(double left, double right, double *res)
{
	*res = left - right;
	return 0;
}

static int arithfunc_mul(double left, double right, double *res)
{
	*res = left * right;
	return 0;
}

static int arithfunc_div(double left, double right, double *res)
{
	if (right == 0.0)
	{
		return -DER_DIV_BY_ZERO;
	}
	*res = left / right;
	return 0;
}

#define filter_func_arith(op) \
int filter_func_##op(struct filter_part_run_t *args) \
{\
	double left;\
	double right;\
	int    rc = 0;\
\
	rc = filter_func_getdata(args, &left);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
	rc = filter_func_getdata(args, &right);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
\
	rc = arithfunc_##op(left, right, &args->value_out);\
exit:\
	if (rc < 0)\
	{\
		return rc;\
	}\
	return 0;\
}

filter_func_arith(add)
filter_func_arith(sub)
filter_func_arith(mul)
filter_func_arith(div)

int
filter_func_like(struct filter_part_run_t *args)
{
	char		*left_str;
	char		*right_str;
	size_t		left_size;
	size_t		left_pos;
	size_t		right_size;
	size_t		right_pos;
	size_t		right_anchor;
	bool		right_anchor_set;
	bool		scaping;
	int		rc = 0;

	rc = filter_func_getdata_st(args, &left_str, &left_size);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	rc = filter_func_getdata_st(args, &right_str, &right_size);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}

	left_pos 		= 0;
	right_pos		= 0;
	right_anchor		= 0;
	right_anchor_set	= false;
	scaping			= false;

	while (left_pos < left_size && right_pos < right_size)
	{
		if (right_str[right_pos] == '\\') {
			scaping = true;
			right_pos++;
			if (right_pos == right_size)
			{
				/** We should never reach this. */
				args->log_out = false;
				D_GOTO(exit, rc = -DER_INVAL);
			}
		}
		if (right_str[right_pos] == '%' && scaping == false)
		{
			right_anchor_set = true;
			right_anchor = ++right_pos;
			if (right_pos == right_size)
			{
				/** '%' is at the end. Pass. */
				args->log_out = true;
				D_GOTO(exit, rc = 0);
			}
		}
		if ((right_str[right_pos] == '_' && scaping == false) ||
		     left_str[left_pos] == right_str[right_pos])
		{
			left_pos++;
			right_pos++;
		}
		else if (right_anchor_set == false)
		{
			/** Mismatch and no wildcard. No pass. */
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		}
		else
		{
			right_pos = right_anchor;
			left_pos++;
		}
		scaping = false;
	}
	if (left_pos == left_size && right_pos == right_size)
	{
		/** At the end of both strings. Function pass. */
		args->log_out = true;
	}
	else
	{
		/** One string still has characters left. No pass. */
		args->log_out = false;
	}

exit:
	return rc;
}

int
filter_func_isnull(struct filter_part_run_t *args)
{
	int    rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	args->log_out = (args->iov_out == NULL);

	return 0;
}

int
filter_func_isnotnull(struct filter_part_run_t *args)
{
	int    rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	args->log_out = (args->iov_out != NULL);

	return 0;
}

int
filter_func_not(struct filter_part_run_t *args)
{
	int    rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	args->log_out = (! args->log_out);

	return 0;
}

int
filter_func_and(struct filter_part_run_t *args)
{
	int    rc;
	bool   left;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left = args->log_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	args->log_out = (left && args->log_out);

	return 0;
}

int
filter_func_or(struct filter_part_run_t *args)
{
	int    rc;
	bool   left;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left = args->log_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	args->log_out = (left || args->log_out);

	return 0;
}
