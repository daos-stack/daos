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
/*
 * tier_ping
 * Implements the ping function of srv_internal.h.
 **/
#define D_LOGFAC	DD_FAC(tier)

#include <daos_srv/daos_ct_srv.h>
#include <daos/rpc.h>
#include "rpc.h"

void
ds_tier_ping_handler(crt_rpc_t *rpc)
{
	struct tier_ping_in *in = crt_req_get(rpc);
	struct tier_ping_out *out = crt_reply_get(rpc);

	D_DEBUG(DF_TIERS, "receive, ping %d.\n", rpc->cr_opc);

	out->ping_out = in->ping_in + 1;

	crt_reply_send(rpc);

	D_DEBUG(DF_TIERS, "ping ret val, 1 higher than input: %d\n",
		out->ping_out);
}


