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
#include <daos/daos_rpc.h>
#include <daos/daos_common.h>

DTP_GEN_PROC(echo_in_t,
        ((bool)(age)) ((dtp_string_t)(name)) ((uint32_t)(days)))
DTP_GEN_PROC(echo_out_t,
        ((int32_t)(ret)) ((uint32_t)(room_no)))

static int
echo(dtp_rpc_t *req)
{
	echo_in_t	*in = NULL;
	echo_out_t	*out = NULL;
	int		 rc = 0;

	/* dtp internally already allocated the input/output buffer */
	in = (echo_in_t *)req->dr_input;
	assert(in != NULL);
	out = (echo_out_t *)req->dr_output;
	assert(out != NULL);

	D_DEBUG(DF_MGMT, "echo_srv recv'd checkin, opc: 0x%x\n", req->dr_opc);
	D_DEBUG(DF_MGMT, "checkin input - age: %d, name: %s, days: %d\n",
		in->age, in->name, in->days);

	out->ret = 0;
	out->room_no = 1082;

	rc = dtp_reply_send(req);

	D_DEBUG(DF_MGMT, "echo_srv sent checkin reply, ret: %d, room_no: %d\n",
		out->ret, out->room_no);

	return rc;
}

/** Handlers for RPC sent by clients */
static struct dss_handler dmg_cl_hdlrs[] = {
	{
		/* .sh_name	= */	"ECHO",
		/* .sh_opc	= */	0xa1,
		/* .sh_ver	= */	1,
		/* .sh_flags	= */	0,
		/* .sh_in_hdlr	= */	dtp_proc_echo_in_t,
		/* .sh_in_sz	= */	sizeof(echo_in_t),
		/* .sh_out_hdlr	= */	dtp_proc_echo_out_t,
		/* .sh_out_sz	= */	sizeof(echo_out_t),
		/* .sh_hdlr	= */	echo,
	},
	{
	},
};

/** Handlers for RPC sent by other servers */
static struct dss_handler dmg_srv_hdlrs[] = {
	{
	},
};

int
dmg_init()
{
	D_DEBUG(DF_MGMT, "successfull init call\n");
	return 0;
}

int
dmg_fini()
{
	D_DEBUG(DF_MGMT, "successfull fini call\n");
	return 0;
}

struct dss_module daos_mgmt_srv_module = {
	.sm_name	= "daos_mgmt_srv",
	.sm_mod_id	= DAOS_DMG_MODULE,
	.sm_ver		= 1,
	.sm_init	= dmg_init,
	.sm_fini	= dmg_fini,
	.sm_cl_hdlrs	= dmg_cl_hdlrs,
	.sm_srv_hdlrs	= dmg_srv_hdlrs,
};
