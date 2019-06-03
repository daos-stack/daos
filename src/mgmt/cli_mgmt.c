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
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/mgmt.h>
#include <daos/event.h>
#include "rpc.h"

static int
rip_cp(tse_task_t *task, void *data)
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
dc_mgmt_svc_rip(tse_task_t *task)
{
	daos_svc_rip_t		*args;
	crt_endpoint_t		 svr_ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		 opc;
	struct mgmt_svc_rip_in	*rip_in;
	int			 rc;

	args = dc_task_get_args(task);
	rc = daos_group_attach(args->grp, &svr_ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to grp %s, rc %d.\n", args->grp, rc);
		rc = DER_INVAL;
		goto out_task;
	}

	svr_ep.ep_rank = args->rank;
	svr_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_SVC_RIP, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_SVC_RIP) failed, rc: %d.\n",
			rc);
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	rip_in = crt_req_get(rpc);
	D_ASSERT(rip_in != NULL);

	/** fill in request buffer */
	rip_in->rip_flags = args->force;

	rc = tse_task_register_comp_cb(task, rip_cp, &rpc, sizeof(rpc));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	crt_req_addref(rpc); /** for rip_cp */
	D_DEBUG(DB_MGMT, "killing rank %u\n", args->rank);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	daos_group_detach(svr_ep.ep_grp);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_mgmt_set_params(tse_task_t *task)
{
	daos_set_params_t		*args;
	struct mgmt_params_set_in	*in;
	crt_endpoint_t			ep;
	crt_rpc_t			*rpc = NULL;
	crt_opcode_t			opc;
	int				rc;

	args = dc_task_get_args(task);
	rc = daos_group_attach(args->grp, &ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to grp %s, rc %d.\n", args->grp, rc);
		rc = -DER_INVAL;
		goto out_task;
	}

	/* if rank == -1 means it will set params on all servers, which we will
	 * send it to 0 temporarily.
	 */
	ep.ep_rank = args->rank == -1 ? 0 : args->rank;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_PARAMS_SET, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_SVC_RIP) failed, rc: %d.\n",
			rc);
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	/** fill in request buffer */
	in->ps_rank = args->rank;
	in->ps_key_id = args->key_id;
	in->ps_value = args->value;
	in->ps_value_extra = args->value_extra;

	rc = tse_task_register_comp_cb(task, rip_cp, &rpc, sizeof(rpc));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	crt_req_addref(rpc); /** for rip_cp */
	D_DEBUG(DB_MGMT, "set parameter %d/%u/"DF_U64".\n", args->rank,
		 args->key_id, args->value);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	daos_group_detach(ep.ep_grp);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_mgmt_profile(uint64_t modules, char *path, bool start)
{
	struct mgmt_profile_in	*in;
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		opc;
	int			rc;

	rc = daos_group_attach(NULL, &ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to grp rc %d.\n", rc);
		return -DER_INVAL;
	}

	ep.ep_rank = 0;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_PROFILE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_get_crt_ctx(), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create failed, rc: %d.\n", rc);
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	in = crt_req_get(rpc);
	in->p_module = modules;
	in->p_path = path;
	in->p_op = start ? MGMT_PROFILE_START : MGMT_PROFILE_STOP;
	/** send the request */
	rc = daos_rpc_send_wait(rpc);
err_grp:
	D_DEBUG(DB_MGMT, "mgmt profile: rc %d\n", rc);
	daos_group_detach(ep.ep_grp);
	return rc;
}

/**
 * Initialize management interface
 */
int
dc_mgmt_init()
{
	int rc;

	rc = daos_rpc_register(&mgmt_proto_fmt, MGMT_PROTO_CLI_COUNT,
				NULL, DAOS_MGMT_MODULE);
	if (rc != 0)
		D_ERROR("failed to register mgmt RPCs: %d\n", rc);

	return rc;
}

/**
 * Finalize management interface
 */
void
dc_mgmt_fini()
{
	daos_rpc_unregister(&mgmt_proto_fmt);
}

int dc2_mgmt_svc_rip(tse_task_t *task)
{
	return -DER_NOSYS;
}
