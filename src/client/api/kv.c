/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * This file is part of daos
 *
 * src/client/api/daos_kv.c
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/kv.h>
#include <daos_kv.h>

int
daos_kv_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	     daos_handle_t *oh, daos_event_t *ev)
{
	daos_kv_open_t	*args;
	tse_task_t	*task;
	int		rc;

	rc = dc_task_create(dc_kv_open, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	 = coh;
	args->oid	 = oid;
	args->mode	 = mode;
	args->oh	 = oh;

	return dc_task_schedule(task, true);
}

int
daos_kv_close(daos_handle_t oh, daos_event_t *ev)
{
	daos_kv_close_t	*args;
	tse_task_t	*task;
	int		rc;

	rc = dc_task_create(dc_kv_close, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh = oh;

	return dc_task_schedule(task, true);
}

int
daos_kv_destroy(daos_handle_t oh, daos_handle_t th, daos_event_t *ev)
{
	daos_kv_destroy_t	*args;
	tse_task_t		*task;
	int			 rc;

	rc = dc_task_create(dc_kv_destroy, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;

	return dc_task_schedule(task, true);
}

int
daos_kv_put(daos_handle_t oh, daos_handle_t th, uint64_t flags, const char *key,
	    daos_size_t buf_size, const void *buf, daos_event_t *ev)
{
	daos_kv_put_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dc_kv_put, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->flags	= flags;
	args->key	= key;
	args->buf_size	= buf_size;
	args->buf	= buf;

	return dc_task_schedule(task, true);
}

int
daos_kv_get(daos_handle_t oh, daos_handle_t th, uint64_t flags, const char *key,
	    daos_size_t *buf_size, void *buf, daos_event_t *ev)
{
	daos_kv_get_t	*args;
	tse_task_t	*task;
	int		 rc;

	rc = dc_task_create(dc_kv_get, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->flags	= flags;
	args->key	= key;
	args->buf_size	= buf_size;
	args->buf	= buf;

	return dc_task_schedule(task, true);
}

int
daos_kv_remove(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       const char *key, daos_event_t *ev)
{
	daos_kv_remove_t	*args;
	tse_task_t		*task;
	int			rc;

	rc = dc_task_create(dc_kv_remove, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->th	= th;
	args->flags	= flags;
	args->key	= key;

	return dc_task_schedule(task, true);
}

int
daos_kv_list(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
	     daos_key_desc_t *kds, d_sg_list_t *sgl, daos_anchor_t *anchor,
	     daos_event_t *ev)
{
	daos_kv_list_t	*args;
	tse_task_t	*task;
	int		rc;

	rc = dc_task_create(dc_kv_list, NULL, ev, &task);
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
