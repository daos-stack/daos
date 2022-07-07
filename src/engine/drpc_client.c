/*
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS server. It implements the dRPC client for
 * communicating with daos_server.
 */

#define D_LOGFAC DD_FAC(server)

#include <daos_srv/daos_engine.h>

#include <daos_types.h>
#include <daos/drpc.h>
#include <daos/drpc_modules.h>
#include "srv.pb-c.h"
#include "srv_internal.h"
#include "drpc_internal.h"

/** dRPC UNIX-domain socket path (full, not directory-only) */
static char *dss_drpc_path;

struct dss_drpc_thread_arg {
	int32_t		  cta_module;
	int32_t		  cta_method;
	int		  cta_flags;
	void		 *cta_req;
	size_t		  cta_req_size;
	Drpc__Response	**cta_resp;
};

static void *
dss_drpc_thread(void *varg)
{
	struct dss_drpc_thread_arg	*arg = varg;
	struct drpc			*ctx;
	Drpc__Call			*call;
	Drpc__Response			*resp;
	int				 flags = R_SYNC;
	int				 rc;

	/* Establish a private connection to avoid dRPC concurrency problems. */
	rc = drpc_connect(dss_drpc_path, &ctx);
	if (rc != 0) {
		D_ERROR("failed to connect to dRPC server at %s: "DF_RC"\n",
			dss_drpc_path, DP_RC(rc));
		goto out;
	}

	rc = drpc_call_create(ctx, arg->cta_module, arg->cta_method, &call);
	if (rc != 0) {
		D_ERROR("failed to create dRPC %d/%d: "DF_RC"\n",
			arg->cta_module, arg->cta_method, DP_RC(rc));
		goto out_ctx;
	}
	call->body.data = arg->cta_req;
	call->body.len = arg->cta_req_size;

	if (arg->cta_flags & DSS_DRPC_NO_RESP)
		flags &= ~R_SYNC;

	rc = drpc_call(ctx, flags, call, &resp);
	if (rc != 0) {
		D_ERROR("failed to invoke dRPC %d/%d: "DF_RC"\n",
			arg->cta_module, arg->cta_method, DP_RC(rc));
		goto out_call;
	}

	if (arg->cta_flags & DSS_DRPC_NO_RESP)
		drpc_response_free(resp);
	else
		*arg->cta_resp = resp;
out_call:
	/* Let the caller free its own buffer. */
	call->body.data = NULL;
	call->body.len = 0;
	drpc_call_free(call);
out_ctx:
	drpc_close(ctx);
out:
	return (void *)(intptr_t)rc;
}

/**
 * Invoke a dRPC. See dss_drpc_call_flag for the usage of \a flags. If \a flags
 * includes DSS_DRPC_NO_RESP, \a resp is ignored; otherwise, the caller must
 * specify \a resp, and is responsible for freeing the response with
 * drpc_response_free.
 */
int
dss_drpc_call(int32_t module, int32_t method, void *req, size_t req_size,
	      unsigned int flags, Drpc__Response **resp)
{
	struct dss_drpc_thread_arg	 arg;
	struct sched_req_attr		 attr = {0};
	uuid_t				 anonym_uuid;
	struct sched_request		*sched_req;
	pthread_t			 thread;
	struct d_backoff_seq		 backoff_seq;
	void				*thread_rc;
	int				 rc;

	arg.cta_module = module;
	arg.cta_method = method;
	arg.cta_flags = flags;
	arg.cta_req = req;
	arg.cta_req_size = req_size;
	arg.cta_resp = resp;

	if (flags & (DSS_DRPC_NO_RESP | DSS_DRPC_NO_SCHED))
		return (int)(intptr_t)dss_drpc_thread(&arg);

	/* Initialize sched_req for the backoffs below. */
	uuid_clear(anonym_uuid);
	sched_req_attr_init(&attr, SCHED_REQ_ANONYM, &anonym_uuid);
	sched_req = sched_req_get(&attr, ABT_THREAD_NULL);
	if (sched_req == NULL) {
		D_ERROR("failed to get sched req\n");
		return -DER_NOMEM;
	}

	/* Create a thread to avoid blocking the current xstream. */
	rc = pthread_create(&thread, NULL /* attr */, dss_drpc_thread, &arg);
	if (rc != 0) {
		D_ERROR("failed to create thread for dRPC: %d "DF_RC"\n", rc,
			DP_RC(daos_errno2der(rc)));
		rc = daos_errno2der(rc);
		return rc;
	}

	/* Poll the thread for its completion. */
	rc = d_backoff_seq_init(&backoff_seq, 0 /* nzeros */,
				2 /* factor */, 8 /* next (ms) */,
				1 << 10 /* max (ms) */);
	D_ASSERTF(rc == 0, "d_backoff_seq_init: "DF_RC"\n", DP_RC(rc));
	do {
		sched_req_sleep(sched_req, d_backoff_seq_next(&backoff_seq));
		rc = pthread_tryjoin_np(thread, &thread_rc);
	} while (rc == EBUSY);
	/*
	 * The pthread_tryjoin_np call is expected to return either EBUSY or 0,
	 * unless there is a bug somewhere affecting its internal logic. If the
	 * thread may still be running, then we can't return safely, for arg is
	 * on the stack. Allocating arg on the heap seems to be a overkill.
	 * Hence, we simply assert that rc must be 0.
	 */
	D_ASSERTF(rc == 0, "failed to join dRPC thread: %d\n", rc);
	d_backoff_seq_fini(&backoff_seq);

	sched_req_put(sched_req);
	return (int)(intptr_t)thread_rc;
}

/* Notify daos_server that we are ready (e.g., to receive dRPC requests). */
int
drpc_notify_ready(void)
{
	Srv__NotifyReadyReq	req = SRV__NOTIFY_READY_REQ__INIT;
	uint8_t		       *reqb;
	size_t			reqb_size;
	Drpc__Response	       *dresp;
	uint64_t		incarnation;
	size_t			nr_sec_uris;
	char		      **sec_uris = NULL;
	int			rc;
	int			i;

	rc = crt_self_uri_get(0 /* tag */, &req.uri);
	if (rc != 0)
		goto out;
	rc = crt_self_incarnation_get(&incarnation);
	if (rc != 0)
		goto out_uri;

	nr_sec_uris = crt_get_nr_secondary_providers();
	if (nr_sec_uris > 0) {
		D_ALLOC_ARRAY(sec_uris, nr_sec_uris);
		if (sec_uris == NULL)
			D_GOTO(out_uri, rc = -DER_NOMEM);
		for (i = 0; i < nr_sec_uris; i++) {
			rc = crt_self_uri_get_secondary(i, &sec_uris[i]);
			if (rc != 0) {
				D_ERROR("failed to get secondary provider URI, idx=%d, rc=%d\n",
					i, rc);
				nr_sec_uris = i;
				goto out_sec_uri;
			}
			D_DEBUG(DB_MGMT, "secondary provider URI: %s\n", sec_uris[i]);
		}

		D_DEBUG(DB_MGMT, "setting secondary provider URIs\n");
		req.secondaryuris = sec_uris;
		req.n_secondaryuris = nr_sec_uris;

		D_DEBUG(DB_MGMT, "setting secondary provider number cart ctxs\n");
		req.n_secondarynctxs = nr_sec_uris;
		D_ALLOC_ARRAY(req.secondarynctxs, nr_sec_uris);
		if (req.secondarynctxs == NULL)
			D_GOTO(out_sec_uri, rc = -DER_NOMEM);
		for (i = 0; i < nr_sec_uris; i++)
			req.secondarynctxs[i] = dss_sec_xs_nr;
	}

	req.incarnation = incarnation;
	req.nctxs = DSS_CTX_NR_TOTAL;

	/* Do not free, this string is managed by the dRPC listener */
	req.drpclistenersock = drpc_listener_socket_path;
	req.instanceidx = dss_instance_idx;
	req.ntgts = dss_tgt_nr;

	reqb_size = srv__notify_ready_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL)
		D_GOTO(out_sec_nctxs, rc = -DER_NOMEM);
	srv__notify_ready_req__pack(&req, reqb);

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_SRV_NOTIFY_READY, reqb,
			   reqb_size, DSS_DRPC_NO_SCHED, &dresp);
	if (rc != 0)
		goto out_reqb;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		rc = -DER_IO;
	}

	drpc_response_free(dresp);
out_reqb:
	D_FREE(reqb);
out_sec_nctxs:
	D_FREE(req.secondarynctxs);
out_sec_uri:
	for (i = 0; i < nr_sec_uris; i++)
		D_FREE(sec_uris[i]);
	D_FREE(sec_uris);
out_uri:
	D_FREE(req.uri);
out:
	return rc;
}

/*
 * Notify daos_server that there has been a I/O error. This function doesn't
 * Argobots-schedule.
 */
int
ds_notify_bio_error(int media_err_type, int tgt_id)
{
	Srv__BioErrorReq	 bioerr_req = SRV__BIO_ERROR_REQ__INIT;
	uint8_t			*req;
	size_t			 req_size;
	int			 rc;

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

	/*
	 * Do not wait for the response, so that we don't Argobots-schedule or
	 * pthread-block.
	 */
	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_SRV_BIO_ERR, req,
			   req_size, DSS_DRPC_NO_RESP, NULL /* resp */);
	if (rc != 0)
		goto out_req;

out_req:
	D_FREE(req);
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
	Drpc__Response		*dresp;
	uint8_t			*req;
	size_t			 req_size;
	d_rank_list_t		*ranks;
	int			 rc;

	D_ALLOC(gps_req.uuid, DAOS_UUID_STR_SIZE);
	if (gps_req.uuid == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	uuid_unparse_lower(pool_uuid, gps_req.uuid);

	D_DEBUG(DB_MGMT, "fetching svc_ranks for "DF_UUID"\n",
		DP_UUID(pool_uuid));

	req_size = srv__get_pool_svc_req__get_packed_size(&gps_req);
	D_ALLOC(req, req_size);
	if (req == NULL)
		D_GOTO(out_uuid, rc = -DER_NOMEM);
	srv__get_pool_svc_req__pack(&gps_req, req);

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_SRV_GET_POOL_SVC, req,
			   req_size, 0 /* flags */, &dresp);
	if (rc != 0)
		goto out_req;
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
		if (gps_resp->status == -DER_NONEXIST) /* not an error */
			D_DEBUG(DB_MGMT, "pool svc "DF_UUID" not found: "
				DF_RC"\n",
				DP_UUID(pool_uuid), DP_RC(gps_resp->status));
		else
			D_ERROR("failure fetching svc_ranks for "DF_UUID": "
				DF_RC"\n",
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
out_req:
	D_FREE(req);
out_uuid:
	D_FREE(gps_req.uuid);
out:
	return rc;
}

int
ds_pool_find_bylabel(d_const_string_t label, uuid_t pool_uuid,
		     d_rank_list_t **svc_ranks)
{
	struct drpc_alloc		alloc = PROTO_ALLOCATOR_INIT(alloc);
	Srv__PoolFindByLabelReq		frq = SRV__POOL_FIND_BY_LABEL_REQ__INIT;
	Srv__PoolFindByLabelResp       *frsp = NULL;
	Drpc__Response		       *dresp;
	uint8_t			       *req;
	size_t				req_size;
	d_rank_list_t		       *ranks;
	int				rc;

	D_STRNDUP(frq.label, label, DAOS_PROP_LABEL_MAX_LEN);
	if (frq.label == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_DEBUG(DB_MGMT, "fetching svc_ranks for pool %s\n", label);

	req_size = srv__pool_find_by_label_req__get_packed_size(&frq);
	D_ALLOC(req, req_size);
	if (req == NULL)
		D_GOTO(out_label, rc = -DER_NOMEM);
	srv__pool_find_by_label_req__pack(&frq, req);

	rc = dss_drpc_call(DRPC_MODULE_SRV, DRPC_METHOD_SRV_POOL_FIND_BYLABEL,
			   req, req_size, 0 /* flags */, &dresp);
	if (rc != 0)
		goto out_req;
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("received erroneous dRPC response: %d\n",
			dresp->status);
		D_GOTO(out_dresp, rc = -DER_IO);
	}

	frsp = srv__pool_find_by_label_resp__unpack(&alloc.alloc,
						    dresp->body.len,
						    dresp->body.data);
	if (alloc.oom) {
		D_GOTO(out_dresp, rc = -DER_NOMEM);
	} else if (frsp == NULL) {
		D_ERROR("failed to unpack resp (get pool svc)\n");
		D_GOTO(out_dresp, rc = -DER_NOMEM);
	}

	if (frsp->status != 0) {
		if (frsp->status == -DER_NONEXIST) /* not an error */
			D_DEBUG(DB_MGMT, "pool %s not found, "DF_RC"\n",
				frq.label, DP_RC(frsp->status));
		else
			D_ERROR("failure finding pool %s, "DF_RC"\n",
				frq.label, DP_RC(frsp->status));
		D_GOTO(out_resp, rc = frsp->status);
	}

	rc = uuid_parse(frsp->uuid, pool_uuid);
	if (rc != 0) {
		D_ERROR("Unable to parse pool UUID %s: "DF_RC"\n", frsp->uuid,
			DP_RC(rc));
		D_GOTO(out_resp, rc = -DER_IO);
	}

	ranks = uint32_array_to_rank_list(frsp->svcreps,
					  frsp->n_svcreps);
	if (ranks == NULL)
		D_GOTO(out_resp, rc = -DER_NOMEM);
	*svc_ranks = ranks;
	D_DEBUG(DB_MGMT, "pool %s: UUID="DF_UUID", %u svc replicas\n",
		frq.label, DP_UUID(pool_uuid), ranks->rl_nr);

out_resp:
	srv__pool_find_by_label_resp__free_unpacked(frsp, &alloc.alloc);
out_dresp:
	drpc_response_free(dresp);
out_req:
	D_FREE(req);
out_label:
	D_FREE(frq.label);
out:
	return rc;
}

int
drpc_init(void)
{
	D_ASSERT(dss_drpc_path == NULL);
	D_ASPRINTF(dss_drpc_path, "%s/%s", dss_socket_dir, "daos_server.sock");
	if (dss_drpc_path == NULL)
		return -DER_NOMEM;
	return 0;
}

void
drpc_fini(void)
{
	D_ASSERT(dss_drpc_path != NULL);
	D_FREE(dss_drpc_path);
}
