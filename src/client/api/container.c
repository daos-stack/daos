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
#define D_LOGFAC       DD_FAC(client)

#include <daos/container.h>
#include <daos/task.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_cont_local2global(daos_handle_t coh, d_iov_t *glob)
{
	return dc_cont_local2global(coh, glob);
}

int
daos_cont_global2local(daos_handle_t poh, d_iov_t glob, daos_handle_t *coh)
{
	return dc_cont_global2local(poh, glob, coh);
}

int
daos_cont_create(daos_handle_t poh, const uuid_t uuid, daos_prop_t *cont_prop,
		 daos_event_t *ev)
{
	daos_cont_create_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_CREATE);
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

	if (cont_prop != NULL && !daos_prop_valid(cont_prop, false, true)) {
		D_ERROR("Invalid container properties.\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_cont_create, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->poh = poh;
	uuid_copy((unsigned char *)args->uuid, uuid);
	args->prop = cont_prop;

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
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

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
	if (!daos_uuid_valid(uuid))
		return -DER_INVAL;

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
		daos_prop_t *cont_prop, daos_event_t *ev)
{
	daos_cont_query_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_QUERY);
	if (cont_prop != NULL && !daos_prop_valid(cont_prop, false, false)) {
		D_ERROR("invalid cont_prop parameter.\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_cont_query, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->info	= info;
	args->prop	= cont_prop;

	return dc_task_schedule(task, true);
}

int
daos_cont_get_acl(daos_handle_t coh, daos_prop_t **acl_prop, daos_event_t *ev)
{
	daos_prop_t	*prop;
	const size_t	nr_entries = 3;
	int		rc;

	if (acl_prop == NULL) {
		D_ERROR("invalid acl_prop parameter\n");
		return -DER_INVAL;
	}

	prop = daos_prop_alloc(nr_entries);
	if (prop == NULL)
		return -DER_NOMEM;

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ACL;
	prop->dpp_entries[1].dpe_type = DAOS_PROP_CO_OWNER;
	prop->dpp_entries[2].dpe_type = DAOS_PROP_CO_OWNER_GROUP;

	rc = daos_cont_query(coh, NULL, prop, ev);
	if (rc == 0)
		*acl_prop = prop;
	else
		daos_prop_free(prop);

	return rc;
}

int
daos_cont_set_prop(daos_handle_t coh, daos_prop_t *prop, daos_event_t *ev)
{
	daos_cont_set_prop_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_SET_PROP);
	if (prop != NULL && !daos_prop_valid(prop, false, true)) {
		D_ERROR("invalid prop parameter.\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_cont_set_prop, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->prop	= prop;

	return dc_task_schedule(task, true);
}

int
daos_cont_overwrite_acl(daos_handle_t coh, struct daos_acl *acl,
			daos_event_t *ev)
{
	daos_prop_t	*prop;
	int		rc;

	if (daos_acl_cont_validate(acl) != 0) {
		D_ERROR("invalid acl parameter\n");
		return -DER_INVAL;
	}

	prop = daos_prop_alloc(1);
	if (prop == NULL)
		return -DER_NOMEM;

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_ACL;
	prop->dpp_entries[0].dpe_val_ptr = daos_acl_dup(acl);

	rc = daos_cont_set_prop(coh, prop, ev);

	daos_prop_free(prop);
	return rc;
}

int
daos_cont_update_acl(daos_handle_t coh, struct daos_acl *acl, daos_event_t *ev)
{
	daos_cont_update_acl_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_UPDATE_ACL);
	if (daos_acl_validate(acl) != 0) {
		D_ERROR("invalid acl parameter.\n");
		return -DER_INVAL;
	}

	rc = dc_task_create(dc_cont_update_acl, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->acl	= acl;

	return dc_task_schedule(task, true);
}

int
daos_cont_delete_acl(daos_handle_t coh, enum daos_acl_principal_type type,
		     d_string_t name, daos_event_t *ev)
{
	daos_cont_delete_acl_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_DELETE_ACL);

	rc = dc_task_create(dc_cont_delete_acl, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->type	= (uint8_t)type;
	args->name	= name;

	return dc_task_schedule(task, true);
}

int
daos_cont_set_owner(daos_handle_t coh, d_string_t user, d_string_t group,
		    daos_event_t *ev)
{
	daos_prop_t	*prop;
	uint32_t	nr = 0;
	uint32_t	i = 0;
	int		rc;

	if (user != NULL) {
		if (!daos_acl_principal_is_valid(user)) {
			D_ERROR("user principal invalid\n");
			return -DER_INVAL;
		}

		nr++;
	}

	if (group != NULL) {
		if (!daos_acl_principal_is_valid(group)) {
			D_ERROR("group principal invalid\n");
			return -DER_INVAL;
		}

		nr++;
	}

	if (nr == 0) {
		D_ERROR("user or group required\n");
		return -DER_INVAL;
	}

	prop = daos_prop_alloc(nr);
	if (prop == NULL)
		return -DER_NOMEM;

	if (user != NULL) {
		prop->dpp_entries[i].dpe_type = DAOS_PROP_CO_OWNER;
		D_STRNDUP(prop->dpp_entries[i].dpe_str, user,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
		i++;
	}

	if (group != NULL) {
		prop->dpp_entries[i].dpe_type = DAOS_PROP_CO_OWNER_GROUP;
		D_STRNDUP(prop->dpp_entries[i].dpe_str, group,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);
		i++;
	}

	rc = daos_cont_set_prop(coh, prop, ev);

	daos_prop_free(prop);
	return rc;
}

int
daos_cont_aggregate(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev)
{
	daos_cont_aggregate_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_AGGREGATE);
	rc = dc_task_create(dc_cont_aggregate, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;

	return dc_task_schedule(task, true);
}

int
daos_cont_rollback(daos_handle_t coh, daos_epoch_t epoch, daos_event_t *ev)
{
	daos_cont_rollback_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_ROLLBACK);
	rc = dc_task_create(dc_cont_rollback, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;

	return dc_task_schedule(task, true);
}

int
daos_cont_subscribe(daos_handle_t coh, daos_epoch_t *epoch, daos_event_t *ev)
{
	daos_cont_subscribe_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_SUBSCRIBE);
	rc = dc_task_create(dc_cont_subscribe, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;

	return dc_task_schedule(task, true);
}

int
daos_cont_alloc_oids(daos_handle_t coh, daos_size_t num_oids, uint64_t *oid,
		    daos_event_t *ev)
{
	daos_cont_alloc_oids_t	*args;
	tse_task_t		*task;
	int                      rc;

	DAOS_API_ARG_ASSERT(*args, CONT_ALLOC_OIDS);

	rc = dc_task_create(dc_cont_alloc_oids, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->num_oids	= num_oids;
	args->oid	= oid;

	return dc_task_schedule(task, true);
}

int
daos_cont_list_attr(daos_handle_t coh, char *buf, size_t *size,
		    daos_event_t *ev)
{
	daos_cont_list_attr_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_LIST_ATTR);

	rc = dc_task_create(dc_cont_list_attr, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->buf	= buf;
	args->size	= size;

	return dc_task_schedule(task, true);
}

int
daos_cont_get_attr(daos_handle_t coh, int n, char const *const names[],
		   void *const values[], size_t sizes[], daos_event_t *ev)
{
	daos_cont_get_attr_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_GET_ATTR);

	rc = dc_task_create(dc_cont_get_attr, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->n		= n;
	args->names	= names;
	args->values	= values;
	args->sizes	= sizes;

	return dc_task_schedule(task, true);
}

int
daos_cont_set_attr(daos_handle_t coh, int n, char const *const names[],
		   void const *const values[], size_t const sizes[],
		   daos_event_t *ev)
{
	daos_cont_set_attr_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_SET_ATTR);

	rc = dc_task_create(dc_cont_set_attr, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->n		= n;
	args->names	= names;
	args->values	= values;
	args->sizes	= sizes;

	return dc_task_schedule(task, true);
}

int
daos_cont_del_attr(daos_handle_t coh, int n, char const *const names[],
		   daos_event_t *ev)
{
	daos_cont_del_attr_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_DEL_ATTR);

	rc = dc_task_create(dc_cont_del_attr, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->n		= n;
	args->names	= names;

	return dc_task_schedule(task, true);
}

int
daos_cont_list_snap(daos_handle_t coh, int *nr, daos_epoch_t *epochs,
		    char **names, daos_anchor_t *anchor, daos_event_t *ev)
{
	daos_cont_list_snap_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_LIST_SNAP);

	rc = dc_task_create(dc_cont_list_snap, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->nr	= nr;
	args->epochs	= epochs;
	args->names	= names;
	args->anchor	= anchor;

	return dc_task_schedule(task, true);
}

int
daos_cont_create_snap(daos_handle_t coh, daos_epoch_t *epoch, char *name,
		      daos_event_t *ev)
{
	daos_cont_create_snap_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_CREATE_SNAP);

	rc = dc_task_create(dc_cont_create_snap, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epoch	= epoch;
	args->name	= name;

	return dc_task_schedule(task, true);
}

int
daos_cont_destroy_snap(daos_handle_t coh, daos_epoch_range_t epr,
		       daos_event_t *ev)
{
	daos_cont_destroy_snap_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, CONT_DESTROY_SNAP);

	rc = dc_task_create(dc_cont_destroy_snap, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->epr	= epr;

	return dc_task_schedule(task, true);
}
