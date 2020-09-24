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
daos_mgmt_set_params(const char *grp, d_rank_t rank, unsigned int key_id,
		     uint64_t value, uint64_t value_extra, daos_event_t *ev)
{
	daos_set_params_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, SET_PARAMS);
	rc = dc_task_create(dc_mgmt_set_params, NULL, ev, &task);
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
daos_pool_create(uint32_t mode, uid_t uid, gid_t gid, const char *grp,
		 const d_rank_list_t *tgts, const char *dev,
		 daos_size_t scm_size, daos_size_t nvme_size,
		 daos_prop_t *pool_prop, d_rank_list_t *svc,
		 uuid_t uuid, daos_event_t *ev)
{
	daos_pool_create_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_CREATE);
	if (pool_prop != NULL && !daos_prop_valid(pool_prop, true, true)) {
		D_ERROR("Invalid pool properties.\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_pool_create, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->mode	= mode;
	args->uid	= uid;
	args->gid	= gid;
	args->grp	= grp;
	args->tgts	= tgts;
	args->dev	= dev;
	args->scm_size	= scm_size;
	args->nvme_size	= nvme_size;
	args->prop	= pool_prop;
	args->svc	= svc;
	args->uuid	= uuid;

	return dc_task_schedule(task, true);
}

int
daos_pool_destroy(const uuid_t uuid, const char *grp, int force,
		  daos_event_t *ev)
{
	daos_pool_destroy_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_DESTROY);
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_destroy, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->force	= force;
	uuid_copy((unsigned char *)args->uuid, uuid);

	return dc_task_schedule(task, true);
}

int
daos_pool_add_tgt(const uuid_t uuid, const char *grp,
		  const d_rank_list_t *svc, struct d_tgt_list *tgts,
		  daos_event_t *ev)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_add, NULL, ev, &task);
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
daos_pool_add_replicas(const uuid_t uuid, const char *group,
		       d_rank_list_t *svc, d_rank_list_t *targets,
		       d_rank_list_t *failed, daos_event_t *ev)
{
	daos_pool_replicas_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_ADD_REPLICAS);
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_add_replicas, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	uuid_copy((unsigned char *)args->uuid, uuid);
	args->group	= group;
	args->svc	= svc;
	args->targets	= targets;
	args->failed	= failed;

	return dc_task_schedule(task, true);
}

int
daos_pool_remove_replicas(const uuid_t uuid, const char *group,
			  d_rank_list_t *svc, d_rank_list_t *targets,
			  d_rank_list_t *failed, daos_event_t *ev)
{
	daos_pool_replicas_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_REMOVE_REPLICAS);
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	rc = dc_task_create(dc_pool_remove_replicas, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	uuid_copy((unsigned char *)args->uuid, uuid);
	args->group	= group;
	args->svc	= svc;
	args->targets	= targets;
	args->failed	= failed;

	return dc_task_schedule(task, true);
}

int
daos_mgmt_list_pools(const char *group, daos_size_t *npools,
		     daos_mgmt_pool_info_t *pools, daos_event_t *ev)
{
	daos_mgmt_list_pools_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, MGMT_LIST_POOLS);

	if (npools == NULL) {
		D_ERROR("npools must be non-NULL\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_mgmt_list_pools, NULL, ev, &task);
	if (rc)
		return rc;
	args = dc_task_get_args(task);
	args->grp = group;
	args->pools = pools;
	args->npools = npools;

	return dc_task_schedule(task, true);
}

int
daos_mgmt_add_mark(const char *mark)
{
	return dc_mgmt_add_mark(mark);
}
