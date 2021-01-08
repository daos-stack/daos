/*
 * (C) Copyright 2019-2020 Intel Corporation.
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

#include <signal.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/pool.h>
#include <daos_api.h>
#include <daos_security.h>

#include "svc.pb-c.h"
#include "acl.pb-c.h"
#include "pool.pb-c.h"
#include "cont.pb-c.h"
#include "srv_internal.h"
#include "drpc_internal.h"

static void
pack_daos_response(Mgmt__DaosResp *daos_resp, Drpc__Response *drpc_resp)
{
	uint8_t	*body;
	size_t	 len;

	len = mgmt__daos_resp__get_packed_size(daos_resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__daos_resp__pack(daos_resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}
}

void
ds_mgmt_drpc_prep_shutdown(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PrepShutdownReq	*req = NULL;
	Mgmt__DaosResp		 resp = MGMT__DAOS_RESP__INIT;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__prep_shutdown_req__unpack(&alloc.alloc,
					      drpc_req->body.len,
					      drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (prep shutdown)\n");
		return;
	}

	D_INFO("Received request to prep shutdown %u\n", req->rank);

#ifndef DRPC_TEST
	ds_pool_disable_evict();
#endif

	/* TODO: disable auto evict and pool rebuild here */
	D_INFO("Service rank %d is being prepared for controlled shutdown\n",
		req->rank);

	pack_daos_response(&resp, drpc_resp);
	mgmt__prep_shutdown_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_ping_rank(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PingRankReq	*req = NULL;
	Mgmt__DaosResp		 resp = MGMT__DAOS_RESP__INIT;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__ping_rank_req__unpack(&alloc.alloc,
					  drpc_req->body.len,
					  drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (ping rank)\n");
		return;
	}

	D_INFO("Received request to ping rank %u\n", req->rank);

	/* TODO: verify iosrv components are functioning as expected */

	pack_daos_response(&resp, drpc_resp);
	mgmt__ping_rank_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_set_rank(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__SetRankReq	*req = NULL;
	Mgmt__DaosResp		 resp = MGMT__DAOS_RESP__INIT;
	int			 rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__set_rank_req__unpack(&alloc.alloc,
					 drpc_req->body.len,
					 drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (set rank)\n");
		return;
	}

	D_INFO("Received request to set rank to %u\n", req->rank);

	rc = crt_rank_self_set(req->rank);
	if (rc != 0)
		D_ERROR("Failed to set self rank %u: "DF_RC"\n", req->rank,
			DP_RC(rc));

	resp.status = rc;
	pack_daos_response(&resp, drpc_resp);
	mgmt__set_rank_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_group_update(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__GroupUpdateReq	*req = NULL;
	Mgmt__GroupUpdateResp	resp = MGMT__GROUP_UPDATE_RESP__INIT;
	struct mgmt_grp_up_in	in = {};
	uint8_t			*body;
	size_t			 len;
	int			 rc, i;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__group_update_req__unpack(
		&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (group_update)\n");
		return;
	}

	D_INFO("Received request to update group map\n");

	D_ALLOC_ARRAY(in.gui_servers, req->n_servers);
	if (in.gui_servers == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	for (i = 0; i < req->n_servers; i++) {
		in.gui_servers[i].se_rank = req->servers[i]->rank;
		in.gui_servers[i].se_uri = req->servers[i]->uri;
	}
	in.gui_n_servers = req->n_servers;
	in.gui_map_version = req->map_version;

	rc = ds_mgmt_group_update_handler(&in);
out:
	if (in.gui_servers != NULL)
		D_FREE(in.gui_servers);

	resp.status = rc;
	len = mgmt__group_update_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__group_update_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__group_update_req__free_unpacked(req, &alloc.alloc);
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
		if (out_owner == NULL)
			D_GOTO(err_out, rc = -DER_NOMEM);

		entries++;
	}

	if (owner_grp != NULL && *owner_grp != '\0') {
		D_ASPRINTF(out_owner_grp, "%s", owner_grp);
		if (out_owner_grp == NULL)
			D_GOTO(err_out, rc = -DER_NOMEM);

		entries++;
	}

	if (entries == 0) {
		D_ERROR("No prop entries provided, aborting!\n");
		D_GOTO(err_out, rc = -DER_INVAL);
	}

	new_prop = daos_prop_alloc(entries);
	if (new_prop == NULL)
		D_GOTO(err_out, rc = -DER_NOMEM);

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
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolCreateReq	*req = NULL;
	Mgmt__PoolCreateResp	 resp = MGMT__POOL_CREATE_RESP__INIT;
	d_rank_list_t		*targets = NULL;
	d_rank_list_t		*svc = NULL;
	uuid_t			 pool_uuid;
	daos_prop_t		*prop = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_create_req__unpack(&alloc.alloc, drpc_req->body.len,
					    drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (create pool)\n");
		return;
	}

	D_INFO("Received request to create pool on %zu ranks\n", req->n_ranks);

	if (req->n_ranks > 0) {
		targets = uint32_array_to_rank_list(req->ranks, req->n_ranks);
		if (targets == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = uuid_parse(req->uuid, pool_uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->uuid,
			DP_RC(rc));
		D_GOTO(out, rc = -DER_INVAL);
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
		D_ERROR("failed to create pool: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	D_ASSERT(svc->rl_nr > 0);

	rc = rank_list_to_uint32_array(svc, &resp.svcreps, &resp.n_svcreps);
	if (rc != 0)
		D_GOTO(out_svc, rc);

	D_DEBUG(DB_MGMT, "%d service replicas\n", svc->rl_nr);

out_svc:
	d_rank_list_free(svc);
out:
	resp.status = rc;
	len = mgmt__pool_create_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_create_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_create_req__free_unpacked(req, &alloc.alloc);

	daos_prop_free(prop);

	/** check for '\0' which is a static allocation from protobuf */
	if (resp.svcreps)
		D_FREE(resp.svcreps);
}

void
ds_mgmt_drpc_pool_destroy(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolDestroyReq	*req = NULL;
	Mgmt__PoolDestroyResp	 resp = MGMT__POOL_DESTROY_RESP__INIT;
	uuid_t			 uuid;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_destroy_req__unpack(&alloc.alloc,
					     drpc_req->body.len,
					     drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (destroy pool)\n");
		return;
	}

	D_INFO("Received request to destroy pool %s\n", req->uuid);

	rc = uuid_parse(req->uuid, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->uuid,
			DP_RC(rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Sys and force params are currently ignored in receiver. */
	rc = ds_mgmt_destroy_pool(uuid, svc_ranks, req->sys,
				  (req->force == true) ? 1 : 0);
	if (rc != 0) {
		D_ERROR("Failed to destroy pool %s: "DF_RC"\n", req->uuid,
			DP_RC(rc));
	}

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_destroy_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_destroy_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_destroy_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_pool_evict(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolEvictReq	*req = NULL;
	Mgmt__PoolEvictResp	 resp = MGMT__POOL_EVICT_RESP__INIT;
	uuid_t			 uuid;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_evict_req__unpack(&alloc.alloc,
					   drpc_req->body.len,
					   drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (evict pool_connections)\n");
		return;
	}

	D_INFO("Received request to evict pool connections %s\n", req->uuid);

	rc = uuid_parse(req->uuid, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->uuid,
			DP_RC(rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_evict_pool(uuid, svc_ranks, req->sys);
	if (rc != 0) {
		D_ERROR("Failed to evict pool connections %s: "DF_RC"\n",
			req->uuid, DP_RC(rc));
	}

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_evict_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_evict_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_evict_req__free_unpacked(req, &alloc.alloc);
}

static int
pool_change_target_state(char *id, d_rank_list_t *svc_ranks,
			 size_t n_targetidx, uint32_t *targetidx,
			 uint32_t rank, pool_comp_state_t state)
{
	uuid_t				uuid;
	struct pool_target_id_list	target_id_list;
	int				num_idxs;
	int				rc, i;

	num_idxs = (n_targetidx > 0) ? n_targetidx : 1;
	rc = uuid_parse(id, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", id,
			DP_RC(rc));
		return -DER_INVAL;
	}

	rc = pool_target_id_list_alloc(num_idxs, &target_id_list);
	if (rc)
		return rc;

	if (n_targetidx > 0) {
		for (i = 0; i < n_targetidx; ++i)
			target_id_list.pti_ids[i].pti_id = targetidx[i];
	} else {
		target_id_list.pti_ids[0].pti_id = -1;
	}

	rc = ds_mgmt_pool_target_update_state(uuid, svc_ranks, rank,
					      &target_id_list, state);
	if (rc != 0) {
		D_ERROR("Failed to set pool target up %s: "DF_RC"\n", uuid,
			DP_RC(rc));
	}

	pool_target_id_list_free(&target_id_list);
	return rc;
}

void
ds_mgmt_drpc_pool_exclude(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolExcludeReq	*req = NULL;
	Mgmt__PoolExcludeResp	resp;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			len;
	int			rc;

	mgmt__pool_exclude_resp__init(&resp);

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_exclude_req__unpack(&alloc.alloc,
					     drpc_req->body.len,
					     drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (Exclude target)\n");
		return;
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = pool_change_target_state(req->uuid, svc_ranks,
				      req->n_targetidx, req->targetidx,
				      req->rank, PO_COMP_ST_DOWN);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_exclude_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_exclude_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_exclude_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_pool_drain(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolDrainReq	*req = NULL;
	Mgmt__PoolDrainResp	resp;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			len;
	int			rc;

	mgmt__pool_drain_resp__init(&resp);

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_drain_req__unpack(&alloc.alloc,
					   drpc_req->body.len,
					   drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (Drain target)\n");
		return;
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = pool_change_target_state(req->uuid, svc_ranks, req->n_targetidx,
			req->targetidx, req->rank, PO_COMP_ST_DRAIN);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_drain_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_drain_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_drain_req__free_unpacked(req, &alloc.alloc);
}
void
ds_mgmt_drpc_pool_extend(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolExtendReq	*req = NULL;
	Mgmt__PoolExtendResp	resp;
	d_rank_list_t		*rank_list = NULL;
	d_rank_list_t		*svc_ranks = NULL;
	uuid_t			uuid;
	uint8_t			*body;
	size_t			len;
	int			rc;

	mgmt__pool_extend_resp__init(&resp);

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_extend_req__unpack(&alloc.alloc,
					    drpc_req->body.len,
					    drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (Extend target)\n");
		return;
	}

	rc = uuid_parse(req->uuid, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->uuid,
			DP_RC(rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	rank_list = uint32_array_to_rank_list(req->ranks, req->n_ranks);
	if (rank_list == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out_list, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_extend(uuid, svc_ranks, rank_list, "pmem",
				 req->scmbytes, req->nvmebytes);

	if (rc != 0)
		D_ERROR("Failed to extend pool %s: "DF_RC"\n", req->uuid,
			DP_RC(rc));

	d_rank_list_free(svc_ranks);

out_list:
	d_rank_list_free(rank_list);
out:
	resp.status = rc;
	len = mgmt__pool_extend_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_extend_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_extend_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_pool_reintegrate(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc		alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolReintegrateReq	*req = NULL;
	Mgmt__PoolReintegrateResp	resp;
	d_rank_list_t			*svc_ranks = NULL;
	uint8_t				*body;
	size_t				len;
	int				rc;

	mgmt__pool_reintegrate_resp__init(&resp);

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_reintegrate_req__unpack(&alloc.alloc,
						 drpc_req->body.len,
						 drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (Reintegrate target)\n");
		return;
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = pool_change_target_state(req->uuid, svc_ranks,
				      req->n_targetidx, req->targetidx,
				      req->rank, PO_COMP_ST_UP);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_reintegrate_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_reintegrate_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_reintegrate_req__free_unpacked(req, &alloc.alloc);
}

void ds_mgmt_drpc_pool_set_prop(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolSetPropReq	*req = NULL;
	Mgmt__PoolSetPropResp	 resp = MGMT__POOL_SET_PROP_RESP__INIT;
	daos_prop_t		*new_prop = NULL;
	daos_prop_t		*result = NULL;
	struct daos_prop_entry	*entry = NULL;
	uuid_t			 uuid;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_set_prop_req__unpack(&alloc.alloc, drpc_req->body.len,
					     drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (pool setprop)\n");
		return;
	}

	rc = uuid_parse(req->uuid, uuid);
	if (rc != 0) {
		D_ERROR("Couldn't parse '%s' to UUID\n", req->uuid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_INFO(DF_UUID": received request to set pool property\n",
	       DP_UUID(uuid));

	new_prop = daos_prop_alloc(1);
	if (new_prop == NULL) {
		D_ERROR("Failed to allocate daos property\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	if (req->property_case != MGMT__POOL_SET_PROP_REQ__PROPERTY_NUMBER) {
		D_ERROR("Pool property request must be numeric\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	new_prop->dpp_entries[0].dpe_type = req->number;

	switch (req->value_case) {
	case MGMT__POOL_SET_PROP_REQ__VALUE_STRVAL:
		if (req->strval == NULL) {
			D_ERROR("string value is NULL\n");
			D_GOTO(out, rc = -DER_PROTO);
		}

		entry = &new_prop->dpp_entries[0];
		D_STRNDUP(entry->dpe_str, req->strval,
			  DAOS_PROP_LABEL_MAX_LEN);
		if (entry->dpe_str == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		break;
	case MGMT__POOL_SET_PROP_REQ__VALUE_NUMVAL:
		new_prop->dpp_entries[0].dpe_val = req->numval;
		break;
	default:
		D_ERROR("Pool property request with no value (%d)\n",
			req->value_case);
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_set_prop(uuid, svc_ranks, new_prop, &result);
	if (rc != 0) {
		D_ERROR("Failed to set pool property on "DF_UUID": "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto out_ranks;
	}

	if (result == NULL) {
		D_ERROR("Null set pool property response\n");
		D_GOTO(out_ranks, rc = -DER_NOMEM);
	}

	entry = daos_prop_entry_get(result, req->number);
	if (entry == NULL) {
		D_ERROR("Did not receive property %d in result\n",
			req->number);
		D_GOTO(out_result, rc = -DER_INVAL);
	}

	if (entry->dpe_type != req->number) {
		D_ERROR("Property req/resp mismatch (%d != %d)",
			entry->dpe_type, req->number);
		D_GOTO(out_result, rc = -DER_INVAL);
	}

	resp.property_case = MGMT__POOL_SET_PROP_RESP__PROPERTY_NUMBER;
	resp.number = entry->dpe_type;

	switch (req->value_case) {
	case MGMT__POOL_SET_PROP_REQ__VALUE_STRVAL:
		if (entry->dpe_str == NULL)
			D_GOTO(out_result, rc = -DER_INVAL);
		D_ASPRINTF(resp.strval, "%s", entry->dpe_str);
		if (resp.strval == NULL)
			D_GOTO(out_result, rc = -DER_NOMEM);
		resp.value_case = MGMT__POOL_SET_PROP_RESP__VALUE_STRVAL;
		break;
	case MGMT__POOL_SET_PROP_REQ__VALUE_NUMVAL:
		resp.numval = entry->dpe_val;
		resp.value_case = MGMT__POOL_SET_PROP_RESP__VALUE_NUMVAL;
		break;
	default:
		D_ERROR("Pool property response with no value (%d)\n",
			req->value_case);
		D_GOTO(out_result, rc = -DER_INVAL);
	}

out_result:
	daos_prop_free(result);
out_ranks:
	d_rank_list_free(svc_ranks);
out:
	daos_prop_free(new_prop);

	resp.status = rc;
	len = mgmt__pool_set_prop_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__pool_set_prop_resp__pack(&resp, body);
		drpc_resp->body.len  = len;
		drpc_resp->body.data = body;
	}

	if (req->value_case == MGMT__POOL_SET_PROP_REQ__VALUE_STRVAL)
		D_FREE(resp.strval);
	mgmt__pool_set_prop_req__free_unpacked(req, &alloc.alloc);
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

static void
free_resp_acl(Mgmt__ACLResp *resp)
{
	free_ace_list(resp->acl, resp->n_acl);
}

static int
add_acl_to_response(struct daos_acl *acl, Mgmt__ACLResp *resp)
{
	char	**ace_list = NULL;
	size_t	ace_nr = 0;
	int	rc;

	if (acl == NULL)
		return 0; /* nothing to do */

	rc = daos_acl_to_strs(acl, &ace_list, &ace_nr);
	if (rc != 0) {
		D_ERROR("Couldn't convert ACL to string list, rc="DF_RC"",
			DP_RC(rc));
		return rc;
	}

	resp->n_acl = ace_nr;
	resp->acl = ace_list;

	return 0;
}

static int
prop_to_acl_response(daos_prop_t *prop, Mgmt__ACLResp *resp)
{
	struct daos_prop_entry	*entry;
	int			rc = 0;

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_ACL);
	if (entry != NULL) {
		rc = add_acl_to_response((struct daos_acl *)entry->dpe_val_ptr,
					 resp);
		if (rc != 0)
			return rc;
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER);
	if (entry != NULL && entry->dpe_str != NULL)
		D_STRNDUP(resp->owneruser, entry->dpe_str,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER_GROUP);
	if (entry != NULL && entry->dpe_str != NULL)
		D_STRNDUP(resp->ownergroup, entry->dpe_str,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);

	return 0;
}

static void
pack_acl_resp(Mgmt__ACLResp *acl_resp, Drpc__Response *drpc_resp)
{
	size_t	len;
	uint8_t	*body;

	len = mgmt__aclresp__get_packed_size(acl_resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate buffer for packed ACLResp\n");
	} else {
		mgmt__aclresp__pack(acl_resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}
}

void
ds_mgmt_drpc_pool_get_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__GetACLReq		*req = NULL;
	Mgmt__ACLResp		resp = MGMT__ACLRESP__INIT;
	int			rc;
	uuid_t			pool_uuid;
	daos_prop_t		*access_prop = NULL;
	d_rank_list_t		*svc_ranks = NULL;

	req = mgmt__get_aclreq__unpack(&alloc.alloc, drpc_req->body.len,
				       drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack GetACLReq\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to get ACL for pool %s\n", req->uuid);

	if (uuid_parse(req->uuid, pool_uuid) != 0) {
		D_ERROR("Couldn't parse '%s' to UUID\n", req->uuid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_get_acl(pool_uuid, svc_ranks, &access_prop);
	if (rc != 0) {
		D_ERROR("Couldn't get pool ACL, rc="DF_RC"\n", DP_RC(rc));
		D_GOTO(out_ranks, rc);
	}

	rc = prop_to_acl_response(access_prop, &resp);
	if (rc != 0)
		D_GOTO(out_acl, rc);

out_acl:
	daos_prop_free(access_prop);
out_ranks:
	d_rank_list_free(svc_ranks);
out:
	resp.status = rc;

	pack_acl_resp(&resp, drpc_resp);
	free_resp_acl(&resp);

	mgmt__get_aclreq__free_unpacked(req, &alloc.alloc);
}

/*
 * Pulls params out of the ModifyACLReq and validates them.
 */
static int
get_params_from_modify_acl_req(Drpc__Call *drpc_req, uuid_t uuid_out,
			       d_rank_list_t **svc_ranks_out,
			       struct daos_acl **acl_out)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__ModifyACLReq	*req = NULL;
	d_rank_list_t		*svc_ranks = NULL;
	int			rc;

	req = mgmt__modify_aclreq__unpack(&alloc.alloc, drpc_req->body.len,
					  drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack ModifyACLReq\n");
		return -DER_PROTO;
	}

	if (uuid_parse(req->uuid, uuid_out) != 0) {
		D_ERROR("Couldn't parse UUID\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_acl_from_strs((const char **)req->acl, req->n_acl, acl_out);
	if (rc != 0) {
		D_ERROR("Couldn't parse requested ACL strings to DAOS ACL, "
			"rc="DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	*svc_ranks_out = svc_ranks;

out:
	mgmt__modify_aclreq__free_unpacked(req, &alloc.alloc);
	return rc;
}

void
ds_mgmt_drpc_pool_overwrite_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__ACLResp	resp = MGMT__ACLRESP__INIT;
	int		rc = 0;
	uuid_t		pool_uuid;
	d_rank_list_t	*svc_ranks = NULL;
	struct daos_acl	*acl = NULL;
	daos_prop_t	*result = NULL;

	rc = get_params_from_modify_acl_req(drpc_req, pool_uuid,
					    &svc_ranks, &acl);
	if (rc == -DER_PROTO) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_mgmt_pool_overwrite_acl(pool_uuid, svc_ranks, acl, &result);
	if (rc != 0) {
		D_ERROR("Couldn't overwrite pool ACL, rc="DF_RC"\n", DP_RC(rc));
		D_GOTO(out_acl, rc);
	}

	rc = prop_to_acl_response(result, &resp);
	daos_prop_free(result);

out_acl:
	d_rank_list_free(svc_ranks);
	daos_acl_free(acl);
out:
	resp.status = rc;

	pack_acl_resp(&resp, drpc_resp);
	free_resp_acl(&resp);
}

void
ds_mgmt_drpc_pool_update_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__ACLResp	resp = MGMT__ACLRESP__INIT;
	int		rc = 0;
	uuid_t		pool_uuid;
	d_rank_list_t	*svc_ranks = NULL;
	struct daos_acl	*acl = NULL;
	daos_prop_t	*result = NULL;

	rc = get_params_from_modify_acl_req(drpc_req, pool_uuid,
					    &svc_ranks, &acl);
	if (rc == -DER_PROTO) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}
	if (rc != 0)
		D_GOTO(out, rc);

	rc = ds_mgmt_pool_update_acl(pool_uuid, svc_ranks, acl, &result);
	if (rc != 0) {
		D_ERROR("Couldn't update pool ACL, rc=%d\n", rc);
		D_GOTO(out_acl, rc);
	}

	rc = prop_to_acl_response(result, &resp);
	daos_prop_free(result);

out_acl:
	d_rank_list_free(svc_ranks);
	daos_acl_free(acl);
out:
	resp.status = rc;

	pack_acl_resp(&resp, drpc_resp);
	free_resp_acl(&resp);
}

void
ds_mgmt_drpc_pool_delete_acl(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__DeleteACLReq	*req;
	Mgmt__ACLResp		resp = MGMT__ACLRESP__INIT;
	int			rc = 0;
	uuid_t			pool_uuid;
	d_rank_list_t		*svc_ranks;
	daos_prop_t		*result = NULL;

	req = mgmt__delete_aclreq__unpack(&alloc.alloc, drpc_req->body.len,
					  drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack DeleteACLReq\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	if (uuid_parse(req->uuid, pool_uuid) != 0) {
		D_ERROR("Couldn't parse UUID\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_delete_acl(pool_uuid, svc_ranks,
				     req->principal, &result);
	if (rc != 0) {
		D_ERROR("Couldn't delete entry from pool ACL, rc=%d\n", rc);
		D_GOTO(out_ranks, rc);
	}

	rc = prop_to_acl_response(result, &resp);
	daos_prop_free(result);

out_ranks:
	d_rank_list_free(svc_ranks);
out:
	resp.status = rc;

	pack_acl_resp(&resp, drpc_resp);
	free_resp_acl(&resp);

	mgmt__delete_aclreq__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_pool_list_cont(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc		alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__ListContReq		*req = NULL;
	Mgmt__ListContResp		 resp = MGMT__LIST_CONT_RESP__INIT;
	uuid_t				 req_uuid;
	d_rank_list_t			*svc_ranks;
	uint8_t				*body;
	size_t				 len;
	struct daos_pool_cont_info	*containers = NULL;
	uint64_t			 containers_len = 0;
	int				 i;
	int				 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__list_cont_req__unpack(&alloc.alloc, drpc_req->body.len,
					  drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (list containers)\n");
		mgmt__list_cont_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	D_INFO("Received request to list containers in DAOS pool %s\n",
		req->uuid);

	/* resp.containers, n_containers are NULL/0 */

	if (uuid_parse(req->uuid, req_uuid) != 0) {
		D_ERROR("Failed to parse pool uuid %s\n", req->uuid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_list_cont(req_uuid, svc_ranks,
				    &containers, &containers_len);
	if (rc != 0) {
		D_ERROR("Failed to list containers in pool %s :%d\n",
			req->uuid, rc);
		D_GOTO(out_ranks, rc);
	}

	if (containers) {
		D_ALLOC_ARRAY(resp.containers, containers_len);
		if (resp.containers == NULL)
			D_GOTO(out_ranks, rc = -DER_NOMEM);
	}
	resp.n_containers = containers_len;

	for (i = 0; i < containers_len; i++) {
		D_ALLOC_PTR(resp.containers[i]);
		if (resp.containers[i] == NULL)
			D_GOTO(out_ranks, rc = -DER_NOMEM);

		mgmt__list_cont_resp__cont__init(resp.containers[i]);

		D_ALLOC(resp.containers[i]->uuid, DAOS_UUID_STR_SIZE);
		if (resp.containers[i]->uuid == NULL)
			D_GOTO(out_ranks, rc = -DER_NOMEM);
		uuid_unparse(containers[i].pci_uuid, resp.containers[i]->uuid);
	}

out_ranks:
	d_rank_list_free(svc_ranks);
out:
	resp.status = rc;
	len = mgmt__list_cont_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__list_cont_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__list_cont_req__free_unpacked(req, &alloc.alloc);

	if (resp.containers) {
		for (i = 0; i < resp.n_containers; i++) {
			if (resp.containers[i]) {
				if (resp.containers[i]->uuid)
					D_FREE(resp.containers[i]->uuid);
				D_FREE(resp.containers[i]);
			}
		}
		D_FREE(resp.containers);
	}

	D_FREE(containers);
}

static void
storage_usage_stats_from_pool_space(Mgmt__StorageUsageStats *stats,
				    struct daos_pool_space *space,
				    unsigned int media_type)
{
	D_ASSERT(media_type < DAOS_MEDIA_MAX);

	stats->total = space->ps_space.s_total[media_type];
	stats->free = space->ps_space.s_free[media_type];
	stats->min = space->ps_free_min[media_type];
	stats->max = space->ps_free_max[media_type];
	stats->mean = space->ps_free_mean[media_type];
}

static void
pool_rebuild_status_from_info(Mgmt__PoolRebuildStatus *rebuild,
			      struct daos_rebuild_status *info)
{
	rebuild->status = info->rs_errno;
	if (rebuild->status == 0) {
		rebuild->objects = info->rs_obj_nr;
		rebuild->records = info->rs_rec_nr;

		if (info->rs_version == 0)
			rebuild->state = MGMT__POOL_REBUILD_STATUS__STATE__IDLE;
		else if (info->rs_done)
			rebuild->state = MGMT__POOL_REBUILD_STATUS__STATE__DONE;
		else
			rebuild->state = MGMT__POOL_REBUILD_STATUS__STATE__BUSY;
	}
}

void
ds_mgmt_drpc_pool_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	int			rc = 0;
	Mgmt__PoolQueryReq	*req;
	Mgmt__PoolQueryResp	resp = MGMT__POOL_QUERY_RESP__INIT;
	Mgmt__StorageUsageStats	scm = MGMT__STORAGE_USAGE_STATS__INIT;
	Mgmt__StorageUsageStats	nvme = MGMT__STORAGE_USAGE_STATS__INIT;
	Mgmt__PoolRebuildStatus	rebuild = MGMT__POOL_REBUILD_STATUS__INIT;
	uuid_t			uuid;
	daos_pool_info_t	pool_info = {0};
	d_rank_list_t		*svc_ranks;
	size_t			len;
	uint8_t			*body;

	req = mgmt__pool_query_req__unpack(&alloc.alloc, drpc_req->body.len,
					   drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack pool query req\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to query DAOS pool %s\n", req->uuid);

	if (uuid_parse(req->uuid, uuid) != 0) {
		D_ERROR("Failed to parse pool uuid %s\n", req->uuid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	pool_info.pi_bits = DPI_ALL;
	rc = ds_mgmt_pool_query(uuid, svc_ranks, &pool_info);
	if (rc != 0) {
		D_ERROR("Failed to query the pool, rc=%d\n", rc);
		D_GOTO(out_ranks, rc);
	}

	/* Populate the response */
	resp.uuid = req->uuid;
	resp.totaltargets = pool_info.pi_ntargets;
	resp.disabledtargets = pool_info.pi_ndisabled;
	resp.activetargets = pool_info.pi_space.ps_ntargets;
	resp.totalnodes = pool_info.pi_nnodes;
	resp.leader = pool_info.pi_leader;
	resp.version = pool_info.pi_map_ver;

	storage_usage_stats_from_pool_space(&scm, &pool_info.pi_space,
					    DAOS_MEDIA_SCM);
	resp.scm = &scm;

	storage_usage_stats_from_pool_space(&nvme, &pool_info.pi_space,
					    DAOS_MEDIA_NVME);
	resp.nvme = &nvme;

	pool_rebuild_status_from_info(&rebuild, &pool_info.pi_rebuild_st);
	resp.rebuild = &rebuild;

out_ranks:
	d_rank_list_free(svc_ranks);
out:
	resp.status = rc;

	len = mgmt__pool_query_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__pool_query_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_query_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_smd_list_devs(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__SmdDevReq		*req = NULL;
	Mgmt__SmdDevResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 i;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__smd_dev_req__unpack(&alloc.alloc,
					drpc_req->body.len,
					drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (smd list devs)\n");
		return;
	}

	D_INFO("Received request to list SMD devices\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__smd_dev_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__smd_dev_resp__init(resp);

	rc = ds_mgmt_smd_list_devs(resp);
	if (rc != 0)
		D_ERROR("Failed to list SMD devices :"DF_RC"\n", DP_RC(rc));

	resp->status = rc;
	len = mgmt__smd_dev_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__smd_dev_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__smd_dev_req__free_unpacked(req, &alloc.alloc);

	/* all devs should already be freed upon error */
	if (rc != 0)
		goto out;

	for (i = 0; i < resp->n_devices; i++) {
		if (resp->devices[i] != NULL) {
			if (resp->devices[i]->uuid != NULL)
				D_FREE(resp->devices[i]->uuid);
			if (resp->devices[i]->tgt_ids != NULL)
				D_FREE(resp->devices[i]->tgt_ids);
			if (resp->devices[i]->state != NULL)
				D_FREE(resp->devices[i]->state);
			if (resp->devices[i]->traddr != NULL)
				D_FREE(resp->devices[i]->traddr);
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
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__SmdPoolReq	*req = NULL;
	Mgmt__SmdPoolResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 i;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__smd_pool_req__unpack(&alloc.alloc,
					 drpc_req->body.len,
					 drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (smd list pools)\n");
		return;
	}

	D_INFO("Received request to list SMD pools\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__smd_pool_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__smd_pool_resp__init(resp);

	rc = ds_mgmt_smd_list_pools(resp);
	if (rc != 0)
		D_ERROR("Failed to list SMD pools :"DF_RC"\n", DP_RC(rc));

	resp->status = rc;
	len = mgmt__smd_pool_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__smd_pool_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__smd_pool_req__free_unpacked(req, &alloc.alloc);

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
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__BioHealthReq	*req = NULL;
	Mgmt__BioHealthResp	*resp = NULL;
	struct mgmt_bio_health	*bio_health = NULL;
	struct nvme_stats	 stats;
	uuid_t			 uuid;
	uint8_t			*body;
	size_t			 len;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__bio_health_req__unpack(&alloc.alloc,
					   drpc_req->body.len,
					   drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (bio health query)\n");
		return;
	}

	D_INFO("Received request to query BIO health data\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__bio_health_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__bio_health_resp__init(resp);

	if (strlen(req->dev_uuid) != 0) {
		rc = uuid_parse(req->dev_uuid, uuid);
		if (rc != 0) {
			D_ERROR("Unable to parse device UUID %s: "DF_RC"\n",
				req->dev_uuid, DP_RC(rc));
			D_GOTO(out, rc = -DER_INVAL);
		}
	} else
		uuid_clear(uuid); /* need to set uuid = NULL */

	D_ALLOC_PTR(bio_health);
	if (bio_health == NULL) {
		D_ERROR("Failed to allocate bio health struct\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = ds_mgmt_bio_health_query(bio_health, uuid, req->tgt_id);
	if (rc != 0) {
		D_ERROR("Failed to query BIO health data :"DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	D_ALLOC(resp->dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->dev_uuid == NULL) {
		D_ERROR("failed to allocate buffer");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	uuid_unparse_lower(bio_health->mb_devid, resp->dev_uuid);
	stats = bio_health->mb_dev_state;
	resp->timestamp = stats.timestamp;
	resp->warn_temp_time = stats.warn_temp_time;
	resp->crit_temp_time = stats.crit_temp_time;
	resp->ctrl_busy_time = stats.ctrl_busy_time;
	resp->power_cycles = stats.power_cycles;
	resp->power_on_hours = stats.power_on_hours;
	resp->unsafe_shutdowns = stats.unsafe_shutdowns;
	resp->err_log_entries = stats.err_log_entries;
	resp->temperature = stats.temperature;
	resp->media_errs = stats.media_errs;
	resp->bio_read_errs = stats.bio_read_errs;
	resp->bio_write_errs = stats.bio_write_errs;
	resp->bio_unmap_errs = stats.bio_unmap_errs;
	resp->checksum_errs = stats.checksum_errs;
	resp->temp_warn = stats.temp_warn;
	resp->avail_spare_warn = stats.avail_spare_warn;
	resp->read_only_warn = stats.read_only_warn;
	resp->dev_reliability_warn = stats.dev_reliability_warn;
	resp->volatile_mem_warn = stats.volatile_mem_warn;

	D_STRNDUP(resp->model, stats.model, HEALTH_STAT_STR_LEN);
	if (resp->model == NULL) {
		D_ERROR("failed to allocate model ID buffer");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	D_STRNDUP(resp->serial, stats.serial, HEALTH_STAT_STR_LEN);
	if (resp->serial == NULL) {
		D_ERROR("failed to allocate serial ID buffer");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	resp->total_bytes = stats.total_bytes;
	resp->avail_bytes = stats.avail_bytes;
out:
	resp->status = rc;
	len = mgmt__bio_health_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__bio_health_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__bio_health_req__free_unpacked(req, &alloc.alloc);
	D_FREE(resp);

	if (bio_health != NULL)
		D_FREE(bio_health);
}

void
ds_mgmt_drpc_dev_state_query(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__DevStateReq	*req = NULL;
	Mgmt__DevStateResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	uuid_t			 uuid;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__dev_state_req__unpack(&alloc.alloc,
					  drpc_req->body.len,
					  drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (dev state query)\n");
		return;
	}

	D_INFO("Received request to query device state\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__dev_state_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__dev_state_resp__init(resp);

	if (strlen(req->dev_uuid) != 0) {
		if (uuid_parse(req->dev_uuid, uuid) != 0) {
			D_ERROR("Unable to parse device UUID %s: %d\n",
				req->dev_uuid, rc);
			uuid_clear(uuid);
		}
	} else
		uuid_clear(uuid); /* need to set uuid = NULL */

	rc = ds_mgmt_dev_state_query(uuid, resp);
	if (rc != 0)
		D_ERROR("Failed to query device state :%d\n", rc);

	resp->status = rc;
	len = mgmt__dev_state_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__dev_state_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__dev_state_req__free_unpacked(req, &alloc.alloc);

	if (rc == 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
	}

	D_FREE(resp);
}

void
ds_mgmt_drpc_dev_set_faulty(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__DevStateReq	*req = NULL;
	Mgmt__DevStateResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	uuid_t			 uuid;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__dev_state_req__unpack(&alloc.alloc,
					  drpc_req->body.len,
					  drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (dev state set faulty)\n");
		return;
	}

	D_INFO("Received request to set device state to FAULTY\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__dev_state_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__dev_state_resp__init(resp);

	if (strlen(req->dev_uuid) != 0) {
		if (uuid_parse(req->dev_uuid, uuid) != 0) {
			D_ERROR("Unable to parse device UUID %s: %d\n",
				req->dev_uuid, rc);
			uuid_clear(uuid);
		}
	} else
		uuid_clear(uuid); /* need to set uuid = NULL */

	rc = ds_mgmt_dev_set_faulty(uuid, resp);
	if (rc != 0)
		D_ERROR("Failed to set FAULTY device state :%d\n", rc);

	resp->status = rc;
	len = mgmt__dev_state_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__dev_state_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__dev_state_req__free_unpacked(req, &alloc.alloc);

	if (rc == 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
	}

	D_FREE(resp);
}

void
ds_mgmt_drpc_dev_replace(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__DevReplaceReq	*req = NULL;
	Mgmt__DevReplaceResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	uuid_t			 old_uuid;
	uuid_t			 new_uuid;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__dev_replace_req__unpack(&alloc.alloc,
					    drpc_req->body.len,
					    drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (dev replace)\n");
		return;
	}

	if (strlen(req->old_dev_uuid) == 0) {
		D_ERROR("Old device UUID for replacement command is empty\n");
		drpc_resp->status = DRPC__STATUS__FAILURE;
		mgmt__dev_replace_req__free_unpacked(req, &alloc.alloc);
		return;
	}
	if (strlen(req->new_dev_uuid) == 0) {
		D_ERROR("New device UUID for replacement command is empty\n");
		drpc_resp->status = DRPC__STATUS__FAILURE;
		mgmt__dev_replace_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	D_INFO("Received request to replace device %s with device %s\n",
	       req->old_dev_uuid, req->new_dev_uuid);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__dev_replace_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__dev_replace_resp__init(resp);

	if (uuid_parse(req->old_dev_uuid, old_uuid) != 0) {
		D_ERROR("Unable to parse device UUID %s: %d\n",
			req->old_dev_uuid, rc);
		uuid_clear(old_uuid); /* need to set uuid = NULL */
	}

	if (uuid_parse(req->new_dev_uuid, new_uuid) != 0) {
		D_ERROR("Unable to parse device UUID %s: %d\n",
			req->new_dev_uuid, rc);
		uuid_clear(new_uuid); /* need to set uuid = NULL */
	}

	/* TODO: Implement no-reint device replacement option */

	rc = ds_mgmt_dev_replace(old_uuid, new_uuid, resp);
	if (rc != 0)
		D_ERROR("Failed to replace device :%d\n", rc);

	resp->status = rc;
	len = mgmt__dev_replace_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__dev_replace_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__dev_replace_req__free_unpacked(req, &alloc.alloc);

	if (rc == 0) {
		if (resp->dev_state != NULL)
			D_FREE(resp->dev_state);
		if (resp->new_dev_uuid != NULL)
			D_FREE(resp->new_dev_uuid);
	}

	D_FREE(resp);
}

void
ds_mgmt_drpc_dev_identify(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__DevIdentifyReq	*req = NULL;
	Mgmt__DevIdentifyResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	uuid_t			 dev_uuid;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__dev_identify_req__unpack(&alloc.alloc,
					     drpc_req->body.len,
					     drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to unpack req (dev identify)\n");
		return;
	}

	if (strlen(req->dev_uuid) == 0) {
		D_ERROR("Device UUID for identify command is empty\n");
		drpc_resp->status = DRPC__STATUS__FAILURE;
		mgmt__dev_identify_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	D_INFO("Received request to identify device %s\n", req->dev_uuid);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate daos response ref\n");
		mgmt__dev_identify_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	mgmt__dev_identify_resp__init(resp);
	/* Init empty strings to NULL to avoid error cleanup with free */
	resp->dev_uuid = NULL;
	resp->led_state = NULL;

	if (uuid_parse(req->dev_uuid, dev_uuid) != 0) {
		D_ERROR("Unable to parse device UUID %s: %d\n",
			req->dev_uuid, rc);
		uuid_clear(dev_uuid); /* need to set uuid = NULL */
	}

	rc = ds_mgmt_dev_identify(dev_uuid, resp);
	if (rc != 0)
		D_ERROR("Failed to set LED to IDENTIFY on device:%s\n",
			req->dev_uuid);

	resp->status = rc;
	len = mgmt__dev_identify_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		D_ERROR("Failed to allocate drpc response body\n");
	} else {
		mgmt__dev_identify_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__dev_identify_req__free_unpacked(req, &alloc.alloc);

	if (rc == 0) {
		if (resp->led_state != NULL)
			D_FREE(resp->led_state);
		if (resp->dev_uuid != NULL)
			D_FREE(resp->dev_uuid);
	}

	D_FREE(resp);
}



void
ds_mgmt_drpc_set_up(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosResp	resp = MGMT__DAOS_RESP__INIT;

	D_INFO("Received request to setup server\n");

	dss_init_state_set(DSS_INIT_STATE_SET_UP);

	pack_daos_response(&resp, drpc_resp);
}

void
ds_mgmt_drpc_cont_set_owner(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__ContSetOwnerReq	*req = NULL;
	Mgmt__ContSetOwnerResp	 resp = MGMT__CONT_SET_OWNER_RESP__INIT;
	uint8_t			*body;
	size_t			 len;
	uuid_t			 pool_uuid, cont_uuid;
	d_rank_list_t		*svc_ranks = NULL;
	int			 rc = 0;

	req = mgmt__cont_set_owner_req__unpack(&alloc.alloc, drpc_req->body.len,
					       drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack req (cont set owner)\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to change container owner\n");

	if (uuid_parse(req->contuuid, cont_uuid) != 0) {
		D_ERROR("Container UUID is invalid\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (uuid_parse(req->pooluuid, pool_uuid) != 0) {
		D_ERROR("Pool UUID is invalid\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_cont_set_owner(pool_uuid, svc_ranks, cont_uuid,
				    req->owneruser, req->ownergroup);
	if (rc != 0)
		D_ERROR("Set owner failed: %d\n", rc);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__cont_set_owner_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		D_ERROR("Failed to allocate response body\n");
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__cont_set_owner_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__cont_set_owner_req__free_unpacked(req, &alloc.alloc);
}
