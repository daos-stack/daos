/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <daos/common.h>
#include "pipeline_rpc.h"

int
dc_pipeline_init(void)
{
	int rc;

	rc = daos_rpc_register(&pipeline_proto_fmt, PIPELINE_PROTO_CLI_COUNT, NULL,
			       DAOS_PIPELINE_MODULE);
	if (rc != 0)
		D_ERROR("failed to register DAOS pipeline RPCs: " DF_RC "\n", DP_RC(rc));
	return rc;
}

void
dc_pipeline_fini(void)
{
	int rc;

	rc = daos_rpc_unregister(&pipeline_proto_fmt);
	if (rc != 0)
		D_ERROR("failed to unregister DAOS pipeline RPCs: "DF_RC"\n", DP_RC(rc));
}
