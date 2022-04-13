/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

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

#define PIPELINE_ITERATION_MAX	1024

/**
 * Used keep track of the credit system for yielding.
 */
struct enum_credits {
	uint32_t used;
	uint32_t max;
};

static int
enum_pack_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	     vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	d_iov_t *d_key = cb_arg;

	if (unlikely(type != VOS_ITER_DKEY)) {
		D_ASSERTF(false, "unknown/unsupported type %d\n", type);
		return -DER_INVAL;
	}
	if (d_key->iov_len != 0) /** one dkey at a time */
		return 1;
	if (entry->ie_key.iov_len > 0)
		*d_key = entry->ie_key;

	return 0;
}

static int
pipeline_fetch_record(daos_handle_t vos_coh, daos_unit_oid_t oid, struct vos_iter_anchors *anchors,
		      daos_epoch_range_t epr, daos_iod_t *iods, uint32_t nr_iods, d_iov_t *d_key,
		      d_sg_list_t *sgl_recx)
{
	int			rc    = 0;
	int			rc1   = 0;
	int			type  = VOS_ITER_DKEY;
	vos_iter_param_t	param = {0};
	daos_handle_t		ioh   = DAOS_HDL_INVAL;
	struct bio_desc		*biod;
	size_t			io_size;

	param.ip_hdl        = vos_coh;
	param.ip_oid        = oid;
	param.ip_epr.epr_lo = epr.epr_lo;
	param.ip_epr.epr_hi = epr.epr_hi;
	/* items show epoch is <= epr_hi. For range, use VOS_IT_EPC_RE */
	param.ip_epc_expr   = VOS_IT_EPC_LE;

	/* TODO: Set enum_arg.csummer !  Figure out how checksum works */

	/** iterating over dkeys only */
	d_key->iov_len      = 0;
	rc = vos_iterate(&param, type, false, anchors, enum_pack_cb, NULL, d_key, NULL);
	D_DEBUG(DB_IO, "enum type %d rc " DF_RC "\n", type, DP_RC(rc));
	if (rc < 0)
		return rc;
	if (d_key->iov_len == 0) /** d_key not found */
		return 1;

	/** fetching record */
	rc = vos_fetch_begin(vos_coh, oid, epr.epr_hi, d_key, nr_iods, iods, 0, NULL, &ioh, NULL);
	if (rc) {
		D_CDEBUG(rc == -DER_INPROGRESS || rc == -DER_NONEXIST || rc == -DER_TX_RESTART,
			 DB_IO, DLOG_ERR, "Fetch begin for " DF_UOID " failed: " DF_RC "\n",
			 DP_UOID(oid), DP_RC(rc));
		D_GOTO(out, rc);
	}
	biod = vos_ioh2desc(ioh);
	rc   = bio_iod_prep(biod, BIO_CHK_TYPE_IO, NULL, CRT_BULK_RW);
	if (rc) {
		D_ERROR(DF_UOID " bio_iod_prep failed: " DF_RC ".\n", DP_UOID(oid), DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = bio_iod_copy(biod, sgl_recx, nr_iods);
	if (rc) {
		if (rc == -DER_OVERFLOW)
			rc = -DER_REC2BIG;
		D_CDEBUG(rc == -DER_REC2BIG, DLOG_DBG, DLOG_ERR,
			 DF_UOID " data transfer failed, rc " DF_RC "", DP_UOID(oid), DP_RC(rc));
		/** D_GOTO(post, rc); */
	}
	/**post:*/
	rc = bio_iod_post(biod, rc);
out:
	rc1 = vos_fetch_end(ioh, &io_size, rc);
	if (rc1 != 0) {
		D_CDEBUG(rc1 == -DER_REC2BIG || rc1 == -DER_INPROGRESS || rc1 == -DER_TX_RESTART ||
			 rc1 == -DER_EXIST || rc1 == -DER_NONEXIST || rc1 == -DER_ALREADY,
			 DLOG_DBG, DLOG_ERR, DF_UOID " %s end failed: " DF_RC "\n", DP_UOID(oid),
			 "Fetch", DP_RC(rc1));
		if (rc == 0)
			rc = rc1;
	}
	return rc;
}

int
pipeline_aggregations(struct pipeline_compiled_t *pipe, struct filter_part_run_t *args,
		      d_iov_t *dkey, d_sg_list_t *akeys, d_sg_list_t *sgl_agg)
{
	uint32_t i;
	int      rc = 0;

	args->dkey  = dkey;
	args->akeys = akeys;
	for (i = 0; i < pipe->num_aggr_filters; i++) {
		args->part_idx = 0;
		args->parts    = pipe->aggr_filters[i].parts;
		args->iov_aggr = &sgl_agg->sg_iovs[i];

		rc             = args->parts[0].filter_func(args);
		if (rc != 0)
			D_GOTO(exit, rc);
	}
exit:
	return rc;
}

int
pipeline_filters(struct pipeline_compiled_t *pipe, struct filter_part_run_t *args, d_iov_t *dkey,
		 d_sg_list_t *akeys)
{
	uint32_t i;
	int      rc = 0;

	args->dkey  = dkey;
	args->akeys = akeys;
	for (i = 0; i < pipe->num_filters; i++) {
		args->part_idx = 0;
		args->parts    = pipe->filters[i].parts;

		rc             = args->parts[0].filter_func(args);
		if (rc != 0)
			D_GOTO(exit, rc);
		if (!args->log_out)
			D_GOTO(exit, rc = 1);
	}
exit:
	return rc;
}

/** TODO: This code still assumes dkey==NULL. The code for dkey!=NULL has to be written */
static int
ds_pipeline_run(daos_handle_t vos_coh, daos_unit_oid_t oid, daos_pipeline_t pipeline,
		daos_epoch_range_t epr, uint64_t flags, daos_key_t *dkey, uint32_t *nr_iods,
		daos_iod_t *iods, daos_anchor_t *anchor, uint32_t nr_kds, uint32_t *nr_kds_out,
		daos_key_desc_t *kds, d_sg_list_t *sgl_keys, uint32_t *nr_recx,
		d_sg_list_t *sgl_recx, d_sg_list_t *sgl_agg, daos_pipeline_stats_t *stats)
{
	int                        rc;
	uint32_t                   nr_kds_pass;
	d_iov_t                    d_key_iter;
	d_iov_t                   *d_key_ptr;
	d_sg_list_t               *sgl_recx_iter      = NULL;
	d_iov_t                   *sgl_recx_iter_iovs = NULL;
	size_t                     iov_alloc_size;
	uint32_t                   i, k;
	uint32_t                   j                  = 0;
	struct enum_credits        credits            = {0};
	uint32_t                   sr_idx             = 0;
	struct vos_iter_anchors    anchors            = {0};
	struct pipeline_compiled_t pipeline_compiled  = {0};
	struct filter_part_run_t   pipe_run_args      = {0};

	*nr_kds_out = 0;
	*nr_recx    = 0;
	rc          = d_pipeline_check(&pipeline);
	if (rc != 0)
		D_GOTO(exit, rc); /** bad pipeline */
	if (pipeline.version != 1)
		D_GOTO(exit, rc = -DER_MISMATCH); /** wrong version */
	if (daos_anchor_is_eof(anchor))
		D_GOTO(exit, rc = 0); /** no more rows */
	if (*nr_iods == 0)
		D_GOTO(exit, rc = 0); /** nothing to return */
	if (nr_kds == 0 && pipeline.num_aggr_filters == 0)
		D_GOTO(exit, rc = 0); /** nothing to return */

	/** -- init all aggregation counters */

	pipeline_aggregations_init(&pipeline, sgl_agg);

	/** -- init pipe_run_args data struct */

	pipe_run_args.nr_iods = *nr_iods;
	pipe_run_args.iods    = iods;

	/** -- "compile" pipeline */

	rc                    = pipeline_compile(&pipeline, &pipeline_compiled);
	if (rc != 0)
		D_GOTO(exit, rc); /** compilation failed. Bad pipeline? */

	/** -- allocating space for sgl_recx_iter */

	D_ALLOC_ARRAY(sgl_recx_iter, *nr_iods);
	D_ALLOC_ARRAY(sgl_recx_iter_iovs, *nr_iods);
	if (sgl_recx_iter == NULL || sgl_recx_iter_iovs == NULL)
		D_GOTO(exit, rc = -DER_NOMEM);

	for (; j < *nr_iods; j++) {
		if (iods[j].iod_type == DAOS_IOD_ARRAY) {
			iov_alloc_size = 0;
			for (k = 0; k < iods[j].iod_nr; k++)
				iov_alloc_size += iods[j].iod_recxs[k].rx_nr;
			iov_alloc_size *= iods[j].iod_size;
		} else {
			iov_alloc_size = iods[j].iod_size;
		}

		D_ALLOC(sgl_recx_iter_iovs[j].iov_buf, iov_alloc_size);
		if (sgl_recx_iter_iovs[j].iov_buf == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);

		sgl_recx_iter_iovs[j].iov_len     = 0;
		sgl_recx_iter_iovs[j].iov_buf_len = iov_alloc_size;
		sgl_recx_iter[j].sg_iovs          = &sgl_recx_iter_iovs[j];
		sgl_recx_iter[j].sg_nr            = 1;
		sgl_recx_iter[j].sg_nr_out        = 0;
	}

	/**
	 *  -- Iterating over dkeys and doing filtering and aggregation. The variable nr_kds_pass
	 *     stores the number of dkeys in total that pass the filter.
	 */

	nr_kds_pass     = 0;
	anchors.ia_dkey = *anchor;
	credits.max     = PIPELINE_ITERATION_MAX;

	while (!daos_anchor_is_eof(&anchors.ia_dkey)) {
		if (pipeline.num_aggr_filters == 0 && nr_kds_pass == nr_kds)
			break; /** all records read */

		/** -- fetching record */

		rc = pipeline_fetch_record(vos_coh, oid, &anchors, epr, iods, *nr_iods, &d_key_iter,
					   sgl_recx_iter);
		if (rc < 0)
			D_GOTO(exit, rc); /** error */
		if (rc == 1)
			continue; /** nothing returned; no more records? */

		stats->nr_dkeys += 1; /** new record considered for filtering */

		credits.used++;
		if (credits.used > credits.max) {
			/** we have used all the credit. Yielding... */
			credits.used = 0;
			dss_sleep(0); /** 0 msec will not sleep, just yield */
		}

		/** -- doing filtering... */

		rc = pipeline_filters(&pipeline_compiled, &pipe_run_args, &d_key_iter,
				      sgl_recx_iter);
		if (rc < 0)
			D_GOTO(exit, rc); /** error */
		if (rc == 1)
			continue; /** record does not pass filters */

		/** -- dkey+akey pass filters */

		nr_kds_pass++;

		/** -- aggregations */

		rc = pipeline_aggregations(&pipeline_compiled, &pipe_run_args, &d_key_iter,
					   sgl_recx_iter, sgl_agg);
		if (rc < 0)
			D_GOTO(exit, rc);

		/**
		 * -- Returning matching records. We don't need to return all matching records if
		 *    aggregation is being performed: at most one is returned.
		 */

		if (nr_kds == 0 || (nr_kds > 0 && pipeline.num_aggr_filters > 0 && nr_kds_pass > 1))
			continue;

		/**
		 * saving record info
		 */

		/**
		 * dkey
		 */

		d_key_ptr                            = &sgl_keys->sg_iovs[nr_kds_pass - 1];
		d_key_ptr->iov_buf                   = NULL;
		D_ALLOC(d_key_ptr->iov_buf, d_key_iter.iov_buf_len);
		if (d_key_ptr->iov_buf == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);
		memcpy(d_key_ptr->iov_buf, d_key_iter.iov_buf, d_key_iter.iov_len);
		d_key_ptr->iov_len                   = d_key_iter.iov_len;
		d_key_ptr->iov_buf_len               = d_key_iter.iov_buf_len;

		sgl_keys->sg_nr_out                 += 1;

		kds[nr_kds_pass - 1].kd_key_len     = d_key_iter.iov_len;

		/**
		 * akeys
		 */
		for (i = 0; i < *nr_iods; i++) {
			if (sgl_recx_iter[i].sg_nr_out == 0)
				continue;

			/** copying sgl_recx */
			sgl_recx->sg_iovs[sr_idx].iov_buf = NULL;
			D_ALLOC(sgl_recx->sg_iovs[sr_idx].iov_buf,
				sgl_recx_iter[i].sg_iovs->iov_buf_len);
			if (sgl_recx->sg_iovs[sr_idx].iov_buf == NULL)
				D_GOTO(exit, rc = -DER_NOMEM);

			memcpy(sgl_recx->sg_iovs[sr_idx].iov_buf,
			       sgl_recx_iter[i].sg_iovs->iov_buf,
			       sgl_recx_iter[i].sg_iovs->iov_len);

			sgl_recx->sg_iovs[sr_idx].iov_buf_len =
				sgl_recx_iter[i].sg_iovs->iov_buf_len;
			sgl_recx->sg_iovs[sr_idx].iov_len     = sgl_recx_iter[i].sg_iovs->iov_len;
			sgl_recx->sg_nr_out                  += 1;

			sr_idx++;
		}
	}

	/**
	 * -- fixing averages: during aggregation, we don't know how many records will pass the
	 *    filters
	 */

	if (nr_kds_pass > 0)
		pipeline_aggregations_fixavgs(&pipeline, (double)nr_kds_pass, sgl_agg);

	/** -- backing up anchor */

	*anchor = anchors.ia_dkey;

	/** -- kds and recx returned */

	if (nr_kds > 0 && pipeline.num_aggr_filters == 0) {
		*nr_kds_out = nr_kds_pass;
		*nr_recx    = (*nr_iods) * nr_kds_pass;
	} else if (nr_kds > 0 && pipeline.num_aggr_filters > 0 && nr_kds_pass > 0) {
		*nr_kds_out = 1;
		*nr_recx    = *nr_iods;
	} else {
		*nr_kds_out = 0;
		*nr_recx    = 0;
	}

	rc = 0;
exit:
	pipeline_compile_free(&pipeline_compiled);

	for (i = 0; i < j; i++)
		D_FREE(sgl_recx_iter_iovs[i].iov_buf);
	if (sgl_recx_iter_iovs != NULL)
		D_FREE(sgl_recx_iter_iovs);
	if (sgl_recx_iter != NULL)
		D_FREE(sgl_recx_iter);

	return rc;
}

void
ds_pipeline_run_handler(crt_rpc_t *rpc)
{
	struct pipeline_run_in  *pri;
	struct pipeline_run_out *pro;
	int                      rc;
	struct ds_cont_hdl      *coh;
	struct ds_cont_child    *coc = NULL;
	daos_handle_t            vos_coh;
	uint32_t                 i;
	uint32_t                 nr_iods;
	daos_key_desc_t         *kds = NULL;
	uint32_t                 nr_kds;
	uint32_t                 nr_kds_out = 0;
	d_sg_list_t              sgl_keys;
	d_sg_list_t              sgl_recx;
	uint32_t                 nr_recx    = 0;
	uint32_t                 nr_aggr    = 0;
	d_sg_list_t              sgl_aggr;
	daos_pipeline_stats_t    stats      = {0};

	pri = crt_req_get(rpc);
	D_ASSERT(pri != NULL);
	pro = crt_reply_get(rpc);
	D_ASSERT(pro != NULL);
	D_DEBUG(DB_IO, "flags = " DF_U64 "\n", pri->pri_flags);

	/** -- get vos container handle */

	rc = ds_cont_find_hdl(pri->pri_pool_uuid, pri->pri_co_hdl, &coh);
	if (rc != 0)
		D_GOTO(exit, rc);

	coc     = coh->sch_cont;
	vos_coh = coc->sc_hdl;

	/** --  */

	printf("pri->pri_kds_bulk=%p pri->pri_sgl_keys_bulk=%p pri->pri_sgl_recx_bulk=%p"
	       "pri->pri_sgl_agg_bulk=%p\n", pri->pri_kds_bulk, pri->pri_sgl_keys_bulk,
	       pri->pri_sgl_recx_bulk, pri->pri_sgl_agg_bulk);
	fflush(stdout);

	nr_iods = pri->pri_iods.nr;
	nr_kds  = pri->pri_nr_kds;
	nr_aggr = pri->pri_pipe.num_aggr_filters;

	D_ALLOC_ARRAY(kds, nr_kds);
	if (kds == NULL)
		D_GOTO(exit0, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(sgl_keys.sg_iovs, nr_kds);
	D_ALLOC_ARRAY(sgl_recx.sg_iovs, nr_kds * nr_iods);
	D_ALLOC_ARRAY(sgl_aggr.sg_iovs, nr_aggr);
	if (sgl_keys.sg_iovs == NULL || sgl_recx.sg_iovs == NULL || sgl_aggr.sg_iovs == NULL)
		D_GOTO(exit0, rc = -DER_NOMEM);

	sgl_keys.sg_nr     = nr_kds;
	sgl_keys.sg_nr_out = 0;
	sgl_recx.sg_nr     = nr_kds * nr_iods;
	sgl_recx.sg_nr_out = 0;
	sgl_aggr.sg_nr     = nr_aggr;
	sgl_aggr.sg_nr_out = nr_aggr;

	for (i = 0; i < nr_aggr; i++) {
		D_ALLOC(sgl_aggr.sg_iovs[i].iov_buf, sizeof(double));
		if (sgl_aggr.sg_iovs[i].iov_buf == NULL)
			D_GOTO(exit0, rc = -DER_NOMEM);

		sgl_aggr.sg_iovs[i].iov_len     = sizeof(double);
		sgl_aggr.sg_iovs[i].iov_buf_len = sizeof(double);
	}

	/** -- calling pipeline run */

	rc = ds_pipeline_run(vos_coh, pri->pri_oid, pri->pri_pipe, pri->pri_epr, pri->pri_flags,
			     &pri->pri_dkey, &nr_iods, pri->pri_iods.iods, &pri->pri_anchor, nr_kds,
			     &nr_kds_out, kds, &sgl_keys, &nr_recx, &sgl_recx, &sgl_aggr, &stats);

exit0:
	ds_cont_hdl_put(coh);
exit:

	/** set output data */

	if (rc == 0) {
		pro->pro_iods.nr           = nr_iods;
		pro->pro_iods.iods         = pri->pri_iods.iods;
		pro->pro_anchor            = pri->pri_anchor;
		pro->pro_kds.ca_count      = nr_kds_out;
		if (nr_kds_out == 0) {
			pro->pro_kds.ca_arrays      = NULL;
			pro->pro_sgl_keys           = (d_sg_list_t){0};
		} else {
			pro->pro_kds.ca_arrays      = kds;
			pro->pro_sgl_keys           = sgl_keys;
		}
		if (nr_recx == 0)
			pro->pro_sgl_recx           = (d_sg_list_t){0};
		else
			pro->pro_sgl_recx           = sgl_recx;
		pro->pro_sgl_agg           = sgl_aggr;
		pro->stats                 = stats;
		/** pro->pro_epoch (TODO) */
	}
	pro->pro_ret = rc;

	/** send RPC back */

	rc           = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: " DF_RC "\n", DP_RC(rc));

	/** free memory */

	D_FREE(kds);

	for (i = 0; i < sgl_keys.sg_nr_out; i++) {
		D_FREE(sgl_keys.sg_iovs[i].iov_buf);
	}
	D_FREE(sgl_keys.sg_iovs);

	for (i = 0; i < sgl_recx.sg_nr_out; i++) {
		D_FREE(sgl_recx.sg_iovs[i].iov_buf);
	}
	D_FREE(sgl_recx.sg_iovs);

	for (i = 0; i < sgl_aggr.sg_nr_out; i++) {
		D_FREE(sgl_aggr.sg_iovs[i].iov_buf);
	}
	D_FREE(sgl_aggr.sg_iovs);
}
