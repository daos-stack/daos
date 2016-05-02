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
pool_create_cb(const struct dtp_cb_info *cb_info)
{
	dtp_rpc_t			*rpc_req;
	struct dmg_pool_create_in	*pc_in;
	struct dmg_pool_create_out	*pc_out;
	daos_event_t			*ev;
	int				rc = 0;

	rpc_req = cb_info->dci_rpc;
	pc_in = rpc_req->dr_input;
	pc_out = rpc_req->dr_output;
	rc = cb_info->dci_rc;
	ev = (daos_event_t *)cb_info->dci_arg;
	D_ASSERT(ev != NULL && pc_in != NULL && pc_out != NULL);

	if (rc != 0) {
		D_ERROR("RPC failed, rc: %d.\n", rc);
		goto out;
	}
	rc = pc_out->pc_rc;
	if (rc != 0) {
		D_ERROR("DMG_POOL_CREATE replied failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	daos_rank_list_copy(pc_in->pc_svc, pc_out->pc_svc, false);

out:
	ev->ev_error = rc;
	daos_event_complete(ev);
	return 0;
}

int
dmg_pool_create(const uuid_t uuid, unsigned int mode, const char *grp,
		const daos_rank_list_t *tgts, const char *dev, daos_size_t size,
		daos_rank_list_t *svc, daos_event_t *ev)
{
	dtp_endpoint_t			svr_ep;
	dtp_rpc_t			*rpc_req = NULL;
	struct dmg_pool_create_in	*pc_in;
	struct dmg_pool_create_out	*pc_out;
	int				rc = 0;

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

	svr_ep.ep_rank = 0;
	svr_ep.ep_tag = 0;
	rc = dtp_req_create(dmgc_ctx, svr_ep, DMG_POOL_CREATE, &rpc_req);
	if (rc != 0) {
		D_ERROR("dtp_req_create(DMG_POOL_CREATE) failed, rc: %d.\n",
			rc);
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_req != NULL);
	pc_in = (struct dmg_pool_create_in *)rpc_req->dr_input;
	pc_out = (struct dmg_pool_create_out *)rpc_req->dr_output;
	D_ASSERT(pc_in != NULL && pc_out != NULL);

	uuid_copy(pc_in->pc_uuid, uuid);
	pc_in->pc_mode = mode;
	/* TODO pc_in->pc_grp_id */
	rc = daos_rank_list_dup(&pc_in->pc_tgts, tgts, true);
	if (rc != 0) {
		D_ERROR("daos_rank_list_dup failed, rc: %d.\n", rc);
		dtp_req_decref(rpc_req);
		D_GOTO(out, rc);
	}
	pc_in->pc_tgt_dev = dev;
	pc_in->pc_tgt_size = size;
	rc = daos_rank_list_dup(&pc_in->pc_svc, svc, true);
	if (rc != 0) {
		D_ERROR("daos_rank_list_dup failed, rc: %d.\n", rc);
		daos_rank_list_free(pc_in->pc_tgts);
		dtp_req_decref(rpc_req);
		D_GOTO(out, rc);
	}

	if (ev == NULL) {
		dtp_req_addref(rpc_req);

		rc = dtp_sync_req(rpc_req, 0);
		if (rc != 0) {
			D_ERROR("dtp_sync_req failed, rc: %d.\n", rc);
			daos_rank_list_free(pc_in->pc_svc);
			daos_rank_list_free(pc_in->pc_tgts);
			dtp_req_decref(rpc_req);
			D_GOTO(out, rc);
		}
		rc = pc_out->pc_rc;
		if (rc != 0) {
			D_ERROR("DMG_POOL_CREATE replied failed, rc: %d.\n",
				rc);
			dtp_req_decref(rpc_req);
			D_GOTO(out, rc);
		}

		daos_rank_list_copy(svc, pc_out->pc_svc, false);
		dtp_req_decref(rpc_req);
	} else {
		rc = daos_event_launch(ev);
		if (rc != 0) {
			D_ERROR("daos_event_launch failed, rc: %d.\n", rc);
			daos_rank_list_free(pc_in->pc_svc);
			daos_rank_list_free(pc_in->pc_tgts);
			dtp_req_decref(rpc_req);
			D_GOTO(out, rc);
		}

		rc = dtp_req_send(rpc_req, pool_create_cb, ev);
		if (rc != 0) {
			D_ERROR("dtp_req_send failed, rc: %d.\n", rc);
			daos_rank_list_free(pc_in->pc_svc);
			daos_rank_list_free(pc_in->pc_tgts);
			ev->ev_error = rc;
			daos_event_complete(ev);
			rc = 0;
		}
	}

out:
	return rc;
}
