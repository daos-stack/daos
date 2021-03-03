/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS server. It implements the dRPC client for
 * communicating with daos_server.
 */

#define D_LOGFAC DD_FAC(server)

#include <daos_types.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
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
ds_notify_bio_error(int media_err_type, int tgt_id)
{
	Srv__BioErrorReq	 bioerr_req = SRV__BIO_ERROR_REQ__INIT;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("dRPC not connected\n");
		return -DER_INVAL;
	}
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
		D_GOTO(out_uri, rc = -DER_NOMEM);

	srv__bio_error_req__pack(&bioerr_req, req);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_BIO_ERR, &dreq);
	if (rc != 0) {
		D_FREE(req);
		goto out_uri;
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
out_uri:
	D_FREE(bioerr_req.uri);

	return rc;
}

int
ds_get_pool_svc_ranks(uuid_t pool_uuid, d_rank_list_t **svc_ranks)
{
	struct drpc_alloc	alloc = PROTO_ALLOCATOR_INIT(alloc);
	Srv__GetPoolSvcReq	gps_req = SRV__GET_POOL_SVC_REQ__INIT;
	Srv__GetPoolSvcResp	*gps_resp = NULL;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	d_rank_list_t		*ranks;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("dRPC not connected\n");
		return -DER_UNINIT;
	}

	D_ALLOC(gps_req.uuid, DAOS_UUID_STR_SIZE);
	if (gps_req.uuid == NULL) {
		D_ERROR("failed to allocate pool uuid\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}
	uuid_unparse_lower(pool_uuid, gps_req.uuid);

	D_DEBUG(DB_MGMT, "fetching svc_ranks for "DF_UUID"\n",
		DP_UUID(pool_uuid));

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
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	gps_resp = srv__get_pool_svc_resp__unpack(&alloc.alloc,
						  dresp->body.len,
						  dresp->body.data);
	if (alloc.oom) {
		D_GOTO(out_dresp, rc = -DER_NOMEM);
	} else if (gps_resp == NULL) {
		D_ERROR("failed to unpack resp (get pool svc)\n");
		D_GOTO(out_dresp, rc = -DER_NOMEM);
	}

	if (gps_resp->status != 0) {
		D_ERROR("failure fetching svc_ranks for "DF_UUID": "DF_RC"\n",
			DP_UUID(pool_uuid), DP_RC(gps_resp->status));
		D_GOTO(out_resp, rc = gps_resp->status);
	}

	ranks = uint32_array_to_rank_list(gps_resp->svcreps,
					  gps_resp->n_svcreps);
	if (ranks == NULL)
		D_GOTO(out_resp, rc = -DER_NOMEM);

	D_DEBUG(DB_MGMT, "fetched %d svc_ranks for "DF_UUID"\n",
		ranks->rl_nr, DP_UUID(pool_uuid));
	*svc_ranks = ranks;

out_resp:
	srv__get_pool_svc_resp__free_unpacked(gps_resp, &alloc.alloc);
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

int
drpc_init(void)
{
	char	*path;
	int	 rc;

	D_ASPRINTF(path, "%s/%s", dss_socket_dir, "daos_server.sock");
	if (path == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

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
