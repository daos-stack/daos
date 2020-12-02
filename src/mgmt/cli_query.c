/*
 * (C) Copyright 2020 Intel Corporation.
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
 * Storage query methods
 */

#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/mgmt.h>
#include <daos/event.h>
#include <daos/rsvc.h>
#include <daos_api.h>
#include <daos_security.h>
#include "rpc.h"

struct mgmt_get_bs_state_arg {
	struct dc_mgmt_sys	*sys;
	crt_rpc_t		*rpc;
	int			*state;
};

static int
mgmt_get_bs_state_cp(tse_task_t *task, void *data)
{
	struct mgmt_get_bs_state_arg	*arg;
	struct mgmt_get_bs_state_out	*bs_out;
	struct mgmt_get_bs_state_in	*bs_in;
	int				 rc = task->dt_result;

	arg = (struct mgmt_get_bs_state_arg *)data;

	if (rc) {
		D_ERROR("RPC error while querying blobstore state: "DF_RC"\n",
			DP_RC(rc));
		D_GOTO(out, rc);
	}

	bs_out = crt_reply_get(arg->rpc);
	D_ASSERT(bs_out != NULL);
	rc = bs_out->bs_rc;
	if (rc) {
		D_ERROR("MGMT_GET_BS_STATE replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}
	*arg->state = bs_out->bs_state;


	bs_in = crt_req_get(arg->rpc);
	D_ASSERT(bs_in != NULL);

out:
	dc_mgmt_sys_detach(arg->sys);
	crt_req_decref(arg->rpc);

	return rc;
}

int
dc_mgmt_get_bs_state(tse_task_t *task)
{
	daos_mgmt_get_bs_state_t	*args;
	crt_endpoint_t			 svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_opcode_t			 opc;
	struct mgmt_get_bs_state_in	*bs_in;
	struct mgmt_get_bs_state_arg	 cb_args;
	int				 rc;


	args = dc_task_get_args(task);

	rc = dc_mgmt_sys_attach(args->grp, &cb_args.sys);
	if (rc != 0) {
		D_ERROR("cannot attach to DAOS system: %s\n", args->grp);
		D_GOTO(out, rc);
	}

	svr_ep.ep_grp = cb_args.sys->sy_group;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_GET_BS_STATE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);

	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_GET_BS_STATE failed, rc: %d.\n",
			rc);
		D_GOTO(out_grp, rc);
	}

	D_ASSERT(rpc_req != NULL);
	bs_in = crt_req_get(rpc_req);
	D_ASSERT(bs_in != NULL);

	/** fill in request buffer */
	uuid_copy(bs_in->bs_uuid, args->uuid);

	crt_req_addref(rpc_req);
	cb_args.rpc = rpc_req;
	cb_args.state = args->state;

	rc = tse_task_register_comp_cb(task, mgmt_get_bs_state_cp, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DB_MGMT, "getting internal blobstore state in DAOS system:%s\n",
		args->grp);

	/** send the request */
	return daos_rpc_send(rpc_req, task);

out_put_req:
	crt_req_decref(rpc_req);
	crt_req_decref(rpc_req);

out_grp:
	dc_mgmt_sys_detach(cb_args.sys);

out:
	tse_task_complete(task, rc);
	return rc;
}
