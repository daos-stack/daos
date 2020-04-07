/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#define DAOS_RSVC_VERSION 1
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
	((uint32_t)		(sai_flags)		CRT_VAR) \
	((uint64_t)		(sai_size)		CRT_VAR) \
	((d_rank_list_t)	(sai_ranks)		CRT_PTR)

#define DAOS_OSEQ_RSVC_START /* output fields (rc: err count) */ \
	((int32_t)		(sao_rc)		CRT_VAR) \
	((int32_t)		(sao_rc_errval)		CRT_VAR)


CRT_RPC_DECLARE(rsvc_start, DAOS_ISEQ_RSVC_START, DAOS_OSEQ_RSVC_START)

#define DAOS_ISEQ_RSVC_STOP /* input fields */			 \
	((d_iov_t)		(soi_svc_id)		CRT_VAR) \
	((uint32_t)		(soi_class)		CRT_VAR) \
	((uint32_t)		(soi_flags)		CRT_VAR) \
	((d_rank_list_t)	(soi_ranks)		CRT_PTR)

#define DAOS_OSEQ_RSVC_STOP /* output fields */			 \
	((int32_t)		(soo_rc)		CRT_VAR)

CRT_RPC_DECLARE(rsvc_stop, DAOS_ISEQ_RSVC_STOP, DAOS_OSEQ_RSVC_STOP)

#endif /* __RSVC_RPC_H__ */
