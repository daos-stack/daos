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
#include <daos_tier.h>
#include <daos/pool.h>
#include "dct_rpc.h"

struct tier_fetch_arg {
	crt_rpc_t	*rpc;
	struct dc_pool	*pool;
	daos_handle_t	hdl;
};

static int
dct_fetch_cb(struct daos_task *task, void *data)
{
	struct tier_fetch_arg	*arg = (struct tier_fetch_arg *)data;
	struct dc_pool		*pool = arg->pool;
	struct tier_fetch_out	*tfo;
	int			rc = task->dt_result;

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
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc;
	struct tier_fetch_arg	*arg;
	struct dc_pool		*pool;
	int			rc;

	D_DEBUG(DF_MISC, "Entering daos_fetch_container()\n");

	/* FIXME Harded coded enpoint stuff */
	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	/* Create RPC and allocate memory for the various field-eybops */
	rc = dct_req_create(daos_task2ctx(task), ep, TIER_FETCH, &rpc);
	if (rc != 0)
		D_GOTO(out_task, rc);

	/* Grab the input struct of the RPC */
	in = crt_req_get(rpc);

	pool = dc_pool_lookup(poh);
	if (pool == NULL)
		return -DER_NO_HDL;

	uuid_copy(in->tfi_co_hdl, cont_id);
	uuid_copy(in->tfi_pool, pool->dp_pool);
	uuid_copy(in->tfi_pool_hdl, pool->dp_pool_hdl);
	in->tfi_ep  = fetch_ep;

	crt_req_addref(rpc);

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		D_GOTO(out_req_put, rc = -DER_NOMEM);

	arg->rpc = rpc;
	arg->hdl = poh;
	arg->pool = pool;

	rc = daos_task_register_comp_cb(task, dct_fetch_cb, arg);
	if (rc != 0)
		D_GOTO(out_arg, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, task);

	D_DEBUG(DF_MISC, "leaving dct_fetch()\n");

	return rc;

out_arg:
	D_FREE_PTR(arg);
out_req_put:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_task:
	daos_task_complete(task, rc);
	return rc;
}
