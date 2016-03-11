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
 * dsms: Module Definitions
 *
 * dsms is the DSM server module/library. It exports the DSM RPC handlers and
 * the DSM server API. This file contains the definitions expected by server;
 * the DSM server API methods are exported directly where they are defined as
 * extern functions.
 */

#include <daos_srv/daos_server.h>
#include <daos/daos_rpc.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"

static int
init(void)
{
	return 0;
}

static int
fini(void)
{
	return 0;
}

static struct daos_rpc client_rpcs[] = {
	{
		.dr_name	= "DSM_POOL_CONNECT",
		.dr_opc		= 1,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= dsm_proc_pool_connect_in,
		.dr_in_sz	= 0,
		.dr_out_hdlr	= dsm_proc_pool_connect_out,
		.dr_out_sz	= 0,
		.dr_hdlr	= dsms_hdlr_pool_connect
	}, {
		.dr_name	= "DSM_POOL_DISCONNECT",
		.dr_opc		= 2,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_in_hdlr	= dsm_proc_pool_disconnect_in,
		.dr_in_sz	= 0,
		.dr_out_hdlr	= dsm_proc_pool_disconnect_out,
		.dr_out_sz	= 0,
		.dr_hdlr	= dsms_hdlr_pool_disconnect
	}, {
		.dr_opc		= 0
	}
};

struct dss_module daos_m_srv_module =  {
	.sm_name	= "daos_m_srv",
	.sm_mod_id	= DAOS_DSMS_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_rpcs	= client_rpcs,
	.sm_srv_rpcs	= NULL
};
