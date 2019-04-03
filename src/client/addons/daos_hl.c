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
 * src/addons/daos_obj.c
 */
#define D_LOGFAC	DD_FAC(addons)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/addons.h>
#include <daos_addons.h>

int
daos_kv_put(daos_handle_t oh, daos_handle_t th, const char *key,
	    daos_size_t buf_size, const void *buf, daos_event_t *ev)
{
	daos_kv_put_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dac_kv_put, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->key	= key;
	args->buf_size	= buf_size;
	args->buf	= buf;

	return dc_task_schedule(task, true);
}

int
daos_kv_get(daos_handle_t oh, daos_handle_t th, const char *key,
	    daos_size_t *buf_size, void *buf, daos_event_t *ev)
{
	daos_kv_get_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dac_kv_get, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->key	= key;
	args->buf_size	= buf_size;
	args->buf	= buf;

	return dc_task_schedule(task, true);
}

int
daos_kv_remove(daos_handle_t oh, daos_handle_t th, const char *key,
	       daos_event_t *ev)
{
	daos_kv_remove_t	*args;
	tse_task_t		*task;
	int			rc;

	rc = dc_task_create(dac_kv_remove, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->key	= key;

	return dc_task_schedule(task, true);
}

int
daos_kv_list(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
	     daos_key_desc_t *kds, daos_sg_list_t *sgl, daos_anchor_t *anchor,
	     daos_event_t *ev)
{
	daos_kv_list_t	*args;
	tse_task_t	*task;
	int		rc;

	rc = dc_task_create(dac_kv_list, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->nr	= nr;
	args->kds	= kds;
	args->sgl	= sgl;
	args->anchor	= anchor;

	return dc_task_schedule(task, true);
}

int
daos_obj_fetch_multi(daos_handle_t oh, daos_handle_t th,
		     unsigned int num_dkeys, daos_dkey_io_t *io_array,
		     daos_event_t *ev)
{
	daos_obj_multi_io_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (num_dkeys == 0)
		return 0;

	rc = dc_task_create(dac_obj_fetch_multi, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->num_dkeys	= num_dkeys;
	args->io_array	= io_array;

	return dc_task_schedule(task, true);
}

int
daos_obj_update_multi(daos_handle_t oh, daos_handle_t th,
		      unsigned int num_dkeys, daos_dkey_io_t *io_array,
		      daos_event_t *ev)
{
	daos_obj_multi_io_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (num_dkeys == 0)
		return 0;

	rc = dc_task_create(dac_obj_update_multi, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->num_dkeys	= num_dkeys;
	args->io_array	= io_array;

	return dc_task_schedule(task, true);
}
