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
#define DD_SUBSYS	DD_FAC(tier)

#include <daos_types.h>
#include <daos_tier.h>
#include "dct_rpc.h"

static int
dct_ping_cb(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp *sp = arg;
	struct dct_ping_out *out;

	D_DEBUG(DF_MISC, "Entering dct_ping_cb\n");

	/* extract the RPC reply */
	out = crt_reply_get(sp->sp_rpc);

	D_DEBUG(DF_MISC, "DCT Ping Return Val %d\n", out->ping_out);

	D_DEBUG(DF_MISC, "Leaving dct_ping_cb()");

	return rc;
}

int
dc_tier_ping(uint32_t ping_val, daos_event_t *ev)
{

	D_DEBUG(DF_MISC, "Entering dct_ping()\n");

	struct dct_ping_in	*in;
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc;
	int			rc;
	struct daos_op_sp      *sp;


	D_DEBUG(DF_MISC, "Ping Val to Issue: %d\n", ping_val);

	/* Harded coded enpoint stuff */
	ep.ep_grp = NULL;
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	/* Create RPC and allocate memory for the various field-eybops */
	rc = dct_req_create(daos_ev2ctx(ev), ep, DCT_PING, &rpc);

	/* Grab the input struct of the RPC */
	in = crt_req_get(rpc);

	/* set the value we want to send out */
	in->ping_in = ping_val;

	/*
	 * Get the "scratch pad" data affiliated with this RPC
	 * used to maintain per-call invocation state (I think)
	 */
	sp = daos_ev2sp(ev);
	crt_req_addref(rpc);
	sp->sp_rpc = rpc;

	rc = daos_event_register_comp_cb(ev, dct_ping_cb, sp);
	if (rc != 0)
		D_GOTO(out_req_put, rc);

	/* Mark the event as inflight and register our various callbacks */
	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(out_req_put, rc);

	/*
	 * If we fail, decrement the ref count....twice? Mimicking pattern seen
	 * elsewhere
	 */

	/* And now actually issue the darn RPC */
	rc = daos_rpc_send(rpc, ev);
	D_DEBUG(DF_MISC, "leaving dct_ping()\n");

	return rc;

out_req_put:
	crt_req_decref(rpc);
	crt_req_decref(rpc);
	return rc;
}
