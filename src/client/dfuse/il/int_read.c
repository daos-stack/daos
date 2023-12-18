/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(il)

#include <sys/ioctl.h>

#include "dfuse_common.h"
#include "intercept.h"
#include "daos.h"
#include "daos_array.h"

#include "dfuse_ioctl.h"

#include "ioil.h"

static ssize_t
read_bulk(int fd, char *buff, size_t len, off_t position, struct fd_entry *entry, int *errcode)
{
	daos_size_t	read_size = 0;
	d_iov_t		iov       = {};
	d_sg_list_t	sgl       = {};
	daos_event_t	ev;
	daos_handle_t	eqh;
	int		rc;
	struct dfuse_io_vec diov = {};

	diov.base     = buff;
	diov.len      = len;
	diov.position = position;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "%#zx-%#zx", position, position + len - 1);

	rc = ioctl(fd, DFUSE_IOCTL_READ, &diov);
	if (rc == -1) {
		DS_ERROR(errno, "Error from ioctl on fd %d", fd);
		*errcode = errno;
		return -1;
	}
	return diov.len;

#if 0

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;

	rc = ioil_get_eqh(&eqh);
	if (rc == 0) {
		bool	flag = false;

		rc = daos_event_init(&ev, eqh, NULL);
		if (rc) {
			DFUSE_TRA_ERROR(entry->fd_dfsoh, "daos_event_init() failed: "DF_RC,
					DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		rc = dfs_read(entry->fd_cont->ioc_dfs, entry->fd_dfsoh, &sgl, position,
			      &read_size, &ev);
		if (rc)
			D_GOTO(out, rc);

		while (1) {
			rc = daos_event_test(&ev, DAOS_EQ_NOWAIT, &flag);
			if (rc) {
				DFUSE_TRA_ERROR(entry->fd_dfsoh, "daos_event_test() failed: "DF_RC,
						DP_RC(rc));
				D_GOTO(out, rc = daos_der2errno(rc));
			}
			if (flag)
				break;
			sched_yield();
		}
		rc = ev.ev_error;
	} else {
		rc = dfs_read(entry->fd_cont->ioc_dfs, entry->fd_dfsoh, &sgl, position, &read_size,
			      NULL);
	}
out:
	if (rc) {
		DFUSE_TRA_ERROR(entry->fd_dfsoh, "dfs_read() failed: %d (%s)", rc, strerror(rc));
		*errcode = rc;
		return -1;
	}
	return read_size;
#endif
}

ssize_t
ioil_do_pread(int fd, char *buff, size_t len, off_t position, struct fd_entry *entry, int *errcode)
{
	return read_bulk(fd, buff, len, position, entry, errcode);
}

ssize_t
ioil_do_preadv(int fd, const struct iovec *iov, int count, off_t position, struct fd_entry *entry,
	       int *errcode)
{
	ssize_t bytes_read;
	ssize_t total_read = 0;
	int     i;

	for (i = 0; i < count; i++) {
		bytes_read =
		    read_bulk(fd, iov[i].iov_base, iov[i].iov_len, position, entry, errcode);

		read_bulk(fd, iov[i].iov_base, iov[i].iov_len, position, entry, errcode);

		if (bytes_read == 0)
			return total_read;

		position += bytes_read;
		total_read += bytes_read;
	}

	return total_read;
}
