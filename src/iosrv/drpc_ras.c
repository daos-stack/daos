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

static void
free_ras(Mgmt__RASEvent *evt)
{
	if (evt->hostname)
		D_FREE(evt->hostname);
	if (evt->timestamp)
		D_FREE(evt->timestamp);
}

static int
init_ras(char *id, enum ras_event_sev sev, enum ras_event_type type, char *hid,
	 d_rank_t *rank, char *jid, uuid_t *puuid, uuid_t *cuuid,
	 daos_obj_id_t *oid, char *cop, char *msg, Mgmt__RASEvent *evt)
{
	FILE		*stream;
	char		*buf;
	size_t		 len;
	struct timeval	 tv;
	struct tm	*tm;
	struct utsname	 uts;
	int		 rc;

	stream = open_memstream(&buf, &len);

	/* populate mandatory RAS fields */

	if (!id) {
		D_ERROR("missing ID parameter\n");
		D_GOTO(out_fail, rc = -DER_INVAL);
	}
	evt->id = id;
	D_FPRINTF(stream, " id: [%s]", id);

	(void)gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);
	if (tm) {
		D_ALLOC(evt->timestamp, DAOS_RAS_STR_FIELD_SIZE);
		if (!evt->timestamp)
			D_GOTO(out_fail, rc = -DER_NOMEM);
		D_ASPRINTF(evt->timestamp,
			   "%04d/%02d/%02d-%02d:%02d:%02d.%02ld",
			   tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			   tm->tm_hour, tm->tm_min, tm->tm_sec,
			   (long int)tv.tv_usec / 10000);
		D_FPRINTF(stream, " ts: [%s]", evt->timestamp);
	}

	/** XXX: nodename should be fetched only once and not on every call */
	D_ALLOC(evt->hostname, DAOS_RAS_STR_FIELD_SIZE);
	if (!evt->hostname)
		D_GOTO(out_fail, rc = -DER_NOMEM);
	(void)uname(&uts);
	D_ASPRINTF(evt->hostname, "%s", uts.nodename);
	D_FPRINTF(stream, " host: [%s]", evt->hostname);

	evt->severity = (uint32_t)sev;
	evt->type = (uint32_t)type;
	D_FPRINTF(stream, " sev: [%s] type: [%s]", ras_event_sev2str(sev),
		  ras_event_type2str(type));

	if (!msg) {
		D_ERROR("missing msg parameter\n");
		D_GOTO(out_fail, rc = -DER_INVAL);
	}
	evt->msg = msg;
	D_FPRINTF(stream, " msg: [%s]", msg);

	/* populate optional RAS fields */

	if (hid) {
		evt->hid = hid;
		D_FPRINTF(stream, " hwid: [%s]", hid);
	}

	if (rank) {
		evt->rank = (uint32_t)*rank;
		D_FPRINTF(stream, " rank: [%u]", *rank);
	}

	if (jid) {
		evt->jid = jid;
		D_FPRINTF(stream, " jobid: [%s]", jid);
	}

	if (puuid && !uuid_is_null(*puuid)) {
		D_ALLOC(evt->puuid, DAOS_UUID_STR_SIZE);
		if (!evt->puuid)
			D_GOTO(out_fail, rc = -DER_NOMEM);
		D_ASPRINTF(evt->puuid, DF_UUIDF, DP_UUID(*puuid));
		D_FPRINTF(stream, " puuid: ["DF_UUIDF"]", DP_UUID(*puuid));
	}

	if (cuuid && !uuid_is_null(*cuuid)) {
		D_ALLOC(evt->cuuid, DAOS_UUID_STR_SIZE);
		if (!evt->cuuid)
			D_GOTO(out_fail, rc = -DER_NOMEM);
		D_ASPRINTF(evt->cuuid, DF_UUIDF, DP_UUID(*cuuid));
		D_FPRINTF(stream, " cuuid: ["DF_UUIDF"]", DP_UUID(*cuuid));
	}

	if (oid) {
		D_ALLOC(evt->oid, DAOS_RAS_STR_FIELD_SIZE);
		if (!evt->oid)
			D_GOTO(out_fail, rc = -DER_NOMEM);
		D_ASPRINTF(evt->oid, DF_OID, DP_OID(*oid));
		D_FPRINTF(stream, " oid: ["DF_OID"]", DP_OID(*oid));
	}

	if (cop) {
		evt->cop = cop;
		D_FPRINTF(stream, " control_op: [%s]", cop);
	}

	fclose(stream);
	D_DEBUG(DB_MGMT, "&&& RAS EVENT%s", buf);
	free(buf);

	return 0;
out_fail:
	free_ras(evt);
	fclose(stream);
	free(buf);

	return rc;
}

static int
send_ras(Mgmt__RASEvent *evt)
{
	Mgmt__ClusterEventReq	 req = MGMT__CLUSTER_EVENT_REQ__INIT;
	uint8_t			*reqb;
	size_t			 reqb_size;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	int			 rc;

	if (!evt) {
		D_ERROR("null RAS event\n");
		return -DER_INVAL;
	}
	req.event = evt;

	reqb_size = mgmt__cluster_event_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	mgmt__cluster_event_req__pack(&req, reqb);
	rc = drpc_call_create(dss_drpc_ctx, DRPC_MODULE_MGMT,
			      DRPC_METHOD_MGMT_CLUSTER_EVENT, &dreq);
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
	return rc;
}

void
ds_notify_ras_event(char *id, enum ras_event_type type, enum ras_event_sev sev,
		    char *hid, d_rank_t *rank, char *jid, uuid_t *puuid,
		    uuid_t *cuuid, daos_obj_id_t *oid, char *cop, char *msg,
		    char *data)
{
	Mgmt__RASEvent	 evt = MGMT__RASEVENT__INIT;
	int		 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("dRPC not connected: "DF_RC"\n", DP_RC(-DER_UNINIT));
		return;
	}

	/* use opaque blob oneof case for extended info for passthrough event */
	evt.extended_info_case = MGMT__RASEVENT__EXTENDED_INFO_STR_INFO;
	evt.str_info = data;

	rc = init_ras(id, sev, type, hid, rank, jid, puuid, cuuid, oid, cop,
		      msg, &evt);
	if (rc != 0) {
		D_ERROR("failed to init RAS event: "DF_RC"\n", DP_RC(rc));
		return;
	}

	rc = send_ras(&evt);
	if (rc != 0)
		D_ERROR("failed to send RAS event: "DF_RC"\n", DP_RC(rc));

	free_ras(&evt);
}

int
ds_notify_pool_svc_update(uuid_t *puuid, d_rank_list_t *svc)
{
	Mgmt__RASEvent		 evt = MGMT__RASEVENT__INIT;
	Mgmt__PoolSvcEventInfo	 info = MGMT__POOL_SVC_EVENT_INFO__INIT;
	int			 rc;

	if (dss_drpc_ctx == NULL) {
		D_ERROR("dRPC not connected\n");
		return -DER_UNINIT;
	}

	D_ASSERT(puuid && !uuid_is_null(*puuid));
	D_ASSERT(svc != NULL && svc->rl_nr > 0);

	rc = rank_list_to_uint32_array(svc, &info.svc_reps, &info.n_svc_reps);
	if (rc != 0) {
		D_ERROR("failed to convert svc replicas to proto\n");
		return rc;
	}

	evt.extended_info_case = MGMT__RASEVENT__EXTENDED_INFO_POOL_SVC_INFO;
	evt.pool_svc_info = &info;

	/* TODO: add rank to event */
	rc = init_ras(RAS_POOL_SVC_REPS_UPDATE, RAS_SEV_INFO,
		      RAS_TYPE_STATE_CHANGE, NULL /* hid */, NULL /* rank */,
		      NULL /* jid */, puuid, NULL /* cuuid */, NULL /* oid */,
		      NULL /* cop */,
		      "List of pool service replica ranks has been updated.",
		      &evt);
	if (rc != 0) {
		D_ERROR("failed to populate generic RAS event details\n");
		goto out_svcreps;
	}

	rc = send_ras(&evt);

	free_ras(&evt);
out_svcreps:
	D_FREE(info.svc_reps);

	return rc;
}
