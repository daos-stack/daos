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

#include <daos_srv/daos_server.h>
#include <daos/daos_common.h>

static int
ping(void *req)
{
	return 0;
}

/** Handlers for RPC sent by clients */
static struct dss_handler dmg_cl_hdlrs[] = {
	{
		.sh_name	= "PING",
		.sh_opc		= 0x1,
		.sh_ver		= 0,
		.sh_flags	= 0,
		.sh_func	= ping,
	},
};

/** Handlers for RPC sent by other servers */
static struct dss_handler dmg_srv_hdlrs[] = {
	{
		.sh_name	= "PING",
		.sh_opc		= 0x2,
		.sh_ver		= 0,
		.sh_flags	= 0,
		.sh_func	= ping,
	},
};

int
dmg_init()
{
	D_DEBUG(DF_MGMT, "successfull init call");
	return 0;
}

int
dmg_fini()
{
	D_DEBUG(DF_MGMT, "successfull fini call");
	return 0;
}

struct dss_module daos_mgmt_srv_module = {
	.sm_name	= "daos_mgmt_srv",
	.sm_ver		= 1,
	.sm_init	= dmg_init,
	.sm_fini	= dmg_fini,
	.sm_cl_hdlrs	= dmg_cl_hdlrs,
	.sm_srv_hdlrs	= dmg_srv_hdlrs,
};
