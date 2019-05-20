/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
/*
 * DAOS management client library. It exports the mgmt API defined in
 * daos_mgmt.h
 */
#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/mgmt.h>

#include <daos/agent.h>
#include <daos/drpc_modules.h>
#include <daos/drpc.pb-c.h>
#include <daos/event.h>
#include "mgmt.pb-c.h"
#include "rpc.h"

static int
rip_cp(tse_task_t *task, void *data)
{
	crt_rpc_t		*rpc = *((crt_rpc_t **)data);
	int                      rc = task->dt_result;

	if (rc)
		D_ERROR("RPC error while killing rank: %d\n", rc);

	dc_mgmt_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

int
dc_mgmt_svc_rip(tse_task_t *task)
{
	daos_svc_rip_t		*args;
	crt_endpoint_t		 svr_ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		 opc;
	struct mgmt_svc_rip_in	*rip_in;
	int			 rc;

	args = dc_task_get_args(task);
	rc = dc_mgmt_group_attach(args->grp, &svr_ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to grp %s, rc %d.\n", args->grp, rc);
		rc = -DER_INVAL;
		goto out_task;
	}

	svr_ep.ep_rank = args->rank;
	svr_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_SVC_RIP, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_SVC_RIP) failed, rc: %d.\n",
			rc);
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	rip_in = crt_req_get(rpc);
	D_ASSERT(rip_in != NULL);

	/** fill in request buffer */
	rip_in->rip_flags = args->force;

	rc = tse_task_register_comp_cb(task, rip_cp, &rpc, sizeof(rpc));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	crt_req_addref(rpc); /** for rip_cp */
	D_DEBUG(DB_MGMT, "killing rank %u\n", args->rank);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	dc_mgmt_group_detach(svr_ep.ep_grp);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_mgmt_set_params(tse_task_t *task)
{
	daos_set_params_t		*args;
	struct mgmt_params_set_in	*in;
	crt_endpoint_t			ep;
	crt_rpc_t			*rpc = NULL;
	crt_opcode_t			opc;
	int				rc;

	args = dc_task_get_args(task);
	rc = dc_mgmt_group_attach(args->grp, &ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to grp %s, rc %d.\n", args->grp, rc);
		rc = -DER_INVAL;
		goto out_task;
	}

	/* if rank == -1 means it will set params on all servers, which we will
	 * send it to 0 temporarily.
	 */
	ep.ep_rank = args->rank == -1 ? 0 : args->rank;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_PARAMS_SET, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_SVC_RIP) failed, rc: %d.\n",
			rc);
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	in = crt_req_get(rpc);
	D_ASSERT(in != NULL);

	/** fill in request buffer */
	in->ps_rank = args->rank;
	in->ps_key_id = args->key_id;
	in->ps_value = args->value;
	in->ps_value_extra = args->value_extra;

	rc = tse_task_register_comp_cb(task, rip_cp, &rpc, sizeof(rpc));
	if (rc != 0)
		D_GOTO(err_rpc, rc);

	crt_req_addref(rpc); /** for rip_cp */
	D_DEBUG(DB_MGMT, "set parameter %d/%u/"DF_U64".\n", args->rank,
		 args->key_id, args->value);

	/** send the request */
	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	dc_mgmt_group_detach(ep.ep_grp);
out_task:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_mgmt_profile(uint64_t modules, char *path, bool start)
{
	struct mgmt_profile_in	*in;
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		opc;
	int			rc;

	rc = dc_mgmt_group_attach(NULL, &ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to grp rc %d.\n", rc);
		return -DER_INVAL;
	}

	ep.ep_rank = 0;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_PROFILE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_get_crt_ctx(), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create failed, rc: %d.\n", rc);
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	in = crt_req_get(rpc);
	in->p_module = modules;
	in->p_path = path;
	in->p_op = start ? MGMT_PROFILE_START : MGMT_PROFILE_STOP;
	/** send the request */
	rc = daos_rpc_send_wait(rpc);
err_grp:
	D_DEBUG(DB_MGMT, "mgmt profile: rc %d\n", rc);
	dc_mgmt_group_detach(ep.ep_grp);
	return rc;
}

struct psr {
	d_rank_t	 rank;
	char		*uri;
};

/*
 * Get the attach info (i.e., the CaRT PSRs) for sys. npsrs outputs the number
 * of elements in psrs. psrs outputs the array of struct psr objects. Callers
 * are responsible for freeing psrs and the URIs in it.
 */
static int
get_attach_info(const char *sys, int *npsrs, struct psr **psrs)
{
	struct drpc		*ctx;
	Mgmt__GetAttachInfoReq	 req = MGMT__GET_ATTACH_INFO_REQ__INIT;
	Mgmt__GetAttachInfoResp	*resp;
	uint8_t			*reqb;
	size_t			 reqb_size;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	struct psr		*p;
	int			 i;
	int			 rc;

	D_DEBUG(DB_MGMT, "getting attach info for %s\n", sys);

	/* Connect to daos_agent. */
	D_ASSERT(dc_agent_sockpath != NULL);
	ctx = drpc_connect(dc_agent_sockpath);
	if (ctx == NULL) {
		D_ERROR("failed to connect to %s\n", dc_agent_sockpath);
		rc = -DER_BADPATH;
		goto out;
	}

	/* Prepare the GetAttachInfo request. */
	req.sys = (char *)sys;
	reqb_size = mgmt__get_attach_info_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL) {
		rc = -DER_NOMEM;
		goto out_ctx;
	}
	mgmt__get_attach_info_req__pack(&req, reqb);
	dreq = drpc_call_create(ctx, DRPC_MODULE_MGMT,
				DRPC_METHOD_MGMT_GET_ATTACH_INFO);
	if (dreq == NULL) {
		rc = -DER_NOMEM;
		D_FREE(reqb);
		goto out_ctx;
	}
	dreq->body.len = reqb_size;
	dreq->body.data = reqb;

	/* Make the GetAttachInfo call and get the response. */
	rc = drpc_call(ctx, R_SYNC, dreq, &dresp);
	if (rc != 0) {
		D_ERROR("GetAttachInfo call failed: %d\n", rc);
		goto out_dreq;
	}
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("GetAttachInfo unsuccessful: %d\n", dresp->status);
		rc = -DER_MISC;
		goto out_dresp;
	}
	resp = mgmt__get_attach_info_resp__unpack(NULL, dresp->body.len,
						  dresp->body.data);
	if (resp == NULL) {
		D_ERROR("failed to unpack GetAttachInfo response\n");
		rc = -DER_MISC;
		goto out_dresp;
	}
	if (resp->status != MGMT__DAOS_REQUEST_STATUS__SUCCESS) {
		D_ERROR("GetAttachInfo failed: %d\n", resp->status);
		rc = -DER_MISC;
		goto out_resp;
	}

	/* Output to the caller. */
	D_ALLOC_ARRAY(p, resp->n_psrs);
	if (p == NULL) {
		rc = -DER_NOMEM;
		goto out_resp;
	}
	for (i = 0; i < resp->n_psrs; i++) {
		p[i].rank = resp->psrs[i]->rank;
		D_ASPRINTF(p[i].uri, "%s", resp->psrs[i]->uri);
		if (p[i].uri == NULL) {
			rc = -DER_NOMEM;
			break;
		}
	}
	if (rc != 0) {
		for (; i >= 0; i--) {
			if (p[i].uri != NULL)
				D_FREE(p[i].uri);
		}
		D_FREE(p);
		goto out_resp;
	}
	*npsrs = resp->n_psrs;
	*psrs = p;

out_resp:
	mgmt__get_attach_info_resp__free_unpacked(resp, NULL);
out_dresp:
	drpc_response_free(dresp);
out_dreq:
	/* This also frees reqb via dreq->body.data. */
	drpc_call_free(dreq);
out_ctx:
	drpc_close(ctx);
out:
	return rc;
}

/* Write a temporary attach info file and report dir and path. */
static int
write_attach_info_file(const char *group_id, int npsrs, struct psr *psrs,
		       char **dir, char **path)
{
	char		*d;
	char		*p;
	FILE		*f;
	unsigned	 size = 128;
	int		 i;
	int		 rc;

	/* Make a temporary directory for the attach info file. */
	D_ASPRINTF(d, "/tmp/daos_attach_info-XXXXXXXX");
	if (d == NULL) {
		rc = -DER_NOMEM;
		goto err;
	}
	p = mkdtemp(d);
	if (p == NULL) {
		rc = errno;
		D_ERROR("failed to create %s: %d\n", d, rc);
		rc = daos_errno2der(rc);
		goto err_d;
	}

	/* Open the attach info file. */
	D_ASPRINTF(p, "%s/%s.attach_info_tmp", d, group_id);
	if (p == NULL) {
		rc = -DER_NOMEM;
		goto err_dir;
	}
	f = fopen(p, "w");
	if (f == NULL) {
		rc = errno;
		D_ERROR("failed to create %s: %d\n", p, rc);
		rc = daos_errno2der(rc);
		goto err_p;
	}

	/* Write the attach info. */
	d_getenv_int("DAOS_SERVER_GROUP_MAX", &size);
	rc = fprintf(f, "name %s\nsize %u\nself\n", group_id, size);
	if (rc < 0) {
		rc = errno;
		D_ERROR("failed to write to %s: %d\n", p, rc);
		rc = daos_errno2der(rc);
		goto err_f;
	}
	for (i = 0; i < npsrs; i++) {
		rc = fprintf(f, "%u %s\n", psrs[i].rank, psrs[i].uri);
		if (rc < 0) {
			rc = errno;
			D_ERROR("failed to write name to %s: %d\n", p, rc);
			rc = daos_errno2der(rc);
			goto err_f;
		}
	}
	rc = fclose(f);
	if (rc != 0) {
		rc = errno;
		D_ERROR("failed to close %s: %d\n", p, rc);
		rc = daos_errno2der(rc);
		goto err_f;
	}

	*dir = d;
	*path = p;
	return 0;

err_f:
	fclose(f);
	unlink(p);
err_p:
	D_FREE(p);
err_dir:
	rmdir(d);
err_d:
	D_FREE(d);
err:
	return rc;
}

/*
 * Before CaRT supports a better way, perform the attach operation using a
 * temporary attach info file.
 */
static int
attach(const char *group_id, int npsrs, struct psr *psrs, crt_group_t **group)
{
	char   *dir = NULL;
	char   *path = NULL;
	int	rc;

	if (psrs != NULL) {
		rc = write_attach_info_file(group_id, npsrs, psrs, &dir, &path);
		if (rc != 0)
			goto out;
		rc = crt_group_config_path_set(dir);
		if (rc != 0) {
			D_ERROR("failed to set group config dir to %s: %d\n",
				dir, rc);
			goto out_dir;
		}
	}

	rc = crt_group_attach((char *)group_id, group);
	if (rc != 0)
		D_ERROR("failed to attach to group %s: %d\n", group_id, rc);

out_dir:
	if (dir != NULL) {
		/* Try removing the temporary file and ignore any errors. */
		unlink(path);
		D_FREE(path);
		rmdir(dir);
		D_FREE(dir);
	}
out:
	return rc;
}

static pthread_mutex_t attach_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Attach to system \a group_id.
 *
 * \param[in]	group_id	group identifier (i.e., system name)
 * \param[out]	group		group handle
 */
int
dc_mgmt_group_attach(const char *group_id, crt_group_t **group)
{
	bool		pmixless = false;
	struct psr     *psrs = NULL;
	int		npsrs = 0;
	int		i;
	int		rc;

	if (group_id == NULL)
		group_id = DAOS_DEFAULT_GROUP_ID;

	D_MUTEX_LOCK(&attach_lock);

	*group = crt_group_lookup((char *)group_id);
	if (*group != NULL) {
		/*
		 * Either the group has already been attached, or this process
		 * is one of the servers. Skip the GetAttachInfo dRPC, but
		 * still call attach to get an attach reference.
		 */
		rc = 0;
		goto attach;
	}

	D_DEBUG(DB_MGMT, "attaching to group '%s'\n", group_id);

	d_getenv_bool("DAOS_PMIXLESS", &pmixless);
	if (pmixless) {
		rc = get_attach_info(group_id, &npsrs, &psrs);
		if (rc != 0)
			goto out_lock;
	}

attach:
	rc = attach(group_id, npsrs, psrs, group);

	if (psrs != NULL) {
		for (i = 0; i < npsrs; i++)
			D_FREE(psrs[i].uri);
		D_FREE(psrs);
	}
out_lock:
	D_MUTEX_UNLOCK(&attach_lock);
	return rc;
}

/**
 * Detach from system group.
 *
 * \param[in]	group	group handle
 */
int
dc_mgmt_group_detach(crt_group_t *group)
{
	int rc;

	D_ASSERT(group != NULL);
	D_MUTEX_LOCK(&attach_lock);
	D_DEBUG(DB_MGMT, "detaching from group '%s'\n", group->cg_grpid);
	rc = crt_group_detach(group);
	D_MUTEX_UNLOCK(&attach_lock);
	return rc;
}

static int
query_cp(tse_task_t *task, void *data)
{
	crt_rpc_t	       *rpc = *(crt_rpc_t **)data;
	struct mgmt_query_out  *out = crt_reply_get(rpc);
	daos_query_t	       *args = dc_task_get_args(task);
	struct server_entry    *servers = out->qo_servers.ca_arrays;
	int			i;
	int			rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("RPC error while querying system: %d\n", rc);
		goto out;
	}

	D_PRINT("System: %s\n", args->grp);
	D_PRINT("Map version: %u\n", out->qo_map_version);
	D_PRINT("Map in sync: %s\n", out->qo_map_in_sync ? "yes" : "no");
	D_PRINT("Servers:\n");
	for (i = 0; i < out->qo_servers.ca_count; i++)
		D_PRINT("\t%u\t%s\t%s\n", servers[i].se_rank,
			servers[i].se_flags == 0 ? "OUT" : "IN",
			servers[i].se_uri);

out:
	dc_mgmt_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

int
dc_mgmt_query(tse_task_t *task)
{
	daos_query_t		*args;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	crt_opcode_t		 opc;
	int			 rc;

	args = dc_task_get_args(task);
	rc = dc_mgmt_group_attach(args->grp, &ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to group %s: %d\n", args->grp, rc);
		goto err_task;
	}

	ep.ep_rank = args->rank;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_QUERY, DAOS_MGMT_MODULE, DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create MGMT_QUERY request: %d\n", rc);
		goto err_grp;
	}

	rc = tse_task_register_comp_cb(task, query_cp, &rpc, sizeof(rpc));
	if (rc != 0)
		goto err_rpc;

	crt_req_addref(rpc); /* for query_cp */

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	dc_mgmt_group_detach(ep.ep_grp);
err_task:
	tse_task_complete(task, rc);
	return rc;
}

static int
query_server_cp(tse_task_t *task, void *data)
{
	crt_rpc_t			*rpc = *(crt_rpc_t **)data;
	struct mgmt_query_server_out	*out = crt_reply_get(rpc);
	daos_query_server_t		*args = dc_task_get_args(task);
	struct server_entry		*servers = out->eo_servers.ca_arrays;
	int				 i;
	int				 rc = task->dt_result;

	if (rc != 0) {
		D_ERROR("RPC error while querying server: %d\n", rc);
		goto out;
	}

	D_PRINT("System: %s\n", args->grp);
	D_PRINT("Server: %u\n", args->rank);
	D_PRINT("Known map version: %u\n", out->eo_map_version);
	D_PRINT("Known servers:\n");
	for (i = 0; i < out->eo_servers.ca_count; i++)
		D_PRINT("\t%u\t%s\n", servers[i].se_rank,
			servers[i].se_flags == SWIM_MEMBER_ALIVE ? "ALIVE" :
			(servers[i].se_flags == SWIM_MEMBER_SUSPECT ?
			 "SUSPECT" : "DEAD"));

out:
	dc_mgmt_group_detach(rpc->cr_ep.ep_grp);
	crt_req_decref(rpc);
	return rc;
}

int
dc_mgmt_query_server(tse_task_t *task)
{
	daos_query_server_t	*args;
	crt_endpoint_t		 ep;
	crt_rpc_t		*rpc;
	crt_opcode_t		 opc;
	int			 rc;

	args = dc_task_get_args(task);
	rc = dc_mgmt_group_attach(args->grp, &ep.ep_grp);
	if (rc != 0) {
		D_ERROR("failed to attach to group %s: %d\n", args->grp, rc);
		goto err_task;
	}

	ep.ep_rank = args->rank;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_QUERY_SERVER, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create MGMT_QUERY_SERVER request: %d\n", rc);
		goto err_grp;
	}

	rc = tse_task_register_comp_cb(task, query_server_cp, &rpc,
				       sizeof(rpc));
	if (rc != 0)
		goto err_rpc;

	crt_req_addref(rpc); /* for query_cp */

	return daos_rpc_send(rpc, task);

err_rpc:
	crt_req_decref(rpc);
err_grp:
	dc_mgmt_group_detach(ep.ep_grp);
err_task:
	tse_task_complete(task, rc);
	return rc;
}

/**
 * Initialize management interface
 */
int
dc_mgmt_init()
{
	int rc;

	rc = daos_rpc_register(&mgmt_proto_fmt, MGMT_PROTO_CLI_COUNT,
				NULL, DAOS_MGMT_MODULE);
	if (rc != 0)
		D_ERROR("failed to register mgmt RPCs: %d\n", rc);

	return rc;
}

/**
 * Finalize management interface
 */
void
dc_mgmt_fini()
{
	daos_rpc_unregister(&mgmt_proto_fmt);
}

int dc2_mgmt_svc_rip(tse_task_t *task)
{
	return -DER_NOSYS;
}
