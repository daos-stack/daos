/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_rsvc: RPC Protocol Definitions
 */

#ifndef __RSVC_RPC_H__
#define __RSVC_RPC_H__

#include <stdint.h>
#include <uuid/uuid.h>
#include <daos/rpc.h>
#include <daos/rsvc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_RSVC_VERSION 3
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define RSVC_PROTO_SRV_RPC_LIST						\
	X(RSVC_START,							\
		0, &CQF_rsvc_start,					\
		ds_rsvc_start_handler,					\
		&ds_rsvc_start_co_ops),					\
	X(RSVC_STOP,							\
		0, &CQF_rsvc_stop,					\
		ds_rsvc_stop_handler,					\
		&ds_rsvc_stop_co_ops)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum rsvc_operation {
	RSVC_PROTO_SRV_RPC_LIST
};

#undef X

extern struct crt_proto_format rsvc_proto_fmt;

#define DAOS_ISEQ_RSVC_START /* input fields */			 \
	((d_iov_t)		(sai_svc_id)		CRT_VAR) \
	((uuid_t)		(sai_db_uuid)		CRT_VAR) \
	((uint32_t)		(sai_class)		CRT_VAR) \
	((uint32_t)		(sai_mode)		CRT_VAR) \
	((uint32_t)		(sai_flags)		CRT_VAR) \
	((uint32_t)		(sai_padding)		CRT_VAR) \
	((uint64_t)		(sai_size)		CRT_VAR) \
	((d_rank_list_t)	(sai_ranks)		CRT_PTR)

#define DAOS_OSEQ_RSVC_START /* output fields (rc: err count) */ \
	((int32_t)		(sao_rc)		CRT_VAR) \
	((int32_t)		(sao_rc_errval)		CRT_VAR)


CRT_RPC_DECLARE(rsvc_start, DAOS_ISEQ_RSVC_START, DAOS_OSEQ_RSVC_START)

#define DAOS_ISEQ_RSVC_STOP /* input fields */			 \
	((d_iov_t)		(soi_svc_id)		CRT_VAR) \
	((uint32_t)		(soi_class)		CRT_VAR) \
	((uint32_t)		(soi_flags)		CRT_VAR)

#define DAOS_OSEQ_RSVC_STOP /* output fields */			 \
	((int32_t)		(soo_rc)		CRT_VAR)

CRT_RPC_DECLARE(rsvc_stop, DAOS_ISEQ_RSVC_STOP, DAOS_OSEQ_RSVC_STOP)

#endif /* __RSVC_RPC_H__ */
