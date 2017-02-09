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
 * Pool create/destroy methods
 */
#define DD_SUBSYS	DD_FAC(mgmt)

#include <daos/mgmt.h>
#include <daos/event.h>
#include "rpc.h"

struct pool_create_arg {
	crt_rpc_t               *rpc;
	daos_rank_list_t	*svc;
};

static int
pool_create_cp(struct daos_task *task, void *data)
{
	struct pool_create_arg		*arg = (struct pool_create_arg *)data;
	daos_rank_list_t		*svc = arg->svc;
	struct mgmt_pool_create_out	*pc_out;
	int				rc = task->dt_result;

	if (rc) {
		D_ERROR("RPC error while disconnecting from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pc_out = crt_reply_get(arg->rpc);
	rc = pc_out->pc_rc;
	if (rc) {
		D_ERROR("MGMT_POOL_CREATE replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	/** report list of targets running the metadata service */
	daos_rank_list_copy(svc, pc_out->pc_svc, false);
out:
	daos_group_detach(arg->rpc->cr_ep.ep_grp);
	crt_req_decref(arg->rpc);
	D_FREE_PTR(arg);
	return rc;
}

int
dc_pool_create(unsigned int mode, unsigned int uid, unsigned int gid,
	       const char *grp, const daos_rank_list_t *tgts, const char *dev,
	       daos_size_t size, daos_rank_list_t *svc, uuid_t uuid,
	       struct daos_task *task)
{
	crt_endpoint_t			svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_opcode_t			opc;
	struct mgmt_pool_create_in	*pc_in;
	struct pool_create_arg		*arg;
	int				rc = 0;

	if (dev == NULL || strlen(dev) == 0) {
		D_ERROR("Invalid parameter of dev (NULL or empty string).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	uuid_generate(uuid);

	rc = daos_group_attach(grp, &svr_ep.ep_grp);
	if (rc != 0)
		D_GOTO(out, rc);

	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(MGMT_POOL_CREATE, DAOS_MGMT_MODULE, 1);
	rc = crt_req_create(daos_task2ctx(task), svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_POOL_CREATE) failed, rc: %d.\n",
			rc);
		D_GOTO(out_grp, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

	/** fill in request buffer */
	uuid_copy(pc_in->pc_pool_uuid, uuid);
	pc_in->pc_mode = mode;
	pc_in->pc_uid = uid;
	pc_in->pc_gid = gid;
	pc_in->pc_grp = (crt_string_t)grp;
	pc_in->pc_tgt_dev = (crt_string_t)dev;
	pc_in->pc_tgts = (daos_rank_list_t *)tgts;
	pc_in->pc_tgt_size = size;
	pc_in->pc_svc_nr = svc->rl_nr.num;

	D_ALLOC_PTR(arg);
	if (arg == NULL)
		D_GOTO(out_put_req, rc = -DER_NOMEM);

	crt_req_addref(rpc_req);
	arg->rpc = rpc_req;
	arg->svc = svc;

	rc = daos_task_register_comp_cb(task, pool_create_cp, arg);
	if (rc != 0)
		D_GOTO(out_arg, rc);

	D_DEBUG(DB_MGMT, DF_UUID": creating pool\n", DP_UUID(uuid));

	/** send the request */
	return daos_rpc_send(rpc_req, task);

out_arg:
	D_FREE_PTR(arg);
	crt_req_decref(rpc_req);
out_put_req:
	crt_req_decref(rpc_req);
out_grp:
	daos_group_detach(svr_ep.ep_grp);
out:
	daos_task_complete(task, rc);
	return rc;
}

static int
pool_destroy_cp(struct daos_task *task, void *data)
{
	crt_rpc_t			*rpc = (crt_rpc_t *)data;
	struct mgmt_pool_destroy_out	*pd_out;
	int				rc = task->dt_result;

	if (rc) {
		D_ERROR("RPC error while destroying pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pd_out = crt_reply_get(rpc);
	rc = pd_out->pd_rc;
	if (rc) {
		D_ERROR("MGMT_POOL_DESTROY replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	daos_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

int
dc_pool_destroy(const uuid_t uuid, const char *grp, int force,
		struct daos_task *task)
{
	crt_endpoint_t			 svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_opcode_t			 opc;
	struct mgmt_pool_destroy_in	*pd_in;
	int				 rc = 0;

	if (uuid_is_null(uuid)) {
		D_ERROR("Invalid parameter of uuid (NULL).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_group_attach(grp, &svr_ep.ep_grp);
	if (rc != 0)
		D_GOTO(out, rc);

	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(MGMT_POOL_DESTROY, DAOS_MGMT_MODULE, 1);
	rc = crt_req_create(daos_task2ctx(task), svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_POOL_DESTROY) failed, rc: %d.\n",
			rc);
		D_GOTO(out_group, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pd_in = crt_req_get(rpc_req);
	D_ASSERT(pd_in != NULL);

	/** fill in request buffer */
	uuid_copy(pd_in->pd_pool_uuid, uuid);
	pd_in->pd_grp = (crt_string_t)grp;
	pd_in->pd_force = (force == 0) ? false : true;

	crt_req_addref(rpc_req);

	rc = daos_task_register_comp_cb(task, pool_destroy_cp, rpc_req);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DB_MGMT, DF_UUID": destroying pool\n", DP_UUID(uuid));

	/** send the request */
	return daos_rpc_send(rpc_req, task);

out_put_req:
	/** dec ref taken for task args */
	crt_req_decref(rpc_req);
	/** dec ref taken for crt_req_create */
	crt_req_decref(rpc_req);
out_group:
	daos_group_detach(svr_ep.ep_grp);
out:
	daos_task_complete(task, rc);
	return rc;
}
