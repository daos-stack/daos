
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

int
filter_func_eq_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);
		args->log_out = (left == right);
	}
	return 0;
}

int
filter_func_eq_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t  left;
	int16_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);
		args->log_out = (left == right);
	}
	return 0;
}

int
filter_func_eq_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t  left;
	int32_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);
		args->log_out = (left == right);
	}
	return 0;
}

int
filter_func_eq_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t  left;
	int64_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);
		args->log_out = (left == right);
	}
	return 0;
}

int
filter_func_eq_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);
		args->log_out = (left == right);
	}
	return 0;
}

int
filter_func_eq_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);
		args->log_out = (left == right);
	}
	return 0;
}

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

int
filter_func_in_i1(struct filter_part_run_t *args)
{
	d_iov_t  *left_iov;
	d_iov_t  *right_iov;
	int8_t   left;
	int8_t   right;
	uint32_t comparisons;
	uint32_t i = 0;
	int      rc;

	comparisons = args->parts[args->part_idx].num_operands - 1;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left_iov      = args->iov_out;
	if (left_iov == NULL)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}
	left = *((int8_t *) left_iov->iov_buf);

	for (; i < comparisons; i++)
	{
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
		{
			args->log_out = false;
			D_GOTO(exit, rc);
		}
		right_iov = args->iov_out;
		if (right_iov == NULL)
		{
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		}
		right = *((int8_t *) right_iov->iov_buf);

		if ((args->log_out = (left == right)))
		{
			i++;
			D_GOTO(exit, rc = 0);
		}
	}

exit:
	args->part_idx += comparisons - i;
	return rc;
}

int
filter_func_in_i2(struct filter_part_run_t *args)
{
	d_iov_t  *left_iov;
	d_iov_t  *right_iov;
	int16_t  left;
	int16_t  right;
	uint32_t comparisons;
	uint32_t i = 0;
	int      rc;

	comparisons = args->parts[args->part_idx].num_operands - 1;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left_iov      = args->iov_out;
	if (left_iov == NULL)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}
	left = *((int16_t *) left_iov->iov_buf);

	for (; i < comparisons; i++)
	{
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
		{
			args->log_out = false;
			D_GOTO(exit, rc);
		}
		right_iov = args->iov_out;
		if (right_iov == NULL)
		{
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		}
		right = *((int16_t *) right_iov->iov_buf);

		if ((args->log_out = (left == right)))
		{
			i++;
			D_GOTO(exit, rc = 0);
		}
	}

exit:
	args->part_idx += comparisons - i;
	return rc;
}

int
filter_func_in_i4(struct filter_part_run_t *args)
{
	d_iov_t  *left_iov;
	d_iov_t  *right_iov;
	int32_t  left;
	int32_t  right;
	uint32_t comparisons;
	uint32_t i = 0;
	int      rc;

	comparisons = args->parts[args->part_idx].num_operands - 1;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left_iov      = args->iov_out;
	if (left_iov == NULL)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}
	left = *((int32_t *) left_iov->iov_buf);

	for (; i < comparisons; i++)
	{
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
		{
			args->log_out = false;
			D_GOTO(exit, rc);
		}
		right_iov = args->iov_out;
		if (right_iov == NULL)
		{
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		}
		right = *((int32_t *) right_iov->iov_buf);

		if ((args->log_out = (left == right)))
		{
			i++;
			D_GOTO(exit, rc = 0);
		}
	}

exit:
	args->part_idx += comparisons - i;
	return rc;
}

int
filter_func_in_i8(struct filter_part_run_t *args)
{
	d_iov_t  *left_iov;
	d_iov_t  *right_iov;
	int64_t  left;
	int64_t  right;
	uint32_t comparisons;
	uint32_t i = 0;
	int      rc;

	comparisons = args->parts[args->part_idx].num_operands - 1;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left_iov      = args->iov_out;
	if (left_iov == NULL)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}
	left = *((int64_t *) left_iov->iov_buf);

	for (; i < comparisons; i++)
	{
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
		{
			args->log_out = false;
			D_GOTO(exit, rc);
		}
		right_iov = args->iov_out;
		if (right_iov == NULL)
		{
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		}
		right = *((int64_t *) right_iov->iov_buf);

		if ((args->log_out = (left == right)))
		{
			i++;
			D_GOTO(exit, rc = 0);
		}
	}

exit:
	args->part_idx += comparisons - i;
	return rc;
}

int
filter_func_in_r4(struct filter_part_run_t *args)
{
	d_iov_t  *left_iov;
	d_iov_t  *right_iov;
	float    left;
	float    right;
	uint32_t comparisons;
	uint32_t i = 0;
	int      rc;

	comparisons = args->parts[args->part_idx].num_operands - 1;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left_iov      = args->iov_out;
	if (left_iov == NULL)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}
	left = *((float *) left_iov->iov_buf);

	for (; i < comparisons; i++)
	{
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
		{
			args->log_out = false;
			D_GOTO(exit, rc);
		}
		right_iov = args->iov_out;
		if (right_iov == NULL)
		{
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		}
		right = *((float *) right_iov->iov_buf);

		if ((args->log_out = (left == right)))
		{
			i++;
			D_GOTO(exit, rc = 0);
		}
	}

exit:
	args->part_idx += comparisons - i;
	return rc;
}

int
filter_func_in_r8(struct filter_part_run_t *args)
{
	d_iov_t  *left_iov;
	d_iov_t  *right_iov;
	int64_t   left;
	int64_t   right;
	uint32_t comparisons;
	uint32_t i = 0;
	int      rc;

	comparisons = args->parts[args->part_idx].num_operands - 1;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		D_GOTO(exit, rc);
	}
	left_iov      = args->iov_out;
	if (left_iov == NULL)
	{
		args->log_out = false;
		D_GOTO(exit, rc = 0);
	}
	left = *((int64_t *) left_iov->iov_buf);

	for (; i < comparisons; i++)
	{
		args->part_idx += 1;
		rc = args->parts[args->part_idx].filter_func(args);
		if (unlikely(rc != 0))
		{
			args->log_out = false;
			D_GOTO(exit, rc);
		}
		right_iov = args->iov_out;
		if (right_iov == NULL)
		{
			args->log_out = false;
			D_GOTO(exit, rc = 0);
		}
		right = *((int64_t *) right_iov->iov_buf);

		if ((args->log_out = (left == right)))
		{
			i++;
			D_GOTO(exit, rc = 0);
		}
	}

exit:
	args->part_idx += comparisons - i;
	return rc;
}

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

int
filter_func_ne_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);
		args->log_out = (left != right);
	}
	return 0;
}

int
filter_func_ne_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t  left;
	int16_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);
		args->log_out = (left != right);
	}
	return 0;
}

int
filter_func_ne_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t  left;
	int32_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);
		args->log_out = (left != right);
	}
	return 0;
}

int
filter_func_ne_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t  left;
	int64_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);
		args->log_out = (left != right);
	}
	return 0;
}

int
filter_func_ne_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);
		args->log_out = (left != right);
	}
	return 0;
}

int
filter_func_ne_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);
		args->log_out = (left != right);
	}
	return 0;
}

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

int
filter_func_lt_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);
		args->log_out = (left < right);
	}
	return 0;
}

int
filter_func_lt_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t  left;
	int16_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);
		args->log_out = (left < right);
	}
	return 0;
}

int
filter_func_lt_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t  left;
	int32_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);
		args->log_out = (left < right);
	}
	return 0;
}

int
filter_func_lt_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t  left;
	int64_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);
		args->log_out = (left < right);
	}
	return 0;
}

int
filter_func_lt_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);
		args->log_out = (left < right);
	}
	return 0;
}

int
filter_func_lt_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);
		args->log_out = (left < right);
	}
	return 0;
}

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

int
filter_func_le_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);
		args->log_out = (left <= right);
	}
	return 0;
}

int
filter_func_le_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t  left;
	int16_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);
		args->log_out = (left <= right);
	}
	return 0;
}

int
filter_func_le_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t  left;
	int32_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);
		args->log_out = (left <= right);
	}
	return 0;
}

int
filter_func_le_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t  left;
	int64_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);
		args->log_out = (left <= right);
	}
	return 0;
}

int
filter_func_le_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);
		args->log_out = (left <= right);
	}
	return 0;
}

int
filter_func_le_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);
		args->log_out = (left <= right);
	}
	return 0;
}

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

int
filter_func_ge_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);
		args->log_out = (left >= right);
	}
	return 0;
}

int
filter_func_ge_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t  left;
	int16_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);
		args->log_out = (left >= right);
	}
	return 0;
}

int
filter_func_ge_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t  left;
	int32_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);
		args->log_out = (left >= right);
	}
	return 0;
}

int
filter_func_ge_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t  left;
	int64_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);
		args->log_out = (left >= right);
	}
	return 0;
}

int
filter_func_ge_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);
		args->log_out = (left >= right);
	}
	return 0;
}

int
filter_func_ge_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);
		args->log_out = (left >= right);
	}
	return 0;
}

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

int
filter_func_gt_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);
		args->log_out = (left > right);
	}
	return 0;
}

int
filter_func_gt_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t  left;
	int16_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);
		args->log_out = (left > right);
	}
	return 0;
}

int
filter_func_gt_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t  left;
	int32_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);
		args->log_out = (left > right);
	}
	return 0;
}

int
filter_func_gt_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t  left;
	int64_t  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);
		args->log_out = (left > right);
	}
	return 0;
}

int
filter_func_gt_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);
		args->log_out = (left > right);
	}
	return 0;
}

int
filter_func_gt_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		args->log_out = false;
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->log_out = false;
	}
	else
	{
		left  = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);
		args->log_out = (left > right);
	}
	return 0;
}

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

int
filter_func_add_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int8_t  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);

		v_out  = (int8_t *) args->iov_extra.iov_buf;
		*v_out = left + right;

		args->iov_extra.iov_len = sizeof(int8_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_add_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t left;
	int16_t right;
	int16_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);

		v_out  = (int16_t *) args->iov_extra.iov_buf;
		*v_out = left + right;

		args->iov_extra.iov_len = sizeof(int16_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_add_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t left;
	int32_t right;
	int32_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);

		v_out  = (int32_t *) args->iov_extra.iov_buf;
		*v_out = left + right;

		args->iov_extra.iov_len = sizeof(int32_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_add_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t left;
	int64_t right;
	int64_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);

		v_out  = (int64_t *) args->iov_extra.iov_buf;
		*v_out = left + right;

		args->iov_extra.iov_len = sizeof(int64_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_add_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	float   *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);

		v_out  = (float *) args->iov_extra.iov_buf;
		*v_out = left + right;

		args->iov_extra.iov_len = sizeof(float);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_add_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	double  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);

		v_out  = (double *) args->iov_extra.iov_buf;
		*v_out = left + right;

		args->iov_extra.iov_len = sizeof(double);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_sub_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int8_t  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);

		v_out  = (int8_t *) args->iov_extra.iov_buf;
		*v_out = left - right;

		args->iov_extra.iov_len = sizeof(int8_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_sub_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t left;
	int16_t right;
	int16_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);

		v_out  = (int16_t *) args->iov_extra.iov_buf;
		*v_out = left - right;

		args->iov_extra.iov_len = sizeof(int16_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_sub_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t left;
	int32_t right;
	int32_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);

		v_out  = (int32_t *) args->iov_extra.iov_buf;
		*v_out = left - right;

		args->iov_extra.iov_len = sizeof(int32_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_sub_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t left;
	int64_t right;
	int64_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);

		v_out  = (int64_t *) args->iov_extra.iov_buf;
		*v_out = left - right;

		args->iov_extra.iov_len = sizeof(int64_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_sub_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	float   *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);

		v_out  = (float *) args->iov_extra.iov_buf;
		*v_out = left - right;

		args->iov_extra.iov_len = sizeof(float);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_sub_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	double  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);

		v_out  = (double *) args->iov_extra.iov_buf;
		*v_out = left - right;

		args->iov_extra.iov_len = sizeof(double);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_mul_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int8_t  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);

		v_out  = (int8_t *) args->iov_extra.iov_buf;
		*v_out = left * right;

		args->iov_extra.iov_len = sizeof(int8_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_mul_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t left;
	int16_t right;
	int16_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);

		v_out  = (int16_t *) args->iov_extra.iov_buf;
		*v_out = left * right;

		args->iov_extra.iov_len = sizeof(int16_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_mul_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t left;
	int32_t right;
	int32_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);

		v_out  = (int32_t *) args->iov_extra.iov_buf;
		*v_out = left * right;

		args->iov_extra.iov_len = sizeof(int32_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_mul_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t left;
	int64_t right;
	int64_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);

		v_out  = (int64_t *) args->iov_extra.iov_buf;
		*v_out = left * right;

		args->iov_extra.iov_len = sizeof(int64_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_mul_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	float   *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);

		v_out  = (float *) args->iov_extra.iov_buf;
		*v_out = left * right;

		args->iov_extra.iov_len = sizeof(float);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_mul_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	double  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);

		v_out  = (double *) args->iov_extra.iov_buf;
		*v_out = left * right;

		args->iov_extra.iov_len = sizeof(double);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_div_i1(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int8_t  left;
	int8_t  right;
	int8_t  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int8_t *) left_iov->iov_buf);
		right = *((int8_t *) right_iov->iov_buf);
		if (unlikely(right == 0))
		{
			return -DER_DIV_BY_ZERO;
		}

		v_out  = (int8_t *) args->iov_extra.iov_buf;
		*v_out = left / right;

		args->iov_extra.iov_len = sizeof(int8_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_div_i2(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int16_t left;
	int16_t right;
	int16_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int16_t *) left_iov->iov_buf);
		right = *((int16_t *) right_iov->iov_buf);
		if (unlikely(right == 0))
		{
			return -DER_DIV_BY_ZERO;
		}

		v_out  = (int16_t *) args->iov_extra.iov_buf;
		*v_out = left / right;

		args->iov_extra.iov_len = sizeof(int16_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_div_i4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int32_t left;
	int32_t right;
	int32_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int32_t *) left_iov->iov_buf);
		right = *((int32_t *) right_iov->iov_buf);
		if (unlikely(right == 0))
		{
			return -DER_DIV_BY_ZERO;
		}

		v_out  = (int32_t *) args->iov_extra.iov_buf;
		*v_out = left / right;

		args->iov_extra.iov_len = sizeof(int32_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_div_i8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	int64_t left;
	int64_t right;
	int64_t *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((int64_t *) left_iov->iov_buf);
		right = *((int64_t *) right_iov->iov_buf);
		if (unlikely(right == 0))
		{
			return -DER_DIV_BY_ZERO;
		}

		v_out  = (int64_t *) args->iov_extra.iov_buf;
		*v_out = left / right;

		args->iov_extra.iov_len = sizeof(int64_t);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_div_r4(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	float   left;
	float   right;
	float   *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((float *) left_iov->iov_buf);
		right = *((float *) right_iov->iov_buf);
		if (unlikely(right == 0.0))
		{
			return -DER_DIV_BY_ZERO;
		}

		v_out  = (float *) args->iov_extra.iov_buf;
		*v_out = left / right;

		args->iov_extra.iov_len = sizeof(float);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

int
filter_func_div_r8(struct filter_part_run_t *args)
{
	d_iov_t *left_iov;
	d_iov_t *right_iov;
	double  left;
	double  right;
	double  *v_out;
	int     rc;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	left_iov = args->iov_out;

	args->part_idx += 1;
	rc = args->parts[args->part_idx].filter_func(args);
	if (unlikely(rc != 0))
	{
		return rc;
	}
	right_iov = args->iov_out;

	if (left_iov == NULL || right_iov == NULL)
	{
		args->iov_out = NULL;
	}
	else
	{
		left = *((double *) left_iov->iov_buf);
		right = *((double *) right_iov->iov_buf);
		if (unlikely(right == 0.0))
		{
			return -DER_DIV_BY_ZERO;
		}

		v_out  = (double *) args->iov_extra.iov_buf;
		*v_out = left / right;

		args->iov_extra.iov_len = sizeof(double);
		args->iov_out           = &args->iov_extra;
	}
	return 0;
}

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
