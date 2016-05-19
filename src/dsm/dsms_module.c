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

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler dsms_handlers[] = {
	{
		.dr_opc		= DSM_POOL_CONNECT,
		.dr_hdlr	= dsms_hdlr_pool_connect
	}, {
		.dr_opc		= DSM_POOL_DISCONNECT,
		.dr_hdlr	= dsms_hdlr_pool_disconnect
	}, {
		.dr_opc		= DSM_CONT_CREATE,
		.dr_hdlr	= dsms_hdlr_cont_create
	}, {
		.dr_opc		= DSM_CONT_DESTROY,
		.dr_hdlr	= dsms_hdlr_cont_destroy
	}, {
		.dr_opc		= DSM_CONT_OPEN,
		.dr_hdlr	= dsms_hdlr_cont_open
	}, {
		.dr_opc		= DSM_CONT_CLOSE,
		.dr_hdlr	= dsms_hdlr_cont_close
	}, {
		.dr_opc		= DSM_TGT_OBJ_UPDATE,
		.dr_hdlr	= dsms_hdlr_object_rw,
	}, {
		.dr_opc		= DSM_TGT_OBJ_FETCH,
		.dr_hdlr	= dsms_hdlr_object_rw,
	}, {
		.dr_opc		= 0
	}
};

struct dss_module daos_m_srv_module =  {
	.sm_name	= "daos_m_srv",
	.sm_mod_id	= DAOS_DSM_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_rpcs	= dsm_rpcs,
	.sm_handlers	= dsms_handlers,
};
