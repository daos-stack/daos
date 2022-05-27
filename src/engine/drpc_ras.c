/*
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS server. It implements dRPC client RAS event
 * functionality for communicating events to the control plane.
 */

#define D_LOGFAC DD_FAC(server)

#include <daos/drpc_modules.h>
#include <daos_srv/ras.h>
#include <sys/utsname.h>
#include "event.pb-c.h"
#include "drpc_internal.h"
#include "srv_internal.h"
#include "srv.pb-c.h"

static void
free_event(Shared__RASEvent *evt)
{
	D_FREE(evt->obj_id);
	D_FREE(evt->pool_uuid);
	D_FREE(evt->cont_uuid);
	D_FREE(evt->hostname);
	D_FREE(evt->timestamp);
}

static d_rank_t
safe_self_rank(void)
{
	d_rank_t	rank;
	int		rc;

	rc = crt_group_rank(NULL /* grp */, &rank);
	if (rc != 0) {
		D_ERROR("failed to get self rank: "DF_RC"\n", DP_RC(rc));
		rank = CRT_NO_RANK;
	}

	return rank;
}

static int
init_event(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev,
	   char *hwid, d_rank_t *rank, uint64_t *inc, char *jobid,
	   uuid_t *pool, uuid_t *cont, daos_obj_id_t *objid, char *ctlop,
	   Shared__RASEvent *evt)
{
	struct dss_module_info	*dmi = get_module_info();
	struct timeval		 tv;
	struct tm		*tm;
	char			 zone[6]; /* ±0000\0 */
	int			 rc;

	/* Populate mandatory RAS fields. */

	(void)gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);
	if (tm == NULL) {
		D_ERROR("localtime() failed\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	strftime(zone, 6, "%z", tm);
	/* NB: Timestamp should be in ISO8601 format. */
	D_ASPRINTF(evt->timestamp, "%04d-%02d-%02dT%02d:%02d:%02d.%06ld%s",
		   tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour,
		   tm->tm_min, tm->tm_sec, tv.tv_usec, zone);
	if (evt->timestamp == NULL) {
		D_ERROR("failed to generate timestamp string\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	evt->id = (uint32_t)id;
	evt->type = (uint32_t)type;
	evt->severity = (uint32_t)sev;
	evt->proc_id = (uint64_t)getpid();

	if (dmi == NULL) {
		D_ERROR("failed to retrieve xstream id");
		D_GOTO(out_ts, rc = -DER_UNINIT);
	}
	evt->thread_id = (uint64_t)dmi->dmi_xs_id;

	if (strnlen(dss_hostname, DSS_HOSTNAME_MAX_LEN) == 0) {
		D_ERROR("missing hostname parameter\n");
		D_GOTO(out_ts, rc = -DER_UNINIT);
	}
	D_STRNDUP(evt->hostname, dss_hostname, DSS_HOSTNAME_MAX_LEN);

	if ((msg == NULL) || strnlen(msg, DAOS_RAS_STR_FIELD_SIZE) == 0) {
		D_ERROR("missing msg parameter\n");
		D_GOTO(out_hn, rc = -DER_INVAL);
	}
	evt->msg = msg;

	/* Populate optional RAS fields. */

	evt->hw_id = (hwid != NULL) ? hwid : NULL;
	/* UINT32_MAX/CRT_NO_RANK indicates nil rank in daos_{,io_}server */
	evt->rank = (rank != NULL) ? (uint32_t)*rank : CRT_NO_RANK;
	evt->incarnation = (inc != NULL) ? (uint64_t)*inc : 0;
	evt->job_id = (jobid != NULL) ? jobid : NULL;
	evt->ctl_op = (ctlop != NULL) ? ctlop : NULL;

	if ((pool != NULL) && !uuid_is_null(*pool))
		D_ASPRINTF(evt->pool_uuid, DF_UUIDF, DP_UUID(*pool));
	else
		evt->pool_uuid = NULL;

	if ((cont != NULL) && !uuid_is_null(*cont))
		D_ASPRINTF(evt->cont_uuid, DF_UUIDF, DP_UUID(*cont));
	else
		evt->cont_uuid = NULL;

	if (objid != NULL)
		D_ASPRINTF(evt->obj_id, DF_OID, DP_OID(*objid));
	else
		evt->obj_id = NULL;

	return 0;

out_hn:
	D_FREE(evt->hostname);
out_ts:
	D_FREE(evt->timestamp);
out:
	return rc;
}

static void
log_event(Shared__RASEvent *evt)
{
	FILE					*stream;
	char					*buf = NULL;
	size_t					 len;
	Shared__RASEvent__ExtendedInfoCase	 eic = evt->extended_info_case;

	stream = open_memstream(&buf, &len);
	if (stream == NULL) {
		D_ERROR("open_memstream failed: "DF_RC"\n", DP_RC(-DER_NOMEM));
		return;
	}

	/* Log mandatory RAS fields. */
	D_FPRINTF(stream, " id: [%s]", ras_event2str(evt->id));
	if (evt->timestamp != NULL)
		D_FPRINTF(stream, " ts: [%s]", evt->timestamp);
	if (evt->hostname != NULL)
		D_FPRINTF(stream, " host: [%s]", evt->hostname);
	D_FPRINTF(stream, " type: [%s] sev: [%s]", ras_type2str(evt->type),
		  ras_sev2str(evt->severity));
	if (evt->msg != NULL)
		D_FPRINTF(stream, " msg: [%s]", evt->msg);
	D_FPRINTF(stream, " pid: [%lu]", evt->proc_id);
	D_FPRINTF(stream, " tid: [%lu]", evt->thread_id);

	/* Log optional RAS fields. */
	if (evt->hw_id != NULL)
		D_FPRINTF(stream, " hwid: [%s]", evt->hw_id);
	if (evt->rank != CRT_NO_RANK)
		D_FPRINTF(stream, " rank: [%u]", evt->rank);
	if (evt->incarnation != 0)
		D_FPRINTF(stream, " inc: [%lx]", evt->incarnation);
	if (evt->job_id != NULL)
		D_FPRINTF(stream, " jobid: [%s]", evt->job_id);
	if (evt->pool_uuid != NULL)
		D_FPRINTF(stream, " pool: [%s]", evt->pool_uuid);
	if (evt->cont_uuid != NULL)
		D_FPRINTF(stream, " container: [%s]", evt->cont_uuid);
	if (evt->obj_id != NULL)
		D_FPRINTF(stream, " objid: [%s]", evt->obj_id);
	if (evt->ctl_op != NULL)
		D_FPRINTF(stream, " ctlop: [%s]", evt->ctl_op);

	/* Log data blob if event info is non-specific */
	if (eic != SHARED__RASEVENT__EXTENDED_INFO_STR_INFO)
		goto out;
	if (evt->str_info != NULL)
		D_FPRINTF(stream, " data: [%s]", evt->str_info);

out:
	fclose(stream);
	D_INFO("&&& RAS EVENT%s\n", buf);
	D_FREE(buf);
}

static int
send_event(Shared__RASEvent *evt, bool wait_for_resp)
{
	Shared__ClusterEventReq	 req = SHARED__CLUSTER_EVENT_REQ__INIT;
	uint8_t			*reqb;
	size_t			 reqb_size;
	Drpc__Response		*dresp;
	int			 rc;

	if (evt == NULL) {
		D_ERROR("null RAS event\n");
		return -DER_INVAL;
	}
	req.event = evt;

	reqb_size = shared__cluster_event_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	shared__cluster_event_req__pack(&req, reqb);

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_SRV_CLUSTER_EVENT, reqb,
			   reqb_size, wait_for_resp ? 0 : DSS_DRPC_NO_RESP,
			   wait_for_resp ? &dresp : NULL);
	if (rc != 0)
		goto out_reqb;
	if (wait_for_resp) {
		if (dresp->status != DRPC__STATUS__SUCCESS) {
			D_ERROR("received erroneous dRPC response: %d\n",
				dresp->status);
			rc = -DER_IO;
		}
		drpc_response_free(dresp);
	}

out_reqb:
	D_FREE(reqb);
out:
	free_event(evt);

	return rc;
}

static int
raise_ras(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev, char *hwid,
	  d_rank_t *rank, uint64_t *inc, char *jobid, uuid_t *pool, uuid_t *cont,
	  daos_obj_id_t *objid, char *ctlop, Shared__RASEvent *evt,
	  bool wait_for_resp)
{
	int rc = init_event(id, msg, type, sev, hwid, rank, inc, jobid, pool, cont,
			    objid, ctlop, evt);
	if (rc != 0) {
		D_ERROR("failed to init RAS event %s: "DF_RC"\n",
			ras_event2str(id), DP_RC(rc));
		return rc;
	}

	log_event(evt);
	rc = send_event(evt, wait_for_resp);
	if (rc != 0)
		D_ERROR("failed to send RAS event %s over dRPC: "DF_RC"\n",
			ras_event2str(id), DP_RC(rc));

	free_event(evt);

	return rc;
}

void
ds_notify_ras_event(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev,
		    char *hwid, d_rank_t *rank, uint64_t *inc, char *jobid,
		    uuid_t *pool, uuid_t *cont, daos_obj_id_t *objid,
		    char *ctlop, char *data)
{
	Shared__RASEvent	evt = SHARED__RASEVENT__INIT;
	d_rank_t		this_rank;

	/* use opaque blob oneof case for extended info for passthrough event */
	evt.extended_info_case = SHARED__RASEVENT__EXTENDED_INFO_STR_INFO;
	evt.str_info = (data == NULL) ? "" : data;

	/* populate rank param if empty */
	if (rank == NULL) {
		this_rank = safe_self_rank();
		rank = &this_rank;
	}

	raise_ras(id, msg, type, sev, hwid, rank, inc, jobid, pool, cont, objid,
		  ctlop, &evt, false /* wait_for_resp */);
}

void
ds_notify_ras_eventf(ras_event_t id, ras_type_t type, ras_sev_t sev, char *hwid,
		     d_rank_t *rank, uint64_t *inc, char *jobid, uuid_t *pool,
		     uuid_t *cont, daos_obj_id_t *objid, char *ctlop, char *data,
		     const char *fmt, ...)
{
	char	buf[DAOS_RAS_STR_FIELD_SIZE];
	va_list	ap;
	int	rc;

	va_start(ap, fmt);
	rc = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (rc >= sizeof(buf)) {
		/* The message is too long. End it with '$'. */
		buf[sizeof(buf) - 2] = '$';
	}

	ds_notify_ras_event(id, buf, type, sev, hwid, rank, inc, jobid, pool, cont,
			    objid, ctlop, data);
}

int
ds_notify_pool_svc_update(uuid_t *pool, d_rank_list_t *svcl)
{
	Shared__RASEvent			evt = SHARED__RASEVENT__INIT;
	Shared__RASEvent__PoolSvcEventInfo	info = \
		SHARED__RASEVENT__POOL_SVC_EVENT_INFO__INIT;
	d_rank_t				rank = safe_self_rank();
	int					rc;

	if ((pool == NULL) || uuid_is_null(*pool)) {
		D_ERROR("invalid pool\n");
		return -DER_INVAL;
	}
	if ((svcl == NULL) || svcl->rl_nr == 0) {
		D_ERROR("invalid service replicas\n");
		return -DER_INVAL;
	}

	rc = rank_list_to_uint32_array(svcl, &info.svc_reps, &info.n_svc_reps);
	if (rc != 0) {
		D_ERROR("failed to convert svc replicas to proto\n");
		return rc;
	}

	evt.extended_info_case = SHARED__RASEVENT__EXTENDED_INFO_POOL_SVC_INFO;
	evt.pool_svc_info = &info;

	rc = raise_ras(RAS_POOL_REPS_UPDATE,
		       "List of pool service replica ranks has been updated.",
		       RAS_TYPE_STATE_CHANGE, RAS_SEV_NOTICE, NULL /* hwid */,
		       &rank /* rank */, NULL /* inc */, NULL /* jobid */, pool,
		       NULL /* cont */, NULL /* objid */, NULL /* ctlop */,
		       &evt, true /* wait_for_resp */);

	D_FREE(info.svc_reps);

	return rc;
}

int
ds_notify_swim_rank_dead(d_rank_t rank, uint64_t incarnation)
{
	Shared__RASEvent	evt = SHARED__RASEVENT__INIT;

	return raise_ras(RAS_SWIM_RANK_DEAD, "SWIM marked rank as dead.",
			 RAS_TYPE_STATE_CHANGE, RAS_SEV_NOTICE, NULL /* hwid */,
			 &rank /* rank */, &incarnation /* inc */, NULL /* jobid */,
			 NULL /* pool */, NULL /* cont */, NULL /* objid */,
			 NULL /* ctlop */, &evt, false /* wait_for_resp */);
}

void
ds_chk_free_pool_list(struct chk_list_pool *clp, uint32_t nr)
{
	int	i;

	for (i = 0; i < nr; i++) {
		D_FREE(clp[i].clp_label);
		d_rank_list_free(clp[i].clp_svcreps);
	}

	D_FREE(clp);
}

int
ds_chk_listpool_upcall(struct chk_list_pool **clp)
{
	struct chk_list_pool	*pools = NULL;
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	Srv__CheckListPoolReq	 req = SRV__CHECK_LIST_POOL_REQ__INIT;
	Srv__CheckListPoolResp	*respb = NULL;
	Drpc__Response		*dresp = NULL;
	uint8_t			*reqb = NULL;
	size_t			 size;
	int			 rc;
	int			 i;

	size = srv__check_list_pool_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	srv__check_list_pool_req__pack(&req, reqb);

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_LIST_POOL, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for list pool: %d\n", dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb = srv__check_list_pool_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb)
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

		pools[i].clp_svcreps = uint32_array_to_rank_list(respb->pools[i]->svcreps,
								 respb->pools[i]->n_svcreps);
		if (pools[i].clp_svcreps == NULL)
			D_GOTO(out_parse, rc = -DER_NOMEM);
	}

	rc = respb->n_pools;
	*clp = pools;
	pools = NULL;

out_parse:
	if (pools != NULL)
		ds_chk_free_pool_list(pools, respb->n_pools);
out_respb:
	srv__check_list_pool_resp__free_unpacked(respb, &alloc.alloc);
out_dresp:
	drpc_response_free(dresp);
out_req:
	D_FREE(reqb);

	return rc;
}

int
ds_chk_regpool_upcall(uint64_t seq, uuid_t uuid, char *label, d_rank_list_t *svcreps)
{
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	Srv__CheckRegPoolReq	 req = SRV__CHECK_REG_POOL_REQ__INIT;
	Srv__CheckRegPoolResp	*respb = NULL;
	Drpc__Response		*dresp = NULL;
	uint8_t			*reqb = NULL;
	size_t			 size;
	int			 rc;

	req.seq = seq;
	D_ASPRINTF(req.uuid, DF_UUIDF, DP_UUID(uuid));
	if (req.uuid == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	req.label = label;
	req.n_svcreps = svcreps->rl_nr;
	req.svcreps = svcreps->rl_ranks;

	size = srv__check_reg_pool_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	srv__check_reg_pool_req__pack(&req, reqb);
	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_REG_POOL, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for register pool: %d\n", dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb = srv__check_reg_pool_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	rc = respb->status;
	srv__check_reg_pool_resp__free_unpacked(respb, &alloc.alloc);

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
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	Srv__CheckDeregPoolReq	 req = SRV__CHECK_DEREG_POOL_REQ__INIT;
	Srv__CheckDeregPoolResp	*respb = NULL;
	Drpc__Response		*dresp = NULL;
	uint8_t			*reqb = NULL;
	size_t			 size;
	int			 rc;

	req.seq = seq;
	D_ASPRINTF(req.uuid, DF_UUIDF, DP_UUID(uuid));
	if (req.uuid == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	size = srv__check_dereg_pool_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	srv__check_dereg_pool_req__pack(&req, reqb);
	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_DEREG_POOL, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for de-register pool: %d\n",
			dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb = srv__check_dereg_pool_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	rc = respb->status;
	srv__check_dereg_pool_resp__free_unpacked(respb, &alloc.alloc);

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
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	Srv__CheckReportReq	 req = SRV__CHECK_REPORT_REQ__INIT;
	Srv__CheckReportResp	*respb = NULL;
	Drpc__Response		*dresp = NULL;
	uint8_t			*reqb = NULL;
	size_t			 size;
	int			 rc;

	D_ASSERT(rpt != NULL);
	req.report = rpt;

	size = srv__check_report_req__get_packed_size(&req);
	D_ALLOC(reqb, size);
	if (reqb == NULL)
		D_GOTO(out_req, rc = -DER_NOMEM);

	srv__check_report_req__pack(&req, reqb);
	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_CHK_REPORT, reqb, size, 0, &dresp);
	if (rc != 0)
		goto out_req;

	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Received erroneous dRPC response for check report: %d\n", dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	respb = srv__check_report_resp__unpack(&alloc.alloc, dresp->body.len, dresp->body.data);
	if (alloc.oom || respb == NULL)
		D_GOTO(out_dresp, rc = -DER_NOMEM);

	rc = respb->status;
	srv__check_report_resp__free_unpacked(respb, &alloc.alloc);

out_dresp:
	drpc_response_free(dresp);
out_req:
	D_FREE(reqb);

	return rc;
}
