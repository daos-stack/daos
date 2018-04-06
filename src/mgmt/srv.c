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
/**
 * This file is part of the DAOS server. It implements the DAOS storage
 * management interface that covers:
 * - storage detection;
 * - storage allocation;
 * - DAOS pool initialization.
 *
 * The management server is a first-class server module (like object/pool
 * server-side library) and can be unloaded/reloaded.
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include "srv_internal.h"
#include <signal.h>

static struct daos_rpc_handler ds_mgmt_handlers[] = {
	{
		.dr_opc		= MGMT_POOL_CREATE,
		.dr_hdlr	= ds_mgmt_hdlr_pool_create,
	}, {
		.dr_opc		= MGMT_POOL_DESTROY,
		.dr_hdlr	= ds_mgmt_hdlr_pool_destroy,
	}, {
		.dr_opc		= MGMT_TGT_CREATE,
		.dr_hdlr	= ds_mgmt_hdlr_tgt_create,
		.dr_corpc_ops	= {
			.co_aggregate	= ds_mgmt_tgt_create_aggregator,
			.co_pre_forward	= NULL,
		}
	}, {
		.dr_opc		= MGMT_TGT_DESTROY,
		.dr_hdlr	= ds_mgmt_hdlr_tgt_destroy,
	}, {
		.dr_opc		= MGMT_SVC_RIP,
		.dr_hdlr	= ds_mgmt_hdlr_svc_rip,
	}, {
		.dr_opc		= MGMT_PARAMS_SET,
		.dr_hdlr	= ds_mgmt_params_set_hdlr,
	}, {
		.dr_opc		= MGMT_TGT_PARAMS_SET,
		.dr_hdlr	= ds_mgmt_tgt_params_set_hdlr,
	}, {
		.dr_opc = 0,
	}
};

/**
 * Set parameter on a single target.
 */
void
ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_tgt_params_set_in	*in;
	struct mgmt_srv_out		*out;
	int rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	rc = dss_parameters_set(in->tps_key_id, in->tps_value);
	if (rc)
		D_ERROR("Set parameter failed key_id %d: rc %d\n",
			 in->tps_key_id, rc);

	out = crt_reply_get(rpc);
	out->srv_rc = rc;
	crt_reply_send(rpc);
}

/**
 * Set parameter on all of server targets, for testing or other
 * purpose.
 */
void
ds_mgmt_params_set_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_params_set_in	*ps_in;
	crt_opcode_t			opc;
	int				topo;
	crt_rpc_t			*tc_req;
	struct mgmt_tgt_params_set_in	*tc_in;
	struct mgmt_srv_out		*out;
	int				rc;

	ps_in = crt_req_get(rpc);
	D_ASSERT(ps_in != NULL);
	if (ps_in->ps_rank != -1) {
		/* Only set local parameter */
		rc = dss_parameters_set(ps_in->ps_key_id, ps_in->ps_value);
		if (rc)
			D_ERROR("Set parameter failed key_id %d: rc %d\n",
				ps_in->ps_key_id, rc);
		D_GOTO(out, rc);
	}

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_PARAMS_SET, DAOS_MGMT_MODULE, 1);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(out, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);

	tc_in->tps_key_id = ps_in->ps_key_id;
	tc_in->tps_value = ps_in->ps_value;

	rc = dss_rpc_send(tc_req);
	if (rc != 0) {
		crt_req_decref(tc_req);
		D_GOTO(out, rc);
	}

	out = crt_reply_get(tc_req);
	rc = out->srv_rc;
	if (rc != 0) {
		crt_req_decref(tc_req);
		D_GOTO(out, rc);
	}
out:
	out = crt_reply_get(rpc);
	out->srv_rc = rc;
	crt_reply_send(rpc);
}

void
ds_mgmt_hdlr_svc_rip(crt_rpc_t *rpc)
{
	struct mgmt_svc_rip_in	*murderer;
	int			 sig;
	bool			 force;
	d_rank_t		 rank = -1;

	murderer = crt_req_get(rpc);
	if (murderer == NULL)
		return;

	force = (murderer->rip_flags != 0);

	/*
	 * the yield below is to workaround an ofi err msg at client-side -
	 * fi_cq_readerr got err: 5(Input/output error) ..
	 */
	int i;
	for (i = 0; i < 200; i++) {
		ABT_thread_yield();
		usleep(10);
	}

	/** ... adieu */
	if (force)
		sig = SIGKILL;
	else
		sig = SIGTERM;
	crt_group_rank(NULL, &rank);
	D_PRINT("Service rank %d is being killed by signal %d... farewell\n",
		rank, sig);
	kill(getpid(), sig);
}

static int
ds_mgmt_init()
{
	int rc;

	rc = ds_mgmt_tgt_init();
	if (rc)
		return rc;

	D_DEBUG(DB_MGMT, "successfull init call\n");
	return 0;
}

static int
ds_mgmt_fini()
{
	ds_mgmt_tgt_fini();
	D_DEBUG(DB_MGMT, "successfull fini call\n");
	return 0;
}

struct dss_module mgmt_module = {
	.sm_name	= "mgmt",
	.sm_mod_id	= DAOS_MGMT_MODULE,
	.sm_ver		= 1,
	.sm_init	= ds_mgmt_init,
	.sm_fini	= ds_mgmt_fini,
	.sm_cl_rpcs	= mgmt_rpcs,
	.sm_srv_rpcs	= mgmt_srv_rpcs,
	.sm_handlers	= ds_mgmt_handlers,
};
