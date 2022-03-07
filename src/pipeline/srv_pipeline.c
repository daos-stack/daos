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
	int				rc = 0;
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
	{
		void *ptr;

		D_ASSERT(pipe_arg->copy_data_cb != NULL);

		if (pipe_arg->akey_idx < 0)
		{
			break; /** akey is not in iods passed by client */
		}

		sgls	= pipe_arg->sgl_recx;
		iov	= sgls[pipe_arg->akey_idx].sg_iovs;

		iov->iov_len = 0;

		if (entry->ie_rsize > iov->iov_buf_len)
		{
			D_REALLOC(ptr, iov->iov_buf,
				  iov->iov_buf_len, entry->ie_rsize);
			if (ptr == NULL)
			{
				return -DER_NOMEM;
			}
			iov->iov_buf     = ptr;
			iov->iov_buf_len = entry->ie_rsize;
		}

		rc = pipe_arg->copy_data_cb(ih, entry, iov);
		if (rc == 0)
		{
			sgls[pipe_arg->akey_idx].sg_nr_out = 1;
			pipe_arg->akey_idx		   = -1;
		}

		break;
	}
	default:
		D_ASSERTF(false, "unknown/unsupported type %d\n", type);
		return -DER_INVAL;
	}

	return rc;
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

int pipeline_aggregations(struct pipeline_compiled_t *pipe,
			  struct filter_part_run_t *args, d_iov_t *dkey,
			  d_sg_list_t *akeys, d_sg_list_t *sgl_agg)
{
	uint32_t 	i;
	int		rc = 0;

	args->dkey	= dkey;
	args->akeys	= akeys;

	for (i = 0; i < pipe->num_aggr_filters; i++)
	{
		args->part_idx	= 0;
		args->parts	= pipe->aggr_filters[i].parts;
		args->iov_aggr	= sgl_agg[i].sg_iovs;

		rc = args->parts[0].filter_func(args);
		if (rc != 0)
		{
			D_GOTO(exit, rc);
		}
	}

exit:
	return rc;
}

int pipeline_filters(struct pipeline_compiled_t *pipe,
		     struct filter_part_run_t *args, d_iov_t *dkey,
		     d_sg_list_t *akeys)
{
	uint32_t 	i;
	int		rc = 0;

	args->dkey	= dkey;
	args->akeys	= akeys;

	for (i = 0; i < pipe->num_filters; i++)
	{
		args->part_idx	= 0;
		args->parts	= pipe->filters[i].parts;

		rc = args->parts[0].filter_func(args);
		if (rc != 0)
		{
			D_GOTO(exit, rc);
		}
		if (args->log_out == false)
		{
			D_GOTO(exit, rc = 1);
		}
	}

exit:
	return rc;
}

// TODO: This code still assumes dkey!=NULL. The code for dkey=NULL has to be
// written
static int
ds_pipeline_run(daos_handle_t vos_coh, daos_unit_oid_t oid,
		daos_pipeline_t pipeline, daos_epoch_range_t epr, uint64_t flags,
		daos_key_t *dkey, uint32_t *nr_iods, daos_iod_t *iods,
		daos_anchor_t *anchor, uint32_t nr_kds, uint32_t *nr_kds_out,
		daos_key_desc_t **kds, d_sg_list_t **sgl_keys,
		uint32_t *nr_recx, d_sg_list_t **sgl_recx, d_sg_list_t *sgl_agg)
{
	int				rc;
	uint32_t			nr_kds_pass;
	d_iov_t				d_key_iter;
	daos_key_desc_t			*kds_;
	uint32_t			kds_alloc		= 0;
	d_sg_list_t			*sgl_keys_		= NULL;
	uint32_t			sgl_keys_alloc		= 0;
	d_sg_list_t			*sgl_recx_		= NULL;
	uint32_t			sgl_recx_alloc		= 0;
	d_iov_t				*d_key_ptr;
	d_sg_list_t			*sgl_recx_iter		= NULL;
	d_iov_t				*sgl_recx_iter_iovs	= NULL;
	uint32_t			i;
	uint32_t			j			= 0;
	uint32_t			sr_idx			= 0;
	struct vos_iter_anchors		anchors			= { 0 };
	struct pipeline_compiled_t	pipeline_compiled	= { 0 };
	struct filter_part_run_t	pipe_run_args		= { 0 };

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
	if (nr_kds == 0 && pipeline.num_aggr_filters == 0)
	{
		D_GOTO(exit, rc = 0);	/** nothing to return */
	}

	/** -- init all aggregation counters */

	pipeline_aggregations_init(&pipeline, sgl_agg);

	/** -- init pipe_run_args data struct */

	pipe_run_args.nr_iods			= *nr_iods;
	pipe_run_args.iods			= iods;

	/** -- "compile" pipeline */

	rc = pipeline_compile(&pipeline, &pipeline_compiled);
	if (rc != 0)
	{
		D_GOTO(exit, rc);	/** compilation failed. Bad pipeline? */
	}

	/** -- allocating space for sgl_recx_iter */

	D_ALLOC_ARRAY(sgl_recx_iter, *nr_iods);
	D_ALLOC_ARRAY(sgl_recx_iter_iovs, *nr_iods);
	if (sgl_recx_iter == NULL || sgl_recx_iter_iovs == NULL)
	{
		D_GOTO(exit, rc = -DER_NOMEM);
	}
	for (; j < *nr_iods; j++)
	{
		D_ALLOC(sgl_recx_iter_iovs[j].iov_buf, iods[j].iod_size);
		if (sgl_recx_iter_iovs[j].iov_buf == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		sgl_recx_iter_iovs[j].iov_len     = 0;
		sgl_recx_iter_iovs[j].iov_buf_len = iods[j].iod_size;

		sgl_recx_iter[j].sg_iovs   = &sgl_recx_iter_iovs[j];
		sgl_recx_iter[j].sg_nr     = 1;
		sgl_recx_iter[j].sg_nr_out = 0;
	}

	/**
	 *  -- Iterating over dkeys and doing filtering and aggregation. The
	 *     variable nr_kds_pass stores the number of dkeys in total that
	 *     pass the filter.
	 */

	nr_kds_pass		= 0;
	anchors.ia_dkey		= *anchor;

	while (!daos_anchor_is_eof(&anchors.ia_dkey))
	{
		if (pipeline.num_aggr_filters == 0 &&
			nr_kds_pass == nr_kds)
		{
			break; /** all records read */
		}

		/** -- fetching record */

		rc = pipeline_fetch_record(vos_coh, oid, &anchors, epr, iods,
					   *nr_iods, &d_key_iter,
					   sgl_recx_iter);

		if (rc < 0)
		{
			D_GOTO(exit, rc); /** error */
		}
		if (rc == 1)
		{
			continue; /** nothing returned; no more records? */
		}

		/** -- doing filtering... */

		rc = pipeline_filters(&pipeline_compiled, &pipe_run_args,
				      &d_key_iter, sgl_recx_iter);
		if (rc < 0)
		{
			D_GOTO(exit, rc); /** error */
		}
		if (rc == 1)
		{
			continue; /** record does not pass filters */
		}

		/** -- dkey+akey pass filters */

		nr_kds_pass++;

		/*{
			char *buf = (char *) sgl_recx_iter[0].sg_iovs->iov_buf;
			size_t sbuf = *((size_t *) &buf[8]);
			printf("name=%.*s\n", (int)sbuf, &buf[16]);
			sbuf= *((size_t *) &buf[37]);
			printf("owner=%.*s\n", (int)sbuf, &buf[45]);
			sbuf= *((size_t *) &buf[66]);
			printf("species=%.*s\n", (int)sbuf, &buf[74]);
			sbuf= *((size_t *) &buf[95]);
			printf("sex=%.*s\n", (int)sbuf, &buf[103]);
			printf("weight=%d\n", *((int *) &buf[104]));
			printf("r=%f\n", *((double *) &buf[108]));
			sbuf= *((size_t *) &buf[116]);
			printf("birth=%.*s\n", (int)sbuf, &buf[124]);
			fflush(stdout);
		}*/

		/** -- aggregations */

		rc = pipeline_aggregations(&pipeline_compiled, &pipe_run_args,
					   &d_key_iter, sgl_recx_iter, sgl_agg);
		if (rc < 0)
		{
			D_GOTO(exit, rc);
		}

		/**
		 * -- Returning matching records. We don't need to return all
		 *     matching records if aggregation is being performed: at
		 *     most one is returned.
		 */

		if (nr_kds == 0 ||
				(nr_kds > 0 &&
				pipeline.num_aggr_filters > 0 &&
				nr_kds_pass > 1))
		{
			continue;
		}

		/**
		 * saving record info
		 */

		/** dkey */
		D_REALLOC_ARRAY(kds_, *kds, nr_kds_pass-1, nr_kds_pass);
		if (kds_ == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		kds_alloc++;
		*kds = kds_;

		D_REALLOC_ARRAY(sgl_keys_, *sgl_keys,
				nr_kds_pass-1, nr_kds_pass);
		if (sgl_keys_ == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		sgl_keys_alloc++;
		*sgl_keys = sgl_keys_;

		D_ALLOC(d_key_ptr, sizeof(*d_key_ptr));
		sgl_keys_[nr_kds_pass-1].sg_iovs = d_key_ptr;
		if (d_key_ptr == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}

		d_key_ptr->iov_buf = NULL;
		D_ALLOC(d_key_ptr->iov_buf, d_key_iter.iov_buf_len);
		if (d_key_ptr->iov_buf == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		memcpy(d_key_ptr->iov_buf, d_key_iter.iov_buf,
		       d_key_iter.iov_len);
		d_key_ptr->iov_len	= d_key_iter.iov_len;
		d_key_ptr->iov_buf_len	= d_key_iter.iov_buf_len;

		sgl_keys_[nr_kds_pass-1].sg_nr		= 1;
		sgl_keys_[nr_kds_pass-1].sg_nr_out	= 1;
		kds_[nr_kds_pass-1].kd_key_len		= d_key_iter.iov_len;

		/** akeys */
		D_REALLOC_ARRAY(sgl_recx_, *sgl_recx,
				(nr_kds_pass-1) * (*nr_iods),
				nr_kds_pass * (*nr_iods));
		if (sgl_recx_ == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		sgl_recx_alloc += *nr_iods;
		*sgl_recx = sgl_recx_;

		for (i = 0; i < *nr_iods; i++, sr_idx++)
		{
			sgl_recx_[sr_idx].sg_iovs = NULL;
			D_ALLOC(sgl_recx_[sr_idx].sg_iovs,
				sizeof(*sgl_recx_[sr_idx].sg_iovs));
			if (sgl_recx_[sr_idx].sg_iovs == NULL)
			{
				D_GOTO(exit, rc = -DER_NOMEM);
			}

			sgl_recx_[sr_idx].sg_iovs->iov_buf = NULL;
			D_ALLOC(sgl_recx_[sr_idx].sg_iovs->iov_buf,
				sgl_recx_iter[i].sg_iovs->iov_buf_len);
			if (sgl_recx_[sr_idx].sg_iovs->iov_buf == NULL)
			{
				D_GOTO(exit, rc = -DER_NOMEM);
			}

			memcpy(sgl_recx_[sr_idx].sg_iovs->iov_buf,
			       sgl_recx_iter[i].sg_iovs->iov_buf,
			       sgl_recx_iter[i].sg_iovs->iov_len);

			sgl_recx_[sr_idx].sg_iovs->iov_buf_len =
					  sgl_recx_iter[i].sg_iovs->iov_buf_len;
			sgl_recx_[sr_idx].sg_iovs->iov_len =
					      sgl_recx_iter[i].sg_iovs->iov_len;
			sgl_recx_[sr_idx].sg_nr     = 1;
			sgl_recx_[sr_idx].sg_nr_out = 1;
			sgl_recx_iter[i].sg_nr_out = 0; /** to re-utilize */
		}
	}

	/**
	 * -- fixing averages: during aggregation, we don't know how many
	 *    records will pass the filters
	 */

	if (nr_kds_pass > 0)
	{
		pipeline_aggregations_fixavgs(&pipeline,
					      (double) nr_kds_pass,
					      sgl_agg);
	}

	/** -- backing up anchor */

	*anchor = anchors.ia_dkey;

	/** -- kds and recx returned */

	if (nr_kds > 0 && pipeline.num_aggr_filters == 0)
	{
		*nr_kds_out  = nr_kds_pass;
		*nr_recx     = (*nr_iods) * nr_kds_pass;
	}
	else if (nr_kds > 0 && pipeline.num_aggr_filters > 0 && nr_kds_pass > 0)
	{
		*nr_kds_out  = 1;
		*nr_recx     = *nr_iods;
	}
	else
	{
		*nr_kds_out  = 0;
		*nr_recx     = 0;
	}

	rc = 0;
exit:
	pipeline_compile_free(&pipeline_compiled);

	for (i = 0; i < j; i++)
	{
		D_FREE(sgl_recx_iter_iovs[i].iov_buf);
	}
	if (sgl_recx_iter_iovs != NULL)
	{
		D_FREE(sgl_recx_iter_iovs);
	}
	if (sgl_recx_iter != NULL)
	{
		D_FREE(sgl_recx_iter);
	}

	if (rc != 0)
	{
		uint32_t limit;

		if (kds_alloc > 0)
		{
			D_FREE(*kds);
		}

		if (sgl_keys_alloc > 0)
		{
			for (i = 0; i < sgl_keys_alloc; i++)
			{
				if (sgl_keys[0][i].sg_iovs != NULL)
				{
					if (sgl_keys[0][i].sg_iovs->iov_buf != NULL)
					{
						D_FREE(sgl_keys[0][i].sg_iovs->iov_buf);
					}
					D_FREE(sgl_keys[0][i].sg_iovs);
				}
			}		
			D_FREE(*sgl_keys);
		}

		if (sgl_recx_alloc > 0)
		{
			if (sr_idx == sgl_recx_alloc)
			{
				limit = sr_idx;
			}
			else
			{
				limit = sr_idx + 1;
			}
			for (i = 0; i < limit; i++)
			{
				if (sgl_recx[0][i].sg_iovs != NULL)
				{
					if (sgl_recx[0][i].sg_iovs->iov_buf != NULL)
					{
						D_FREE(sgl_recx[0][i].sg_iovs->iov_buf);
					}
					D_FREE(sgl_recx[0][i].sg_iovs);
				}
			}
			D_FREE(*sgl_recx);
		}
	}

	return rc;
}

void
ds_pipeline_run_handler(crt_rpc_t *rpc)
{
	struct pipeline_run_in		*pri;
	struct pipeline_run_out		*pro;
	int				rc;
	struct ds_cont_hdl		*coh;
	struct ds_cont_child		*coc		= NULL;
	daos_handle_t			vos_coh;
	uint32_t			i;
	uint32_t			nr_iods;
	daos_key_desc_t			*kds		= NULL;
	uint32_t			nr_kds;
	uint32_t			nr_kds_out	= 0;
	d_sg_list_t			*sgl_keys	= NULL;
	d_sg_list_t			*sgl_recx	= NULL;
	uint32_t			nr_recx		= 0;
	d_sg_list_t			*sgl_aggr	= NULL;

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

	/** --  */

	nr_iods		= pri->pri_iods.nr;
	nr_kds		= pri->pri_nr_kds;

	if (pri->pri_pipe.num_aggr_filters > 0)
	{
		D_ALLOC_ARRAY(sgl_aggr, pri->pri_pipe.num_aggr_filters);
		if (sgl_aggr == NULL)
		{
			D_GOTO(exit, rc = -DER_NOMEM);
		}
		for (i = 0; i < pri->pri_pipe.num_aggr_filters; i++)
		{
			D_ALLOC(sgl_aggr[i].sg_iovs,
				sizeof(*sgl_aggr[i].sg_iovs));
			if (sgl_aggr[i].sg_iovs == NULL)
			{
				D_GOTO(exit, rc = -DER_NOMEM);
			}
			sgl_aggr[i].sg_nr     = 1;
			sgl_aggr[i].sg_nr_out = 1;

			D_ALLOC(sgl_aggr[i].sg_iovs->iov_buf, sizeof(double));
			if (sgl_aggr[i].sg_iovs->iov_buf == NULL)
			{
				D_GOTO(exit, rc = -DER_NOMEM);
			}
			sgl_aggr[i].sg_iovs->iov_len     = sizeof(double);
			sgl_aggr[i].sg_iovs->iov_buf_len = sizeof(double);
		}
	}

	/** -- calling pipeline run */

	rc = ds_pipeline_run(vos_coh, pri->pri_oid, pri->pri_pipe, pri->pri_epr,
			     pri->pri_flags, &pri->pri_dkey, &nr_iods,
			     pri->pri_iods.iods, &pri->pri_anchor, nr_kds,
			     &nr_kds_out, &kds, &sgl_keys, &nr_recx, &sgl_recx,
			     sgl_aggr);

exit:

	/** set output data */

	if (rc == 0)
	{
		pro->pro_iods.nr		= nr_iods;
		pro->pro_iods.iods		= pri->pri_iods.iods;
		pro->pro_anchor			= pri->pri_anchor;
		pro->pro_kds.ca_count		= nr_kds_out;
		pro->pro_sgl_keys.ca_count	= nr_kds_out;
		if (nr_kds_out == 0)
		{
			pro->pro_kds.ca_arrays		= NULL;
			pro->pro_sgl_keys.ca_arrays	= NULL;
		}
		else
		{
			pro->pro_kds.ca_arrays		= kds;
			pro->pro_sgl_keys.ca_arrays	= sgl_keys;
		}
		pro->pro_sgl_recx.ca_count	= nr_recx;
		if (nr_recx == 0)
		{
			pro->pro_sgl_recx.ca_arrays	= NULL;
		}
		else
		{
			pro->pro_sgl_recx.ca_arrays	= sgl_recx;
		}
		pro->pro_sgl_agg.ca_count	= pri->pri_pipe.num_aggr_filters;
		pro->pro_sgl_agg.ca_arrays	= sgl_aggr;
		//pro->pro_epoch // TODO
	}
	pro->pro_ret = rc;

	/** send RPC back */

	rc = crt_reply_send(rpc);
	if (rc != 0)
	{
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
	}

	/** free memory */

	if (nr_kds_out > 0)
	{
		for (i = 0; i < nr_kds_out; i++)
		{
			D_FREE(sgl_keys[i].sg_iovs->iov_buf);
			D_FREE(sgl_keys[i].sg_iovs);
		}
		D_FREE(sgl_keys);
		D_FREE(kds);
	}
	if (nr_recx > 0)
	{
		for (i = 0; i < nr_recx; i++)
		{
			D_FREE(sgl_recx[i].sg_iovs->iov_buf);
			D_FREE(sgl_recx[i].sg_iovs);
		}
		D_FREE(sgl_recx);
	}
}
