
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

#define filter_func_getdata(type, ctype)\
static int filter_func_getdata_##type(struct filter_part_run_t *args,\
				      _##ctype *data)\
{\
	int     rc;\
	d_iov_t *iov;\
\
	args->part_idx += 1;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		return rc;\
	}\
	iov    = args->iov_out;\
	if (iov == NULL)\
	{\
		return 1;\
	}\
	*data = args->value_##type##_out;\
\
	return 0;\
}

filter_func_getdata(u, uint64_t)
filter_func_getdata(i, int64_t)
filter_func_getdata(d, double)

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

#define logfunc_eq(type, ctype)\
static bool logfunc_eq_##type(_##ctype left, _##ctype right)\
{\
	return left == right;\
}

logfunc_eq(u, uint64_t)
logfunc_eq(i, int64_t)
logfunc_eq(d, double)

#define logfunc_ne(type, ctype)\
static bool logfunc_ne_##type(_##ctype left, _##ctype right)\
{\
	return left != right;\
}

logfunc_ne(u, uint64_t)
logfunc_ne(i, int64_t)
logfunc_ne(d, double)

#define logfunc_lt(type, ctype)\
static bool logfunc_lt_##type(_##ctype left, _##ctype right)\
{\
	return left < right;\
}

logfunc_lt(u, uint64_t)
logfunc_lt(i, int64_t)
logfunc_lt(d, double)

#define logfunc_le(type, ctype)\
static bool logfunc_le_##type(_##ctype left, _##ctype right)\
{\
	return left <= right;\
}

logfunc_le(u, uint64_t)
logfunc_le(i, int64_t)
logfunc_le(d, double)

#define logfunc_ge(type, ctype)\
static bool logfunc_ge_##type(_##ctype left, _##ctype right)\
{\
	return left >= right;\
}

logfunc_ge(u, uint64_t)
logfunc_ge(i, int64_t)
logfunc_ge(d, double)

#define logfunc_gt(type, ctype)\
static bool logfunc_gt_##type(_##ctype left, _##ctype right)\
{\
	return left > right;\
}

logfunc_gt(u, uint64_t)
logfunc_gt(i, int64_t)
logfunc_gt(d, double)

#define filter_func_log(op, type, ctype)\
int filter_func_##op##_##type(struct filter_part_run_t *args)\
{\
	_##ctype left;\
	_##ctype right;\
	uint32_t comparisons;\
	uint32_t i = 0;\
	int    rc;\
\
	comparisons = args->parts[args->part_idx].num_operands - 1;\
\
	rc = filter_func_getdata_##type(args, &left);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
	for (; i < comparisons; i++)\
	{\
		rc = filter_func_getdata_##type(args, &right);\
		if (unlikely(rc != 0))\
		{\
			return rc;\
		}\
		if ((args->log_out = logfunc_##op##_##type(left, right)))\
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

filter_func_log(eq, u, uint64_t)
filter_func_log(ne, u, uint64_t)
filter_func_log(lt, u, uint64_t)
filter_func_log(le, u, uint64_t)
filter_func_log(ge, u, uint64_t)
filter_func_log(gt, u, uint64_t)

filter_func_log(eq, i, int64_t)
filter_func_log(ne, i, int64_t)
filter_func_log(lt, i, int64_t)
filter_func_log(le, i, int64_t)
filter_func_log(ge, i, int64_t)
filter_func_log(gt, i, int64_t)

filter_func_log(eq, d, double)
filter_func_log(ne, d, double)
filter_func_log(lt, d, double)
filter_func_log(le, d, double)
filter_func_log(ge, d, double)
filter_func_log(gt, d, double)

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

#define arithfunc_add(type, ctype)\
static int arithfunc_add_##type(_##ctype left, _##ctype right, _##ctype *res)\
{\
	*res = left + right;\
	return 0;\
}

arithfunc_add(u, uint64_t)
arithfunc_add(i, int64_t)
arithfunc_add(d, double)

#define arithfunc_sub(type, ctype)\
static int arithfunc_sub_##type(_##ctype left, _##ctype right, _##ctype *res)\
{\
	*res = left - right;\
	return 0;\
}

arithfunc_sub(u, uint64_t)
arithfunc_sub(i, int64_t)
arithfunc_sub(d, double)

#define arithfunc_mul(type, ctype)\
static int arithfunc_mul_##type(_##ctype left, _##ctype right, _##ctype *res)\
{\
	*res = left * right;\
	return 0;\
}

arithfunc_mul(u, uint64_t)
arithfunc_mul(i, int64_t)
arithfunc_mul(d, double)

#define arithfunc_div(type, ctype)\
static int arithfunc_div_##type(_##ctype left, _##ctype right, _##ctype *res)\
{\
	if (right == (_##ctype) 0)\
	{\
		return -DER_DIV_BY_ZERO;\
	}\
	*res = left / right;\
	return 0;\
}

arithfunc_div(u, uint64_t)
arithfunc_div(i, int64_t)
arithfunc_div(d, double)

#define filter_func_arith(op, type, ctype) \
int filter_func_##op##_##type(struct filter_part_run_t *args) \
{\
	_##ctype left;\
	_##ctype right;\
	int      rc = 0;\
\
	rc = filter_func_getdata_##type(args, &left);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
	rc = filter_func_getdata_##type(args, &right);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
\
	rc = arithfunc_##op##_##type(left, right, &args->value_##type##_out);\
exit:\
	if (rc < 0)\
	{\
		return rc;\
	}\
	return 0;\
}

filter_func_arith(add, u, uint64_t)
filter_func_arith(add, i, int64_t)
filter_func_arith(add, d, double)
filter_func_arith(sub, u, uint64_t)
filter_func_arith(sub, i, int64_t)
filter_func_arith(sub, d, double)
filter_func_arith(mul, u, uint64_t)
filter_func_arith(mul, i, int64_t)
filter_func_arith(mul, d, double)
filter_func_arith(div, u, uint64_t)
filter_func_arith(div, i, int64_t)
filter_func_arith(div, d, double)

#define filter_func_bitand(type, ctype)\
int filter_func_bitand_##type(struct filter_part_run_t *args)\
{\
	_##ctype left;\
	_##ctype right;\
	int      rc = 0;\
\
	rc = filter_func_getdata_##type(args, &left);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
	rc = filter_func_getdata_##type(args, &right);\
	if (unlikely(rc != 0))\
	{\
		D_GOTO(exit, rc);\
	}\
\
	args->value_##type##_out = left & right;\
exit:\
	if (rc < 0)\
	{\
		return rc;\
	}\
	return 0;\
}

filter_func_bitand(u, uint64_t)
filter_func_bitand(i, int64_t);

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
