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
#define DDSUBSYS	DDFAC(client)

#include <daos/object.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_obj_class_register(daos_handle_t coh, daos_oclass_id_t cid,
			daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	D__ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_class_query(daos_handle_t coh, daos_oclass_id_t cid,
		     daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	D__ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_class_list(daos_handle_t coh, daos_oclass_list_t *clist,
		    daos_hash_out_t *anchor, daos_event_t *ev)
{
	D__ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		 daos_obj_attr_t *oa, daos_event_t *ev)
{
	D__ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	      unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	daos_obj_open_t		*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_OPEN);
	rc = dc_task_create(dc_obj_open, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->coh	= coh;
	args->oid	= oid;
	args->epoch	= epoch;
	args->mode	= mode;
	args->oh	= oh;

	return dc_task_schedule(task, true);
}

int
daos_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	daos_obj_close_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_CLOSE);
	rc = dc_task_create(dc_obj_close, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev)
{
	daos_obj_punch_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_PUNCH);
	rc = dc_task_create(dc_obj_punch, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->epoch	= epoch;
	args->oh	= oh;
	/* dkey, akeys are NULL by default */

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_dkeys(daos_handle_t oh, daos_epoch_t epoch, unsigned int nr,
		     daos_key_t *dkeys, daos_event_t *ev)
{
	daos_obj_punch_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (dkeys == NULL) {
		D__ERROR("NULL dkeys\n");
		return -DER_INVAL;
	} else if (nr != 1) {
		/* TODO: create multiple tasks for punch of multiple dkeys */
		D__ERROR("Can't punch multiple dkeys for now\n");
		return -DER_INVAL;
	} else if (dkeys[0].iov_buf == NULL || dkeys[0].iov_len == 0) {
		D__ERROR("invalid dkey (NULL iov_buf or zero iov_len.\n");
		return -DER_INVAL;
	}

	DAOS_API_ARG_ASSERT(*args, OBJ_PUNCH_DKEYS);
	rc = dc_task_create(dc_obj_punch_dkeys, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->dkey	= &dkeys[0];
	args->akeys	= NULL;
	args->akey_nr	= 0;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_akeys(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		     unsigned int nr, daos_key_t *akeys, daos_event_t *ev)
{
	daos_obj_punch_t	*args;
	tse_task_t		*task;
	int			 rc;

	if (dkey == NULL || dkey->iov_buf == NULL || dkey->iov_len == 0) {
		D_ERROR("NULL or invalid dkey\n");
		return -DER_INVAL;
	}

	DAOS_API_ARG_ASSERT(*args, OBJ_PUNCH_AKEYS);
	rc = dc_task_create(dc_obj_punch_akeys, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->dkey	= dkey;
	args->akeys	= akeys;
	args->akey_nr	= nr;

	return dc_task_schedule(task, true);
}

int
daos_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	       d_rank_list_t *ranks, daos_event_t *ev)
{
	D__ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
	       unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
	       daos_iom_t *maps, daos_event_t *ev)
{
	daos_obj_fetch_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_FETCH);
	rc = dc_task_create(dc_obj_fetch, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->dkey	= dkey;
	args->nr	= nr;
	args->iods	= iods;
	args->sgls	= sgls;
	args->maps	= maps;

	return dc_task_schedule(task, true);
}

int
daos_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		unsigned int nr, daos_iod_t *iods, daos_sg_list_t *sgls,
		daos_event_t *ev)
{
	daos_obj_update_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_UPDATE);
	rc = dc_task_create(dc_obj_update, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->dkey	= dkey;
	args->nr	= nr;
	args->iods	= iods;
	args->sgls	= sgls;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		   daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev)
{
	daos_obj_list_dkey_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_LIST_DKEY);
	rc = dc_task_create(dc_obj_list_dkey, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->nr	= nr;
	args->kds	= kds;
	args->sgl	= sgl;
	args->anchor	= anchor;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev)
{
	daos_obj_list_akey_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_LIST_AKEY);
	rc = dc_task_create(dc_obj_list_akey, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->dkey	= dkey;
	args->nr	= nr;
	args->kds	= kds;
	args->sgl	= sgl;
	args->anchor	= anchor;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_recx(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		   daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs,
		   daos_hash_out_t *anchor, bool incr_order, daos_event_t *ev)
{
	daos_obj_list_recx_t	*args;
	tse_task_t		*task;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_LIST_RECX);
	rc = dc_task_create(dc_obj_list_rec, NULL, ev, &task);
	if (rc)
		return rc;

	args = dc_task_get_args(task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->dkey	= dkey;
	args->akey	= akey;
	args->type	= DAOS_IOD_ARRAY;
	args->size	= size;
	args->nr	= nr;
	args->recxs	= recxs;
	args->eprs	= eprs;
	args->cookies	= NULL;
	args->versions	= NULL;
	args->anchor	= anchor;
	args->incr_order = incr_order;

	return dc_task_schedule(task, true);
}
