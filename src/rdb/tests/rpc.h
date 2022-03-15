/**
 * (C) Copyright 2017-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef RDB_TESTS_RPC_H
#define RDB_TESTS_RPC_H

#include <daos/rsvc.h>

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_RDBT_VERSION 3
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define RDBT_PROTO_CLI_RPC_LIST						\
	X(RDBT_INIT,							\
		0, &CQF_rdbt_init,					\
		rdbt_init_handler, NULL),				\
	X(RDBT_FINI,							\
		0, &CQF_rdbt_fini,					\
		rdbt_fini_handler, NULL),				\
	X(RDBT_PING,							\
		0, &CQF_rdbt_ping,					\
		rdbt_ping_handler, NULL),				\
	X(RDBT_CREATE,							\
		0, &CQF_rdbt_create,					\
		rdbt_create_handler, NULL),				\
	X(RDBT_DESTROY,							\
		0, &CQF_rdbt_destroy,					\
		rdbt_destroy_handler, NULL),				\
	X(RDBT_TEST,							\
		0, &CQF_rdbt_test,					\
		rdbt_test_handler, NULL),				\
	X(RDBT_REPLICAS_ADD,						\
		0, &CQF_rdbt_replicas_add,				\
		rdbt_replicas_add_handler, NULL),			\
	X(RDBT_REPLICAS_REMOVE,						\
		0, &CQF_rdbt_replicas_remove,				\
		rdbt_replicas_remove_handler, NULL),			\
	X(RDBT_START_ELECTION,						\
		0, &CQF_rdbt_start_election,				\
		rdbt_start_election_handler, NULL),			\
	X(RDBT_DESTROY_REPLICA,						\
		0, &CQF_rdbt_destroy_replica,				\
		rdbt_destroy_replica_handler, NULL),			\
	X(RDBT_DICTATE,							\
		0, &CQF_rdbt_dictate,					\
		rdbt_dictate_handler, NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum rdbt_operation {
	RDBT_PROTO_CLI_RPC_LIST,
	RDBT_PROTO_CLI_COUNT,
	RDBT_PROTO_CLI_LAST = RDBT_PROTO_CLI_COUNT - 1,
};

#undef X

enum rdbt_membership_op {
	RDBT_MEMBER_NOOP = 0,
	RDBT_MEMBER_RESIGN,
	RDBT_MEMBER_CAMPAIGN
};

static inline const char *rdbt_membership_opname(enum rdbt_membership_op op)
{
	switch (op) {
	case RDBT_MEMBER_NOOP:
		return "MEMBER_NOOP";
	case RDBT_MEMBER_RESIGN:
		return "MEMBER_RESIGN";
	case RDBT_MEMBER_CAMPAIGN:
		return "MEMBER_CAMPAIGN";
	default:
		return "INVALID MEMBERSHIP op";
	}
}
extern struct crt_proto_format rdbt_proto_fmt;

#define DAOS_ISEQ_RDBT_INIT_OP	/* input fields */		 \
	((uuid_t)		(tii_uuid)		CRT_VAR) \
	((uint32_t)		(tii_nreplicas)		CRT_VAR)

#define DAOS_OSEQ_RDBT_INIT_OP	/* output fields */		 \
	((int32_t)		(tio_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_init, DAOS_ISEQ_RDBT_INIT_OP, DAOS_OSEQ_RDBT_INIT_OP)

#define DAOS_ISEQ_RDBT_FINI_OP	/* input fields */

#define DAOS_OSEQ_RDBT_FINI_OP	/* output fields */		 \
	((int32_t)		(tfo_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_fini, DAOS_ISEQ_RDBT_FINI_OP, DAOS_OSEQ_RDBT_FINI_OP)

#define DAOS_ISEQ_RDBT_PING_OP	/* input fields (none) */

#define DAOS_OSEQ_RDBT_PING_OP	/* output fields */		 \
	((struct rsvc_hint)	(tpo_hint)		CRT_VAR) \
	((int32_t)		(tpo_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_ping, DAOS_ISEQ_RDBT_PING_OP, DAOS_OSEQ_RDBT_PING_OP)

#define DAOS_ISEQ_RDBT_CREATE_OP	/* input fields (none) */

#define DAOS_OSEQ_RDBT_CREATE_OP	/* output fields */	 \
	((struct rsvc_hint)	(tco_hint)		CRT_VAR) \
	((int32_t)		(tco_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_create, DAOS_ISEQ_RDBT_CREATE_OP,
		DAOS_OSEQ_RDBT_CREATE_OP)

#define DAOS_ISEQ_RDBT_DESTROY_OP	/* input fields (none) */

#define DAOS_OSEQ_RDBT_DESTROY_OP	/* output fields */	 \
	((struct rsvc_hint)	(tdo_hint)		CRT_VAR) \
	((int32_t)		(tdo_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_destroy, DAOS_ISEQ_RDBT_DESTROY_OP,
		DAOS_OSEQ_RDBT_DESTROY_OP)

#define DAOS_ISEQ_RDBT_TEST_OP	/* input fields */		 \
	((int32_t)		(tti_update)		CRT_VAR) \
	((int32_t)		(tti_memb_op)		CRT_VAR) \
	((uint64_t)		(tti_key)		CRT_VAR) \
	((uint64_t)		(tti_val)		CRT_VAR)

#define DAOS_OSEQ_RDBT_TEST_OP	/* output fields */		 \
	((struct rsvc_hint)	(tto_hint)		CRT_VAR) \
	((uint64_t)		(tto_val)		CRT_VAR) \
	((int32_t)		(tto_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_test, DAOS_ISEQ_RDBT_TEST_OP, DAOS_OSEQ_RDBT_TEST_OP)

#define DAOS_ISEQ_RDBT_MEMBERSHIP /* input fields */		 \
	((d_rank_list_t)	(rtmi_ranks)		CRT_PTR)

#define DAOS_OSEQ_RDBT_MEMBERSHIP /* output fields */		 \
	((struct rsvc_hint)	(rtmo_hint)		CRT_VAR) \
	((d_rank_list_t)	(rtmo_failed)		CRT_PTR) \
	((int32_t)		(rtmo_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_replicas_add, DAOS_ISEQ_RDBT_MEMBERSHIP,
		DAOS_OSEQ_RDBT_MEMBERSHIP)
CRT_RPC_DECLARE(rdbt_replicas_remove, DAOS_ISEQ_RDBT_MEMBERSHIP,
		DAOS_OSEQ_RDBT_MEMBERSHIP)

#define DAOS_ISEQ_RDBT_STARTSTOP /* input fields */		 \
	((d_rank_list_t)	(rts_ranks)		CRT_PTR)

#define DAOS_OSEQ_RDBT_STARTSTOP /* output fields */		 \
	((int32_t)		(rts_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_replicas_start, DAOS_ISEQ_RDBT_STARTSTOP,
		DAOS_OSEQ_RDBT_STARTSTOP)
CRT_RPC_DECLARE(rdbt_replicas_stop, DAOS_ISEQ_RDBT_STARTSTOP,
		DAOS_OSEQ_RDBT_STARTSTOP)

#define DAOS_ISEQ_RDBT_START_ELECTION /* input fields (none) */

#define DAOS_OSEQ_RDBT_START_ELECTION /* output fields */	\
	((int32_t)		(rtse_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_start_election, DAOS_ISEQ_RDBT_START_ELECTION,
		DAOS_OSEQ_RDBT_START_ELECTION)

#define DAOS_ISEQ_RDBT_DESTROY_REPLICA /* input fields (none) */

#define DAOS_OSEQ_RDBT_DESTROY_REPLICA /* output fields */	\
	((int32_t)		(reo_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_destroy_replica, DAOS_ISEQ_RDBT_DESTROY_REPLICA,
		DAOS_OSEQ_RDBT_DESTROY_REPLICA)

#define DAOS_ISEQ_RDBT_DICTATE /* input fields (none) */

#define DAOS_OSEQ_RDBT_DICTATE /* output fields */	\
	((int32_t)		(rto_rc)		CRT_VAR)

CRT_RPC_DECLARE(rdbt_dictate, DAOS_ISEQ_RDBT_DICTATE,
		DAOS_OSEQ_RDBT_DICTATE)

#endif /* RDB_TESTS_RPC_H */
