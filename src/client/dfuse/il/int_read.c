/**
 * (C) Copyright 2017-2020 Intel Corporation.
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

#include "ioil.h"

static ssize_t
read_bulk(char *buff, size_t len, off_t position,
	  struct fd_entry *entry, int *errcode)
{
	daos_size_t		read_size = 0;
	d_iov_t			iov = {};
	d_sg_list_t		sgl = {};
	int rc;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "%#zx-%#zx",
			position, position + len - 1);

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;
	rc = dfs_read(entry->fd_cont->ioc_dfs, entry->fd_dfsoh, &sgl,
		      position,	&read_size, NULL);
	if (rc) {
		DFUSE_TRA_DEBUG(entry->fd_dfsoh, "dfs_read() failed: %d", rc);
		*errcode = rc;
		return -1;
	}
	return read_size;
}

ssize_t ioil_do_pread(char *buff, size_t len, off_t position,
		      struct fd_entry *entry, int *errcode)
{
	return read_bulk(buff, len, position, entry, errcode);
}

ssize_t
ioil_do_preadv(const struct iovec *iov, int count, off_t position,
	       struct fd_entry *entry, int *errcode)
{
	ssize_t bytes_read;
	ssize_t total_read = 0;
	int i;

	for (i = 0; i < count; i++) {
		bytes_read = read_bulk(iov[i].iov_base, iov[i].iov_len,
				       position, entry, errcode);

		if (bytes_read == -1)
			return (ssize_t)-1;

		if (bytes_read == 0)
			return total_read;

		position += bytes_read;
		total_read += bytes_read;
	}

	return total_read;
}
