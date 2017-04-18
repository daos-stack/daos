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
#define DD_SUBSYS	DD_FAC(mgmt)

#include <daos/mgmt.h>
#include <daos/event.h>
#include "rpc.h"

static int
rip_cp(struct daos_task *task, void *data)
{
	crt_rpc_t		*rpc = *((crt_rpc_t **)data);
	int                      rc = task->dt_result;

	if (rc)
		D_ERROR("RPC error while killing rank: %d\n", rc);

	daos_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

int
dc_mgmt_svc_rip(const char *grp, daos_rank_t rank, bool force,
		struct daos_task *task)
{
	crt_endpoint_t		 svr_ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		 opc;
	struct mgmt_svc_rip_in	*rip_in;
	int			 rc;

	rc = daos_group_attach(grp, &svr_ep.ep_grp);
	if (rc != 0)
		return rc;

	svr_ep.ep_rank = rank;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(MGMT_SVC_RIP, DAOS_MGMT_MODULE, 1);
	rc = crt_req_create(daos_task2ctx(task), svr_ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_SVC_RIP) failed, rc: %d.\n",
			rc);
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	rip_in = crt_req_get(rpc);
	D_ASSERT(rip_in != NULL);

	/** fill in request buffer */
	rip_in->rip_flags = force;

	rc = daos_task_register_comp_cb(task, rip_cp, sizeof(rpc), &rpc);
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	crt_req_addref(rpc); /** for rip_cp */
	D_DEBUG(DB_MGMT, "killing rank %u\n", rank);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	daos_group_detach(svr_ep.ep_grp);
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
