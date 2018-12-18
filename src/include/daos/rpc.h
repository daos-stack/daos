/**
 * (C) Copyright 2016-2018 Intel Corporation.
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

#include <daos/common.h>
#include <daos/tse.h>
#include <cart/api.h>

/* Opcode registered in crt will be
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

/** DAOS-specific RPC format */
extern struct crt_msg_field DMF_OID;
extern struct crt_msg_field DMF_IOVEC;
extern struct crt_msg_field DMF_IOD_ARRAY;
extern struct crt_msg_field DMF_EPOCH_STATE;
extern struct crt_msg_field DMF_ANCHOR;
extern struct crt_msg_field DMF_KEY_DESC_ARRAY;
extern struct crt_msg_field DMF_REC_SIZE_ARRAY;
extern struct crt_msg_field DMF_SGL;
extern struct crt_msg_field DMF_SGL_ARRAY;
extern struct crt_msg_field DMF_SGL_DESC;
extern struct crt_msg_field DMF_SGL_DESC_ARRAY;
extern struct crt_msg_field DMF_NR_ARRAY;
extern struct crt_msg_field DMF_RECX_ARRAY;
extern struct crt_msg_field DMF_RECX;
extern struct crt_msg_field DMF_EPR_ARRAY;
extern struct crt_msg_field DMF_UUID_ARRAY;
extern struct crt_msg_field DMF_OID_ARRAY;
extern struct crt_msg_field DMF_UINT32_ARRAY;
extern struct crt_msg_field DMF_UINT64_ARRAY;
extern struct crt_msg_field DMF_RSVC_HINT;
extern struct crt_msg_field DMF_KEY_ARRAY;
extern struct crt_msg_field DMF_TGT_ADDR_LIST;
#define DMF_DAOS_SIZE CMF_UINT64

enum daos_module_id {
	DAOS_VOS_MODULE		= 0, /** version object store */
	DAOS_MGMT_MODULE	= 1, /** storage management */
	DAOS_POOL_MODULE	= 2, /** pool service */
	DAOS_CONT_MODULE	= 3, /** container service */
	DAOS_OBJ_MODULE		= 4, /** object service */
	DAOS_REBUILD_MODULE	= 5, /** rebuild **/
	DAOS_RDB_MODULE		= 6, /** rdb */
	DAOS_RDBT_MODULE	= 7, /** rdb test */
	DAOS_MAX_MODULE		= (1 << MOD_ID_BITS) - 1,
};

enum daos_rpc_flags {
	/** flag of reply disabled */
	DAOS_RPC_NO_REPLY	= CRT_RPC_FEAT_NO_REPLY,
};

struct daos_rpc_handler {
	/* Operation code */
	crt_opcode_t		 dr_opc;
	/* Request handler, only relevant on the server side */
	crt_rpc_cb_t		 dr_hdlr;
	/* CORPC operations (co_aggregate == NULL for point-to-point RPCs) */
	struct crt_corpc_ops	*dr_corpc_ops;
};

static inline struct daos_rpc_handler *
daos_rpc_handler_find(struct daos_rpc_handler *handlers, crt_opcode_t opc)
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
 * \param[in] proto_fmt	CRT specification of RPC protocol.
 * \param[in] cli_count	count of RPCs to be registered in client.
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
daos_rpc_register(struct crt_proto_format *proto_fmt, uint32_t cli_count,
		  struct daos_rpc_handler *handlers, int mod_id)
{
	uint32_t i;

	if (proto_fmt == NULL)
		return 0;

	if (handlers != NULL) {
		/* walk through the RPC list and fill with handlers */
		for (i = 0; i < proto_fmt->cpf_count; i++) {
			proto_fmt->cpf_prf[i].prf_hdlr =
						handlers[i].dr_hdlr;
			proto_fmt->cpf_prf[i].prf_co_ops =
						handlers[i].dr_corpc_ops;
		}
	} else {
		proto_fmt->cpf_count = cli_count;
	}

	return crt_proto_register(proto_fmt);
}

static inline int
daos_rpc_unregister(struct crt_proto_format *proto_fmt)
{
	if (proto_fmt == NULL)
		return 0;

	/* no supported for now */
	return 0;
}

static inline d_sg_list_t *
daos2crt_sg(daos_sg_list_t *sgl)
{
	/** XXX better integration with CaRT required */
	D_CASSERT(sizeof(daos_sg_list_t) == sizeof(d_sg_list_t));
	D_CASSERT(offsetof(daos_sg_list_t, sg_nr) ==
		  offsetof(d_sg_list_t, sg_nr));
	D_CASSERT(offsetof(daos_sg_list_t, sg_iovs) ==
		  offsetof(d_sg_list_t, sg_iovs));
	D_CASSERT(sizeof(daos_iov_t) == sizeof(d_iov_t));
	D_CASSERT(offsetof(daos_iov_t, iov_buf) ==
		  offsetof(d_iov_t, iov_buf));
	D_CASSERT(offsetof(daos_iov_t, iov_buf_len) ==
		  offsetof(d_iov_t, iov_buf_len));
	D_CASSERT(offsetof(daos_iov_t, iov_len) ==
		  offsetof(d_iov_t, iov_len));
	return (d_sg_list_t *)sgl;
}

int daos_rpc_send(crt_rpc_t *rpc, tse_task_t *task);
int daos_rpc_complete(crt_rpc_t *rpc, tse_task_t *task);

#define DAOS_DEFAULT_GROUP_ID "daos_server"

static inline int
daos_group_attach(const char *group_id, crt_group_t **group)
{
	D_DEBUG(DB_NET, "attaching to group '%s'\n", group_id);
	if (group_id == NULL)
		group_id = DAOS_DEFAULT_GROUP_ID;
	return crt_group_attach((char *)group_id, group);
}

static inline int
daos_group_detach(crt_group_t *group)
{
	D_ASSERT(group != NULL);
	D_DEBUG(DB_NET, "detaching from group '%s'\n", group->cg_grpid);
	return crt_group_detach(group);
}

/* Currently, this is used on rcs in metadata RPC reply buffers. */
static inline bool
daos_rpc_retryable_rc(int rc)
{
	return rc == -DER_TIMEDOUT || rc == -DER_HG;
}

#endif /* __DRPC_API_H__ */
