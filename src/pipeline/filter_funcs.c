
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

#define filter_func_getdata(type, ctype)                                                           \
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

filter_func_getdata(u, uint64_t) filter_func_getdata(i, int64_t) filter_func_getdata(d, double)

    static int filter_func_getdata_st(struct filter_part_run_t *args, char **st, size_t *st_len)
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

#define logfunc_eq(type, ctype)                                                                    \
	static bool logfunc_eq_##type(_##ctype left, _##ctype right) { return left == right; }

logfunc_eq(u, uint64_t);
logfunc_eq(i, int64_t);
logfunc_eq(d, double);

#define logfunc_ne(type, ctype)                                                                    \
	static bool logfunc_ne_##type(_##ctype left, _##ctype right) { return left != right; }

logfunc_ne(u, uint64_t);
logfunc_ne(i, int64_t);
logfunc_ne(d, double);

#define logfunc_lt(type, ctype)                                                                    \
	static bool logfunc_lt_##type(_##ctype left, _##ctype right) { return left < right; }

logfunc_lt(u, uint64_t);
logfunc_lt(i, int64_t);
logfunc_lt(d, double);

#define logfunc_le(type, ctype)                                                                    \
	static bool logfunc_le_##type(_##ctype left, _##ctype right) { return left <= right; }

logfunc_le(u, uint64_t);
logfunc_le(i, int64_t);
logfunc_le(d, double);

#define logfunc_ge(type, ctype)                                                                    \
	static bool logfunc_ge_##type(_##ctype left, _##ctype right) { return left >= right; }

logfunc_ge(u, uint64_t);
logfunc_ge(i, int64_t);
logfunc_ge(d, double);

#define logfunc_gt(type, ctype)                                                                    \
	static bool logfunc_gt_##type(_##ctype left, _##ctype right) { return left > right; }

logfunc_gt(u, uint64_t);
logfunc_gt(i, int64_t);
logfunc_gt(d, double);

#define filter_func_log(op, type, ctype)                                                           \
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

filter_func_log(eq, u, uint64_t);
filter_func_log(ne, u, uint64_t);
filter_func_log(lt, u, uint64_t);
filter_func_log(le, u, uint64_t);
filter_func_log(ge, u, uint64_t);
filter_func_log(gt, u, uint64_t);

filter_func_log(eq, i, int64_t);
filter_func_log(ne, i, int64_t);
filter_func_log(lt, i, int64_t);
filter_func_log(le, i, int64_t);
filter_func_log(ge, i, int64_t);
filter_func_log(gt, i, int64_t);

filter_func_log(eq, d, double);
filter_func_log(ne, d, double);
filter_func_log(lt, d, double);
filter_func_log(le, d, double);
filter_func_log(ge, d, double);
filter_func_log(gt, d, double);

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

#define filter_func_log_st(op)                                                                     \
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

filter_func_log_st(eq);
filter_func_log_st(ne);
filter_func_log_st(lt);
filter_func_log_st(le);
filter_func_log_st(ge);
filter_func_log_st(gt);

#define arithfunc_add(type, ctype)                                                                 \
	static int arithfunc_add_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		*res = left + right;                                                               \
		return 0;                                                                          \
	}

arithfunc_add(u, uint64_t);
arithfunc_add(i, int64_t);
arithfunc_add(d, double);

#define arithfunc_sub(type, ctype)                                                                 \
	static int arithfunc_sub_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		*res = left - right;                                                               \
		return 0;                                                                          \
	}

arithfunc_sub(u, uint64_t);
arithfunc_sub(i, int64_t);
arithfunc_sub(d, double);

#define arithfunc_mul(type, ctype)                                                                 \
	static int arithfunc_mul_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		*res = left * right;                                                               \
		return 0;                                                                          \
	}

arithfunc_mul(u, uint64_t);
arithfunc_mul(i, int64_t);
arithfunc_mul(d, double);

#define arithfunc_div(type, ctype)                                                                 \
	static int arithfunc_div_##type(_##ctype left, _##ctype right, _##ctype * res)             \
	{                                                                                          \
		if (right == (_##ctype)0)                                                          \
			return -DER_DIV_BY_ZERO;                                                   \
		*res = left / right;                                                               \
		return 0;                                                                          \
	}

arithfunc_div(u, uint64_t);
arithfunc_div(i, int64_t);
arithfunc_div(d, double);

#define filter_func_arith(op, type, ctype)                                                         \
	int filter_func_##op##_##type(struct filter_part_run_t *args)                              \
	{                                                                                          \
		_##ctype left  = (_##ctype)0;                                                      \
		_##ctype right = (_##ctype)0;                                                      \
		int      rc    = 0;                                                                \
		rc = filter_func_getdata_##type(args, &left);                                      \
		if (unlikely(rc != 0))                                                             \
			D_GOTO(exit, rc);                                                          \
		rc = filter_func_getdata_##type(args, &right);                                     \
		if (unlikely(rc != 0))                                                             \
			D_GOTO(exit, rc);                                                          \
		rc = arithfunc_##op##_##type(left, right, &args->value_##type##_out);              \
exit:                                                                                              \
		return rc;                                                                         \
	}

filter_func_arith(add, u, uint64_t);
filter_func_arith(add, i, int64_t);
filter_func_arith(add, d, double);
filter_func_arith(sub, u, uint64_t);
filter_func_arith(sub, i, int64_t);
filter_func_arith(sub, d, double);
filter_func_arith(mul, u, uint64_t);
filter_func_arith(mul, i, int64_t);
filter_func_arith(mul, d, double);
filter_func_arith(div, u, uint64_t);
filter_func_arith(div, i, int64_t);
filter_func_arith(div, d, double);

#define filter_func_bitand(type, ctype)                                                            \
	int filter_func_bitand_##type(struct filter_part_run_t *args)                              \
	{                                                                                          \
		_##ctype left  = 0;                                                                \
		_##ctype right = 0;                                                                \
		int      rc    = 0;                                                                \
		rc = filter_func_getdata_##type(args, &left);                                      \
		if (unlikely(rc != 0))                                                             \
			D_GOTO(exit, rc);                                                          \
		rc = filter_func_getdata_##type(args, &right);                                     \
		if (unlikely(rc != 0))                                                             \
			D_GOTO(exit, rc);                                                          \
		args->value_##type##_out = left & right;                                           \
exit:                                                                                              \
		return rc;                                                                         \
	}

filter_func_bitand(u, uint64_t);
filter_func_bitand(i, int64_t);

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
			left_pos++;
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
