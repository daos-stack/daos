/**
 * (C) Copyright 2017 Intel Corporation.
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

#ifndef RDB_TESTS_RPC_H
#define RDB_TESTS_RPC_H

/*
 * RPC operation codes
 *
 * These are for daos_rpc::dr_opc and DAOS_RPC_OPCODE(opc, ...) rather than
 * crt_req_create(..., opc, ...). See src/include/daos/rpc.h.
 */
#define DAOS_RDBT_VERSION 1
/* LIST of internal RPCS in form of:
 * OPCODE, flags, FMT, handler, corpc_hdlr,
 */
#define RDBT_PROTO_CLI_RPC_LIST						\
	X(RDBT_INIT,							\
		0, &DQF_RDBT_INIT,					\
		rdbt_init_handler, NULL),				\
	X(RDBT_FINI,							\
		0, &DQF_RDBT_FINI,					\
		rdbt_fini_handler, NULL),				\
	X(RDBT_TEST,							\
		0, &DQF_RDBT_TEST,					\
		rdbt_test_handler, NULL)

/* Define for RPC enum population below */
#define X(a, b, c, d, e) a

enum rdbt_operation {
	RDBT_PROTO_CLI_RPC_LIST,
	RDBT_PROTO_CLI_COUNT,
	RDBT_PROTO_CLI_LAST = RDBT_PROTO_CLI_COUNT - 1,
};

#undef X

extern struct crt_proto_format rdbt_proto_fmt;

struct rdbt_init_in {
	uuid_t		tii_uuid;
	uint32_t	tii_nreplicas;
};

struct rdbt_init_out {
	int	tio_rc;
};

struct rdbt_fini_in {
};

struct rdbt_fini_out {
	int	tfo_rc;
};

struct rdbt_test_in {
	int	tti_update;
};

struct rdbt_test_out {
	int	tto_rc;
};

#endif /* RDB_TESTS_RPC_H */
