/**
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
 * This file is part of the DAOS server. It implements the DAOS storage
 * management interface that covers:
 * - storage detection;
 * - storage allocation;
 * - storage health query
 * - DAOS pool initialization.
 *
 * The management server is a first-class server module (like object/pool
 * server-side library) and can be unloaded/reloaded.
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <signal.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/rsvc.h>
#include <daos/drpc_modules.h>
#include <daos_mgmt.h>

#include "srv_internal.h"
#include "drpc_internal.h"

const int max_svc_nreplicas = 13;

static struct crt_corpc_ops ds_mgmt_hdlr_tgt_create_co_ops = {
	.co_aggregate	= ds_mgmt_tgt_create_aggregator,
	.co_pre_forward	= NULL,
};

static struct crt_corpc_ops ds_mgmt_hdlr_tgt_map_update_co_ops = {
	.co_aggregate	= ds_mgmt_tgt_map_update_aggregator,
	.co_pre_forward	= ds_mgmt_tgt_map_update_pre_forward,
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
process_drpc_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	/**
	 * Process drpc request and populate daos response,
	 * command errors should be indicated inside daos response.
	 */
	switch (drpc_req->method) {
	case DRPC_METHOD_MGMT_PREP_SHUTDOWN:
		ds_mgmt_drpc_prep_shutdown(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_PING_RANK:
		ds_mgmt_drpc_ping_rank(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_RANK:
		ds_mgmt_drpc_set_rank(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CREATE_MS:
		ds_mgmt_drpc_create_mgmt_svc(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_START_MS:
		ds_mgmt_drpc_start_mgmt_svc(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_GET_ATTACH_INFO:
		ds_mgmt_drpc_get_attach_info(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_JOIN:
		ds_mgmt_drpc_join(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_CREATE:
		ds_mgmt_drpc_pool_create(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DESTROY:
		ds_mgmt_drpc_pool_destroy(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_EVICT:
		ds_mgmt_drpc_pool_evict(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_UP:
		ds_mgmt_drpc_set_up(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_EXCLUDE:
		ds_mgmt_drpc_pool_exclude(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DRAIN:
		ds_mgmt_drpc_pool_drain(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_REINTEGRATE:
		ds_mgmt_drpc_pool_reintegrate(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_EXTEND:
		ds_mgmt_drpc_pool_extend(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_BIO_HEALTH_QUERY:
		ds_mgmt_drpc_bio_health_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SMD_LIST_DEVS:
		ds_mgmt_drpc_smd_list_devs(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SMD_LIST_POOLS:
		ds_mgmt_drpc_smd_list_pools(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_STATE_QUERY:
		ds_mgmt_drpc_dev_state_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DEV_SET_FAULTY:
		ds_mgmt_drpc_dev_set_faulty(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_GET_ACL:
		ds_mgmt_drpc_pool_get_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_LIST_POOLS:
		ds_mgmt_drpc_list_pools(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_OVERWRITE_ACL:
		ds_mgmt_drpc_pool_overwrite_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_UPDATE_ACL:
		ds_mgmt_drpc_pool_update_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_DELETE_ACL:
		ds_mgmt_drpc_pool_delete_acl(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_LIST_CONTAINERS:
		ds_mgmt_drpc_pool_list_cont(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_SET_PROP:
		ds_mgmt_drpc_pool_set_prop(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_POOL_QUERY:
		ds_mgmt_drpc_pool_query(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CONT_SET_OWNER:
		ds_mgmt_drpc_cont_set_owner(drpc_req, drpc_resp);
		break;
	default:
		drpc_resp->status = DRPC__STATUS__UNKNOWN_METHOD;
		D_ERROR("Unknown method\n");
	}
}

static struct dss_drpc_handler mgmt_drpc_handlers[] = {
	{
		.module_id = DRPC_MODULE_MGMT,
		.handler = process_drpc_request
	},
	{
		.module_id = 0,
		.handler = NULL
	}
};

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
		if (rc == 0 && ps_in->ps_key_id == DMG_KEY_FAIL_LOC)
			rc = dss_parameters_set(DMG_KEY_FAIL_VALUE,
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

/**
 * Set parameter on all of server targets, for testing or other
 * purpose.
 */
void
ds_mgmt_profile_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_profile_in	*in;
	crt_opcode_t		opc;
	int			topo;
	crt_rpc_t		*tc_req;
	struct mgmt_profile_in	*tc_in;
	struct mgmt_profile_out	*out;
	int			rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_PROFILE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(out, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);

	tc_in->p_path = in->p_path;
	tc_in->p_op = in->p_op;
	tc_in->p_avg = in->p_avg;
	rc = dss_rpc_send(tc_req);
	if (rc != 0) {
		crt_req_decref(tc_req);
		D_GOTO(out, rc);
	}

	out = crt_reply_get(tc_req);
	rc = out->p_rc;
	if (rc != 0) {
		crt_req_decref(tc_req);
		D_GOTO(out, rc);
	}
out:
	out = crt_reply_get(rpc);
	D_DEBUG(DB_MGMT, "profile hdlr: rc "DF_RC"\n", DP_RC(rc));
	out->p_rc = rc;
	crt_reply_send(rpc);
}

/**
 * Set mark on all of server targets
 */
void
ds_mgmt_mark_hdlr(crt_rpc_t *rpc)
{
	struct mgmt_mark_in	*in;
	crt_opcode_t		opc;
	int			topo;
	crt_rpc_t		*tc_req;
	struct mgmt_mark_in	*tc_in;
	struct mgmt_mark_out	*out;
	int			rc;

	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	topo = crt_tree_topo(CRT_TREE_KNOMIAL, 32);
	opc = DAOS_RPC_OPCODE(MGMT_TGT_MARK, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_corpc_req_create(dss_get_module_info()->dmi_ctx, NULL, NULL,
				  opc, NULL, NULL, 0, topo, &tc_req);
	if (rc)
		D_GOTO(out, rc);

	tc_in = crt_req_get(tc_req);
	D_ASSERT(tc_in != NULL);

	tc_in->m_mark = in->m_mark;
	rc = dss_rpc_send(tc_req);
	if (rc != 0) {
		crt_req_decref(tc_req);
		D_GOTO(out, rc);
	}

	out = crt_reply_get(tc_req);
	rc = out->m_rc;
	if (rc != 0) {
		crt_req_decref(tc_req);
		D_GOTO(out, rc);
	}
out:
	out = crt_reply_get(rpc);
	D_DEBUG(DB_MGMT, "mark hdlr: rc "DF_RC"\n", DP_RC(rc));
	out->m_rc = rc;
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

	rc = ds_mgmt_system_module_init();
	if (rc != 0)
		return rc;

	D_DEBUG(DB_MGMT, "successful init call\n");
	return 0;
}

static int
ds_mgmt_fini()
{
	ds_mgmt_system_module_fini();

	D_DEBUG(DB_MGMT, "successful fini call\n");
	return 0;
}

static int
ds_mgmt_setup()
{
	return ds_mgmt_tgt_setup();
}

static int
ds_mgmt_cleanup()
{
	ds_mgmt_tgt_cleanup();
	return ds_mgmt_svc_stop();
}

struct dss_module mgmt_module = {
	.sm_name		= "mgmt",
	.sm_mod_id		= DAOS_MGMT_MODULE,
	.sm_ver			= DAOS_MGMT_VERSION,
	.sm_init		= ds_mgmt_init,
	.sm_fini		= ds_mgmt_fini,
	.sm_setup		= ds_mgmt_setup,
	.sm_cleanup		= ds_mgmt_cleanup,
	.sm_proto_fmt		= &mgmt_proto_fmt,
	.sm_cli_count		= MGMT_PROTO_CLI_COUNT,
	.sm_handlers		= mgmt_handlers,
	.sm_drpc_handlers	= mgmt_drpc_handlers,
};
