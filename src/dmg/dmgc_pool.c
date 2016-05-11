/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dmgc: Pool Methods
 */

#include "dmgc_internal.h"

static int
pool_create_cp(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{

	daos_rank_list_t		*svc = (daos_rank_list_t *)sp->sp_arg;
	struct dmg_pool_create_out	*pc_out;

	if (rc) {
		D_ERROR("RPC error while disconnecting from pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	pc_out = dtp_reply_get(sp->sp_rpc);
	rc = pc_out->pc_rc;
	if (rc) {
		D_ERROR("DMG_POOL_CREATE replied failed, rc: %d\n", rc);
		D_GOTO(out, rc);
	}

	/** report list of targets running the metadata service */
	daos_rank_list_copy(svc, pc_out->pc_svc, false);
out:
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dmg_pool_create(unsigned int mode, const char *grp,
		const daos_rank_list_t *tgts, const char *dev, daos_size_t size,
		daos_rank_list_t *svc, uuid_t uuid, daos_event_t *ev)
{
	dtp_endpoint_t			 svr_ep;
	dtp_rpc_t			*rpc_req = NULL;
	dtp_opcode_t			 opc;
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
	if (size == 0) {
		D_ERROR("Invalid parameter of size (0).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (dmg_initialized() == false) {
		D_ERROR("dmg client library un-initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc)
			return rc;
	}

	uuid_generate(uuid);

	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	opc = DAOS_RPC_OPCODE(DMG_POOL_CREATE, DAOS_DMG_MODULE, 1);
	rc = dtp_req_create(daos_ev2ctx(ev), svr_ep, opc, &rpc_req);
	if (rc != 0) {
		D_ERROR("dtp_req_create(DMG_POOL_CREATE) failed, rc: %d.\n",
			rc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = dtp_req_get(rpc_req);
	D_ASSERT(pc_in != NULL);

	/** fill in request buffer */
	uuid_copy(pc_in->pc_uuid, uuid);
	pc_in->pc_mode = mode;
	pc_in->pc_grp = strdup(grp);
	if (pc_in->pc_grp == NULL) {
		D_ERROR("strdup(grp) failed.\n");
		D_GOTO(out_err_req_created, rc = -DER_NOMEM);
	}
	pc_in->pc_tgt_dev = strdup(dev);
	if (pc_in->pc_tgt_dev == NULL) {
		D_ERROR("strdup(dev) failed.\n");
		D_GOTO(out_err_grp_dupped, rc = -DER_NOMEM);
	}
	rc = daos_rank_list_dup(&pc_in->pc_tgts, tgts, true);
	if (rc != 0) {
		D_ERROR("daos_rank_list_dup failed, rc: %d.\n", rc);
		D_GOTO(out_err_tgt_dev_dupped, rc);
	}
	pc_in->pc_tgt_size = size;
	rc = daos_rank_list_dup(&pc_in->pc_svc, svc, true);
	if (rc != 0) {
		D_ERROR("daos_rank_list_dup failed, rc: %d.\n", rc);
		D_GOTO(out_err_tgts_dupped, rc);
	}

	/** fill in scratchpad associated with the event */
	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc_req); /** for scratchpad */
	sp->sp_rpc = rpc_req;
	sp->sp_arg = svc;

	rc = daos_event_launch(ev, NULL, pool_create_cp);
	if (rc)
		D_GOTO(out_err_svc_dupped, rc);

	/** send the request */
	rc = daos_rpc_send(rpc_req, ev);
	D_GOTO(out, rc);

out_err_svc_dupped:
	dtp_req_decref(rpc_req);
	daos_rank_list_free(pc_in->pc_svc);
out_err_tgts_dupped:
	daos_rank_list_free(pc_in->pc_tgts);
out_err_tgt_dev_dupped:
	D_FREE(pc_in->pc_tgt_dev, strlen(dev));
out_err_grp_dupped:
	D_FREE(pc_in->pc_grp, strlen(grp));
out_err_req_created:
	dtp_req_decref(rpc_req);
out:
	return rc;
}
