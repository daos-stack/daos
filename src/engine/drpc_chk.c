/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS engine. It implements dRPC client checker upcalls to communicate
 * with the control plane.
 */
#define D_LOGFAC DD_FAC(server)

#include <daos_srv/daos_chk.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include <daos_srv/daos_engine.h>
#include "check_engine.pb-c.h"

/**
 * TODO DAOS-18537: Consider if it would be best to put the checker functionality into a new dRPC
 * module.
 */

void
ds_chk_free_pool_list(struct chk_list_pool *clp, uint32_t nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		D_FREE(clp[i].clp_label);
		d_rank_list_free(clp[i].clp_svcreps);
	}

	D_FREE(clp);
}

int
ds_chk_listpool_upcall(struct chk_list_pool **clp)
{
	struct chk_list_pool      *pools = NULL;
	struct drpc_alloc          alloc = PROTO_ALLOCATOR_INIT(alloc);
	Shared__CheckListPoolReq   req   = SHARED__CHECK_LIST_POOL_REQ__INIT;
	Shared__CheckListPoolResp *respb = NULL;
	Drpc__Response            *dresp = NULL;
	uint8_t                   *reqb  = NULL;
	size_t                     size;
	int                        rc;
	int                        i;

	size = shared__check_list_pool_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	rc = shared__check_list_pool_req__pack(&req, reqb);
	if (rc < 0)
		goto out_req;

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_LIST_POOL, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for list pool: %d\n", dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb =
	    shared__check_list_pool_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	if (respb->status != 0)
		D_GOTO(out_respb, rc = respb->status);

	D_ALLOC_ARRAY(pools, respb->n_pools);
	if (pools == NULL)
		D_GOTO(out_respb, rc = -DER_NOMEM);

	for (i = 0; i < respb->n_pools; i++) {
		rc = uuid_parse(respb->pools[i]->uuid, pools[i].clp_uuid);
		if (rc != 0) {
			D_ERROR("Failed to parse uuid %s: %d\n", respb->pools[i]->uuid, rc);
			D_GOTO(out_parse, rc);
		}

		D_STRNDUP(pools[i].clp_label, respb->pools[i]->label, DAOS_PROP_LABEL_MAX_LEN);
		if (pools[i].clp_label == NULL)
			D_GOTO(out_parse, rc = -DER_NOMEM);

		pools[i].clp_svcreps =
		    uint32_array_to_rank_list(respb->pools[i]->svcreps, respb->pools[i]->n_svcreps);
		if (pools[i].clp_svcreps == NULL)
			D_GOTO(out_parse, rc = -DER_NOMEM);
	}

	rc    = respb->n_pools;
	*clp  = pools;
	pools = NULL;

out_parse:
	if (pools != NULL)
		ds_chk_free_pool_list(pools, respb->n_pools);
out_respb:
	shared__check_list_pool_resp__free_unpacked(respb, &alloc.alloc);
out_dresp:
	drpc_response_free(dresp);
out_req:
	D_FREE(reqb);

	return rc;
}

/*
 * Register the pool information on MS via DRPC_METHOD_CHK_REG_POOL:
 * if the pool does not exist, then add it on MS; otherwise, refresh
 * the pool service replicas and label information.
 */
int
ds_chk_regpool_upcall(uint64_t seq, uuid_t uuid, char *label, d_rank_list_t *svcreps)
{
	struct drpc_alloc         alloc = PROTO_ALLOCATOR_INIT(alloc);
	Shared__CheckRegPoolReq   req   = SHARED__CHECK_REG_POOL_REQ__INIT;
	Shared__CheckRegPoolResp *respb = NULL;
	Drpc__Response           *dresp = NULL;
	uint8_t                  *reqb  = NULL;
	size_t                    size;
	int                       rc;

	if (DAOS_FAIL_CHECK(DAOS_CHK_LEADER_FAIL_REGPOOL))
		return -DER_IO;

	req.seq = seq;
	D_ASPRINTF(req.uuid, DF_UUIDF, DP_UUID(uuid));
	if (req.uuid == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	req.label     = label;
	req.n_svcreps = svcreps->rl_nr;
	req.svcreps   = svcreps->rl_ranks;

	size = shared__check_reg_pool_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	rc = shared__check_reg_pool_req__pack(&req, reqb);
	if (rc < 0)
		goto out_req;

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_REG_POOL, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for register pool: %d\n", dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb =
	    shared__check_reg_pool_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	rc = respb->status;
	shared__check_reg_pool_resp__free_unpacked(respb, &alloc.alloc);

out_dresp:
	drpc_response_free(dresp);
out_req:
	D_FREE(req.uuid);
	D_FREE(reqb);

	return rc;
}

int
ds_chk_deregpool_upcall(uint64_t seq, uuid_t uuid)
{
	struct drpc_alloc           alloc = PROTO_ALLOCATOR_INIT(alloc);
	Shared__CheckDeregPoolReq   req   = SHARED__CHECK_DEREG_POOL_REQ__INIT;
	Shared__CheckDeregPoolResp *respb = NULL;
	Drpc__Response             *dresp = NULL;
	uint8_t                    *reqb  = NULL;
	size_t                      size;
	int                         rc;

	req.seq = seq;
	D_ASPRINTF(req.uuid, DF_UUIDF, DP_UUID(uuid));
	if (req.uuid == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	size = shared__check_dereg_pool_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	rc = shared__check_dereg_pool_req__pack(&req, reqb);
	if (rc < 0)
		goto out_req;

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_DEREG_POOL, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for de-register pool: %d\n",
			dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb =
	    shared__check_dereg_pool_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	rc = respb->status;
	shared__check_dereg_pool_resp__free_unpacked(respb, &alloc.alloc);

out_dresp:
	drpc_response_free(dresp);
out_req:
	D_FREE(req.uuid);
	D_FREE(reqb);

	return rc;
}

int
ds_chk_report_upcall(void *rpt)
{
	struct drpc_alloc        alloc = PROTO_ALLOCATOR_INIT(alloc);
	Shared__CheckReportReq   req   = SHARED__CHECK_REPORT_REQ__INIT;
	Shared__CheckReportResp *respb = NULL;
	Drpc__Response          *dresp = NULL;
	uint8_t                 *reqb  = NULL;
	size_t                   size;
	int                      rc;

	D_ASSERT(rpt != NULL);
	req.report = rpt;

	size = shared__check_report_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	rc = shared__check_report_req__pack(&req, reqb);
	if (rc < 0)
		goto out_req;

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_REPORT, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for check report: %d\n", dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb = shared__check_report_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	rc = respb->status;
	shared__check_report_resp__free_unpacked(respb, &alloc.alloc);

out_dresp:
	drpc_response_free(dresp);
out_req:
	D_FREE(reqb);

	return rc;
}
