/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * create object task.
 */
#define DDSUBSYS	DDFAC(object)

#include <daos_task.h>
#include <daos_types.h>
#include <daos/container.h>
#include <daos/pool.h>
#include <daos/task.h>
#include "obj_internal.h"

int
dc_obj_open_task_create(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
			daos_handle_t *oh, daos_event_t *ev, tse_sched_t *tse,
			tse_task_t **task)
{
	daos_obj_open_t	*args;
	int		rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_OPEN);
	rc = dc_task_create(dc_obj_open, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->coh	= coh;
	args->oid	= oid;
	args->mode	= mode;
	args->oh	= oh;

	return 0;
}

int
dc_obj_close_task_create(daos_handle_t oh, daos_event_t *ev,
			 tse_sched_t *tse, tse_task_t **task)
{
	daos_obj_close_t *args;
	int		 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_CLOSE);
	rc = dc_task_create(dc_obj_close, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh = oh;

	return 0;
}

int
dc_obj_punch_task_create(daos_handle_t oh, daos_handle_t th, uint64_t flags,
			 daos_event_t *ev, tse_sched_t *tse, tse_task_t **task)
{
	daos_obj_punch_t *args;
	int		 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_PUNCH);
	rc = dc_task_create(dc_obj_punch_task, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->th	= th;
	args->flags	= flags;
	args->oh	= oh;

	return 0;
}

int
dc_obj_punch_dkeys_task_create(daos_handle_t oh, daos_handle_t th,
			       uint64_t flags, unsigned int nr,
			       daos_key_t *dkeys, daos_event_t *ev,
			       tse_sched_t *tse, tse_task_t **task)
{
	daos_obj_punch_t	*args;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_PUNCH_DKEYS);
	rc = dc_task_create(dc_obj_punch_dkeys_task, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->th	= th;
	args->flags	= flags;
	args->dkey	= &dkeys[0];
	args->akeys	= NULL;
	args->akey_nr	= 0;

	return 0;
}

int
dc_obj_punch_akeys_task_create(daos_handle_t oh, daos_handle_t th,
			       uint64_t flags, daos_key_t *dkey,
			       unsigned int nr, daos_key_t *akeys,
			       daos_event_t *ev, tse_sched_t *tse,
			       tse_task_t **task)
{
	daos_obj_punch_t	*args;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_PUNCH_AKEYS);
	rc = dc_task_create(dc_obj_punch_akeys_task, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->th	= th;
	args->flags	= flags;
	args->dkey	= dkey;
	args->akeys	= akeys;
	args->akey_nr	= nr;

	return 0;
}

int
dc_obj_query_key_task_create(daos_handle_t oh, daos_handle_t th,
			     uint64_t flags, daos_key_t *dkey, daos_key_t *akey,
			     daos_recx_t *recx, daos_event_t *ev,
			     tse_sched_t *tse, tse_task_t **task)
{
	daos_obj_query_key_t	*args;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_QUERY_KEY);
	rc = dc_task_create(dc_obj_query_key, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->th	= th;
	args->flags	= flags;
	args->dkey	= dkey;
	args->akey	= akey;
	args->recx	= recx;

	return 0;
}

int
dc_obj_sync_task_create(daos_handle_t oh, daos_epoch_t epoch,
			daos_epoch_t **epochs_p, int *nr, daos_event_t *ev,
			tse_sched_t *tse, tse_task_t **task)
{
	struct daos_obj_sync_args	*args;
	int				 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_SYNC);
	rc = dc_task_create(dc_obj_sync, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->epoch	= epoch;
	args->epochs_p	= epochs_p;
	args->nr	= nr;

	return 0;
}

int
dc_obj_fetch_task_create(daos_handle_t oh, daos_handle_t th, uint64_t api_flags,
			 daos_key_t *dkey, uint32_t nr, uint32_t extra_flags,
			 daos_iod_t *iods, d_sg_list_t *sgls, daos_iom_t *ioms,
			 void *extra_arg, daos_event_t *ev, tse_sched_t *tse,
			 tse_task_t **task)
{
	daos_obj_fetch_t	*args;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_FETCH);
	rc = dc_task_create(dc_obj_fetch_task, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh		= oh;
	args->th		= th;
	args->flags		= api_flags;
	args->dkey		= dkey;
	args->nr		= nr;
	args->extra_flags	= extra_flags;
	args->iods		= iods;
	args->sgls		= sgls;
	args->ioms		= ioms;
	args->extra_arg		= extra_arg;

	return 0;
}

int
dc_obj_update_task_create(daos_handle_t oh, daos_handle_t th, uint64_t flags,
			  daos_key_t *dkey, unsigned int nr,
			  daos_iod_t *iods, d_sg_list_t *sgls,
			  daos_event_t *ev, tse_sched_t *tse,
			  tse_task_t **task)
{
	daos_obj_update_t	*args;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_UPDATE);
	rc = dc_task_create(dc_obj_update_task, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->th	= th;
	args->flags	= flags;
	args->dkey	= dkey;
	args->nr	= nr;
	args->iods	= iods;
	args->sgls	= sgls;

	return 0;
}

int
dc_obj_list_dkey_task_create(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
			     daos_key_desc_t *kds, d_sg_list_t *sgl,
			     daos_anchor_t *anchor, daos_event_t *ev,
			     tse_sched_t *tse, tse_task_t **task)
{
	daos_obj_list_dkey_t	*args;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_LIST_DKEY);
	rc = dc_task_create(dc_obj_list_dkey, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh		= oh;
	args->th		= th;
	args->nr		= nr;
	args->kds		= kds;
	args->sgl		= sgl;
	args->dkey_anchor	= anchor;

	return 0;
}

int
dc_obj_list_akey_task_create(daos_handle_t oh, daos_handle_t th,
			     daos_key_t *dkey, uint32_t *nr,
			     daos_key_desc_t *kds, d_sg_list_t *sgl,
			     daos_anchor_t *anchor, daos_event_t *ev,
			     tse_sched_t *tse, tse_task_t **task)
{
	daos_obj_list_akey_t	*args;
	int			 rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_LIST_AKEY);
	rc = dc_task_create(dc_obj_list_akey, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh		= oh;
	args->th		= th;
	args->dkey		= dkey;
	args->nr		= nr;
	args->kds		= kds;
	args->sgl		= sgl;
	args->akey_anchor	= anchor;

	return 0;
}

int
dc_obj_list_recx_task_create(daos_handle_t oh, daos_handle_t th,
			     daos_key_t *dkey, daos_key_t *akey,
			     daos_iod_type_t type,
			     daos_size_t *size, uint32_t *nr,
			     daos_recx_t *recxs, daos_epoch_range_t *eprs,
			     daos_anchor_t *anchor, bool incr_order,
			     daos_event_t *ev, tse_sched_t *tse,
			     tse_task_t **task)
{
	daos_obj_list_recx_t	*args;
	int			rc;

	DAOS_API_ARG_ASSERT(*args, OBJ_LIST_RECX);
	rc = dc_task_create(dc_obj_list_rec, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->th	= th;
	args->dkey	= dkey;
	args->akey	= akey;
	args->type	= type;
	args->size	= size;
	args->nr	= nr;
	args->recxs	= recxs;
	args->eprs	= eprs;
	args->anchor	= anchor;
	args->incr_order = incr_order;

	return 0;
}

int
dc_obj_list_obj_task_create(daos_handle_t oh, daos_handle_t th,
			    daos_epoch_range_t *epr, daos_key_t *dkey,
			    daos_key_t *akey, daos_size_t *size,
			    uint32_t *nr, daos_key_desc_t *kds,
			    d_sg_list_t *sgl, daos_anchor_t *anchor,
			    daos_anchor_t *dkey_anchor,
			    daos_anchor_t *akey_anchor, bool incr_order,
			    daos_event_t *ev, tse_sched_t *tse,
			    d_iov_t *csum, tse_task_t **task)
{
	daos_obj_list_obj_t	*args;
	int			rc;

	rc = dc_task_create(dc_obj_list_obj, tse, ev, task);
	if (rc)
		return rc;

	args = dc_task_get_args(*task);
	args->oh	= oh;
	args->th	= th;
	args->dkey	= dkey;
	args->akey	= akey;
	args->size	= size;
	args->nr	= nr;
	args->kds	= kds;
	args->sgl	= sgl;
	args->eprs	= epr;
	args->anchor	= anchor;
	args->dkey_anchor = dkey_anchor;
	args->akey_anchor = akey_anchor;
	args->incr_order = incr_order;
	args->csum	= csum;

	return 0;
}
