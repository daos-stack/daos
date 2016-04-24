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
 * dsmc: Pool Methods
 */

#include <daos_m.h>
#include <unistd.h>
#include "dsm_rpc.h"
#include "dsmc_internal.h"

/*
 * Client pool handle
 *
 * This is called a connection to avoid potential confusion with the server
 * pool_hdl.
 */
struct pool_conn {
	uuid_t		pc_pool;
	uuid_t		pc_pool_hdl;
	uint64_t	pc_capas;
};

static inline int
flags_are_valid(unsigned int flags)
{
	unsigned int mode = flags & (DAOS_PC_RO | DAOS_PC_RW | DAOS_PC_EX);

	return (mode != DAOS_PC_RO) && (mode != DAOS_PC_RW) &&
	       (mode != DAOS_PC_EX);
}

int
dsm_pool_connect(const uuid_t uuid, const char *grp,
		 const daos_rank_list_t *tgts, unsigned int flags,
		 daos_rank_list_t *failed, daos_handle_t *poh, daos_event_t *ev)
{
	dtp_endpoint_t			ep;
	dtp_rpc_t		       *rpc;
	struct pool_connect_in	       *in;
	struct pool_connect_out	       *out;
	struct pool_conn	       *conn;
	int				rc;

	/* TODO: Implement these. */
	D_ASSERT(grp == NULL);
	D_ASSERT(tgts == NULL);
	D_ASSERT(failed == NULL);
	D_ASSERT(ev == NULL);

	if (uuid_is_null(uuid) || !flags_are_valid(flags) || poh == NULL)
		return -DER_INVAL;

	/*
	 * Currently, rank 0 runs the pool and the (only) container service.
	 * ep.ep_grp_id and ep.ep_tag are not used at the moment.
	 */
	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(dsm_context, ep, DSM_POOL_CONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		return rc;
	}

	in = rpc->dr_input;
	uuid_copy(in->pci_pool, uuid);
	uuid_generate(in->pci_pool_hdl);
	in->pci_uid = geteuid();
	in->pci_gid = getegid();
	in->pci_capas = flags;
	in->pci_pool_map_bulk = NULL;	/* TODO */

	dtp_req_addref(rpc);

	rc = dtp_sync_req(rpc, 0 /* infinite timeout */);
	if (rc != 0)
		D_GOTO(out_req, rc);

	out = rpc->dr_output;
	if (out->pco_rc != 0) {
		D_ERROR("failed to connect to pool: %d\n", out->pco_rc);
		D_GOTO(out_req, rc = out->pco_rc);
	}

	D_ALLOC_PTR(conn);
	if (conn == NULL) {
		D_ERROR("failed to allocate pool connection\n");
		D_GOTO(out_req, rc = -DER_NOMEM);
	}

	uuid_copy(conn->pc_pool, in->pci_pool);
	uuid_copy(conn->pc_pool_hdl, in->pci_pool_hdl);
	conn->pc_capas = in->pci_capas;

	poh->cookie = (uint64_t)conn;
out_req:
	dtp_req_decref(rpc);
	return rc;
}

int
dsm_pool_disconnect(daos_handle_t poh, daos_event_t *ev)
{
	struct pool_conn	       *conn = (struct pool_conn *)poh.cookie;
	dtp_endpoint_t			ep;
	dtp_rpc_t		       *rpc;
	struct pool_disconnect_in      *in;
	struct pool_disconnect_out     *out;
	int				rc;

	D_ASSERT(ev == NULL);

	if (conn == NULL)
		return -DER_NO_HDL;

	uuid_clear(ep.ep_grp_id);
	ep.ep_rank = 0;
	ep.ep_tag = 0;

	rc = dsm_req_create(dsm_context, ep, DSM_POOL_DISCONNECT, &rpc);
	if (rc != 0) {
		D_ERROR("failed to create rpc: %d\n", rc);
		return rc;
	}

	in = rpc->dr_input;
	uuid_copy(in->pdi_pool, conn->pc_pool);
	uuid_copy(in->pdi_pool_hdl, conn->pc_pool_hdl);

	dtp_req_addref(rpc);

	rc = dtp_sync_req(rpc, 0 /* infinite timeout */);
	if (rc != 0)
		D_GOTO(out_req, rc);

	out = rpc->dr_output;
	if (out->pdo_rc != 0) {
		D_ERROR("failed to disconnect to pool: %d\n", out->pdo_rc);
		D_GOTO(out_req, rc = out->pdo_rc);
	}

	D_FREE_PTR(conn);
out_req:
	dtp_req_decref(rpc);
	return rc;
}
