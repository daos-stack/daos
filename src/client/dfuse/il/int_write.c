/**
 * (C) Copyright 2017-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#define D_LOGFAC DD_FAC(il)
#include "dfuse_common.h"
#include "intercept.h"
#include "daos.h"
#include "daos_array.h"

ssize_t
ioil_do_writex(const char *buff, size_t len, off_t position,
	       struct fd_entry *entry, int *errcode)
{
	daos_array_iod_t	iod;
	daos_range_t		rg;
	d_iov_t			iov = {};
	d_sg_list_t		sgl = {};
	int rc;

	DFUSE_TRA_INFO(entry, "%#zx-%#zx ", position, position + len - 1);

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;

	iod.arr_nr = 1;
	rg.rg_len = len;
	rg.rg_idx = position;
	iod.arr_rgs = &rg;

	rc = daos_array_write(entry->fd_aoh, DAOS_TX_NONE, &iod, &sgl, NULL,
			      NULL);
	if (rc) {
		DFUSE_TRA_INFO(entry, "daos_array_write() failed %d", rc);
		*errcode = daos_der2errno(rc);
		return -1;
	}

	return len;
}

ssize_t
ioil_do_pwritev(const struct iovec *iov, int count, off_t position,
		struct fd_entry *entry, int *errcode)
{
	ssize_t bytes_written;
	ssize_t total_write = 0;
	int i;

	for (i = 0; i < count; i++) {
		bytes_written = ioil_do_writex(iov[i].iov_base, iov[i].iov_len,
					       position, entry, errcode);

		if (bytes_written == -1)
			return (ssize_t)-1;

		if (bytes_written == 0)
			return total_write;

		position += bytes_written;
		total_write += bytes_written;
	}

	return total_write;
}
