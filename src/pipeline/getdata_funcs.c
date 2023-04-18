
/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_internal.h"

/**
 * getdata functions for dkeys, akeys, and constants.
 */

#define DEFINE_GETDATA_FUNC_DKEY(typename, size, typec, outtypename, outtypec)                     \
	int getdata_func_dkey_##typename##size(struct filter_part_run_t *args)                     \
	{                                                                                          \
		char  *buf;                                                                        \
		size_t offset;                                                                     \
		buf                             = (char *)args->dkey->iov_buf;                     \
		offset                          = args->parts[args->part_idx].data_offset;         \
		buf                             = &buf[offset];                                    \
		args->value_##outtypename##_out = (_##outtypec) * ((_##typec *)buf);               \
		args->data_out                  = buf;                                             \
		return 0;                                                                          \
	}

DEFINE_GETDATA_FUNC_DKEY(u, 1, uint8_t, u, uint64_t)
DEFINE_GETDATA_FUNC_DKEY(u, 2, uint16_t, u, uint64_t)
DEFINE_GETDATA_FUNC_DKEY(u, 4, uint32_t, u, uint64_t)
DEFINE_GETDATA_FUNC_DKEY(u, 8, uint64_t, u, uint64_t)
DEFINE_GETDATA_FUNC_DKEY(i, 1, int8_t, i, int64_t)
DEFINE_GETDATA_FUNC_DKEY(i, 2, int16_t, i, int64_t)
DEFINE_GETDATA_FUNC_DKEY(i, 4, int32_t, i, int64_t)
DEFINE_GETDATA_FUNC_DKEY(i, 8, int64_t, i, int64_t)
DEFINE_GETDATA_FUNC_DKEY(r, 4, float, d, double)
DEFINE_GETDATA_FUNC_DKEY(r, 8, double, d, double)

int
getdata_func_dkey_raw(struct filter_part_run_t *args)
{
	char  *buf;
	size_t offset;

	buf                = (char *)args->dkey->iov_buf;
	offset             = args->parts[args->part_idx].data_offset;
	args->data_out     = &buf[offset];
	args->data_len_out = args->parts[args->part_idx].data_len;
	if (args->dkey->iov_len < offset + args->data_len_out)
		args->data_len_out = args->dkey->iov_len - offset;

	return 0;
}

int
getdata_func_dkey_st(struct filter_part_run_t *args)
{
	char  *buf;
	size_t offset;
	size_t len;

	buf    = (char *)args->dkey->iov_buf;
	offset = args->parts[args->part_idx].data_offset;
	len    = *((size_t *)&buf[offset]);

	if (offset + sizeof(size_t) + len > args->dkey->iov_len)
		len = args->dkey->iov_len - offset - sizeof(size_t);

	args->data_out     = &buf[offset + sizeof(size_t)];
	args->data_len_out = len;

	return 0;
}

int
getdata_func_dkey_cst(struct filter_part_run_t *args)
{
	char  *buf;
	size_t offset;
	size_t len;

	buf    = (char *)args->dkey->iov_buf;
	offset = args->parts[args->part_idx].data_offset;
	buf    = &buf[offset];
	len    = strlen(buf);

	if (offset + len > args->dkey->iov_len)
		len = args->dkey->iov_len - offset;

	args->data_out     = buf;
	args->data_len_out = len;

	return 0;
}

static void
getdata_func_akey_(struct filter_part_run_t *args)
{
	char        *akey_name_str;
	size_t       akey_name_size;
	daos_iod_t  *iod;
	d_iov_t     *akey;
	/*daos_iom_t  *iom;*/
	daos_recx_t *recx;
	char        *iod_name_str;
	size_t       iod_name_size;
	uint32_t     i, j;
	char        *buf;
	size_t       target_offset;
	size_t       offset;
	size_t       len;

	akey_name_str  = (char *)args->parts[args->part_idx].iov->iov_buf;
	akey_name_size = args->parts[args->part_idx].iov->iov_len;
	target_offset  = args->parts[args->part_idx].data_offset;
	len            = args->parts[args->part_idx].data_len;
	buf            = NULL;

	for (i = 0; i < args->nr_iods; i++) {
		iod           = &args->iods[i];
		iod_name_str  = (char *)iod->iod_name.iov_buf;
		iod_name_size = iod->iod_name.iov_len;
		if (iod_name_size != akey_name_size)
			continue;

		akey = args->akeys[i].sg_iovs;
		/** akey exists and has data */
		if (!memcmp(akey_name_str, iod_name_str, iod_name_size) &&
		    akey->iov_len > 0) {
			if (iod->iod_type == DAOS_IOD_SINGLE) {
				buf = (char *)akey->iov_buf;
				buf = &buf[target_offset];
				if (target_offset + len > iod->iod_size)
					len = iod->iod_size - target_offset;

				D_GOTO(exit, buf);
			}
			/** DAOS_IOD_ARRAY */
			/*iom = &args->ioms[i];*/

			offset = 0;
			/*for (j = 0; j < iom->iom_nr_out; j++)*/
			for (j = 0; j < iod->iod_nr; j++) {
				/*recx = &iom->iom_recxs[j];*/
				recx = &iod->iod_recxs[j];

				if ((target_offset < recx->rx_idx + recx->rx_nr) &&
				    target_offset >= recx->rx_idx) { /** extend found */
					buf = (char *)akey->iov_buf;
					buf = &buf[offset];
					if (iod->iod_size * recx->rx_nr < len)
						len = (size_t)recx->rx_nr * iod->iod_size;

					D_GOTO(exit, buf);
				}
				offset += (size_t)recx->rx_nr * iod->iod_size;
			}
			D_GOTO(exit, buf);
			/**
			 * Even if extent is not found we return, since there are not two akeys
			 * with the same name (i.e., key value)
			 */
		}
	}
exit:
	args->data_out     = buf;
	args->data_len_out = len;
}

#define DEFINE_GETDATA_FUNC_AKEY(typename, size, typec, outtypename, outtypec)                     \
	int getdata_func_akey_##typename##size(struct filter_part_run_t *args)                     \
	{                                                                                          \
		char *buf;                                                                         \
		getdata_func_akey_(args);                                                          \
		if (args->data_out != NULL && args->data_len_out >= sizeof(_##typec)) {            \
			buf                             = args->data_out;                          \
			args->value_##outtypename##_out = (_##outtypec) * ((_##typec *)buf);       \
		}                                                                                  \
		return 0;                                                                          \
	}

DEFINE_GETDATA_FUNC_AKEY(u, 1, uint8_t, u, uint64_t)
DEFINE_GETDATA_FUNC_AKEY(u, 2, uint16_t, u, uint64_t)
DEFINE_GETDATA_FUNC_AKEY(u, 4, uint32_t, u, uint64_t)
DEFINE_GETDATA_FUNC_AKEY(u, 8, uint64_t, u, uint64_t)
DEFINE_GETDATA_FUNC_AKEY(i, 1, int8_t, i, int64_t)
DEFINE_GETDATA_FUNC_AKEY(i, 2, int16_t, i, int64_t)
DEFINE_GETDATA_FUNC_AKEY(i, 4, int32_t, i, int64_t)
DEFINE_GETDATA_FUNC_AKEY(i, 8, int64_t, i, int64_t)
DEFINE_GETDATA_FUNC_AKEY(r, 4, float, d, double)
DEFINE_GETDATA_FUNC_AKEY(r, 8, double, d, double)

int
getdata_func_akey_raw(struct filter_part_run_t *args)
{
	getdata_func_akey_(args);
	return 0;
}

int
getdata_func_akey_st(struct filter_part_run_t *args)
{
	char  *buf;
	size_t len;

	getdata_func_akey_(args);
	if (args->data_out != NULL) {
		buf = (char *)args->data_out;
		len = *((size_t *)buf);

		if (len + sizeof(size_t) > args->data_len_out)
			len = args->data_len_out - sizeof(size_t);

		args->data_out     = &buf[sizeof(size_t)];
		args->data_len_out = len;
	}
	return 0;
}

int
getdata_func_akey_cst(struct filter_part_run_t *args)
{
	char  *buf;
	size_t len;

	getdata_func_akey_(args);
	if (args->data_out != NULL) {
		buf = (char *)args->data_out;
		len = strlen(buf);

		if (len < args->data_len_out)
			args->data_len_out = len;
	}
	return 0;
}

#define DEFINE_GETDATA_FUNC_CONST(typename, size, typec, outtypename, outtypec)                    \
	int getdata_func_const_##typename##size(struct filter_part_run_t *args)                    \
	{                                                                                          \
		args->data_out                  = (char *)args->parts[args->part_idx].iov->iov_buf;\
		args->value_##outtypename##_out = (_##outtypec) * ((_##typec *)args->data_out);    \
		return 0;                                                                          \
	}

DEFINE_GETDATA_FUNC_CONST(u, 1, uint8_t, u, uint64_t)
DEFINE_GETDATA_FUNC_CONST(u, 2, uint16_t, u, uint64_t)
DEFINE_GETDATA_FUNC_CONST(u, 4, uint32_t, u, uint64_t)
DEFINE_GETDATA_FUNC_CONST(u, 8, uint64_t, u, uint64_t)
DEFINE_GETDATA_FUNC_CONST(i, 1, int8_t, i, int64_t)
DEFINE_GETDATA_FUNC_CONST(i, 2, int16_t, i, int64_t)
DEFINE_GETDATA_FUNC_CONST(i, 4, int32_t, i, int64_t)
DEFINE_GETDATA_FUNC_CONST(i, 8, int64_t, i, int64_t)
DEFINE_GETDATA_FUNC_CONST(r, 4, float, d, double)
DEFINE_GETDATA_FUNC_CONST(r, 8, double, d, double)

int
getdata_func_const_raw(struct filter_part_run_t *args)
{
	char *buf;

	buf                = (char *)args->parts[args->part_idx].iov->iov_buf;
	args->data_len_out = args->parts[args->part_idx].iov->iov_len;
	args->data_out     = buf;
	return 0;
}

int
getdata_func_const_st(struct filter_part_run_t *args)
{
	char  *buf;
	size_t len;

	buf = (char *)args->parts[args->part_idx].iov->iov_buf;
	len = *((size_t *)buf);

	if (len + sizeof(size_t) > args->parts[args->part_idx].iov->iov_len)
		len = args->parts[args->part_idx].iov->iov_len - sizeof(size_t);

	args->data_out     = &buf[sizeof(size_t)];
	args->data_len_out = len;
	return 0;
}

int
getdata_func_const_cst(struct filter_part_run_t *args)
{
	char  *buf;
	size_t len;

	buf = (char *)args->parts[args->part_idx].iov->iov_buf;
	len = strlen(buf);

	if (len > args->parts[args->part_idx].iov->iov_len)
		len = args->parts[args->part_idx].iov->iov_len;

	args->data_out     = buf;
	args->data_len_out = len;
	return 0;
}
