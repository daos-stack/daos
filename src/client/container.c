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

#define DD_SUBSYS       DD_FAC(client)
#include <daos/container.h>
#include "client_internal.h"

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
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_cont_create(poh, uuid, task);

	return daos_client_result_wait(ev);
}

int
daos_cont_open(daos_handle_t poh, const uuid_t uuid, unsigned int flags,
	       daos_handle_t *coh, daos_cont_info_t *info, daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_cont_open(poh, uuid, flags, coh, info, task);

	return daos_client_result_wait(ev);
}

int
daos_cont_close(daos_handle_t coh, daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_cont_close(coh, task);

	return daos_client_result_wait(ev);
}

int
daos_cont_destroy(daos_handle_t poh, const uuid_t uuid, int force,
		  daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_cont_destroy(poh, uuid, force, task);

	return daos_client_result_wait(ev);
}

int
daos_cont_query(daos_handle_t container, daos_cont_info_t *info,
		daos_event_t *ev)
{
	return -DER_NOSYS;
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
