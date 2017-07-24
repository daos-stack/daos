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
#define DD_SUBSYS	DD_FAC(tier)

#include <daos_types.h>
#include <daos_api.h>
#include <daos_tier.h>
#include <daos/pool.h>
#include <daos/container.h>
#include "rpc.h"
#include "cli_internal.h"
#include <daos_errno.h>

struct tier_fetch_arg {
	crt_rpc_t	*rpc;
	struct dc_pool	*pool;
	daos_handle_t	hdl;
};

static int
tier_fetch_cb(struct daos_task *task, void *data)
{
	struct tier_fetch_arg	*arg = (struct tier_fetch_arg *)data;
	struct dc_pool		*pool = arg->pool;
	struct tier_fetch_out	*tfo;
	int			 rc = task->dt_result;

	if (rc) {
		D_ERROR("RPC error while fetching: %d\n", rc);
		D_GOTO(out, rc);
	}

	tfo = crt_reply_get(arg->rpc);
	rc = tfo->tfo_ret;
	if (rc) {
		D_ERROR("failed to fetch: %d\n", rc);
		D_GOTO(out, rc);
	}

	arg->hdl.cookie = 0;
out:
	crt_req_decref(arg->rpc);
	dc_pool_put(pool);
	return rc;
}

int
dc_tier_fetch_cont(daos_handle_t poh, const uuid_t cont_id,
		   daos_epoch_t fetch_ep, daos_oid_list_t *obj_list,
		   struct daos_task *task)
{
	struct tier_fetch_in	*in;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	struct tier_fetch_arg	arg;
	struct dc_pool		*pool;
	int			rc = 0;
	daos_tier_info_t	*from;

	D_DEBUG(DF_MISC, "Entering tier_fetch_cont()\n");

	/* FIXME nuke the global */
	from = g_tierctx.dtc_colder;
	if (from == NULL) {
		D_DEBUG(DF_TIERC, "fetch: have no colder tier\n");
		D_GOTO(out, -DER_NONEXIST);
	}
	/*
	 * MSC - Switch this to use a task. This will hang if API call is done
	 * synchronously.
	 */
	/* Create the local recipient container */
	rc = daos_cont_create(poh, cont_id, NULL);
	if (rc) {
		D_DEBUG(DF_TIERC, "fetch: create local container: %d\n", rc);
		D_GOTO(out, rc);
	}
	/* FIXME Harded coded enpoint stuff */
	ep.ep_grp = from->ti_group;
	ep.ep_rank = from->ti_leader;
	ep.ep_tag = 0;

	/* Create RPC and allocate memory for the various field-eybops */
	rc = tier_req_create(daos_task2ctx(task), &ep, TIER_FETCH, &rpc);
	if (rc != 0)
		D_GOTO(out_task, rc);

	/* Grab the input struct of the RPC */
	in = crt_req_get(rpc);

	pool = dc_hdl2pool(from->ti_poh);
	if (pool == NULL)
		D_GOTO(out_task, -DER_NO_HDL);

	uuid_copy(in->tfi_co_hdl, cont_id);
	uuid_copy(in->tfi_pool, from->ti_pool_id);
	in->tfi_ep  = fetch_ep;

	crt_req_addref(rpc);

	arg.rpc = rpc;
	arg.hdl = poh;
	arg.pool = pool;

	rc = daos_task_register_comp_cb(task, tier_fetch_cb, &arg, sizeof(arg));
	if (rc != 0)
		D_GOTO(out_req_put, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, task);

	D_DEBUG(DF_MISC, "leaving tier_fetch_cont()\n");

out_req_put:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_task:
	daos_task_complete(task, rc);
out:
	return rc;
}
