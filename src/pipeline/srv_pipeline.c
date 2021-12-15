/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/rpc.h>
#include "pipeline_rpc.h"
#include "pipeline_internal.h"


void
ds_pipeline_run_handler(crt_rpc_t *rpc)
{
	struct pipeline_run_in		*pri;
	struct pipeline_run_out		*pro;
	int				rc;
	daos_pipeline_t			pipeline;

	pri	= crt_req_get(rpc);
	D_ASSERT(pri != NULL);
	pro	= crt_reply_get(rpc);
	D_ASSERT(pro != NULL);

	pipeline = pri->pri_pipe;

	fprintf(stdout, "(ds_pipeline_run_handler) pipeline.version = %lu\n", pipeline.version);
	fflush(stdout);
	fprintf(stdout, "(ds_pipeline_run_handler) pipeline.num_filters = %u\n", pipeline.num_filters);
	fflush(stdout);
	fprintf(stdout, "(ds_pipeline_run_handler) pipeline.filters = %p\n", pipeline.filters);
	fflush(stdout);
	fprintf(stdout, "(ds_pipeline_run_handler) pipeline.num_aggr_filters = %u\n", pipeline.num_aggr_filters);
	fflush(stdout);
	fprintf(stdout, "(ds_pipeline_run_handler) pipeline.aggr_filters = %p\n", pipeline.aggr_filters);
	fflush(stdout);

	pro->pro_pong	= 0 + pri->pri_target;
	pro->pro_ret	= 0;

	rc = crt_reply_send(rpc);
	if (rc != 0)
	{
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
	}
}
