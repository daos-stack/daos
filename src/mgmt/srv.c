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

#include <signal.h>
#include <daos/drpc_modules.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/rsvc.h>
#include <daos_api.h>

#include "mgmt.pb-c.h"
#include "srv.pb-c.h"
#include "srv_internal.h"

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
process_killrank_request(Drpc__Call *drpc_req, Mgmt__DaosResponse *daos_resp)
{
	Mgmt__DaosRank	*pb_rank = NULL;

	mgmt__daos_response__init(daos_resp);

	/* Unpack the daos request from the drpc call body */
	pb_rank = mgmt__daos_rank__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (pb_rank == NULL) {
		daos_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
		D_ERROR("Failed to extract rank from request\n");

		return;
	}

	/* response status is populated with SUCCESS on init */
	D_DEBUG(DB_MGMT, "Received request to kill rank (%u) on pool (%s)\n",
		pb_rank->rank, pb_rank->pool_uuid);

	/* TODO: do something with request and populate daos response status */

	mgmt__daos_rank__free_unpacked(pb_rank, NULL);
}

static void
process_setrank_request(Drpc__Call *drpc_req, Mgmt__DaosResponse *daos_resp)
{
	Mgmt__SetRankReq	*pb_req = NULL;
	int			rc;

	mgmt__daos_response__init(daos_resp);

	/* Unpack the daos request from the drpc call body */
	pb_req = mgmt__set_rank_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (pb_req == NULL) {
		daos_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
		D_ERROR("Failed to extract request\n");

		return;
	}

	/* response status is populated with SUCCESS on init */
	D_DEBUG(DB_MGMT, "Received request to set rank to %u\n", pb_req->rank);

	rc = crt_rank_self_set(pb_req->rank);
	if (rc != 0) {
		D_ERROR("Failed to set self rank %u: %d\n", pb_req->rank, rc);
		daos_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
	}

	dss_notify_rank_set();

	mgmt__set_rank_req__free_unpacked(pb_req, NULL);
}

/*
 * TODO: Make sure the MS doesn't accept any requests until StartMS completes.
 * See also process_startms_request.
 */
static void
process_createms_request(Drpc__Call *drpc_req, Mgmt__DaosResponse *daos_resp)
{
	Mgmt__CreateMsReq	*pb_req = NULL;
	uuid_t			uuid;
	int			rc;

	mgmt__daos_response__init(daos_resp);

	/* Unpack the daos request from the drpc call body */
	pb_req = mgmt__create_ms_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (pb_req == NULL) {
		daos_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
		D_ERROR("Failed to extract request\n");

		return;
	}

	/* response status is populated with SUCCESS on init */
	D_DEBUG(DB_MGMT, "Received request to create MS (bootstrap=%d)\n",
		pb_req->bootstrap);

	if (pb_req->bootstrap) {
		rc = uuid_parse(pb_req->uuid, uuid);
		if (rc != 0) {
			D_ERROR("Unable to parse server UUID: %s\n",
				pb_req->uuid);
			goto out;
		}
	}

	rc = ds_mgmt_svc_start(true /* create */, ds_rsvc_get_md_cap(),
			       pb_req->bootstrap, uuid, pb_req->addr);
	if (rc != 0)
		D_ERROR("Failed to create MS (bootstrap=%d): %d\n",
			pb_req->bootstrap, rc);

out:
	mgmt__create_ms_req__free_unpacked(pb_req, NULL);
	if (rc != 0)
		daos_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
}

/*
 * TODO: Make sure the MS doesn't accept any requests until StartMS completes.
 * See also process_createms_request, which already starts the MS.
 */
static void
process_startms_request(Drpc__Call *drpc_req, Mgmt__DaosResponse *daos_resp)
{
	int rc;

	mgmt__daos_response__init(daos_resp);

	/* response status is populated with SUCCESS on init */
	D_DEBUG(DB_MGMT, "Received request to start MS\n");

	rc = ds_mgmt_svc_start(false /* !create */, 0 /* size */,
			       false /* !bootstrap */, NULL /* uuid */,
			       NULL /* addr */);
	if (rc == -DER_ALREADY) {
		D_DEBUG(DB_MGMT, "MS already started\n");
	} else if (rc != 0) {
		D_ERROR("Failed to start MS: %d\n", rc);
		daos_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
	}
}

static void
process_getattachinfo_request(Drpc__Call *drpc_req,
			      Mgmt__GetAttachInfoResp *resp)
{
	Mgmt__GetAttachInfoReq	*pb_req = NULL;
	int			rc;

	mgmt__get_attach_info_resp__init(resp);

	/* Unpack the daos request from the drpc call body */
	pb_req = mgmt__get_attach_info_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (pb_req == NULL) {
		resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
		D_ERROR("Failed to extract request\n");

		return;
	}

	/* response status is populated with SUCCESS on init */
	D_DEBUG(DB_MGMT, "Received request to get attach info\n");

	rc = ds_mgmt_get_attach_info_handler(resp);
	if (rc != 0)
		D_ERROR("Failed to get attach info: %d\n", rc);

	mgmt__get_attach_info_req__free_unpacked(pb_req, NULL);
	if (rc != 0)
		resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
}

static void
process_join_request(Drpc__Call *drpc_req, Mgmt__JoinResp *resp)
{
	Mgmt__JoinReq		*pb_req = NULL;
	struct mgmt_join_in	in = {};
	struct mgmt_join_out	out = {};
	size_t			len;
	int			rc;

	mgmt__join_resp__init(resp);

	/* Unpack the daos request from the drpc call body */
	pb_req = mgmt__join_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (pb_req == NULL) {
		resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
		D_ERROR("Failed to extract request\n");

		return;
	}

	/* response status is populated with SUCCESS on init */
	D_DEBUG(DB_MGMT, "Received request to join\n");

	in.ji_rank = pb_req->rank;
	in.ji_server.sr_flags = SERVER_IN;
	in.ji_server.sr_nctxs = pb_req->nctxs;
	rc = uuid_parse(pb_req->uuid, in.ji_server.sr_uuid);
	if (rc != 0) {
		D_ERROR("Failed to parse UUID: %s\n", pb_req->uuid);
		goto out;
	}
	len = strnlen(pb_req->addr, ADDR_STR_MAX_LEN);
	if (len >= ADDR_STR_MAX_LEN) {
		D_ERROR("Server address '%.*s...' too long\n", ADDR_STR_MAX_LEN,
			pb_req->addr);
		goto out;
	}
	memcpy(in.ji_server.sr_addr, pb_req->addr, len + 1);
	len = strnlen(pb_req->uri, ADDR_STR_MAX_LEN);
	if (len >= ADDR_STR_MAX_LEN) {
		D_ERROR("Self URI '%.*s...' too long\n", ADDR_STR_MAX_LEN,
			pb_req->uri);
		goto out;
	}
	memcpy(in.ji_server.sr_uri, pb_req->uri, len + 1);

	rc = ds_mgmt_join_handler(&in, &out);
	if (rc != 0) {
		D_ERROR("Failed to join: %d\n", rc);
		goto out;
	}

	resp->rank = out.jo_rank;
	if (out.jo_flags & SERVER_IN)
		resp->state = MGMT__JOIN_RESP__STATE__IN;
	else
		resp->state = MGMT__JOIN_RESP__STATE__OUT;

out:
	mgmt__join_req__free_unpacked(pb_req, NULL);
	if (rc != 0)
		resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
}

static void
process_create_pool_request(Drpc__Call *drpc_req, Mgmt__CreatePoolResp *pb_resp)
{
	Mgmt__CreatePoolReq	*pb_req = NULL;
	d_rank_list_t		*targets = NULL;
	d_rank_list_t		*svc = NULL;
	uuid_t			pool_uuid;
	int			buflen = 16;
	int			index;
	int			i;
	int			rc = 0;
	char			*extra = NULL;

	/* response status is populated with SUCCESS on init */
	mgmt__create_pool_resp__init(pb_resp);

	/* Unpack the daos request from the drpc call body */
	pb_req = mgmt__create_pool_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (pb_req == NULL) {
		pb_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
		D_ERROR("Failed to extract request\n");

		return;
	}

	/* parse targets rank list */
	if (strlen(pb_req->ranks) != 0) {
		targets = daos_rank_list_parse(pb_req->ranks, ",");
		if (targets == NULL) {
			D_ERROR("failed to parse target ranks\n");
			rc = -1;
			goto out;
		}
		D_DEBUG(DB_MGMT, "ranks in: %s\n", pb_req->ranks);
	}

	uuid_generate(pool_uuid);
	D_DEBUG(DB_MGMT, DF_UUID": creating pool\n", DP_UUID(pool_uuid));

	/* ranks to allocate targets (in) & svc for pool replicas (out) */
	rc = ds_mgmt_create_pool(pool_uuid, pb_req->sys, "pmem",
			targets, pb_req->scmbytes, pb_req->nvmebytes,
			NULL /* props */, pb_req->numsvcreps, &svc);
	if (targets != NULL)
		d_rank_list_free(targets);
	if (rc != 0) {
		D_ERROR("failed to create pool: %d\n", rc);
		goto out;
	}

	D_ALLOC(pb_resp->uuid, DAOS_UUID_STR_SIZE);

	if (pb_resp->uuid == NULL) {
		D_ERROR("failed to allocate buffer");
		rc = -DER_NOMEM;
		goto out;
	}

	uuid_unparse_lower(pool_uuid, pb_resp->uuid);

	assert(svc->rl_nr > 0);

	D_ALLOC(pb_resp->svcreps, buflen);

	if (pb_resp->svcreps == NULL) {
		D_ERROR("failed to allocate buffer");
		rc = -DER_NOMEM;
		goto out;
	}

	/* populate the pool service replica ranks string. */
	index = sprintf(pb_resp->svcreps, "%u", svc->rl_ranks[0]);

	for (i = 1; i < svc->rl_nr; i++) {
		index += snprintf(&pb_resp->svcreps[index], buflen-index,
			",%u", svc->rl_ranks[i]);
		if (index >= buflen) {
			buflen *= 2;

			D_ALLOC(extra, buflen);

			if (extra == NULL) {
				D_ERROR("failed to allocate buffer");
				rc = -DER_NOMEM;
				goto out;
			}

			index = snprintf(extra, buflen, "%s,%u",
				pb_resp->svcreps, svc->rl_ranks[i]);

			D_FREE(pb_resp->svcreps);
			pb_resp->svcreps = extra;
		}
	}

	D_DEBUG(DB_MGMT, "%d service replicas: %s\n", svc->rl_nr,
		pb_resp->svcreps);

out:
	mgmt__create_pool_req__free_unpacked(pb_req, NULL);
	if (svc)
		d_rank_list_free(svc);
	if (rc != 0)
		pb_resp->status = MGMT__DAOS_REQUEST_STATUS__ERR_UNKNOWN;
}

static void
pack_daos_response(Mgmt__DaosResponse *daos_resp, Drpc__Response *drpc_resp)
{
	uint8_t	*body;
	size_t	len;

	len = mgmt__daos_response__get_packed_size(daos_resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
		return;
	}

	if (mgmt__daos_response__pack(daos_resp, body) != len) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Unexpected num bytes for daos resp\n");
		return;
	}

	/* Populate drpc response body with packed daos response */
	drpc_resp->body.len = len;
	drpc_resp->body.data = body;
}

static void
process_drpc_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosResponse	*daos_resp = NULL;
	Mgmt__JoinResp		*join_resp;
	Mgmt__GetAttachInfoResp	*getattachinfo_resp;
	Mgmt__CreatePoolResp	*create_pool_resp;
	uint8_t			*body;
	size_t			len;

	D_ALLOC_PTR(daos_resp);
	if (daos_resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");

		return;
	}

	/**
	 * Process drpc request and populate daos response,
	 * command errors should be indicated inside daos response.
	 */
	switch (drpc_req->method) {
	case DRPC_METHOD_MGMT_KILL_RANK:
		process_killrank_request(drpc_req, daos_resp);
		pack_daos_response(daos_resp, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_RANK:
		process_setrank_request(drpc_req, daos_resp);
		pack_daos_response(daos_resp, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CREATE_MS:
		process_createms_request(drpc_req, daos_resp);
		pack_daos_response(daos_resp, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_START_MS:
		process_startms_request(drpc_req, daos_resp);
		pack_daos_response(daos_resp, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_GET_ATTACH_INFO:
		D_ALLOC_PTR(getattachinfo_resp);
		if (getattachinfo_resp == NULL) {
			drpc_resp->status = DRPC__STATUS__FAILURE;
			D_ERROR("Failed to allocate daos response ref\n");
			break;
		}
		process_getattachinfo_request(drpc_req, getattachinfo_resp);
		len = mgmt__get_attach_info_resp__get_packed_size(
							    getattachinfo_resp);
		D_ALLOC(body, len);
		if (body == NULL) {
			drpc_resp->status = DRPC__STATUS__FAILURE;
			D_ERROR("Failed to allocate drpc response body\n");
			D_FREE(getattachinfo_resp);
			break;
		}
		mgmt__get_attach_info_resp__pack(getattachinfo_resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
		D_FREE(getattachinfo_resp);
		break;
	case DRPC_METHOD_MGMT_JOIN:
		D_ALLOC_PTR(join_resp);
		if (join_resp == NULL) {
			drpc_resp->status = DRPC__STATUS__FAILURE;
			D_ERROR("Failed to allocate daos response ref\n");
			break;
		}
		process_join_request(drpc_req, join_resp);
		len = mgmt__join_resp__get_packed_size(join_resp);
		D_ALLOC(body, len);
		if (body == NULL) {
			drpc_resp->status = DRPC__STATUS__FAILURE;
			D_ERROR("Failed to allocate drpc response body\n");
			D_FREE(join_resp);
			break;
		}
		mgmt__join_resp__pack(join_resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
		D_FREE(join_resp);
		break;
	case DRPC_METHOD_MGMT_CREATE_POOL:
		D_ALLOC_PTR(create_pool_resp);
		if (create_pool_resp == NULL) {
			drpc_resp->status = DRPC__STATUS__FAILURE;
			D_ERROR("Failed to allocate daos response ref\n");
			break;
		}
		process_create_pool_request(drpc_req, create_pool_resp);
		len = mgmt__create_pool_resp__get_packed_size(create_pool_resp);
		D_ALLOC(body, len);
		if (body == NULL) {
			drpc_resp->status = DRPC__STATUS__FAILURE;
			D_ERROR("Failed to allocate drpc response body\n");
			D_FREE(create_pool_resp);
			break;
		}
		mgmt__create_pool_resp__pack(create_pool_resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
		D_FREE(create_pool_resp->svcreps);
		D_FREE(create_pool_resp->uuid);
		D_FREE(create_pool_resp);
		break;
	default:
		drpc_resp->status = DRPC__STATUS__UNKNOWN_METHOD;
		D_ERROR("Unknown method\n");
	}

	D_FREE(daos_resp);
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

	tc_in->p_module = in->p_module;
	tc_in->p_path = in->p_path;
	tc_in->p_op = in->p_op;
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
	D_DEBUG(DB_MGMT, "profile hdlr: rc %d\n", rc);
	out->p_rc = rc;
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

	rc = ds_mgmt_system_module_init();
	if (rc != 0) {
		ds_mgmt_tgt_fini();
		return rc;
	}

	D_DEBUG(DB_MGMT, "successfull init call\n");
	return 0;
}

static int
ds_mgmt_fini()
{
	ds_mgmt_system_module_fini();
	ds_mgmt_tgt_fini();
	D_DEBUG(DB_MGMT, "successfull fini call\n");
	return 0;
}

static int
ds_mgmt_cleanup()
{
	return ds_mgmt_svc_stop();
}

struct dss_module mgmt_module = {
	.sm_name		= "mgmt",
	.sm_mod_id		= DAOS_MGMT_MODULE,
	.sm_ver			= DAOS_MGMT_VERSION,
	.sm_init		= ds_mgmt_init,
	.sm_fini		= ds_mgmt_fini,
	.sm_cleanup		= ds_mgmt_cleanup,
	.sm_proto_fmt		= &mgmt_proto_fmt,
	.sm_cli_count		= MGMT_PROTO_CLI_COUNT,
	.sm_handlers		= mgmt_handlers,
	.sm_drpc_handlers	= mgmt_drpc_handlers,
};
