/**
 * (C) Copyright 2015-2020 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(client)

#include <daos/mgmt.h>
#include <daos/sys_debug.h>
#include <daos/pool.h>
#include <daos/task.h>
#include <daos_mgmt.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_mgmt_svc_rip(const char *grp, d_rank_t rank, bool force,
		  daos_event_t *ev)
{
	daos_svc_rip_t		*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, SVC_RIP);
	rc = dc_task_create(dc_mgmt_svc_rip, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->rank	= rank;
	args->force	= force;

	return dc_task_schedule(task, true);
}

int
daos_debug_set_params(const char *grp, d_rank_t rank, unsigned int key_id,
		     uint64_t value, uint64_t value_extra, daos_event_t *ev)
{
	daos_set_params_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, SET_PARAMS);
	rc = dc_task_create(dc_debug_set_params, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp		= grp;
	args->rank		= rank;
	args->key_id		= key_id;
	args->value		= value;
	args->value_extra	= value_extra;

	return dc_task_schedule(task, true);
}

int
daos_pool_reint_tgt(const uuid_t uuid, const char *grp,
		    const d_rank_list_t *svc, struct d_tgt_list *tgts,
		    daos_event_t *ev)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_reint, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_pool_drain_tgt(const uuid_t uuid, const char *grp,
		  const d_rank_list_t *svc, struct d_tgt_list *tgts,
		  daos_event_t *ev)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_drain, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_pool_tgt_exclude_out(const uuid_t uuid, const char *grp,
			  const d_rank_list_t *svc, struct d_tgt_list *tgts,
			  daos_event_t *ev)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_exclude_out, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_pool_tgt_exclude(const uuid_t uuid, const char *grp,
		      const d_rank_list_t *svc, struct d_tgt_list *tgts,
		      daos_event_t *ev)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_EXCLUDE);
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_exclude, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
	args->tgts	= tgts;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_pool_extend(const uuid_t uuid, const char *grp, d_rank_list_t *tgts,
		 d_rank_list_t *failed, daos_event_t *ev)
{
	return -DER_NOSYS;
}

int
daos_debug_add_mark(const char *mark)
{
	return dc_debug_add_mark(mark);
}

int
daos_mgmt_get_bs_state(const char *group, uuid_t blobstore_uuid,
		       int *blobstore_state, daos_event_t *ev)
{
	daos_mgmt_get_bs_state_t	*args;
	tse_task_t			*task;
	int				rc;

	DAOS_API_ARG_ASSERT(*args, MGMT_GET_BS_STATE);

	if (uuid_is_null(blobstore_uuid)) {
		D_ERROR("Blobstore UUID must be non-NULL\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_mgmt_get_bs_state, NULL, ev, &task);
	if (rc)
		return rc;
	args = dc_task_get_args(task);
	args->grp = group;
	args->state = blobstore_state;
	uuid_copy(args->uuid, blobstore_uuid);

	return dc_task_schedule(task, true);
}
