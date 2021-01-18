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
free_event(Shared__RASEvent *evt)
{
	if (evt->obj_id != NULL)
		D_FREE(evt->obj_id);
	if (evt->pool_uuid != NULL)
		D_FREE(evt->pool_uuid);
	if (evt->cont_uuid != NULL)
		D_FREE(evt->cont_uuid);
	if (evt->hostname != NULL)
		D_FREE(evt->hostname);
	if (evt->timestamp != NULL)
		D_FREE(evt->timestamp);
}

static int
init_event(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev,
	   char *hwid, d_rank_t *rank, char *jobid, uuid_t *pool,
	   uuid_t *cont, daos_obj_id_t *objid, char *ctlop,
	   Shared__RASEvent *evt)
{
	struct dss_module_info	*dmi = get_module_info();
	struct timeval		 tv;
	struct tm		*tm;
	int			 rc;

	/* Populate mandatory RAS fields. */

	(void)gettimeofday(&tv, 0);
	tm = localtime(&tv.tv_sec);
	if (tm != NULL) {
		D_ASPRINTF(evt->timestamp,
			   "%04d/%02d/%02d-%02d:%02d:%02d.%02ld",
			   tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			   tm->tm_hour, tm->tm_min, tm->tm_sec,
			   (long)tv.tv_usec / 10000);
	} else {
		D_ERROR("unable to generate timestamp\n");
		evt->timestamp = NULL;
	}

	evt->id = (uint32_t)id;
	evt->type = (uint32_t)type;
	evt->severity = (uint32_t)sev;
	evt->proc_id = (uint64_t)getpid();
	dmi = get_module_info();
	if (dmi != NULL)
		evt->thread_id = (uint64_t)dmi->dmi_xs_id;
	else
		D_ERROR("failed to retrieve xstream id");

	if (strnlen(dss_hostname, DSS_HOSTNAME_MAX_LEN) == 0) {
		D_ERROR("missing hostname parameter\n");
		D_GOTO(out_ts, rc = -DER_UNINIT);
	}
	D_STRNDUP(evt->hostname, dss_hostname, DSS_HOSTNAME_MAX_LEN);

	if ((msg == NULL) || strnlen(msg, DAOS_RAS_STR_FIELD_SIZE) == 0) {
		D_ERROR("missing msg parameter\n");
		D_GOTO(out_host, rc = -DER_INVAL);
	}
	evt->msg = msg;

	/* Populate optional RAS fields. */

	evt->hw_id = (hwid != NULL) ? hwid : NULL;
	/* UINT32_MAX/CRT_NO_RANK indicates nil rank in daos_{,io_}server */
	evt->rank = (rank != NULL) ? (uint32_t)*rank : CRT_NO_RANK;
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

out_host:
	if (evt->hostname != NULL)
		D_FREE(evt->hostname);
out_ts:
	if (evt->timestamp != NULL)
		D_FREE(evt->timestamp);

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

	/* Log optional RAS fields. */
	if (evt->hw_id != NULL)
		D_FPRINTF(stream, " hwid: [%s]", evt->hw_id);
	if (evt->rank != CRT_NO_RANK)
		D_FPRINTF(stream, " rank: [%u]", evt->rank);
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
	D_DEBUG(DB_MGMT, "&&& RAS EVENT%s", buf);
	D_FREE(buf);
}

static int
send_event(Shared__RASEvent *evt)
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
	free_event(evt);

	return rc;
}

static int
raise_ras(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev, char *hwid,
	  d_rank_t *rank, char *jobid, uuid_t *pool, uuid_t *cont,
	  daos_obj_id_t *objid, char *ctlop, Shared__RASEvent *evt)
{
	int	rc;

	rc = init_event(id, msg, type, sev, hwid, rank, jobid, pool, cont,
			objid, ctlop, evt);
	if (rc != 0) {
		D_ERROR("failed to init RAS event %s: "DF_RC"\n",
			ras_event2str(RAS_POOL_REPS_UPDATE), DP_RC(rc));
		return rc;
	}

	log_event(evt);
	rc = send_event(evt);
	if (rc != 0)
		D_ERROR("failed to send RAS event %s over dRPC: "DF_RC"\n",
			ras_event2str(RAS_POOL_REPS_UPDATE), DP_RC(rc));

	free_event(evt);

	return rc;
}

void
ds_notify_ras_event(ras_event_t id, char *msg, ras_type_t type, ras_sev_t sev,
		    char *hwid, d_rank_t *rank, char *jobid, uuid_t *pool,
		    uuid_t *cont, daos_obj_id_t *objid, char *ctlop, char *data)
{
	Shared__RASEvent	evt = SHARED__RASEVENT__INIT;
	d_rank_t		this_rank = dss_self_rank();

	/* use opaque blob oneof case for extended info for passthrough event */
	evt.extended_info_case = SHARED__RASEVENT__EXTENDED_INFO_STR_INFO;
	evt.str_info = (data == NULL) ? "" : data;

	/* populate rank param if empty */
	if (rank == NULL)
		rank = &this_rank;

	raise_ras(id, msg, type, sev, hwid, rank, jobid, pool, cont, objid,
		  ctlop, &evt);
}

int
ds_notify_pool_svc_update(uuid_t *pool, d_rank_list_t *svcl)
{
	Shared__RASEvent			evt = SHARED__RASEVENT__INIT;
	Shared__RASEvent__PoolSvcEventInfo	info = \
		SHARED__RASEVENT__POOL_SVC_EVENT_INFO__INIT;
	d_rank_t				rank = dss_self_rank();
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
		       RAS_TYPE_STATE_CHANGE, RAS_SEV_INFO, NULL /* hwid */,
		       &rank /* rank */, NULL /* jobid */, pool,
		       NULL /* cont */, NULL /* objid */, NULL /* ctlop */,
		       &evt);

	D_FREE(info.svc_reps);

	return rc;
}
