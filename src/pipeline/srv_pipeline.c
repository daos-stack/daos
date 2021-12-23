/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/rpc.h>
#include "pipeline_rpc.h"
#include "pipeline_internal.h"


int
ds_pipeline_run(daos_handle_t coh, daos_handle_t oh, daos_pipeline_t pipeline,
		struct dtx_id dti, uint64_t flags, daos_key_t *dkey,
		uint32_t *nr_iods, daos_iod_t *iods, daos_anchor_t *anchor,
		uint32_t *nr_kds, daos_key_desc_t *kds, d_sg_list_t *sgl_keys,
		d_sg_list_t *sgl_recx, d_sg_list_t *sgl_agg)
{
	return 0;
}

void
ds_pipeline_run_handler(crt_rpc_t *rpc)
{
	struct pipeline_run_in		*pri;
	struct pipeline_run_out		*pro;
	int				rc;
	daos_handle_t			coh;
	daos_handle_t			oh;
	daos_key_desc_t			*kds = NULL;
	d_sg_list_t			*sgl_keys = NULL;
	d_sg_list_t			*sgl_recx = NULL;
	d_sg_list_t			*sgl_agg = NULL;
	

	pri	= crt_req_get(rpc);
	D_ASSERT(pri != NULL);
	pro	= crt_reply_get(rpc);
	D_ASSERT(pro != NULL);

	pro->pro_pad32_1	= 0 + pri->pri_target;
	pro->pro_ret		= 0;

	rc = ds_pipeline_run(coh, oh, pri->pri_pipe, pri->pri_dti,
			     pri->pri_flags, &pri->pri_dkey, &pri->pri_nr_iods,
			     pri->pri_iods.iods, &pri->pri_anchor,
			     &pri->pri_nr_kds, kds, sgl_keys, sgl_recx,
			     sgl_agg);
	if (rc != 0)
	{
		D_ERROR("Error running ds_pipeline_run()\n");
	}
	rc = crt_reply_send(rpc);
	if (rc != 0)
	{
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
	}
}
