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
 * DAOS common code for RPC management. Infrastructure for registering the
 * protocol between the client library and the server module as well as between
 * the server modules.
 */
#ifndef __DRPC_API_H__
#define __DRPC_API_H__

#include <daos/daos_transport.h>

enum daos_module_id {
	DAOS_DMG_MODULE		= 0,
	DAOS_DSMS_MODULE	= 1,
};

/* Opcode registered in dtp will be
 * client/server | mod_id | rpc_version | op_code
 *    {1 bit}	  {7 bits}    {8 bits}    {16 bits}
 */
#define OPCODE_MASK	0xffff
#define OPCODE_OFFSET	0

#define RPC_VERSION_MASK 0xff
#define RPC_VERSION_OFFSET 16

#define MODID_MASK	0xff
#define MODID_OFFSET	24

#define DAOS_RPC_OPCODE(opc, mod_id, rpc_ver)			\
	((opc & OPCODE_MASK) << OPCODE_OFFSET |			\
	 (rpc_ver & RPC_VERSION_MASK) << RPC_VERSION_OFFSET |	\
	 (mod_id & MODID_MASK) << MODID_OFFSET)

/**
 * common RPC format definition for both client and server
 */
struct daos_rpc {
	/* Name of the RPC */
	const char	*dr_name;
	/* Operation code associated with the RPC */
	dtp_opcode_t	 dr_opc;
	/* RPC version */
	int		 dr_ver;
	/* Operation flags, TBD */
	int		 dr_flags;
	/* Pack/unpack input parameter, invoked from C code */
	dtp_proc_cb_t	 dr_in_hdlr;
	/* Size of input parameter */
	int		 dr_in_sz;
	/* Pack/unpack output parameter, invoked from C code */
	dtp_proc_cb_t	 dr_out_hdlr;
	/* Size of output parameter */
	int		 dr_out_sz;
	/* Request handler, only relevant on the server side, invoked from C
	 * code */
	dtp_rpc_cb_t	 dr_hdlr;
};

/**
 * Register RPCs for both clients and servers.
 *
 * \param[in] rpcs	RPC list to be registered.
 * \param[in] mod_id	module id of the module.
 * \param[in] server	True if the node is a DAOS server,
 *			False if client
 *
 * \retval	0 if registration succeeds
 * \retval	negative errno if registration fails.
 */
static inline int
daos_rpc_register(struct daos_rpc *rpcs, int mod_id, bool server)
{
	struct daos_rpc	*rpc;
	int		 rc;

	if (rpcs == NULL)
		return 0;

	/* walk through the handler list and register each individual RPC */
	for (rpc = rpcs; rpc->dr_opc != 0; rpc++) {
		dtp_opcode_t opcode;

		opcode = DAOS_RPC_OPCODE(rpc->dr_opc, mod_id, rpc->dr_ver);
		if (server)
			rc = dtp_rpc_srv_reg(opcode, rpc->dr_in_hdlr,
					     rpc->dr_out_hdlr, rpc->dr_in_sz,
					     rpc->dr_out_sz, rpc->dr_hdlr);
		else
			rc = dtp_rpc_reg(opcode, rpc->dr_in_hdlr,
					 rpc->dr_out_hdlr, rpc->dr_in_sz,
					 rpc->dr_out_sz);
		if (rc)
			return rc;
	}
	return 0;
}

static inline int
daos_client_rpc_register(struct daos_rpc *rpcs, int mod_id)
{
	return daos_rpc_register(rpcs, mod_id, false);
}

static inline int
daos_rpc_unregister(struct daos_rpc *rpcs)
{
	if (rpcs == NULL)
		return 0;

	/* no supported for now */
	return 0;
}
#endif /* __DRPC_API_H__ */
