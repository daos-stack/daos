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

#include <daos/object.h>
#include <daos/event.h>
#include <daos/container.h>
#include "client_internal.h"

int
daos_oclass_register(daos_handle_t coh, daos_oclass_id_t cid,
		     daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_oclass_register(coh, cid, cattr, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_oclass_query(daos_handle_t coh, daos_oclass_id_t cid,
		  daos_oclass_attr_t *cattr, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_oclass_query(coh, cid, cattr, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_oclass_list(daos_handle_t coh, daos_oclass_list_t *clist,
		 daos_hash_out_t *anchor, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_oclass_list(coh, clist, anchor, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		 daos_obj_attr_t *oa, daos_event_t *ev)
{
	return 0;
}

int
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	      unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_obj_open(coh, oid, epoch, mode, oh, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_obj_close(oh, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_obj_punch(oh, epoch, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

int
daos_obj_query(daos_handle_t oh, daos_epoch_t epoch, daos_obj_attr_t *oa,
	       daos_rank_list_t *ranks, daos_event_t *ev)
{
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dc_obj_query(oh, epoch, oa, ranks, ev);
	if (rc)
		return rc;

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}

static int
daos_obj_pool_hdl_get(daos_handle_t oh, daos_handle_t *ph)
{
	daos_handle_t	ch;

	ch = dc_obj_hdl2cont_hdl(oh);
	if (daos_handle_is_inval(ch))
		return -DER_NO_HDL;

	*ph = dc_cont_hdl2pool_hdl(ch);
	if (daos_handle_is_inval(*ph))
		return -DER_NO_HDL;

	return 0;
}

/* Get pool map version from object handle */
static int
daos_obj_pool_map_version_get(daos_handle_t oh, unsigned int *map_ver)
{
	daos_handle_t	ph;
	int rc;

	rc = daos_obj_pool_hdl_get(oh, &ph);
	if (rc != 0)
		return rc;

	rc = daos_pool_map_version_get(ph, map_ver);

	return rc;
}

enum daos_obj_opc {
	DAOS_OBJ_UPDATE	= 1,
	DAOS_OBJ_FETCH = 2,
	DAOS_OBJ_DKEY_LIST = 3,
	DAOS_OBJ_AKEY_LIST = 4,
};

struct daos_obj_arg {
	int		opc;
	daos_handle_t	oh;
	daos_epoch_t	epoch;
	daos_key_t	*dkey;
	unsigned int	nr;
	daos_vec_iod_t	*iods;
	daos_sg_list_t	*sgls;
	daos_vec_map_t	*maps;
};

struct daos_obj_list_arg {
	int		opc;
	daos_handle_t	oh;
	daos_epoch_t	epoch;
	uint32_t	*nr;
	daos_key_desc_t	*kds;
	daos_dkey_t	*dkey;
	daos_sg_list_t	*sgl;
	daos_hash_out_t	*anchor;
};

static int
daos_obj_fetch_task(struct daos_task *task)
{
	struct daos_obj_arg *arg = daos_task2arg(task);
	unsigned int	map_ver;
	int		rc;

	rc = daos_obj_pool_map_version_get(arg->oh, &map_ver);
	if (rc != 0) {
		daos_task_complete(task, rc);
		return rc;
	}

	return dc_obj_fetch(arg->oh, arg->epoch, arg->dkey, arg->nr,
			    arg->iods, arg->sgls, arg->maps, map_ver, task);
}

static int
daos_obj_update_task(struct daos_task *task)
{
	struct daos_obj_arg *arg = daos_task2arg(task);
	unsigned int	map_ver;
	int		rc;

	rc = daos_obj_pool_map_version_get(arg->oh, &map_ver);
	if (rc != 0) {
		daos_task_complete(task, rc);
		return rc;
	}

	return dc_obj_update(arg->oh, arg->epoch, arg->dkey, arg->nr,
			     arg->iods, arg->sgls, map_ver, task);
}

static int
daos_obj_dkey_list_task(struct daos_task *task)
{
	struct daos_obj_list_arg *arg = daos_task2arg(task);
	unsigned int	map_ver;
	int		rc;

	rc = daos_obj_pool_map_version_get(arg->oh, &map_ver);
	if (rc != 0) {
		daos_task_complete(task, rc);
		return rc;
	}

	return dc_obj_list_dkey(arg->oh, arg->epoch, arg->nr, arg->kds,
				arg->sgl, arg->anchor, map_ver, task);
}

static int
daos_obj_akey_list_task(struct daos_task *task)
{
	struct daos_obj_list_arg *arg = daos_task2arg(task);
	unsigned int	map_ver;
	int		rc;

	rc = daos_obj_pool_map_version_get(arg->oh, &map_ver);
	if (rc != 0) {
		daos_task_complete(task, rc);
		return rc;
	}

	return dc_obj_list_akey(arg->oh, arg->epoch, arg->dkey, arg->nr,
				arg->kds, arg->sgl, arg->anchor, map_ver,
				task);
}

static int
pool_query_comp_cb(struct daos_task *task, void *data)
{
	struct daos_obj_arg	*arg = daos_task2arg(task);
	daos_pool_info_t	*info = data;

	dc_obj_layout_refresh(arg->oh);

	D_FREE_PTR(info);
	return 0;
}

static int
daos_obj_pool_query_task(struct daos_task *task)
{
	struct daos_obj_arg	*arg = daos_task2arg(task);
	daos_pool_info_t	*info;
	daos_handle_t		 ph;
	int			 rc;

	rc = daos_obj_pool_hdl_get(arg->oh, &ph);
	if (rc != 0)
		return rc;

	D_ALLOC_PTR(info);
	if (info == NULL)
		return -DER_NOMEM;

	rc = daos_task_register_comp_cb(task, pool_query_comp_cb, info);
	if (rc != 0) {
		D_FREE_PTR(info);
		return rc;
	}

	rc = daos_pool_query_async(ph, NULL, info, task);
	return rc;
}


/**
 * This function will check the result of dc object API
 * then decide if it needs retry.
 **/
static int
daos_obj_comp_cb(struct daos_task *task, void *data)
{
	struct daos_sched *sched = daos_task2sched(task);
	struct daos_obj_arg *arg = daos_task2arg(task);
	struct daos_task  *pool_task;
	struct daos_task  *rw_task;
	int		  rc = task->dt_result;

	if (rc == 0 || !daos_obj_retry_error(rc)) {
		sched->ds_result = rc;
		return rc;
	}

	D_DEBUG(DB_IO, "task %p opc %d fails %d let's retry.\n",
		task, arg->opc, rc);
	/* Let's reset task result before retry */
	task->dt_result = 0;
	sched->ds_result = 0;

	/* Add pool map update task */
	D_ALLOC_PTR(pool_task);
	if (pool_task == NULL)
		D_GOTO(out, rc);

	rc = daos_task_init(pool_task, daos_obj_pool_query_task,
			    arg, sizeof(*arg), sched, NULL);
	if (rc != 0) {
		D_FREE_PTR(pool_task);
		D_GOTO(out, rc);
	}

	/* add dependent rw task */
	D_ALLOC_PTR(rw_task);
	if (rw_task == NULL)
		D_GOTO(out, rc);

	switch (arg->opc) {
	case DAOS_OBJ_UPDATE:
		rc = daos_task_init(rw_task, daos_obj_update_task,
				    arg, sizeof(struct daos_obj_arg),
				    sched, pool_task);
		break;
	case DAOS_OBJ_FETCH:
		rc = daos_task_init(rw_task, daos_obj_fetch_task,
				    arg, sizeof(struct daos_obj_arg),
				    sched, pool_task);
		break;
	case DAOS_OBJ_DKEY_LIST:
		rc = daos_task_init(rw_task, daos_obj_dkey_list_task,
				    arg, sizeof(struct daos_obj_list_arg),
				    sched, pool_task);
		break;
	case DAOS_OBJ_AKEY_LIST:
		rc = daos_task_init(rw_task, daos_obj_akey_list_task,
				    arg, sizeof(struct daos_obj_list_arg),
				    sched, pool_task);
		break;
	default:
		D_ASSERTF(0, "invalid opc %d\n", arg->opc);
	}
	if (rc != 0) {
		D_FREE_PTR(rw_task);
		D_GOTO(out, rc);
	}

	rc = daos_task_register_comp_cb(rw_task, daos_obj_comp_cb, NULL);
	if (rc != 0)
		D_GOTO(out, rc);
out:
	return rc;
}

int
daos_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	       unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	       daos_vec_map_t *maps, daos_event_t *ev)
{
	struct daos_obj_arg arg;
	struct daos_task *task;
	unsigned int map_ver;
	int rc;

	rc = daos_obj_pool_map_version_get(oh, &map_ver);
	if (rc != 0)
		return rc;

	arg.opc		= DAOS_OBJ_FETCH;
	arg.oh		= oh;
	arg.epoch	= epoch;
	arg.dkey	= dkey;
	arg.nr		= nr;
	arg.iods	= iods;
	arg.sgls	= sgls;
	arg.maps	= maps;

	rc = daos_client_task_prep(daos_obj_comp_cb, &arg, sizeof(arg),
				   &task, &ev);
	if (rc != 0)
		return rc;

	dc_obj_fetch(oh, epoch, dkey, nr, iods, sgls, maps, map_ver,
		     task);

	return daos_client_result_wait(ev);
}

int
daos_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
		unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		daos_event_t *ev)
{
	struct daos_task	*task;
	struct daos_obj_arg	arg;
	unsigned int		map_ver;
	int			rc;

	rc = daos_obj_pool_map_version_get(oh, &map_ver);
	if (rc != 0)
		return rc;

	arg.opc		= DAOS_OBJ_UPDATE;
	arg.oh		= oh;
	arg.epoch	= epoch;
	arg.dkey	= dkey;
	arg.nr		= nr;
	arg.iods	= iods;
	arg.sgls	= sgls;
	rc = daos_client_task_prep(daos_obj_comp_cb, &arg, sizeof(arg),
				   &task, &ev);
	if (rc != 0)
		return rc;

	dc_obj_update(oh, epoch, dkey, nr, iods, sgls, map_ver, task);

	return daos_client_result_wait(ev);
}

int
daos_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		   daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev)
{
	struct daos_task	*task;
	struct daos_obj_list_arg arg;
	unsigned int		map_ver;
	int			rc;

	rc = daos_obj_pool_map_version_get(oh, &map_ver);
	if (rc != 0)
		return rc;

	arg.opc		= DAOS_OBJ_DKEY_LIST;
	arg.oh		= oh;
	arg.epoch	= epoch;
	arg.nr		= nr;
	arg.kds		= kds;
	arg.sgl		= sgl;
	arg.anchor	= anchor;
	rc = daos_client_task_prep(daos_obj_comp_cb, &arg, sizeof(arg),
				   &task, &ev);
	if (rc != 0)
		return rc;

	dc_obj_list_dkey(oh, epoch, nr, kds, sgl, anchor, map_ver, task);

	return daos_client_result_wait(ev);
}

int
daos_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		   daos_hash_out_t *anchor, daos_event_t *ev)
{
	struct daos_task	*task;
	struct daos_obj_list_arg arg;
	unsigned int		map_ver;
	int			rc;

	rc = daos_obj_pool_map_version_get(oh, &map_ver);
	if (rc != 0)
		return rc;

	arg.opc = DAOS_OBJ_AKEY_LIST;
	arg.oh	= oh;
	arg.epoch = epoch;
	arg.nr = nr;
	arg.kds = kds;
	arg.sgl = sgl;
	arg.dkey = dkey;
	arg.anchor = anchor;
	rc = daos_client_task_prep(daos_obj_comp_cb, &arg,
				   sizeof(arg), &task, &ev);
	if (rc != 0)
		return rc;

	dc_obj_list_akey(oh, epoch, dkey, nr, kds, sgl, anchor, map_ver,
			 task);

	return daos_client_result_wait(ev);
}
