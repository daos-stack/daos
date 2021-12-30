/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/rpc.h>
#include <daos_srv/container.h>
#include "pipeline_rpc.h"
#include "pipeline_internal.h"


int
ds_pipeline_run(daos_handle_t vos_coh, daos_unit_oid_t oid,
		daos_pipeline_t pipeline, struct dtx_id dti, uint64_t flags,
		daos_key_t *dkey, uint32_t *nr_iods, daos_iod_t *iods,
		daos_anchor_t *anchor, uint32_t *nr_kds, daos_key_desc_t *kds,
		d_sg_list_t *sgl_keys, d_sg_list_t *sgl_recx,
		d_sg_list_t *sgl_agg)
{
	return 0;
}

void
ds_pipeline_run_handler(crt_rpc_t *rpc)
{
	struct pipeline_run_in		*pri;
	struct pipeline_run_out		*pro;
	int				rc;
	struct ds_cont_hdl		*coh;
	struct ds_cont_child		*coc = NULL;
	daos_handle_t			vos_coh;
	uint32_t			i;
	uint32_t			nr_iods;
	daos_key_desc_t			*kds;
	uint32_t			nr_kds;
	d_sg_list_t			*sgl_keys = NULL;
	d_sg_list_t			*sgl_recx = NULL;
	d_sg_list_t			*sgl_aggr = NULL;
	

	pri	= crt_req_get(rpc);
	D_ASSERT(pri != NULL);
	pro	= crt_reply_get(rpc);
	D_ASSERT(pro != NULL);
	D_DEBUG(DB_IO, "flags = "DF_U64"\n", pri->pri_flags);

	/** -- get vos container handle */

	rc = ds_cont_find_hdl(pri->pri_pool_uuid, pri->pri_co_hdl, &coh);
	if (rc != 0)
	{
		D_GOTO(exit, rc);
	}
	coc		= coh->sch_cont;
	vos_coh		= coc->sc_hdl;

	/** -- alloc output buffers */

	nr_iods		= pri->pri_iods.nr;
	nr_kds		= pri->pri_sgl_keys.nr;
	
	D_ALLOC_ARRAY(kds, nr_kds);
	if (kds == NULL)
	{
		D_GOTO(exit, rc = -DER_NOMEM);
	}
	sgl_keys	= pri->pri_sgl_keys.sgls;
	for (i = 0; i < nr_kds; i++)
	{
		D_ALLOC_ARRAY(sgl_keys[i].sg_iovs->iov_buf,
			      sgl_keys[i].sg_iovs->iov_len);
		if (sgl_keys[i].sg_iovs->iov_buf == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
	}
	sgl_recx	= pri->pri_sgl_recx.sgls;
	for (i = 0; i < nr_kds*nr_iods; i++)
	{
		D_ALLOC_ARRAY(sgl_recx[i].sg_iovs->iov_buf,
			      sgl_recx[i].sg_iovs->iov_len);
		if (sgl_recx[i].sg_iovs->iov_buf == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
	}
	sgl_aggr	= pri->pri_sgl_aggr.sgls;
	for (i = 0; i < pri->pri_pipe.num_aggr_filters; i++)
	{
		D_ALLOC_ARRAY(sgl_aggr[i].sg_iovs->iov_buf,
			      sgl_aggr[i].sg_iovs->iov_len);
		if (sgl_aggr[i].sg_iovs->iov_buf == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
	}

	/** -- calling pipeline run */

	rc = ds_pipeline_run(vos_coh, pri->pri_oid, pri->pri_pipe, pri->pri_dti,
			     pri->pri_flags, &pri->pri_dkey, &nr_iods,
			     pri->pri_iods.iods, &pri->pri_anchor, &nr_kds, kds,
			     sgl_keys, sgl_recx, sgl_aggr);
	if (rc != 0)
	{
		D_GOTO(exit, rc);
	}

	pro->pro_pad32_1	= 0 + pri->pri_target; // TODO: delete this

exit:

	// TODO: Free stuff

	pro->pro_ret = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
	{
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
	}
}
