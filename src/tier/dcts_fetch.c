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
 * dcts_fetch
 * Implements cross-tier fetching of objects and sub-objects (or will soon).
 **/

#include <daos_srv/daos_ct_srv.h>
#include <daos/transport.h>
#include "dct_rpc.h"


int
dcts_hdlr_fetch(dtp_rpc_t *rpc)
{

	struct tier_fetch_in *in = dtp_req_get(rpc);
	struct tier_fetch_out *out = dtp_reply_get(rpc);
	int  rc = 0;

	D_DEBUG(DF_TIERS, "dcts_fetch\n");
	D_DEBUG(DF_TIERS, "\tpool:"DF_UUIDF"\n", in->tfi_pool);
	D_DEBUG(DF_TIERS, "\tpool_hdl:"DF_UUIDF"\n", in->tfi_pool_hdl);
	D_DEBUG(DF_TIERS, "\tcont_id:"DF_UUIDF"\n", in->tfi_co_hdl);
	D_DEBUG(DF_TIERS, "\tepoch:"DF_U64"\n", in->tfi_ep);

	out->tfo_ret = 0;

	rc = dtp_reply_send(rpc);

	return rc;
}


