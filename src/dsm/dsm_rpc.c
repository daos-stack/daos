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
 * dsm: RPC Protocol Serialization Functions
 */

#include <daos/event.h>
#include <daos/rpc.h>
#include "dsm_rpc.h"

static int
proc_pool_map(dtp_proc_t proc, void *data)
{
	struct pool_map	       *p = data;
	int			rc;

	rc = dtp_proc_uint64_t(proc, &p->pm_version);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->pm_ndomains);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->pm_ntargets);
	if (rc != 0)
		return rc;

	return 0;
}

static int
proc_pool_connect_in(dtp_proc_t proc, void *data)
{
	struct pool_connect_in *p = data;
	int			rc;

	rc = dtp_proc_uuid_t(proc, &p->pci_pool);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uuid_t(proc, &p->pci_pool_hdl);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->pci_uid);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->pci_gid);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint64_t(proc, &p->pci_capas);
	if (rc != 0)
		return rc;

	rc = dtp_proc_dtp_bulk_t(proc, &p->pci_pool_map_bulk);
	if (rc != 0)
		return rc;

	return 0;
}

static int
proc_pool_connect_out(dtp_proc_t proc, void *data)
{
	struct pool_connect_out	       *p = data;
	int				rc;

	rc = dtp_proc_int32_t(proc, &p->pco_rc);
	if (rc != 0)
		return rc;

	rc = proc_pool_map(proc, &p->pco_pool_map);
	if (rc != 0)
		return rc;

	return 0;
}

static int
proc_pool_disconnect_in(dtp_proc_t proc, void *data)
{
	struct pool_disconnect_in      *p = data;
	int				rc;

	rc = dtp_proc_uuid_t(proc, &p->pdi_pool);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uuid_t(proc, &p->pdi_pool_hdl);
	if (rc != 0)
		return rc;

	return 0;
}

static int
proc_pool_disconnect_out(dtp_proc_t proc, void *data)
{
	struct pool_disconnect_out     *p = data;

	return dtp_proc_int32_t(proc, &p->pdo_rc);
}

static int
dsm_proc_ping_in(dtp_proc_t proc, void *data)
{
	struct ping_in    *p = data;

	return dtp_proc_int32_t(proc, &p->unused);
}

static int
dsm_proc_ping_out(dtp_proc_t proc, void *data)
{
	struct ping_out     *p = data;

	return dtp_proc_int32_t(proc, &p->ret);
}

struct daos_rpc dsm_rpcs[] = {
	{
		.dr_name	= "DSM_POOL_CONNECT",
		.dr_opc		= DSM_POOL_CONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= proc_pool_connect_in,
		.dr_in_sz	= sizeof(struct pool_connect_in),
		.dr_out_hdlr	= proc_pool_connect_out,
		.dr_out_sz	= sizeof(struct pool_connect_out)
	}, {
		.dr_name	= "DSM_POOL_DISCONNECT",
		.dr_opc		= DSM_POOL_DISCONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= proc_pool_disconnect_in,
		.dr_in_sz	= sizeof(struct pool_disconnect_in),
		.dr_out_hdlr	= proc_pool_disconnect_out,
		.dr_out_sz	= sizeof(struct pool_disconnect_out)
	}, {
		.dr_name	= "DSM_PING",
		.dr_opc		= DSM_PING,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= dsm_proc_ping_in,
		.dr_in_sz	= sizeof(struct ping_in),
		.dr_out_hdlr	= dsm_proc_ping_out,
		.dr_out_sz	= sizeof(struct ping_out),
	}, {
		.dr_opc		= 0
	}
};

int
dsm_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep,
	       dtp_opcode_t opc, dtp_rpc_t **req)
{
	dtp_opcode_t opcode;

	opcode = DAOS_RPC_OPCODE(opc, DAOS_DSMS_MODULE, 1);

	return dtp_req_create(dtp_ctx, tgt_ep, opcode, req);
}

static int
dsm_client_async_cb(const struct dtp_cb_info *cb_info)
{
	struct daos_event *event;

	event = (struct daos_event *)cb_info->dci_arg;

	event->ev_error = cb_info->dci_rc;
	daos_event_complete(event);
	return 0;
}

int
dsm_client_async_rpc(dtp_rpc_t *rpc_req, struct daos_event *event)
{
	int			rc = 0;

	rc = daos_event_launch(event);
	if (rc != 0)
		return rc;

	rc = dtp_req_send(rpc_req, dsm_client_async_cb, event);
	if (rc != 0) {
		daos_event_abort(event);
		return rc;
	}

	return rc;
}
