/**
 * (C) Copyright 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS read & write ops */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/array.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos/object.h>

#include "dfs_internal.h"

static void
dfs_update_file_metrics(dfs_t *dfs, daos_size_t read_bytes, daos_size_t write_bytes)
{
	if (dfs == NULL || dfs->metrics == NULL)
		return;

	if (read_bytes > 0)
		d_tm_inc_gauge(dfs->metrics->dm_read_bytes, read_bytes);
	if (write_bytes > 0)
		d_tm_inc_gauge(dfs->metrics->dm_write_bytes, write_bytes);
}

struct dfs_read_params {
	dfs_t           *dfs;
	daos_size_t     *read_size;
	daos_array_iod_t arr_iod;
	daos_range_t     rg;
};

static int
read_cb(tse_task_t *task, void *data)
{
	struct dfs_read_params *params;
	int                     rc = task->dt_result;

	params = daos_task_get_priv(task);
	D_ASSERT(params != NULL);

	if (rc != 0) {
		D_ERROR("Failed to read from array object: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	DFS_OP_STAT_INCR(params->dfs, DOS_READ);
	dfs_update_file_metrics(params->dfs, params->arr_iod.arr_nr_read, 0);
	*params->read_size = params->arr_iod.arr_nr_read;
out:
	D_FREE(params);
	return rc;
}

static int
dfs_read_int(dfs_t *dfs, dfs_obj_t *obj, daos_off_t off, dfs_iod_t *iod, d_sg_list_t *sgl,
	     daos_size_t buf_size, daos_size_t *read_size, daos_event_t *ev)
{
	tse_task_t             *task = NULL;
	daos_array_io_t        *args;
	struct dfs_read_params *params;
	int                     rc;

	D_ASSERT(ev);
	daos_event_errno_rc(ev);

	rc = dc_task_create(dc_array_read, NULL, ev, &task);
	if (rc != 0)
		return daos_der2errno(rc);

	D_ALLOC_PTR(params);
	if (params == NULL)
		D_GOTO(err_task, rc = -DER_NOMEM);

	params->dfs       = dfs;
	params->read_size = read_size;

	/** set array location */
	if (iod == NULL) {
		params->arr_iod.arr_nr  = 1;
		params->rg.rg_len       = buf_size;
		params->rg.rg_idx       = off;
		params->arr_iod.arr_rgs = &params->rg;
	} else {
		params->arr_iod.arr_nr  = iod->iod_nr;
		params->arr_iod.arr_rgs = iod->iod_rgs;
	}

	args      = dc_task_get_args(task);
	args->oh  = obj->oh;
	args->th  = dfs->th;
	args->sgl = sgl;
	args->iod = &params->arr_iod;

	daos_task_set_priv(task, params);
	rc = tse_task_register_cbs(task, NULL, NULL, 0, read_cb, NULL, 0);
	if (rc)
		D_GOTO(err_params, rc);

	/*
	 * dc_task_schedule() calls tse_task_complete() even on error (which also calls the
	 * completion cb that frees params in this case, so we can just ignore the rc here.
	 */
	dc_task_schedule(task, true);

	return 0;

err_params:
	D_FREE(params);
err_task:
	tse_task_complete(task, rc);
	/** the event is completed with the proper rc */
	return 0;
}

int
dfs_read(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off, daos_size_t *read_size,
	 daos_event_t *ev)
{
	daos_size_t buf_size;
	int         i, rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	buf_size = 0;
	for (i = 0; i < sgl->sg_nr; i++)
		buf_size += sgl->sg_iovs[i].iov_len;
	if (buf_size == 0) {
		*read_size = 0;
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_READ);
		return 0;
	}

	D_DEBUG(DB_TRACE, "DFS Read: Off %" PRIu64 ", Len %zu\n", off, buf_size);

	if (ev == NULL) {
		daos_array_iod_t iod;
		daos_range_t     rg;

		/** set array location */
		iod.arr_nr  = 1;
		rg.rg_len   = buf_size;
		rg.rg_idx   = off;
		iod.arr_rgs = &rg;

		rc = daos_array_read(obj->oh, dfs->th, &iod, sgl, NULL);
		if (rc) {
			D_ERROR("daos_array_read() failed, " DF_RC "\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		DFS_OP_STAT_INCR(dfs, DOS_READ);
		*read_size = iod.arr_nr_read;
		dfs_update_file_metrics(dfs, iod.arr_nr_read, 0);
		return 0;
	}

	return dfs_read_int(dfs, obj, off, NULL, sgl, buf_size, read_size, ev);
}

int
dfs_readx(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl, daos_size_t *read_size,
	  daos_event_t *ev)
{
	int rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if (read_size == NULL)
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_WRONLY)
		return EPERM;

	if (iod->iod_nr == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_READ);
		return 0;
	}

	if (ev == NULL) {
		daos_array_iod_t arr_iod;

		/** set array location */
		arr_iod.arr_nr  = iod->iod_nr;
		arr_iod.arr_rgs = iod->iod_rgs;

		rc = daos_array_read(obj->oh, dfs->th, &arr_iod, sgl, ev);
		if (rc) {
			D_ERROR("daos_array_read() failed (%d)\n", rc);
			return daos_der2errno(rc);
		}

		DFS_OP_STAT_INCR(dfs, DOS_READ);
		*read_size = arr_iod.arr_nr_read;
		dfs_update_file_metrics(dfs, arr_iod.arr_nr_read, 0);
		return 0;
	}

	return dfs_read_int(dfs, obj, 0, iod, sgl, 0, read_size, ev);
}

int
dfs_write(dfs_t *dfs, dfs_obj_t *obj, d_sg_list_t *sgl, daos_off_t off, daos_event_t *ev)
{
	daos_array_iod_t iod;
	daos_range_t     rg;
	daos_size_t      buf_size;
	int              i;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;

	buf_size = 0;
	if (sgl)
		for (i = 0; i < sgl->sg_nr; i++)
			buf_size += sgl->sg_iovs[i].iov_len;
	if (buf_size == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		return 0;
	}

	/** set array location */
	iod.arr_nr  = 1;
	rg.rg_len   = buf_size;
	rg.rg_idx   = off;
	iod.arr_rgs = &rg;

	D_DEBUG(DB_TRACE, "DFS Write: Off %" PRIu64 ", Len %zu\n", off, buf_size);

	if (ev)
		daos_event_errno_rc(ev);

	rc = daos_array_write(obj->oh, DAOS_TX_NONE, &iod, sgl, ev);
	if (rc == 0) {
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		dfs_update_file_metrics(dfs, 0, buf_size);
	} else {
		D_ERROR("daos_array_write() failed, " DF_RC "\n", DP_RC(rc));
	}

	return daos_der2errno(rc);
}

int
dfs_writex(dfs_t *dfs, dfs_obj_t *obj, dfs_iod_t *iod, d_sg_list_t *sgl, daos_event_t *ev)
{
	daos_array_iod_t arr_iod;
	daos_size_t      buf_size;
	int              i;
	int              rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (dfs->amode != O_RDWR)
		return EPERM;
	if (obj == NULL || !S_ISREG(obj->mode))
		return EINVAL;
	if ((obj->flags & O_ACCMODE) == O_RDONLY)
		return EPERM;
	if (iod == NULL)
		return EINVAL;

	if (iod->iod_nr == 0) {
		if (ev) {
			daos_event_launch(ev);
			daos_event_complete(ev, 0);
		}
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		return 0;
	}

	/** set array location */
	arr_iod.arr_nr  = iod->iod_nr;
	arr_iod.arr_rgs = iod->iod_rgs;

	if (ev)
		daos_event_errno_rc(ev);

	buf_size = 0;
	if (dfs->metrics != NULL && sgl != NULL)
		for (i = 0; i < sgl->sg_nr; i++)
			buf_size += sgl->sg_iovs[i].iov_len;

	rc = daos_array_write(obj->oh, DAOS_TX_NONE, &arr_iod, sgl, ev);
	if (rc == 0) {
		DFS_OP_STAT_INCR(dfs, DOS_WRITE);
		dfs_update_file_metrics(dfs, 0, buf_size);
	} else {
		D_ERROR("daos_array_write() failed (%d)\n", rc);
	}

	return daos_der2errno(rc);
}
