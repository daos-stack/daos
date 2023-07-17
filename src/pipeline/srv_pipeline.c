/**
 * (C) Copyright 2016-2023 Intel Corporation.
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

/**
 * ULT yields every PIPELINE_ITERATION_MAX record fetches.
 */
#define PIPELINE_ITERATION_MAX	1024

/**
 * Used keep track of the credit system for yielding.
 */
struct enum_credits {
	uint32_t used;
	uint32_t max;
};

/**
 * Used to keep track of where we need to copy data on return buffers.
 */
struct pack_ret_data_args {
	daos_size_t      *recx_size;
	uint32_t          nr_iods;
	daos_key_desc_t  *kds;
	d_sg_list_t      *keys;
	uint32_t          keys_iov_idx;
	d_sg_list_t      *recx;
	uint32_t          recx_iov_idx;
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
	uint32_t		i;

	param.ip_hdl        = vos_coh;
	param.ip_oid        = oid;
	param.ip_epr.epr_lo = epr.epr_lo;
	param.ip_epr.epr_hi = epr.epr_hi;
	/* items show epoch is <= epr_hi. For range, use VOS_IT_EPC_RE */
	param.ip_epc_expr   = VOS_IT_EPC_LE;

	/* TODO: Set enum_arg.csummer !  Figure out how checksum works */

	/** reset buffers */
	d_key->iov_len      = 0;
	for (i = 0; i < nr_iods; i++)
		sgl_recx[i].sg_iovs->iov_len = 0;

	/** iterating over dkeys only */
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
		D_ERROR(DF_UOID " bio_iod_prep failed: " DF_RC "\n", DP_UOID(oid), DP_RC(rc));
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

static int
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

static int
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

static int
alloc_iter_bufs(daos_iod_t *iods, uint32_t nr, daos_iod_t **iods_iter, d_sg_list_t **sgl_recx_iter)
{
	daos_iod_t     *iods_iter_out;
	d_sg_list_t    *sgl_recx_iter_out  = NULL;
	d_iov_t        *sgl_recx_iter_iovs = NULL;
	uint32_t        i;
	uint32_t        j;
	size_t          iov_alloc_size;
	int             rc;

	D_ASSERT(nr != 0);
	D_ALLOC_ARRAY(sgl_recx_iter_out, nr);
	if (sgl_recx_iter_out == NULL)
		return -DER_NOMEM;
	D_ALLOC_ARRAY(sgl_recx_iter_iovs, nr);
	if (sgl_recx_iter_iovs == NULL) {
		D_FREE(sgl_recx_iter_out);
		return -DER_NOMEM;
	}
	D_ALLOC_ARRAY(iods_iter_out, nr);
	if (iods_iter_out == NULL) {
		D_FREE(sgl_recx_iter_out);
		D_FREE(sgl_recx_iter_iovs);
		return -DER_NOMEM;
	}

	memcpy(iods_iter_out, iods, sizeof(*iods) * nr);

	for (i = 0; i < nr; i++) {
		iov_alloc_size = 0;
		if (iods[i].iod_type == DAOS_IOD_ARRAY) {
			for (j = 0; j < iods[i].iod_nr; j++)
				iov_alloc_size += iods[i].iod_recxs[j].rx_nr;
			iov_alloc_size *= iods[i].iod_size;
		} else {
			iov_alloc_size = iods[i].iod_size;
		}
		D_ALLOC(sgl_recx_iter_iovs[i].iov_buf, iov_alloc_size);
		if (sgl_recx_iter_iovs[i].iov_buf == NULL)
			D_GOTO(error, rc = -DER_NOMEM);
		sgl_recx_iter_iovs[i].iov_buf_len = iov_alloc_size;

		sgl_recx_iter_out[i].sg_nr     = 1;
		sgl_recx_iter_out[i].sg_nr_out = 0;
		sgl_recx_iter_out[i].sg_iovs   = &sgl_recx_iter_iovs[i];
	}

	*iods_iter     = iods_iter_out;
	*sgl_recx_iter = sgl_recx_iter_out;
	return 0;
error:
	if (iods_iter_out != NULL)
		D_FREE(iods_iter_out);
	for (j = 0; j < i; j++)
		D_FREE(sgl_recx_iter_iovs[j].iov_buf);
	if (sgl_recx_iter_iovs != NULL)
		D_FREE(sgl_recx_iter_iovs);
	if (sgl_recx_iter_out != NULL)
		D_FREE(sgl_recx_iter_out);
	return rc;
}

static void
free_iter_bufs(uint32_t nr, daos_iod_t *iods_iter, d_sg_list_t *sgl_recx_iter)
{
	uint32_t i;

	if (sgl_recx_iter != NULL) {
		for (i = 0; i < nr; i++) {
			if (sgl_recx_iter[i].sg_iovs != NULL &&
			    sgl_recx_iter[i].sg_iovs->iov_buf != NULL)
				D_FREE(sgl_recx_iter[i].sg_iovs->iov_buf);
		}
		if (sgl_recx_iter[0].sg_iovs != NULL)
			D_FREE(sgl_recx_iter[0].sg_iovs);
		D_FREE(sgl_recx_iter);
	}
	if (iods_iter != NULL)
		D_FREE(iods_iter);
}

static int
pack_value(d_sg_list_t *sgl, uint32_t *iov_idx, d_iov_t *iov)
{
	uint32_t      iov_idx_bk = *iov_idx;
	d_iov_t      *out_iov    = &sgl->sg_iovs[*iov_idx];
	char         *ptr;

try_out_iov:
	if (iov->iov_len + out_iov->iov_len > out_iov->iov_buf_len) {
		if (iov_idx_bk > *iov_idx || (iov_idx_bk == *iov_idx && !out_iov->iov_len)) {
			/**
			 * If we can't still copy the input iov in an empty out_iov, then the
			 * out_iov is way too small. No empty iovs are skipped in the sgl.
			 */
			D_ERROR("iov has no space available for data value\n");
			return -DER_REC2BIG;
		}
		/**
		 * No space left in current out_iov to copy the iov, and data values are never split
		 * among different iovs. Trying one more time with the next iov in the sgl.
		 */
		iov_idx_bk++;
		if (iov_idx_bk == sgl->sg_nr) {
			D_ERROR("Consumed all iovs in the sgl, no space available for the rest of "
				"the data\n");
			return -DER_REC2BIG;
		}
		out_iov = &sgl->sg_iovs[iov_idx_bk];
		goto try_out_iov;
	}
	*iov_idx = iov_idx_bk;

	ptr               = out_iov->iov_buf;
	ptr              += out_iov->iov_len;
	memcpy(ptr, iov->iov_buf, iov->iov_len);

	out_iov->iov_len += iov->iov_len;
	if (out_iov->iov_len == iov->iov_len) {
		/**
		 * out_iov was a fresh new iov, so we need to increase the number of valid output
		 * iovs in the sgl.
		 */
		sgl->sg_nr_out++;
	}

	return 0;
}

static int
pack_record(d_iov_t *d_key_iter, daos_iod_t *iods_iter, d_sg_list_t *sgl_recx_iter,
	    uint32_t dkey_idx, struct pack_ret_data_args *pack_args)
{
	uint32_t idx;
	uint32_t i;
	uint32_t nr_iods = pack_args->nr_iods;
	int      rc;

	/**
	 * dkey
	 */
	idx = pack_args->keys_iov_idx;
	rc  = pack_value(pack_args->keys, &idx, d_key_iter);
	if (rc != 0)
		return rc;
	pack_args->keys_iov_idx             = idx;
	pack_args->kds[dkey_idx].kd_key_len = d_key_iter->iov_len;

	/**
	 * akeys' record data
	 */
	idx = pack_args->recx_iov_idx;
	for (i = 0; i < nr_iods; i++) {
		rc = pack_value(pack_args->recx, &idx, sgl_recx_iter[i].sg_iovs);
		if (rc != 0)
			return rc;

		pack_args->recx_iov_idx                      = idx;
		pack_args->recx_size[dkey_idx * nr_iods + i] = iods_iter[i].iod_size;
	}

	return 0;
}

/** TODO: This code still assumes dkey==NULL. The code for dkey!=NULL has to be written */
static int
ds_pipeline_run(daos_handle_t vos_coh, daos_unit_oid_t oid, daos_pipeline_t pipeline,
		daos_epoch_range_t epr, uint64_t flags, daos_key_t *dkey, uint32_t nr_iods,
		uint32_t *nr_iods_out, daos_iod_t *iods, daos_anchor_t *anchor, uint32_t nr_kds,
		uint32_t *nr_kds_out, daos_key_desc_t *kds, daos_size_t *recx_size,
		d_sg_list_t *sgl_keys, d_sg_list_t *sgl_recx, d_sg_list_t *sgl_agg,
		daos_pipeline_stats_t *stats)
{
	int                         rc;
	uint32_t                    nr_kds_pass;
	d_iov_t                     d_key_iter;
	d_sg_list_t                *sgl_recx_iter      = NULL;
	daos_iod_t                 *iods_iter          = NULL;
	struct enum_credits         credits            = {0};
	struct vos_iter_anchors     anchors            = {0};
	struct pipeline_compiled_t  pipeline_compiled  = {0};
	struct filter_part_run_t    pipe_run_args      = {0};
	struct pack_ret_data_args   pack_args          = {0};

	*nr_kds_out  = 0;
	*nr_iods_out = 0;
	rc           = d_pipeline_check(&pipeline);
	if (rc != 0)
		D_GOTO(exit, rc); /** bad pipeline */
	if (pipeline.version != 1)
		D_GOTO(exit, rc = -DER_MISMATCH); /** wrong version */
	if (daos_anchor_is_eof(anchor))
		D_GOTO(exit, rc = 0); /** no more rows */
	if (nr_iods == 0)
		D_GOTO(exit, rc = 0); /** nothing to return */
	if (nr_kds == 0 && pipeline.num_aggr_filters == 0)
		D_GOTO(exit, rc = 0); /** nothing to return */

	/** -- init all aggregation counters */

	pipeline_aggregations_init(&pipeline, sgl_agg);

	/** -- "compile" pipeline */

	rc = pipeline_compile(&pipeline, &pipeline_compiled);
	if (rc != 0)
		D_GOTO(exit, rc); /** compilation failed. Bad pipeline? */

	/** -- allocating space for temporary bufs */

	rc = alloc_iter_bufs(iods, nr_iods, &iods_iter, &sgl_recx_iter);
	if (rc != 0)
		D_GOTO(exit, rc);

	/** -- init pipe run data struct and pack result data struct */

	pipe_run_args.nr_iods  = nr_iods;
	pipe_run_args.iods     = iods_iter;

	pack_args.recx_size    = recx_size;
	pack_args.nr_iods      = nr_iods;
	pack_args.kds          = kds;
	pack_args.keys         = sgl_keys;
	pack_args.recx         = sgl_recx;
	pack_args.keys_iov_idx = 0;
	pack_args.recx_iov_idx = 0;

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

		rc = pipeline_fetch_record(vos_coh, oid, &anchors, epr, iods_iter, nr_iods,
					   &d_key_iter, sgl_recx_iter);
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
		 * -- Saving record info to be returned.
		 */

		rc = pack_record(&d_key_iter, iods_iter, sgl_recx_iter, nr_kds_pass - 1,
				 &pack_args);
		if (rc != 0)
			D_GOTO(exit, rc);
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
		*nr_kds_out     = nr_kds_pass;
		*nr_iods_out    = nr_iods * nr_kds_pass;
	} else if (nr_kds > 0 && pipeline.num_aggr_filters > 0 && nr_kds_pass > 0) {
		*nr_kds_out     = 1;
		*nr_iods_out    = nr_iods;
	} else {
		*nr_kds_out     = 0;
		*nr_iods_out    = 0;
	}

	rc = 0;
exit:
	pipeline_compile_free(&pipeline_compiled);
	free_iter_bufs(nr_iods, iods_iter, sgl_recx_iter);

	return rc;
}

static int
alloc_iovs_in_sgl(d_sg_list_t *sgl)
{
	uint32_t i = 0;
	uint32_t j;

	for (; i < sgl->sg_nr; i++) {
		D_ALLOC(sgl->sg_iovs[i].iov_buf, sgl->sg_iovs[i].iov_buf_len);
		if (sgl->sg_iovs[i].iov_buf == NULL)
			goto error;
	}

	return 0;
error:
	for (j = 0; j < i; j++)
		D_FREE(sgl->sg_iovs[j].iov_buf);
	return -DER_NOMEM;
}

struct pipeline_bulk_args {
	int           bulks_inflight;
	int           result;
	bool          inited;
	ABT_eventual  eventual;
};

static int
pipeline_bulk_comp_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct pipeline_bulk_args  *arg;
	struct crt_bulk_desc       *bulk_desc;
	crt_rpc_t                  *rpc;

	bulk_desc               = cb_info->bci_bulk_desc;
	D_ASSERT(bulk_desc->bd_local_hdl != CRT_BULK_NULL);
	crt_bulk_free(bulk_desc->bd_local_hdl);
	bulk_desc->bd_local_hdl = CRT_BULK_NULL;

	if (cb_info->bci_rc != 0)
		D_ERROR("bulk transfer failed: %d\n", cb_info->bci_rc);

	rpc = bulk_desc->bd_rpc;
	arg = (struct pipeline_bulk_args *)cb_info->bci_arg;

	if (!arg->result)
		arg->result = cb_info->bci_rc;

	D_ASSERT(arg->bulks_inflight > 0);
	arg->bulks_inflight--;
	if (arg->bulks_inflight == 0)
		ABT_eventual_set(arg->eventual, &arg->result, sizeof(arg->result));

	crt_req_decref(rpc);

	return cb_info->bci_rc;
}

static int
do_bulk_transfer_sgl(crt_rpc_t *rpc, crt_bulk_t bulk, d_sg_list_t *sgl, int sgl_idx,
		     struct pipeline_bulk_args *arg)
{
	size_t                size;
	unsigned int          iov_idx     = 0;
	crt_bulk_t            local_bulk;
	unsigned int          local_off;
	struct crt_bulk_desc  bulk_desc;
	off_t                 remote_off  = 0;
	crt_bulk_opid_t       bulk_opid;
	int                   rc;

	if (bulk == NULL) {
		D_ERROR("Remote bulk is NULL\n");
		return -DER_INVAL;
	}

	rc = crt_bulk_get_len(bulk, &size);
	if (rc != 0) {
		D_ERROR("Failed to get remote bulk size "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	while (iov_idx < sgl->sg_nr_out) {
		unsigned int  start;
		d_sg_list_t   sgl_sent;
		size_t        length    = 0;

		if (remote_off >= size) {
			rc = -DER_OVERFLOW;
			D_ERROR("Remote bulk is used up. off:%zu, size:%zu, "DF_RC"\n",
				remote_off, size, DP_RC(rc));
			break;
		}

		/** creating local bulk handle on-the-fly */
		start            = iov_idx;
		sgl_sent.sg_iovs = &sgl->sg_iovs[start];

		while (iov_idx < sgl->sg_nr_out && sgl->sg_iovs[iov_idx].iov_buf != NULL) {
			length += sgl->sg_iovs[iov_idx].iov_len;
			iov_idx++;
		}
		D_ASSERT(iov_idx > start);

		local_off          = 0;
		sgl_sent.sg_nr     = iov_idx - start;
		sgl_sent.sg_nr_out = sgl_sent.sg_nr;

		rc = crt_bulk_create(rpc->cr_ctx, &sgl_sent, CRT_BULK_RO, &local_bulk);
		if (rc != 0) {
			D_ERROR("crt_bulk_create %d error: " DF_RC "\n", sgl_idx, DP_RC(rc));
			break;
		}
		D_ASSERT(local_bulk != NULL);

		D_ASSERT(size > remote_off);
		if (length > (size - remote_off)) {
			rc = -DER_OVERFLOW;
			D_ERROR("Remote bulk isn't large enough. local_sz:%zu, remote_sz:%zu, "
				"remote_off:%zu, "DF_RC"\n", length, size, remote_off, DP_RC(rc));
			break;
		}

		crt_req_addref(rpc);

		bulk_desc.bd_rpc        = rpc;
		bulk_desc.bd_bulk_op    = CRT_BULK_PUT;
		bulk_desc.bd_remote_hdl = bulk;
		bulk_desc.bd_local_hdl  = local_bulk;
		bulk_desc.bd_len        = length;
		bulk_desc.bd_remote_off = remote_off;
		bulk_desc.bd_local_off  = local_off;

		/** transfer data !! */
		arg->bulks_inflight++;
		rc = crt_bulk_transfer(&bulk_desc, pipeline_bulk_comp_cb, arg, &bulk_opid);
		if (rc < 0) {
			D_ERROR("crt_bulk_transfer %d error: " DF_RC "\n", sgl_idx, DP_RC(rc));
			arg->bulks_inflight--;
			crt_bulk_free(local_bulk);
			crt_req_decref(rpc);
			break;
		}
		remote_off += length;
	}

	return rc;
}

static int
do_bulk_transfers(crt_rpc_t *rpc, crt_bulk_t *bulks, d_sg_list_t **sgls, int sgl_nr)
{
	int                        i;
	int                        rc, ret;
	int                       *status;
	struct pipeline_bulk_args  arg = {0};

	if (bulks == NULL) {
		D_ERROR("No remote bulks provided\n");
		return -DER_INVAL;
	}

	rc = ABT_eventual_create(sizeof(*status), &arg.eventual);
	if (rc != 0)
		return dss_abterr2der(rc);
	arg.inited = true;
	D_DEBUG(DB_IO, "bulk_op CRT_BULK_PUT sgl_nr %d\n", sgl_nr);

	arg.bulks_inflight++;

	for (i = 0; i < sgl_nr; i++) {
		if (bulks[i] == NULL)
			continue;
		rc = do_bulk_transfer_sgl(rpc, bulks[i], sgls[i], i, &arg);
		if (rc != 0)
			break;
	}

	arg.bulks_inflight--;
	if (!arg.bulks_inflight)
		ABT_eventual_set(arg.eventual, &rc, sizeof(rc));

	/** XXX: Currently, we only do sync transfer */
	ret = ABT_eventual_wait(arg.eventual, (void **)&status);
	if (rc == 0)
		rc = ret != 0 ? dss_abterr2der(ret) : *status;

	ABT_eventual_free(&arg.eventual);

	return rc;
}

static int
pipeline_bulk_transfer(crt_rpc_t *rpc)
{
	struct pipeline_run_in   *pri;
	struct pipeline_run_out  *pro;
	d_iov_t                   tmp_iov[2];
	d_sg_list_t               tmp_sgl[2];
	d_sg_list_t              *sgls[4]     = {0};
	crt_bulk_t                bulks[4]    = {0};
	int                       idx         = 0;
	int                       rc;

	pri = crt_req_get(rpc);
	pro = crt_reply_get(rpc);

	/** -- preparing buffers */
	if (pri->pri_kds_bulk != NULL && pro->pro_kds.ca_count > 0) {
		tmp_iov[0].iov_buf     = pro->pro_kds.ca_arrays;
		tmp_iov[0].iov_buf_len = pro->pro_kds.ca_count * sizeof(daos_key_desc_t);
		tmp_iov[0].iov_len     = pro->pro_kds.ca_count * sizeof(daos_key_desc_t);

		tmp_sgl[0].sg_nr       = 1;
		tmp_sgl[0].sg_nr_out   = 1;
		tmp_sgl[0].sg_iovs     = &tmp_iov[0];

		sgls[idx]              = &tmp_sgl[0];
		bulks[idx]             = pri->pri_kds_bulk;
		idx++;

		D_DEBUG(DB_IO, "reply kds bulk %zd\n", tmp_iov[0].iov_len);
	}
	if (pri->pri_iods_bulk != NULL && pro->pro_recx_size.ca_count > 0) {
		tmp_iov[1].iov_buf     = pro->pro_recx_size.ca_arrays;
		tmp_iov[1].iov_buf_len = pro->pro_recx_size.ca_count * sizeof(daos_size_t);
		tmp_iov[1].iov_len     = pro->pro_recx_size.ca_count * sizeof(daos_size_t);

		tmp_sgl[1].sg_nr       = 1;
		tmp_sgl[1].sg_nr_out   = 1;
		tmp_sgl[1].sg_iovs     = &tmp_iov[1];

		sgls[idx]              = &tmp_sgl[1];
		bulks[idx]             = pri->pri_iods_bulk;
		idx++;

		D_DEBUG(DB_IO, "reply iods bulk %zd\n", tmp_iov[1].iov_len);
	}
	if (pri->pri_sgl_keys_bulk != NULL && pro->pro_sgl_keys.sg_nr_out > 0) {
		sgls[idx]              = &pro->pro_sgl_keys;
		bulks[idx]             = pri->pri_sgl_keys_bulk;
		idx++;
	}
	if (pri->pri_sgl_recx_bulk != NULL && pro->pro_sgl_recx.sg_nr_out > 0) {
		sgls[idx]              = &pro->pro_sgl_recx;
		bulks[idx]             = pri->pri_sgl_recx_bulk;
		idx++;
	}
	if (!idx)
		return 0; /** no bulk transfers */

	rc = do_bulk_transfers(rpc, bulks, sgls, idx);

	/**
	 * -- after bulk transfers, we have to free buffers to avoid sending the data inline with
	 *    the RPC.
	 */
	if (pri->pri_kds_bulk != NULL && pro->pro_kds.ca_count > 0) {
		D_FREE(pro->pro_kds.ca_arrays);
		pro->pro_kds.ca_count = 0;
	}
	if (pri->pri_iods_bulk != NULL && pro->pro_recx_size.ca_count > 0) {
		D_FREE(pro->pro_recx_size.ca_arrays);
		pro->pro_recx_size.ca_count = 0;
	}
	/**
	 * These will be freed when pri* sgls are freed. Setting output sgls to zero here to avoid
	 * sending them with RPC.
	 */
	if (pri->pri_sgl_keys_bulk != NULL && pro->pro_sgl_keys.sg_nr_out > 0)
		pro->pro_sgl_keys = (d_sg_list_t){0};
	if (pri->pri_sgl_recx_bulk != NULL && pro->pro_sgl_recx.sg_nr_out > 0)
		pro->pro_sgl_recx = (d_sg_list_t){0};

	return rc;
}

void
ds_pipeline_run_handler(crt_rpc_t *rpc)
{
	struct pipeline_run_in  *pri;
	struct pipeline_run_out *pro;
	int                      rc;
	struct ds_cont_hdl      *coh;
	struct ds_cont_child    *coc         = NULL;
	daos_handle_t            vos_coh;
	daos_key_desc_t         *kds         = NULL;
	daos_size_t             *recx_size   = NULL;
	uint32_t                 nr_kds_out  = 0;
	uint32_t                 nr_iods_out = 0;
	daos_pipeline_stats_t    stats       = {0};

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

	D_ALLOC_ARRAY(kds, pri->pri_nr_kds);
	if (kds == NULL)
		D_GOTO(exit0, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(recx_size, pri->pri_iods.nr * pri->pri_nr_kds);
	if (recx_size == NULL)
		D_GOTO(exit0, rc = -DER_NOMEM);

	rc = alloc_iovs_in_sgl(&pri->pri_sgl_keys);
	if (rc != 0)
		D_GOTO(exit0, rc);
	rc = alloc_iovs_in_sgl(&pri->pri_sgl_recx);
	if (rc != 0)
		D_GOTO(exit0, rc);
	rc = alloc_iovs_in_sgl(&pri->pri_sgl_agg);
	if (rc != 0)
		D_GOTO(exit0, rc);

	/** -- calling pipeline run */

	rc = ds_pipeline_run(vos_coh, pri->pri_oid, pri->pri_pipe, pri->pri_epr, pri->pri_flags,
			     &pri->pri_dkey, pri->pri_iods.nr, &nr_iods_out, pri->pri_iods.iods,
			     &pri->pri_anchor, pri->pri_nr_kds, &nr_kds_out, kds, recx_size,
			     &pri->pri_sgl_keys, &pri->pri_sgl_recx, &pri->pri_sgl_agg, &stats);

exit0:
	ds_cont_hdl_put(coh);
exit:
	if (rc == 0) {
		/** pack data to be sent inline with the ret RPC */
		pro->pro_anchor               = pri->pri_anchor;
		pro->pro_kds.ca_count         = nr_kds_out;
		pro->pro_recx_size.ca_count   = nr_iods_out;

		if (nr_kds_out == 0)
			pro->pro_kds.ca_arrays        = NULL;
		else
			pro->pro_kds.ca_arrays        = kds;
		if (nr_iods_out == 0)
			pro->pro_recx_size.ca_arrays  = NULL;
		else
			pro->pro_recx_size.ca_arrays  = recx_size;

		pro->pro_sgl_keys          = pri->pri_sgl_keys;
		pro->pro_sgl_recx          = pri->pri_sgl_recx;
		pro->pro_sgl_agg           = pri->pri_sgl_agg;
		pro->stats                 = stats;
		pro->pro_nr_kds            = nr_kds_out;

		/** TODO: for dkey!=NULL, this will be nr_iods_out */
		pro->pro_nr_iods           = pri->pri_iods.nr;

		/** pro->pro_epoch (TODO) */

		/** handle any data that has to be transferred in bulk (RDMA) */
		rc = pipeline_bulk_transfer(rpc);
	}

	/** -- send RPC */

	pro->pro_ret = rc;
	rc           = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: " DF_RC "\n", DP_RC(rc));

	/** free memory after sending RPC */
	D_FREE(kds);
	D_FREE(recx_size);
	d_sgl_fini(&pri->pri_sgl_keys, true);
	d_sgl_fini(&pri->pri_sgl_recx, true);
	d_sgl_fini(&pri->pri_sgl_agg, true);
}
