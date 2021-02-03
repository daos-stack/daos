/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
