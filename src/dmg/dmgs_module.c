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
/**
 * This file is part of the DAOS server. It implements the DAOS storage
 * management interface that covers:
 * - storage detection;
 * - storage allocation;
 * - DAOS pool initialization.
 *
 * The storage manager is a first-class server module (like dsm/r server-side
 * library) and can be unloaded/reloaded.
 */

#include "dmgs_internal.h"

static struct daos_rpc_handler dmgs_handlers[] = {
	{
		.dr_opc		= DMG_POOL_CREATE,
		.dr_hdlr	= dmgs_hdlr_pool_create,
	}, {
		.dr_opc		= DMG_TGT_CREATE,
		.dr_hdlr	= dmgs_hdlr_tgt_create,
	}, {
		.dr_opc		= DMG_TGT_DESTROY,
		.dr_hdlr	= dmgs_hdlr_tgt_destroy,
	}, {
		.dr_opc = 0,
	}
};

static int
dmgs_init()
{
	int rc;

	rc = dmgs_tgt_init();
	if (rc)
		return rc;

	D_DEBUG(DF_MGMT, "successfull init call\n");
	return 0;
}

static int
dmgs_fini()
{
	D_DEBUG(DF_MGMT, "successfull fini call\n");
	return 0;
}

struct dss_module daos_mgmt_srv_module = {
	.sm_name	= "daos_mgmt_srv",
	.sm_mod_id	= DAOS_DMG_MODULE,
	.sm_ver		= 1,
	.sm_init	= dmgs_init,
	.sm_fini	= dmgs_fini,
	.sm_cl_rpcs	= dmg_rpcs,
	.sm_handlers	= dmgs_handlers,
};
