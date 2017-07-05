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

#include <daos/common.h>
#include <daos/scheduler.h>
#include <daos/tier.h>
#include <daos/pool.h>
#include "client_internal.h"
#include "task_internal.h"
#include "../tier/cli_internal.h"


struct xconn_arg {
	uuid_t	uuid;
	char	grp[128];
	daos_handle_t   *poh;
	daos_event_t	*evp;
};

static int
tier_task_prep(void *arg, int arg_size, struct daos_task **taskp,
	       daos_event_t **evp)
{
	daos_event_t *ev = *evp;
	struct daos_task *task = NULL;
	int rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = daos_task_init(NULL, arg, arg_size, daos_ev2sched(ev), &task);
	if (rc != 0)
		D_GOTO(err_task, rc = -DER_NOMEM);

	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(err_task, rc);

	rc = daos_task_schedule(task, false);
	if (rc != 0)
		D_GOTO(err_task, rc);

	*taskp = task;
	*evp = ev;

	return rc;

err_task:
	D_FREE_PTR(task);
	return rc;
}


/*Task Callbacks for cascading and dependent RPCs*/
static int cross_conn_cb(struct daos_task *task, void *data)
{
	/*TODO more meaningful return codes*/
	struct xconn_arg *cb_arg = (struct xconn_arg *)data;

	daos_event_complete(cb_arg->evp, 0);

	return 0;
}


static int
local_tier_conn_cb(struct daos_task *task, void *data)
{

	struct daos_sched	*sched;
	struct daos_task	*cross_conn_task;
	struct xconn_arg	*cb_arg = (struct xconn_arg *)data;
	int			rc = task->dt_result;


	/*Check for task error*/
	if (rc) {
		D_ERROR("Tier Conn task returned error:%d\n", rc);
		return rc;
	}

	D_INFO("local grp:%s\n", cb_arg->grp);
	*cb_arg->poh = g_tierctx.dtc_this->ti_poh;

	/*Grab Scheduler of the task*/
	sched = daos_task2sched(task);

	rc = daos_task_init(NULL, NULL, 0, sched, &cross_conn_task);
	if (rc != 0)
		return -DER_NOMEM;

	rc = daos_task_register_comp_cb(cross_conn_task, cross_conn_cb,
					cb_arg, sizeof(struct xconn_arg));
	if (rc != 0) {
		D_ERROR("Failed to register completion callback: %d\n", rc);
		return rc;
	}

	rc = daos_task_schedule(cross_conn_task, false);
	if (rc != 0)
		return rc;

	rc = dc_tier_connect(cb_arg->uuid, cb_arg->grp, cross_conn_task);
	if (rc != 0) {
		D_ERROR("Error from dc_tier_connect: %d\n", rc);
		return rc;
	}

	return rc;
}


int
daos_tier_fetch_cont(daos_handle_t poh, const uuid_t cont_id,
		     daos_epoch_t fetch_ep, daos_oid_list_t *obj_list,
		     daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_tier_fetch_cont(poh, cont_id, fetch_ep, obj_list, task);

	return daos_client_result_wait(ev);
}


/*TODO eventually aliasing magic...*/
int daos_tier_pool_connect(const uuid_t uuid, const char *grp,
		    const daos_rank_list_t *svc, unsigned int flags,
		    daos_handle_t *poh, daos_pool_info_t *info,
		    daos_event_t *ev)
{
	int			rc = 0;
	struct daos_task	*local_conn_task;
	struct xconn_arg	*cb_arg;
	daos_tier_info_t	*pt;
	struct daos_task_args   *dta;


	/*TODO free these guys, but where...*/
	D_ALLOC_PTR(cb_arg);
	if (cb_arg == NULL)
		return -DER_NOMEM;

	/* Make copies of the UUID and group for our our connection work
	* triggered in the callback
	*/
	uuid_copy(cb_arg->uuid, uuid);
	strcpy(cb_arg->grp, grp);
	cb_arg->evp = ev;
	cb_arg->poh = poh;
	D_INFO("local grp:%s\n", cb_arg->grp);

	/*Client prep, plus a manual callback register to  add our CB arg*/
	/*TODO try manual even completion?*/
	rc = tier_task_prep(NULL, 0, &local_conn_task, &ev);
	if (rc) {
		D_ERROR("Error in client task prep: %d\n", rc);
		return rc;
	}

	rc = daos_task_register_comp_cb(local_conn_task, local_tier_conn_cb,
					cb_arg, sizeof(struct xconn_arg));

	if (rc) {
		D_ERROR("Error registering comp cb: %d\n", rc);
		return rc;
	}

	/*Connect to local pool, if this succeeeds, its noted in the CB,
	 * which will trigger the cross connect logic
	 */
	pt = tier_lookup(grp);
	if (pt == NULL) {
		D_ERROR("Failed to lookup group\n");
		return -DER_ENOENT;
	}

	dta = daos_task_buf_get(local_conn_task, sizeof(*dta));
	dta->opc = DAOS_OPC_POOL_CONNECT;
	uuid_copy((unsigned char *)dta->op_args.pool_connect.uuid, uuid);
	dta->op_args.pool_connect.grp = grp;
	dta->op_args.pool_connect.svc = svc;
	dta->op_args.pool_connect.flags = flags;
	dta->op_args.pool_connect.poh = &pt->ti_poh;
	dta->op_args.pool_connect.info = info;

	rc = dc_pool_connect(local_conn_task);
	if (rc) {
		D_ERROR("Error from dc_pool_connect: %d\n", rc);
		return rc;
	}


	/*Annnnd we wait if its a private event complete*/
	rc = daos_client_result_wait(ev);

	return rc;
}

int
daos_tier_register_cold(const uuid_t colder_id, const char *colder_grp,
			const uuid_t tgt_uuid, char *tgt_grp, daos_event_t *ev)
{

	int rc;
	struct daos_task	*trc_task;

	tier_setup_this_tier(tgt_uuid, tgt_grp);
	tier_setup_cold_tier(colder_id, colder_grp);

	rc = daos_client_task_prep(NULL, 0, &trc_task, &ev);

	rc = dc_tier_register_cold(colder_id, colder_grp, tgt_grp, trc_task);
	if (rc)
		return rc;

	return daos_client_result_wait(ev);
}

int
daos_tier_ping(uint32_t ping_val, daos_event_t *ev)
{
	struct daos_task	*task;
	int			rc;

	rc = daos_client_task_prep(NULL, 0, &task, &ev);
	if (rc != 0)
		return rc;

	dc_tier_ping(ping_val, task);

	return daos_client_result_wait(ev);
}
