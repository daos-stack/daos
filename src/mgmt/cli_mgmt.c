/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS management client library. It exports the mgmt API defined in
 * daos_mgmt.h
 */

#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/mgmt.h>

#include <daos/agent.h>
#include <daos/drpc_modules.h>
#include <daos/drpc.pb-c.h>
#include <daos/event.h>
#include <daos/job.h>
#include <daos/pool.h>
#include "svc.pb-c.h"
#include "rpc.h"
#include <errno.h>
#include <stdlib.h>

int
dc_cp(tse_task_t *task, void *data)
{
	struct cp_arg	*arg = data;
	int		 rc = task->dt_result;

	if (rc)
		D_ERROR("RPC error: "DF_RC"\n", DP_RC(rc));

	dc_mgmt_sys_detach(arg->sys);
	crt_req_decref(arg->rpc);
	return rc;
}

int
dc_deprecated(tse_task_t *task)
{
	D_ERROR("This API is deprecated\n");
	tse_task_complete(task, -DER_NOSYS);
	return -DER_NOSYS;
}

int
dc_mgmt_profile(char *path, int avg, bool start)
{
	struct dc_mgmt_sys	*sys;
	struct mgmt_profile_in	*in;
	crt_endpoint_t		ep;
	crt_rpc_t		*rpc = NULL;
	crt_opcode_t		opc;
	int			rc;

	rc = dc_mgmt_sys_attach(NULL, &sys);
	if (rc != 0) {
		D_ERROR("failed to attach to grp rc "DF_RC"\n", DP_RC(rc));
		return -DER_INVAL;
	}

	ep.ep_grp = sys->sy_group;
	ep.ep_rank = 0;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_PROFILE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_get_crt_ctx(), &ep, opc, &rpc);
	if (rc != 0) {
		D_ERROR("crt_req_create failed, rc: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_grp, rc);
	}

	D_ASSERT(rpc != NULL);
	in = crt_req_get(rpc);
	in->p_path = path;
	in->p_avg = avg;
	in->p_op = start ? MGMT_PROFILE_START : MGMT_PROFILE_STOP;
	/** send the request */
	rc = daos_rpc_send_wait(rpc);
err_grp:
	D_DEBUG(DB_MGMT, "mgmt profile: rc "DF_RC"\n", DP_RC(rc));
	dc_mgmt_sys_detach(sys);
	return rc;
}

#define copy_str(dest, src)				\
({							\
	int	__rc = 1;				\
	size_t	__size = strnlen(src, sizeof(dest));	\
							\
	if (__size != sizeof(dest)) {			\
		memcpy(dest, src, __size + 1);		\
		__rc = 0;				\
	}						\
	__rc;						\
})

/* Fill info based on resp. */
static int
fill_sys_info(Mgmt__GetAttachInfoResp *resp, struct dc_mgmt_sys_info *info)
{
	int			 i;
	Mgmt__ClientNetHint	*hint = resp->client_net_hint;

	if (hint == NULL) {
		D_ERROR("GetAttachInfo failed: %d. "
			"no client networking hint set. "
			"libdaos.so is incompatible with DAOS Agent.\n",
			resp->status);
		return -DER_AGENT_INCOMPAT;
	}

	if (strnlen(hint->provider, sizeof(info->provider)) == 0) {
		D_ERROR("GetAttachInfo failed: %d. "
			"provider is undefined. "
			"libdaos.so is incompatible with DAOS Agent.\n",
			resp->status);
		return -DER_AGENT_INCOMPAT;
	}

	if (strnlen(hint->interface, sizeof(info->interface)) == 0) {
		D_ERROR("GetAttachInfo failed: %d. "
			"interface is undefined. "
			"libdaos.so is incompatible with DAOS Agent.\n",
			resp->status);
		return -DER_AGENT_INCOMPAT;
	}

	if (strnlen(hint->domain, sizeof(info->domain)) == 0) {
		D_ERROR("GetAttachInfo failed: %d. "
			"domain string is undefined. "
			"libdaos.so is incompatible with DAOS Agent.\n",
			resp->status);
		return -DER_AGENT_INCOMPAT;
	}

	if (copy_str(info->provider, hint->provider)) {
		D_ERROR("GetAttachInfo failed: %d. "
			"provider string too long.\n",
			resp->status);

		return -DER_INVAL;
	}

	if (copy_str(info->interface, hint->interface)) {
		D_ERROR("GetAttachInfo failed: %d. "
			"interface string too long\n",
			resp->status);
		return -DER_INVAL;
	}

	if (copy_str(info->domain, hint->domain)) {
		D_ERROR("GetAttachInfo failed: %d. "
			"domain string too long\n",
			resp->status);
		return -DER_INVAL;
	}

	if (strnlen(resp->sys, sizeof(info->system_name)) > 0) {
		if (copy_str(info->system_name, resp->sys)) {
			D_ERROR("GetAttachInfo failed: %d. System name string too long\n",
				resp->status);
			return -DER_INVAL;
		}
	} else {
		D_NOTE("No system name in GetAttachInfo. Agent may be out of date with libdaos\n");
	}

	info->crt_ctx_share_addr = hint->crt_ctx_share_addr;
	info->crt_timeout = hint->crt_timeout;
	info->srv_srx_set = hint->srv_srx_set;

	/* Fill info->ms_ranks. */
	if (resp->n_ms_ranks == 0) {
		D_ERROR("GetAttachInfo returned zero MS ranks\n");
		return -DER_AGENT_INCOMPAT;
	}
	info->ms_ranks = d_rank_list_alloc(resp->n_ms_ranks);
	if (info->ms_ranks == NULL)
		return -DER_NOMEM;
	for (i = 0; i < resp->n_ms_ranks; i++) {
		info->ms_ranks->rl_ranks[i] = resp->ms_ranks[i];
		D_DEBUG(DB_MGMT, "GetAttachInfo ms_ranks[%d]: rank=%u\n", i,
			info->ms_ranks->rl_ranks[i]);
	}

	D_DEBUG(DB_MGMT,
		"GetAttachInfo Provider: %s, Interface: %s, Domain: %s,"
		"CRT_CTX_SHARE_ADDR: %u, CRT_TIMEOUT: %u, "
		"FI_OFI_RXM_USE_SRX: %d\n",
		info->provider, info->interface, info->domain,
		info->crt_ctx_share_addr, info->crt_timeout,
		info->srv_srx_set);

	return 0;
}

static void
free_get_attach_info_resp(Mgmt__GetAttachInfoResp *resp)
{
	struct drpc_alloc alloc = PROTO_ALLOCATOR_INIT(alloc);

	mgmt__get_attach_info_resp__free_unpacked(resp, &alloc.alloc);
}

static void
put_attach_info(struct dc_mgmt_sys_info *info, Mgmt__GetAttachInfoResp *resp)
{
	if (resp != NULL)
		free_get_attach_info_resp(resp);
	d_rank_list_free(info->ms_ranks);
}

void
dc_put_attach_info(struct dc_mgmt_sys_info *info, Mgmt__GetAttachInfoResp *resp)
{
	return put_attach_info(info, resp);
}

/*
 * Get the attach info (i.e., rank URIs) for name. To avoid duplicating the
 * rank URIs, we return the GetAttachInfo response directly. Callers are
 * responsible for finalizing info and respp using put_attach_info.
 */
static int
get_attach_info(const char *name, bool all_ranks, struct dc_mgmt_sys_info *info,
		Mgmt__GetAttachInfoResp **respp)
{
	struct drpc_alloc	 alloc = PROTO_ALLOCATOR_INIT(alloc);
	struct drpc		*ctx;
	Mgmt__GetAttachInfoReq	 req = MGMT__GET_ATTACH_INFO_REQ__INIT;
	Mgmt__GetAttachInfoResp	*resp;
	uint8_t			*reqb;
	size_t			 reqb_size;
	Drpc__Call		*dreq;
	Drpc__Response		*dresp;
	int			 rc;

	D_DEBUG(DB_MGMT, "getting attach info for %s\n", name);

	/* Connect to daos_agent. */
	D_ASSERT(dc_agent_sockpath != NULL);
	rc = drpc_connect(dc_agent_sockpath, &ctx);
	if (rc != -DER_SUCCESS) {
		D_ERROR("failed to connect to %s " DF_RC "\n",
			dc_agent_sockpath, DP_RC(rc));
		if (rc == -DER_NONEXIST)
			rc = -DER_AGENT_COMM;
		D_GOTO(out, rc);
	}

	/* Prepare the GetAttachInfo request. */
	req.sys = (char *)name;
	req.all_ranks = all_ranks;
	reqb_size = mgmt__get_attach_info_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL) {
		rc = -DER_NOMEM;
		goto out_ctx;
	}
	mgmt__get_attach_info_req__pack(&req, reqb);
	rc = drpc_call_create(ctx, DRPC_MODULE_MGMT,
				DRPC_METHOD_MGMT_GET_ATTACH_INFO, &dreq);
	if (rc != 0) {
		D_FREE(reqb);
		goto out_ctx;
	}
	dreq->body.len = reqb_size;
	dreq->body.data = reqb;

	/* Make the GetAttachInfo call and get the response. */
	rc = drpc_call(ctx, R_SYNC, dreq, &dresp);
	if (rc != 0) {
		D_ERROR("GetAttachInfo call failed: "DF_RC"\n", DP_RC(rc));
		goto out_dreq;
	}
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("GetAttachInfo unsuccessful: %d\n", dresp->status);
		rc = -DER_MISC;
		goto out_dresp;
	}
	resp = mgmt__get_attach_info_resp__unpack(&alloc.alloc, dresp->body.len,
						  dresp->body.data);
	if (alloc.oom)
		D_GOTO(out_dresp, rc = -DER_NOMEM);
	if (resp == NULL) {
		D_ERROR("failed to unpack GetAttachInfo response\n");
		rc = -DER_MISC;
		goto out_dresp;
	}
	if (resp->status != 0) {
		D_ERROR("GetAttachInfo(%s) failed: "DF_RC"\n", req.sys,
			DP_RC(resp->status));
		rc = resp->status;
		goto out_resp;
	}

	/* Output to the caller. */
	rc = fill_sys_info(resp, info);
	if (rc != 0)
		goto out_resp;
	*respp = resp;

out_resp:
	if (rc != 0)
		mgmt__get_attach_info_resp__free_unpacked(resp, &alloc.alloc);
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

int
dc_get_attach_info(const char *name, bool all_ranks,
		   struct dc_mgmt_sys_info *info,
		   Mgmt__GetAttachInfoResp **respp) {
	return get_attach_info(name, all_ranks, info, respp);
}

static void
free_rank_uris(struct daos_rank_uri *uris, uint32_t nr_uris)
{
	uint32_t i;

	for (i = 0; i < nr_uris; i++)
		D_FREE(uris[i].dru_uri);
	D_FREE(uris);
}

static int
alloc_rank_uris(Mgmt__GetAttachInfoResp *resp, struct daos_rank_uri **out)
{
	struct daos_rank_uri	*uris;
	uint32_t		i;

	D_ALLOC_ARRAY(uris, resp->n_rank_uris);
	if (uris == NULL)
		return -DER_NOMEM;

	for (i = 0; i < resp->n_rank_uris; i++) {
		uris[i].dru_rank = resp->rank_uris[i]->rank;

		D_STRNDUP(uris[i].dru_uri, resp->rank_uris[i]->uri, CRT_ADDR_STR_MAX_LEN - 1);
		if (uris[i].dru_uri == NULL) {
			free_rank_uris(uris, i);
			return -DER_NOMEM;
		}
	}

	*out = uris;
	return 0;
}

int
dc_mgmt_get_sys_info(const char *sys, struct daos_sys_info **out)
{
	struct daos_sys_info	*info;
	struct dc_mgmt_sys_info	internal = {0};
	Mgmt__GetAttachInfoResp	*resp = NULL;
	struct daos_rank_uri	*ranks = NULL;
	int			rc = 0;

	if (out == NULL) {
		D_ERROR("daos_sys_info must be non-NULL\n");
		return -DER_INVAL;
	}

	rc = dc_get_attach_info(sys, true, &internal, &resp);
	if (rc != 0) {
		D_ERROR("dc_get_attach_info failed: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_ALLOC_PTR(info);
	if (info == NULL)
		D_GOTO(out_attach_info, rc = -DER_NOMEM);

	rc = alloc_rank_uris(resp, &ranks);
	if (rc != 0) {
		D_ERROR("failed to allocate rank URIs: "DF_RC"\n", DP_RC(rc));
		D_GOTO(err_info, rc);
	}

	info->dsi_nr_ranks = resp->n_ms_ranks;
	info->dsi_ranks = ranks;
	copy_str(info->dsi_system_name, internal.system_name);
	copy_str(info->dsi_fabric_provider, internal.provider);

	*out = info;

	D_GOTO(out_attach_info, rc = 0);

err_info:
	D_FREE(info);
out_attach_info:
	dc_put_attach_info(&internal, resp);
out:
	return rc;
}

void
dc_mgmt_put_sys_info(struct daos_sys_info *info)
{
	if (info == NULL)
		return;
	free_rank_uris(info->dsi_ranks, info->dsi_nr_ranks);
	D_FREE(info);
}

#define SYS_INFO_BUF_SIZE 16

static int g_num_serv_ranks = 1;

int dc_mgmt_net_get_num_srv_ranks(void)
{
	return g_num_serv_ranks;
}

static int
_split_env(char *env, char **name, char **value)
{
	char *sep;

	if (strnlen(env, 1024) == 1024)
		return -DER_INVAL;

	sep = strchr(env, '=');
	if (sep == NULL)
		return -DER_INVAL;
	*sep = '\0';
	*name = env;
	*value = sep + 1;

	return 0;
}

/*
 * Get the CaRT network configuration for this client node
 * via the get_attach_info() dRPC.
 * Configure the client's local environment with these parameters
 */
int dc_mgmt_net_cfg(const char *name)
{
	int rc;
	char buf[SYS_INFO_BUF_SIZE];
	char *crt_timeout;
	char *ofi_interface;
	char *ofi_domain;
	char *cli_srx_set;
	struct dc_mgmt_sys_info info;
	Mgmt__GetAttachInfoResp *resp;

	/* Query the agent for the CaRT network configuration parameters */
	rc = get_attach_info(name, true /* all_ranks */, &info, &resp);
	if (rc != 0)
		return rc;

	if (resp->client_net_hint != NULL && resp->client_net_hint->n_env_vars > 0) {
		int i;
		char *env = NULL;
		char *v_name = NULL;
		char *v_value = NULL;

		for (i = 0; i < resp->client_net_hint->n_env_vars; i++) {
			env = resp->client_net_hint->env_vars[i];
			if (env == NULL)
				continue;

			rc = _split_env(env, &v_name, &v_value);
			if (rc != 0) {
				D_ERROR("invalid client env var: %s\n", env);
				continue;
			}

			rc = setenv(v_name, v_value, 0);
			if (rc != 0)
				D_GOTO(cleanup, rc = d_errno2der(errno));
			D_DEBUG(DB_MGMT, "set server-supplied client env: %s", env);
		}
	}

	/* Save number of server ranks */
	g_num_serv_ranks = resp->n_rank_uris;
	D_INFO("Setting number of server ranks to %d\n", g_num_serv_ranks);
	/* These two are always set */
	rc = setenv("CRT_PHY_ADDR_STR", info.provider, 1);
	if (rc != 0)
		D_GOTO(cleanup, rc = d_errno2der(errno));

	sprintf(buf, "%d", info.crt_ctx_share_addr);
	rc = setenv("CRT_CTX_SHARE_ADDR", buf, 1);
	if (rc != 0)
		D_GOTO(cleanup, rc = d_errno2der(errno));

	/* If the server has set this, the client must use the same value. */
	if (info.srv_srx_set != -1) {
		sprintf(buf, "%d", info.srv_srx_set);
		rc = setenv("FI_OFI_RXM_USE_SRX", buf, 1);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));
		D_INFO("Using server's value for FI_OFI_RXM_USE_SRX: %s\n",
		       buf);
	} else {
		/* Client may not set it if the server hasn't. */
		cli_srx_set = getenv("FI_OFI_RXM_USE_SRX");
		if (cli_srx_set) {
			D_ERROR("Client set FI_OFI_RXM_USE_SRX to %s, "
				"but server is unset!\n", cli_srx_set);
			D_GOTO(cleanup, rc = -DER_INVAL);
		}
	}

	/* Allow client env overrides for these three */
	crt_timeout = getenv("CRT_TIMEOUT");
	if (!crt_timeout) {
		sprintf(buf, "%d", info.crt_timeout);
		rc = setenv("CRT_TIMEOUT", buf, 1);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));
	} else {
		D_INFO("Using client provided CRT_TIMEOUT: %s\n",
			crt_timeout);
	}

	ofi_interface = getenv("OFI_INTERFACE");
	ofi_domain = getenv("OFI_DOMAIN");
	if (!ofi_interface) {
		rc = setenv("OFI_INTERFACE", info.interface, 1);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));

		/*
		 * If we use the agent as the source, client env shouldn't be allowed to override
		 * the domain. Otherwise we could get a mismatch between interface and domain.
		 */
		if (ofi_domain)
			D_WARN("Ignoring OFI_DOMAIN '%s' because OFI_INTERFACE is not set; using "
			       "automatic configuration instead\n", ofi_domain);

		rc = setenv("OFI_DOMAIN", info.domain, 1);
		if (rc != 0)
			D_GOTO(cleanup, rc = d_errno2der(errno));
	} else {
		D_INFO("Using client provided OFI_INTERFACE: %s\n", ofi_interface);

		/* If the client env didn't provide a domain, we can assume we don't need one. */
		if (ofi_domain)
			D_INFO("Using client provided OFI_DOMAIN: %s\n", ofi_domain);
	}

	D_INFO("Network interface: %s, Domain: %s\n", getenv("OFI_INTERFACE"),
	       getenv("OFI_DOMAIN"));
	D_DEBUG(DB_MGMT,
		"CaRT initialization with:\n"
		"\tCRT_PHY_ADDR_STR: %s, "
		"CRT_CTX_SHARE_ADDR: %s, CRT_TIMEOUT: %s\n",
		getenv("CRT_PHY_ADDR_STR"),
		getenv("CRT_CTX_SHARE_ADDR"), getenv("CRT_TIMEOUT"));

cleanup:
	put_attach_info(&info, resp);

	return rc;
}

int dc_mgmt_net_cfg_check(const char *name)
{
	int rc;
	char *cli_srx_set;
	struct dc_mgmt_sys_info info;
	Mgmt__GetAttachInfoResp *resp;

	/* Query the agent for the CaRT network configuration parameters */
	rc = get_attach_info(name, true /* all_ranks */, &info, &resp);
	if (rc != 0)
		return rc;

	/* Client may not set it if the server hasn't. */
	if (info.srv_srx_set == -1) {
		cli_srx_set = getenv("FI_OFI_RXM_USE_SRX");
		if (cli_srx_set) {
			D_ERROR("Client set FI_OFI_RXM_USE_SRX to %s, "
				"but server is unset!\n", cli_srx_set);
			rc = -DER_INVAL;
			goto out;
		}
	}

out:
	put_attach_info(&info, resp);
	return rc;
}

static int send_monitor_request(struct dc_pool *pool, int request_type)
{
	struct drpc		 *ctx;
	Mgmt__PoolMonitorReq	 req = MGMT__POOL_MONITOR_REQ__INIT;
	uint8_t			 *reqb;
	size_t			 reqb_size;
	char			 pool_uuid[DAOS_UUID_STR_SIZE];
	char			 pool_hdl_uuid[DAOS_UUID_STR_SIZE];
	Drpc__Call		 *dreq;
	Drpc__Response		 *dresp;
	int			 rc;

	/* Connect to daos_agent. */
	D_ASSERT(dc_agent_sockpath != NULL);
	rc = drpc_connect(dc_agent_sockpath, &ctx);
	if (rc != -DER_SUCCESS) {
		D_ERROR("failed to connect to %s " DF_RC "\n",
			dc_agent_sockpath, DP_RC(rc));
		D_GOTO(out, 0);
	}

	uuid_unparse(pool->dp_pool, pool_uuid);
	uuid_unparse(pool->dp_pool_hdl, pool_hdl_uuid);
	req.pooluuid = pool_uuid;
	req.poolhandleuuid = pool_hdl_uuid;
	req.jobid = dc_jobid;
	req.sys = pool->dp_sys->sy_name;

	reqb_size = mgmt__pool_monitor_req__get_packed_size(&req);
	D_ALLOC(reqb, reqb_size);
	if (reqb == NULL) {
		rc = -DER_NOMEM;
		goto out_ctx;
	}
	mgmt__pool_monitor_req__pack(&req, reqb);

	rc = drpc_call_create(ctx, DRPC_MODULE_MGMT, request_type, &dreq);
	if (rc != 0) {
		D_FREE(reqb);
		goto out_ctx;
	}
	dreq->body.len = reqb_size;
	dreq->body.data = reqb;

	/* Make the call and get the response. */
	rc = drpc_call(ctx, R_SYNC, dreq, &dresp);
	if (rc != 0) {
		D_ERROR("Sending monitor request failed: "DF_RC"\n", DP_RC(rc));
		goto out_dreq;
	}
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Monitor Request unsuccessful: %d\n", dresp->status);
		rc = -DER_MISC;
		goto out_dresp;
	}

out_dresp:
	drpc_response_free(dresp);
out_dreq:
	drpc_call_free(dreq);
out_ctx:
	drpc_close(ctx);
out:
	return rc;
}

/*
 * Send an upcall to the agent to notify it of a pool disconnect.
 */
int
dc_mgmt_notify_pool_disconnect(struct dc_pool *pool) {
	return send_monitor_request(pool,
				    DRPC_METHOD_MGMT_NOTIFY_POOL_DISCONNECT);
}

/*
 * Send an upcall to the agent to notify it of a successful pool connect.
 */
int
dc_mgmt_notify_pool_connect(struct dc_pool *pool) {
	return send_monitor_request(pool, DRPC_METHOD_MGMT_NOTIFY_POOL_CONNECT);
}

/*
 * Send an upcall to the agent to notify it of a clean process shutdown.
 */
int
dc_mgmt_notify_exit(void)
{
	struct drpc		 *ctx;
	Drpc__Call		 *dreq;
	Drpc__Response		 *dresp;
	int			 rc;

	D_DEBUG(DB_MGMT, "disconnecting process for pid:%d\n", getpid());

	/* Connect to daos_agent. */
	D_ASSERT(dc_agent_sockpath != NULL);
	rc = drpc_connect(dc_agent_sockpath, &ctx);
	if (rc != -DER_SUCCESS) {
		D_ERROR("failed to connect to %s " DF_RC "\n",
			dc_agent_sockpath, DP_RC(rc));
		if (rc == -DER_NONEXIST)
			rc = -DER_AGENT_COMM;
		D_GOTO(out, rc);
	}

	rc = drpc_call_create(ctx, DRPC_MODULE_MGMT,
			      DRPC_METHOD_MGMT_NOTIFY_EXIT, &dreq);
	if (rc != 0)
		goto out_ctx;

	/* Make the Process Disconnect call and get the response. */
	rc = drpc_call(ctx, R_SYNC, dreq, &dresp);
	if (rc != 0) {
		D_ERROR("Process Disconnect call failed: "DF_RC"\n", DP_RC(rc));
		goto out_dreq;
	}
	if (dresp->status != DRPC__STATUS__SUCCESS) {
		D_ERROR("Process Disconnect unsuccessful: %d\n", dresp->status);
		rc = -DER_MISC;
		goto out_dresp;
	}

out_dresp:
	drpc_response_free(dresp);
out_dreq:
	drpc_call_free(dreq);
out_ctx:
	drpc_close(ctx);
out:
	return rc;
}

struct sys_buf {
	char	syb_name[DAOS_SYS_NAME_MAX + 1];
};

static int
attach_group(const char *name, struct dc_mgmt_sys_info *info,
	     Mgmt__GetAttachInfoResp *resp, crt_group_t **groupp)
{
	crt_group_t    *group;
	int		i;
	int		rc;

	rc = crt_group_view_create((char *)name, &group);
	if (rc != 0) {
		D_ERROR("failed to create group %s: "DF_RC"\n", name,
			DP_RC(rc));
		goto err;
	}

	for (i = 0; i < resp->n_rank_uris; i++) {
		Mgmt__GetAttachInfoResp__RankUri *rank_uri = resp->rank_uris[i];

		rc = crt_group_primary_rank_add(daos_get_crt_ctx(), group,
						rank_uri->rank, rank_uri->uri);
		if (rc != 0) {
			D_ERROR("failed to add rank %u URI %s to group %s: "
				DF_RC"\n", rank_uri->rank, rank_uri->uri, name,
				DP_RC(rc));
			goto err_group;
		}
	}

	*groupp = group;
	return 0;

err_group:
	crt_group_view_destroy(group);
err:
	return rc;
}

static void
detach_group(bool server, crt_group_t *group)
{
	int rc = 0;

	if (!server)
		rc = crt_group_view_destroy(group);
	D_ASSERTF(rc == 0, ""DF_RC"\n", DP_RC(rc));
}

static int
attach(const char *name, struct dc_mgmt_sys **sysp)
{
	struct dc_mgmt_sys	*sys;
	crt_group_t		*group;
	Mgmt__GetAttachInfoResp	*resp;
	int			 rc;

	D_DEBUG(DB_MGMT, "attaching to system '%s'\n", name);

	D_ALLOC_PTR(sys);
	if (sys == NULL) {
		rc = -DER_NOMEM;
		goto err;
	}
	D_INIT_LIST_HEAD(&sys->sy_link);
	rc = snprintf(sys->sy_name, sizeof(sys->sy_name), "%s", name);
	D_ASSERTF(rc >= 0, ""DF_RC"\n", DP_RC(rc));
	if (rc >= sizeof(sys->sy_name)) {
		D_ERROR("system name %s longer than %zu bytes\n", sys->sy_name,
			sizeof(sys->sy_name) - 1);
		rc = -DER_OVERFLOW;
		goto err_sys;
	}

	group = crt_group_lookup((char *)name);
	if (group != NULL) {
		/* This is one of the servers. Skip the get_attach_info call. */
		sys->sy_server = true;
		sys->sy_group = group;
		goto out;
	}

	rc = get_attach_info(name, true /* all_ranks */, &sys->sy_info, &resp);
	if (rc != 0)
		goto err_sys;

	rc = attach_group(name, &sys->sy_info, resp, &sys->sy_group);
	if (rc != 0)
		goto err_info;

	free_get_attach_info_resp(resp);
out:
	*sysp = sys;
	return 0;

err_info:
	put_attach_info(&sys->sy_info, resp);
err_sys:
	D_FREE(sys);
err:
	return rc;
}

static void
detach(struct dc_mgmt_sys *sys)
{
	D_DEBUG(DB_MGMT, "detaching from system '%s'\n", sys->sy_name);
	D_ASSERT(d_list_empty(&sys->sy_link));
	D_ASSERTF(sys->sy_ref == 0, "%d\n", sys->sy_ref);
	detach_group(sys->sy_server, sys->sy_group);
	if (!sys->sy_server)
		put_attach_info(&sys->sy_info, NULL /* resp */);
	D_FREE(sys);
}

static D_LIST_HEAD(systems);
static pthread_mutex_t systems_lock = PTHREAD_MUTEX_INITIALIZER;

static struct dc_mgmt_sys *
lookup_sys(const char *name)
{
	struct dc_mgmt_sys *sys;

	d_list_for_each_entry(sys, &systems, sy_link) {
		if (strcmp(sys->sy_name, name) == 0)
			return sys;
	}
	return NULL;
}

static int
sys_attach(const char *name, struct dc_mgmt_sys **sysp)
{
	struct dc_mgmt_sys     *sys;
	int			rc = 0;

	D_MUTEX_LOCK(&systems_lock);

	sys = lookup_sys(name);
	if (sys != NULL)
		goto ok;

	rc = attach(name, &sys);
	if (rc != 0)
		goto out_lock;

	d_list_add(&sys->sy_link, &systems);

ok:
	sys->sy_ref++;
	*sysp = sys;
out_lock:
	D_MUTEX_UNLOCK(&systems_lock);
	return rc;
}

/**
 * Attach to system \a name.
 *
 * \param[in]		name	system name
 * \param[in,out]	sys	system handle
 */
int
dc_mgmt_sys_attach(const char *name, struct dc_mgmt_sys **sysp)
{
	if (name == NULL)
		name = DAOS_DEFAULT_SYS_NAME;

	return sys_attach(name, sysp);
}

/**
 * Detach from system \a sys.
 *
 * \param[in]	sys	system handle
 */
void
dc_mgmt_sys_detach(struct dc_mgmt_sys *sys)
{
	D_ASSERT(sys != NULL);
	D_MUTEX_LOCK(&systems_lock);
	sys->sy_ref--;
	if (sys->sy_ref == 0) {
		d_list_del_init(&sys->sy_link);
		detach(sys);
	}
	D_MUTEX_UNLOCK(&systems_lock);
}

/**
 * Encode \a sys into \a buf of capacity \a cap. If \a buf is NULL, just return
 * the number of bytes that would be required. If \a buf is not NULL and \a cap
 * is insufficient, return -DER_TRUNC.
 */
ssize_t
dc_mgmt_sys_encode(struct dc_mgmt_sys *sys, void *buf, size_t cap)
{
	struct sys_buf *sysb = buf;
	size_t		len;

	len = sizeof(*sysb);

	if (sysb == NULL)
		return len;

	if (cap < len)
		return -DER_TRUNC;

	D_CASSERT(sizeof(sysb->syb_name) == sizeof(sys->sy_name));
	strncpy(sysb->syb_name, sys->sy_name, sizeof(sysb->syb_name));

	return len;
}

/** Decode \a buf of length \a len. */
ssize_t
dc_mgmt_sys_decode(void *buf, size_t len, struct dc_mgmt_sys **sysp)
{
	struct sys_buf *sysb;

	if (len < sizeof(*sysb)) {
		D_ERROR("truncated sys_buf: %zu < %zu\n", len, sizeof(*sysb));
		return -DER_IO;
	}
	sysb = buf;

	return sys_attach(sysb->syb_name, sysp);
}

/* For a given pool label or UUID, contact mgmt. service to look up its
 * service replica ranks. Note: synchronous RPC with caller already
 * in a task execution context. On successful return, caller is responsible
 * for freeing the d_rank_list_t allocated here. Must not be called by server.
 */
int
dc_mgmt_pool_find(struct dc_mgmt_sys *sys, const char *label, uuid_t puuid,
		  d_rank_list_t **svcranksp)
{
	d_rank_list_t		       *ms_ranks;
	crt_endpoint_t			srv_ep;
	crt_rpc_t		       *rpc = NULL;
	struct mgmt_pool_find_in       *rpc_in;
	struct mgmt_pool_find_out      *rpc_out = NULL;
	crt_opcode_t			opc;
	int				i;
	int				idx;
	crt_context_t			ctx;
	uuid_t				null_uuid;
	bool				success = false;
	int				rc = 0;

	D_ASSERT(sys->sy_server == 0);
	uuid_clear(null_uuid);

	/* NB: ms_ranks may have multiple entries even for single MS replica,
	 * since there may be multiple engines there. Some of which may have
	 * been stopped or faulted. May need to contact multiple engines.
	 * Assumed: any MS replica engine can be contacted, even non-leaders.
	 */
	ms_ranks = sys->sy_info.ms_ranks;
	D_ASSERT(ms_ranks->rl_nr > 0);
	idx = d_rand() % ms_ranks->rl_nr;
	ctx = daos_get_crt_ctx();
	opc = DAOS_RPC_OPCODE(MGMT_POOL_FIND, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);

	srv_ep.ep_grp = sys->sy_group;
	srv_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	for (i = 0 ; i < ms_ranks->rl_nr; i++) {
		uint32_t	timeout;

		srv_ep.ep_rank = ms_ranks->rl_ranks[idx];
		rpc = NULL;
		rc = crt_req_create(ctx, &srv_ep, opc, &rpc);
		if (rc != 0) {
			D_ERROR("crt_req_create() failed, "DF_RC"\n",
				DP_RC(rc));
			idx = (idx + 1) % ms_ranks->rl_nr;
			continue;
		}

		/* Shorten the timeout (but not lower than 10 seconds) to speed up pool find */
		rc = crt_req_get_timeout(rpc, &timeout);
		D_ASSERTF(rc == 0, "crt_req_get_timeout: "DF_RC"\n", DP_RC(rc));
		rc = crt_req_set_timeout(rpc, max(10, timeout / 4));
		D_ASSERTF(rc == 0, "crt_req_set_timeout: "DF_RC"\n", DP_RC(rc));

		rpc_in = NULL;
		rpc_in = crt_req_get(rpc);
		D_ASSERT(rpc_in != NULL);
		if (label) {
			rpc_in->pfi_bylabel = 1;
			rpc_in->pfi_label = label;
			uuid_copy(rpc_in->pfi_puuid, null_uuid);
			D_DEBUG(DB_MGMT, "%s: ask rank %u for replicas\n",
				label, srv_ep.ep_rank);
		} else {
			rpc_in->pfi_bylabel = 0;
			rpc_in->pfi_label = MGMT_POOL_FIND_DUMMY_LABEL;
			uuid_copy(rpc_in->pfi_puuid, puuid);
			D_DEBUG(DB_MGMT, DF_UUID": ask rank %u for replicas\n",
				DP_UUID(puuid), srv_ep.ep_rank);
		}

		crt_req_addref(rpc);
		rc = daos_rpc_send_wait(rpc);
		if (rc != 0) {
			D_DEBUG(DB_MGMT, "daos_rpc_send_wait() failed, "
				DF_RC "\n", DP_RC(rc));
			crt_req_decref(rpc);
			idx = (idx + 1) % ms_ranks->rl_nr;
			success = false;
			continue;
		}

		success = true; /* The RPC invocation succeeded. */

		/* Special case: Unpack the response and check for a
		 * -DER_NONEXIST from the upcall handler; in which
		 * case we should retry with another replica.
		 */
		rpc_out = crt_reply_get(rpc);
		D_ASSERT(rpc_out != NULL);
		if (rpc_out->pfo_rc == -DER_NONEXIST) {
			/* This MS replica may have a stale copy of the DB. */
			if (label) {
				D_DEBUG(DB_MGMT, "%s: pool not found on rank %u\n",
					label, srv_ep.ep_rank);
			} else {
				D_DEBUG(DB_MGMT, DF_UUID": pool not found on rank %u\n",
					DP_UUID(puuid), srv_ep.ep_rank);
			}
			if (i + 1 < ms_ranks->rl_nr) {
				crt_req_decref(rpc);
				idx = (idx + 1) % ms_ranks->rl_nr;
			}
			continue;
		}

		break;
	}

	if (!success) {
		if (label)
			D_ERROR("%s: failed to get PS replicas from %d servers"
				", "DF_RC"\n", label, ms_ranks->rl_nr,
				DP_RC(rc));
		else
			D_ERROR(DF_UUID": failed to get PS replicas from %d "
				"servers, "DF_RC"\n", DP_UUID(puuid),
				ms_ranks->rl_nr, DP_RC(rc));
		return rc;
	}

	D_ASSERT(rpc_out != NULL);
	rc = rpc_out->pfo_rc;
	if (rc != 0) {
		if (label) {
			D_CDEBUG(rc == -DER_NONEXIST, DB_MGMT, DLOG_ERR,
				 "%s: MGMT_POOL_FIND rpc failed to %d ranks, " DF_RC "\n", label,
				 ms_ranks->rl_nr, DP_RC(rc));
		} else {
			D_ERROR(DF_UUID ": MGMT_POOL_FIND rpc failed to %d "
					"ranks, " DF_RC "\n",
				DP_UUID(puuid), ms_ranks->rl_nr, DP_RC(rc));
		}
		goto decref;
	}
	if (label)
		uuid_copy(puuid, rpc_out->pfo_puuid);

	rc = d_rank_list_dup(svcranksp, rpc_out->pfo_ranks);
	if (rc != 0) {
		D_ERROR("d_rank_list_dup() failed, " DF_RC "\n", DP_RC(rc));
		goto decref;
	}

	D_DEBUG(DB_MGMT, "rank %u returned pool "DF_UUID"\n",
		srv_ep.ep_rank, DP_UUID(rpc_out->pfo_puuid));

decref:
	crt_req_decref(rpc);
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
		D_ERROR("failed to register mgmt RPCs: "DF_RC"\n", DP_RC(rc));

	return rc;
}

/**
 * Finalize management interface
 */
void
dc_mgmt_fini()
{
	int rc;

	rc = daos_rpc_unregister(&mgmt_proto_fmt);
	if (rc != 0)
		D_ERROR("failed to unregister mgmt RPCs: "DF_RC"\n", DP_RC(rc));
}

int dc2_mgmt_svc_rip(tse_task_t *task)
{
	return -DER_NOSYS;
}
