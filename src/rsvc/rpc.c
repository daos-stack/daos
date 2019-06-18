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
 * ds_rsvc: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(rsvc)

#include <daos/rpc.h>
#include "rpc.h"

CRT_RPC_DEFINE(rsvc_start, DAOS_ISEQ_RSVC_START, DAOS_OSEQ_RSVC_START)
CRT_RPC_DEFINE(rsvc_stop, DAOS_ISEQ_RSVC_STOP, DAOS_OSEQ_RSVC_STOP)

/* Define for cont_rpcs[] array population below.
 * See POOL_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format rsvc_proto_rpc_fmt[] = {
	RSVC_PROTO_SRV_RPC_LIST,
};

#undef X

struct crt_proto_format rsvc_proto_fmt = {
	.cpf_name  = "rsvc-proto",
	.cpf_ver   = DAOS_RSVC_VERSION,
	.cpf_count = ARRAY_SIZE(rsvc_proto_rpc_fmt),
	.cpf_prf   = rsvc_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_RSVC_MODULE, 0)
};
