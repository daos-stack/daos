/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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
#define D_LOGFAC       DD_FAC(client)

#include <daos/container.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_cont_local2global(daos_handle_t coh, daos_iov_t *glob)
{
	return dc_cont_local2global(coh, glob);
}

int
daos_cont_global2local(daos_handle_t poh, daos_iov_t glob, daos_handle_t *coh)
{
	return dc_cont_global2local(poh, glob, coh);
}

int
daos_cont_create(daos_handle_t poh, const uuid_t uuid, daos_event_t *ev)
{
	daos_cont_create_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_CREATE);

	rc = dc_task_create(dc_cont_create, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->poh = poh;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_cont_open(daos_handle_t poh, const uuid_t uuid, unsigned int flags,
	       daos_handle_t *coh, daos_cont_info_t *info, daos_event_t *ev)
{
	daos_cont_open_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_OPEN);

	rc = dc_task_create(dc_cont_open, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->poh		= poh;
	args->flags		= flags;
	args->coh		= coh;
	args->info		= info;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_cont_close(daos_handle_t coh, daos_event_t *ev)
{
	daos_cont_close_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_CLOSE);

	rc = dc_task_create(dc_cont_close, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;

	return dc_task_schedule(task, true);
}

int
daos_cont_destroy(daos_handle_t poh, const uuid_t uuid, int force,
		  daos_event_t *ev)
{
	daos_cont_destroy_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_DESTROY);

	rc = dc_task_create(dc_cont_destroy, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->poh	= poh;
	args->force	= force;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_cont_query(daos_handle_t coh, daos_cont_info_t *info,
		daos_event_t *ev)
{
	daos_cont_query_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_QUERY);

	rc = dc_task_create(dc_cont_query, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->info	= info;

	return dc_task_schedule(task, true);
}

int
daos_cont_oid_alloc(daos_handle_t coh, daos_size_t num_oids, uint64_t *oid,
		    daos_event_t *ev)
{
	daos_cont_oid_alloc_t	*args;
	tse_task_t		*task;
	int                      rc;

	DAOS_API_ARG_ASSERT(*args, CONT_OID_ALLOC);

	rc = dc_task_create(dc_cont_oid_alloc, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->num_oids	= num_oids;
	args->oid	= oid;

	return dc_task_schedule(task, true);
}

int
daos_cont_attr_list(daos_handle_t coh, char *buf, size_t *size,
		    daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
daos_cont_attr_get(daos_handle_t coh, int n, const char *const names[],
		   void *bufs[], size_t *sizes[], daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
daos_cont_attr_set(daos_handle_t coh, int n, const char *const names[],
		   const void *const values[], const size_t sizes[],
		   daos_event_t *ev)
{
	return -DER_NOSYS;
}
