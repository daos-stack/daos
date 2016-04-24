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
#include <daos/rpc.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"

static int
init(void)
{
	int rc;

	rc = dsms_storage_init();
	if (rc != 0)
		return rc;

	rc = dsms_pool_init();
	if (rc != 0)
		dsms_storage_fini();

	return rc;
}

static int
fini(void)
{
	dsms_pool_fini();
	dsms_storage_fini();
	return 0;
}

int
dsms_hdlr_ping(dtp_rpc_t *rpc)
{
	struct ping_out	*ping_output = NULL;
	int		rc = 0;

	D_DEBUG(DF_UNKNOWN, "receive, ping %x.\n", rpc->dr_opc);

	ping_output = (struct ping_out *)rpc->dr_output;
	ping_output->ret = 0;

	rc = dtp_reply_send(rpc);

	D_DEBUG(DF_UNKNOWN, "ping ret: %d\n", ping_output->ret);

	return rc;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler dsms_handlers[] = {
	{
		.dr_opc		= DSM_PING,
		.dr_hdlr	= dsms_hdlr_ping,
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
	.sm_cl_rpcs	= dsm_rpcs,
	.sm_handlers	= dsms_handlers,
};
