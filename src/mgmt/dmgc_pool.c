/**
 * (C) Copyright 2016 Intel Corporation.
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
 * dmgc: Pool Methods
 */

#include "dmg_rpc.h"

static int
pool_create_cp(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp		*sp = arg;
	crt_rank_list_t		*svc = (crt_rank_list_t *)sp->sp_arg;
	struct dmg_pool_create_out	*pc_out;

	if (rc) {
		D_ERROR("RPC error while disconnecting from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pc_out = crt_reply_get(sp->sp_rpc);
	rc = pc_out->pc_rc;
	if (rc) {
		D_ERROR("DMG_POOL_CREATE replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	/** report list of targets running the metadata service */
	crt_rank_list_copy(svc, pc_out->pc_svc, false);
out:
	crt_req_decref(sp->sp_rpc);
	return rc;
}

int
daos_pool_create(unsigned int mode, unsigned int uid, unsigned int gid,
		 const char *grp, const crt_rank_list_t *tgts, const char *dev,
		 crt_size_t size, crt_rank_list_t *svc, uuid_t uuid,
		 daos_event_t *ev)
{
	crt_endpoint_t			 svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_opcode_t			 opc;
	struct dmg_pool_create_in	*pc_in;
	struct daos_op_sp		*sp;
	int				 rc = 0;

	if (grp == NULL || strlen(grp) == 0) {
		D_ERROR("Invalid parameter of grp (NULL or empty string).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (dev == NULL || strlen(dev) == 0) {
		D_ERROR("Invalid parameter of dev (NULL or empty string).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	uuid_generate(uuid);

	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(DMG_POOL_CREATE, DAOS_MGMT_MODULE, 1);
	rc = crt_req_create(daos_ev2ctx(ev), svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(DMG_POOL_CREATE) failed, rc: %d.\n",
			rc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = crt_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

	/** fill in request buffer */
	uuid_copy(pc_in->pc_pool_uuid, uuid);
	pc_in->pc_mode = mode;
	pc_in->pc_uid = uid;
	pc_in->pc_gid = gid;
	pc_in->pc_grp = (crt_string_t)grp;
	pc_in->pc_tgt_dev = (crt_string_t)dev;
	pc_in->pc_tgts = (crt_rank_list_t *)tgts;
	pc_in->pc_tgt_size = size;
	pc_in->pc_svc_nr = svc->rl_nr.num;

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	crt_req_addref(rpc_req); /** for scratchpad */
	sp->sp_rpc = rpc_req;
	sp->sp_arg = svc;

	rc = daos_event_register_comp_cb(ev, pool_create_cp, sp);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DF_MGMT, DF_UUID": creating pool\n", CP_UUID(uuid));

	/** send the request */
	rc = daos_rpc_send(rpc_req, ev);
out:
	return rc;

out_put_req:
	/** dec ref taken for scratchpad */
	crt_req_decref(rpc_req);
	/** dec ref taken for crt_req_create */
	crt_req_decref(rpc_req);
	D_GOTO(out, rc);
}

static int
pool_destroy_cp(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp *sp = arg;
	struct dmg_pool_destroy_out	*pd_out;

	if (rc) {
		D_ERROR("RPC error while destroying pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pd_out = crt_reply_get(sp->sp_rpc);
	rc = pd_out->pd_rc;
	if (rc) {
		D_ERROR("DMG_POOL_DESTROY replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

out:
	crt_req_decref(sp->sp_rpc);
	return rc;
}

int
daos_pool_destroy(const uuid_t uuid, const char *grp, int force,
		  daos_event_t *ev)
{
	crt_endpoint_t			 svr_ep;
	crt_rpc_t			*rpc_req = NULL;
	crt_opcode_t			 opc;
	struct dmg_pool_destroy_in	*pd_in;
	struct daos_op_sp		*sp;
	int				 rc = 0;

	if (uuid_is_null(uuid)) {
		D_ERROR("Invalid parameter of uuid (NULL).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (grp == NULL || strlen(grp) == 0) {
		D_ERROR("Invalid parameter of grp (NULL or empty string).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	svr_ep.ep_grp = NULL;
	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(DMG_POOL_DESTROY, DAOS_MGMT_MODULE, 1);
	rc = crt_req_create(daos_ev2ctx(ev), svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("crt_req_create(DMG_POOL_DESTROY) failed, rc: %d.\n",
			rc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pd_in = crt_req_get(rpc_req);
	D_ASSERT(pd_in != NULL);

	/** fill in request buffer */
	uuid_copy(pd_in->pd_pool_uuid, uuid);
	pd_in->pd_grp = (crt_string_t)grp;
	pd_in->pd_force = (force == 0) ? false : true;

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	crt_req_addref(rpc_req); /** for scratchpad */
	sp->sp_rpc = rpc_req;

	rc = daos_event_register_comp_cb(ev, pool_destroy_cp,
					 sp);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(out_put_req, rc);

	D_DEBUG(DF_MGMT, DF_UUID": destroying pool\n", CP_UUID(uuid));

	/** send the request */
	rc = daos_rpc_send(rpc_req, ev);
out:
	return rc;

out_put_req:
	/** dec ref taken for scratchpad */
	crt_req_decref(rpc_req);
	/** dec ref taken for crt_req_create */
	crt_req_decref(rpc_req);
	D_GOTO(out, rc);
}
