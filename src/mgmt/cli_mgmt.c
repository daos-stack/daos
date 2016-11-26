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
 * DAOS management client library. It exports the mgmt API defined in
 * daos_mgmt.h
 */

#include "rpc.h"

static int
rip_cp(void *arg, daos_event_t *ev, int rc)
{
	crt_rpc_t		*rpc = arg;
	struct mgmt_svc_rip_out	*rip_out;

	if (rc) {
		D_ERROR("RPC error while killing rank: %d\n", rc);
		D_GOTO(out, rc);
	}

	rip_out = crt_reply_get(rpc);
	rc = rip_out->rip_rc;
	if (rc) {
		D_ERROR("MGMT_SVC_RIP replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	crt_req_decref(rpc);
	return rc;
}

int
dc_mgmt_svc_rip(const char *grp, daos_rank_t rank, bool force,
		daos_event_t *ev)
{
	crt_endpoint_t		 svr_ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		 opc;
	struct mgmt_svc_rip_in	*rip_in;
	int			 rc = 0;

	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = rank;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(MGMT_SVC_RIP, DAOS_MGMT_MODULE, 1);
	rc = crt_req_create(daos_ev2ctx(ev), svr_ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_SVC_RIP) failed, rc: %d.\n",
			rc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc != NULL);
	rip_in = crt_req_get(rpc);
	D_ASSERT(rip_in != NULL);

	/** fill in request buffer */
	rip_in->rip_flags = force;

	rc = daos_event_register_comp_cb(ev, rip_cp, rpc);
	if (rc != 0) {
		crt_req_decref(rpc);
		D_GOTO(out, rc);
	}

	rc = daos_event_launch(ev);
	if (rc != 0) {
		crt_req_decref(rpc);
		D_GOTO(out, rc);
	}

	crt_req_addref(rpc); /** for rip_cp */
	D_DEBUG(DF_MGMT, "killing rank %u\n", rank);

	/** send the request */
	rc = daos_rpc_send(rpc, ev);
out:
	return rc;
}

/**
 * Initialize management interface
 */
int
dc_mgmt_init()
{
	int rc;

	rc = daos_rpc_register(mgmt_rpcs, NULL, DAOS_MGMT_MODULE);
	if (rc != 0)
		D_ERROR("failed to register rpcs: %d\n", rc);

	return rc;
}

/**
 * Finalize management interface
 */
void
dc_mgmt_fini()
{
	daos_rpc_unregister(mgmt_rpcs);
}
