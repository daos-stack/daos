/*
 * (C) Copyright 2016-2020 Intel Corporation.
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
 * \file
 *
 * Pool create/destroy methods
 */

#define D_LOGFAC	DD_FAC(mgmt)

#include <daos/mgmt.h>
#include <daos/event.h>
#include <daos/rsvc.h>
#include <daos_api.h>
#include <daos_security.h>
#include "rpc.h"

static int
mgmt_rsvc_client_complete_rpc(struct rsvc_client *client, crt_endpoint_t *ep,
			      int rc_crt, int rc_svc, struct rsvc_hint *hint,
			      tse_task_t *task)
{
	int rc;

	rc = rsvc_client_complete_rpc(client, ep, rc_crt, rc_svc, hint);
	if (rc == RSVC_CLIENT_RECHOOSE ||
	    (rc == RSVC_CLIENT_PROCEED && daos_rpc_retryable_rc(rc_svc))) {
		rc = tse_task_reinit(task);
		if (rc != 0)
			return rc;
		return RSVC_CLIENT_RECHOOSE;
	}
	return RSVC_CLIENT_PROCEED;
}

struct pool_create_state {
	struct rsvc_client	client;
	daos_prop_t	       *prop;
	struct dc_mgmt_sys     *sys;
	crt_rpc_t	       *rpc;
};

static int
pool_create_cp(tse_task_t *task, void *data)
{
	daos_pool_create_t		*args = dc_task_get_args(task);
	struct pool_create_state	*state = dc_task_get_priv(task);
	d_rank_list_t			*svc = args->svc;
	struct mgmt_pool_create_out	*pc_out = crt_reply_get(state->rpc);
	bool				 free_state = true;
	int				 rc;

	rc = mgmt_rsvc_client_complete_rpc(&state->client, &state->rpc->cr_ep,
					   task->dt_result, pc_out->pc_rc,
					   NULL /* hint */, task);
	if (rc < 0) {
		goto out;
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		free_state = false;
		rc = 0;
		goto out;
	}

	rc = pc_out->pc_rc;
	if (rc) {
		D_ERROR("MGMT_POOL_CREATE replied failed, rc: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

	/*
	 * Report the actual list of pool service replicas. Don't use
	 * daos_rank_list_copy, since svc->rl_ranks is from the user and may be
	 * unreallocable.
	 */
	if (pc_out->pc_svc->rl_nr > svc->rl_nr) {
		D_ERROR("more pool service replicas created (%u) than "
			"requested (%u)\n", pc_out->pc_svc->rl_nr, svc->rl_nr);
		rc = -DER_PROTO;
		goto out;
	}
	svc->rl_nr = pc_out->pc_svc->rl_nr;
	memcpy(svc->rl_ranks, pc_out->pc_svc->rl_ranks,
	       sizeof(*svc->rl_ranks) * svc->rl_nr);

out:
	crt_req_decref(state->rpc);
	state->rpc = NULL;
	if (free_state) {
		rsvc_client_fini(&state->client);
		dc_mgmt_sys_detach(state->sys);
		daos_prop_free(state->prop);
		D_FREE(state);
	}
	return rc;
}

static bool
daos_prop_has_entry(daos_prop_t *prop, uint32_t entry_type)
{
	return (prop != NULL) &&
	       (daos_prop_entry_get(prop, entry_type) != NULL);
}

/*
 * Translates uid/gid to user and group name, and adds them as owners to a new
 * copy of the daos_prop_t passed in.
 * The newly allocated prop is expected to be freed by the pool create callback.
 */
static int
add_ownership_props(daos_prop_t **prop_out, daos_prop_t *prop_in,
		    uid_t uid, gid_t gid)
{
	char	       *owner = NULL;
	char	       *owner_grp = NULL;
	daos_prop_t    *final_prop = NULL;
	uint32_t	idx = 0;
	uint32_t	entries;
	int		rc = 0;

	entries = (prop_in == NULL) ? 0 : prop_in->dpp_nr;

	/*
	 * TODO: remove uid/gid input params and use euid/egid instead
	 */
	if (!daos_prop_has_entry(prop_in, DAOS_PROP_PO_OWNER)) {
		rc = daos_acl_uid_to_principal(uid, &owner);
		if (rc) {
			D_ERROR("Invalid uid\n");
			D_GOTO(err_out, rc);
		}

		entries++;
	}

	if (!daos_prop_has_entry(prop_in, DAOS_PROP_PO_OWNER_GROUP)) {
		rc = daos_acl_gid_to_principal(gid, &owner_grp);
		if (rc) {
			D_ERROR("Invalid gid\n");
			D_GOTO(err_out, rc);
		}

		entries++;
	}

	/* We always free this prop in the callback - so need to make a copy */
	final_prop = daos_prop_alloc(entries);
	if (final_prop == NULL) {
		D_ERROR("failed to allocate props");
		D_GOTO(err_out, -DER_NOMEM);
	}

	if (prop_in != NULL) {
		rc = daos_prop_copy(final_prop, prop_in);
		if (rc)
			D_GOTO(err_out, rc);
		idx = prop_in->dpp_nr;
	}

	if (prop_in == NULL || entries > prop_in->dpp_nr) {
		if (owner != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_PO_OWNER;
			final_prop->dpp_entries[idx].dpe_str = owner;
			owner = NULL; /* prop is responsible for it now */
			idx++;
		}

		if (owner_grp != NULL) {
			final_prop->dpp_entries[idx].dpe_type =
				DAOS_PROP_PO_OWNER_GROUP;
			final_prop->dpp_entries[idx].dpe_str = owner_grp;
			owner_grp = NULL; /* prop is responsible for it now */
			idx++;
		}

	}

	*prop_out = final_prop;

	return rc;

err_out:
	daos_prop_free(final_prop);
	D_FREE(owner);
	D_FREE(owner_grp);
	return rc;
}

int
dc_pool_create(tse_task_t *task)
{
	daos_pool_create_t	       *args = dc_task_get_args(task);
	struct pool_create_state       *state = dc_task_get_priv(task);
	crt_endpoint_t			svr_ep;
	crt_rpc_t		       *rpc_req = NULL;
	crt_opcode_t			opc;
	struct mgmt_pool_create_in     *pc_in;
	int				rc;

	if (state == NULL) {
		d_rank_list_t	ranks;
		d_rank_t	rank;

		if (args->uuid == NULL || args->dev == NULL ||
		    strlen(args->dev) == 0) {
			D_ERROR("Invalid parameter of dev (NULL or empty "
				"string)\n");
			rc = -DER_INVAL;
			goto out;
		}

		uuid_generate(args->uuid);

		D_ALLOC_PTR(state);
		if (state == NULL) {
			D_ERROR("failed to allocate state\n");
			rc = -DER_NOMEM;
			goto out;
		}

		rc = add_ownership_props(&state->prop, args->prop, args->uid,
					 args->gid);
		if (rc != 0)
			goto out_state;

		rc = dc_mgmt_sys_attach(args->grp, &state->sys);
		if (rc != 0)
			goto out_prop;

		rank = 0;
		ranks.rl_ranks = &rank;
		ranks.rl_nr = 1;
		rc = rsvc_client_init(&state->client, &ranks);
		if (rc != 0) {
			D_ERROR("failed to initialize rsvc_client %d\n", rc);
			goto out_grp;
		}

		daos_task_set_priv(task, state);
	}

	svr_ep.ep_grp = state->sys->sy_group;
	rc = rsvc_client_choose(&state->client, &svr_ep);
	if (rc != 0) {
		D_ERROR("%s: cannot find management service: "DF_RC"\n",
			args->grp, DP_RC(rc));
		goto out_client;
	}
	opc = DAOS_RPC_OPCODE(MGMT_POOL_CREATE, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_POOL_CREATE) failed, rc: %d.\n",
			rc);
		D_GOTO(out_client, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

	/** fill in request buffer */
	uuid_copy(pc_in->pc_pool_uuid, args->uuid);
	pc_in->pc_grp = (d_string_t)args->grp;
	pc_in->pc_tgt_dev = (d_string_t)args->dev;
	pc_in->pc_tgts = (d_rank_list_t *)args->tgts;
	pc_in->pc_scm_size = args->scm_size;
	pc_in->pc_nvme_size = args->nvme_size;
	pc_in->pc_prop = state->prop;
	pc_in->pc_svc_nr = args->svc->rl_nr;

	crt_req_addref(rpc_req);
	state->rpc = rpc_req;

	rc = tse_task_register_comp_cb(task, pool_create_cp, NULL, 0);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DB_MGMT, DF_UUID": creating pool\n", DP_UUID(args->uuid));

	/** send the request */
	return daos_rpc_send(rpc_req, task);

out_put_req:
	crt_req_decref(state->rpc);
	crt_req_decref(rpc_req);
out_client:
	rsvc_client_fini(&state->client);
out_grp:
	dc_mgmt_sys_detach(state->sys);
out_prop:
	daos_prop_free(state->prop);
out_state:
	D_FREE(state);
out:
	tse_task_complete(task, rc);
	return rc;
}

struct pool_destroy_state {
	struct rsvc_client	client;
	struct dc_mgmt_sys     *sys;
	crt_rpc_t	       *rpc;
};

static int
pool_destroy_cp(tse_task_t *task, void *data)
{
	struct pool_destroy_state	*state = dc_task_get_priv(task);
	struct mgmt_pool_destroy_out	*pd_out = crt_reply_get(state->rpc);
	bool				 free_state = true;
	int				 rc;

	/*
	 * Work around pool destroy issues after killing servers by not
	 * retrying for now.
	 */
	if (task->dt_result == 0 &&
	    (daos_crt_network_error(pd_out->pd_rc) ||
	     pd_out->pd_rc == -DER_TIMEDOUT))
		goto proceed;

	rc = mgmt_rsvc_client_complete_rpc(&state->client, &state->rpc->cr_ep,
					   task->dt_result, pd_out->pd_rc,
					   NULL /* hint */, task);
	if (rc < 0) {
		goto out;
	} else if (rc == RSVC_CLIENT_RECHOOSE) {
		free_state = false;
		rc = 0;
		goto out;
	}

proceed:
	rc = pd_out->pd_rc;
	if (rc) {
		D_ERROR("MGMT_POOL_DESTROY replied failed, rc: "DF_RC"\n",
			DP_RC(rc));
		goto out;
	}

out:
	crt_req_decref(state->rpc);
	state->rpc = NULL;
	if (free_state) {
		rsvc_client_fini(&state->client);
		dc_mgmt_sys_detach(state->sys);
		D_FREE(state);
	}
	return rc;
}

int
dc_pool_destroy(tse_task_t *task)
{
	daos_pool_destroy_t		*args = dc_task_get_args(task);
	struct pool_destroy_state	*state = dc_task_get_priv(task);
	crt_endpoint_t			 svr_ep;
	crt_rpc_t			*rpc_req;
	crt_opcode_t			 opc;
	struct mgmt_pool_destroy_in	*pd_in;
	int				 rc;

	if (state == NULL) {
		d_rank_list_t	ranks;
		d_rank_t	rank;

		if (!daos_uuid_valid(args->uuid)) {
			D_ERROR("Invalid parameter of uuid (NULL).\n");
			rc = -DER_INVAL;
			goto out;
		}

		D_ALLOC_PTR(state);
		if (state == NULL) {
			D_ERROR("failed to allocate state\n");
			rc = -DER_NOMEM;
			goto out;
		}

		rc = dc_mgmt_sys_attach(args->grp, &state->sys);
		if (rc != 0)
			goto out_state;

		rank = 0;
		ranks.rl_ranks = &rank;
		ranks.rl_nr = 1;
		rc = rsvc_client_init(&state->client, &ranks);
		if (rc != 0) {
			D_ERROR("failed to initialize rsvc_client %d\n", rc);
			goto out_group;
		}

		daos_task_set_priv(task, state);
	}

	svr_ep.ep_grp = state->sys->sy_group;
	rc = rsvc_client_choose(&state->client, &svr_ep);
	if (rc != 0) {
		D_ERROR("%s: cannot find management service: "DF_RC"\n",
			args->grp, DP_RC(rc));
		goto out_client;
	}
	opc = DAOS_RPC_OPCODE(MGMT_POOL_DESTROY, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_POOL_DESTROY) failed, rc: %d.\n",
			rc);
		D_GOTO(out_client, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pd_in = crt_req_get(rpc_req);
	D_ASSERT(pd_in != NULL);

	/** fill in request buffer */
	uuid_copy(pd_in->pd_pool_uuid, args->uuid);
	pd_in->pd_grp = (d_string_t)args->grp;
	pd_in->pd_force = (args->force == 0) ? false : true;

	crt_req_addref(rpc_req);
	state->rpc = rpc_req;

	rc = tse_task_register_comp_cb(task, pool_destroy_cp, NULL, 0);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DB_MGMT, DF_UUID": destroying pool\n", DP_UUID(args->uuid));

	/** send the request */
	return daos_rpc_send(rpc_req, task);

out_put_req:
	crt_req_decref(state->rpc);
	crt_req_decref(rpc_req);
out_client:
	rsvc_client_fini(&state->client);
out_group:
	dc_mgmt_sys_detach(state->sys);
out_state:
	D_FREE(state);
out:
	tse_task_complete(task, rc);
	return rc;
}

struct mgmt_list_pools_arg {
	struct dc_mgmt_sys     *sys;
	crt_rpc_t	       *rpc;
	daos_mgmt_pool_info_t  *pools;
	daos_size_t		req_npools;
	daos_size_t	       *npools;
};

static int
mgmt_list_pools_cp(tse_task_t *task, void *data)
{
	struct mgmt_list_pools_arg	*arg;
	struct mgmt_list_pools_out	*pc_out;
	struct mgmt_list_pools_in	*pc_in;
	uint64_t			 pidx;
	int				 rc = task->dt_result;

	arg = (struct mgmt_list_pools_arg *)data;

	if (rc) {
		D_ERROR("RPC error while listing pools: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	pc_out = crt_reply_get(arg->rpc);
	D_ASSERT(pc_out != NULL);
	rc = pc_out->lp_rc;
	*arg->npools = pc_out->lp_npools;
	if (rc) {
		D_ERROR("MGMT_LIST_POOLS replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	pc_in = crt_req_get(arg->rpc);
	D_ASSERT(pc_in != NULL);

	/* copy RPC response pools info to client buffer, if provided */
	if (arg->pools) {
		/* Response ca_count expected <= client-specified npools */
		for (pidx = 0; pidx < pc_out->lp_pools.ca_count; pidx++) {
			struct mgmt_list_pools_one	*rpc_pool =
					&pc_out->lp_pools.ca_arrays[pidx];
			daos_mgmt_pool_info_t		*cli_pool =
					&arg->pools[pidx];

			uuid_copy(cli_pool->mgpi_uuid, rpc_pool->lp_puuid);

			/* allocate rank list for caller (simplifies API) */
			rc = d_rank_list_dup(&cli_pool->mgpi_svc,
					     rpc_pool->lp_svc);
			if (rc) {
				D_ERROR("Copy RPC reply svc list failed\n");
				D_GOTO(out_free_svcranks, rc = -DER_NOMEM);
			}
		}
	}

out_free_svcranks:
	if (arg->pools && (rc != 0)) {
		for (pidx = 0; pidx < pc_out->lp_pools.ca_count; pidx++) {
			daos_mgmt_pool_info_t *pool = &arg->pools[pidx];

			if (pool->mgpi_svc)
				d_rank_list_free(pool->mgpi_svc);
		}
	}

out:
	dc_mgmt_sys_detach(arg->sys);
	crt_req_decref(arg->rpc);

	return rc;
}

int
dc_mgmt_list_pools(tse_task_t *task)
{
	daos_mgmt_list_pools_t	        *args;
	crt_endpoint_t			svr_ep;
	crt_rpc_t		       *rpc_req = NULL;
	crt_opcode_t			opc;
	struct mgmt_list_pools_in      *pc_in;
	struct mgmt_list_pools_arg	cb_args;
	int				rc = 0;

	args = dc_task_get_args(task);

	D_ERROR("dc_mgmt_sys_attach\n");
	rc = dc_mgmt_sys_attach(args->grp, &cb_args.sys);
	if (rc != 0) {
		D_ERROR("cannot attach to DAOS system: %s\n", args->grp);
		D_GOTO(out, rc);
	}

	svr_ep.ep_grp = cb_args.sys->sy_group;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_LIST_POOLS, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);

	D_ERROR("crt_req_create\n");
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_LIST_POOLS failed, rc: %d.\n",
			rc);
		D_GOTO(out_grp, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

	/** fill in request buffer */
	pc_in->lp_grp = (d_string_t)args->grp;
	/* If provided pools is NULL, caller needs the number of pools
	 * to be returned in npools. Set npools=0 in the request in this case
	 * (caller value may be uninitialized).
	 */
	if (args->pools == NULL)
		pc_in->lp_npools = 0;
	else
		pc_in->lp_npools = *args->npools;

	D_DEBUG(DF_DSMC, "req_npools="DF_U64" (pools=%p, *npools="DF_U64"\n",
			 pc_in->lp_npools, args->pools,
			 *args->npools);

	crt_req_addref(rpc_req);
	cb_args.rpc = rpc_req;
	cb_args.npools = args->npools;
	cb_args.pools = args->pools;
	cb_args.req_npools = pc_in->lp_npools;

	D_ERROR("tse_task_register_comp_cb\n");
	rc = tse_task_register_comp_cb(task, mgmt_list_pools_cp, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_put_req, rc);
	D_ERROR("retrieving list of pools in DAOS system: %s\n", args->grp);
	D_DEBUG(DB_MGMT, "retrieving list of pools in DAOS system: %s\n",
		args->grp);

	/** send the request */
	D_ERROR("daos_rpc_send\n");
	return daos_rpc_send(rpc_req, task);

out_put_req:
	crt_req_decref(rpc_req);
	crt_req_decref(rpc_req);

out_grp:
	dc_mgmt_sys_detach(cb_args.sys);
out:
	tse_task_complete(task, rc);
	return rc;
}

struct mgmt_list_devs_arg {
	struct dc_mgmt_sys     *sys;
	crt_rpc_t	       *rpc;
	daos_mgmt_dev_info_t  *devs;
	daos_size_t		req_ndevs;
	daos_size_t	       *ndevs;
};

static int
mgmt_list_devs_cp(tse_task_t *task, void *data)
{
	struct mgmt_list_devs_arg	*arg;
	struct mgmt_list_devs_out	*pc_out;
	struct mgmt_list_devs_in	*pc_in;
	uint64_t			 didx;
	int				 rc = task->dt_result;

	arg = (struct mgmt_list_devs_arg *)data;

	if (rc) {
		D_ERROR("RPC error while listing devices: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	pc_out = crt_reply_get(arg->rpc);
	D_ASSERT(pc_out != NULL);
	rc = pc_out->ld_rc;
	*arg->ndevs = pc_out->ld_ndevs;
	if (rc) {
		D_ERROR("MGMT_LIST_DEVS replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	pc_in = crt_req_get(arg->rpc);
	D_ASSERT(pc_in != NULL);

	/* copy RPC response pools info to client buffer, if provided */
	if (arg->devs) {
		/* Response ca_count expected <= client-specified npools */
		for (didx = 0; didx < pc_out->ld_devices.ca_count; didx++) {
			struct mgmt_list_devs_one	*rpc_dev =
					&pc_out->ld_devices.ca_arrays[didx];
			daos_mgmt_dev_info_t		*cli_dev =
					&arg->devs[didx];

			uuid_copy(cli_dev->mgdi_uuid, rpc_dev->ld_devuuid);
		}
	}

out:
	dc_mgmt_sys_detach(arg->sys);
	crt_req_decref(arg->rpc);

	return rc;
}

int
dc_mgmt_smd_list_all_devs(tse_task_t *task)
{
	daos_mgmt_list_devs_t	        *args;
	crt_endpoint_t			svr_ep;
	crt_rpc_t		       *rpc_req = NULL;
	crt_opcode_t			opc;
	struct mgmt_list_devs_in	*pc_in;
	struct mgmt_list_devs_arg	cb_args;
	int				rc = 0;

	args = dc_task_get_args(task);

	D_ERROR("dc_mgmt_sys_attach\n");
	rc = dc_mgmt_sys_attach(args->grp, &cb_args.sys);
	if (rc != 0) {
		D_ERROR("cannot attach to DAOS system: %s\n", args->grp);
		D_GOTO(out, rc);
	}

	svr_ep.ep_grp = cb_args.sys->sy_group;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = daos_rpc_tag(DAOS_REQ_MGMT, 0);
	opc = DAOS_RPC_OPCODE(MGMT_LIST_DEVS, DAOS_MGMT_MODULE,
			      DAOS_MGMT_VERSION);

	D_ERROR("crt_req_create\n");
	rc = crt_req_create(daos_task2ctx(task), &svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(MGMT_LIST_POOLS failed, rc: %d.\n",
			rc);
		D_GOTO(out_grp, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

//	/** fill in request buffer */
//	pc_in->lp_grp = (d_string_t)args->grp;
//	/* If provided pools is NULL, caller needs the number of pools
//	 * to be returned in npools. Set npools=0 in the request in this case
//	 * (caller value may be uninitialized).
//	 */
//	if (args->pools == NULL)
//		pc_in->lp_npools = 0;
//	else
//		pc_in->lp_npools = *args->npools;

//	D_DEBUG(DF_DSMC, "req_npools="DF_U64" (pools=%p, *npools="DF_U64"\n",
//			 pc_in->lp_npools, args->pools,
//			 *args->npools);

	crt_req_addref(rpc_req);
	cb_args.rpc = rpc_req;
	cb_args.ndevs = args->ndevs;
	cb_args.devs = args->devs;
	cb_args.req_ndevs = pc_in->ld_ndevs;

	D_ERROR("tse_task_register_comp_c\n");
	rc = tse_task_register_comp_cb(task, mgmt_list_devs_cp, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DB_MGMT, "retrieving list of SMD devices in DAOS system: %s\n",
		args->grp);

	D_ERROR("daos_rpc_send\n");
	/** send the request */
	return daos_rpc_send(rpc_req, task);

out_put_req:
	crt_req_decref(rpc_req);
	crt_req_decref(rpc_req);

out_grp:
	dc_mgmt_sys_detach(cb_args.sys);
out:
	tse_task_complete(task, rc);
	return rc;
}
