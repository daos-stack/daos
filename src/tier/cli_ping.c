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
 * dctc_ping: Client portion of ping test
 */
#define DDSUBSYS	DDFAC(tier)

#include <daos_types.h>
#include <daos_tier.h>
#include "rpc.h"

struct tier_ping_arg {
	crt_rpc_t               *rpc;
};

static int
tier_ping_cb(tse_task_t *task, void *data)
{
	struct tier_ping_arg	*arg = (struct tier_ping_arg *)data;
	crt_rpc_t		*rpc = arg->rpc;
	struct tier_ping_out	*out;
	int                     rc = task->dt_result;

	D__DEBUG(DF_MISC, "Entering tier_ping_cb\n");

	/* extract the RPC reply */
	out = crt_reply_get(rpc);

	D__DEBUG(DF_MISC, "DCT Ping Return Val %d\n", out->ping_out);

	D__DEBUG(DF_MISC, "Leaving tier_ping_cb()");

	crt_req_decref(rpc);
	return rc;
}

int
dc_tier_ping(uint32_t ping_val, tse_task_t *task)
{

	D__DEBUG(DF_MISC, "Entering daos_tier_ping()\n");

	struct tier_ping_in	*in;
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc;
	struct tier_ping_arg    arg;
	int			rc;

	D__DEBUG(DF_MISC, "Ping Val to Issue: %d\n", ping_val);

	/* Harded coded enpoint stuff */
	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	/* Create RPC and allocate memory for the various field-eybops */
	rc = tier_req_create(daos_task2ctx(task), &ep, TIER_PING, &rpc);
	if (rc != 0)
		D__GOTO(out_task, rc);

	/* Grab the input struct of the RPC */
	in = crt_req_get(rpc);

	/* set the value we want to send out */
	in->ping_in = ping_val;

	crt_req_addref(rpc);

	arg.rpc = rpc;

	rc = tse_task_register_comp_cb(task, tier_ping_cb, &arg, sizeof(arg));
	if (rc != 0)
		D__GOTO(out_req_put, rc);

	/** send the request */
	rc = daos_rpc_send(rpc, task);

	D__DEBUG(DF_MISC, "leaving daos_tier_ping()\n");

	return rc;

out_req_put:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
out_task:
	tse_task_complete(task, rc);
	return rc;
}
