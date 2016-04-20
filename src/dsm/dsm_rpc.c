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
#include <daos/daos_rpc.h>
#include "dsm_rpc.h"

static int
dsm_proc_pool_connect_in(dtp_proc_t proc, void *data)
{
	struct pool_connect_in *p = data;
	int			rc;

	rc = dtp_proc_uuid_t(proc, &p->pool);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uuid_t(proc, &p->pool_hdl);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->uid);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &p->gid);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint64_t(proc, &p->pool_capas);
	if (rc != 0)
		return rc;

	rc = dtp_proc_dtp_bulk_t(proc, &p->pool_map_bulk);
	if (rc != 0)
		return rc;

	return 0;
}

static int
dsm_proc_pool_connect_out(dtp_proc_t proc, void *data)
{
	struct pool_connect_out	       *p = data;
	int				rc;

	rc = dtp_proc_int32_t(proc, &p->rc);
	if (rc != 0)
		return rc;

	rc = proc_pool_map(proc, &p->pool_map);
	if (rc != 0)
		return rc;

	return 0;
}

static int
dsm_proc_pool_disconnect_in(dtp_proc_t proc, void *data)
{
	struct pool_disconnect_in      *p = data;

	return dtp_proc_uuid_t(proc, &p->pool_hdl);
}

static int
dsm_proc_pool_disconnect_out(dtp_proc_t proc, void *data)
{
	struct pool_disconnect_out     *p = data;

	return dtp_proc_int32_t(proc, &p->rc);
}

struct daos_rpc dsm_client_rpcs[] = {
	{
		.dr_name	= "DSM_POOL_CONNECT",
		.dr_opc		= POOL_CONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= dsm_proc_pool_connect_in,
		.dr_in_sz	= 0,	/* TODO */
		.dr_out_hdlr	= dsm_proc_pool_connect_out,
		.dr_out_sz	= 0,	/* TODO */
	}, {
		.dr_name	= "DSM_POOL_DISCONNECT",
		.dr_opc		= POOL_DISCONNECT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= dsm_proc_pool_disconnect_in,
		.dr_in_sz	= 0,	/* TODO */
		.dr_out_hdlr	= dsm_proc_pool_disconnect_out,
		.dr_out_sz	= 0,	/* TODO */
	}, {
		.dr_opc		= 0
	}
};
