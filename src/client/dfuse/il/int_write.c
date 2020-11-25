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

ssize_t
ioil_do_writex(const char *buff, size_t len, off_t position,
	       struct fd_entry *entry, int *errcode)
{
	d_iov_t			iov = {};
	d_sg_list_t		sgl = {};
	int rc;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "%#zx-%#zx",
			position, position + len - 1);

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;

	rc = dfs_write(entry->fd_cont->ioc_dfs,
		       entry->fd_dfsoh, &sgl, position, NULL);
	if (rc) {
		DFUSE_TRA_DEBUG(entry->fd_dfsoh, "dfs_write() failed: %d", rc);
		*errcode = rc;
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
