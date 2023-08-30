
/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

/**
 * Definition of high level getdata functions. Get data functions are used to return the data
 * for akeys, constants, and dkeys.
 */

#define DEFINE_FILTER_FUNC_GETDATA(type, ctype)                                                    \
	static int filter_func_getdata_##type(struct filter_part_run_t *args, _##ctype * data)     \
	{                                                                                          \
		int rc;                                                                            \
		args->part_idx += 1;                                                               \
		rc = args->parts[args->part_idx].filter_func(args);                                \
		if (unlikely(rc != 0))                                                             \
			return rc > 0 ? 0 : rc;                                                    \
		if (args->data_out == NULL)                                                        \
			return 1;                                                                  \
		*data = args->value_##type##_out;                                                  \
		return 0;                                                                          \
	}

DEFINE_FILTER_FUNC_GETDATA(u, uint64_t)
DEFINE_FILTER_FUNC_GETDATA(i, int64_t)
DEFINE_FILTER_FUNC_GETDATA(d, double)

static int
filter_func_getdata_st(struct filter_part_run_t *args, char **st, size_t *st_len)
{
	int rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
		return rc > 0 ? 0 : rc;
	if (args->data_out == NULL)
		return 1;
	*st     = args->data_out;
	*st_len = args->data_len_out;
	return 0;
}

/**
 * Definition of logical functions for different types.
 */

#define DEFINE_LOGFUNC_EQ(type, ctype)                                                             \
	static bool logfunc_eq_##type(_##ctype left, _##ctype right) { return left == right; }

DEFINE_LOGFUNC_EQ(u, uint64_t)
DEFINE_LOGFUNC_EQ(i, int64_t)
DEFINE_LOGFUNC_EQ(d, double)

#define DEFINE_LOGFUNC_NE(type, ctype)                                                             \
	static bool logfunc_ne_##type(_##ctype left, _##ctype right) { return left != right; }

DEFINE_LOGFUNC_NE(u, uint64_t)
DEFINE_LOGFUNC_NE(i, int64_t)
DEFINE_LOGFUNC_NE(d, double)

#define DEFINE_LOGFUNC_LT(type, ctype)                                                             \
	static bool logfunc_lt_##type(_##ctype left, _##ctype right) { return left < right; }

DEFINE_LOGFUNC_LT(u, uint64_t)
DEFINE_LOGFUNC_LT(i, int64_t)
DEFINE_LOGFUNC_LT(d, double)

#define DEFINE_LOGFUNC_LE(type, ctype)                                                             \
	static bool logfunc_le_##type(_##ctype left, _##ctype right) { return left <= right; }

DEFINE_LOGFUNC_LE(u, uint64_t)
DEFINE_LOGFUNC_LE(i, int64_t)
DEFINE_LOGFUNC_LE(d, double)

#define DEFINE_LOGFUNC_GE(type, ctype)                                                             \
	static bool logfunc_ge_##type(_##ctype left, _##ctype right) { return left >= right; }

DEFINE_LOGFUNC_GE(u, uint64_t)
DEFINE_LOGFUNC_GE(i, int64_t)
DEFINE_LOGFUNC_GE(d, double)

#define DEFINE_LOGFUNC_GT(type, ctype)                                                             \
	static bool logfunc_gt_##type(_##ctype left, _##ctype right) { return left > right; }

DEFINE_LOGFUNC_GT(u, uint64_t)
DEFINE_LOGFUNC_GT(i, int64_t)
DEFINE_LOGFUNC_GT(d, double)

#define DEFINE_FILTER_FUNC_LOG(op, type, ctype)                                                    \
	int filter_func_##op##_##type(struct filter_part_run_t *args)                              \
	{                                                                                          \
		_##ctype left  = (_##ctype)0;                                                      \
		_##ctype right = (_##ctype)0;                                                      \
		uint32_t idx_end_subtree;                                                          \
		uint32_t comparisons;                                                              \
		uint32_t i;                                                                        \
		int      rc     = 0;                                                               \
		comparisons     = args->parts[args->part_idx].num_operands - 1;                    \
		idx_end_subtree = args->parts[args->part_idx].idx_end_subtree;                     \
		rc              = filter_func_getdata_##type(args, &left);                         \
		if (unlikely(rc != 0))                                                             \
			D_GOTO(exit, rc);                                                          \
		for (i = 0; i < comparisons; i++) {                                                \
			rc = filter_func_getdata_##type(args, &right);                             \
			if (unlikely(rc != 0))                                                     \
				D_GOTO(exit, rc);                                                  \
			args->log_out = logfunc_##op##_##type(left, right);                        \
			if (args->log_out)                                                         \
				D_GOTO(exit, rc = 0);                                              \
		}                                                                                  \
exit:                                                                                              \
		args->part_idx = idx_end_subtree;                                                  \
		if (unlikely(rc != 0)) {                                                           \
			args->log_out = false;                                                     \
			return rc > 0 ? 0 : rc;                                                    \
		}                                                                                  \
		return 0;                                                                          \
	}

DEFINE_FILTER_FUNC_LOG(eq, u, uint64_t)
DEFINE_FILTER_FUNC_LOG(ne, u, uint64_t)
DEFINE_FILTER_FUNC_LOG(lt, u, uint64_t)
DEFINE_FILTER_FUNC_LOG(le, u, uint64_t)
DEFINE_FILTER_FUNC_LOG(ge, u, uint64_t)
DEFINE_FILTER_FUNC_LOG(gt, u, uint64_t)

DEFINE_FILTER_FUNC_LOG(eq, i, int64_t)
DEFINE_FILTER_FUNC_LOG(ne, i, int64_t)
DEFINE_FILTER_FUNC_LOG(lt, i, int64_t)
DEFINE_FILTER_FUNC_LOG(le, i, int64_t)
DEFINE_FILTER_FUNC_LOG(ge, i, int64_t)
DEFINE_FILTER_FUNC_LOG(gt, i, int64_t)

DEFINE_FILTER_FUNC_LOG(eq, d, double)
DEFINE_FILTER_FUNC_LOG(ne, d, double)
DEFINE_FILTER_FUNC_LOG(lt, d, double)
DEFINE_FILTER_FUNC_LOG(le, d, double)
DEFINE_FILTER_FUNC_LOG(ge, d, double)
DEFINE_FILTER_FUNC_LOG(gt, d, double)

static bool logfunc_eq_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl)
		return false;
	return (memcmp(l, r, rl) == 0);
}

static bool
logfunc_ne_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl)
		return true;
	return (memcmp(l, r, rl) != 0);
}

static bool
logfunc_lt_st(char *l, size_t ll, char *r, size_t rl)
{
	size_t len = ll <= rl ? ll : rl;

	return (memcmp(l, r, len) < 0);
}

static bool
logfunc_le_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl) {
		size_t len = ll <= rl ? ll : rl;

		return (memcmp(l, r, len) < 0);
	}
	return (memcmp(l, r, rl) <= 0);
}

static bool
logfunc_ge_st(char *l, size_t ll, char *r, size_t rl)
{
	if (ll != rl) {
		size_t len = ll <= rl ? ll : rl;

		return (memcmp(l, r, len) > 0);
	}
	return (memcmp(l, r, rl) >= 0);
}

static bool
logfunc_gt_st(char *l, size_t ll, char *r, size_t rl)
{
	size_t len = ll <= rl ? ll : rl;

	return (memcmp(l, r, len) > 0);
}

#define DEFINE_FILTER_FUNC_LOG_ST(op)                                                              \
	int filter_func_##op##_st(struct filter_part_run_t *args)                                  \
	{                                                                                          \
		char  *left       = NULL;                                                          \
		size_t left_size  = 0;                                                             \
		char  *right      = NULL;                                                          \
		size_t right_size = 0;                                                             \
		uint32_t idx_end_subtree;                                                          \
		uint32_t comparisons;                                                              \
		uint32_t i;                                                                        \
		int      rc     = 0;                                                               \
		comparisons     = args->parts[args->part_idx].num_operands - 1;                    \
		idx_end_subtree = args->parts[args->part_idx].idx_end_subtree;                     \
		rc              = filter_func_getdata_st(args, &left, &left_size);                 \
		if (unlikely(rc != 0))                                                             \
			D_GOTO(exit, rc);                                                          \
		for (i = 0; i < comparisons; i++) {                                                \
			rc = filter_func_getdata_st(args, &right, &right_size);                    \
			if (unlikely(rc != 0))                                                     \
				D_GOTO(exit, rc);                                                  \
			args->log_out = logfunc_##op##_st(left, left_size, right, right_size);     \
			if (args->log_out)                                                         \
				D_GOTO(exit, rc = 0);                                              \
		}                                                                                  \
exit:                                                                                              \
		args->part_idx = idx_end_subtree;                                                  \
		if (unlikely(rc != 0)) {                                                           \
			args->log_out = false;                                                     \
			return rc > 0 ? 0 : rc;                                                    \
		}                                                                                  \
		return 0;                                                                          \
	}

DEFINE_FILTER_FUNC_LOG_ST(eq)
DEFINE_FILTER_FUNC_LOG_ST(ne)
DEFINE_FILTER_FUNC_LOG_ST(lt)
DEFINE_FILTER_FUNC_LOG_ST(le)
DEFINE_FILTER_FUNC_LOG_ST(ge)
DEFINE_FILTER_FUNC_LOG_ST(gt)

/**
 * Definition of arithmetic functions for different types.
 */

#define DEFINE_ARITHFUNC_ADD(type, ctype)                                                          \
	static int arithfunc_add_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		*res = left + right;                                                               \
		return 0;                                                                          \
	}

DEFINE_ARITHFUNC_ADD(u, uint64_t)
DEFINE_ARITHFUNC_ADD(i, int64_t)
DEFINE_ARITHFUNC_ADD(d, double)

#define DEFINE_ARITHFUNC_SUB(type, ctype)                                                          \
	static int arithfunc_sub_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		*res = left - right;                                                               \
		return 0;                                                                          \
	}

DEFINE_ARITHFUNC_SUB(u, uint64_t)
DEFINE_ARITHFUNC_SUB(i, int64_t)
DEFINE_ARITHFUNC_SUB(d, double)

#define DEFINE_ARITHFUNC_MUL(type, ctype)                                                          \
	static int arithfunc_mul_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		*res = left * right;                                                               \
		return 0;                                                                          \
	}

DEFINE_ARITHFUNC_MUL(u, uint64_t)
DEFINE_ARITHFUNC_MUL(i, int64_t)
DEFINE_ARITHFUNC_MUL(d, double)

#define DEFINE_ARITHFUNC_DIV(type, ctype)                                                          \
	static int arithfunc_div_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		if (right == (_##ctype)0)                                                          \
			return -DER_DIV_BY_ZERO;                                                   \
		*res = left / right;                                                               \
		return 0;                                                                          \
	}

DEFINE_ARITHFUNC_DIV(u, uint64_t)
DEFINE_ARITHFUNC_DIV(i, int64_t)
DEFINE_ARITHFUNC_DIV(d, double)

#define DEFINE_FILTER_FUNC_ARITH(op, type, ctype)                                                  \
	int filter_func_##op##_##type(struct filter_part_run_t *args)                              \
	{                                                                                          \
		_##ctype left  = (_##ctype)0;                                                      \
		_##ctype right = (_##ctype)0;                                                      \
		int      rc;                                                                       \
		rc = filter_func_getdata_##type(args, &left);                                      \
		if (unlikely(rc != 0))                                                             \
			return rc;                                                                 \
		rc = filter_func_getdata_##type(args, &right);                                     \
		if (unlikely(rc != 0))                                                             \
			return rc;                                                                 \
		rc = arithfunc_##op##_##type(left, right, &args->value_##type##_out);              \
		return rc;                                                                         \
	}

DEFINE_FILTER_FUNC_ARITH(add, u, uint64_t)
DEFINE_FILTER_FUNC_ARITH(add, i, int64_t)
DEFINE_FILTER_FUNC_ARITH(add, d, double)
DEFINE_FILTER_FUNC_ARITH(sub, u, uint64_t)
DEFINE_FILTER_FUNC_ARITH(sub, i, int64_t)
DEFINE_FILTER_FUNC_ARITH(sub, d, double)
DEFINE_FILTER_FUNC_ARITH(mul, u, uint64_t)
DEFINE_FILTER_FUNC_ARITH(mul, i, int64_t)
DEFINE_FILTER_FUNC_ARITH(mul, d, double)
DEFINE_FILTER_FUNC_ARITH(div, u, uint64_t)
DEFINE_FILTER_FUNC_ARITH(div, i, int64_t)
DEFINE_FILTER_FUNC_ARITH(div, d, double)

#define DEFINE_FILTER_FUNC_BITAND(type, ctype)                                                     \
	int filter_func_bitand_##type(struct filter_part_run_t *args)                              \
	{                                                                                          \
		_##ctype left  = 0;                                                                \
		_##ctype right = 0;                                                                \
		int      rc;                                                                       \
		rc = filter_func_getdata_##type(args, &left);                                      \
		if (unlikely(rc != 0))                                                             \
			return rc;                                                                 \
		rc = filter_func_getdata_##type(args, &right);                                     \
		if (unlikely(rc != 0))                                                             \
			return rc;                                                                 \
		args->value_##type##_out = left & right;                                           \
		return rc;                                                                         \
	}

DEFINE_FILTER_FUNC_BITAND(u, uint64_t)
DEFINE_FILTER_FUNC_BITAND(i, int64_t)

/**
 * like function for strings
 */

int
filter_func_like(struct filter_part_run_t *args)
{
	char  *left_str  = NULL;
	char  *right_str = NULL;
	size_t left_size = 0;
	size_t left_pos;
	size_t right_size = 0;
	size_t right_pos;
	size_t right_anchor;
	bool   right_anchor_set;
	bool   scaping;
	int    rc = 0;

	rc = filter_func_getdata_st(args, &left_str, &left_size);
	if (unlikely(rc != 0))
		D_GOTO(exit, rc);
	rc = filter_func_getdata_st(args, &right_str, &right_size);
	if (unlikely(rc != 0))
		D_GOTO(exit, rc);

	left_pos         = 0;
	right_pos        = 0;
	right_anchor     = 0;
	right_anchor_set = false;
	scaping          = false;

	while (left_pos < left_size && right_pos < right_size) {
		if (right_str[right_pos] == '\\') {
			scaping = true;
			right_pos++;
			if (right_pos == right_size)
				D_GOTO(exit, rc = -DER_INVAL); /** We should never reach this. */
		}
		if (right_str[right_pos] == '%' && !scaping) {
			right_anchor_set = true;
			right_anchor     = ++right_pos;
			if (right_pos == right_size) {
				/** '%' is at the end of pattern. Pass. */
				args->log_out = true;
				D_GOTO(exit, rc = 0);
			}
		}
		if ((right_str[right_pos] == '_' && !scaping) ||
		    left_str[left_pos] == right_str[right_pos]) {
			left_pos++;
			right_pos++;
		} else if (!right_anchor_set) {
			/** Mismatch and no wildcard. No pass. */
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		} else {
			right_pos = right_anchor;
			if (left_str[left_pos] != right_str[right_pos])
				left_pos++; /** only advance if left is not equal to the anchor */
		}
		scaping = false;
		if ((left_pos == left_size) && (right_pos == right_size - 1) &&
		    right_str[right_pos] == '%') {
			/** At the end of string and only thing left is '%'. Pass. */
			args->log_out = true;
			D_GOTO(exit, rc = 0);
		}
	}
	if (left_pos == left_size && right_pos == right_size) {
		/** At the end of both strings. Function pass. */
		args->log_out = true;
	} else {
		/** One string still has characters left. No pass. */
		args->log_out = false;
	}

exit:
	if (unlikely(rc != 0)) {
		args->log_out = false;
		return rc > 0 ? 0 : rc;
	}
	return 0;
}

/**
 * ==NULL and !=NULL
 */

int
filter_func_isnull(struct filter_part_run_t *args)
{
	int rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0)) {
		args->log_out = false;
		return rc > 0 ? 0 : rc;
	}
	args->log_out = (args->data_out == NULL);
	return 0;
}

int
filter_func_isnotnull(struct filter_part_run_t *args)
{
	int rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0)) {
		args->log_out = false;
		return rc > 0 ? 0 : rc;
	}
	args->log_out = (args->data_out != NULL);
	return 0;
}

/**
 * general logical operators
 */

int
filter_func_not(struct filter_part_run_t *args)
{
	int rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0)) {
		args->log_out = false;
		return rc > 0 ? 0 : rc;
	}
	args->log_out = (!args->log_out);

	return 0;
}

int
filter_func_and(struct filter_part_run_t *args)
{
	int      rc  = 0;
	bool     res = false;
	uint32_t idx_end_subtree;
	uint32_t comparisons;
	uint32_t i;

	comparisons     = args->parts[args->part_idx].num_operands - 1;
	idx_end_subtree = args->parts[args->part_idx].idx_end_subtree;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
		D_GOTO(exit, rc);
	if (!args->log_out)
		D_GOTO(exit, rc = 0);
	for (i = 0; i < comparisons; i++) {
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
			D_GOTO(exit, rc);
		if (!args->log_out)
			D_GOTO(exit, rc = 0);
	}
	res = true;
exit:
	args->part_idx = idx_end_subtree;
	args->log_out  = res;
	if (unlikely(rc > 0))
		return 0;
	return rc;
}

int
filter_func_or(struct filter_part_run_t *args)
{
	int      rc  = 0;
	bool     res = true;
	uint32_t idx_end_subtree;
	uint32_t comparisons;
	uint32_t i;

	comparisons     = args->parts[args->part_idx].num_operands - 1;
	idx_end_subtree = args->parts[args->part_idx].idx_end_subtree;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
		D_GOTO(exit, rc);
	if (args->log_out)
		D_GOTO(exit, rc = 0);
	for (i = 0; i < comparisons; i++) {
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
			D_GOTO(exit, rc);
		if (args->log_out)
			D_GOTO(exit, rc = 0);
	}
	res = false;
exit:
	args->part_idx = idx_end_subtree;
	if (unlikely(rc != 0)) {
		res = false;
		return rc > 0 ? 0 : rc;
	}
	args->log_out = res;
	return 0;
}
