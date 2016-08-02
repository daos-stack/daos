/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * DAOS common code for RPC management. Infrastructure for registering the
 * protocol between the client library and the server module as well as between
 * the server modules.
 */

#ifndef __DRPC_API_H__
#define __DRPC_API_H__

#include <daos/transport.h>
#include <daos/event.h>

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
#define MOD_ID_BITS	8
#define opc_get_mod_id(opcode)	((opcode >> 24) & MODID_MASK)
#define opc_get(opcode)		(opcode & OPCODE_MASK)

#define DAOS_RPC_OPCODE(opc, mod_id, rpc_ver)			\
	((opc & OPCODE_MASK) << OPCODE_OFFSET |			\
	 (rpc_ver & RPC_VERSION_MASK) << RPC_VERSION_OFFSET |	\
	 (mod_id & MODID_MASK) << MODID_OFFSET)

enum daos_module_id {
	DAOS_VOS_MODULE	= 0, /** version object store */
	DAOS_DMG_MODULE	= 1, /** storage management */
	DAOS_DSM_MODULE	= 2, /** memory-class DAOS */
	DAOS_DSR_MODULE	= 3, /** sharding & resilience */
	DAOS_DCT_MODULE	= 4, /** caching & tiering */
	DAOS_DSP_MODULE	= 5, /** posix emulation */
	DAOS_DSH_MODULE	= 6, /** hadoop interface */
	DAOS_MAX_MODULE	= (1 << MOD_ID_BITS) - 1,
};

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
	/* RPC request format */
	struct dtp_req_format *dr_req_fmt;
};

struct daos_rpc_handler {
	/* Operation code */
	dtp_opcode_t		dr_opc;
	/* Request handler, only relevant on the server side */
	dtp_rpc_cb_t		dr_hdlr;
	/* CORPC operations (co_aggregate == NULL for point-to-point RPCs) */
	struct dtp_corpc_ops	dr_corpc_ops;
};

static inline struct daos_rpc_handler *
daos_rpc_handler_find(struct daos_rpc_handler *handlers, dtp_opcode_t opc)
{
	struct daos_rpc_handler *handler;

	for (handler = handlers; handler->dr_opc != 0; handler++) {
		if (handler->dr_opc == opc)
			return handler;
	}
	return NULL;
}

/**
 * Register RPCs for both clients and servers.
 *
 * \param[in] rpcs	RPC list to be registered.
 * \param[in] handlers	RPC handlers to be registered, if
 *                      it is NULL, then it is for registering
 *                      client side RPC, otherwise it is for
 *                      server.
 * \param[in] mod_id	module id of the module.
 *
 * \retval	0 if registration succeeds
 * \retval	negative errno if registration fails.
 */
static inline int
daos_rpc_register(struct daos_rpc *rpcs, struct daos_rpc_handler *handlers,
		  int mod_id)
{
	struct daos_rpc	*rpc;
	int		 rc;

	if (rpcs == NULL)
		return 0;

	/* walk through the handler list and register each individual RPC */
	for (rpc = rpcs; rpc->dr_opc != 0; rpc++) {
		dtp_opcode_t opcode;

		opcode = DAOS_RPC_OPCODE(rpc->dr_opc, mod_id, rpc->dr_ver);
		if (handlers != NULL) {
			struct daos_rpc_handler *handler;

			handler = daos_rpc_handler_find(handlers, rpc->dr_opc);
			if (handler == NULL) {
				D_ERROR("failed to find handler for opc %x\n",
					rpc->dr_opc);
				return rc;
			}
			if (handler->dr_corpc_ops.co_aggregate == NULL)
				rc = dtp_rpc_srv_reg(opcode, rpc->dr_req_fmt,
						     handler->dr_hdlr);
			else
				rc = dtp_corpc_reg(opcode, rpc->dr_req_fmt,
						   handler->dr_hdlr,
						   &handler->dr_corpc_ops);
		} else {
			rc = dtp_rpc_reg(opcode, rpc->dr_req_fmt);
		}
		if (rc)
			return rc;
	}
	return 0;
}

static inline int
daos_rpc_unregister(struct daos_rpc *rpcs)
{
	if (rpcs == NULL)
		return 0;

	/* no supported for now */
	return 0;
}

int
daos_rpc_cb(const struct dtp_cb_info *cb_info);

static inline int
daos_rpc_send(dtp_rpc_t *rpc, daos_event_t *ev)
{
	int	rc;

	/* Send request */
	rc = dtp_req_send(rpc, daos_rpc_cb, ev);
	if (rc != 0) {
		/**
		 * event was started already, let's report the error
		 * asynchronously
		 */
		daos_event_complete(ev, rc);
		rc = 0;
	}

	/** wait for completion if blocking mode */
	if (daos_event_is_priv(ev))
		rc = daos_event_priv_wait(ev);

	return rc;
}
#endif /* __DRPC_API_H__ */
