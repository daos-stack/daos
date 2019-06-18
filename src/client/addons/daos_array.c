/**
 * (C) Copyright 2016 Intel Corporation.
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
/**
 * This file is part of daos_m
 *
 * src/addons/daos_array.c
 */
#define D_LOGFAC	DD_FAC(addons)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/addons.h>
#include <daos_addons.h>

int
daos_array_create(daos_handle_t coh, daos_obj_id_t oid, daos_handle_t th,
		  daos_size_t cell_size, daos_size_t chunk_size,
		  daos_handle_t *oh, daos_event_t *ev)
{
	daos_array_create_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dac_array_create, NULL, ev, &task);
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

	rc = dc_task_create(dac_array_open, NULL, ev, &task);
	if (rc)
		return rc;

	*cell_size	 = 0;
	*chunk_size	 = 0;

	args = dc_task_get_args(task);
	args->coh	 = coh;
	args->oid	 = oid;
	args->th	 = th;
	args->mode	 = mode;
	args->cell_size	 = cell_size;
	args->chunk_size = chunk_size;
	args->oh	 = oh;

	return dc_task_schedule(task, true);
}

int
daos_array_local2global(daos_handle_t oh, d_iov_t *glob)
{
	return dac_array_local2global(oh, glob);
}

int
daos_array_global2local(daos_handle_t coh, d_iov_t glob, unsigned int mode,
			daos_handle_t *oh)
{
	return dac_array_global2local(coh, glob, mode, oh);
}

int
daos_array_close(daos_handle_t oh, daos_event_t *ev)
{
	daos_array_close_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dac_array_close, NULL, ev, &task);
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

	rc = dc_task_create(dac_array_destroy, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;

	return dc_task_schedule(task, true);
}

int
daos_array_read(daos_handle_t oh, daos_handle_t th,
		daos_array_iod_t *iod, d_sg_list_t *sgl,
		daos_csum_buf_t *csums, daos_event_t *ev)
{
	daos_array_io_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dac_array_read, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->iod	= iod;
	args->sgl	= sgl;
	args->csums	= csums;

	return dc_task_schedule(task, true);
}

int
daos_array_write(daos_handle_t oh, daos_handle_t th,
		 daos_array_iod_t *iod, d_sg_list_t *sgl,
		 daos_csum_buf_t *csums, daos_event_t *ev)
{
	daos_array_io_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dac_array_write, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->iod	= iod;
	args->sgl	= sgl;
	args->csums	= csums;

	return dc_task_schedule(task, true);
}

int
daos_array_punch(daos_handle_t oh, daos_handle_t th,
		 daos_array_iod_t *iod, daos_event_t *ev)
{
	daos_array_io_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dac_array_punch, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->iod	= iod;
	args->sgl	= NULL;
	args->csums	= NULL;

	return dc_task_schedule(task, true);
}

int
daos_array_get_size(daos_handle_t oh, daos_handle_t th, daos_size_t *size,
		    daos_event_t *ev)
{
	daos_array_get_size_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dac_array_get_size, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->size	= size;

	return dc_task_schedule(task, true);
} /* end daos_array_get_size */

int
daos_array_set_size(daos_handle_t oh, daos_handle_t th, daos_size_t size,
		    daos_event_t *ev)
{
	daos_array_set_size_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dac_array_set_size, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->size	= size;

	return dc_task_schedule(task, true);
} /* end daos_array_set_size */
