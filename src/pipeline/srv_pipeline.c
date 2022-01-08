/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <math.h>
#include <daos/rpc.h>
#include <daos_srv/container.h>
#include <daos_srv/object.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include "daos_api.h"
#include "pipeline_rpc.h"
#include "pipeline_internal.h"


static int
enum_pack_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	int	rc;

	switch(type)
	{
	case VOS_ITER_DKEY:
		rc = ds_obj_fill_key(ih, entry, cb_arg, type);
		break;
	default:
		D_ASSERTF(false, "unknown/unsupported type %d\n", type);
		rc = -DER_INVAL;
	}

	return rc;
}

static int
pipeline_list_dkey(daos_handle_t vos_coh, daos_unit_oid_t oid,
		   struct vos_iter_anchors *anchors, daos_epoch_range_t epr,
		   daos_key_desc_t *kds, d_sg_list_t *sgl_keys)
{
	int			rc;
	int			type;
	struct dss_enum_arg	enum_arg = { 0 };
	vos_iter_param_t	param = { 0 };

	param.ip_hdl		= vos_coh;
	param.ip_oid		= oid;
	param.ip_epr.epr_lo	= epr.epr_lo;
	param.ip_epr.epr_hi	= epr.epr_hi;
	/* items show epoch is <= epr_hi. For range, use VOS_IT_EPC_RE */
	param.ip_epc_expr	= VOS_IT_EPC_LE;

	enum_arg.sgl		= sgl_keys;
	enum_arg.sgl_idx	= 0;
	enum_arg.kds		= kds;
	enum_arg.kds_cap	= 1; /* TODO: Does it work to get more than one? */
	enum_arg.kds_len	= 0;
	/* TODO: Set enum_arg.csummer !  Figure out how checksum works */

	type			= VOS_ITER_DKEY;

	rc = vos_iterate(&param, type, false, anchors, enum_pack_cb, NULL,
			 &enum_arg, NULL);
	D_DEBUG(DB_IO, "enum type %d rc "DF_RC"\n", type, DP_RC(rc));

	return rc;
}

static void
pipeline_compile(daos_pipeline_t *pipeline)
{
	return;
}

static void
pipeline_aggregations_init(daos_pipeline_t *pipeline, d_sg_list_t *sgl_agg)
{
	uint32_t		i;
	double			*buf;
	daos_filter_part_t	*part;
	char			*part_type;
	size_t			part_type_s;

	for (i = 0; i < pipeline->num_aggr_filters; i++)
	{
		part      = pipeline->aggr_filters[i]->parts[0];
		buf       = (double *) sgl_agg[i].sg_iovs->iov_buf;
		part_type   = (char *) part->part_type.iov_buf;
		part_type_s = part->part_type.iov_len;

		if (!strncmp(part_type, "DAOS_FILTER_FUNC_MAX", part_type_s))
		{
			*buf = -INFINITY;
		}
		else if (!strncmp(part_type, "DAOS_FILTER_FUNC_MIN", part_type_s))
		{
			*buf = INFINITY;
		}
		else
		{
			*buf = 0;
		}
	}
}

int
ds_pipeline_run(daos_handle_t vos_coh, daos_unit_oid_t oid,
		daos_pipeline_t pipeline, daos_epoch_range_t epr, uint64_t flags,
		daos_key_t *dkey, uint32_t *nr_iods, daos_iod_t *iods,
		daos_anchor_t *anchor, uint32_t *nr_kds, daos_key_desc_t *kds,
		d_sg_list_t *sgl_keys, d_sg_list_t *sgl_recx,
		d_sg_list_t *sgl_agg)
{
	int				rc;
	uint32_t			i;
	daos_key_desc_t			kds_iter;
	uint32_t			nr_kds_pass;
	d_iov_t				keys_iov_iter;
	d_sg_list_t			sgl_keys_iter;
	char				**bufs;
	d_iov_t				*recx_iov_iter;
	d_sg_list_t			*sgl_recx_iter;
	struct vos_iter_anchors		anchors;


	rc = d_pipeline_check(&pipeline);
	if (rc != 0)
	{
		D_GOTO(exit, rc);	/** bad pipeline */
	}
	if (pipeline.version != 1)
	{
		D_GOTO(exit, rc = -DER_MISMATCH);	/** wrong version */
	}
	if (daos_anchor_is_eof(anchor))
	{
		D_GOTO(exit, rc = 0);	/** no more rows */
	}
	if (*nr_iods == 0)
	{
		D_GOTO(exit, rc = 0);	/** nothing to return */
	}
	if (*nr_kds == 0 && pipeline.num_aggr_filters == 0)
	{
		D_GOTO(exit, rc = 0);	/** nothing to return */
	}

	sgl_keys_iter.sg_iovs = &keys_iov_iter;
	D_ALLOC_ARRAY(recx_iov_iter, *nr_iods);
	D_ALLOC_ARRAY(sgl_recx_iter, *nr_iods);
	D_ALLOC_ARRAY(bufs, *nr_iods);
	if (recx_iov_iter == NULL || sgl_recx_iter == NULL || bufs == NULL)
	{
		D_GOTO(exit, rc = -DER_NOMEM);
	}
	bzero(bufs, sizeof(*bufs)*(*nr_iods));

	/** -- Init all aggregation counters. */
	pipeline_aggregations_init(&pipeline, sgl_agg);

	/** -- "compile" pipeline */
	pipeline_compile(&pipeline);

	/**
	 *  -- Iterating over dkeys and doing filtering and aggregation. The
	 *     variable nr_kds_pass stores the number of dkeys in total that
	 *     pass the filter.
	 */

	nr_kds_pass			= 0;
	bzero(&anchors, sizeof(anchors));
	anchors.ia_reprobe_dkey		= 1;
	anchors.ia_dkey			= *anchor;

	while (!daos_anchor_is_eof(&anchors.ia_dkey))
	{
		if (pipeline.num_aggr_filters == 0 &&
			nr_kds_pass == *nr_kds)
		{
			break; /** all records read */
		}

		if (bufs[0] == NULL)
		{
			for (i = 0; i < *nr_iods; i++)
			{
				size_t iov_buf_len = sgl_recx[nr_kds_pass*i].sg_iovs->iov_buf_len;

				D_ALLOC(bufs[i], iov_buf_len);
				if (bufs[i] == NULL)
				{
					D_GOTO(exit, rc = -DER_NOMEM);
				}
				d_iov_set(&recx_iov_iter[i], bufs[i], iov_buf_len);
				sgl_recx_iter[i].sg_nr = sgl_recx[nr_kds_pass*i].sg_nr;
				sgl_recx_iter[i].sg_nr_out = sgl_recx[nr_kds_pass*i].sg_nr_out;
				sgl_recx_iter[i].sg_iovs = &recx_iov_iter[i];
			}
		}

		rc = pipeline_list_dkey(vos_coh, oid, &anchors, epr, &kds_iter,
					&sgl_keys_iter);



	}

exit:
	return rc;
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

	rc = ds_pipeline_run(vos_coh, pri->pri_oid, pri->pri_pipe, pri->pri_epr,
			     pri->pri_flags, &pri->pri_dkey, &nr_iods,
			     pri->pri_iods.iods, &pri->pri_anchor, &nr_kds, kds,
			     sgl_keys, sgl_recx, sgl_aggr);
	if (rc != 0)
	{
		D_GOTO(exit, rc);
	}

exit:
	pro->pro_pad32_1	= 0 + pri->pri_target; // TODO: delete this

	// TODO: Free stuff

	pro->pro_ret = rc;
	rc = crt_reply_send(rpc);
	if (rc != 0)
	{
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
	}
}
