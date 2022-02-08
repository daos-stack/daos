
/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include "pipeline_internal.h"

int
getdata_func_akey(struct filter_part_run_t *args)
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
	return 0;
}

int
getdata_func_const(struct filter_part_run_t *args)
{
	args->iov_out = args->parts[args->part_idx].iov;
	return 0;
}

int
getdata_func_dkey(struct filter_part_run_t *args)
{
	args->iov_out = args->dkey;
	return 0;
}
