/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
#include <daos_security.h>
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

static bool
valid_pool_create_mode(uint32_t mode)
{
	uint32_t mandatory_bits_mask = (DAOS_PC_RW | DAOS_PC_EX) |
			((DAOS_PC_RW | DAOS_PC_EX) << DAOS_PC_NBITS) |
			((DAOS_PC_RW | DAOS_PC_EX) << (DAOS_PC_NBITS * 2));

	/* extra bits */
	if (mode >= 1U << (DAOS_PC_NBITS * 3))
		return false;

	/* do not allow to create pool with no write perm */
	if ((mode & mandatory_bits_mask) == 0)
		return false;

	return true;
}

static bool
daos_prop_has_entry(daos_prop_t *prop, uint32_t entry_type)
{
	return (prop != NULL) &&
	       (daos_prop_entry_get(prop, entry_type) != NULL);
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
	char			*owner = NULL;
	char			*owner_grp = NULL;
	uint32_t		 entries;
	daos_prop_t		*final_prop = NULL;

	DAOS_API_ARG_ASSERT(*args, POOL_CREATE);
	if (!valid_pool_create_mode(mode)) {
		D_ERROR("Invalid pool creation mode (%o).\n", mode);
		return -DER_INVAL;
	}
	if (pool_prop != NULL && !daos_prop_valid(pool_prop, true, true)) {
		D_ERROR("Invalid pool properties.\n");
		return -DER_INVAL;
	}

	entries = (pool_prop == NULL) ? 0 : pool_prop->dpp_nr;

	/*
	 * TODO: remove uid/gid input params and use euid/egid instead
	 */
	if (!daos_prop_has_entry(pool_prop, DAOS_PROP_PO_OWNER)) {
		rc = daos_acl_uid_to_principal(uid, &owner);
		if (rc) {
			D_ERROR("Invalid uid\n");
			return rc;
		}

		entries++;
	}

	if (!daos_prop_has_entry(pool_prop, DAOS_PROP_PO_OWNER_GROUP)) {
		rc = daos_acl_gid_to_principal(gid, &owner_grp);
		if (rc) {
			D_ERROR("Invalid gid\n");
			D_GOTO(out, rc);
		}

		entries++;
	}

	if (pool_prop == NULL || entries > pool_prop->dpp_nr) {
		uint32_t idx = 0;

		final_prop = daos_prop_alloc(entries);
		if (pool_prop != NULL) {
			rc = daos_prop_copy(final_prop, pool_prop);
			if (rc)
				D_GOTO(out, rc);
			idx = pool_prop->dpp_nr;
		}
		if (owner != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_PO_OWNER;
			final_prop->dpp_entries[idx].dpe_str = owner;
			owner = NULL;
			idx++;
		}

		if (owner_grp != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_PO_OWNER_GROUP;
			final_prop->dpp_entries[idx].dpe_str = owner_grp;
			owner_grp = NULL;
			idx++;
		}

	} else {
		final_prop = pool_prop;
	}

	rc = dc_task_create(dc_pool_create, NULL, ev, &task);
	if (rc)
		D_GOTO(out, rc);

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->tgts	= tgts;
	args->dev	= dev;
	args->scm_size	= scm_size;
	args->nvme_size	= nvme_size;
	args->prop	= final_prop;
	args->svc	= svc;
	args->uuid	= uuid;

	rc = dc_task_schedule(task, true);

out:
	daos_prop_free(final_prop);
	D_FREE(owner);
	D_FREE(owner_grp);
	return rc;
}

int
daos_pool_destroy(const uuid_t uuid, const char *grp, int force,
		  daos_event_t *ev)
{
	daos_pool_destroy_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_DESTROY);
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
daos_pool_evict(const uuid_t uuid, const char *grp, const d_rank_list_t *svc,
		daos_event_t *ev)
{
	daos_pool_evict_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, POOL_EVICT);
	rc = dc_task_create(dc_pool_evict, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->grp	= grp;
	args->svc	= (d_rank_list_t *)svc;
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
daos_pool_tgt_exclude_out(const uuid_t uuid, const char *grp,
			  const d_rank_list_t *svc, struct d_tgt_list *tgts,
			  daos_event_t *ev)
{
	daos_pool_update_t	*args;
	tse_task_t		*task;
	int			 rc;

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

