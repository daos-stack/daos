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
#define DD_SUBSYS	DD_FAC(client)

#include <daos/pool.h>
#include <daos/pool_map.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_pool_connect(const uuid_t uuid, const char *grp,
		  const daos_rank_list_t *svc, unsigned int flags,
		  daos_handle_t *poh, daos_pool_info_t *info, daos_event_t *ev)
{
	daos_pool_connect_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_CONNECT);

	args.grp = grp;
	args.svc = svc;
	args.flags = flags;
	args.poh = poh;
	args.info = info;
	uuid_copy((unsigned char *)args.uuid, uuid);

	dc_task_create(DAOS_OPC_POOL_CONNECT, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_disconnect(daos_handle_t poh, daos_event_t *ev)
{
	daos_pool_disconnect_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_DISCONNECT);

	args.poh = poh;

	dc_task_create(DAOS_OPC_POOL_DISCONNECT, &args, sizeof(args), &task,
		       &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_local2global(daos_handle_t poh, daos_iov_t *glob)
{
	return dc_pool_local2global(poh, glob);
}

int
daos_pool_global2local(daos_iov_t glob, daos_handle_t *poh)
{
	return dc_pool_global2local(glob, poh);
}

int
daos_pool_query(daos_handle_t poh, daos_rank_list_t *tgts,
		daos_pool_info_t *info, daos_event_t *ev)
{
	daos_pool_query_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_QUERY);

	args.poh = poh;
	args.tgts = tgts;
	args.info = info;

	dc_task_create(DAOS_OPC_POOL_QUERY, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_target_query(daos_handle_t poh, daos_rank_list_t *tgts,
		       daos_rank_list_t *failed, daos_target_info_t *info_list,
		       daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
daos_pool_svc_stop(daos_handle_t poh, daos_event_t *ev)
{
	daos_pool_svc_stop_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_SVC_STOP);

	args.poh = poh;

	dc_task_create(DAOS_OPC_POOL_SVC_STOP, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}
