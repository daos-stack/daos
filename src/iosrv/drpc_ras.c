/*
 * (C) Copyright 2021 Intel Corporation.
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

static void
free_ras(Shared__RASEvent *evt)
{
	if (evt->obj_id)
		D_FREE(evt->obj_id);
	if (evt->pool_uuid)
		D_FREE(evt->pool_uuid);
	if (evt->cont_uuid)
		D_FREE(evt->cont_uuid);
	if (evt->hostname)
		D_FREE(evt->hostname);
	if (evt->timestamp)
		D_FREE(evt->timestamp);
}

static int
init_ras(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev, char *hid,
	 d_rank_t *rank, char *jid, uuid_t *puuid, uuid_t *cuuid,
	 daos_obj_id_t *oid, char *cop, Shared__RASEvent *evt)
{
	struct timeval	 tv;
	struct tm	*tm;
	int		 rc;

	evt->timestamp = NULL;
	evt->hostname = NULL;
	evt->msg = NULL;
	/* max value indicates nil rank */
	evt->rank = UINT32_MAX;
	evt->hw_id = NULL;
	evt->proc_id = NULL;
	evt->thread_id = NULL;
	evt->job_id = NULL;
	evt->pool_uuid = NULL;
	evt->cont_uuid = NULL;
	evt->obj_id = NULL;
	evt->ctl_op = NULL;

	/* Populate mandatory RAS fields. */

	(void)gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);
	if (tm)
		D_ASPRINTF(evt->timestamp,
			   "%04d/%02d/%02d-%02d:%02d:%02d.%02ld",
			   tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			   tm->tm_hour, tm->tm_min, tm->tm_sec,
			   (long)tv.tv_usec / 10000);

	evt->id = (uint32_t)id;
	evt->type = (uint32_t)type;
	evt->severity = (uint32_t)sev;

	if (strnlen(dss_hostname, DSS_HOSTNAME_MAX_LEN) == 0) {
		D_ERROR("missing hostname parameter\n");
		D_GOTO(out_fail, rc = -DER_UNINIT);
	}
	D_STRNDUP(evt->hostname, dss_hostname, DSS_HOSTNAME_MAX_LEN);

	if (!msg || strnlen(msg, DAOS_RAS_STR_FIELD_SIZE) == 0) {
		D_ERROR("missing msg parameter\n");
		D_GOTO(out_fail, rc = -DER_INVAL);
	}
	evt->msg = msg;

	/* Populate optional RAS fields. */

	if (hid)
		evt->hw_id = hid;
	if (rank)
		evt->rank = (uint32_t)*rank;
	if (jid)
		evt->job_id = jid;
	if (puuid && !uuid_is_null(*puuid))
		D_ASPRINTF(evt->pool_uuid, DF_UUIDF, DP_UUID(*puuid));
	if (cuuid && !uuid_is_null(*cuuid))
		D_ASPRINTF(evt->cont_uuid, DF_UUIDF, DP_UUID(*cuuid));
	if (oid)
		D_ASPRINTF(evt->obj_id, DF_OID, DP_OID(*oid));
	if (cop)
		evt->ctl_op = cop;

	return 0;
out_fail:
	free_ras(evt);

	return rc;
}

static void
log_ras(Shared__RASEvent *evt)
{
	FILE		*stream;
	char		*buf = NULL;
	size_t		 len;

	stream = open_memstream(&buf, &len);
	if (!stream) {
		D_ERROR("open_memstream failed: "DF_RC"\n", DP_RC(-DER_NOMEM));
		return;
	}

	/* Log mandatory RAS fields. */

	D_FPRINTF(stream, " id: [%s]", ras_event2str(evt->id));
	if (evt->timestamp)
		D_FPRINTF(stream, " ts: [%s]", evt->timestamp);
	if (evt->hostname)
		D_FPRINTF(stream, " host: [%s]", evt->hostname);
	D_FPRINTF(stream, " type: [%s] sev: [%s]", ras_type2str(evt->type),
		  ras_sev2str(evt->severity));
	if (evt->msg)
		D_FPRINTF(stream, " msg: [%s]", evt->msg);

	/* Log optional RAS fields. */

	if (evt->hw_id)
		D_FPRINTF(stream, " hwid: [%s]", evt->hw_id);
	if (evt->rank != UINT32_MAX)
		D_FPRINTF(stream, " rank: [%u]", evt->rank);
	if (evt->job_id)
		D_FPRINTF(stream, " jobid: [%s]", evt->job_id);
	if (evt->pool_uuid)
		D_FPRINTF(stream, " puuid: [%s]", evt->pool_uuid);
	if (evt->cont_uuid)
		D_FPRINTF(stream, " cuuid: [%s]", evt->cont_uuid);
	if (evt->obj_id)
		D_FPRINTF(stream, " oid: [%s]", evt->obj_id);
	if (evt->ctl_op)
		D_FPRINTF(stream, " control_op: [%s]", evt->ctl_op);
	if (evt->extended_info_case == \
			SHARED__RASEVENT__EXTENDED_INFO_STR_INFO && \
			evt->str_info)
		D_FPRINTF(stream, " data: [%s]", evt->str_info);

	fclose(stream);
	D_DEBUG(DB_MGMT, "&&& RAS EVENT%s", buf);
	D_FREE(buf);
}

static int
send_ras(Shared__RASEvent *evt)
{
	Shared__ClusterEventReq	 req = SHARED__CLUSTER_EVENT_REQ__INIT;
	uint8_t			*reqb;
	size_t			 reqb_size;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("dRPC not connected: "DF_RC"\n", DP_RC(-DER_UNINIT));
		return -DER_UNINIT;
	}

	if (!evt) {
		D_ERROR("null RAS event\n");
		return -DER_INVAL;
	}
	req.event = evt;

	reqb_size = shared__cluster_event_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	shared__cluster_event_req__pack(&req, reqb);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_SRV,
			      DRPC_METHOD_SRV_CLUSTER_EVENT, &dreq);
	if (rc != 0) {
		D_FREE(reqb);
		goto out;
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
out:
	free_ras(evt);
	return rc;
}

void
ds_notify_ras_event(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev,
		    char *hid, d_rank_t *rank, char *jid, uuid_t *puuid,
		    uuid_t *cuuid, daos_obj_id_t *oid, char *cop, char *data)
{
	Shared__RASEvent	evt = SHARED__RASEVENT__INIT;
	d_rank_t		this_rank = dss_self_rank();
	int			rc;

	/* use opaque blob oneof case for extended info for passthrough event */
	evt.extended_info_case = SHARED__RASEVENT__EXTENDED_INFO_STR_INFO;
	evt.str_info = data;
	D_DEBUG(DB_MGMT, "&&& RAS EVENT OPT DATA: %s", evt.str_info);

	/* populate rank param if empty */
	if (!rank)
		rank = &this_rank;

	rc = init_ras(id, msg, type, sev, hid, rank, jid, puuid, cuuid, oid,
		      cop, &evt);
	if (rc != 0) {
		D_ERROR("failed to init RAS event %s: "DF_RC"\n",
			ras_event2str(id), DP_RC(rc));
		return;
	}

	log_ras(&evt);
	rc = send_ras(&evt);
	if (rc != 0)
		D_ERROR("failed to send RAS event %s over dRPC: "DF_RC"\n",
			ras_event2str(id), DP_RC(rc));

	free_ras(&evt);
}

int
ds_notify_pool_svc_update(uuid_t *puuid, d_rank_list_t *svc)
{
	Shared__RASEvent			evt = SHARED__RASEVENT__INIT;
	Shared__RASEvent__PoolSvcEventInfo	info = \
		SHARED__RASEVENT__POOL_SVC_EVENT_INFO__INIT;
	d_rank_t				rank = dss_self_rank();
	int					rc;

	if (!puuid || uuid_is_null(*puuid)) {
		D_ERROR("invalid pool\n");
		return -DER_INVAL;
	}
	if (!svc || svc->rl_nr == 0) {
		D_ERROR("invalid service replicas\n");
		return -DER_INVAL;
	}

	rc = rank_list_to_uint32_array(svc, &info.svc_reps, &info.n_svc_reps);
	if (rc != 0) {
		D_ERROR("failed to convert svc replicas to proto\n");
		return rc;
	}

	evt.extended_info_case = SHARED__RASEVENT__EXTENDED_INFO_POOL_SVC_INFO;
	evt.pool_svc_info = &info;

	rc = init_ras(RAS_POOL_REPS_UPDATE,
		      "List of pool service replica ranks has been updated.",
		      RAS_TYPE_STATE_CHANGE, RAS_SEV_INFO, NULL /* hid */,
		      &rank /* rank */, NULL /* jid */, puuid, NULL /* cuuid */,
		      NULL /* oid */, NULL /* cop */, &evt);
	if (rc != 0) {
		D_ERROR("failed to init RAS event %s: "DF_RC"\n",
			ras_event2str(RAS_POOL_REPS_UPDATE), DP_RC(rc));
		goto out_svcreps;
	}

	log_ras(&evt);
	rc = send_ras(&evt);
	if (rc != 0)
		D_ERROR("failed to send RAS event %s over dRPC: "DF_RC"\n",
			ras_event2str(RAS_POOL_REPS_UPDATE), DP_RC(rc));

	free_ras(&evt);
out_svcreps:
	D_FREE(info.svc_reps);

	return rc;
}
