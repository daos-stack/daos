/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/mgmt.h>
#include <daos/event.h>
#include "rpc.h"

struct pool_create_arg {
	crt_rpc_t	*rpc;
	d_rank_list_t	*svc;
};

static int
pool_create_cp(tse_task_t *task, void *data)
{
	struct pool_create_arg		*arg = (struct pool_create_arg *)data;
	d_rank_list_t			*svc = arg->svc;
	struct mgmt_pool_create_out	*pc_out;
	int				 rc = task->dt_result;

	if (rc) {
		D_ERROR("RPC error while creating pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pc_out = crt_reply_get(arg->rpc);
	rc = pc_out->pc_rc;
	if (rc) {
		D_ERROR("MGMT_POOL_CREATE replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	/*
	 * Report the actual list of pool service replicas. Don't use
	 * daos_rank_list_copy, since svc->rl_ranks is from the user and may be
	 * unreallocable.
	 */
	if (pc_out->pc_svc->rl_nr > svc->rl_nr) {
		D_ERROR("more pool service replicas created (%u) than "
			"requested (%u)\n", pc_out->pc_svc->rl_nr, svc->rl_nr);
		rc = -DER_PROTO;
		goto out;
	}
	svc->rl_nr = pc_out->pc_svc->rl_nr;
	memcpy(svc->rl_ranks, pc_out->pc_svc->rl_ranks,
	       sizeof(*svc->rl_ranks) * svc->rl_nr);

out:
	daos_group_detach(arg->rpc->cr_ep.ep_grp);
	crt_req_decref(arg->rpc);
	return rc;
}

int
dc_pool_create(tse_task_t *task)
{
	daos_pool_create_t		*args;
	crt_endpoint_t			svr_ep;
	crt_rpc_t		       *rpc_req = NULL;
	crt_opcode_t			opc;
	struct mgmt_pool_create_in     *pc_in;
	struct pool_create_arg		create_args;
	int				rc = 0;

	args = dc_task_get_args(task);
	if (!args->uuid || args->dev == NULL || strlen(args->dev) == 0) {
		D_ERROR("Invalid parameter of dev (NULL or empty string).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	uuid_generate(args->uuid);

	rc = daos_group_attach(args->grp, &svr_ep.ep_grp);
	if (rc != 0)
		D_GOTO(out, rc);

	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_POOL_CREATE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_POOL_CREATE) failed, rc: %d.\n",
			rc);
		D_GOTO(out_grp, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

	/** fill in request buffer */
	uuid_copy(pc_in->pc_pool_uuid, args->uuid);
	pc_in->pc_mode = args->mode;
	pc_in->pc_uid = args->uid;
	pc_in->pc_gid = args->gid;
	pc_in->pc_grp = (d_string_t)args->grp;
	pc_in->pc_tgt_dev = (d_string_t)args->dev;
	pc_in->pc_tgts = (d_rank_list_t *)args->tgts;
	pc_in->pc_scm_size = args->scm_size;
	pc_in->pc_nvme_size = args->nvme_size;
	pc_in->pc_prop = args->prop;
	pc_in->pc_svc_nr = args->svc->rl_nr;

	crt_req_addref(rpc_req);
	create_args.rpc = rpc_req;
	create_args.svc = args->svc;

	rc = tse_task_register_comp_cb(task, pool_create_cp, &create_args,
				       sizeof(create_args));
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DB_MGMT, DF_UUID": creating pool\n", DP_UUID(args->uuid));

	/** send the request */
	return daos_rpc_send(rpc_req, task);

out_put_req:
	crt_req_decref(rpc_req);
	crt_req_decref(rpc_req);
out_grp:
	daos_group_detach(svr_ep.ep_grp);
out:
	tse_task_complete(task, rc);
	return rc;
}

static int
pool_destroy_cp(tse_task_t *task, void *data)
{
	crt_rpc_t			*rpc = *((crt_rpc_t **)data);
	struct mgmt_pool_destroy_out	*pd_out;
	int				 rc = task->dt_result;

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
dc_pool_destroy(tse_task_t *task)
{
	daos_pool_destroy_t		*args;
	crt_endpoint_t			 svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_opcode_t			 opc;
	struct mgmt_pool_destroy_in	*pd_in;
	int				 rc = 0;

	args = dc_task_get_args(task);
	if (!daos_uuid_valid(args->uuid)) {
		D_ERROR("Invalid parameter of uuid (NULL).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_group_attach(args->grp, &svr_ep.ep_grp);
	if (rc != 0)
		D_GOTO(out, rc);

	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_POOL_DESTROY, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_POOL_DESTROY) failed, rc: %d.\n",
			rc);
		D_GOTO(out_group, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pd_in = crt_req_get(rpc_req);
	D_ASSERT(pd_in != NULL);

	/** fill in request buffer */
	uuid_copy(pd_in->pd_pool_uuid, args->uuid);
	pd_in->pd_grp = (d_string_t)args->grp;
	pd_in->pd_force = (args->force == 0) ? false : true;

	crt_req_addref(rpc_req);
	rc = tse_task_register_comp_cb(task, pool_destroy_cp, &rpc_req,
				       sizeof(rpc_req));
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DB_MGMT, DF_UUID": destroying pool\n", DP_UUID(args->uuid));

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
	tse_task_complete(task, rc);
	return rc;
}
