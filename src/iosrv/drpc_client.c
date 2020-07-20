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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of the DAOS server. It implements the dRPC client for
 * communicating with daos_server.
 */

#define D_LOGFAC DD_FAC(server)

#include <daos_types.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos_srv/daos_server.h>
#include "srv.pb-c.h"
#include "srv_internal.h"
#include "drpc_internal.h"

/** dRPC client context */
struct drpc *dss_drpc_ctx;

/* Notify daos_server that we are ready (e.g., to receive dRPC requests). */
static int
notify_ready(void)
{
	Srv__NotifyReadyReq	req = SRV__NOTIFY_READY_REQ__INIT;
	uint8_t		       *reqb;
	size_t			reqb_size;
	Drpc__Call	       *dreq;
	Drpc__Response	       *dresp;
	int			rc;

	rc = crt_self_uri_get(0 /* tag */, &req.uri);
	if (rc != 0)
		goto out;
	req.nctxs = DSS_CTX_NR_TOTAL;
	/* Do not free, this string is managed by the dRPC listener */
	req.drpclistenersock = drpc_listener_socket_path;
	req.instanceidx = dss_instance_idx;
	req.ntgts = dss_tgt_nr;

	reqb_size = srv__notify_ready_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL)
		D_GOTO(out_uri, rc = -DER_NOMEM);

	srv__notify_ready_req__pack(&req, reqb);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_NOTIFY_READY, &dreq);
	if (rc != 0) {
		D_FREE(reqb);
		goto out_uri;
	}

	dreq->body.len = reqb_size;
	dreq->body.data = reqb;

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

	drpc_response_free(dresp);
out_dreq:
	/* This also frees reqb via dreq->body.data. */
	drpc_call_free(dreq);
out_uri:
	D_FREE(req.uri);
out:
	return rc;
}

/* Notify daos_server that there has been a I/O error. */
int
notify_bio_error(int media_err_type, int tgt_id)
{
	Srv__BioErrorReq	 bioerr_req = SRV__BIO_ERROR_REQ__INIT;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_INVAL;
	}

	/* TODO: How does this get freed on error? */
	rc = crt_self_uri_get(0 /* tag */, &bioerr_req.uri);
	if (rc != 0)
		return rc;

	/* TODO: add checksum error */
	if (media_err_type == MET_UNMAP)
		bioerr_req.unmaperr = true;
	else if (media_err_type == MET_WRITE)
		bioerr_req.writeerr = true;
	else if (media_err_type == MET_READ)
		bioerr_req.readerr = true;
	bioerr_req.tgtid = tgt_id;
	bioerr_req.instanceidx = dss_instance_idx;
	bioerr_req.drpclistenersock = drpc_listener_socket_path;

	req_size = srv__bio_error_req__get_packed_size(&bioerr_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		return -DER_NOMEM;

	srv__bio_error_req__pack(&bioerr_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_BIO_ERR, &dreq);
	if (rc != 0) {
		D_FREE(req);
		return rc;
	}

	dreq->body.len = req_size;
	dreq->body.data = req;

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

	drpc_response_free(dresp);

out_dreq:
	drpc_call_free(dreq);

	return rc;
}

/* FIXME: Don't copy these -- move to common? */
static d_rank_list_t *
uint32_array_to_rank_list(uint32_t *ints, size_t len)
{
	d_rank_list_t	*result;
	size_t		i;

	result = d_rank_list_alloc(len);
	if (result == NULL)
		return NULL;

	for (i = 0; i < len; i++)
		result->rl_ranks[i] = (d_rank_t)ints[i];

	return result;
}

int
get_pool_svc_ranks(uuid_t pool_uuid, d_rank_list_t **svc_ranks)
{
	Srv__GetPoolSvcReq	gps_req = SRV__GET_POOL_SVC_REQ__INIT;
	Srv__GetPoolSvcResp	*gps_resp = NULL;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	d_rank_list_t		*ranks;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_INVAL;
	}

	D_ALLOC(gps_req.uuid, DAOS_UUID_STR_SIZE);
	if (gps_req.uuid == NULL) {
		D_ERROR("failed to allocate device uuid\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	uuid_unparse_lower(pool_uuid, gps_req.uuid);

	req_size = srv__get_pool_svc_req__get_packed_size(&gps_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		D_GOTO(out_uuid, rc = -DER_NOMEM);

	srv__get_pool_svc_req__pack(&gps_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_GET_POOL_SVC, &dreq);
	if (rc != 0) {
		D_FREE(req);
		goto out_uuid;
	}

	dreq->body.len = req_size;
	dreq->body.data = req;

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

	gps_resp = srv__get_pool_svc_resp__unpack(
			NULL, dresp->body.len, dresp->body.data);
	if (gps_resp == NULL) {
		D_ERROR("failed to unpack resp (get pool svc)\n");
		D_GOTO(out_dresp, rc = -DER_NOMEM);
	}

	if (gps_resp->status != 0)
		D_GOTO(out_dresp, rc = gps_resp->status);

	ranks = uint32_array_to_rank_list(gps_resp->svcreps,
					  gps_resp->n_svcreps);
	if (ranks == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	D_DEBUG(DB_MGMT, "got %d svc_ranks\n", ranks->rl_nr);
	*svc_ranks = ranks;

out_dresp:
	drpc_response_free(dresp);
out_dreq:
	/* also frees req via dreq->body.data */
	drpc_call_free(dreq);
out_uuid:
	D_FREE(gps_req.uuid);
out:
	return rc;
}

/* NB: The following functions (and any helpers/messages) should be
 * removed when the C mgmt API is removed:
 * free_pool_list()
 * rank_list_to_uint32_array()
 * pool_list_upcall()
 * pool_create_upcall()
 * pool_destroy_upcall()
 */

static void
free_pool_list(daos_mgmt_pool_info_t **poolsp, uint64_t len)
{
	daos_mgmt_pool_info_t	*pools;

	D_ASSERT(poolsp != NULL);
	pools = *poolsp;

	if (pools) {
		uint64_t pc;

		for (pc = 0; pc < len; pc++)
			d_rank_list_free(pools[pc].mgpi_svc);
		D_FREE(pools);
		*poolsp = NULL;
	}
}

int
pool_list_upcall(const char *group, uint64_t *npools,
		 daos_mgmt_pool_info_t **out_pools, size_t *pools_len)
{
	Srv__PoolListUpcall		plu_req = SRV__POOL_LIST_UPCALL__INIT;
	Srv__PoolListUpcallResp		*plu_resp = NULL;
	Srv__PoolListUpcallResp__Pool	*plu_pool;
	Drpc__Call			*dreq;
	Drpc__Response			*dresp;
	uint8_t				*req;
	size_t			 	req_size;
	uint64_t			pi = 0;
	uint64_t			pool_count = 0;
	uint64_t			avail_npools;
	daos_mgmt_pool_info_t		*pools = NULL;
	daos_mgmt_pool_info_t		*pool;
	int				rc;

	*out_pools = NULL;
	*pools_len = 0;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_INVAL;
	}

	if (npools == NULL)
		avail_npools = UINT64_MAX; /* get all the pools */
	else
		avail_npools = *npools;

	if (group != NULL)
		D_STRNDUP(plu_req.group, group, DAOS_SYS_NAME_MAX);
	plu_req.npools = avail_npools;

	req_size = srv__pool_list_upcall__get_packed_size(&plu_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	srv__pool_list_upcall__pack(&plu_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_POOL_LIST_UPCALL, &dreq);
	if (rc != 0) {
		D_FREE(req);
		goto out;
	}

	dreq->body.len = req_size;
	dreq->body.data = req;

	D_DEBUG(DB_MGMT, "sending pool list upcall\n");

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		D_GOTO(out_dreq, rc = -DER_IO);
	}

	plu_resp = srv__pool_list_upcall_resp__unpack(NULL,
					   dresp->body.len,
					   dresp->body.data);
	if (plu_resp == NULL) {
		D_ERROR("Failed to unpack resp (list pools)\n");
		D_GOTO(out_dreq, rc = -DER_INVAL);
	}

	D_DEBUG(DB_MGMT, "got %zu pools back\n", plu_resp->n_pools);

	D_ALLOC(pools, plu_resp->n_pools);
	if (pools == NULL) {
		D_ERROR("failed to allocate pool list\n");
		D_GOTO(out_resp, rc = -DER_INVAL);
	}

	for (pi = 0; pi < plu_resp->n_pools; pi++) {
		plu_pool = plu_resp->pools[pi];
		pool = &pools[pi];
		rc = uuid_parse(plu_pool->uuid, pool->mgpi_uuid);
		if (rc != 0) {
			D_GOTO(out_resp, rc = -DER_INVAL);
		}
		pool->mgpi_svc = uint32_array_to_rank_list(plu_pool->svcreps,
							plu_pool->n_svcreps);
		if (pool->mgpi_svc == NULL)
			D_GOTO(out_resp, rc = -DER_NOMEM);
		pool_count++;
	}

out_resp:
	srv__pool_list_upcall_resp__free_unpacked(plu_resp, NULL);
out_dreq:
	/* also frees req via dreq->body.data */
	drpc_call_free(dreq);
out:
	if (npools != NULL)
		*npools = plu_resp->n_pools;

	if (rc != 0) {
		/* Error in iteration */
		free_pool_list(&pools, pi);
	} else {
		*out_pools = pools;
		*pools_len = pool_count;
	}

	return rc;
}

static int
rank_list_to_uint32_array(d_rank_list_t *rl, uint32_t **ints, size_t *len)
{
	uint32_t i;

	D_ALLOC_ARRAY(*ints, rl->rl_nr);
	if (*ints == NULL)
		return -DER_NOMEM;

	*len = rl->rl_nr;

	for (i = 0; i < rl->rl_nr; i++)
		(*ints)[i] = (uint32_t)rl->rl_ranks[i];

	return 0;
}

int
pool_create_upcall(uuid_t pool_uuid, d_rank_list_t *svc_ranks)
{
	Srv__PoolCreateUpcall	pcu_req = SRV__POOL_CREATE_UPCALL__INIT;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_INVAL;
	}

	if (svc_ranks == NULL) {
		D_ERROR("pool service ranks was NULL\n");
		return -DER_INVAL;
	}

	D_ALLOC(pcu_req.uuid, DAOS_UUID_STR_SIZE);
	if (pcu_req.uuid == NULL) {
		D_ERROR("failed to allocate device uuid\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	uuid_unparse_lower(pool_uuid, pcu_req.uuid);

	rc = rank_list_to_uint32_array(svc_ranks,
		&pcu_req.svcreps, &pcu_req.n_svcreps);
	if (rc != 0) {
		D_ERROR("failed to allocate rank list\n");
		D_GOTO(out_uuid, rc = -DER_NOMEM);
	}

	req_size = srv__pool_create_upcall__get_packed_size(&pcu_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		D_GOTO(out_svcreps, rc = -DER_NOMEM);

	srv__pool_create_upcall__pack(&pcu_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_POOL_CREATE_UPCALL, &dreq);
	if (rc != 0) {
		D_FREE(req);
		goto out_svcreps;
	}

	dreq->body.len = req_size;
	dreq->body.data = req;

	D_DEBUG(DB_MGMT, DF_UUID": sending pool create upcall (# ranks: %zu)\n",
		DP_UUID(pool_uuid), pcu_req.n_svcreps);

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

out_dreq:
	/* also frees req via dreq->body.data */
	drpc_call_free(dreq);
out_svcreps:
	D_FREE(pcu_req.svcreps);
out_uuid:
	D_FREE(pcu_req.uuid);
out:
	return rc;
}

int
pool_destroy_upcall(uuid_t pool_uuid)
{
	Srv__PoolDestroyUpcall	pdu_req = SRV__POOL_DESTROY_UPCALL__INIT;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("DRPC not connected\n");
		return -DER_INVAL;
	}

	D_ALLOC(pdu_req.uuid, DAOS_UUID_STR_SIZE);
	if (pdu_req.uuid == NULL) {
		D_ERROR("failed to allocate device uuid\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	uuid_unparse_lower(pool_uuid, pdu_req.uuid);

	req_size = srv__pool_destroy_upcall__get_packed_size(&pdu_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		D_GOTO(out_uuid, rc = -DER_NOMEM);

	srv__pool_destroy_upcall__pack(&pdu_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_POOL_DESTROY_UPCALL, &dreq);
	if (rc != 0) {
		D_FREE(req);
		goto out_uuid;
	}

	dreq->body.len = req_size;
	dreq->body.data = req;

	D_DEBUG(DB_MGMT, DF_UUID": sending pool destroy upcall\n",
		DP_UUID(pool_uuid));

	rc = drpc_call(dss_drpc_ctx, R_SYNC, dreq, &dresp);
	if (rc != 0)
		goto out_dreq;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

out_dreq:
	/* also frees req via dreq->body.data */
	drpc_call_free(dreq);
out_uuid:
	D_FREE(pdu_req.uuid);
out:
	return rc;
}

int
drpc_init(void)
{
	char   *path;
	int	rc;

	D_ASPRINTF(path, "%s/%s", dss_socket_dir, "daos_server.sock");
	if (path == NULL) {
		D_GOTO(out, rc = -DER_NOMEM);
	}

	D_ASSERT(dss_drpc_ctx == NULL);
	rc = drpc_connect(path, &dss_drpc_ctx);
	if (dss_drpc_ctx == NULL)
		D_GOTO(out_path, 0);

	rc = notify_ready();
	if (rc != 0) {
		drpc_close(dss_drpc_ctx);
		dss_drpc_ctx = NULL;
	}

out_path:
	D_FREE(path);
out:
	return rc;
}

void
drpc_fini(void)
{
	int rc;

	D_ASSERT(dss_drpc_ctx != NULL);
	rc = drpc_close(dss_drpc_ctx);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
	dss_drpc_ctx = NULL;
}
