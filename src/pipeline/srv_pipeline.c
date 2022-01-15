/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <math.h>
#include <daos/rpc.h>
#include <daos_srv/container.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/vos.h>
#include <daos_srv/bio.h>
#include "daos_api.h"
#include "pipeline_rpc.h"
#include "pipeline_internal.h"


struct pipeline_enum_arg {
	d_iov_t			dkey;
	bool			dkey_set;
	daos_iod_t		*iods;
	uint32_t		nr_iods;
	d_sg_list_t		*sgl_recx;
	int			akey_idx;
	iter_copy_data_cb_t	copy_data_cb;
};


#if 0
static int
pipeline_obj_fetch(daos_handle_t vos_coh, daos_unit_oid_t oid,
		   daos_epoch_t epoch, daos_key_t *dkey, unsigned int nr,
		   daos_iod_t *iods)
{
	int			rc;
	daos_handle_t		ioh;
	struct vos_io_context	*ioc;
	daos_size_t		size;

	rc = vos_fetch_begin(vos_coh, oid, epoch, dkey, nr, iods, 0, NULL,
	
	// TODO: fetch data pointer here

			     &ioh, NULL);
	rc = vos_fetch_end(ioh, &size, rc);

	return rc;
}
#endif

static int
enum_pack_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	int				rc;
	struct pipeline_enum_arg	*pipe_arg = cb_arg;
	d_sg_list_t			*sgls;
	d_iov_t				*iov;
	uint32_t			i;

	switch(type)
	{
	case VOS_ITER_AKEY:
		D_ASSERT(pipe_arg->akey_idx < 0);
		D_ASSERT(entry->ie_key.iov_len > 0);

		for (i = 0; i < pipe_arg->nr_iods; i++)
		{
			if (pipe_arg->iods[i].iod_name.iov_len !=
				entry->ie_key.iov_len)
			{
				continue;
			}
			if (memcmp(pipe_arg->iods[i].iod_name.iov_buf,
				   entry->ie_key.iov_buf,
				   entry->ie_key.iov_len) == 0)
			{
				pipe_arg->akey_idx = i;
				break;
			}
		}
		break;
	case VOS_ITER_DKEY:
		if (pipe_arg->dkey_set == true)
		{
			return 1;
		}
		if (entry->ie_key.iov_len > 0)
		{
			pipe_arg->dkey		= entry->ie_key;
			pipe_arg->dkey_set	= true;
		}
		break;
	case VOS_ITER_SINGLE:
		D_ASSERT(pipe_arg->copy_data_cb != NULL);

		if (pipe_arg->akey_idx < 0)
		{
			break; /** akey is not in iods passed by client */
		}

		sgls	= pipe_arg->sgl_recx;
		iov	= sgls[pipe_arg->akey_idx].sg_iovs;

		rc = pipe_arg->copy_data_cb(ih, entry, iov);
		if (rc == 0)
		{
			sgls[pipe_arg->akey_idx].sg_nr_out = 1;
			pipe_arg->akey_idx		   = -1;
		}

		break;
	default:
		D_ASSERTF(false, "unknown/unsupported type %d\n", type);
		return -DER_INVAL;
	}

	return 0;
}

static int
pipeline_fetch_record(daos_handle_t vos_coh, daos_unit_oid_t oid,
		      struct vos_iter_anchors *anchors, daos_epoch_range_t epr,
		      daos_iod_t *iods, uint32_t nr_iods, d_iov_t *d_key,
		      d_sg_list_t *sgl_recx)
{
	int			rc;
	int			type;
	struct pipeline_enum_arg	enum_arg = { 0 };
	vos_iter_param_t	param = { 0 };

	param.ip_hdl		= vos_coh;
	param.ip_oid		= oid;
	param.ip_epr.epr_lo	= epr.epr_lo;
	param.ip_epr.epr_hi	= epr.epr_hi;
	/* items show epoch is <= epr_hi. For range, use VOS_IT_EPC_RE */
	param.ip_epc_expr	= VOS_IT_EPC_LE;

	/* TODO: Set enum_arg.csummer !  Figure out how checksum works */

	type			= VOS_ITER_DKEY;
	enum_arg.iods		= iods;
	enum_arg.nr_iods	= nr_iods;
	enum_arg.sgl_recx	= sgl_recx;
	enum_arg.akey_idx	= -1;
	enum_arg.copy_data_cb	= vos_iter_copy;

	rc = vos_iterate(&param, type, true, anchors, enum_pack_cb, NULL,
			 &enum_arg, NULL);
	D_DEBUG(DB_IO, "enum type %d rc "DF_RC"\n", type, DP_RC(rc));
	if (rc < 0)
	{
		return rc;
	}
	if (enum_arg.dkey_set == true)
	{
		*d_key	= enum_arg.dkey;
		return 0;
	}

	return 1;
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
	uint32_t			nr_kds_pass;
	d_iov_t				d_key_iter;
	d_sg_list_t			*sgl_recx_iter;
	uint32_t			sgl_recx_idx;
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
	sgl_recx_idx			= 0;
	bzero(&anchors, sizeof(anchors));
	anchors.ia_dkey			= *anchor;

	while (!daos_anchor_is_eof(&anchors.ia_dkey))
	{
		if (pipeline.num_aggr_filters == 0 &&
			nr_kds_pass == *nr_kds)
		{
			break; /** all records read */
		}

		/** -- fetching record */

		sgl_recx_iter = &sgl_recx[sgl_recx_idx*(*nr_iods)];
		rc = pipeline_fetch_record(vos_coh, oid, &anchors, epr, iods,
					   *nr_iods, &d_key_iter,
					   sgl_recx_iter);
		if (rc < 0)
		{
			D_GOTO(exit, rc);
		}
		if (rc == 1)
		{
			continue; /** nothing returned; no more records? */
		}

		printf("dkey = %.*s\n", (int)d_key_iter.iov_len, (char *)d_key_iter.iov_buf);
		fflush(stdout);
		for (i = 0; i < (*nr_iods)-1; i++)
		{
			printf("k:%.*s v:%.*s  ", (int)iods[i].iod_name.iov_len,
						  (char *)iods[i].iod_name.iov_buf,
						  (int)sgl_recx_iter[i].sg_iovs->iov_len,
						  (char *)sgl_recx_iter[i].sg_iovs->iov_buf);
			fflush(stdout);
		}
		printf("k:%.*s v:%d  ", (int)iods[i].iod_name.iov_len,
					  (char *)iods[i].iod_name.iov_buf,
					  *((int *)sgl_recx_iter[i].sg_iovs->iov_buf));
		fflush(stdout);
		printf("\n");
		fflush(stdout);

		break;

		/** this is here for the case where dkey is provided by the
		 *  user. */
		/*
		rc = pipeline_obj_fetch(vos_coh, oid, epr.epr_hi,
					sgl_keys_iter.sg_iovs, *nr_iods, iods);
		if (rc != 0)
		{
			D_GOTO(exit, rc);
		}*/

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
if (pri->pri_target)
{
	D_GOTO(exit, rc = 0);
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
