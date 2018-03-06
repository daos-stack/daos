/**
 * (C) Copyright 2016 Intel Corporation.
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
/*
 * client portion of the fetch operation
 *
 * dctc is the DCT part of client module/library. It exports part of the DCT
 * API defined in daos_tier.h.
 */
#define DDSUBSYS	DDFAC(tier)

#include <daos/common.h>
#include <daos_types.h>
#include <daos_api.h>
#include <daos_tier.h>
#include <daos/pool.h>
#include <daos/container.h>
#include "rpc.h"
#include "cli_internal.h"
#include <daos_errno.h>
#include <daos_task.h>
#include "../client/task_internal.h"

struct tier_fetch_arg {
	crt_rpc_t	*rpc;
	struct dc_pool	*pool;
	daos_handle_t	 hdl;
	tse_task_t *subtask;
	int		 *prc;
};

struct tier_fetch_co_cr_arg {
	int              *prc;
};

static int
tier_fetch_cb(tse_task_t *task, void *data)
{
	struct tier_fetch_arg	*arg = (struct tier_fetch_arg *)data;
	struct tier_fetch_out	*tfo;
	int			 rc = task->dt_result;

	if (rc) {
		D__ERROR("RPC error while fetching: %d\n", rc);
		D__GOTO(out, rc);
	}

	tfo = crt_reply_get(arg->rpc);
	rc = tfo->tfo_ret;
	if (rc) {
		D__ERROR("failed to fetch: %d\n", rc);
		D__GOTO(out, rc);
	}

	arg->hdl.cookie = 0;

	if (*arg->prc < 0) {
		D__ERROR("Failed to create warm tier container: %d\n",
			*arg->prc);
		D__GOTO(out, *arg->prc);
	}

	D__FREE(arg->prc, sizeof(*arg->prc));
out:
	crt_req_decref(arg->rpc);
	return rc;
}

static int
tier_fetch_cont_create_cb(tse_task_t *task, void *data)
{
	struct tier_fetch_co_cr_arg *arg = (struct tier_fetch_co_cr_arg *)data;
	int			     rc = task->dt_result;

	*arg->prc = rc;
	return rc;
}

int
dc_tier_fetch_cont(daos_handle_t poh, const uuid_t cont_id,
		   daos_epoch_t fetch_ep, daos_oid_list_t *obj_list,
		   tse_task_t *task)
{
	struct tier_fetch_in	*in;
	tse_sched_t		*sched;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct tier_fetch_arg	arg;
	int			rc = 0;
	daos_tier_info_t	*from;
	tse_task_t		*cont_open_task;
	struct tier_fetch_co_cr_arg co_args;
	int			*prc;
	daos_cont_open_t	*cont_args;

	D_DEBUG(DF_MISC, "Entering tier_fetch_cont()\n");

	from = g_tierctx.dtc_colder;
	if (from == NULL) {
		D__ERROR(" have no colder tier\n");
		D__GOTO(out, -DER_NONEXIST);
	}
	D__ALLOC_PTR(prc);
	if (prc == NULL) {
		D__ERROR(" could not allocate rc ptr\n");
		D__GOTO(out, -DER_NOMEM);
	}

	sched = tse_task2sched(task);
	co_args.prc = prc;
	*prc = 1;
	rc = dc_task_create(dc_cont_create, sched, NULL, &cont_open_task);
	if (rc != 0) {
		D__FREE_PTR(prc);
		return rc;
	}

	rc = dc_task_reg_comp_cb(cont_open_task, tier_fetch_cont_create_cb,
				 &co_args, sizeof(co_args));
	if (rc != 0) {
		D__ERROR("tse_task_register_comp_cb returned %d\n", rc);
		return rc;
	}

	cont_args = dc_task_get_args(cont_open_task);
	cont_args->poh = poh;
	uuid_copy((unsigned char *)cont_args->uuid, cont_id);

	/* Create the local recipient container */
	rc = dc_task_schedule(cont_open_task, true);
	if (rc) {
		D__ERROR(" create local container: %d\n", rc);
		D__GOTO(out, rc);
	}

	while (*prc == 1) {
		bool empty;

		rc = daos_progress(sched, DAOS_EQ_NOWAIT, &empty);
	}

	ep.ep_grp = from->ti_group;
	ep.ep_rank = from->ti_leader;
	ep.ep_tag = 0;

	/* Create RPC and allocate memory for the various field-eybops */
	rc = tier_req_create(daos_task2ctx(task), &ep, TIER_FETCH, &rpc);
	if (rc != 0)
		D__GOTO(out_task, rc);

	/* Grab the input struct of the RPC */
	in = crt_req_get(rpc);


	uuid_copy(in->tfi_co_id, cont_id);
	uuid_copy(in->tfi_pool, from->ti_pool_id);
	in->tfi_ep  = fetch_ep;

	crt_req_addref(rpc);

	arg.rpc = rpc;
	arg.hdl = poh;

	arg.subtask = cont_open_task;
	arg.prc  = prc;
	rc = tse_task_register_comp_cb(task, tier_fetch_cb, &arg, sizeof(arg));
	if (rc != 0)
		D__GOTO(out_req_put, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, task);
	return rc;

out_req_put:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_task:
	tse_task_complete(task, rc);
out:
	return rc;
}
