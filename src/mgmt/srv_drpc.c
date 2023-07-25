/*
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS Engine. It implements the handlers to
 * process incoming dRPC requests for management tasks.
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <signal.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/pool.h>
#include <daos_api.h>
#include <daos_security.h>

#include "svc.pb-c.h"
#include "acl.pb-c.h"
#include "pool.pb-c.h"
#include "cont.pb-c.h"
#include "server.pb-c.h"
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
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
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
	ds_pool_disable_exclude();
#endif

	D_INFO("Service rank %d is being prepared for controlled shutdown\n",
		req->rank);

	pack_daos_response(&resp, drpc_resp);
	mgmt__prep_shutdown_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_ping_rank(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
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

	/* TODO: verify engine components are functioning as expected */

	pack_daos_response(&resp, drpc_resp);
	mgmt__ping_rank_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_set_log_masks(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	Ctl__SetLogMasksReq	*req = NULL;
	Ctl__SetLogMasksResp	 resp;
	uint8_t			*body;
	size_t			 len;
	char			 retbuf[1024];

	/* Unpack the inner request from the drpc call body */
	req = ctl__set_log_masks_req__unpack(&alloc.alloc,
					     drpc_req->body.len,
					     drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (set log masks)\n");
		return;
	}

	/** Response status is populated with SUCCESS (0) on init */
	ctl__set_log_masks_resp__init(&resp);

	/**
	 * Assuming req->masks and req->streams are null terminated strings, update engine log
	 * masks and debug stream mask.
	 */
	d_log_sync_mask_ex(req->masks, req->streams);

	/** Check settings have persisted */
	d_log_getmasks(retbuf, 0, sizeof(retbuf), 0);
	D_INFO("Received request to set log masks '%s' masks are now %s, debug streams (DD_MASK) "
	       "set to '%s'\n", req->masks, retbuf, req->streams);

	len = ctl__set_log_masks_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		ctl__set_log_masks_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	ctl__set_log_masks_req__free_unpacked(req, &alloc.alloc);
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

	D_INFO("Received request to set rank to %u and map_version to %u\n", req->rank,
	       req->map_version);

	rc = crt_rank_self_set(req->rank, req->map_version);
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
	req = mgmt__group_update_req__unpack(&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (group_update)\n");
		return;
	}

	D_INFO("Received request to update group map with %zu ranks.\n",
	       req->n_engines);

	D_ALLOC_ARRAY(in.gui_servers, req->n_engines);
	if (in.gui_servers == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	for (i = 0; i < req->n_engines; i++) {
		in.gui_servers[i].se_rank = req->engines[i]->rank;
		in.gui_servers[i].se_incarnation = req->engines[i]->incarnation;
		in.gui_servers[i].se_uri = req->engines[i]->uri;
	}
	in.gui_n_servers = req->n_engines;
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
	} else {
		mgmt__group_update_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__group_update_req__free_unpacked(req, &alloc.alloc);
}

static int
conv_req_props(daos_prop_t **out_prop, bool set_props,
	       Mgmt__PoolProperty **req_props, size_t n_props)
{
	daos_prop_t		*new_props = NULL;
	struct daos_prop_entry	*entry = NULL;
	int			 i, rc = 0;

	D_ASSERT(out_prop != NULL);
	D_ASSERT(req_props != NULL);

	if (n_props < 1)
		return 0;

	new_props = daos_prop_alloc(n_props);
	if (new_props == NULL) {
		return -DER_NOMEM;
	}

	for (i = 0; i < n_props; i++) {
		entry = &new_props->dpp_entries[i];
		entry->dpe_type = req_props[i]->number;

		if (!set_props)
			continue;

		switch (req_props[i]->value_case) {
		case MGMT__POOL_PROPERTY__VALUE_STRVAL:
			if (req_props[i]->strval == NULL) {
				D_ERROR("string value is NULL\n");
				D_GOTO(out, rc = -DER_PROTO);
			}

			D_STRNDUP(entry->dpe_str, req_props[i]->strval,
				DAOS_PROP_LABEL_MAX_LEN);
			if (entry->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		case MGMT__POOL_PROPERTY__VALUE_NUMVAL:
			entry->dpe_val = req_props[i]->numval;
			break;
		default:
			D_ERROR("Pool property request with no value (%d)\n",
				req_props[i]->value_case);
			D_GOTO(out, rc = -DER_INVAL);
		}
	}
out:
	if (rc != 0)
		daos_prop_free(new_props);
	else
		*out_prop = new_props;

	return rc;
}

static int
create_pool_props(daos_prop_t **out_prop, uint32_t numsvcreps, char *owner, char *owner_grp,
		  const char **ace_list, size_t ace_nr)
{
	uint64_t	out_svc_rf = DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT;
	char		*out_owner = NULL;
	char		*out_owner_grp = NULL;
	struct daos_acl	*out_acl = NULL;
	daos_prop_t	*new_prop = NULL;
	uint32_t	entries = 1;
	uint32_t	idx = 0;
	int		rc = 0;

	if (numsvcreps > 0) {
#ifndef DRPC_TEST
		out_svc_rf = ds_pool_svc_rf_from_nreplicas(numsvcreps);
#else
		if (numsvcreps % 2 == 0)
			numsvcreps--;
		out_svc_rf = numsvcreps / 2;
#endif
		if (!daos_svc_rf_is_valid(out_svc_rf)) {
			D_ERROR("invalid numsvcreps %u\n", numsvcreps);
			rc = -DER_INVAL;
			goto err_out;
		}
	}

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

	new_prop = daos_prop_alloc(entries);
	if (new_prop == NULL)
		D_GOTO(err_out, rc = -DER_NOMEM);

	new_prop->dpp_entries[idx].dpe_type = DAOS_PROP_PO_SVC_REDUN_FAC;
	new_prop->dpp_entries[idx].dpe_val = out_svc_rf;
	idx++;

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

static int pool_create_fill_resp(Mgmt__PoolCreateResp *resp, uuid_t uuid, d_rank_list_t *svc_ranks)
{
	int			rc = 0;
	int			index;
	d_rank_list_t	       *enabled_ranks = NULL;
	daos_pool_info_t	pool_info = { .pi_bits = DPI_ENGINES_ENABLED | DPI_SPACE };

	D_ASSERT(svc_ranks != NULL);
	D_ASSERT(svc_ranks->rl_nr > 0);

	rc = rank_list_to_uint32_array(svc_ranks, &resp->svc_reps, &resp->n_svc_reps);
	if (rc != 0) {
		D_ERROR("Failed to convert svc rank list: rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DB_MGMT, "%d service replicas\n", svc_ranks->rl_nr);

	rc = ds_mgmt_pool_query(uuid, svc_ranks, &enabled_ranks, &pool_info, NULL, NULL);
	if (rc != 0) {
		D_ERROR("Failed to query created pool: rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	resp->leader = pool_info.pi_leader;

	rc = rank_list_to_uint32_array(enabled_ranks, &resp->tgt_ranks, &resp->n_tgt_ranks);
	if (rc != 0) {
		D_ERROR("Failed to convert enabled target rank list: rc=%d\n", rc);
		D_GOTO(out, rc);
	}

	for (index = 0; index < DAOS_MEDIA_MAX; ++index) {
		D_ASSERT(pool_info.pi_space.ps_space.s_total[index] % resp->n_tgt_ranks == 0);
	}
	D_ALLOC_ARRAY(resp->tier_bytes, DAOS_MEDIA_MAX);
	if (resp->tier_bytes == NULL) {
		rc = -DER_NOMEM;
		D_GOTO(out, rc);
	}
	resp->n_tier_bytes = DAOS_MEDIA_MAX;
	for (index = 0; index < DAOS_MEDIA_MAX; ++index) {
		resp->tier_bytes[index] =
			pool_info.pi_space.ps_space.s_total[index] / resp->n_tgt_ranks;
	}

out:
	d_rank_list_free(enabled_ranks);
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
	daos_prop_t		*req_props = NULL;
	daos_prop_t		*base_props = NULL;
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

	D_INFO("Received request to create pool on %zu ranks.\n", req->n_ranks);

	if (req->n_tierbytes != DAOS_MEDIA_MAX)
		D_GOTO(out, rc = -DER_INVAL);

	if (req->n_properties > 0) {
		rc = conv_req_props(&req_props, true,
				    req->properties, req->n_properties);
		if (rc != 0) {
			D_ERROR("get_req_props() failed: "DF_RC"\n", DP_RC(rc));
			goto out;
		}
	}

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

	rc = create_pool_props(&base_props, req->numsvcreps, req->user, req->usergroup,
			       (const char **)req->acl, req->n_acl);
	if (rc != 0)
		goto out;

	if (req->n_properties > 0) {
		prop = daos_prop_merge(base_props, req_props);
		if (prop == NULL) {
			D_GOTO(out, rc = -DER_NOMEM);
		}
	} else {
		prop = base_props;
		base_props = NULL;
	}

	/* Ranks to allocate targets (in) & svc for pool replicas (out). */
	rc = ds_mgmt_create_pool(pool_uuid, req->sys, "pmem", targets,
				 req->tierbytes[DAOS_MEDIA_SCM], req->tierbytes[DAOS_MEDIA_NVME],
				 prop, &svc, req->n_faultdomains, req->faultdomains);
	if (rc != 0) {
		D_ERROR("failed to create pool: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = pool_create_fill_resp(&resp, pool_uuid, svc);
	d_rank_list_free(svc);

out:
	resp.status = rc;
	len = mgmt__pool_create_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__pool_create_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_create_req__free_unpacked(req, &alloc.alloc);

	daos_prop_free(base_props);
	daos_prop_free(req_props);
	daos_prop_free(prop);

	if (targets != NULL)
		d_rank_list_free(targets);

	D_FREE(resp.tier_bytes);
	D_FREE(resp.tgt_ranks);
	D_FREE(resp.svc_reps);
}

void
ds_mgmt_drpc_pool_destroy(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolDestroyReq	*req = NULL;
	Mgmt__PoolDestroyResp	 resp = MGMT__POOL_DESTROY_RESP__INIT;
	uuid_t			 uuid;
	d_rank_list_t		*ranks = NULL;
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

	D_INFO("Received request to destroy pool %s\n", req->id);

	rc = uuid_parse(req->id, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->id,
			DP_RC(rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	/*
	 * Note that req->svc_ranks in this dRPC indicates on which ranks we
	 * shall attempt to destroy the pool, not the set of PS ranks, despite
	 * the name. See the caller code in mgmtSvc.PoolDestroy.
	 */
	ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_destroy_pool(uuid, ranks);
	if (rc != 0) {
		D_ERROR("Failed to destroy pool %s: "DF_RC"\n", req->id,
			DP_RC(rc));
	}

	d_rank_list_free(ranks);

out:
	resp.status = rc;
	len = mgmt__pool_destroy_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
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
	uuid_t			*handles = NULL;
	int			 n_handles = 0;
	char			*machine = NULL;
	uint32_t		 count = 0;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			 len;
	uint32_t		 destroy = 0;
	uint32_t		 force_destroy = 0;
	int			 rc;
	int			 i;


	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_evict_req__unpack(&alloc.alloc,
					   drpc_req->body.len,
					   drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (evict pool_connections)\n");
		return;
	}

	D_INFO("Received request to evict pool connections %s\n", req->id);

	rc = uuid_parse(req->id, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->id,
			DP_RC(rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (req->handles) {
		D_ALLOC(handles, sizeof(uuid_t) * req->n_handles);
		if (handles == NULL) {
			d_rank_list_free(svc_ranks);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		for (i = 0; i < req->n_handles; i++) {
			rc = uuid_parse(req->handles[i], handles[i]);
			if (rc != 0) {
				D_ERROR("Unable to parse handle UUID %s: "
				DF_RC"\n", req->id, DP_RC(rc));
				D_GOTO(out_free, rc = -DER_INVAL);
			}
		}
		n_handles = req->n_handles;
	} else if (req->destroy) {
		handles = NULL;
		n_handles = 0;
		destroy = 1;
		force_destroy = (req->force_destroy ? 1 : 0);
	} else if (req->machine != protobuf_c_empty_string) {
		machine = req->machine;
	} else {
		handles = NULL;
		n_handles = 0;
	}

	rc = ds_mgmt_evict_pool(uuid, svc_ranks, handles, n_handles, destroy, force_destroy,
				machine, &count);
	if (rc != 0)
		D_ERROR("Failed to evict pool connections %s: "DF_RC"\n", req->id, DP_RC(rc));

out_free:
	d_rank_list_free(svc_ranks);
	D_FREE(handles);

out:
	resp.status = rc;
	resp.count = count;
	len = mgmt__pool_evict_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__pool_evict_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_evict_req__free_unpacked(req, &alloc.alloc);
}

static int
pool_change_target_state(char *id, d_rank_list_t *svc_ranks, size_t n_targetidx,
			 uint32_t *targetidx, uint32_t rank, pool_comp_state_t state,
			 size_t scm_size, size_t nvme_size)
{
	uuid_t				uuid;
	struct pool_target_addr_list	target_addr_list;
	int				num_addrs;
	int				rc, i;

	num_addrs = (n_targetidx > 0) ? n_targetidx : 1;
	rc = uuid_parse(id, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", id,
			DP_RC(rc));
		return -DER_INVAL;
	}

	rc = pool_target_addr_list_alloc(num_addrs, &target_addr_list);
	if (rc)
		return rc;

	if (n_targetidx > 0) {
		for (i = 0; i < n_targetidx; ++i) {
			target_addr_list.pta_addrs[i].pta_target = targetidx[i];
			target_addr_list.pta_addrs[i].pta_rank = rank;
		}
	} else {
		target_addr_list.pta_addrs[0].pta_target = -1;
		target_addr_list.pta_addrs[0].pta_rank = rank;
	}

	rc = ds_mgmt_pool_target_update_state(uuid, svc_ranks, &target_addr_list, state, scm_size,
					      nvme_size);
	if (rc != 0) {
		D_ERROR("Failed to set pool target up "DF_UUID": "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
	}

	pool_target_addr_list_free(&target_addr_list);
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

	rc = pool_change_target_state(req->id, svc_ranks, req->n_targetidx, req->targetidx,
				      req->rank, PO_COMP_ST_DOWN, 0 /* scm_size */,
				      0 /* nvme_size */);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_exclude_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
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

	rc = pool_change_target_state(req->id, svc_ranks, req->n_targetidx, req->targetidx,
				      req->rank, PO_COMP_ST_DRAIN, 0 /* scm_size */,
				      0 /* nvme_size */);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_drain_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
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
	uint64_t		scm_bytes, nvme_bytes = 0;
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

	if (req->n_tierbytes == 0 || req->n_tierbytes > DAOS_MEDIA_MAX) {
		D_ERROR("Invalid number of storage tiers: "DF_U64"\n",
			req->n_tierbytes);
		D_GOTO(out, rc = -DER_INVAL);
	}

	scm_bytes = req->tierbytes[DAOS_MEDIA_SCM];
	if (req->n_tierbytes > DAOS_MEDIA_NVME) {
		nvme_bytes = req->tierbytes[DAOS_MEDIA_NVME];
	}

	rc = uuid_parse(req->id, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->id,
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
				 scm_bytes, nvme_bytes,
				 req->n_faultdomains, req->faultdomains);

	if (rc != 0)
		D_ERROR("Failed to extend pool %s: "DF_RC"\n", req->id,
			DP_RC(rc));

	d_rank_list_free(svc_ranks);

	/* For the moment, just echo back the allocations from the request.
	 * In the future, we may need to adjust the allocations somehow and
	 * this is how we would let the caller know.
	 */
	resp.n_tier_bytes = req->n_tierbytes;
	resp.tier_bytes = req->tierbytes;

out_list:
	d_rank_list_free(rank_list);
out:
	resp.status = rc;
	len = mgmt__pool_extend_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
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
	uint64_t			scm_bytes;
	uint64_t			nvme_bytes = 0;
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

	if (req->n_tierbytes == 0 || req->n_tierbytes > DAOS_MEDIA_MAX) {
		D_ERROR("Invalid number of storage tiers: "DF_U64"\n",
			req->n_tierbytes);
		D_GOTO(out, rc = -DER_INVAL);
	}

	scm_bytes = req->tierbytes[DAOS_MEDIA_SCM];
	if (req->n_tierbytes > DAOS_MEDIA_NVME) {
		nvme_bytes = req->tierbytes[DAOS_MEDIA_NVME];
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = pool_change_target_state(req->id, svc_ranks, req->n_targetidx, req->targetidx,
				      req->rank, PO_COMP_ST_UP, scm_bytes, nvme_bytes);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_reintegrate_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
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
	daos_prop_t		*new_props = NULL;
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

	rc = uuid_parse(req->id, uuid);
	if (rc != 0) {
		D_ERROR("Couldn't parse '%s' to UUID\n", req->id);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_INFO(DF_UUID": received request to set pool properties\n",
	       DP_UUID(uuid));

	rc = conv_req_props(&new_props, true,
			    req->properties, req->n_properties);
	if (rc != 0)
		goto out;

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_set_prop(uuid, svc_ranks, new_props);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to set pool properties: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto out_ranks;
	}

out_ranks:
	d_rank_list_free(svc_ranks);
out:
	daos_prop_free(new_props);

	resp.status = rc;
	len = mgmt__pool_set_prop_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__pool_set_prop_resp__pack(&resp, body);
		drpc_resp->body.len  = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_set_prop_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_pool_upgrade(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolUpgradeReq	*req = NULL;
	Mgmt__PoolUpgradeResp	 resp = MGMT__POOL_UPGRADE_RESP__INIT;
	uuid_t			 uuid;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_upgrade_req__unpack(&alloc.alloc,
					     drpc_req->body.len,
					     drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (upgrade pool)\n");
		return;
	}

	D_INFO("Received request to upgrade pool %s\n", req->id);

	rc = uuid_parse(req->id, uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", req->id,
			DP_RC(rc));
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_upgrade(uuid, svc_ranks);

	d_rank_list_free(svc_ranks);

out:
	resp.status = rc;
	len = mgmt__pool_upgrade_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__pool_upgrade_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_upgrade_req__free_unpacked(req, &alloc.alloc);
}

void
free_response_props(Mgmt__PoolProperty **props, size_t n_props)
{
	int i;

	for (i = 0; i < n_props; i++) {
		if (props[i]->value_case == MGMT__POOL_PROPERTY__VALUE_STRVAL)
			D_FREE(props[i]->strval);
		D_FREE(props[i]);
	}

	D_FREE(props);
}

static int
add_props_to_resp(daos_prop_t *prop, Mgmt__PoolGetPropResp *resp)
{
	Mgmt__PoolProperty	**resp_props;
	struct daos_prop_entry	*entry;
	int			 i, rc = 0;
	int			 valid_prop_nr = 0;
	int			 j = 0;

	if (prop == NULL || prop->dpp_nr == 0)
		return 0;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		if (daos_prop_is_set(entry))
			valid_prop_nr++;
	}

	if (valid_prop_nr == 0)
		return 0;

	D_ALLOC_ARRAY(resp_props, valid_prop_nr);
	if (resp_props == NULL)
		return -DER_NOMEM;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		if (!daos_prop_is_set(entry))
			continue;
		D_ALLOC(resp_props[j], sizeof(Mgmt__PoolProperty));
		if (resp_props[j] == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		mgmt__pool_property__init(resp_props[j]);

		resp_props[j]->number = entry->dpe_type;

		if (daos_prop_has_str(entry)) {
			if (entry->dpe_str == NULL) {
				D_ERROR("prop string unset\n");
				D_GOTO(out, rc = -DER_INVAL);
			}

			resp_props[j]->value_case =
				MGMT__POOL_PROPERTY__VALUE_STRVAL;
			D_STRNDUP(resp_props[j]->strval, entry->dpe_str,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (resp_props[j]->strval == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		} else if (daos_prop_has_ptr(entry)) {
			switch (entry->dpe_type) {
			case DAOS_PROP_PO_SVC_LIST:
				if (entry->dpe_val_ptr == NULL) {
					D_ERROR("svc rank list unset\n");
					D_GOTO(out, rc = -DER_INVAL);
				}
				resp_props[j]->strval = d_rank_list_to_str(
					(d_rank_list_t *)entry->dpe_val_ptr);
				if (resp_props[j]->strval == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
				resp_props[j]->value_case =
					MGMT__POOL_PROPERTY__VALUE_STRVAL;
				break;
			default:
				D_ERROR("pointer-value props not supported\n");
				D_GOTO(out, rc = -DER_INVAL);
			}
		} else {
			resp_props[j]->numval = entry->dpe_val;
			resp_props[j]->value_case =
				MGMT__POOL_PROPERTY__VALUE_NUMVAL;
		}
		j++;
	}

	resp->properties = resp_props;
	resp->n_properties = valid_prop_nr;

out:
	if (rc != 0)
		free_response_props(resp_props, valid_prop_nr);

	return rc;
}

void ds_mgmt_drpc_pool_get_prop(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Mgmt__PoolGetPropReq	*req = NULL;
	Mgmt__PoolGetPropResp	 resp = MGMT__POOL_GET_PROP_RESP__INIT;
	daos_prop_t		*props = NULL;
	uuid_t			 uuid;
	d_rank_list_t		*svc_ranks = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 rc;

	/* Unpack the inner request from the drpc call body */
	req = mgmt__pool_get_prop_req__unpack(&alloc.alloc, drpc_req->body.len,
					      drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (pool getprop)\n");
		return;
	}

	rc = uuid_parse(req->id, uuid);
	if (rc != 0) {
		D_ERROR("Couldn't parse '%s' to UUID\n", req->id);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_INFO(DF_UUID": received request to get pool properties\n",
	       DP_UUID(uuid));

	rc = conv_req_props(&props, false, req->properties, req->n_properties);
	if (rc != 0)
		goto out;

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_get_prop(uuid, svc_ranks, props);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to get pool properties: "DF_RC"\n",
			DP_UUID(uuid), DP_RC(rc));
		goto out_ranks;
	}

	rc = add_props_to_resp(props, &resp);
	if (rc != 0)
		goto out_ranks;

out_ranks:
	d_rank_list_free(svc_ranks);
out:
	daos_prop_free(props);

	resp.status = rc;
	len = mgmt__pool_get_prop_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__pool_get_prop_resp__pack(&resp, body);
		drpc_resp->body.len  = len;
		drpc_resp->body.data = body;
	}

	free_response_props(resp.properties, resp.n_properties);
	mgmt__pool_get_prop_req__free_unpacked(req, &alloc.alloc);
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
	if (resp->acl != NULL) {
		if (resp->acl->owner_user && resp->acl->owner_user[0] != '\0')
			D_FREE(resp->acl->owner_user);
		if (resp->acl->owner_group && resp->acl->owner_group[0] != '\0')
			D_FREE(resp->acl->owner_group);
		free_ace_list(resp->acl->entries, resp->acl->n_entries);
		D_FREE(resp->acl);
	}
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
		D_ERROR("Couldn't convert ACL to string list: " DF_RC "\n", DP_RC(rc));
		return rc;
	}

	resp->acl->n_entries = ace_nr;
	resp->acl->entries = ace_list;

	return 0;
}

static int
prop_to_acl_response(daos_prop_t *prop, Mgmt__ACLResp *resp)
{
	struct daos_prop_entry	*entry;
	int			rc = 0;

	D_ALLOC_PTR(resp->acl);
	if (resp->acl == NULL)
		return -DER_NOMEM;
	mgmt__access_control_list__init(resp->acl);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_ACL);
	if (entry != NULL) {
		rc = add_acl_to_response((struct daos_acl *)entry->dpe_val_ptr,
					 resp);
		if (rc != 0)
			return rc;
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER);
	if (entry != NULL && entry->dpe_str != NULL &&
	    entry->dpe_str[0] != '\0')
		D_STRNDUP(resp->acl->owner_user, entry->dpe_str,
			  DAOS_ACL_MAX_PRINCIPAL_LEN);

	entry = daos_prop_entry_get(prop, DAOS_PROP_PO_OWNER_GROUP);
	if (entry != NULL && entry->dpe_str != NULL &&
	    entry->dpe_str[0] != '\0')
		D_STRNDUP(resp->acl->owner_group, entry->dpe_str,
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

	D_INFO("Received request to get ACL for pool %s\n", req->id);

	if (uuid_parse(req->id, pool_uuid) != 0) {
		D_ERROR("Couldn't parse '%s' to UUID\n", req->id);
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

	if (uuid_parse(req->id, uuid_out) != 0) {
		D_ERROR("Couldn't parse UUID\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = daos_acl_from_strs((const char **)req->entries, req->n_entries, acl_out);
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

	if (uuid_parse(req->id, pool_uuid) != 0) {
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
		req->id);

	/* resp.containers, n_containers are NULL/0 */

	if (uuid_parse(req->id, req_uuid) != 0) {
		D_ERROR("Failed to parse pool uuid %s\n", req->id);
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_list_cont(req_uuid, svc_ranks,
				    &containers, &containers_len);
	if (rc != 0) {
		D_ERROR("Failed to list containers in pool %s :%d\n",
			req->id, rc);
		D_GOTO(out_ranks, rc);
	}

	if (containers) {
		D_ALLOC_ARRAY(resp.containers, containers_len);
		if (resp.containers == NULL)
			D_GOTO(out_ranks, rc = -DER_NOMEM);
	}
	resp.n_containers = containers_len;

	D_DEBUG(DB_MGMT, "Found %lu containers in DAOS pool %s\n", containers_len, req->id);

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

	stats->media_type = media_type;
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
		else if (info->rs_state == DRS_COMPLETED)
			rebuild->state = MGMT__POOL_REBUILD_STATUS__STATE__DONE;
		else
			rebuild->state = MGMT__POOL_REBUILD_STATUS__STATE__BUSY;
	}
}

static void
pool_query_free_tier_stats(Mgmt__PoolQueryResp *resp)
{
	if (resp->tier_stats != NULL) {
		D_FREE(resp->tier_stats);
		resp->tier_stats = NULL;
	}
	resp->n_tier_stats = 0;
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
	d_rank_list_t		*ranks;
	d_rank_range_list_t	*range_list;
	char			*range_list_str = NULL;
	bool			truncated;
	size_t			len;
	uint8_t			*body;

	req = mgmt__pool_query_req__unpack(&alloc.alloc, drpc_req->body.len,
					   drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack pool query req\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to query DAOS pool %s\n", req->id);

	if (uuid_parse(req->id, uuid) != 0) {
		D_ERROR("Failed to parse pool uuid %s\n", req->id);
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* TODO (DAOS-10250) Enabled and disabled engines should be retrieve both if needed */
	pool_info.pi_bits = req->include_enabled_ranks ? DPI_ALL : (DPI_ALL & ~DPI_ENGINES_ENABLED);
	rc = ds_mgmt_pool_query(uuid, svc_ranks, &ranks, &pool_info, &resp.pool_layout_ver,
				&resp.upgrade_layout_ver);
	if (rc != 0) {
		D_ERROR("Failed to query the pool, rc=%d\n", rc);
		goto out_svc_ranks;
	}

	/* Calculate and stringify rank ranges to return to control plane for display */
	range_list = d_rank_range_list_create_from_ranks(ranks);
	if (range_list == NULL)
		D_GOTO(out_ranks, rc = -DER_NOMEM);
	range_list_str = d_rank_range_list_str(range_list, &truncated);
	if (range_list_str == NULL)
		D_GOTO(out_ranges, rc = -DER_NOMEM);
	D_DEBUG(DB_MGMT, DF_UUID": %s ranks: %s%s\n", DP_UUID(uuid),
		pool_info.pi_bits & DPI_ENGINES_ENABLED ? "ENABLED" : "DISABLED", range_list_str,
		truncated ? " ...(TRUNCATED)" : "");

	/* Populate the response */
	resp.uuid = req->id;
	resp.total_targets = pool_info.pi_ntargets;
	resp.disabled_targets = pool_info.pi_ndisabled;
	resp.active_targets = pool_info.pi_space.ps_ntargets;
	resp.total_engines = pool_info.pi_nnodes;
	resp.leader = pool_info.pi_leader;
	resp.version = pool_info.pi_map_ver;
	resp.enabled_ranks = (req->include_enabled_ranks) ? range_list_str : "";
	resp.disabled_ranks = (req->include_disabled_ranks) ? range_list_str : "";

	D_ALLOC_ARRAY(resp.tier_stats, DAOS_MEDIA_MAX);
	if (resp.tier_stats == NULL) {
		D_GOTO(out_ranges, rc = -DER_NOMEM);
	}

	storage_usage_stats_from_pool_space(&scm, &pool_info.pi_space,
					    DAOS_MEDIA_SCM);
	resp.tier_stats[DAOS_MEDIA_SCM] = &scm;
	resp.n_tier_stats++;

	storage_usage_stats_from_pool_space(&nvme, &pool_info.pi_space,
					    DAOS_MEDIA_NVME);
	resp.tier_stats[DAOS_MEDIA_NVME] = &nvme;
	resp.n_tier_stats++;

	pool_rebuild_status_from_info(&rebuild, &pool_info.pi_rebuild_st);
	resp.rebuild = &rebuild;

out_ranges:
	d_rank_range_list_free(range_list);
out_ranks:
	d_rank_list_free(ranks);
out_svc_ranks:
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

	D_FREE(range_list_str);

	mgmt__pool_query_req__free_unpacked(req, &alloc.alloc);

	pool_query_free_tier_stats(&resp);
}

void
ds_mgmt_drpc_pool_query_targets(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc		alloc = PROTO_ALLOCATOR_INIT(alloc);
	int				rc = 0;
	Mgmt__PoolQueryTargetReq	*req;
	Mgmt__PoolQueryTargetResp	resp = MGMT__POOL_QUERY_TARGET_RESP__INIT;
	uint32_t			i;
	uuid_t				uuid;
	d_rank_list_t			*svc_ranks;
	d_rank_list_t			*tgts;
	size_t				len;
	uint8_t				*body;
	daos_target_info_t		*infos = NULL;
	Mgmt__PoolQueryTargetInfo	*resp_infos = NULL;

	req = mgmt__pool_query_target_req__unpack(&alloc.alloc, drpc_req->body.len,
						  drpc_req->body.data);
	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack pool query targets req\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to query DAOS pool %s, %zu targets\n", req->id, req->n_targets);

	if (uuid_parse(req->id, uuid) != 0) {
		D_ERROR("Failed to parse pool uuid %s\n", req->id);
		D_GOTO(out, rc = -DER_INVAL);
	}

	svc_ranks = uint32_array_to_rank_list(req->svc_ranks, req->n_svc_ranks);
	if (svc_ranks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	tgts = uint32_array_to_rank_list(req->targets, req->n_targets);
	if (tgts == NULL)
		D_GOTO(out_ranks, rc = -DER_NOMEM);

	rc = ds_mgmt_pool_query_targets(uuid, svc_ranks, req->rank, tgts, &infos);
	if (rc != 0) {
		D_ERROR("ds_mgmt_pool_query_targets() failed, pool %s rank %u, "DF_RC"\n",
			req->id, req->rank, DP_RC(rc));
		goto out_tgts;
	}

	/* Populate the response */
	/* array of pointers to Mgmt__PoolQueryTargetInfo */
	D_ALLOC_ARRAY(resp.infos, req->n_targets);
	if (resp.infos == NULL)
		D_GOTO(out_infos, rc = -DER_NOMEM);
	resp.n_infos = req->n_targets;

	/* array of Mgmt__PoolQueryTargetInfo so we don't have to allocate individually */
	D_ALLOC_ARRAY(resp_infos, req->n_targets);
	if (resp_infos == NULL)
		D_GOTO(out_infos, rc = -DER_NOMEM);

	for (i = 0; i < req->n_targets; i++) {
		int				j;
		Mgmt__StorageTargetUsage	*space;

		resp.infos[i] = &resp_infos[i];
		mgmt__pool_query_target_info__init(resp.infos[i]);

		resp.infos[i]->type = (Mgmt__PoolQueryTargetInfo__TargetType) infos[i].ta_type;
		resp.infos[i]->state = (Mgmt__PoolQueryTargetInfo__TargetState) infos[i].ta_state;
		D_ALLOC_ARRAY(resp.infos[i]->space, DAOS_MEDIA_MAX);
		if (resp.infos[i]->space == NULL)
			D_GOTO(out_infos, rc = -DER_NOMEM);
		resp.infos[i]->n_space = DAOS_MEDIA_MAX;

		D_ALLOC_ARRAY(space, DAOS_MEDIA_MAX);
		if (space == NULL)
			D_GOTO(out_infos, rc = -DER_NOMEM);

		for (j = 0; j < DAOS_MEDIA_MAX; j++) {
			resp.infos[i]->space[j] = &space[j];
			mgmt__storage_target_usage__init(resp.infos[i]->space[j]);

			resp.infos[i]->space[j]->total = infos[i].ta_space.s_total[j];
			resp.infos[i]->space[j]->free = infos[i].ta_space.s_free[j];
			resp.infos[i]->space[j]->media_type = j;
		}
	}

out_infos:
	D_FREE(infos);
out_tgts:
	d_rank_list_free(tgts);
out_ranks:
	d_rank_list_free(svc_ranks);
out:
	resp.status = rc;

	len = mgmt__pool_query_target_resp__get_packed_size(&resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__pool_query_target_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__pool_query_target_req__free_unpacked(req, &alloc.alloc);

	for (i = 0; i < resp.n_infos; i++) {
		D_FREE(resp.infos[i]->space[0]);
		D_FREE(resp.infos[i]->space);
	}
	D_FREE(resp.infos);
	D_FREE(resp_infos);
}

void
ds_mgmt_drpc_smd_list_devs(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Ctl__SmdDevReq		*req = NULL;
	Ctl__SmdDevResp		*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 i;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = ctl__smd_dev_req__unpack(&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (smd list devs)\n");
		return;
	}

	D_INFO("Received request to list SMD devices\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		ctl__smd_dev_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	ctl__smd_dev_resp__init(resp);

	rc = ds_mgmt_smd_list_devs(resp);
	if (rc != 0)
		D_ERROR("Failed to list SMD devices :"DF_RC"\n", DP_RC(rc));

	resp->status = rc;
	len = ctl__smd_dev_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		ctl__smd_dev_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	ctl__smd_dev_req__free_unpacked(req, &alloc.alloc);

	/* all devs should already be freed upon error */
	if (rc != 0)
		goto out;

	for (i = 0; i < resp->n_devices; i++) {
		if (resp->devices[i] != NULL) {
			if (resp->devices[i]->uuid != NULL)
				D_FREE(resp->devices[i]->uuid);
			if (resp->devices[i]->tgt_ids != NULL)
				D_FREE(resp->devices[i]->tgt_ids);
			if (resp->devices[i]->tr_addr != NULL)
				D_FREE(resp->devices[i]->tr_addr);
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
	Ctl__SmdPoolReq		*req = NULL;
	Ctl__SmdPoolResp	*resp = NULL;
	uint8_t			*body;
	size_t			 len;
	int			 i;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = ctl__smd_pool_req__unpack(&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (smd list pools)\n");
		return;
	}

	D_INFO("Received request to list SMD pools\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		ctl__smd_pool_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	ctl__smd_pool_resp__init(resp);

	rc = ds_mgmt_smd_list_pools(resp);
	if (rc != 0)
		D_ERROR("Failed to list SMD pools :"DF_RC"\n", DP_RC(rc));

	resp->status = rc;
	len = ctl__smd_pool_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		ctl__smd_pool_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	ctl__smd_pool_req__free_unpacked(req, &alloc.alloc);

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
	Ctl__BioHealthReq	*req = NULL;
	Ctl__BioHealthResp	*resp = NULL;
	struct mgmt_bio_health	*bio_health = NULL;
	struct nvme_stats	 stats;
	uuid_t			 uuid;
	uint8_t			*body;
	size_t			 len;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = ctl__bio_health_req__unpack(&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		D_ERROR("Failed to unpack req (bio health query)\n");
		return;
	}

	D_INFO("Received request to query BIO health data\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		ctl__bio_health_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	ctl__bio_health_resp__init(resp);

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
	if (bio_health == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	bio_health->mb_meta_size = req->meta_size;
	bio_health->mb_rdb_size = req->rdb_size;
	rc = ds_mgmt_bio_health_query(bio_health, uuid);
	if (rc != 0) {
		D_ERROR("Failed to query BIO health data :"DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	D_ALLOC(resp->dev_uuid, DAOS_UUID_STR_SIZE);
	if (resp->dev_uuid == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

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
	resp->total_bytes = stats.total_bytes;
	resp->avail_bytes = stats.avail_bytes;
	resp->cluster_size = stats.cluster_size;
	resp->meta_wal_size = stats.meta_wal_size;
	resp->rdb_wal_size = stats.rdb_wal_size;
	resp->program_fail_cnt_norm = stats.program_fail_cnt_norm;
	resp->program_fail_cnt_raw = stats.program_fail_cnt_raw;
	resp->erase_fail_cnt_norm = stats.erase_fail_cnt_norm;
	resp->erase_fail_cnt_raw = stats.erase_fail_cnt_raw;
	resp->wear_leveling_cnt_norm = stats.wear_leveling_cnt_norm;
	resp->wear_leveling_cnt_min = stats.wear_leveling_cnt_min;
	resp->wear_leveling_cnt_max = stats.wear_leveling_cnt_max;
	resp->wear_leveling_cnt_avg = stats.wear_leveling_cnt_avg;
	resp->endtoend_err_cnt_raw = stats.endtoend_err_cnt_raw;
	resp->crc_err_cnt_raw = stats.crc_err_cnt_raw;
	resp->media_wear_raw = stats.media_wear_raw;
	resp->host_reads_raw = stats.host_reads_raw;
	resp->workload_timer_raw = stats.workload_timer_raw;
	resp->thermal_throttle_status = stats.thermal_throttle_status;
	resp->thermal_throttle_event_cnt = stats.thermal_throttle_event_cnt;
	resp->retry_buffer_overflow_cnt = stats.retry_buffer_overflow_cnt;
	resp->pll_lock_loss_cnt = stats.pll_lock_loss_cnt;
	resp->nand_bytes_written = stats.nand_bytes_written;
	resp->host_bytes_written = stats.host_bytes_written;

out:
	resp->status = rc;
	len = ctl__bio_health_resp__get_packed_size(resp);
	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		ctl__bio_health_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	ctl__bio_health_req__free_unpacked(req, &alloc.alloc);
	D_FREE(resp);

	if (bio_health != NULL)
		D_FREE(bio_health);
}

static void
drpc_dev_manage_pack(Ctl__DevManageResp *resp, Drpc__Response *drpc_resp)
{
	size_t	 len = ctl__dev_manage_resp__get_packed_size(resp);
	uint8_t	*body = NULL;

	D_ALLOC(body, len);
	if (body == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		ctl__dev_manage_resp__pack(resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}
}

static void
drpc_dev_manage_free(Ctl__DevManageResp *resp)
{
	if (resp != NULL) {
		if (resp->device != NULL) {
			if (resp->device->uuid != NULL)
				D_FREE(resp->device->uuid);
			D_FREE(resp->device);
		}
		D_FREE(resp);
	}
}

void
ds_mgmt_drpc_dev_set_faulty(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	Ctl__SetFaultyReq	*req = NULL;
	Ctl__DevManageResp	*resp = NULL;
	uuid_t			 dev_uuid;
	int			 rc = 0;

	req = ctl__set_faulty_req__unpack(&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack req (dev state set faulty)\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to set device state to FAULTY\n");

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		ctl__set_faulty_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	ctl__dev_manage_resp__init(resp);

	if (uuid_parse(req->uuid, dev_uuid) != 0) {
		D_ERROR("Device UUID (%s) is invalid\n", req->uuid);
		D_GOTO(pack_resp, rc = -DER_INVAL);
	}

	rc = ds_mgmt_dev_set_faulty(dev_uuid, resp);
	if (rc != 0)
		D_ERROR("Failed to set FAULTY device state :%d\n", rc);

pack_resp:
	resp->status = rc;
	drpc_dev_manage_pack(resp, drpc_resp);
	drpc_dev_manage_free(resp);
	ctl__set_faulty_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_dev_manage_led(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	Ctl__LedManageReq	*req = NULL;
	Ctl__DevManageResp	*resp = NULL;
	int			 rc = 0;

	req = ctl__led_manage_req__unpack(&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack req (dev manage led)\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to manage LED at address %s\n", req->ids);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		ctl__led_manage_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	ctl__dev_manage_resp__init(resp);

	rc = ds_mgmt_dev_manage_led(req, resp);
	if (rc != 0)
		D_ERROR("Failed to manage LED state (%d)\n", rc);

	resp->status = rc;
	drpc_dev_manage_pack(resp, drpc_resp);
	drpc_dev_manage_free(resp);
	ctl__led_manage_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_dev_replace(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Ctl__DevReplaceReq	*req = NULL;
	Ctl__DevManageResp	*resp = NULL;
	uuid_t			 old_uuid;
	uuid_t			 new_uuid;
	int			 rc = 0;

	/* Unpack the inner request from the drpc call body */
	req = ctl__dev_replace_req__unpack(&alloc.alloc, drpc_req->body.len, drpc_req->body.data);

	if (alloc.oom || req == NULL) {
		D_ERROR("Failed to unpack req (dev replace)\n");
		drpc_resp->status = DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD;
		return;
	}

	D_INFO("Received request to replace device %s with device %s\n",
	       req->old_dev_uuid, req->new_dev_uuid);

	D_ALLOC_PTR(resp);
	if (resp == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		ctl__dev_replace_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	/* Response status is populated with SUCCESS on init. */
	ctl__dev_manage_resp__init(resp);

	D_ALLOC_PTR(resp->device);
	if (resp->device == NULL) {
		drpc_resp->status = DRPC__STATUS__FAILURE;
		drpc_dev_manage_free(resp);
		ctl__dev_replace_req__free_unpacked(req, &alloc.alloc);
		return;
	}

	ctl__smd_device__init(resp->device);
	/* Init empty strings to NULL to avoid error cleanup with free */
	resp->device->uuid = NULL;

	if (uuid_parse(req->old_dev_uuid, old_uuid) != 0) {
		D_ERROR("Old device UUID (%s) is invalid\n", req->old_dev_uuid);
		D_GOTO(pack_resp, rc = -DER_INVAL);
	}

	if (uuid_parse(req->new_dev_uuid, new_uuid) != 0) {
		D_ERROR("New device UUID (%s) is invalid\n", req->new_dev_uuid);
		D_GOTO(pack_resp, rc = -DER_INVAL);
	}

	/* TODO DAOS-6283: Implement no-reint device replacement option */

	rc = ds_mgmt_dev_replace(old_uuid, new_uuid, resp);
	if (rc != 0)
		D_ERROR("Failed to replace device %s with %s (%d)\n", req->old_dev_uuid,
			req->new_dev_uuid, rc);

pack_resp:
	resp->status = rc;
	drpc_dev_manage_pack(resp, drpc_resp);
	drpc_dev_manage_free(resp);
	ctl__dev_replace_req__free_unpacked(req, &alloc.alloc);
}

void
ds_mgmt_drpc_set_up(Drpc__Call *drpc_req, Drpc__Response *drpc_resp)
{
	Mgmt__DaosResp	resp = MGMT__DAOS_RESP__INIT;
	int		rc;

	D_INFO("Received request to setup engine\n");

	rc = dss_module_setup_all();
	if (rc != 0) {
		D_ERROR("Module setup failed: %d\n", rc);
		goto err;
	}

	D_INFO("Modules successfully set up\n");

	dss_init_state_set(DSS_INIT_STATE_SET_UP);
err:
	resp.status = rc;
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
		drpc_resp->status = DRPC__STATUS__FAILED_MARSHAL;
	} else {
		mgmt__cont_set_owner_resp__pack(&resp, body);
		drpc_resp->body.len = len;
		drpc_resp->body.data = body;
	}

	mgmt__cont_set_owner_req__free_unpacked(req, &alloc.alloc);
}
