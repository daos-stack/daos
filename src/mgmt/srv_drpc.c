/*
 * (C) Copyright 2019 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of the DAOS server. It implements the handlers to
 * process incoming dRPC requests for management tasks.
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
#include <daos_api.h>
#include <daos_security.h>

#include "srv_internal.h"
#include "drpc_internal.h"
#include "mgmt.pb-c.h"
#include "srv.pb-c.h"

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

void
ds_mgmt_drpc_kill_rank(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
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

	D_INFO("Received request to kill rank (%u) on pool (%s)\n",
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

void
ds_mgmt_drpc_set_rank(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
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

	D_INFO("Received request to set rank to %u\n",
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
void
ds_mgmt_drpc_create_mgmt_svc(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
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

	D_INFO("Received request to create MS (bootstrap=%d)\n",
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
void
ds_mgmt_drpc_start_mgmt_svc(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosResp	*resp = NULL;
	int rc;

	D_INFO("Received request to start MS\n");

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

void
ds_mgmt_drpc_get_attach_info(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
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

	D_INFO("Received request to get attach info\n");

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

void
ds_mgmt_drpc_join(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
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

	D_INFO("Received request to join\n");

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

static int
create_pool_props(daos_prop_t **out_prop, char *owner, char *owner_grp,
		  const char **ace_list, size_t ace_nr)
{
	char		*out_owner = NULL;
	char		*out_owner_grp = NULL;
	struct daos_acl	*out_acl = NULL;
	daos_prop_t	*new_prop = NULL;
	uint32_t	entries = 0;
	uint32_t	idx = 0;
	int		rc = 0;

	if (ace_list != NULL && ace_nr > 0) {
		rc = daos_acl_from_strs(ace_list, ace_nr, &out_acl);
		if (rc != 0)
			D_GOTO(err_out, rc);

		entries++;
	}

	if (owner != NULL && *owner != '\0') {
		D_ASPRINTF(out_owner, "%s", owner);
		if (out_owner == NULL) {
			rc = -DER_NOMEM;
			goto err_out;
		}

		entries++;
	}

	if (owner_grp != NULL && *owner_grp != '\0') {
		D_ASPRINTF(out_owner_grp, "%s", owner_grp);
		if (out_owner_grp == NULL) {
			rc = -DER_NOMEM;
			goto err_out;
		}

		entries++;
	}

	if (entries == 0) {
		D_ERROR("No prop entries provided, aborting!\n");
		rc = -DER_INVAL;
		goto err_out;
	}

	new_prop = daos_prop_alloc(entries);
	if (new_prop == NULL) {
		rc = -DER_NOMEM;
		goto err_out;
	}

	if (out_owner != NULL) {
		new_prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_OWNER;
		new_prop->dpp_entries[idx].dpe_str = out_owner;
		idx++;
	}

	if (out_owner_grp != NULL) {
		new_prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_OWNER_GROUP;
		new_prop->dpp_entries[idx].dpe_str = out_owner_grp;
		idx++;
	}

	if (out_acl != NULL) {
		new_prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_ACL;
		new_prop->dpp_entries[idx].dpe_val_ptr = out_acl;
		idx++;
	}

	*out_prop = new_prop;

	return rc;

err_out:
	daos_prop_free(new_prop);
	daos_acl_free(out_acl);
	D_FREE(out_owner_grp);
	D_FREE(out_owner);
	return rc;
}

void
ds_mgmt_drpc_pool_create(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__PoolCreateReq	*req = NULL;
	Mgmt__PoolCreateResp	*resp = NULL;
	d_rank_list_t		*targets = NULL;
	d_rank_list_t		*svc = NULL;
	uuid_t			pool_uuid;
	daos_prop_t		*prop = NULL;
	int			buflen = 16;
	int			index;
	int			i;
	char			*extra = NULL;
	uint8_t			*body;
	size_t			len;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_create_req__unpack(NULL, drpc_req->body.len,
						 drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (create pool)\n");
		return;
	}

	D_INFO("Received request to create pool\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__pool_create_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init */
	mgmt__pool_create_resp__init(resp);

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

	rc = uuid_parse(req->uuid, pool_uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: %d\n", req->uuid,
			rc);
		goto out;
	}
	D_DEBUG(DB_MGMT, DF_UUID": creating pool\n", DP_UUID(pool_uuid));

	rc = create_pool_props(&prop, req->user, req->usergroup,
			       (const char **)req->acl, req->n_acl);
	if (rc != 0)
		goto out;

	/* Ranks to allocate targets (in) & svc for pool replicas (out). */
	rc = ds_mgmt_create_pool(pool_uuid, req->sys, "pmem", targets,
				 req->scmbytes, req->nvmebytes,
				 prop, req->numsvcreps, &svc);
	if (targets != NULL)
		d_rank_list_free(targets);
	if (rc != 0) {
		D_ERROR("failed to create pool: %d\n", rc);
		goto out;
	}

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
	len = mgmt__pool_create_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_create_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_create_req__free_unpacked(req, NULL);

	daos_prop_free(prop);

	/** check for '\0' which is a static allocation from protobuf */
	if (resp->svcreps && resp->svcreps[0] != '\0')
		D_FREE(resp->svcreps);
	D_FREE(resp);
}

void
ds_mgmt_drpc_pool_destroy(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__PoolDestroyReq	*req = NULL;
	Mgmt__PoolDestroyResp	*resp = NULL;
	uuid_t			uuid;
	uint8_t			*body;
	size_t			len;
	int			rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_destroy_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (destroy pool)\n");
		return;
	}

	D_INFO("Received request to destroy pool %s\n",
		req->uuid);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__pool_destroy_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__pool_destroy_resp__init(resp);

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
	len = mgmt__pool_destroy_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_destroy_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_destroy_req__free_unpacked(req, NULL);
	D_FREE(resp);
}

static void
free_ace_list(char **list, size_t len)
{
	size_t i;

	if (list == NULL)
		return; /* nothing to do */

	for (i = 0; i < len; i++)
		D_FREE(list[i]);

	D_FREE(list);
}

void
ds_mgmt_drpc_pool_get_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__GetACLReq		*req = NULL;
	Mgmt__GetACLResp	resp = MGMT__GET_ACLRESP__INIT;
	int			rc;
	uuid_t			pool_uuid;
	struct daos_acl		*acl = NULL;
	char			**ace_list = NULL;
	size_t			ace_nr = 0;
	size_t			len;
	uint8_t			*body;

	req = mgmt__get_aclreq__unpack(NULL, drpc_req->body.len,
				       drpc_req->body.data);
	if (req == NULL) {
		D_ERROR("Failed to unpack GetACLReq\n");
		drpc_resp->status = DRPC__STATUS__FAILURE;
		return;
	}

	D_INFO("Received request to get ACL for pool pool %s\n",
		req->uuid);

	if (uuid_parse(req->uuid, pool_uuid) != 0) {
		D_ERROR("Couldn't parse '%s' to UUID\n", req->uuid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = ds_mgmt_pool_get_acl(pool_uuid, &acl);
	if (rc != 0) {
		D_ERROR("Couldn't get pool ACL, rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_acl_to_strs(acl, &ace_list, &ace_nr);
	if (rc != 0) {
		D_ERROR("Couldn't convert ACL to string list, rc=%d", rc);
		D_GOTO(out_acl, rc);
	}

	resp.acl = ace_list;
	resp.n_acl = ace_nr;

out_acl:
	daos_acl_free(acl);
out:
	resp.status = rc;

	len = mgmt__get_aclresp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate buffer for packed GetACLResp\n");
	} else {
		mgmt__get_aclresp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__get_aclreq__free_unpacked(req, NULL);
	free_ace_list(ace_list, ace_nr);
}

void
ds_mgmt_drpc_smd_list_devs(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__SmdDevReq		*req = NULL;
	Mgmt__SmdDevResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 i;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__smd_dev_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (smd list devs)\n");
		return;
	}

	D_INFO("Received request to list SMD devices\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__smd_dev_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__smd_dev_resp__init(resp);

	rc = ds_mgmt_smd_list_devs(resp);
	if (rc != 0)
		D_ERROR("Failed to list SMD devices :%d\n", rc);

	resp->status = rc;
	len = mgmt__smd_dev_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__smd_dev_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__smd_dev_req__free_unpacked(req, NULL);

	/* all devs should already be freed upon error */
	if (rc != 0)
		goto out;

	for (i = 0; i < resp->n_devices; i++) {
		if (resp->devices[i] != NULL) {
			if (resp->devices[i]->uuid != NULL)
				D_FREE(resp->devices[i]->uuid);
			if (resp->devices[i]->tgt_ids != NULL)
				D_FREE(resp->devices[i]->tgt_ids);
			D_FREE(resp->devices[i]);
		}
	}
	D_FREE(resp->devices);
out:
	D_FREE(resp);
}

void
ds_mgmt_drpc_smd_list_pools(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__SmdPoolReq	*req = NULL;
	Mgmt__SmdPoolResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 i;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__smd_pool_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (smd list pools)\n");
		return;
	}

	D_INFO("Received request to list SMD pools\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__smd_pool_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__smd_pool_resp__init(resp);

	rc = ds_mgmt_smd_list_pools(resp);
	if (rc != 0)
		D_ERROR("Failed to list SMD pools :%d\n", rc);

	resp->status = rc;
	len = mgmt__smd_pool_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__smd_pool_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__smd_pool_req__free_unpacked(req, NULL);

	/* all pools should already be freed upon error */
	if (rc != 0)
		goto out;
	for (i = 0; i < resp->n_pools; i++) {
		if (resp->pools[i] != NULL) {
			if (resp->pools[i]->uuid != NULL)
				D_FREE(resp->pools[i]->uuid);
			if (resp->pools[i]->tgt_ids != NULL)
				D_FREE(resp->pools[i]->tgt_ids);
			if (resp->pools[i]->blobs != NULL)
				D_FREE(resp->pools[i]->blobs);
			D_FREE(resp->pools[i]);
		}
	}
	D_FREE(resp->pools);
out:
	D_FREE(resp);
}

void
ds_mgmt_drpc_bio_health_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__BioHealthReq	*req = NULL;
	Mgmt__BioHealthResp	*resp = NULL;
	struct mgmt_bio_health	*bio_health = NULL;
	struct bio_dev_state	 bds;
	uuid_t			 uuid;
	uint8_t			*body;
	size_t			 len;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__bio_health_req__unpack(
		NULL, drpc_req->body.len, drpc_req->body.data);

	if (req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (bio health query)\n");
		return;
	}

	D_DEBUG(DB_MGMT, "Received request to query BIO health data\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__bio_health_req__free_unpacked(req, NULL);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__bio_health_resp__init(resp);

	if (strlen(req->dev_uuid) != 0) {
		rc = uuid_parse(req->dev_uuid, uuid);
		if (rc != 0) {
			D_ERROR("Unable to parse device UUID %s: %d\n",
				req->dev_uuid, rc);
			goto out;
		}
	} else
		uuid_clear(uuid); /* need to set uuid = NULL */

	D_ALLOC_PTR(bio_health);
	if (bio_health == NULL) {
		D_ERROR("Failed to allocate bio health struct\n");
		rc = -DER_NOMEM;
		goto out;
	}

	rc = ds_mgmt_bio_health_query(bio_health, uuid, req->tgt_id);
	if (rc != 0) {
		D_ERROR("Failed to query BIO health data :%d\n", rc);
		goto out;
	}

	D_ALLOC(resp->dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->dev_uuid == NULL) {
		D_ERROR("failed to allocate buffer");
		rc = -DER_NOMEM;
		goto out;
	}

	uuid_unparse_lower(bio_health->mb_devid, resp->dev_uuid);
	bds = bio_health->mb_dev_state;
	resp->error_count = bds.bds_error_count;
	resp->temperature = bds.bds_temperature;
	if (bds.bds_media_errors)
		resp->media_errors = bds.bds_media_errors[0];
	else
		resp->media_errors = 0;
	resp->read_errs = bds.bds_bio_read_errs;
	resp->write_errs = bds.bds_bio_write_errs;
	resp->unmap_errs = bds.bds_bio_unmap_errs;
	resp->checksum_errs = bds.bds_checksum_errs;
	resp->temp = bds.bds_temp_warning ? true : false;
	resp->spare = bds.bds_avail_spare_warning ? true : false;
	resp->readonly = bds.bds_read_only_warning ? true : false;
	resp->device_reliability = bds.bds_dev_reliabilty_warning ?
					true : false;
	resp->volatile_memory = bds.bds_volatile_mem_warning ? true : false;

out:
	resp->status = rc;
	len = mgmt__bio_health_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__bio_health_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__bio_health_req__free_unpacked(req, NULL);
	D_FREE(resp);

	if (bio_health != NULL)
		D_FREE(bio_health);
}

void
ds_mgmt_drpc_set_up(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosResp	*resp = NULL;

	D_INFO("Received request to setup server\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__daos_resp__init(resp);

	dss_init_state_set(DSS_INIT_STATE_SET_UP);

	pack_daos_response(resp, drpc_resp);
	D_FREE(resp);
}

