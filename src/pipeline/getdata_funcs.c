
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include "pipeline_internal.h"

#define getdata_func_dkey(typename, typec)\
int getdata_func_dkey_##typename(struct filter_part_run_t *args)\
{\
	char   *buf;\
	size_t offset;\
	double *double_dkey;\
	args->iov_out = args->dkey;\
	double_dkey   = &args->value_out;\
	buf           = (char *) args->dkey->iov_buf;\
	offset        = args->parts[args->part_idx].data_offset;\
	buf           = &buf[offset];\
	*double_dkey = (double) *((_##typec *) buf);\
	return 0;\
}

getdata_func_dkey(i1, int8_t);
getdata_func_dkey(i2, int16_t);
getdata_func_dkey(i4, int32_t);
getdata_func_dkey(i8, int64_t);
getdata_func_dkey(r4, float);
getdata_func_dkey(r8, double);

int getdata_func_dkey_raw(struct filter_part_run_t *args)
{
	args->iov_out = args->dkey;
	args->data_offset_out = args->parts[args->part_idx].data_offset;
	args->data_len_out    = args->parts[args->part_idx].data_len;	
	return 0;
}

int getdata_func_dkey_st(struct filter_part_run_t *args)
{
	char   *buf;
	size_t offset;
	size_t len;

	args->iov_out = args->dkey;
	buf    = (char *) args->iov_out->iov_buf;
	offset = args->parts[args->part_idx].data_offset;
	len    = *((size_t *) &buf[offset]);

	if (len > args->parts[args->part_idx].data_len)
	{
		return 1;
	}
	args->data_offset_out = offset + sizeof(size_t);
	args->data_len_out    = len;

	return 0;
}

int getdata_func_dkey_cst(struct filter_part_run_t *args)
{
	char   *buf;
	size_t offset;
	size_t len;

	args->iov_out = args->dkey;
	buf    = (char *) args->iov_out->iov_buf;
	offset = args->parts[args->part_idx].data_offset;
	buf    = &buf[offset];
	len = strlen(buf);

	if (len > args->parts[args->part_idx].data_len)
	{
		return 1;
	}
	args->data_offset_out = offset;
	args->data_len_out    = len;

	return 0;
}

static void
getdata_func_akey_(struct filter_part_run_t *args)
{
	char		*akey_name_str;
	size_t		akey_name_size;
	char		*iod_str;
	size_t		iod_size;
	uint32_t	i;

	akey_name_str  = (char *) args->parts[args->part_idx].iov->iov_buf;
	akey_name_size = args->parts[args->part_idx].iov->iov_len;
	args->iov_out = NULL;

	for (i = 0; i < args->nr_iods; i++)
	{
		iod_str  = (char *) args->iods[i].iod_name.iov_buf;
		iod_size = args->iods[i].iod_name.iov_len;

		if (iod_size != akey_name_size)
		{
			continue;
		}
		/** akey exists and has data */
		if (!memcmp(akey_name_str, iod_str, iod_size) &&
			args->akeys[i].sg_iovs->iov_len > 0)
		{
			args->iov_out = args->akeys[i].sg_iovs;
			break;
		}
	}
}

#define getdata_func_akey(typename, typec)\
int getdata_func_akey_##typename(struct filter_part_run_t *args)\
{\
	char   *buf;\
	size_t offset;\
	double *double_akey;\
\
	getdata_func_akey_(args);\
	if (args->iov_out != NULL)\
	{\
		double_akey  = &args->value_out;\
		buf           = (char *) args->iov_out->iov_buf;\
		offset        = args->parts[args->part_idx].data_offset;\
		buf           = &buf[offset];\
		*double_akey = (double) *((_##typec *) buf);\
	}\
	return 0;\
}

getdata_func_akey(i1, int8_t);
getdata_func_akey(i2, int16_t);
getdata_func_akey(i4, int32_t);
getdata_func_akey(i8, int64_t);
getdata_func_akey(r4, float);
getdata_func_akey(r8, double);

int getdata_func_akey_raw(struct filter_part_run_t *args)
{
	getdata_func_akey_(args);
	if (args->iov_out != NULL)
	{
		args->data_offset_out = args->parts[args->part_idx].data_offset;
		args->data_len_out    = args->parts[args->part_idx].data_len;
	}
	return 0;
}

int getdata_func_akey_st(struct filter_part_run_t *args)
{
	char   *buf;
	size_t offset;
	size_t len;

	getdata_func_akey_(args);
	if (args->iov_out != NULL)
	{
		buf    = (char *) args->iov_out->iov_buf;
		offset = args->parts[args->part_idx].data_offset;
		len    = *((size_t *) &buf[offset]);

		if (len > args->parts[args->part_idx].data_len)
		{
			return 1;
		}
		args->data_offset_out = offset + sizeof(size_t);
		args->data_len_out    = len;
	}
	return 0;
}

int getdata_func_akey_cst(struct filter_part_run_t *args)
{
	char   *buf;
	size_t offset;
	size_t len;

	getdata_func_akey_(args);
	if (args->iov_out != NULL)
	{
		buf    = (char *) args->iov_out->iov_buf;
		offset = args->parts[args->part_idx].data_offset;
		buf    = &buf[offset];
		len = strlen(buf);

		if (len > args->parts[args->part_idx].data_len)
		{
			return 1;
		}
		args->data_offset_out = offset;
		args->data_len_out    = len;
	}
	return 0;
}

#define getdata_func_const(typename, typec)\
int getdata_func_const_##typename(struct filter_part_run_t *args)\
{\
	double *double_const;\
	args->iov_out = args->parts[args->part_idx].iov;\
	double_const  = &args->value_out;\
	*double_const = (double) *((_##typec *) args->iov_out->iov_buf);\
	return 0;\
}

getdata_func_const(i1, int8_t);
getdata_func_const(i2, int16_t);
getdata_func_const(i4, int32_t);
getdata_func_const(i8, int64_t);
getdata_func_const(r4, float);
getdata_func_const(r8, double);

int getdata_func_const_raw(struct filter_part_run_t *args)
{
	args->iov_out = args->parts[args->part_idx].iov;
	args->data_offset_out = args->parts[args->part_idx].data_offset;
	args->data_len_out    = args->parts[args->part_idx].data_len;
	return 0;
}

int getdata_func_const_st(struct filter_part_run_t *args)
{
	char   *buf;
	size_t offset;
	size_t len;

	args->iov_out = args->parts[args->part_idx].iov;
	buf           = (char *) args->parts[args->part_idx].iov->iov_buf;
	offset        = args->parts[args->part_idx].data_offset;
	len           = *((size_t *) &buf[offset]);

	if (len > args->parts[args->part_idx].data_len)
	{
		return 1;
	}
	args->data_offset_out = offset + sizeof(size_t);
	args->data_len_out    = len;
	return 0;
}

int getdata_func_const_cst(struct filter_part_run_t *args)
{
	char   *buf;
	size_t offset;
	size_t len;

	args->iov_out = args->parts[args->part_idx].iov;
	buf           = (char *) args->parts[args->part_idx].iov->iov_buf;
	offset        = args->parts[args->part_idx].data_offset;
	buf           = &buf[offset];
	len           = strlen(buf);

	if (len > args->parts[args->part_idx].data_len)
	{
		return 1;
	}
	args->data_offset_out = offset;
	args->data_len_out    = len;
	return 0;
}
