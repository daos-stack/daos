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
 * dsmc: Container Methods
 */

#include <daos_m.h>

#include "dsm_rpc.h"
#include "dsmc_internal.h"

static int
cont_create_complete(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct cont_create_out *out;

	if (rc != 0) {
		D_ERROR("RPC error while creating container: %d\n", rc);
		D_GOTO(out, rc);
	}

	out = dtp_reply_get(sp->sp_rpc);

	rc = out->cco_ret;
	if (rc != 0) {
		D_ERROR("failed to create container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed creating container\n");

out:
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dsm_co_create(daos_handle_t poh, const uuid_t uuid, daos_event_t *ev)
{
	struct cont_create_in  *in;
	struct pool_conn       *conn = (struct pool_conn *)poh.cookie;
	dtp_endpoint_t		ep;
	dtp_rpc_t	       *rpc;
	struct daos_op_sp      *sp;
	int			rc;

	if (conn == NULL || uuid_is_null(uuid))
		return -DER_INVAL;

	if (!(conn->pc_capas & DAOS_PC_RW) && !(conn->pc_capas & DAOS_PC_EX))
		return -DER_NO_PERM;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	D_DEBUG(DF_DSMC, DF_UUID"\n", DP_UUID(uuid));

	/* To the only container service. */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_CONT_CREATE, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		return rc;
	}

	in = dtp_req_get(rpc);
	uuid_copy(in->cci_pool, conn->pc_pool);
	uuid_copy(in->cci_pool_hdl, conn->pc_pool_hdl);
	uuid_copy(in->cci_cont, uuid);

	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc);
	sp->sp_rpc = rpc;

	rc = daos_event_launch(ev, NULL /* abort_cb */, cont_create_complete);
	if (rc != 0) {
		dtp_req_decref(rpc);
		dtp_req_decref(rpc);
		return rc;
	}

	return daos_rpc_send(rpc, ev);
}

static int
cont_destroy_complete(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct cont_destroy_out *out;

	if (rc != 0) {
		D_ERROR("RPC error while destroying container: %d\n", rc);
		D_GOTO(out, rc);
	}

	out = dtp_reply_get(sp->sp_rpc);

	rc = out->cdo_ret;
	if (rc != 0) {
		D_ERROR("failed to destroy container: %d\n", rc);
		D_GOTO(out, rc);
	}

	D_DEBUG(DF_DSMC, "completed destroying container\n");

out:
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dsm_co_destroy(daos_handle_t poh, const uuid_t uuid, int force,
	       daos_event_t *ev)
{
	struct cont_destroy_in *in;
	struct pool_conn       *conn = (struct pool_conn *)poh.cookie;
	dtp_endpoint_t		ep;
	dtp_rpc_t	       *rpc;
	struct daos_op_sp      *sp;
	int			rc;

	/* TODO: Implement "force". */
	D_ASSERT(force != 0);

	if (conn == NULL || uuid_is_null(uuid))
		return -DER_INVAL;

	if (!(conn->pc_capas & DAOS_PC_RW) && !(conn->pc_capas & DAOS_PC_EX))
		return -DER_NO_PERM;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	D_DEBUG(DF_DSMC, DF_UUID" force=%d\n", DP_UUID(uuid), force);

	/* To the only container service. */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(daos_ev2ctx(ev), ep, DSM_CONT_DESTROY, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		return rc;
	}

	in = dtp_req_get(rpc);
	uuid_copy(in->cdi_pool, conn->pc_pool);
	uuid_copy(in->cdi_pool_hdl, conn->pc_pool_hdl);
	uuid_copy(in->cdi_cont, uuid);
	in->cdi_force = force;

	sp = daos_ev2sp(ev);
	dtp_req_addref(rpc);
	sp->sp_rpc = rpc;

	rc = daos_event_launch(ev, NULL /* abort_cb */, cont_destroy_complete);
	if (rc != 0) {
		dtp_req_decref(rpc);
		dtp_req_decref(rpc);
		return rc;
	}

	return daos_rpc_send(rpc, ev);
}
