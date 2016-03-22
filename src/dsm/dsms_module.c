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

static struct dss_handler client_handlers[] = {
	{
		.sh_name	= "DSM_POOL_CONNECT",
		.sh_opc		= 1,
		.sh_ver		= 1,
		.sh_flags	= 0,
		.sh_in_hdlr	= dsm_proc_pool_connect_in,
		.sh_in_sz	= 0,
		.sh_out_hdlr	= dsm_proc_pool_connect_out,
		.sh_out_sz	= 0,
		.sh_hdlr	= dsms_hdlr_pool_connect
	}, {
		.sh_name	= "DSM_POOL_DISCONNECT",
		.sh_opc		= 2,
		.sh_ver		= 1,
		.sh_flags	= 0,
		.sh_in_hdlr	= dsm_proc_pool_disconnect_in,
		.sh_in_sz	= 0,
		.sh_out_hdlr	= dsm_proc_pool_disconnect_out,
		.sh_out_sz	= 0,
		.sh_hdlr	= dsms_hdlr_pool_disconnect
	}, {
		.sh_opc		= 0
	}
};

struct dss_module daos_m_srv_module =  {
	.sm_name	= "daos_m_srv",
	.sm_mod_id	= DAOS_DSMS_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_hdlrs	= client_handlers,
	.sm_srv_hdlrs	= NULL
};
