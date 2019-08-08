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
pack_daos_response(Mgmt__DaosResp *daos_resp, Drpc__Response *drpc_resp)
{
	uint8_t	*body;
	size_t	len;

	len = mgmt__daos_resp__get_packed_size(daos_resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
		return;
	}

	if (mgmt__daos_resp__pack(daos_resp, body) != len) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Unexpected num bytes for daos resp\n");
		return;
	}

	/* Populate drpc response body with packed daos response */
	drpc_resp->body.len = len;
	drpc_resp->body.data = body;
}

static void
process_killrank_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosRank		*req = NULL;
	Mgmt__DaosResp		*resp = NULL;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__daos_rank__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);
	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (kill rank)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to kill rank (%u) on pool (%s)\n",
		req->rank, req->pool_uuid);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__daos_rank__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__daos_resp__init(resp);

	/* TODO: do something with request and populate daos response status */

	mgmt__daos_rank__free_unpacked(req, NULL);
	pack_daos_response(resp, drpc_resp);
	D_FREE(resp);
}

static void
process_setrank_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__SetRankReq	*req = NULL;
	Mgmt__DaosResp		*resp = NULL;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__set_rank_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);
	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (set rank)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to set rank to %u\n",
		req->rank);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__set_rank_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__daos_resp__init(resp);

	rc = crt_rank_self_set(req->rank);
	if (rc != 0) {
		D_ERROR("Failed to set self rank %u: %d\n", req->rank, rc);
		resp->status = rc;
	}

	dss_init_state_set(DSS_INIT_STATE_RANK_SET);

	mgmt__set_rank_req__free_unpacked(req, NULL);
	pack_daos_response(resp, drpc_resp);
	D_FREE(resp);
}

/*
 * TODO: Make sure the MS doesn't accept any requests until StartMS completes.
 * See also process_startms_request.
 */
static void
process_createms_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__CreateMsReq	*req = NULL;
	Mgmt__DaosResp		*resp = NULL;
	uuid_t			uuid;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__create_ms_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);
	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (create MS)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to create MS (bootstrap=%d)\n",
		req->bootstrap);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__create_ms_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__daos_resp__init(resp);

	if (req->bootstrap) {
		rc = uuid_parse(req->uuid, uuid);
		if (rc != 0) {
			D_ERROR("Unable to parse server UUID: %s\n",
				req->uuid);
			goto out;
		}
	}

	rc = ds_mgmt_svc_start(true /* create */, ds_rsvc_get_md_cap(),
			       req->bootstrap, uuid, req->addr);
	if (rc != 0)
		D_ERROR("Failed to create MS (bootstrap=%d): %d\n",
			req->bootstrap, rc);

out:
	mgmt__create_ms_req__free_unpacked(req, NULL);
	resp->status = rc;
	pack_daos_response(resp, drpc_resp);
	D_FREE(resp);
}

/*
 * TODO: Make sure the MS doesn't accept any requests until StartMS completes.
 * See also process_createms_request, which already starts the MS.
 */
static void
process_startms_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosResp	*resp = NULL;
	int rc;

	D_DEBUG(DB_MGMT, "Received request to start MS\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__daos_resp__init(resp);

	rc = ds_mgmt_svc_start(false /* !create */, 0 /* size */,
			       false /* !bootstrap */, NULL /* uuid */,
			       NULL /* addr */);
	if (rc == -DER_ALREADY) {
		D_DEBUG(DB_MGMT, "MS already started\n");
	} else if (rc != 0) {
		D_ERROR("Failed to start MS: %d\n", rc);
		resp->status = rc;
	}

	pack_daos_response(resp, drpc_resp);
	D_FREE(resp);
}

static void
process_getattachinfo_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__GetAttachInfoReq	*req = NULL;
	Mgmt__GetAttachInfoResp	*resp = NULL;
	uint8_t			*body;
	size_t			len;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__get_attach_info_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (get attach info)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to get attach info\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate inner response ref\n");
		mgmt__get_attach_info_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__get_attach_info_resp__init(resp);

	rc = ds_mgmt_get_attach_info_handler(resp);
	if (rc != 0) {
		D_ERROR("Failed to get attach info: %d\n", rc);
		resp->status = rc;
	}

	len = mgmt__get_attach_info_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__get_attach_info_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__get_attach_info_req__free_unpacked(req, NULL);
	D_FREE(resp);
}

static void
process_join_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__JoinReq		*req = NULL;
	Mgmt__JoinResp		*resp = NULL;
	struct mgmt_join_in	in = {};
	struct mgmt_join_out	out = {};
	uint8_t			*body;
	size_t			len;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__join_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (join)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to join\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__join_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__join_resp__init(resp);

	in.ji_rank = req->rank;
	in.ji_server.sr_flags = SERVER_IN;
	in.ji_server.sr_nctxs = req->nctxs;
	rc = uuid_parse(req->uuid, in.ji_server.sr_uuid);
	if (rc != 0) {
		D_ERROR("Failed to parse UUID: %s\n", req->uuid);
		goto out;
	}
	len = strnlen(req->addr, ADDR_STR_MAX_LEN);
	if (len >= ADDR_STR_MAX_LEN) {
		D_ERROR("Server address '%.*s...' too long\n", ADDR_STR_MAX_LEN,
			req->addr);
		rc = -DER_INVAL;
		goto out;
	}
	memcpy(in.ji_server.sr_addr, req->addr, len + 1);
	len = strnlen(req->uri, ADDR_STR_MAX_LEN);
	if (len >= ADDR_STR_MAX_LEN) {
		D_ERROR("Self URI '%.*s...' too long\n", ADDR_STR_MAX_LEN,
			req->uri);
		rc = -DER_INVAL;
		goto out;
	}
	memcpy(in.ji_server.sr_uri, req->uri, len + 1);

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
	resp->status = rc;
	len = mgmt__join_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__join_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__join_req__free_unpacked(req, NULL);
	D_FREE(resp);
}

static void
process_createpool_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__CreatePoolReq	*req = NULL;
	Mgmt__CreatePoolResp	*resp = NULL;
	d_rank_list_t		*targets = NULL;
	d_rank_list_t		*svc = NULL;
	uuid_t			pool_uuid;
	int			buflen = 16;
	int			index;
	int			i;
	char			*extra = NULL;
	uint8_t			*body;
	size_t			len;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__create_pool_req__unpack(NULL, drpc_req->body.len,
						 drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (create pool)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to create pool\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__create_pool_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init */
	mgmt__create_pool_resp__init(resp);

	/* Parse targets rank list. */
	if (strlen(req->ranks) != 0) {
		targets = daos_rank_list_parse(req->ranks, ",");
		if (targets == NULL) {
			D_ERROR("failed to parse target ranks\n");
			rc = -DER_INVAL;
			goto out;
		}
		D_DEBUG(DB_MGMT, "ranks in: %s\n", req->ranks);
	}

	uuid_generate(pool_uuid);
	D_DEBUG(DB_MGMT, DF_UUID": creating pool\n", DP_UUID(pool_uuid));

	/* Ranks to allocate targets (in) & svc for pool replicas (out). */
	rc = ds_mgmt_create_pool(pool_uuid, req->sys, "pmem", targets,
				 req->scmbytes, req->nvmebytes,
				 NULL /* props */, req->numsvcreps, &svc);
	if (targets != NULL)
		d_rank_list_free(targets);
	if (rc != 0) {
		D_ERROR("failed to create pool: %d\n", rc);
		goto out;
	}

	D_ALLOC(resp->uuid, DAOS_UUID_STR_SIZE);
	if (resp->uuid == NULL) {
		D_ERROR("failed to allocate buffer");
		rc = -DER_NOMEM;
		goto out_svc;
	}

	uuid_unparse_lower(pool_uuid, resp->uuid);

	assert(svc->rl_nr > 0);

	D_ALLOC(resp->svcreps, buflen);
	if (resp->svcreps == NULL) {
		D_ERROR("failed to allocate buffer");
		rc = -DER_NOMEM;
		goto out_svc;
	}

	/* Populate the pool service replica ranks string. */
	index = sprintf(resp->svcreps, "%u", svc->rl_ranks[0]);

	for (i = 1; i < svc->rl_nr; i++) {
		index += snprintf(&resp->svcreps[index], buflen-index,
				  ",%u", svc->rl_ranks[i]);
		if (index >= buflen) {
			buflen *= 2;

			D_ALLOC(extra, buflen);

			if (extra == NULL) {
				D_ERROR("failed to allocate buffer");
				rc = -DER_NOMEM;
				goto out_svc;
			}

			index = snprintf(extra, buflen, "%s,%u",
					 resp->svcreps, svc->rl_ranks[i]);

			D_FREE(resp->svcreps);
			resp->svcreps = extra;
		}
	}

	D_DEBUG(DB_MGMT, "%d service replicas: %s\n", svc->rl_nr,
		resp->svcreps);

out_svc:
	d_rank_list_free(svc);
out:
	resp->status = rc;
	len = mgmt__create_pool_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__create_pool_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__create_pool_req__free_unpacked(req, NULL);

	/** check for '\0' which is a static allocation from protobuf */
	if (resp->svcreps && resp->svcreps[0] != '\0')
		D_FREE(resp->svcreps);
	if (resp->uuid && resp->uuid[0] != '\0')
		D_FREE(resp->uuid);
	D_FREE(resp);
}

static void
process_destroypool_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DestroyPoolReq	*req = NULL;
	Mgmt__DestroyPoolResp	*resp = NULL;
	uuid_t			uuid;
	uint8_t			*body;
	size_t			len;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__destroy_pool_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (destroy pool)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to destroy pool %s\n",
		req->uuid);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__destroy_pool_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__destroy_pool_resp__init(resp);

	rc = uuid_parse(req->uuid, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: %d\n", req->uuid,
			rc);
		goto out;
	}

	/* Sys and force params are currently ignored in receiver. */
	rc = ds_mgmt_destroy_pool(uuid, req->sys,
				  (req->force == true) ? 1 : 0);
	if (rc != 0) {
		D_ERROR("Failed to destroy pool %s: %d\n", req->uuid, rc);
		goto out;
	}

out:
	resp->status = rc;
	len = mgmt__destroy_pool_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__destroy_pool_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__destroy_pool_req__free_unpacked(req, NULL);
	D_FREE(resp);
}

static void
process_biohealth_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
//	Mgmt__BioHealthReq	*req = NULL;
	Mgmt__BioHealthResp	*resp = NULL;
	int	rc;

//	/* Unpack the inner request from the drpc call body */
//	req = mgmt__bio_health_req__unpack(
//		NULL, drpc_req->body.len, drpc_req->body.data);

//	if (req == NULL) {
//		drpc_resp->status = DRPC__STATUS__FAILURE;
//		D_ERROR("Failed to unpack req (destroy pool)\n");
//		return;
//	}

	D_DEBUG(DB_MGMT, "Received request to query BIO health data\n");
	D_ERROR("Received request to query BIO health data\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		//mgmt__bio_health_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	//mgmt__bio_health_resp__init(resp);


	rc = ds_mgmt_bio_health_query();
	if (rc != 0) {
		D_ERROR("Failed to query BIO health data :%d\n", rc);
		goto out;
	}

out:
	resp->status = rc;
	//len = mgmt__bio_health_resp__get_packed_size(resp);
	//D_ALLOC(body, len);
	//if (body == NULL) {
	//	drpc_resp->status = DRPC__STATUS__FAILURE;
	//	D_ERROR("Failed to allocate drpc response body\n");
	//} else {
//		mgmt__destroy_pool_resp__pack(resp, body);
//		drpc_resp->body.len = len;
//		drpc_resp->body.data = body;
//	}

//	mgmt__bio_health_req__free_unpacked(req, NULL);
	D_FREE(resp);
}

static void
process_setup_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosResp	*resp = NULL;

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__daos_resp__init(resp);

	D_DEBUG(DB_MGMT, "Received request to set up server\n");

	dss_init_state_set(DSS_INIT_STATE_SET_UP);

	pack_daos_response(resp, drpc_resp);
	D_FREE(resp);
}

static void
process_drpc_request(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	/**
	 * Process drpc request and populate daos response,
	 * command errors should be indicated inside daos response.
	 */
	switch (drpc_req->method) {
	case DRPC_METHOD_MGMT_KILL_RANK:
		process_killrank_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_RANK:
		process_setrank_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CREATE_MS:
		process_createms_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_START_MS:
		process_startms_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_GET_ATTACH_INFO:
		process_getattachinfo_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_JOIN:
		process_join_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_CREATE_POOL:
		process_createpool_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_DESTROY_POOL:
		process_destroypool_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_SET_UP:
		process_setup_request(drpc_req, drpc_resp);
		break;
	case DRPC_METHOD_MGMT_BIO_HEALTH_QUERY:
		process_biohealth_request(drpc_req, drpc_resp);
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
