/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
/**
 * \file
 *
 * debug apis for DAOS  client library. It exports the debug APIs defined in
 * cli_debug.h
 */

#include <daos/mgmt.h>
#include <daos/sys_debug.h>

#include <daos/agent.h>
#include <daos/drpc_modules.h>
#include <daos/drpc.pb-c.h>
#include <daos/event.h>
#include <daos/job.h>
#include "rpc.h"
#include <errno.h>

int
dc_debug_set_params(tse_task_t *task)
{
	daos_set_params_t		*args;
	struct cp_arg			cp_arg;
	struct mgmt_params_set_in	*in;
	crt_endpoint_t			ep;
	crt_rpc_t			*rpc = NULL;
	crt_opcode_t			opc;
	int				rc;

	args = dc_task_get_args(task);
	rc = dc_mgmt_sys_attach(args->grp, &cp_arg.sys);
	if (rc != 0) {
		D_ERROR("failed to attach to grp %s, rc "DF_RC"\n", args->grp,
			DP_RC(rc));
		rc = -DER_INVAL;
		goto out_task;
	}

	ep.ep_grp = cp_arg.sys->sy_group;
	/* if rank == -1 means it will set params on all servers, which we will
	 * send it to 0 temporarily.
	 */
	ep.ep_rank = args->rank == -1 ? 0 : args->rank;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_PARAMS_SET, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_SVC_RIP) failed, rc: "DF_RC"\n",
			DP_RC(rc));
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

	crt_req_addref(rpc);
	cp_arg.rpc = rpc;

	rc = tse_task_register_comp_cb(task, dc_cp, &cp_arg, sizeof(cp_arg));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	D_DEBUG(DB_MGMT, "set parameter %d/%u/"DF_U64"\n", args->rank,
		args->key_id, args->value);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	dc_mgmt_sys_detach(cp_arg.sys);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_debug_add_mark(const char *mark)
{
	struct dc_mgmt_sys	*sys;
	struct mgmt_mark_in	*in;
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		opc;
	int			rc;

	rc = dc_mgmt_sys_attach(NULL, &sys);
	if (rc != 0) {
		D_ERROR("failed to attach to grp rc "DF_RC"\n", DP_RC(rc));
		return -DER_INVAL;
	}

	ep.ep_grp = sys->sy_group;
	ep.ep_rank = 0;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_MARK, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_get_crt_ctx(), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create failed, rc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	in = crt_req_get(rpc);
	in->m_mark = (char *)mark;
	/** send the request */
	rc = daos_rpc_send_wait(rpc);
err_grp:
	D_DEBUG(DB_MGMT, "mgmt mark: rc "DF_RC"\n", DP_RC(rc));
	dc_mgmt_sys_detach(sys);
	return rc;

}
