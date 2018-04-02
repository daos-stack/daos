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
 * dcts: Module Definitions
 *
 * dcts is the DCT server module/library. It exports the DCT RPC handlers and
 * the DCT server API. This file contains the definitions expected by server;
 * the DCT server API methods are exported directly where they are defined as
 * extern functions.
 */
#define D_LOGFAC	DD_FAC(tier)

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "rpc.h"
#include "srv_internal.h"
#include <daos_srv/daos_ct_srv.h>
#include <sys/types.h>

static int
ds_tier_init(void)
{
	ds_tier_init_vars();
	return 0;
}

static int
ds_tier_fini(void)
{
	return 0;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler tier_handlers[] = {
	{
		.dr_opc		= TIER_PING,
		.dr_hdlr	= ds_tier_ping_handler,
	}, {
		.dr_opc		= TIER_FETCH,
		.dr_hdlr	= ds_tier_fetch_handler,
	}, {
		.dr_opc		= TIER_BCAST_FETCH,
		.dr_hdlr	= ds_tier_fetch_bcast_handler,
	}, {
		.dr_opc		= TIER_CROSS_CONN,
		.dr_hdlr	= ds_tier_cross_conn_handler,
	}, {
		.dr_opc		= TIER_UPSTREAM_CONN,
		.dr_hdlr	= ds_tier_upstream_handler,
	}, {
		.dr_opc		= TIER_REGISTER_COLD,
		.dr_hdlr	= ds_tier_register_cold_handler,
	}, {
		.dr_opc		= TIER_BCAST_HDL,
		.dr_hdlr	= ds_tier_hdl_bcast_handler,
	}, {
		.dr_opc		= 0
	}
};

struct dss_module tier_module =  {
	.sm_name	= "tier",
	.sm_mod_id	= DAOS_TIER_MODULE,
	.sm_ver		= 1,
	.sm_facs	= 0, /* set it to DSS_FAC_LOAD_CLI to enable tier */
	.sm_init	= ds_tier_init,
	.sm_fini	= ds_tier_fini,
	.sm_cl_rpcs	= tier_rpcs,
	.sm_handlers	= tier_handlers,
};
