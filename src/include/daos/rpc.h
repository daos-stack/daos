/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
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
#define MOD_ID_BITS	7
#define opc_get_mod_id(opcode)	((opcode >> MODID_OFFSET) & MODID_MASK)
#define opc_get(opcode)		(opcode & OPCODE_MASK)

#define DAOS_RPC_OPCODE(opc, mod_id, rpc_ver)			\
	((opc & OPCODE_MASK) << OPCODE_OFFSET |			\
	 (rpc_ver & RPC_VERSION_MASK) << RPC_VERSION_OFFSET |	\
	 (mod_id & MODID_MASK) << MODID_OFFSET)

enum daos_module_id {
	DAOS_VOS_MODULE		= 0, /** version object store */
	DAOS_MGMT_MODULE	= 1, /** storage management */
	DAOS_POOL_MODULE	= 2, /** pool service */
	DAOS_CONT_MODULE	= 3, /** container service */
	DAOS_OBJ_MODULE		= 4, /** object service */
	DAOS_REBUILD_MODULE	= 5, /** rebuild **/
	DAOS_RSVC_MODULE	= 6, /** replicated service server */
	DAOS_RDB_MODULE		= 7, /** rdb */
	DAOS_RDBT_MODULE	= 8, /** rdb test */
	DAOS_SEC_MODULE		= 9, /** security framework */
	DAOS_DTX_MODULE		= 10, /** DTX */
	DAOS_PIPELINE_MODULE	= 11,
	DAOS_NR_MODULE		= 12, /** number of defined modules */
	DAOS_MAX_MODULE		= 64  /** Size of uint64_t see dmg profile */
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

/** DAOS RPC request type (to determine the target processing tag/context) */
enum daos_rpc_type {
	/** common IO request */
	DAOS_REQ_IO,
	/** management request defined/used by mgmt module */
	DAOS_REQ_MGMT,
	/** pool request defined/used by pool module */
	DAOS_REQ_POOL,
	/** RDB/meta-data request */
	DAOS_REQ_RDB,
	/** container request (including the OID allocate) */
	DAOS_REQ_CONT,
	/** rebuild request such as REBUILD_OBJECTS_SCAN/REBUILD_OBJECTS */
	DAOS_REQ_REBUILD,
	/** the IV/BCAST/SWIM request handled by cart, send/recv by tag 0 */
	DAOS_REQ_IV,
	DAOS_REQ_BCAST,
	DAOS_REQ_SWIM,
	/** Per VOS target request */
	DAOS_REQ_TGT,
};

/** DAOS_TGT0_OFFSET is target 0's cart context offset */
#define DAOS_TGT0_OFFSET		(2)
/** The cart context index of target index */
#define DAOS_IO_CTX_ID(tgt_idx)		((tgt_idx) + DAOS_TGT0_OFFSET)

/**
 * Get the target tag (context ID) for specific request type and target index.
 *
 * \param[in]	req_type	RPC request type (enum daos_rpc_type)
 * \param[in]	tgt_idx		target index (VOS index, main xstream index)
 *
 * \return			target tag (context ID) to be used for the RPC
 */
static inline int
daos_rpc_tag(int req_type, int tgt_idx)
{
	switch (req_type) {
	/* for normal IO request, send to the main service thread/context */
	case DAOS_REQ_IO:
	case DAOS_REQ_TGT:
		return DAOS_IO_CTX_ID(tgt_idx);
	case DAOS_REQ_SWIM:
		return 1;
	/* target tag 0 is to handle below requests */
	case DAOS_REQ_MGMT:
	case DAOS_REQ_POOL:
	case DAOS_REQ_RDB:
	case DAOS_REQ_CONT:
	case DAOS_REQ_REBUILD:
	case DAOS_REQ_IV:
	case DAOS_REQ_BCAST:
		return 0;
	default:
		D_ASSERTF(0, "bad req_type %d.\n", req_type);
		return -DER_INVAL;
	};
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

	/* TODO: mod_id is unused */

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

int daos_rpc_send(crt_rpc_t *rpc, tse_task_t *task);
int daos_rpc_complete(crt_rpc_t *rpc, tse_task_t *task);
int daos_rpc_send_wait(crt_rpc_t *rpc);

#define DAOS_DEFAULT_SYS_NAME "daos_server"

/* Currently, this is used on rcs in metadata RPC reply buffers. */
static inline bool
daos_rpc_retryable_rc(int rc)
{
	return daos_crt_network_error(rc) || rc == -DER_TIMEDOUT ||
	       rc == -DER_GRPVER || rc == -DER_EXCLUDED;
}

/* Determine if the RPC is from a client. If not, it's from a server rank. */
static inline bool
daos_rpc_from_client(crt_rpc_t *rpc)
{
	d_rank_t	srcrank;
	int		rc;

	D_ASSERT(rpc != NULL);

	rc = crt_req_src_rank_get(rpc, &srcrank);
	/* Only possible failures here are invalid inputs */
	D_ASSERTF(rc == 0, "error "DF_RC" should not be possible", DP_RC(rc));

	return (srcrank == CRT_NO_RANK);
}

int
daos_rpc_proto_query(crt_opcode_t base_opc, uint32_t *ver_array, int count, int *ret_ver);

#endif /* __DRPC_API_H__ */
