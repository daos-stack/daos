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

#include <daos_mgmt.h>

#include <daos/mgmt.h>
#include <daos/pool.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_mgmt_svc_rip(const char *grp, daos_rank_t rank, bool force,
		  daos_event_t *ev)
{
	daos_svc_rip_t		args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, SVC_RIP);

	args.grp = grp;
	args.rank = rank;
	args.force = force;

	dc_task_create(DAOS_OPC_SVC_RIP, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_create(unsigned int mode, unsigned int uid, unsigned int gid,
		 const char *grp, const daos_rank_list_t *tgts, const char *dev,
		 daos_size_t size, daos_rank_list_t *svc, uuid_t uuid,
		 daos_event_t *ev)
{
	daos_pool_create_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_CREATE);

	args.mode = mode;
	args.uid = uid;
	args.gid = gid;
	args.grp = grp;
	args.tgts = tgts;
	args.dev = dev;
	args.size = size;
	args.svc = svc;
	args.uuid = uuid;

	dc_task_create(DAOS_OPC_POOL_CREATE, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_destroy(const uuid_t uuid, const char *grp, int force,
		  daos_event_t *ev)
{
	daos_pool_destroy_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_DESTROY);

	args.grp = grp;
	args.force = force;
	uuid_copy((unsigned char *)args.uuid, uuid);

	dc_task_create(DAOS_OPC_POOL_DESTROY, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_evict(const uuid_t uuid, const char *grp, const daos_rank_list_t *svc,
		daos_event_t *ev)
{
	daos_pool_evict_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_EVICT);

	args.grp = grp;
	args.svc = (daos_rank_list_t *)svc;
	uuid_copy((unsigned char *)args.uuid, uuid);

	dc_task_create(DAOS_OPC_POOL_EVICT, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_tgt_add(const uuid_t uuid, const char *grp,
		  const daos_rank_list_t *svc, daos_rank_list_t *tgts,
		  daos_event_t *ev)
{
	daos_pool_update_t	args;
	struct daos_task	*task;

	uuid_copy((void *)args.uuid, uuid);
	args.grp = grp;
	args.svc = (daos_rank_list_t *)svc;
	args.tgts = tgts;

	dc_task_create(DAOS_OPC_POOL_ADD, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_exclude_out(const uuid_t uuid, const char *grp,
		      const daos_rank_list_t *svc, daos_rank_list_t *tgts,
		      daos_event_t *ev)
{
	daos_pool_update_t	args;
	struct daos_task	*task;

	uuid_copy((void *)args.uuid, uuid);
	args.grp = grp;
	args.svc = (daos_rank_list_t *)svc;
	args.tgts = tgts;

	dc_task_create(DAOS_OPC_POOL_EXCLUDE_OUT, &args, sizeof(args), &task,
		       &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_exclude(const uuid_t uuid, const char *grp,
		  const daos_rank_list_t *svc, daos_rank_list_t *tgts,
		  daos_event_t *ev)
{
	daos_pool_update_t	args;
	struct daos_task	*task;

	DAOS_API_ARG_ASSERT(args, POOL_EXCLUDE);

	uuid_copy((void *)args.uuid, uuid);
	args.grp = grp;
	args.svc = (daos_rank_list_t *)svc;
	args.tgts = tgts;

	dc_task_create(DAOS_OPC_POOL_EXCLUDE, &args, sizeof(args), &task, &ev);
	return daos_client_result_wait(ev);
}

int
daos_pool_extend(const uuid_t uuid, const char *grp, daos_rank_list_t *tgts,
		 daos_rank_list_t *failed, daos_event_t *ev)
{
	return -DER_NOSYS;
}
