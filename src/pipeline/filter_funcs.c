
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

#define filter_func_eq(typename, typec) \
int filter_func_eq_##typename(struct filter_part_run_t *args) \
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
		args->log_out = (left == right);\
	}\
	return 0;\
}

filter_func_eq(i1, int8_t)
filter_func_eq(i2, int16_t)
filter_func_eq(i4, int32_t)
filter_func_eq(i8, int64_t)
filter_func_eq(r4, float)
filter_func_eq(r8, double)

int
filter_func_eq_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		right_offset;
	size_t		right_size;
	int		rc;

	args->part_idx += 1;

	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;

	args->part_idx += 1;

	right_offset = args->parts[args->part_idx].data_offset;
	right_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	right = args->iov_out;

	if (left == NULL || right == NULL || left_offset >= left->iov_len ||
		right_offset >= right->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	if (right_offset + right_size > right->iov_len)
	{
		right_size = right->iov_len - right_offset;
	}

	if (left_size != right_size)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];
	right_str = (char *) right->iov_buf;
	right_str = &right_str[right_offset];

	args->log_out = (memcmp(left_str, right_str, right_size) == 0);

exit:
	return rc;
}

int
filter_func_eq_raw(struct filter_part_run_t *args)
{
	return filter_func_eq_st(args);
}

#define filter_func_in(typename, typec) \
int filter_func_in_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	uint32_t comparisons;\
	uint32_t i = 0;\
	int      rc;\
\
	comparisons = args->parts[args->part_idx].num_operands - 1;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		D_GOTO(exit, rc);\
	}\
	left_iov      = args->iov_out;\
	if (left_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len)\
	{\
		args->log_out = false;\
		D_GOTO(exit, rc = 0);\
	}\
	left_ptr = left_iov->iov_buf;\
	left     = *((_##typec *) &left_ptr[left_offset]);\
\
	for (; i < comparisons; i++)\
	{\
		args->part_idx += 1;\
		right_offset = args->parts[args->part_idx].data_offset;\
		rc = args->parts[args->part_idx].filter_func(args);\
		if (unlikely(rc != 0))\
		{\
			args->log_out = false;\
			D_GOTO(exit, rc);\
		}\
		right_iov = args->iov_out;\
		if (right_iov == NULL ||\
			right_offset + sizeof(_##typec) > right_iov->iov_len)\
		{\
			args->log_out = false;\
			D_GOTO(exit, rc = 0);\
		}\
		right_ptr = right_iov->iov_buf;\
		right     = *((_##typec *) &right_ptr[right_offset]);\
\
		if ((args->log_out = (left == right)))\
		{\
			i++;\
			D_GOTO(exit, rc = 0);\
		}\
	}\
\
exit:\
	args->part_idx += comparisons - i;\
	return rc;\
}

filter_func_in(i1, int8_t)
filter_func_in(i2, int16_t)
filter_func_in(i4, int32_t)
filter_func_in(i8, int64_t)
filter_func_in(r4, float)
filter_func_in(r8, double)

int
filter_func_in_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		right_offset;
	size_t		right_size;
	uint32_t	comparisons;
	uint32_t	i = 0;
	int		rc;

	comparisons = args->parts[args->part_idx].num_operands - 1;

	args->part_idx += 1;
	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;
	if (left == NULL || left_offset >= left->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];

	for (; i < comparisons; i++)
	{
		args->part_idx += 1;
		right_offset = args->parts[args->part_idx].data_offset;
		right_size   = args->parts[args->part_idx].data_len;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
		{
			args->log_out = false;
			D_GOTO(exit, rc);
		}
		right = args->iov_out;

		if (right == NULL || right_offset >= right->iov_len)
		{
			continue;
		}

		if (right_offset + right_size > right->iov_len)
		{
			right_size = right->iov_len - right_offset;
		}

		right_str = (char *) right->iov_buf;
		right_str = &right_str[right_offset];

		if (left_size != right_size)
		{
			continue;
		}

		if ((args->log_out =
				(memcmp(left_str, right_str, right_size) == 0)))
		{
			i++;
			D_GOTO(exit, rc = 0);
		}
	}

	args->log_out = false;
exit:
	args->part_idx += comparisons - i;
	return rc;
}

int
filter_func_in_raw(struct filter_part_run_t *args)
{
	return filter_func_in_st(args);
}

#define filter_func_ne(typename, typec)\
int filter_func_ne_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
		args->log_out = (left != right);\
	}\
	return 0;\
}

filter_func_ne(i1, int8_t)
filter_func_ne(i2, int16_t)
filter_func_ne(i4, int32_t)
filter_func_ne(i8, int64_t)
filter_func_ne(r4, float)
filter_func_ne(r8, double)

int
filter_func_ne_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		right_offset;
	size_t		right_size;
	int		rc;

	args->part_idx += 1;
	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;

	args->part_idx += 1;
	right_offset = args->parts[args->part_idx].data_offset;
	right_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	right = args->iov_out;

	if (left == NULL || right == NULL || left_offset >= left->iov_len ||
		right_offset >= right->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	if (right_offset + right_size > right->iov_len)
	{
		right_size = right->iov_len - right_offset;
	}

	if (left_size != right_size)
	{
		args->log_out = true;
		D_GOTO(exit, rc = 0);
	}

	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];
	right_str = (char *) right->iov_buf;
	right_str = &right_str[right_offset];

	args->log_out = (memcmp(left_str, right_str, right_size) != 0);

exit:
	return rc;
}

int
filter_func_ne_raw(struct filter_part_run_t *args)
{
	return filter_func_ne_st(args);
}

#define filter_func_lt(typename, typec)\
int filter_func_lt_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
		args->log_out = (left < right);\
	}\
	return 0;\
}

filter_func_lt(i1, int8_t)
filter_func_lt(i2, int16_t)
filter_func_lt(i4, int32_t)
filter_func_lt(i8, int64_t)
filter_func_lt(r4, float)
filter_func_lt(r8, double)

int
filter_func_lt_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		right_offset;
	size_t		right_size;
	size_t		cmp_size;
	int		rc;

	args->part_idx += 1;
	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;

	args->part_idx += 1;
	right_offset = args->parts[args->part_idx].data_offset;
	right_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	right = args->iov_out;

	if (left == NULL || right == NULL || left_offset >= left->iov_len ||
		right_offset >= right->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	if (right_offset + right_size > right->iov_len)
	{
		right_size = right->iov_len - right_offset;
	}

	cmp_size  = left_size <= right_size ? left_size : right_size;

	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];
	right_str = (char *) right->iov_buf;
	right_str = &right_str[right_offset];

	args->log_out = (memcmp(left_str, right_str, cmp_size) < 0);

exit:
	return rc;
}

int
filter_func_lt_raw(struct filter_part_run_t *args)
{
	return filter_func_ne_st(args);
}

#define filter_func_le(typename, typec)\
int filter_func_le_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
		args->log_out = (left <= right);\
	}\
	return 0;\
}

filter_func_le(i1, int8_t)
filter_func_le(i2, int16_t)
filter_func_le(i4, int32_t)
filter_func_le(i8, int64_t)
filter_func_le(r4, float)
filter_func_le(r8, double)

int
filter_func_le_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		right_offset;
	size_t		right_size;
	size_t		cmp_size;
	int		rc;

	args->part_idx += 1;
	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;

	args->part_idx += 1;
	right_offset = args->parts[args->part_idx].data_offset;
	right_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	right = args->iov_out;

	if (left == NULL || right == NULL || left_offset >= left->iov_len ||
		right_offset >= right->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	if (right_offset + right_size > right->iov_len)
	{
		right_size = right->iov_len - right_offset;
	}

	cmp_size  = left_size <= right_size ? left_size : right_size;

	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];
	right_str = (char *) right->iov_buf;
	right_str = &right_str[right_offset];

	args->log_out = (memcmp(left_str, right_str, cmp_size) <= 0);

exit:
	return rc;
}

int
filter_func_le_raw(struct filter_part_run_t *args)
{
	return filter_func_le_st(args);
}

#define filter_func_ge(typename, typec)\
int filter_func_ge_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
		args->log_out = (left >= right);\
	}\
	return 0;\
}

filter_func_ge(i1, int8_t)
filter_func_ge(i2, int16_t)
filter_func_ge(i4, int32_t)
filter_func_ge(i8, int64_t)
filter_func_ge(r4, float)
filter_func_ge(r8, double)

int
filter_func_ge_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		right_offset;
	size_t		right_size;
	size_t		cmp_size;
	int		rc;

	args->part_idx += 1;
	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;

	args->part_idx += 1;
	right_offset = args->parts[args->part_idx].data_offset;
	right_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	right = args->iov_out;

	if (left == NULL || right == NULL || left_offset >= left->iov_len ||
		right_offset >= right->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	if (right_offset + right_size > right->iov_len)
	{
		right_size = right->iov_len - right_offset;
	}

	cmp_size  = left_size <= right_size ? left_size : right_size;

	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];
	right_str = (char *) right->iov_buf;
	right_str = &right_str[right_offset];

	args->log_out = (memcmp(left_str, right_str, cmp_size) >= 0);

exit:
	return rc;
}

int
filter_func_ge_raw(struct filter_part_run_t *args)
{
	return filter_func_ge_st(args);
}

#define filter_func_gt(typename, typec)\
int filter_func_gt_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
		args->log_out = (left > right);\
	}\
	return 0;\
}

filter_func_gt(i1, int8_t)
filter_func_gt(i2, int16_t)
filter_func_gt(i4, int32_t)
filter_func_gt(i8, int64_t)
filter_func_gt(r4, float)
filter_func_gt(r8, double)

int
filter_func_gt_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		right_offset;
	size_t		right_size;
	size_t		cmp_size;
	int		rc;

	args->part_idx += 1;
	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;

	args->part_idx += 1;
	right_offset = args->parts[args->part_idx].data_offset;
	right_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	right = args->iov_out;

	if (left == NULL || right == NULL || left_offset >= left->iov_len ||
		right_offset >= right->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	if (right_offset + right_size > right->iov_len)
	{
		right_size = right->iov_len - right_offset;
	}

	cmp_size  = left_size <= right_size ? left_size : right_size;

	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];
	right_str = (char *) right->iov_buf;
	right_str = &right_str[right_offset];

	args->log_out = (memcmp(left_str, right_str, cmp_size) > 0);

exit:
	return rc;
}

int
filter_func_gt_raw(struct filter_part_run_t *args)
{
	return filter_func_gt_st(args);
}

#define filter_func_add(typename, typec)\
int filter_func_add_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	_##typec *v_out;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		v_out         = (_##typec *) args->iov_extra.iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
\
		*v_out                  = left + right;\
		args->iov_extra.iov_len = sizeof(_##typec);\
		args->iov_out           = &args->iov_extra;\
	}\
	return 0;\
}

filter_func_add(i1, int8_t)
filter_func_add(i2, int16_t)
filter_func_add(i4, int32_t)
filter_func_add(i8, int64_t)
filter_func_add(r4, float)
filter_func_add(r8, double)

#define filter_func_sub(typename, typec)\
int filter_func_sub_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	_##typec *v_out;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		v_out         = (_##typec *) args->iov_extra.iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
\
		*v_out                  = left - right;\
		args->iov_extra.iov_len = sizeof(_##typec);\
		args->iov_out           = &args->iov_extra;\
	}\
	return 0;\
}

filter_func_sub(i1, int8_t)
filter_func_sub(i2, int16_t)
filter_func_sub(i4, int32_t)
filter_func_sub(i8, int64_t)
filter_func_sub(r4, float)
filter_func_sub(r8, double)

#define filter_func_mul(typename, typec)\
int filter_func_mul_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	_##typec *v_out;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		v_out         = (_##typec *) args->iov_extra.iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
\
		*v_out                  = left * right;\
		args->iov_extra.iov_len = sizeof(_##typec);\
		args->iov_out           = &args->iov_extra;\
	}\
	return 0;\
}

filter_func_mul(i1, int8_t)
filter_func_mul(i2, int16_t)
filter_func_mul(i4, int32_t)
filter_func_mul(i8, int64_t)
filter_func_mul(r4, float)
filter_func_mul(r8, double)

#define filter_func_div(typename, typec)\
int filter_func_div_##typename(struct filter_part_run_t *args)\
{\
	d_iov_t  *left_iov;\
	char     *left_ptr;\
	d_iov_t  *right_iov;\
	char     *right_ptr;\
	_##typec left;\
	size_t   left_offset;\
	_##typec right;\
	size_t   right_offset;\
	_##typec *v_out;\
	int      rc;\
\
	args->part_idx += 1;\
	left_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	left_iov = args->iov_out;\
\
	args->part_idx += 1;\
	right_offset = args->parts[args->part_idx].data_offset;\
	rc = args->parts[args->part_idx].filter_func(args);\
	if (unlikely(rc != 0))\
	{\
		args->log_out = false;\
		return rc;\
	}\
	right_iov = args->iov_out;\
\
	if (left_iov == NULL ||\
		right_iov == NULL ||\
		left_offset + sizeof(_##typec) > left_iov->iov_len ||\
		right_offset + sizeof(_##typec) > right_iov->iov_len)\
	{\
		args->log_out = false;\
	}\
	else\
	{\
		left_ptr      = left_iov->iov_buf;\
		right_ptr     = right_iov->iov_buf;\
		v_out         = (_##typec *) args->iov_extra.iov_buf;\
		left          = *((_##typec *) &left_ptr[left_offset]);\
		right         = *((_##typec *) &right_ptr[right_offset]);\
		if (unlikely(right == ((_##typec) 0)))\
		{\
			return -DER_DIV_BY_ZERO;\
		}\
\
		*v_out                  = left / right;\
		args->iov_extra.iov_len = sizeof(_##typec);\
		args->iov_out           = &args->iov_extra;\
	}\
	return 0;\
}

filter_func_div(i1, int8_t)
filter_func_div(i2, int16_t)
filter_func_div(i4, int32_t)
filter_func_div(i8, int64_t)
filter_func_div(r4, float)
filter_func_div(r8, double)

int
filter_func_like_st(struct filter_part_run_t *args)
{
	d_iov_t		*left;
	d_iov_t		*right;
	char		*left_str;
	char		*right_str;
	size_t		left_offset;
	size_t		left_size;
	size_t		left_pos;
	size_t		right_offset;
	size_t		right_size;
	size_t		right_pos;
	size_t		right_anchor;
	bool		right_anchor_set;
	bool		scaping;
	int		rc = 0;

	args->part_idx += 1;
	left_offset = args->parts[args->part_idx].data_offset;
	left_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left = args->iov_out;

	args->part_idx += 1;
	right_offset = args->parts[args->part_idx].data_offset;
	right_size   = args->parts[args->part_idx].data_len;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	right = args->iov_out;

	if (left == NULL || right == NULL || left_offset >= left->iov_len ||
		right_offset >= right->iov_len)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}

	if (left_offset + left_size > left->iov_len)
	{
		left_size = left->iov_len - left_offset;
	}
	if (right_offset + right_size > right->iov_len)
	{
		right_size = right->iov_len - right_offset;
	}

	left_str  = (char *) left->iov_buf;
	left_str  = &left_str[left_offset];
	right_str = (char *) right->iov_buf;
	right_str = &right_str[right_offset];

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
filter_func_isnull_raw(struct filter_part_run_t *args)
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
filter_func_isnotnull_raw(struct filter_part_run_t *args)
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
