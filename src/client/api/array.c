/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * src/client/api/daos_array.c
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/array.h>
#include <daos_array.h>

int
daos_array_create(daos_handle_t coh, daos_obj_id_t oid, daos_handle_t th,
		  daos_size_t cell_size, daos_size_t chunk_size,
		  daos_handle_t *oh, daos_event_t *ev)
{
	daos_array_create_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_array_create, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	 = coh;
	args->oid	 = oid;
	args->th	 = th;
	args->cell_size	 = cell_size;
	args->chunk_size = chunk_size;
	args->oh	 = oh;

	return dc_task_schedule(task, true);
}

int
daos_array_open(daos_handle_t coh, daos_obj_id_t oid, daos_handle_t th,
		unsigned int mode, daos_size_t *cell_size,
		daos_size_t *chunk_size, daos_handle_t *oh, daos_event_t *ev)
{
	daos_array_open_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_array_open, NULL, ev, &task);
	if (rc)
		return rc;

	*cell_size	 = 0;
	*chunk_size	 = 0;

	args = dc_task_get_args(task);
	args->coh	 = coh;
	args->oid	 = oid;
	args->th	 = th;
	args->mode	 = mode;
	args->open_with_attr = 0;
	args->cell_size	 = cell_size;
	args->chunk_size = chunk_size;
	args->oh	 = oh;

	return dc_task_schedule(task, true);
}

int
daos_array_open_with_attr(daos_handle_t coh, daos_obj_id_t oid,
			  daos_handle_t th, unsigned int mode,
			  daos_size_t cell_size, daos_size_t chunk_size,
			  daos_handle_t *oh, daos_event_t *ev)
{
	daos_array_open_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_array_open, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh		= coh;
	args->oid		= oid;
	args->th		= th;
	args->mode		= mode;
	args->open_with_attr	= 1;
	args->cell_size		= &cell_size;
	args->chunk_size	= &chunk_size;
	args->oh		= oh;

	return dc_task_schedule(task, true);
}

int
daos_array_local2global(daos_handle_t oh, d_iov_t *glob)
{
	return dc_array_local2global(oh, glob);
}

int
daos_array_global2local(daos_handle_t coh, d_iov_t glob, unsigned int mode,
			daos_handle_t *oh)
{
	return dc_array_global2local(coh, glob, mode, oh);
}

int
daos_array_close(daos_handle_t oh, daos_event_t *ev)
{
	daos_array_close_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (ev == NULL)
		return dc_array_close_direct(oh);

	rc = dc_task_create(dc_array_close, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh = oh;

	return dc_task_schedule(task, true);
}

int
daos_array_destroy(daos_handle_t oh, daos_handle_t th, daos_event_t *ev)
{
	daos_array_destroy_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_array_destroy, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;

	return dc_task_schedule(task, true);
}

int
daos_array_get_attr(daos_handle_t oh, daos_size_t *chunk_size,
		    daos_size_t *cell_size)
{
	return dc_array_get_attr(oh, chunk_size, cell_size);
}

int
daos_array_update_chunk_size(daos_handle_t oh, daos_size_t chunk_size)
{
	return dc_array_update_chunk_size(oh, chunk_size);
}

int
daos_array_read(daos_handle_t oh, daos_handle_t th, daos_array_iod_t *iod,
		d_sg_list_t *sgl, daos_event_t *ev)
{
	daos_array_io_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dc_array_read, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->iod	= iod;
	args->sgl	= sgl;

	return dc_task_schedule(task, true);
}

int
daos_array_write(daos_handle_t oh, daos_handle_t th, daos_array_iod_t *iod,
		 d_sg_list_t *sgl, daos_event_t *ev)
{
	daos_array_io_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dc_array_write, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->iod	= iod;
	args->sgl	= sgl;

	return dc_task_schedule(task, true);
}

int
daos_array_punch(daos_handle_t oh, daos_handle_t th,
		 daos_array_iod_t *iod, daos_event_t *ev)
{
	daos_array_io_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dc_array_punch, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->iod	= iod;
	args->sgl	= NULL;

	return dc_task_schedule(task, true);
}

int
daos_array_get_size(daos_handle_t oh, daos_handle_t th, daos_size_t *size,
		    daos_event_t *ev)
{
	daos_array_get_size_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_array_get_size, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->size	= size;

	return dc_task_schedule(task, true);
} /* end daos_array_get_size */

int
daos_array_stat(daos_handle_t oh, daos_handle_t th, daos_array_stbuf_t *stbuf, daos_event_t *ev)
{
	daos_array_stat_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_array_stat, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->stbuf	= stbuf;

	return dc_task_schedule(task, true);
} /* end daos_array_stat */

int
daos_array_set_size(daos_handle_t oh, daos_handle_t th, daos_size_t size,
		    daos_event_t *ev)
{
	daos_array_set_size_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_array_set_size, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->size	= size;

	return dc_task_schedule(task, true);
} /* end daos_array_set_size */
