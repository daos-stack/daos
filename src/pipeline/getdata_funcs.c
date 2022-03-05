
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include "pipeline_internal.h"

#define getdata_func_dkey(typename, size, typec, outtypec)\
int getdata_func_dkey_##typename##size(struct filter_part_run_t *args)\
{\
	char   *buf;\
	size_t offset;\
\
	args->iov_out                = args->dkey;\
	buf                          = (char *) args->dkey->iov_buf;\
	offset                       = args->parts[args->part_idx].data_offset;\
	buf                          = &buf[offset];\
	args->value_##typename##_out = (_##outtypec) *((_##typec *) buf);\
	return 0;\
}

getdata_func_dkey(u, 1, uint8_t, uint64_t);
getdata_func_dkey(u, 2, uint16_t, uint64_t);
getdata_func_dkey(u, 4, uint32_t, uint64_t);
getdata_func_dkey(u, 8, uint64_t, uint64_t);
getdata_func_dkey(i, 1, int8_t, int64_t);
getdata_func_dkey(i, 2, int16_t, int64_t);
getdata_func_dkey(i, 4, int32_t, int64_t);
getdata_func_dkey(i, 8, int64_t, int64_t);
getdata_func_dkey(r, 4, float, double);
getdata_func_dkey(r, 8, double, double);

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

#define getdata_func_akey(typename, size, typec, outtypec)\
int getdata_func_akey_##typename##size(struct filter_part_run_t *args)\
{\
	char   *buf;\
	size_t offset;\
\
	getdata_func_akey_(args);\
	if (args->iov_out != NULL)\
	{\
		buf                        = (char *) args->iov_out->iov_buf;\
		offset                     = args->parts[args->part_idx].data_offset;\
		buf                        = &buf[offset];\
		args->value_##typename##_out = (_##outtypec) *((_##typec *) buf);\
	}\
	return 0;\
}

getdata_func_akey(u, 1, uint8_t, uint64_t);
getdata_func_akey(u, 2, uint16_t, uint64_t);;
getdata_func_akey(u, 4, uint32_t, uint64_t);
getdata_func_akey(u, 8, uint64_t, uint64_t);
getdata_func_akey(i, 1, int8_t, int64_t);
getdata_func_akey(i, 2, int16_t, int64_t);;
getdata_func_akey(i, 4, int32_t, int64_t);
getdata_func_akey(i, 8, int64_t, int64_t);
getdata_func_akey(r, 4, float, double);
getdata_func_akey(r, 8, double, double);

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

#define getdata_func_const(typename, size, typec, outtypec)\
int getdata_func_const_##typename##size(struct filter_part_run_t *args)\
{\
	args->iov_out              = args->parts[args->part_idx].iov;\
	args->value_##typename##_out =\
			(_##outtypec) *((_##typec *) args->iov_out->iov_buf);\
	return 0;\
}

getdata_func_const(u, 1, uint8_t, uint64_t);
getdata_func_const(u, 2, uint16_t, uint64_t);
getdata_func_const(u, 4, uint32_t, uint64_t);
getdata_func_const(u, 8, uint64_t, uint64_t);
getdata_func_const(i, 1, int8_t, int64_t);
getdata_func_const(i, 2, int16_t, int64_t);
getdata_func_const(i, 4, int32_t, int64_t);
getdata_func_const(i, 8, int64_t, int64_t);
getdata_func_const(r, 4, float, double);
getdata_func_const(r, 8, double, double);

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
