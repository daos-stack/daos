/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos_srv/daos_engine.h>
#include <daos/rpc.h>
#include "pipeline_rpc.h"

static int
pipeline_mod_init(void)
{
	return 0;
}

static int
pipeline_mod_fini(void)
{
	return 0;
}

#define X(a, b, c, d, e, f)	\
{				\
	.dr_opc		= a,	\
	.dr_hdlr	= d,	\
	.dr_corpc_ops	= e,	\
},

static struct daos_rpc_handler pipeline_handlers[] = {
	PIPELINE_PROTO_CLI_RPC_LIST
};

#undef X

struct dss_module pipeline_module = {
	.sm_name	= "pipeline",
	.sm_mod_id	= DAOS_PIPELINE_MODULE,
	.sm_ver		= DAOS_PIPELINE_VERSION,
	.sm_init	= pipeline_mod_init,
	.sm_fini	= pipeline_mod_fini,
	.sm_proto_fmt	= {&pipeline_proto_fmt},
	.sm_cli_count	= {PIPELINE_PROTO_CLI_COUNT},
	.sm_handlers	= {pipeline_handlers},
	.sm_proto_count	= 1,
};
