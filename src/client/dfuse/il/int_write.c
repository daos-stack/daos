/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(il)

#include <daos.h>
#include <daos_array.h>

#include "dfuse_common.h"
#include "intercept.h"

#include "ioil.h"

static ssize_t
ioil_do_writesgl(d_sg_list_t *sgl, size_t len, off_t position, struct fd_entry *entry, int *errcode)
{
	daos_event_t	ev;
	daos_handle_t	eqh;
	int		rc;

	DFUSE_TRA_DEBUG(entry->fd_dfsoh, "%#zx-%#zx", position, position + len - 1);

	rc = ioil_get_eqh(&eqh);
	if (rc == 0) {
		bool	flag = false;

		rc = daos_event_init(&ev, eqh, NULL);
		if (rc) {
			DFUSE_TRA_ERROR(entry->fd_dfsoh, "daos_event_init() failed: "DF_RC,
					DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		rc = dfs_write(entry->fd_cont->ioc_dfs, entry->fd_dfsoh, sgl, position, &ev);
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
		rc = dfs_write(entry->fd_cont->ioc_dfs, entry->fd_dfsoh, sgl, position, NULL);
	}
out:
	if (rc) {
		DFUSE_TRA_ERROR(entry->fd_dfsoh, "dfs_write() failed: %d (%s)", rc, strerror(rc));
		*errcode = rc;
		return -1;
	}
	return len;
}

ssize_t
ioil_do_writex(const char *buff, size_t len, off_t position, struct fd_entry *entry, int *errcode)
{
	d_iov_t     iov = {};
	d_sg_list_t sgl = {};

	sgl.sg_nr = 1;
	d_iov_set(&iov, (void *)buff, len);
	sgl.sg_iovs = &iov;

	return ioil_do_writesgl(&sgl, len, position, entry, errcode);
}

ssize_t
ioil_do_pwritev(const struct iovec *iov, int count, off_t position, struct fd_entry *entry,
		int *errcode)
{
	d_iov_t    *diov;
	d_sg_list_t sgl         = {};
	size_t      total_write = 0;
	int         i;
	int         rc;
	int         new_count;

	D_ALLOC_ARRAY(diov, count);
	if (diov == NULL) {
		*errcode = ENOMEM;
		return -1;
	}

	for (i = 0, new_count = 0; i < count; i++) {
		/** See DAOS-15089. This is a workaround */
		if (iov[i].iov_len == 0)
			continue;
		d_iov_set(&diov[new_count++], iov[i].iov_base, iov[i].iov_len);
		total_write += iov[i].iov_len;
	}

	sgl.sg_nr   = new_count;
	sgl.sg_iovs = diov;

	rc = ioil_do_writesgl(&sgl, total_write, position, entry, errcode);

	D_FREE(diov);

	return rc;
}
