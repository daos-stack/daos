/**
 * (C) Copyright 2016-2019 Intel Corporation.
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

/** Fully qualified path to daos_server socket */
char *ds_mgmt_server_socket_path;

static struct crt_corpc_ops ds_mgmt_hdlr_tgt_create_co_ops = {
	.co_aggregate	= ds_mgmt_tgt_create_aggregator,
	.co_pre_forward	= NULL,
};

/* Define for cont_rpcs[] array population below.
 * See MGMT_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.dr_opc       = a,	\
	.dr_hdlr      = d,	\
	.dr_corpc_ops = e,	\
}

static struct daos_rpc_handler mgmt_handlers[] = {
	MGMT_PROTO_CLI_RPC_LIST,
	MGMT_PROTO_SRV_RPC_LIST,
};

#undef X

static void
mgmt_drpc_handler(Drpc__Call *request, Drpc__Response **response)
{
	D_DEBUG(DB_MGMT, "call received by mgmt drpc handler\n");
}

static struct dss_drpc_handler mgmt_drpc_handlers[] = {
	{
		.module_id = DRPC_MODULE_MGMT_SERVER,
		.handler = mgmt_drpc_handler
	},
	{
		.module_id = 0,
		.handler = NULL
	}
};

/**
 * Set parameter on a single target.
 */
void
ds_mgmt_tgt_params_set_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_tgt_params_set_in	*in;
	struct mgmt_tgt_params_set_out	*out;
	int rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	rc = dss_parameters_set(in->tps_key_id, in->tps_value);
	if (rc == 0 && in->tps_key_id == DSS_KEY_FAIL_LOC)
		rc = dss_parameters_set(DSS_KEY_FAIL_VALUE,
					in->tps_value_extra);
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
	struct mgmt_params_set_out	*out;
	int				rc;

	ps_in = crt_req_get(rpc);
	D_ASSERT(ps_in != NULL);
	if (ps_in->ps_rank != -1) {
		/* Only set local parameter */
		rc = dss_parameters_set(ps_in->ps_key_id, ps_in->ps_value);
		if (rc == 0 && ps_in->ps_key_id == DSS_KEY_FAIL_LOC)
			rc = dss_parameters_set(DSS_KEY_FAIL_VALUE,
						ps_in->ps_value_extra);
		if (rc)
			D_ERROR("Set parameter failed key_id %d: rc %d\n",
				ps_in->ps_key_id, rc);
		D_GOTO(out, rc);
	}

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_PARAMS_SET, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(out, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);

	tc_in->tps_key_id = ps_in->ps_key_id;
	tc_in->tps_value = ps_in->ps_value;
	tc_in->tps_value_extra = ps_in->ps_value_extra;

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

	rc = asprintf(&ds_mgmt_server_socket_path, "%s/%s",
			dss_socket_dir, "daos_server.sock");
	if (rc < 0) {
		return rc;
	}

	D_DEBUG(DB_MGMT, "successfull init call\n");
	return 0;
}

static int
ds_mgmt_fini()
{
	ds_mgmt_tgt_fini();
	free(ds_mgmt_server_socket_path);
	ds_mgmt_server_socket_path = NULL;
	D_DEBUG(DB_MGMT, "successfull fini call\n");
	return 0;
}

struct dss_module mgmt_module = {
	.sm_name		= "mgmt",
	.sm_mod_id		= DAOS_MGMT_MODULE,
	.sm_ver			= DAOS_MGMT_VERSION,
	.sm_init		= ds_mgmt_init,
	.sm_fini		= ds_mgmt_fini,
	.sm_proto_fmt		= &mgmt_proto_fmt,
	.sm_cli_count		= MGMT_PROTO_CLI_COUNT,
	.sm_handlers		= mgmt_handlers,
	.sm_drpc_handlers	= mgmt_drpc_handlers,
};
