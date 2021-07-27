/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * object server operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related to object.
 */
#define D_LOGFAC	DD_FAC(object)

#include <uuid/uuid.h>

#include <abt.h>
#include <daos/rpc.h>
#include <daos/cont_props.h>
#include <daos_srv/pool.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/bio.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/security.h>
#include <daos/checksum.h>
#include "daos_srv/srv_csum.h"
#include "obj_rpc.h"
#include "obj_internal.h"

static int
obj_verify_bio_csum(daos_obj_id_t oid, daos_iod_t *iods,
		    struct dcs_iod_csums *iod_csums, struct bio_desc *biod,
		    struct daos_csummer *csummer, uint32_t iods_nr);

static int
obj_ioc2ec_cs(struct obj_io_context *ioc)
{
	return obj_ec_cell_rec_nr(&ioc->ioc_oca);
}

static int
obj_ioc2ec_ss(struct obj_io_context *ioc)
{
	return obj_ec_stripe_rec_nr(&ioc->ioc_oca);
}

/* For single RDG based DTX, parse DTX participants information
 * from the client given dispatch targets information that does
 * NOT contains the original leader information.
 */
static int
obj_gen_dtx_mbs(struct daos_shard_tgt *tgts, bool is_ec, uint32_t *tgt_cnt,
		struct dtx_memberships **p_mbs)
{
	struct dtx_memberships	*mbs;
	size_t			 size;
	int			 i;
	int			 j;

	D_ASSERT(tgts != NULL);

	size = sizeof(struct dtx_daos_target) * *tgt_cnt;
	D_ALLOC(mbs, sizeof(*mbs) + size);
	if (mbs == NULL)
		return -DER_NOMEM;

	for (i = 0, j = 0; i < *tgt_cnt; i++) {
		if (tgts[i].st_rank == DAOS_TGT_IGNORE)
			continue;

		mbs->dm_tgts[j++].ddt_id = tgts[i].st_tgt_id;
	}

	if (j == 0) {
		D_FREE(mbs);
		*tgt_cnt = 0;
	} else {
		mbs->dm_tgt_cnt = j;
		mbs->dm_grp_cnt = 1;
		mbs->dm_data_size = size;
		if (!is_ec)
			mbs->dm_flags = DMF_SRDG_REP;
	}

	*p_mbs = mbs;

	return 0;
}

/**
 * After bulk finish, let's send reply, then release the resource.
 */
static int
obj_rw_complete(crt_rpc_t *rpc, struct obj_io_context *ioc,
		daos_handle_t ioh, int status, struct dtx_handle *dth)
{
	struct obj_rw_in	*orwi = crt_req_get(rpc);
	int			rc;

	if (daos_handle_is_valid(ioh)) {
		bool update = obj_rpc_is_update(rpc);

		if (update) {
			if (status == 0)
				status = dtx_sub_init(dth, &orwi->orw_oid,
						      orwi->orw_dkey_hash);
			rc = vos_update_end(ioh, ioc->ioc_map_ver,
					    &orwi->orw_dkey, status,
					    &ioc->ioc_io_size, dth);
		} else {
			rc = vos_fetch_end(ioh, &ioc->ioc_io_size, status);
		}

		if (rc != 0) {
			D_CDEBUG(rc == -DER_REC2BIG || rc == -DER_INPROGRESS ||
				 rc == -DER_TX_RESTART || rc == -DER_EXIST ||
				 rc == -DER_NONEXIST,
				 DLOG_DBG, DLOG_ERR,
				 DF_UOID " %s end failed: "DF_RC"\n",
				 DP_UOID(orwi->orw_oid),
				 update ? "Update" : "Fetch", DP_RC(rc));
			if (status == 0)
				status = rc;
		}
	}

	return status;
}

static void
obj_rw_reply(crt_rpc_t *rpc, int status, uint64_t epoch,
	     struct obj_io_context *ioc)
{
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	int			 rc;
	int			 i;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, ioc->ioc_map_ver);
	if (DAOS_FAIL_CHECK(DAOS_DTX_START_EPOCH)) {
		/* Return an stale epoch for test. */
		orwo->orw_epoch = dss_get_start_epoch() -
				  crt_hlc_epsilon_get() * 3;
	} else {
		orwo->orw_epoch = epoch;
	}

	D_DEBUG(DB_IO, "rpc %p opc %d send reply, pmv %d, epoch "DF_X64
		", status %d\n", rpc, opc_get(rpc->cr_opc),
		ioc->ioc_map_ver, orwo->orw_epoch, status);

	if (!ioc->ioc_lost_reply) {
		rc = crt_reply_send(rpc);
		if (rc != 0)
			D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
	} else {
		D_WARN("lost reply rpc %p\n", rpc);
	}

	if (obj_rpc_is_fetch(rpc)) {
		if (orwo->orw_iod_sizes.ca_arrays != NULL) {
			D_FREE(orwo->orw_iod_sizes.ca_arrays);
			orwo->orw_iod_sizes.ca_count = 0;
		}

		if (orwo->orw_nrs.ca_arrays != NULL) {
			D_FREE(orwo->orw_nrs.ca_arrays);
			orwo->orw_nrs.ca_count = 0;
		}

		if (orwo->orw_iod_csums.ca_arrays != NULL) {
			D_FREE(orwo->orw_iod_csums.ca_arrays);
			orwo->orw_iod_csums.ca_count = 0;
		}

		if (orwo->orw_maps.ca_arrays != NULL) {
			for (i = 0; i < orwo->orw_maps.ca_count; i++)
				D_FREE(orwo->orw_maps.ca_arrays[i].iom_recxs);

			D_FREE(orwo->orw_maps.ca_arrays);
			orwo->orw_maps.ca_count = 0;
		}

		daos_recx_ep_list_free(orwo->orw_rels.ca_arrays,
				       orwo->orw_rels.ca_count);

		if (ioc->ioc_free_sgls) {
			struct obj_rw_in *orw = crt_req_get(rpc);
			d_sg_list_t *sgls = orwo->orw_sgls.ca_arrays;
			int j;

			for (i = 0; i < orw->orw_nr; i++)
				for (j = 0; j < sgls[i].sg_nr; j++)
					D_FREE(sgls[i].sg_iovs[j].iov_buf);
		}
	}
}

struct obj_bulk_args {
	int		bulks_inflight;
	int		result;
	bool		inited;
	ABT_eventual	eventual;
};

static int
obj_bulk_comp_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct obj_bulk_args	*arg;
	struct crt_bulk_desc	*bulk_desc;
	crt_rpc_t		*rpc;

	if (cb_info->bci_rc != 0)
		D_ERROR("bulk transfer failed: %d\n", cb_info->bci_rc);

	bulk_desc = cb_info->bci_bulk_desc;
	rpc = bulk_desc->bd_rpc;
	arg = (struct obj_bulk_args *)cb_info->bci_arg;
	/**
	 * Note: only one thread will access arg.result, so
	 * it should be safe here.
	 **/
	if (arg->result == 0)
		arg->result = cb_info->bci_rc;

	D_ASSERT(arg->bulks_inflight > 0);
	arg->bulks_inflight--;
	if (arg->bulks_inflight == 0)
		ABT_eventual_set(arg->eventual, &arg->result,
				 sizeof(arg->result));

	crt_req_decref(rpc);
	return cb_info->bci_rc;
}

static inline int
bulk_cp(const struct crt_bulk_cb_info *cb_info)
{
	struct crt_bulk_desc	*bulk_desc;

	bulk_desc = cb_info->bci_bulk_desc;
	D_ASSERT(bulk_desc->bd_local_hdl != CRT_BULK_NULL);
	crt_bulk_free(bulk_desc->bd_local_hdl);
	bulk_desc->bd_local_hdl = CRT_BULK_NULL;

	return obj_bulk_comp_cb(cb_info);
}

static inline int
cached_bulk_cp(const struct crt_bulk_cb_info *cb_info)
{
	return obj_bulk_comp_cb(cb_info);
}

/**
 * Simulate bulk transfer by memcpy, all data are actually dropped.
 */
static void
obj_bulk_bypass(d_sg_list_t *sgl, crt_bulk_op_t bulk_op)
{
	static const int  dummy_buf_len = 4096;
	static char	 *dummy_buf;
	int		  i;

	if (!dummy_buf) {
		D_ALLOC(dummy_buf, dummy_buf_len);
		if (!dummy_buf)
			return; /* ignore error */
	}

	for (i = 0; i < sgl->sg_nr_out; i++) {
		char	*buf;
		int	 total, nob;

		if (sgl->sg_iovs[i].iov_buf == NULL ||
		    sgl->sg_iovs[i].iov_len == 0)
			continue;

		buf   = sgl->sg_iovs[i].iov_buf;
		total = sgl->sg_iovs[i].iov_len;
		while (total != 0) {
			nob = min(dummy_buf_len, total);
			if (bulk_op == CRT_BULK_PUT)
				memcpy(dummy_buf, buf, nob);
			else
				memcpy(buf, dummy_buf, nob);

			total -= nob;
			buf   += nob;
		}
	}
}

static int
bulk_transfer_sgl(daos_handle_t ioh, crt_rpc_t *rpc, crt_bulk_t remote_bulk,
		  off_t remote_off, crt_bulk_op_t bulk_op, bool bulk_bind,
		  d_sg_list_t *sgl, int sgl_idx, struct obj_bulk_args *p_arg)
{
	struct crt_bulk_desc	bulk_desc;
	crt_bulk_perm_t		bulk_perm;
	crt_bulk_opid_t		bulk_opid;
	crt_bulk_t		local_bulk;
	unsigned int		local_off;
	unsigned int		iov_idx = 0;
	size_t			remote_size;
	int			rc;

	if (remote_bulk == NULL) {
		D_ERROR("Remote bulk is NULL\n");
		return -DER_INVAL;
	}

	rc = crt_bulk_get_len(remote_bulk, &remote_size);
	if (rc) {
		D_ERROR("Failed to get remote bulk size "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (remote_off >= remote_size) {
		D_ERROR("remote_bulk_off %zu >= remote_bulk_size %zu\n",
			remote_off, remote_size);
		return -DER_INVAL;
	}

	if (daos_io_bypass & IOBP_SRV_BULK) {
		obj_bulk_bypass(sgl, bulk_op);
		return 0;
	}

	bulk_perm = bulk_op == CRT_BULK_PUT ? CRT_BULK_RO : CRT_BULK_RW;

	while (iov_idx < sgl->sg_nr_out) {
		d_sg_list_t	sgl_sent;
		size_t		length = 0;
		unsigned int	start;
		bool		cached_bulk = false;

		/*
		 * Skip bulk transfer over IOVs with NULL buffer address,
		 * these NULL IOVs are 'holes' or deduped records.
		 */
		while (iov_idx < sgl->sg_nr_out &&
		       sgl->sg_iovs[iov_idx].iov_buf == NULL) {
			remote_off += sgl->sg_iovs[iov_idx].iov_len;
			iov_idx++;
		}

		if (iov_idx == sgl->sg_nr_out)
			break;

		if (remote_off >= remote_size) {
			D_ERROR("Remote bulk is used up. off:%zu, size:%zu\n",
				remote_off, remote_size);
			rc = -DER_OVERFLOW;
			break;
		}

		local_bulk = vos_iod_bulk_at(ioh, sgl_idx, iov_idx, &local_off);
		if (local_bulk != NULL) {
			length = sgl->sg_iovs[iov_idx].iov_len;
			iov_idx++;
			cached_bulk = true;
		} else {
			start = iov_idx;
			sgl_sent.sg_iovs = &sgl->sg_iovs[start];

			/*
			 * For the IOVs not using cached bulk, creates bulk
			 * handle on-the-fly.
			 */
			while (iov_idx < sgl->sg_nr_out &&
			       sgl->sg_iovs[iov_idx].iov_buf != NULL &&
			       vos_iod_bulk_at(ioh, sgl_idx, iov_idx,
						&local_off) == NULL) {
				length += sgl->sg_iovs[iov_idx].iov_len;
				iov_idx++;

			};
			D_ASSERT(iov_idx > start);

			local_off = 0;
			sgl_sent.sg_nr = sgl_sent.sg_nr_out = iov_idx - start;

			rc = crt_bulk_create(rpc->cr_ctx, &sgl_sent, bulk_perm,
					     &local_bulk);
			if (rc != 0) {
				D_ERROR("crt_bulk_create %d error "DF_RC".\n",
					sgl_idx, DP_RC(rc));
				break;
			}
			D_ASSERT(local_bulk != NULL);
		}

		D_ASSERT(remote_size > remote_off);
		if (length > (remote_size - remote_off)) {
			D_ERROR("Remote bulk isn't large enough. "
				"local_sz:%zu, remote_sz:%zu, remote_off:%zu\n",
				length, remote_size, remote_off);
			rc = -DER_OVERFLOW;
			break;
		}

		crt_req_addref(rpc);

		bulk_desc.bd_rpc	= rpc;
		bulk_desc.bd_bulk_op	= bulk_op;
		bulk_desc.bd_remote_hdl	= remote_bulk;
		bulk_desc.bd_local_hdl	= local_bulk;
		bulk_desc.bd_len	= length;
		bulk_desc.bd_remote_off	= remote_off;
		bulk_desc.bd_local_off	= local_off;

		p_arg->bulks_inflight++;
		if (bulk_bind)
			rc = crt_bulk_bind_transfer(&bulk_desc,
				cached_bulk ? cached_bulk_cp : bulk_cp, p_arg,
				&bulk_opid);
		else
			rc = crt_bulk_transfer(&bulk_desc,
				cached_bulk ? cached_bulk_cp : bulk_cp, p_arg,
				&bulk_opid);
		if (rc < 0) {
			D_ERROR("crt_bulk_transfer %d error "DF_RC".\n",
				sgl_idx, DP_RC(rc));
			p_arg->bulks_inflight--;
			if (!cached_bulk)
				crt_bulk_free(local_bulk);
			crt_req_decref(rpc);
			break;
		}
		remote_off += length;
	}

	return rc;
}

static int
obj_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op, bool bulk_bind,
		  crt_bulk_t *remote_bulks, uint64_t *remote_offs,
		  daos_handle_t ioh, d_sg_list_t **sgls, int sgl_nr,
		  struct obj_bulk_args *p_arg)
{
	struct obj_bulk_args	arg = { 0 };
	int			i, rc, *status, ret;
	bool			async = true;

	if (remote_bulks == NULL) {
		D_ERROR("No remote bulks provided\n");
		return -DER_INVAL;
	}

	if (p_arg == NULL) {
		p_arg = &arg;
		async = false;
	}

	rc = ABT_eventual_create(sizeof(*status), &p_arg->eventual);
	if (rc != 0)
		return dss_abterr2der(rc);

	p_arg->inited = true;
	D_DEBUG(DB_IO, "bulk_op %d sgl_nr %d\n", bulk_op, sgl_nr);

	p_arg->bulks_inflight++;

	if (daos_handle_is_valid(ioh)) {
		rc = vos_dedup_verify_init(ioh, rpc->cr_ctx, CRT_BULK_RW);
		if (rc) {
			D_ERROR("Dedup verify prep failed. "DF_RC"\n",
				DP_RC(rc));
			goto done;
		}
	}

	for (i = 0; i < sgl_nr; i++) {
		d_sg_list_t	*sgl, tmp_sgl;

		if (remote_bulks[i] == NULL)
			continue;

		if (sgls != NULL) {
			sgl = sgls[i];
		} else {
			struct bio_sglist *bsgl;

			D_ASSERT(daos_handle_is_valid(ioh));
			bsgl = vos_iod_sgl_at(ioh, i);
			D_ASSERT(bsgl != NULL);

			sgl = &tmp_sgl;
			rc = bio_sgl_convert(bsgl, sgl);
			if (rc)
				break;
		}

		rc = bulk_transfer_sgl(ioh, rpc, remote_bulks[i],
				       remote_offs ? remote_offs[i] : 0,
				       bulk_op, bulk_bind, sgl, i, p_arg);
		if (sgls == NULL)
			d_sgl_fini(sgl, false);
		if (rc)
			break;
	}
done:
	if (--(p_arg->bulks_inflight) == 0)
		ABT_eventual_set(p_arg->eventual, &rc, sizeof(rc));

	if (async)
		return rc;

	ret = ABT_eventual_wait(p_arg->eventual, (void **)&status);
	if (rc == 0)
		rc = ret ? dss_abterr2der(ret) : *status;

	ABT_eventual_free(&p_arg->eventual);
	/* After RDMA is done, corrupt the server data */
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_DISK)) {
		struct bio_sglist	*fbsgl;
		d_sg_list_t		 fsgl;
		int			*fbuffer;

		D_DEBUG(DB_IO, "Data corruption after RDMA\n");
		fbsgl = vos_iod_sgl_at(ioh, 0);
		bio_sgl_convert(fbsgl, &fsgl);
		fbuffer = (int *)fsgl.sg_iovs[0].iov_buf;
		*fbuffer += 0x2;
		d_sgl_fini(&fsgl, false);
	}
	return rc;
}

static int
obj_set_reply_sizes(crt_rpc_t *rpc, daos_iod_t *iods, int iod_nr)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	uint64_t		*sizes = NULL;
	int			i;

	D_ASSERT(obj_rpc_is_fetch(rpc));
	D_ASSERT(orwo != NULL);
	D_ASSERT(orw != NULL);

	if (orw->orw_flags & ORF_CHECK_EXISTENCE)
		goto out;

	if (iod_nr <= 0) {
		D_ERROR("rpc %p contains invalid sizes count %d for "
			DF_UOID" with epc "DF_X64".\n",
			rpc, iod_nr, DP_UOID(orw->orw_oid), orw->orw_epoch);
		return -DER_INVAL;
	}

	/* Re-entry case.*/
	if (orwo->orw_iod_sizes.ca_count != 0) {
		D_ASSERT(orwo->orw_iod_sizes.ca_count == iod_nr);
		D_ASSERT(orwo->orw_iod_sizes.ca_arrays != NULL);

		sizes = orwo->orw_iod_sizes.ca_arrays;
	} else {
		D_ALLOC_ARRAY(sizes, iod_nr);
		if (sizes == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < orw->orw_iod_array.oia_iod_nr; i++) {
		sizes[i] = iods[i].iod_size;
		D_DEBUG(DB_IO, DF_UOID" %d:"DF_U64"\n", DP_UOID(orw->orw_oid),
			i, iods[i].iod_size);
	}

out:
	if (sizes == NULL)
		iod_nr = 0;
	orwo->orw_iod_sizes.ca_count = iod_nr;
	orwo->orw_iod_sizes.ca_arrays = sizes;

	D_DEBUG(DB_TRACE, "rpc %p set sizes count as %d for "
		DF_UOID" with epc "DF_X64".\n",
		rpc, iod_nr, DP_UOID(orw->orw_oid), orw->orw_epoch);

	return 0;
}

/**
 * Pack nrs in sgls inside the reply, so the client can update
 * sgls before it returns to application.
 * Pack sgl's data size in the reply, client fetch can based on
 * it to update sgl's iov_len.
 *
 * Note: this is only needed for bulk transfer, for inline transfer,
 * it will pack the complete sgls inside the req/reply, see obj_shard_rw().
 */
static int
obj_set_reply_nrs(crt_rpc_t *rpc, daos_handle_t ioh, d_sg_list_t *sgls)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	uint32_t		*nrs;
	daos_size_t		*data_sizes;
	uint32_t		 nrs_count = orw->orw_nr;
	int			 i, j;

	if (nrs_count == 0)
		return 0;

	/* Re-entry case. */
	if (orwo->orw_nrs.ca_count != 0) {
		D_ASSERT(orwo->orw_nrs.ca_count == nrs_count);
		return 0;
	}

	/* return sg_nr_out and data size for sgl */
	D_ALLOC(orwo->orw_nrs.ca_arrays,
		nrs_count * (sizeof(uint32_t) + sizeof(daos_size_t)));
	if (orwo->orw_nrs.ca_arrays == NULL)
		return -DER_NOMEM;

	orwo->orw_nrs.ca_count = nrs_count;
	orwo->orw_data_sizes.ca_count = nrs_count;
	orwo->orw_data_sizes.ca_arrays =
		(void *)((char *)orwo->orw_nrs.ca_arrays +
			nrs_count * (sizeof(uint32_t)));

	nrs = orwo->orw_nrs.ca_arrays;
	data_sizes = orwo->orw_data_sizes.ca_arrays;
	for (i = 0; i < nrs_count; i++) {
		struct bio_sglist	*bsgl;
		d_sg_list_t		*sgl;

		if (sgls != NULL) {
			sgl = &sgls[i];
			D_ASSERT(sgl != NULL);
			nrs[i] = sgl->sg_nr_out;
		} else {
			bsgl = vos_iod_sgl_at(ioh, i);
			D_ASSERT(bsgl != NULL);
			nrs[i] = bsgl->bs_nr_out;
			/* tail holes trimmed by ioc_trim_tail_holes() */
			for (j = 0; j < bsgl->bs_nr_out; j++)
				data_sizes[i] += bio_iov2req_len(
					&bsgl->bs_iovs[j]);
		}
	}

	return 0;
}

static void
obj_echo_rw(crt_rpc_t *rpc, daos_iod_t *split_iods, uint64_t *split_offs)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	struct obj_tls		*tls;
	daos_iod_t		*iod;
	uint64_t		*off;
	d_sg_list_t		*p_sgl;
	crt_bulk_op_t		bulk_op;
	bool			bulk_bind;
	int			i;
	int			rc = 0;

	D_DEBUG(DB_TRACE, "opc %d oid "DF_UOID" dkey "DF_KEY
		" tgt/xs %d/%d epc "DF_X64".\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid),
		DP_KEY(&orw->orw_dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id,
		orw->orw_epoch);

	if (obj_rpc_is_fetch(rpc)) {
		rc = obj_set_reply_sizes(rpc, orw->orw_iod_array.oia_iods,
					 orw->orw_iod_array.oia_iod_nr);
		if (rc)
			D_GOTO(out, rc);
	}

	/* Inline fetch/update */
	if (orw->orw_bulks.ca_arrays == NULL && orw->orw_bulks.ca_count == 0) {
		if (obj_rpc_is_fetch(rpc)) {
			orwo->orw_sgls.ca_count = orw->orw_sgls.ca_count;
			orwo->orw_sgls.ca_arrays = orw->orw_sgls.ca_arrays;
		}
		D_GOTO(out, rc);
	}

	/* Only support 1 iod now */
	D_ASSERT(orw->orw_iod_array.oia_iod_nr == 1);
	iod = split_iods == NULL ? orw->orw_iod_array.oia_iods : split_iods;
	off = split_offs == NULL ? orw->orw_iod_array.oia_offs : split_offs;

	tls = obj_tls_get();
	p_sgl = &tls->ot_echo_sgl;

	/* Let's check if tls already have enough buffer */
	if (p_sgl->sg_nr < iod->iod_nr) {
		d_sgl_fini(p_sgl, true);
		rc = d_sgl_init(p_sgl, iod->iod_nr);
		if (rc)
			D_GOTO(out, rc);

		p_sgl->sg_nr_out = p_sgl->sg_nr;
	}

	for (i = 0; i < iod->iod_nr; i++) {
		daos_size_t size = iod->iod_size;

		if (size == DAOS_REC_ANY)
			size = sizeof(uint64_t);

		if (iod->iod_type == DAOS_IOD_ARRAY) {
			D_ASSERT(iod->iod_recxs);
			size *= iod->iod_recxs[i].rx_nr;
		}

		/* Check each vector */
		if (p_sgl->sg_iovs[i].iov_buf_len < size) {
			if (p_sgl->sg_iovs[i].iov_buf != NULL)
				D_FREE(p_sgl->sg_iovs[i].iov_buf);

			D_ALLOC(p_sgl->sg_iovs[i].iov_buf, size);
			/* obj_tls_fini() will free these buffer */
			if (p_sgl->sg_iovs[i].iov_buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			p_sgl->sg_iovs[i].iov_buf_len = size;
			p_sgl->sg_iovs[i].iov_len = size;
		}
	}

	orwo->orw_sgls.ca_count = 0;
	orwo->orw_sgls.ca_arrays = NULL;
	if (obj_rpc_is_fetch(rpc)) {
		rc = obj_set_reply_nrs(rpc, DAOS_HDL_INVAL, p_sgl);
		if (rc != 0)
			D_GOTO(out, rc);
		bulk_op = CRT_BULK_PUT;
	} else {
		bulk_op = CRT_BULK_GET;
	}

	bulk_bind = orw->orw_flags & ORF_BULK_BIND;
	rc = obj_bulk_transfer(rpc, bulk_op, bulk_bind,
			       orw->orw_bulks.ca_arrays, off,
			       DAOS_HDL_INVAL, &p_sgl, orw->orw_nr, NULL);
out:
	orwo->orw_ret = rc;
	orwo->orw_map_version = orw->orw_map_ver;
}

/** if checksums are enabled, fetch needs to allocate the memory that will be
 * used for the csum structures.
 */
static int
obj_fetch_csum_init(struct ds_cont_child *cont, struct obj_rw_in *orw,
		    struct obj_rw_out *orwo)
{
	int rc;

	/**
	 * Allocate memory for the csum structures.
	 * This memory and information will be used by VOS to put the checksums
	 * in as it fetches the data's metadata from the btree/evtree.
	 *
	 * The memory will be freed in obj_rw_reply
	 */
	rc = daos_csummer_alloc_iods_csums(cont->sc_csummer,
					   orw->orw_iod_array.oia_iods,
					   orw->orw_iod_array.oia_iod_nr,
					   false, NULL,
					   &orwo->orw_iod_csums.ca_arrays);

	if (rc >= 0) {
		orwo->orw_iod_csums.ca_count = (uint64_t)rc;
		rc = 0;
	}

	return rc;
}

static struct dcs_iod_csums *
get_iod_csum(struct dcs_iod_csums *iod_csums, int i)
{
	if (iod_csums == NULL)
		return NULL;
	return &iod_csums[i];
}

static int
csum_add2iods(daos_handle_t ioh, daos_iod_t *iods, uint32_t iods_nr,
	      struct daos_csummer *csummer,
	      struct dcs_iod_csums *iod_csums, daos_unit_oid_t oid,
	      daos_key_t *dkey)
{
	int	 rc = 0;
	uint32_t biov_csums_idx = 0;
	size_t	 biov_csums_used = 0;
	int	 i;

	struct bio_desc *biod = vos_ioh2desc(ioh);
	struct dcs_csum_info *csum_infos = vos_ioh2ci(ioh);
	uint32_t csum_info_nr = vos_ioh2ci_nr(ioh);

	for (i = 0; i < iods_nr; i++) {
		if (biov_csums_idx >= csum_info_nr)
			break; /** no more csums to add */
		D_DEBUG(DB_CSUM, DF_C_UOID_DKEY"Adding fetched to IOD: "
				 DF_C_IOD", csum: "DF_CI"\n",
			DP_C_UOID_DKEY(oid, dkey),
			DP_C_IOD(&iods[i]), DP_CI(csum_infos[biov_csums_idx]));
		rc = ds_csum_add2iod(
			&iods[i], csummer,
			bio_iod_sgl(biod, i),
			&csum_infos[biov_csums_idx],
			&biov_csums_used, get_iod_csum(iod_csums, i));

		if (rc != 0) {
			D_ERROR("Failed to add csum for iod\n");
			return rc;
		}
		biov_csums_idx += biov_csums_used;
	}

	return rc;
}

static int
csum_verify_keys(struct daos_csummer *csummer, daos_key_t *dkey,
		 struct dcs_csum_info *dkey_csum,
		 struct obj_iod_array *oia, daos_unit_oid_t *uoid)
{
	uint32_t	i;
	int		rc;

	if (!daos_csummer_initialized(csummer) || csummer->dcs_skip_key_verify)
		return 0;

	if (!DAOS_FAIL_CHECK(DAOS_VC_DIFF_DKEY)) {
		/**
		 * with DAOS_VC_DIFF_DKEY, the dkey will be corrupt on purpose
		 * for object verification tests. Don't reject the
		 * update in this case
		 */
		rc = daos_csummer_verify_key(csummer, dkey, dkey_csum);
		if (rc != 0) {
			D_ERROR("daos_csummer_verify_key error for dkey: "
					DF_RC"\n",
				DP_RC(rc));
			return rc;
		}
	}

	for (i = 0; i < oia->oia_iod_nr; i++) {
		daos_iod_t		*iod = &oia->oia_iods[i];
		struct dcs_iod_csums	*csum = &oia->oia_iod_csums[i];

		if (!csum_iod_is_supported(iod))
			continue;

		D_DEBUG(DB_CSUM, DF_C_UOID_DKEY"iod[%d]: "DF_C_IOD","
				 " csum_nr: %d, first data csum: "DF_CI"\n",
			DP_C_UOID_DKEY(*uoid, dkey), i,
			DP_C_IOD(iod), csum->ic_nr, DP_CI(*csum->ic_data));
		rc = daos_csummer_verify_key(csummer,
					     &iod->iod_name,
					     &csum->ic_akey);
		if (rc != 0) {
			D_ERROR(DF_C_UOID_DKEY"iod[%d]: "DF_C_IOD" verify_key "
				"failed for akey: "DF_KEY", csum: "DF_CI", "
				"error: "DF_RC"\n",
				DP_C_UOID_DKEY(*uoid, dkey), i,
				DP_C_IOD(iod), DP_KEY(&iod->iod_name),
				DP_CI(csum->ic_akey), DP_RC(rc));
			return rc;
		}
	}

	return 0;
}

/** Add a recov record to the recov_lists (for singv degraded fetch) */
static int
obj_singv_ec_add_recov(uint32_t iod_nr, uint32_t iod_idx, uint64_t rec_size,
		       daos_epoch_t epoch,
		       struct daos_recx_ep_list **recov_lists_ptr)
{
	struct daos_recx_ep_list	*recov_lists = *recov_lists_ptr;
	struct daos_recx_ep_list	*recov_list;
	struct daos_recx_ep		 recx_ep;

	if (recov_lists == NULL) {
		D_ALLOC_ARRAY(recov_lists, iod_nr);
		if (recov_lists == NULL)
			return -DER_NOMEM;
		*recov_lists_ptr = recov_lists;
	}

	/* add one recx with any idx/nr to notify client this singv need to be
	 * recovered.
	 */
	recov_list = &recov_lists[iod_idx];
	recx_ep.re_recx.rx_idx = 0;
	recx_ep.re_recx.rx_nr = 1;
	recx_ep.re_ep = epoch;
	recx_ep.re_type = DRT_SHADOW;
	recx_ep.re_rec_size = rec_size;

	return daos_recx_ep_add(recov_list, &recx_ep);
}

/** Filter and prepare for the sing value EC update/fetch */
int
obj_singv_ec_rw_filter(daos_unit_oid_t oid, struct daos_oclass_attr *oca,
		       daos_iod_t *iods, uint64_t *offs, daos_epoch_t epoch,
		       uint32_t flags, uint32_t start_shard,
		       uint32_t nr, bool for_update, bool deg_fetch,
		       struct daos_recx_ep_list **recov_lists_ptr)
{
	daos_iod_t			*iod;
	struct obj_ec_singv_local	 loc;
	uint32_t			 tgt_idx;
	uint32_t			 i;
	int				 rc = 0;
	bool				 reentry = false;

	if (!(flags & ORF_EC))
		return rc;

	tgt_idx = oid.id_shard - start_shard;
	for (i = 0; i < nr; i++) {
		uint64_t	gsize;

		iod = &iods[i];
		if (iod->iod_type != DAOS_IOD_SINGLE)
			continue;

		if (iod->iod_size == DAOS_REC_ANY) /* punch */
			continue;

		/* Use iod_recxs to pass ir_gsize:
		 * akey_update() => akey_update_single()
		 */
		if (for_update) {
			/* For singv EC, "iod_recxs != NULL" means re-entry. */
			if (iod->iod_recxs != NULL) {
				reentry = true;
			} else {
				D_ASSERT(!reentry);
				iod->iod_recxs = (void *)iod->iod_size;
			}
		} else {
			D_ASSERT(iod->iod_recxs == NULL);
		}

		if (reentry)
			gsize = (uintptr_t)iod->iod_recxs;
		else
			gsize = iod->iod_size;

		if (obj_ec_singv_one_tgt(gsize, NULL, oca))
			continue;

		obj_ec_singv_local_sz(gsize, oca, tgt_idx, &loc, for_update);
		if (offs != NULL)
			offs[i] = loc.esl_off;

		if (for_update) {
			if (!reentry) {
				iod->iod_size = loc.esl_size;
				D_ASSERT(iod->iod_size != DAOS_REC_ANY);
			} else {
				D_ASSERT(iod->iod_size == loc.esl_size);
			}
		} else if (deg_fetch) {
			rc = obj_singv_ec_add_recov(nr, i, iod->iod_size,
						    epoch, recov_lists_ptr);
		}
	}

	return rc;
}

/* Call internal method to increment CSUM media error. */
static void
obj_log_csum_err(void)
{
	struct dss_module_info	*info = dss_get_module_info();
	struct bio_xs_context	*bxc;

	D_ASSERT(info != NULL);
	bxc = info->dmi_nvme_ctxt;

	if (bxc == NULL) {
		D_ERROR("BIO NVMe context not initialized for xs:%d, tgt:%d\n",
		info->dmi_xs_id, info->dmi_tgt_id);
		return;
	}

	bio_log_csum_err(bxc, info->dmi_tgt_id);
}

static void
map_add_recx(daos_iom_t *map, const struct bio_iov *biov, uint64_t rec_idx)
{
	map->iom_recxs[map->iom_nr_out].rx_idx = rec_idx;
	map->iom_recxs[map->iom_nr_out].rx_nr = bio_iov2req_len(biov)
						/ map->iom_size;
	map->iom_nr_out++;
}

/** create maps for actually written to extents. */
static int
obj_fetch_create_maps(crt_rpc_t *rpc, struct bio_desc *biod, daos_iod_t *iods)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	daos_iom_t		*maps;
	daos_iom_t		*map;
	struct bio_sglist	*bsgl;
	daos_iod_t		*iod;
	struct bio_iov		*biov;
	uint32_t		 iods_nr;
	uint32_t		 i, r;
	uint64_t		 rec_idx;
	uint32_t		 bsgl_iov_idx;

	/**
	 * Allocate memory for the maps. There will be 1 per iod
	 * Will be freed in obj_rw_reply
	 */
	iods_nr = orw->orw_iod_array.oia_iod_nr;

	/* Re-entry case. */
	if (orwo->orw_maps.ca_count != 0) {
		D_ASSERT(orwo->orw_maps.ca_count == iods_nr);
		return 0;
	}

	D_ALLOC_ARRAY(maps, iods_nr);
	if (maps == NULL)
		return -DER_NOMEM;
	for (i = 0; i <  iods_nr; i++) {
		bsgl = bio_iod_sgl(biod, i); /** 1 bsgl per iod */
		iod = &iods[i];
		map = &maps[i];
		map->iom_nr = bsgl->bs_nr_out - bio_sgl_holes(bsgl);

		/** will be freed in obj_rw_reply */
		D_ALLOC_ARRAY(map->iom_recxs, map->iom_nr);
		if (map->iom_recxs == NULL) {
			for (r = 0; r < i; r++)
				D_FREE(maps[r].iom_recxs);
			D_FREE(maps);
			return -DER_NOMEM;
		}

		map->iom_size = iod->iod_size;
		map->iom_type = iod->iod_type;

		if (map->iom_type != DAOS_IOD_ARRAY ||
			bsgl->bs_nr_out == 0)
			continue;

		/** start rec_idx at first record of iod.recxs */
		bsgl_iov_idx = 0;
		for (r = 0; r < iod->iod_nr; r++) {
			daos_recx_t recx = iod->iod_recxs[r];

			D_DEBUG(DB_CSUM, "processing recx[%d]: "DF_RECX"\n",
				r, DP_RECX(recx));
			rec_idx = recx.rx_idx;

			while (rec_idx <= recx.rx_idx + recx.rx_nr - 1) {
				biov = bio_sgl_iov(bsgl, bsgl_iov_idx);
				if (biov == NULL) /** reached end of bsgl */
					break;
				if (!bio_addr_is_hole(&biov->bi_addr))
					map_add_recx(map, biov, rec_idx);

				rec_idx += (bio_iov2req_len(biov) /
					    map->iom_size);
				bsgl_iov_idx++;
			}
		}

		daos_iom_sort(map);

		/** allocated and used should be the same */
		D_ASSERTF(map->iom_nr == map->iom_nr_out,
			  "map->iom_nr(%d) == map->iom_nr_out(%d)",
			  map->iom_nr, map->iom_nr_out);
		map->iom_recx_lo = map->iom_recxs[0];
		map->iom_recx_hi = map->iom_recxs[map->iom_nr - 1];
		if (orw->orw_flags & ORF_CREATE_MAP_DETAIL)
			map->iom_flags = DAOS_IOMF_DETAIL;
	}

	orwo->orw_maps.ca_count = iods_nr;
	orwo->orw_maps.ca_arrays = maps;

	return 0;
}

static int
obj_fetch_shadow(struct obj_io_context *ioc, daos_unit_oid_t oid,
		 daos_epoch_t epoch, uint64_t cond_flags, daos_key_t *dkey,
		 unsigned int iod_nr, daos_iod_t *iods, uint32_t tgt_idx,
		 struct dtx_handle *dth, struct daos_recx_ep_list **pshadows)
{
	daos_handle_t			 ioh = DAOS_HDL_INVAL;
	int				 rc;

	obj_iod_idx_vos2parity(iod_nr, iods);
	rc = vos_fetch_begin(ioc->ioc_vos_coh, oid, epoch, dkey, iod_nr, iods,
			     cond_flags | VOS_OF_FETCH_RECX_LIST, NULL, &ioh,
			     dth);
	if (rc) {
		D_ERROR(DF_UOID" Fetch begin failed: "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
		goto out;
	}

	*pshadows = vos_ioh2recx_list(ioh);
	vos_fetch_end(ioh, NULL, 0);

out:
	obj_iod_idx_parity2vos(iod_nr, iods);
	if (rc == 0) {
		obj_shadow_list_vos2daos(iod_nr, *pshadows, &ioc->ioc_oca);
		rc = obj_iod_recx_vos2daos(iod_nr, iods, tgt_idx,
					   &ioc->ioc_oca);
	}
	return rc;
}

int
obj_prep_fetch_sgls(crt_rpc_t *rpc, struct obj_io_context *ioc)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	d_sg_list_t		*sgls = orw->orw_sgls.ca_arrays;
	int			nr = orw->orw_sgls.ca_count;
	bool			need_alloc = false;
	int			i;
	int			j;
	int			rc = 0;

	for (i = 0; i < nr; i++) {
		for (j = 0; j < sgls[i].sg_nr; j++) {
			d_iov_t *iov = &sgls[i].sg_iovs[j];

			if (iov->iov_len < iov->iov_buf_len) {
				need_alloc = true;
				break;
			}
		}
	}

	/* reuse input sgls */
	orwo->orw_sgls.ca_count = orw->orw_sgls.ca_count;
	orwo->orw_sgls.ca_arrays = orw->orw_sgls.ca_arrays;
	if (!need_alloc)
		return 0;

	/* Reset the iov first, easier for error cleanup */
	for (i = 0; i < nr; i++) {
		for (j = 0; j < sgls[i].sg_nr; j++)
			sgls[i].sg_iovs[j].iov_buf = NULL;
	}

	sgls = orwo->orw_sgls.ca_arrays;
	for (i = 0; i < nr; i++) {
		for (j = 0; j < sgls[i].sg_nr; j++) {
			d_iov_t *iov = &sgls[i].sg_iovs[j];

			D_ALLOC(iov->iov_buf, iov->iov_buf_len);
			if (iov->iov_buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}
	ioc->ioc_free_sgls = 1;
out:
	if (rc) {
		for (i = 0; i < nr; i++) {
			for (i = 0; j < sgls[i].sg_nr; j++)
				D_FREE(sgls[i].sg_iovs[j].iov_buf);
		}
	}

	return rc;
}

static void
daos_iod_recx_free(daos_iod_t *iods, uint32_t iod_nr)
{
	uint32_t	i;

	if (iods != NULL) {
		for (i = 0; i < iod_nr; i++)
			D_FREE(iods[i].iod_recxs);
		D_FREE(iods);
	}
}

/** Duplicate iod and recx, reuse original iod's dkey/akey, reallocate recxs. */
static int
daos_iod_recx_dup(daos_iod_t *iods, uint32_t iod_nr, daos_iod_t **iods_dup_ptr)
{
	daos_iod_t	*iods_dup;
	daos_iod_t	*src, *dst;
	uint32_t	 i;

	D_ALLOC_ARRAY(iods_dup, iod_nr);
	if (iods_dup == NULL)
		return -DER_NOMEM;

	for (i = 0; i < iod_nr; i++) {
		src = &iods[i];
		dst = &iods_dup[i];
		*dst = *src;
		if (src->iod_nr == 0 || src->iod_recxs == NULL)
			continue;

		D_ALLOC_ARRAY(dst->iod_recxs, dst->iod_nr);
		if (dst->iod_recxs == NULL) {
			daos_iod_recx_free(iods_dup, iod_nr);
			return -DER_NOMEM;
		}

		memcpy(dst->iod_recxs, src->iod_recxs,
		       sizeof(*dst->iod_recxs) * dst->iod_nr);
	}

	*iods_dup_ptr = iods_dup;

	return 0;
}

static bool
obj_ec_recov_need_try_again(struct obj_rw_in *orw, struct obj_io_context *ioc)
{
	D_ASSERT(orw->orw_flags & ORF_EC_RECOV);

	if (DAOS_FAIL_CHECK(DAOS_FAIL_AGG_BOUNDRY_MOVED))
		return true;

	/* agg_eph_boundry advanced, possibly cause epoch of EC data recovery
	 * cannot get corresponding parity/data exts, need to retry the degraded
	 * fetch from beginning. For ORF_EC_RECOV_SNAP case, need not retry as
	 * that flag was only set when (snapshot_epoch < sc_ec_agg_eph_boundry).
	 */
	if ((orw->orw_flags & ORF_EC_RECOV_SNAP) == 0 &&
	    (orw->orw_flags & ORF_FOR_MIGRATION) == 0 &&
	    orw->orw_epoch < ioc->ioc_coc->sc_ec_agg_eph_boundry)
		return true;

	return false;
}

static int
obj_local_rw_internal(crt_rpc_t *rpc, struct obj_io_context *ioc,
		      daos_iod_t *split_iods, struct dcs_iod_csums *split_csums,
		      uint64_t *split_offs, struct dtx_handle *dth)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	struct dcs_iod_csums		*iod_csums;
	uint32_t			tag = dss_get_module_info()->dmi_tgt_id;
	daos_handle_t			ioh = DAOS_HDL_INVAL;
	struct bio_desc			*biod;
	daos_key_t			*dkey;
	crt_bulk_op_t			bulk_op;
	bool				rma;
	bool				bulk_bind;
	bool				create_map;
	bool				spec_fetch = false;
	bool				iod_converted = false;
	struct daos_recx_ep_list	*recov_lists = NULL;
	daos_iod_t			*iods;
	uint64_t			*offs;
	uint64_t			 cond_flags;
	daos_iod_t			*iods_dup = NULL; /* for EC deg fetch */
	bool				 get_parity_list = false;
	struct daos_recx_ep_list	*parity_list = NULL;
	int				err, rc = 0;

	create_map = orw->orw_flags & ORF_CREATE_MAP;

	iods = split_iods == NULL ? orw->orw_iod_array.oia_iods : split_iods;
	offs = split_offs == NULL ? orw->orw_iod_array.oia_offs : split_offs;
	iod_csums = split_csums == NULL ? orw->orw_iod_array.oia_iod_csums :
					 split_csums;

	if (daos_obj_is_echo(orw->orw_oid.id_pub) ||
	    (daos_io_bypass & IOBP_TARGET)) {
		obj_echo_rw(rpc, split_iods, split_offs);
		D_GOTO(out, rc = 0);
	}

	rc = csum_verify_keys(ioc->ioc_coc->sc_csummer, &orw->orw_dkey,
			      orw->orw_dkey_csum, &orw->orw_iod_array,
			      &orw->orw_oid);

	if (rc != 0) {
		D_ERROR(DF_C_UOID_DKEY"verify_keys error: "DF_RC"\n",
			DP_C_UOID_DKEY(orw->orw_oid, &orw->orw_dkey),
			DP_RC(rc));
		if (rc == -DER_CSUM)
			obj_log_csum_err();
		return rc;
	}
	dkey = (daos_key_t *)&orw->orw_dkey;
	D_DEBUG(DB_IO,
		"opc %d oid "DF_UOID" dkey "DF_KEY" tag %d epc "DF_X64".\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid), DP_KEY(dkey),
		tag, orw->orw_epoch);

	rma = (orw->orw_bulks.ca_arrays != NULL ||
	       orw->orw_bulks.ca_count != 0);
	cond_flags = orw->orw_api_flags;

	/* Prepare IO descriptor */
	if (obj_rpc_is_update(rpc)) {
		obj_singv_ec_rw_filter(orw->orw_oid, &ioc->ioc_oca, iods, offs,
				       orw->orw_epoch, orw->orw_flags,
				       orw->orw_start_shard,
				       orw->orw_nr, true, false, NULL);
		bulk_op = CRT_BULK_GET;

		/** Fault injection - corrupt data from network */
		if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE)  && !rma) {
			D_ERROR("csum: Corrupting data (network)\n");
			dcf_corrupt(orw->orw_sgls.ca_arrays,
				    orw->orw_sgls.ca_count);
		}

		if (rma && ioc->ioc_coc->sc_props.dcp_dedup_enabled) {
			cond_flags |= VOS_OF_DEDUP;
			if (ioc->ioc_coc->sc_props.dcp_dedup_verify)
				cond_flags |= VOS_OF_DEDUP_VERIFY;
		}

		rc = vos_update_begin(ioc->ioc_vos_coh, orw->orw_oid,
			      orw->orw_epoch, cond_flags, dkey,
			      orw->orw_nr, iods, iod_csums,
			      ioc->ioc_coc->sc_props.dcp_dedup_size,
			      &ioh, dth);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
				DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}
	} else {
		uint32_t			 fetch_flags = 0;
		bool				 ec_deg_fetch;
		bool				 ec_recov;
		bool				 is_parity_shard;
		struct daos_recx_ep_list	*shadows = NULL;

		bulk_op = CRT_BULK_PUT;
		if (!rma && orw->orw_sgls.ca_arrays == NULL) {
			spec_fetch = true;
			if (orw->orw_flags & ORF_CHECK_EXISTENCE)
				fetch_flags = VOS_OF_FETCH_CHECK_EXISTENCE;
			else
				fetch_flags = VOS_OF_FETCH_SIZE_ONLY;
		}

		ec_deg_fetch = orw->orw_flags & ORF_EC_DEGRADED;
		ec_recov = orw->orw_flags & ORF_EC_RECOV;
		D_ASSERTF(ec_recov == false || ec_deg_fetch == false,
			  "ec_recov %d, ec_deg_fetch %d.\n",
			  ec_recov, ec_deg_fetch);
		is_parity_shard = orw->orw_oid.id_shard >=
				  obj_ec_data_tgt_nr(&ioc->ioc_oca);
		get_parity_list = ec_recov && is_parity_shard &&
				  ((orw->orw_flags & ORF_EC_RECOV_SNAP) == 0);
		if (get_parity_list) {
			D_ASSERT(!ec_deg_fetch);
			fetch_flags |= VOS_OF_FETCH_RECX_LIST;
		}
		if (unlikely(ec_recov &&
			     obj_ec_recov_need_try_again(orw, ioc))) {
			rc = -DER_FETCH_AGAIN;
			D_DEBUG(DB_IO, DF_UOID" "DF_X64"<"DF_X64
				" ec_recov needs redo, "DF_RC".\n",
				DP_UOID(orw->orw_oid), orw->orw_epoch,
				ioc->ioc_coc->sc_ec_agg_eph_boundry,
				DP_RC(rc));
			goto out;
		}
		if (ec_deg_fetch && !spec_fetch) {
			if (orwo->orw_rels.ca_arrays != NULL) {
				/* Re-entry case */
				daos_recx_ep_list_free(orwo->orw_rels.ca_arrays,
						       orwo->orw_rels.ca_count);
				orwo->orw_rels.ca_arrays = NULL;
				orwo->orw_rels.ca_count = 0;
			}

			/* Copy the iods to make it reentrant, as the
			 * obj_fetch_shadow() possibly change the iod.
			 */
			rc = daos_iod_recx_dup(iods, orw->orw_nr, &iods_dup);
			if (rc != 0) {
				D_ERROR(DF_UOID"iod_recx_dup failed: "DF_RC"\n",
					DP_UOID(orw->orw_oid), DP_RC(rc));
				goto out;
			}

			D_ASSERT(iods_dup != NULL);
			iods = iods_dup;

			rc = obj_fetch_shadow(ioc, orw->orw_oid,
					      orw->orw_epoch, cond_flags, dkey,
					      orw->orw_nr, iods,
					      orw->orw_tgt_idx, dth, &shadows);
			if (rc) {
				D_ERROR(DF_UOID" Fetch shadow failed: "DF_RC
					"\n", DP_UOID(orw->orw_oid), DP_RC(rc));
				goto out;
			}
			iod_converted = true;
		}

		rc = vos_fetch_begin(ioc->ioc_vos_coh, orw->orw_oid,
				     orw->orw_epoch, dkey, orw->orw_nr, iods,
				     cond_flags | fetch_flags, shadows, &ioh,
				     dth);
		daos_recx_ep_list_free(shadows, orw->orw_nr);
		if (rc) {
			D_CDEBUG(rc == -DER_INPROGRESS || rc == -DER_NONEXIST ||
				 rc == -DER_TX_RESTART, DB_IO, DLOG_ERR,
				 "Fetch begin for "DF_UOID" failed: "DF_RC"\n",
				 DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}

		if (get_parity_list) {
			parity_list = vos_ioh2recx_list(ioh);
			if (parity_list != NULL) {
				orwo->orw_rels.ca_arrays = parity_list;
				orwo->orw_rels.ca_count = orw->orw_nr;
			}
		}

		rc = obj_set_reply_sizes(rpc, iods, orw->orw_nr);
		if (rc != 0)
			goto out;

		if (rma) {
			orwo->orw_sgls.ca_count = 0;
			orwo->orw_sgls.ca_arrays = NULL;

			rc = obj_set_reply_nrs(rpc, ioh, NULL);
			if (rc != 0)
				goto out;
		} else {
			rc = obj_prep_fetch_sgls(rpc, ioc);
			if (rc)
				goto out;
		}
		if (ec_deg_fetch) {
			D_ASSERT(!get_parity_list);
			recov_lists = vos_ioh2recx_list(ioh);
		}
		rc = obj_singv_ec_rw_filter(orw->orw_oid, &ioc->ioc_oca,
					    iods, offs, orw->orw_epoch,
					    orw->orw_flags,
					    orw->orw_start_shard,
					    orw->orw_nr, false,
					    ec_deg_fetch, &recov_lists);
		if (rc != 0) {
			D_ERROR(DF_UOID" obj_singv_ec_rw_filter failed: "
				DF_RC".\n", DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}
		if (recov_lists != NULL) {
			daos_epoch_t	vos_agg_epoch;
			daos_epoch_t	recov_epoch = 0;
			bool		recov_snap = false;

			/* If fetch from snapshot, and snapshot epoch lower than
			 * vos agg epoch boundary, recovery from snapshot epoch.
			 * Or, will recovery from max{parity_epoch, vos_epoch_
			 * boundary}.
			 */
			vos_agg_epoch = ioc->ioc_coc->sc_ec_agg_eph_boundry;
			if (ioc->ioc_fetch_snap &&
			    orw->orw_epoch < vos_agg_epoch) {
				recov_epoch = orw->orw_epoch;
				recov_snap =  true;
			} else {
				recov_epoch = vos_agg_epoch;
			}
			daos_recx_ep_list_set(recov_lists, orw->orw_nr,
					      recov_epoch, recov_snap);
			orwo->orw_rels.ca_arrays = recov_lists;
			orwo->orw_rels.ca_count = orw->orw_nr;
		}
	}

	if (orw->orw_flags & ORF_CHECK_EXISTENCE)
		goto out;

	biod = vos_ioh2desc(ioh);
	rc = bio_iod_prep(biod, BIO_CHK_TYPE_IO, rma ? rpc->cr_ctx : NULL,
			  CRT_BULK_RW);
	if (rc) {
		D_ERROR(DF_UOID" bio_iod_prep failed: "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		goto out;
	}

	if (obj_rpc_is_fetch(rpc) && !spec_fetch &&
	    daos_csummer_initialized(ioc->ioc_coc->sc_csummer)) {
		rc = obj_fetch_csum_init(ioc->ioc_coc, orw, orwo);
		if (rc) {
			D_ERROR(DF_UOID" fetch csum init failed: %d.\n",
				DP_UOID(orw->orw_oid), rc);
			goto post;
		}

		if (ioc->ioc_coc->sc_props.dcp_csum_enabled) {
			rc = csum_add2iods(ioh,
					   orw->orw_iod_array.oia_iods,
					   orw->orw_iod_array.oia_iod_nr,
					   ioc->ioc_coc->sc_csummer,
					   orwo->orw_iod_csums.ca_arrays,
					   orw->orw_oid, &orw->orw_dkey);
			if (rc) {
				D_ERROR(DF_UOID" fetch verify failed: %d.\n",
					DP_UOID(orw->orw_oid), rc);
				goto post;

			}
		}
	}

	if (rma) {
		bulk_bind = orw->orw_flags & ORF_BULK_BIND;
		rc = obj_bulk_transfer(rpc, bulk_op, bulk_bind,
				       orw->orw_bulks.ca_arrays, offs,
				       ioh, NULL, orw->orw_nr, NULL);
		if (rc == 0) {
			bio_iod_flush(biod);

			/* Timeout for the update RPC from client to server is
			 * 3 seconds. Here, make the server to sleep more than
			 * 3 seconds (3.1) to simulate the case of server being
			 * blocked during bulk data transfer, then client will
			 * get RPC timeout and trigger resend.
			 */
			if (obj_rpc_is_update(rpc) &&
			    !(orw->orw_flags & ORF_RESEND) &&
			    DAOS_FAIL_CHECK(DAOS_DTX_RESEND_DELAY1))
				rc = dss_sleep(3100);
		}
	} else if (orw->orw_sgls.ca_arrays != NULL) {
		rc = bio_iod_copy(biod, orw->orw_sgls.ca_arrays, orw->orw_nr);
	}

	if (rc) {
		if (rc == -DER_OVERFLOW)
			rc = -DER_REC2BIG;

		D_CDEBUG(rc == -DER_REC2BIG, DLOG_DBG, DLOG_ERR,
			 DF_UOID" data transfer failed, dma %d rc "DF_RC"",
			 DP_UOID(orw->orw_oid), rma, DP_RC(rc));
		D_GOTO(post, rc);
	}

	if (obj_rpc_is_update(rpc)) {
		rc = vos_dedup_verify(ioh);
		if (rc)
			goto post;

		rc = obj_verify_bio_csum(orw->orw_oid.id_pub, iods, iod_csums,
					 biod, ioc->ioc_coc->sc_csummer,
					 orw->orw_iod_array.oia_iod_nr);
		if (rc != 0)
			D_ERROR(DF_C_UOID_DKEY " verify_bio_csum failed: "
				DF_RC"\n",
				DP_C_UOID_DKEY(orw->orw_oid, dkey),
				DP_RC(rc));
		/** CSUM Verified on update, now corrupt to fake corruption
		 * on disk
		 */
		if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_DISK) && !rma) {
			D_ERROR("csum: Corrupting data (DISK)\n");
			dcf_corrupt(orw->orw_sgls.ca_arrays,
				    orw->orw_sgls.ca_count);
		}
	}
	if (obj_rpc_is_fetch(rpc) && create_map) {
		/* EC degraded fetch converted original iod to replica daos ext,
		 * need to convert back to vos ext before creating iom, or the
		 * client-side dc_rw_cb_csum_verify() may not work.
		 */
		if (iod_converted)
			obj_iod_recx_daos2vos(orw->orw_nr, iods, &ioc->ioc_oca);

		rc = obj_fetch_create_maps(rpc, biod, iods);
	}

	if (rc == -DER_CSUM)
		obj_log_csum_err();
post:
	err = bio_iod_post(biod);
	rc = rc ? : err;
out:
	rc = obj_rw_complete(rpc, ioc, ioh, rc, dth);
	if (iods_dup != NULL)
		daos_iod_recx_free(iods_dup, orw->orw_nr);
	return rc;
}

static int
obj_local_rw(crt_rpc_t *rpc, struct obj_io_context *ioc,
	     daos_iod_t *split_iods, struct dcs_iod_csums *split_csums,
	     uint64_t *split_offs, struct dtx_handle *dth, bool pin)
{
	int	rc;

again:
	if (pin) {
		rc = vos_dtx_pin(dth, false);
		if (rc != 0)
			return rc;
	}

	rc = obj_local_rw_internal(rpc, ioc, split_iods, split_csums,
				   split_offs, dth);
	if (obj_dtx_need_refresh(dth, rc)) {
		rc = dtx_refresh(dth, ioc->ioc_coc);
		if (rc == -DER_AGAIN)
			goto again;
	}

	return rc;
}

static int
obj_capa_check(struct ds_cont_hdl *coh, bool is_write, bool is_agg_migrate)
{
	if (!is_write && !ds_sec_cont_can_read_data(coh->sch_sec_capas)) {
		D_ERROR("cont hdl "DF_UUID" sec_capas "DF_U64", "
			"NO_PERM to read.\n",
			DP_UUID(coh->sch_uuid), coh->sch_sec_capas);
		return -DER_NO_PERM;
	}

	if (is_write && !ds_sec_cont_can_write_data(coh->sch_sec_capas)) {
		D_ERROR("cont hdl "DF_UUID" sec_capas "DF_U64", "
			"NO_PERM to update.\n",
			DP_UUID(coh->sch_uuid), coh->sch_sec_capas);
		return -DER_NO_PERM;
	}

	if (!is_agg_migrate && coh->sch_cont && coh->sch_cont->sc_rw_disabled)
		return -DER_RF;

	return 0;
}

/**
 * Lookup and return the container handle, if it is a rebuild handle, which
 * will never associate a particular container, then the container structure
 * will be returned to \a ioc::ioc_coc.
 */
static int
obj_ioc_init(uuid_t pool_uuid, uuid_t coh_uuid, uuid_t cont_uuid, int opc,
	     struct obj_io_context *ioc)
{
	struct ds_cont_hdl   *coh;
	struct ds_cont_child *coc;
	int		      rc;

	D_ASSERT(ioc != NULL);
	memset(ioc, 0, sizeof(*ioc));
	ioc->ioc_opc = opc;

	rc = ds_cont_find_hdl(pool_uuid, coh_uuid, &coh);
	if (rc) {
		if (rc == -DER_NONEXIST)
			rc = -DER_NO_HDL;
		return rc;
	}

	/* normal container open handle with ds_cont_child attached */
	if (coh->sch_cont != NULL) {
		ds_cont_child_get(coh->sch_cont);
		coc = coh->sch_cont;
		if (uuid_compare(cont_uuid, coc->sc_uuid) == 0)
			D_GOTO(out, rc = 0);

		D_ERROR("Stale container handle "DF_UUID" != "DF_UUID"\n",
			DP_UUID(cont_uuid), DP_UUID(coh->sch_uuid));
		D_GOTO(failed, rc = -DER_NONEXIST);
	} else {
		/**
		 * The server handle is a dummy and never attached by
		 * a real container
		 */
		D_DEBUG(DB_TRACE, DF_UUID"/%p is server cont hdl\n",
			DP_UUID(coh_uuid), coh);
	}

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_HDL))
		D_GOTO(failed, rc = -DER_NO_HDL);

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_STALE_POOL))
		D_GOTO(failed, rc = -DER_STALE);

	/* load VOS container on demand for rebuild */
	rc = ds_cont_child_lookup(pool_uuid, cont_uuid, &coc);
	if (rc) {
		D_ERROR("Can not find the container "DF_UUID"/"DF_UUID"\n",
			DP_UUID(pool_uuid), DP_UUID(cont_uuid));
		D_GOTO(failed, rc);
	}

	/* load csummer on demand for rebuild if not already loaded */
	rc = ds_cont_csummer_init(coc);
	if (rc)
		D_GOTO(failed, rc);
out:
	D_ASSERT(coc->sc_pool != NULL);
	ioc->ioc_map_ver = coc->sc_pool->spc_map_version;
	ioc->ioc_vos_coh = coc->sc_hdl;
	ioc->ioc_coc	 = coc;
	ioc->ioc_coh	 = coh;
	return 0;
failed:
	ds_cont_hdl_put(coh);
	return rc;
}

static void
obj_ioc_fini(struct obj_io_context *ioc)
{
	if (ioc->ioc_coh != NULL) {
		ds_cont_hdl_put(ioc->ioc_coh);
		ioc->ioc_coh = NULL;
	}

	if (ioc->ioc_coc != NULL) {
		ds_cont_child_put(ioc->ioc_coc);
		ioc->ioc_coc = NULL;
	}
}

/* Setup lite IO context, it is only for compound RPC so far:
 * - no associated object yet
 * - no permission check (not sure it's read/write)
 */
static int
obj_ioc_begin_lite(uint32_t rpc_map_ver, uuid_t pool_uuid,
		   uuid_t coh_uuid, uuid_t cont_uuid, uint32_t opc,
		   struct obj_io_context *ioc)
{
	struct obj_tls		*tls;
	struct ds_pool_child	*poc;
	int			rc;

	rc = obj_ioc_init(pool_uuid, coh_uuid, cont_uuid, opc, ioc);
	if (rc)
		return rc;

	poc = ioc->ioc_coc->sc_pool;
	D_ASSERT(poc != NULL);

	if (unlikely(poc->spc_pool->sp_map == NULL ||
		     DAOS_FAIL_CHECK(DAOS_FORCE_REFRESH_POOL_MAP))) {
		/* XXX: Client (or leader replica) has newer pool map than
		 *	current replica. Two possible cases:
		 *
		 *	1. The current replica was the old leader if with
		 *	   the old pool map version. According to current
		 *	   leader election algorithm, it is still the new
		 *	   leader with the new pool map version. Since no
		 *	   leader switch, the unmatched pool version will
		 *	   not affect DTX related availability check.
		 *
		 *	2. The current replica was NOT the old leader if
		 *	   with the old pool map version. But it becomes
		 *	   the new leader with the new pool map version.
		 *	   In the subsequent modificaiton, it may hit
		 *	   some 'prepared' DTX when make availability
		 *	   check, it will return -DER_INPROGRESS that
		 *	   will cause client to retry. It is possible
		 *	   that the pool map version event arrives at
		 *	   this server during the client retry. It is
		 *	   inefficient, but harmless.
		 */
		D_DEBUG(DB_IO, "stale server map_version %d req %d\n",
			ioc->ioc_map_ver, rpc_map_ver);
		rc = ds_pool_child_map_refresh_async(poc);
		if (rc == 0) {
			ioc->ioc_map_ver = poc->spc_map_version;
			rc = -DER_STALE;
		}

		D_GOTO(out, rc);
	} else if (unlikely(rpc_map_ver < ioc->ioc_map_ver)) {
		D_DEBUG(DB_IO, "stale version req %d map_version %d\n",
			rpc_map_ver, ioc->ioc_map_ver);

		/* For distributed transaction, restart the DTX if using
		 * stale pool map.
		 */
		if (opc == DAOS_OBJ_RPC_CPD)
			D_GOTO(out, rc = -DER_TX_RESTART);

		D_GOTO(out, rc = -DER_STALE);
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_STALE_PM)) {
		D_GOTO(out, rc = -DER_STALE);
	}

out:
	dss_rpc_cntr_enter(DSS_RC_OBJ);
	/** increment active request counter and start the chrono */
	tls = obj_tls_get();
	d_tm_inc_gauge(tls->ot_op_active[opc], 1);
	ioc->ioc_start_time = daos_get_ntime();
	ioc->ioc_began = 1;
	return rc;
}

static inline unsigned int
lat_bucket(uint64_t size)
{
	int nr;

	if (size <= 256)
		return 0;

	/** return number of leading zero-bits */
	nr =  __builtin_clzl(size - 1);

	/** >4MB, return last bucket */
	if (nr < 42)
		return NR_LATENCY_BUCKETS - 1;

	return 56 - nr;
}

static inline void
obj_update_sensors(struct obj_io_context *ioc, int err)
{
	struct obj_tls		*tls = obj_tls_get();
	struct obj_pool_metrics	*opm;
	struct d_tm_node_t	*lat;
	uint32_t		opc = ioc->ioc_opc;
	uint64_t		time;

	opm = ioc->ioc_coc->sc_pool->spc_metrics[DAOS_OBJ_MODULE];

	d_tm_dec_gauge(tls->ot_op_active[opc], 1);
	d_tm_inc_counter(opm->opm_total[opc], 1);

	if (unlikely(err != 0))
		return;

	/**
	 * Measure latency of successful I/O only.
	 * Use bit shift for performance and tolerate some inaccuracy.
	 */
	time = daos_get_ntime() - ioc->ioc_start_time;
	time >>= 10;

	switch (opc) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
		d_tm_inc_counter(opm->opm_update_bytes, ioc->ioc_io_size);
		lat = tls->ot_update_lat[lat_bucket(ioc->ioc_io_size)];
		break;
	case DAOS_OBJ_RPC_FETCH:
		d_tm_inc_counter(opm->opm_fetch_bytes, ioc->ioc_io_size);
		lat = tls->ot_fetch_lat[lat_bucket(ioc->ioc_io_size)];
		break;
	default:
		lat = tls->ot_op_lat[opc];
	}
	d_tm_set_gauge(lat, time);
}

static void
obj_ioc_end(struct obj_io_context *ioc, int err)
{
	if (likely(ioc->ioc_began)) {
		dss_rpc_cntr_exit(DSS_RC_OBJ, !!err);
		ioc->ioc_began = 0;

		/** Update sensors */
		obj_update_sensors(ioc, err);
	}
	obj_ioc_fini(ioc);
}

static int
obj_ioc_init_oca(struct obj_io_context *ioc, daos_obj_id_t oid)
{
	struct daos_oclass_attr *oca;
	bool			 priv;

	oca = daos_oclass_attr_find(oid, &priv);
	if (!oca)
		return -DER_INVAL;

	ioc->ioc_oca = *oca;
	if (daos_oclass_is_ec(oca) && !priv) {
		/* don't ovewrite cell size of private class */
		D_ASSERT(ioc->ioc_coc != NULL);
		ioc->ioc_oca.u.ec.e_len = ioc->ioc_coc->sc_props.dcp_ec_cell_sz;
		D_ASSERT(ioc->ioc_oca.u.ec.e_len != 0);
	}
	return 0;
}

/* Various check before access VOS */
static int
obj_ioc_begin(daos_obj_id_t oid, uint32_t rpc_map_ver, uuid_t pool_uuid,
	      uuid_t coh_uuid, uuid_t cont_uuid, uint32_t opc, uint32_t flags,
	      struct obj_io_context *ioc)
{
	int		rc;

	rc = obj_ioc_begin_lite(rpc_map_ver, pool_uuid, coh_uuid, cont_uuid,
			      opc, ioc);
	if (rc != 0)
		return rc;

	rc = obj_capa_check(ioc->ioc_coh, obj_is_modification_opc(opc),
			    obj_is_ec_agg_opc(opc) ||
			    (flags & ORF_FOR_MIGRATION) ||
			    (flags & ORF_FOR_EC_AGG));
	if (rc != 0)
		goto failed;

	rc = obj_ioc_init_oca(ioc, oid);
	if (rc != 0)
		goto failed;
	return 0;
failed:
	obj_ioc_end(ioc, rc);
	return rc;
}

static uint64_t
orf_to_dtx_epoch_flags(enum obj_rpc_flags orf_flags)
{
	uint64_t flags = 0;

	if (orf_flags & ORF_EPOCH_UNCERTAIN)
		flags |= DTX_EPOCH_UNCERTAIN;
	return flags;
}

void
ds_obj_ec_rep_handler(crt_rpc_t *rpc)
{
	struct obj_ec_rep_in	*oer = crt_req_get(rpc);
	struct obj_ec_rep_out	*oero = crt_reply_get(rpc);
	daos_key_t		*dkey;
	daos_iod_t		*iod;
	struct dcs_iod_csums	*iod_csums;
	struct bio_desc		*biod;
	daos_recx_t		 recx = { 0 };
	daos_epoch_range_t	 epoch_range = { 0 };
	struct obj_io_context	 ioc;
	daos_handle_t		 ioh = DAOS_HDL_INVAL;
	int			 rc;

	D_ASSERT(oer != NULL);
	D_ASSERT(oero != NULL);

	rc = obj_ioc_begin(oer->er_oid.id_pub, oer->er_map_ver,
			   oer->er_pool_uuid, oer->er_coh_uuid,
			   oer->er_cont_uuid, opc_get(rpc->cr_opc), 0, &ioc);
	if (rc)	{
		D_ERROR("ioc_begin failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	if (!daos_oclass_is_ec(&ioc.ioc_oca)) {
		rc = -DER_PROTO;
		goto out;
	}

	D_ASSERT(ioc.ioc_coc != NULL);
	dkey = (daos_key_t *)&oer->er_dkey;
	iod = (daos_iod_t *)&oer->er_iod;
	iod_csums = oer->er_iod_csums.ca_arrays;
	rc = vos_update_begin(ioc.ioc_coc->sc_hdl, oer->er_oid,
			      oer->er_epoch, 0, dkey, 1, iod, iod_csums,
			      0, &ioh, NULL);
	if (rc) {
		D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
			DP_UOID(oer->er_oid), DP_RC(rc));
		goto out;
	}
	biod = vos_ioh2desc(ioh);
	rc = bio_iod_prep(biod, BIO_CHK_TYPE_IO, rpc->cr_ctx, CRT_BULK_RW);
	if (rc) {
		D_ERROR(DF_UOID" bio_iod_prep failed: "DF_RC".\n",
			DP_UOID(oer->er_oid), DP_RC(rc));
		goto out;
	}
	rc = obj_bulk_transfer(rpc, CRT_BULK_PUT, false, &oer->er_bulk, NULL,
			       ioh, NULL, 1, NULL);
	if (rc) {
		D_ERROR(DF_UOID" bulk transfer failed: "DF_RC".\n",
			DP_UOID(oer->er_oid), DP_RC(rc));
		goto out;
	}

	rc = bio_iod_post(biod);
	if (rc) {
		D_ERROR(DF_UOID" bio_iod_post failed: "DF_RC".\n",
			DP_UOID(oer->er_oid), DP_RC(rc));
		goto out;
	}
	rc = vos_update_end(ioh, ioc.ioc_map_ver, dkey, rc,
			    &ioc.ioc_io_size, NULL);
	if (rc) {
		D_ERROR(DF_UOID" vos_update_end failed: "DF_RC".\n",
			DP_UOID(oer->er_oid), DP_RC(rc));
		goto out;
	}
	epoch_range.epr_lo = 0ULL;
	epoch_range.epr_hi = oer->er_epoch;
	recx.rx_nr = obj_ioc2ec_cs(&ioc);
	recx.rx_idx = (oer->er_stripenum * recx.rx_nr) | PARITY_INDICATOR;
	rc = vos_obj_array_remove(ioc.ioc_coc->sc_hdl, oer->er_oid,
				  &epoch_range, dkey, &iod->iod_name, &recx);
out:
	obj_rw_reply(rpc, rc, 0, &ioc);
	obj_ioc_end(&ioc, rc);
}

void
ds_obj_ec_agg_handler(crt_rpc_t *rpc)
{
	struct obj_ec_agg_in	*oea = crt_req_get(rpc);
	struct obj_ec_agg_out	*oeao = crt_reply_get(rpc);
	daos_key_t		*dkey;
	struct bio_desc		*biod;
	daos_iod_t		*iod = &oea->ea_iod;
	struct dcs_iod_csums	*iod_csums = oea->ea_iod_csums.ca_arrays;

	crt_bulk_t		 parity_bulk = oea->ea_bulk;
	daos_recx_t		 recx = { 0 };
	struct obj_io_context	 ioc;
	daos_handle_t		 ioh = DAOS_HDL_INVAL;
	int			 rc;
	int			 rc1;

	D_ASSERT(oea != NULL);
	D_ASSERT(oeao != NULL);

	rc = obj_ioc_begin(oea->ea_oid.id_pub, oea->ea_map_ver,
			   oea->ea_pool_uuid, oea->ea_coh_uuid,
			   oea->ea_cont_uuid, opc_get(rpc->cr_opc), 0, &ioc);

	if (rc)	{
		D_ERROR("ioc_begin failed: "DF_RC"\n", DP_RC(rc));
		goto out;
	}
	if (!daos_oclass_is_ec(&ioc.ioc_oca)) {
		rc = -DER_PROTO;
		goto out;
	}

	D_ASSERT(ioc.ioc_coc != NULL);
	dkey = (daos_key_t *)&oea->ea_dkey;
	if (parity_bulk != CRT_BULK_NULL) {
		rc = vos_update_begin(ioc.ioc_coc->sc_hdl, oea->ea_oid,
				      oea->ea_epoch_range.epr_hi, 0, dkey, 1,
				      iod, iod_csums, 0, &ioh, NULL);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
				DP_UOID(oea->ea_oid), DP_RC(rc));
			goto out;
		}
		biod = vos_ioh2desc(ioh);
		rc = bio_iod_prep(biod, BIO_CHK_TYPE_IO, rpc->cr_ctx,
				  CRT_BULK_RW);
		if (rc) {
			D_ERROR(DF_UOID" bio_iod_prep failed: "DF_RC".\n",
				DP_UOID(oea->ea_oid), DP_RC(rc));
			goto out;
		}
		rc = obj_bulk_transfer(rpc, CRT_BULK_GET, false, &oea->ea_bulk,
				       NULL, ioh, NULL, 1, NULL);
		if (rc) {
			D_ERROR(DF_UOID" bulk transfer failed: "DF_RC".\n",
				DP_UOID(oea->ea_oid), DP_RC(rc));
			goto out;
		}

		rc = bio_iod_post(biod);
		if (rc) {
			D_ERROR(DF_UOID" bio_iod_post failed: "DF_RC".\n",
				DP_UOID(oea->ea_oid), DP_RC(rc));
			goto out;
		}

		rc = vos_update_end(ioh, ioc.ioc_map_ver, dkey, rc,
				    &ioc.ioc_io_size, NULL);
		if (rc) {
			if (rc == -DER_NO_PERM) {
				/* Parity already exists, May need a
				 * different error code.
				 */
				D_DEBUG(DB_EPC, DF_UOID" parity already"
					" exists\n", DP_UOID(oea->ea_oid));
				rc = 0;
			} else {
				D_ERROR(DF_UOID" vos_update_end failed: "
					DF_RC".\n", DP_UOID(oea->ea_oid),
					DP_RC(rc));
				D_GOTO(out, rc);
			}
		}
	}

	/* Since parity update has succeed, so let's ignore the failure
	 * of replica remove, otherwise it will cause the leader not
	 * updating its parity, then the parity will not be consistent.
	 */
	recx.rx_idx = oea->ea_stripenum * obj_ioc2ec_ss(&ioc);
	recx.rx_nr = obj_ioc2ec_ss(&ioc);
	rc1 = vos_obj_array_remove(ioc.ioc_coc->sc_hdl, oea->ea_oid,
				  &oea->ea_epoch_range, dkey,
				  &iod->iod_name, &recx);
	if (rc1)
		D_ERROR(DF_UOID"array_remove failed: "DF_RC"\n",
			DP_UOID(oea->ea_oid), DP_RC(rc1));
out:
	obj_rw_reply(rpc, rc, 0, &ioc);
	obj_ioc_end(&ioc, rc);
}

void
ds_obj_tgt_update_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	daos_key_t			*dkey = &orw->orw_dkey;
	struct obj_io_context		 ioc;
	struct dtx_handle                dth = { 0 };
	struct dtx_memberships		*mbs = NULL;
	struct daos_shard_tgt		*tgts = NULL;
	uint32_t			 tgt_cnt;
	uint32_t			 opc = opc_get(rpc->cr_opc);
	uint32_t			 dtx_flags = 0;
	struct dtx_epoch		 epoch;
	int				 rc;

	D_ASSERT(orw != NULL);
	D_ASSERT(orwo != NULL);

	rc = obj_ioc_begin(orw->orw_oid.id_pub, orw->orw_map_ver,
			   orw->orw_pool_uuid, orw->orw_co_hdl,
			   orw->orw_co_uuid, opc_get(rpc->cr_opc),
			   orw->orw_flags, &ioc);
	if (rc)
		goto out;

	if (DAOS_FAIL_CHECK(DAOS_VC_DIFF_DKEY)) {
		unsigned char	*buf = dkey->iov_buf;
		buf[0] += orw->orw_oid.id_shard + 1;
		orw->orw_dkey_hash = obj_dkey2hash(orw->orw_oid.id_pub, dkey);
	}

	D_DEBUG(DB_IO,
		"rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_X64", pmv %u/%u dti "DF_DTI".\n",
		rpc, opc, DP_UOID(orw->orw_oid), DP_KEY(dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id, orw->orw_epoch,
		orw->orw_map_ver, ioc.ioc_map_ver, DP_DTI(&orw->orw_dti));

	/* Handle resend. */
	if (orw->orw_flags & ORF_RESEND) {
		rc = dtx_handle_resend(ioc.ioc_vos_coh, &orw->orw_dti,
				       &orw->orw_epoch, NULL);

		/* Do nothing if 'prepared' or 'committed'. */
		if (rc == -DER_ALREADY || rc == 0)
			D_GOTO(out, rc = 0);

		/* Abort it firstly if exist but with different epoch,
		 * then re-execute with new epoch.
		 */
		if (rc == -DER_MISMATCH)
			/* Abort it by force with MAX epoch to guarantee
			 * that it can be aborted.
			 */
			rc = vos_dtx_abort(ioc.ioc_vos_coh, DAOS_EPOCH_MAX,
					   &orw->orw_dti, 1);

		if (rc < 0 && rc != -DER_NONEXIST)
			D_GOTO(out, rc);

		dtx_flags |= DTX_RESEND;
	}

	/* Inject failure for test to simulate the case of lost some
	 * record/akey/dkey on some non-leader.
	 */
	if (DAOS_FAIL_CHECK(DAOS_VC_LOST_DATA)) {
		if (orw->orw_dti_cos.ca_count > 0)
			vos_dtx_commit(ioc.ioc_vos_coh,
				       orw->orw_dti_cos.ca_arrays,
				       orw->orw_dti_cos.ca_count, NULL);

		D_GOTO(out, rc = 0);
	}

	tgts = orw->orw_shard_tgts.ca_arrays;
	tgt_cnt = orw->orw_shard_tgts.ca_count;

	if (!daos_is_zero_dti(&orw->orw_dti) && tgt_cnt != 0) {
		rc = obj_gen_dtx_mbs(tgts, orw->orw_flags & ORF_EC,
				     &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	epoch.oe_value = orw->orw_epoch;
	epoch.oe_first = orw->orw_epoch_first;
	epoch.oe_flags = orf_to_dtx_epoch_flags(orw->orw_flags);

	if (orw->orw_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	rc = dtx_begin(ioc.ioc_vos_coh, &orw->orw_dti, &epoch, 1,
		       orw->orw_map_ver, &orw->orw_oid,
		       orw->orw_dti_cos.ca_arrays,
		       orw->orw_dti_cos.ca_count, dtx_flags, mbs, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for update "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		D_GOTO(out, rc = -DER_IO);

	/* RPC may be resent during current update bulk data transfer.
	 * Pre-allocate DTX entry for handling resend under such case.
	 */

	rc = obj_local_rw(rpc, &ioc, NULL, NULL, NULL, &dth,
			  (orw->orw_bulks.ca_arrays != NULL ||
			   orw->orw_bulks.ca_count != 0) ? true : false);
	if (rc != 0)
		D_ERROR(DF_UOID": error="DF_RC".\n", DP_UOID(orw->orw_oid),
			DP_RC(rc));

out:
	rc = dtx_end(&dth, ioc.ioc_coc, rc);
	obj_rw_reply(rpc, rc, 0, &ioc);
	D_FREE(mbs);
	obj_ioc_end(&ioc, rc);
}

static int
obj_tgt_update(struct dtx_leader_handle *dlh, void *arg, int idx,
		  dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg	*exec_arg = arg;

	/* handle local operation */
	if (idx == -1) {
		struct obj_rw_in	*orw = crt_req_get(exec_arg->rpc);
		struct obj_ec_split_req	*split_req = exec_arg->args;
		daos_iod_t		*iods;
		struct dcs_iod_csums	*csums;
		uint64_t		*offs;
		int			 rc = 0;
		bool			 pin;

		if (DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
			D_GOTO(comp, rc = -DER_IO);

		/* No need re-exec local update */
		if (dlh != NULL && dlh->dlh_handle.dth_prepared)
			goto comp;

		/* XXX: For non-solo DTX, leader and non-leader will make each
		 *	own local modification in parallel. If non-leader goes
		 *	so fast as to non-leader may has already moved to handle
		 *	next RPC but the leader has not really started current
		 *	modification yet, such as being blocked at bulk data
		 *	transfer phase. Under such case, it is possible that
		 *	when the non-leader handles next request, it hits the
		 *	DTX that is just prepared locally, then non-leader will
		 *	check such DTX status with leader. But at that time,
		 *	the DTX entry on the leader does not exist, that will
		 *	misguide the non-leader as missed to abort such DTX.
		 *	To avoid such bad case, the leader need to build its
		 *	DTX entry in DRAM before dispatch current request to
		 *	non-leader.
		 *
		 *	On the other hand, even if for solo DTX, the PRC may
		 *	be delay processed because of server side heavy load.
		 *	If it is a update RPC with large data transfer, then
		 *	it is possible that client regard the update RPC is
		 *	timeout and resend the RPC during the original RPC bulk
		 *	data transfer that will yield CPU, and then the resend
		 *	logic on server will not find related DTX entry since
		 *	the DTX for original RPC is not 'prepared' yet. Under
		 *	such case, the update request will be double executed
		 *	on the server. That should be avoided. So pre-allocating
		 *	DTX entry before bulk data transfer is necessary.
		 */

		orw = crt_req_get(exec_arg->rpc);

		if (split_req != NULL) {
			iods = split_req->osr_iods;
			offs = split_req->osr_offs;
			csums = split_req->osr_iod_csums;
		} else {
			iods = NULL;
			offs = NULL;
			csums = NULL;
		}

		if (!dlh->dlh_handle.dth_solo ||
		    orw->orw_bulks.ca_arrays != NULL ||
		    orw->orw_bulks.ca_count != 0)
			pin = true;
		else
			pin = false;

		rc = obj_local_rw(exec_arg->rpc, exec_arg->ioc, iods, csums,
				  offs, &dlh->dlh_handle, pin);

comp:
		if (comp_cb != NULL)
			comp_cb(dlh, idx, rc);

		return rc;
	}

	/* Handle the object remotely */
	return ds_obj_remote_update(dlh, arg, idx, comp_cb);
}

/* Nonnegative return codes of process_epoch */
enum process_epoch_rc {
	PE_OK_REMOTE,	/* OK and epoch chosen remotely */
	PE_OK_LOCAL	/* OK and epoch chosen locally */
};

/*
 * Process the epoch state of an incoming operation. Once this function
 * returns, the epoch state shall contain a chosen epoch. Additionally, if
 * the return value is PE_OK_LOCAL, the epoch can be used for local-RDG
 * operations without uncertainty.
 */
static int
process_epoch(uint64_t *epoch, uint64_t *epoch_first, uint32_t *flags)
{
	if (*epoch == 0 || *epoch == DAOS_EPOCH_MAX)
		/*
		 * *epoch is not a chosen TX epoch. Choose the current HLC
		 * reading as the TX epoch.
		 */
		*epoch = crt_hlc_get();
	else
		/* *epoch is already a chosen TX epoch. */
		return PE_OK_REMOTE;

	/* If this is the first epoch chosen, assign it to *epoch_first. */
	if (epoch_first != NULL && *epoch_first == 0)
		*epoch_first = *epoch;

	D_DEBUG(DB_IO, "overwrite epoch "DF_X64"\n", *epoch);
	return PE_OK_LOCAL;
}

void
ds_obj_rw_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	struct dtx_leader_handle	dlh;
	struct ds_obj_exec_arg		exec_arg = { 0 };
	struct obj_io_context		ioc = { 0 };
	uint32_t			flags = 0;
	uint32_t			dtx_flags = 0;
	uint32_t			opc = opc_get(rpc->cr_opc);
	struct obj_ec_split_req		*split_req = NULL;
	struct dtx_memberships		*mbs = NULL;
	struct daos_shard_tgt		*tgts = NULL;
	struct dtx_id			*dti_cos = NULL;
	int				dti_cos_cnt;
	uint32_t			tgt_cnt;
	uint32_t			version = 0;
	struct dtx_epoch		epoch = {0};
	int				rc;
	bool				need_abort = false;

	D_ASSERT(orw != NULL);
	D_ASSERT(orwo != NULL);

	rc = obj_ioc_begin(orw->orw_oid.id_pub, orw->orw_map_ver,
			   orw->orw_pool_uuid, orw->orw_co_hdl,
			   orw->orw_co_uuid, opc_get(rpc->cr_opc),
			   orw->orw_flags, &ioc);
	if (rc != 0) {
		D_ASSERTF(rc < 0, "unexpected error# "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	D_DEBUG(DB_IO,
		"rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_X64", pmv %u/%u dti "DF_DTI".\n",
		rpc, opc, DP_UOID(orw->orw_oid), DP_KEY(&orw->orw_dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id, orw->orw_epoch,
		orw->orw_map_ver, ioc.ioc_map_ver, DP_DTI(&orw->orw_dti));

	if (obj_rpc_is_fetch(rpc) && !(orw->orw_flags & ORF_EC_RECOV) &&
	    (orw->orw_epoch != 0 && orw->orw_epoch != DAOS_EPOCH_MAX))
		ioc.ioc_fetch_snap = 1;

	rc = process_epoch(&orw->orw_epoch, &orw->orw_epoch_first,
			   &orw->orw_flags);
	if (rc == PE_OK_LOCAL)
		orw->orw_flags &= ~ORF_EPOCH_UNCERTAIN;

	if (obj_rpc_is_fetch(rpc)) {
		struct dtx_handle dth = {0};
		int		  retry = 0;

		if (orw->orw_flags & ORF_CSUM_REPORT) {
			obj_log_csum_err();
			D_GOTO(out, rc = 0);
		}

		if (DAOS_FAIL_CHECK(DAOS_OBJ_FETCH_DATA_LOST))
			D_GOTO(out, rc = -DER_DATA_LOSS);

		epoch.oe_value = orw->orw_epoch;
		epoch.oe_first = orw->orw_epoch_first;
		epoch.oe_flags = orf_to_dtx_epoch_flags(orw->orw_flags);

		if (orw->orw_flags & ORF_FOR_MIGRATION)
			dtx_flags = DTX_FOR_MIGRATION;
		if (orw->orw_flags & ORF_DTX_REFRESH)
			dtx_flags |= DTX_FORCE_REFRESH;

re_fetch:
		rc = dtx_begin(ioc.ioc_vos_coh, &orw->orw_dti, &epoch, 0,
			       orw->orw_map_ver, &orw->orw_oid,
			       NULL, 0, dtx_flags, NULL, &dth);
		if (rc != 0)
			goto out;

		rc = obj_local_rw(rpc, &ioc, NULL, NULL, NULL, &dth, false);
		rc = dtx_end(&dth, ioc.ioc_coc, rc);

		if (rc == -DER_INPROGRESS && dth.dth_local_retry) {
			if (++retry > 5)
				D_GOTO(out, rc = -DER_TX_BUSY);

			/* XXX: Currently, we commit the distributed transaction
			 *	synchronously. Normally it will be very quickly.
			 *	So let's yield then retry. If related
			 *	distributed transaction is still not committed
			 *	after several cycles, replies '-DER_TX_BUSY' to
			 *	the client.
			 */
			D_DEBUG(DB_IO, "Hit non-commit DTX when fetch "
				DF_UOID" (%d)\n", DP_UOID(orw->orw_oid), retry);
			ABT_thread_yield();

			goto re_fetch;
		}

		D_GOTO(out, rc);
	}

	tgts = orw->orw_shard_tgts.ca_arrays;
	tgt_cnt = orw->orw_shard_tgts.ca_count;

	if (!daos_is_zero_dti(&orw->orw_dti) && tgt_cnt != 0) {
		rc = obj_gen_dtx_mbs(tgts, orw->orw_flags & ORF_EC,
				     &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	version = orw->orw_map_ver;

	if (tgt_cnt == 0)
		dtx_flags |= DTX_SOLO;
	if (orw->orw_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	/* Handle resend. */
	if (orw->orw_flags & ORF_RESEND) {
		daos_epoch_t		 e;
		struct obj_pool_metrics	*opm;

		dtx_flags |= DTX_RESEND;

again1:
		opm = ioc.ioc_coc->sc_pool->spc_metrics[DAOS_OBJ_MODULE];
		d_tm_inc_counter(opm->opm_update_resent, 1);
		e = 0;
		rc = dtx_handle_resend(ioc.ioc_vos_coh, &orw->orw_dti,
				       &e, &version);
		switch (rc) {
		case -DER_ALREADY:
			D_GOTO(out, rc = 0);
		case 0:
			flags |= ORF_RESEND;
			orw->orw_epoch = e;
			/* TODO: Also recover the epoch uncertainty. */
			break;
		case -DER_NONEXIST:
			rc = 0;
			break;
		default:
			D_GOTO(out, rc);
		}
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST)) {
		ioc.ioc_lost_reply = 1;
		D_GOTO(out, rc);
	}

again2:
	if (orw->orw_iod_array.oia_oiods != NULL && split_req == NULL) {
		rc = obj_ec_rw_req_split(orw->orw_oid, &orw->orw_iod_array,
					 orw->orw_nr, orw->orw_start_shard,
					 orw->orw_tgt_max, PO_COMP_ID_ALL,
					 NULL, 0, orw->orw_shard_tgts.ca_count,
					 orw->orw_shard_tgts.ca_arrays,
					 &split_req);
		if (rc != 0) {
			D_ERROR(DF_UOID": obj_ec_rw_req_split failed, rc %d.\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out, rc);
		}
	}

	/* For leader case, we need to find out the potential conflict
	 * (or share the same non-committed object/dkey) DTX(s) in the
	 * CoS (committable) cache, piggyback them via the dispdatched
	 * RPC to non-leaders. Then the non-leader replicas can commit
	 * them before real modifications to avoid availability issues.
	 */
	D_FREE(dti_cos);
	dti_cos_cnt = dtx_list_cos(ioc.ioc_coc, &orw->orw_oid,
				   orw->orw_dkey_hash, DTX_THRESHOLD_COUNT,
				   &dti_cos);
	if (dti_cos_cnt < 0)
		D_GOTO(out, rc = dti_cos_cnt);

	epoch.oe_value = orw->orw_epoch;
	epoch.oe_first = orw->orw_epoch_first;
	epoch.oe_flags = orf_to_dtx_epoch_flags(orw->orw_flags);

	/* Since we do not know if other replicas execute the
	 * operation, so even the operation has been execute
	 * locally, we will start dtx and forward requests to
	 * all replicas.
	 *
	 * For new leader, even though the local replica
	 * has ever been modified before, but it doesn't
	 * know whether other replicas have also done the
	 * modification or not, so still need to dispatch
	 * the RPC to other replicas.
	 */

	if (flags & ORF_RESEND)
		dtx_flags |= DTX_PREPARED;
	else
		dtx_flags &= ~DTX_PREPARED;

	rc = dtx_leader_begin(ioc.ioc_vos_coh, &orw->orw_dti, &epoch, 1,
			      version, &orw->orw_oid, dti_cos, dti_cos_cnt,
			      tgts, tgt_cnt, dtx_flags, mbs, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for update "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.args = split_req;
	exec_arg.flags = flags;
	exec_arg.start = orw->orw_start_shard;

	/* Execute the operation on all targets */
	rc = dtx_leader_exec_ops(&dlh, obj_tgt_update, NULL, NULL, &exec_arg);

	/* Stop the distributed transaction */
	rc = dtx_leader_end(&dlh, ioc.ioc_coc, rc);
	switch (rc) {
	case -DER_TX_RESTART:
		/*
		 * If this is a standalone operation, we can restart the
		 * internal transaction right here. Otherwise, we have to defer
		 * the restart to the RPC client.
		 */
		if (opc == DAOS_OBJ_RPC_UPDATE) {
			struct obj_pool_metrics	*m;

			m = ioc.ioc_coc->sc_pool->spc_metrics[DAOS_OBJ_MODULE];

			/*
			 * Only standalone updates use this RPC. Retry with
			 * newer epoch.
			 */
			orw->orw_epoch = crt_hlc_get();
			orw->orw_flags &= ~ORF_RESEND;
			flags = 0;
			d_tm_inc_counter(m->opm_update_restart, 1);
			goto again2;
		}

		/* Standalone fetches do not get -DER_TX_RESTART. */
		D_ASSERT(!daos_is_zero_dti(&orw->orw_dti));

		break;
	case -DER_AGAIN:
		orw->orw_flags |= ORF_RESEND;
		need_abort = true;
		goto again1;
	default:
		break;
	}

	if (opc == DAOS_OBJ_RPC_UPDATE && !(orw->orw_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		ioc.ioc_lost_reply = 1;
out:
	if (rc != 0 && need_abort) {
		struct dtx_entry	 dte;
		struct dtx_entry	*pdte;
		int			 rc1;

		dte.dte_xid = orw->orw_dti;
		dte.dte_ver = version;
		dte.dte_refs = 1;
		dte.dte_mbs = mbs;
		pdte = &dte;
		rc1 = dtx_abort(ioc.ioc_coc, orw->orw_epoch, &pdte, 1);
		if (rc1 != 0 && rc1 != -DER_NONEXIST)
			D_WARN("Failed to abort DTX "DF_DTI": "DF_RC"\n",
			       DP_DTI(&orw->orw_dti), DP_RC(rc1));
	}

	obj_rw_reply(rpc, rc, epoch.oe_value, &ioc);
	obj_ec_split_req_fini(split_req);
	D_FREE(mbs);
	D_FREE(dti_cos);
	obj_ioc_end(&ioc, rc);
}

static void
obj_enum_complete(crt_rpc_t *rpc, int status, int map_version,
		  daos_epoch_t epoch)
{
	struct obj_key_enum_out	*oeo;
	int			 rc;

	oeo = crt_reply_get(rpc);
	D_ASSERT(oeo != NULL);

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	oeo->oeo_epoch = epoch;

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));

	d_sgl_fini(&oeo->oeo_sgl, true);
	D_FREE(oeo->oeo_kds.ca_arrays);
	D_FREE(oeo->oeo_eprs.ca_arrays);
	D_FREE(oeo->oeo_recxs.ca_arrays);
	D_FREE(oeo->oeo_csum_iov.iov_buf);
}

static int
obj_restore_enum_args(crt_rpc_t *rpc, struct dss_enum_arg *des,
		      struct dss_enum_arg *src)
{
	struct obj_key_enum_out	*oeo = crt_reply_get(rpc);
	struct obj_key_enum_in	*oei = crt_req_get(rpc);
	int			 rc;

	if (!des->fill_recxs && des->csum_iov.iov_buf != NULL)
		daos_iov_free(&des->csum_iov);

	*des = *src;

	if (des->fill_recxs)
		return 0;

	if (des->kds != NULL)
		memset(des->kds, 0, des->kds_cap * sizeof(daos_key_desc_t));

	if (oeo->oeo_sgl.sg_iovs == NULL)
		return 0;

	d_sgl_fini(&oeo->oeo_sgl, true);
	rc = daos_sgls_alloc(&oeo->oeo_sgl, &oei->oei_sgl, 1);
	if (rc != 0)
		return rc;

	des->sgl = &oeo->oeo_sgl;
	return 0;
}

static int
obj_local_enum(struct obj_io_context *ioc, crt_rpc_t *rpc,
	       struct vos_iter_anchors *anchors, struct dss_enum_arg *enum_arg,
	       daos_epoch_t *e_out)
{
	vos_iter_param_t	param = { 0 };
	struct vos_iter_anchors	saved_anchors;
	struct dss_enum_arg	saved_arg;
	struct obj_key_enum_in	*oei = crt_req_get(rpc);
	uint32_t		flags = 0;
	int			retry = 0;
	int			opc = opc_get(rpc->cr_opc);
	int			type;
	int			rc;
	int			rc_tmp;
	bool			recursive = false;
	struct dtx_handle	dth = {0};
	struct dtx_epoch	epoch = {0};

	if (oei->oei_flags & ORF_ENUM_WITHOUT_EPR) {
		rc = process_epoch(&oei->oei_epr.epr_hi, &oei->oei_epr.epr_lo,
				   &oei->oei_flags);
		if (rc == PE_OK_LOCAL)
			oei->oei_flags &= ~ORF_EPOCH_UNCERTAIN;
	}

	enum_arg->csummer = ioc->ioc_coc->sc_csummer;
	/* prepare enumeration parameters */
	param.ip_hdl = ioc->ioc_vos_coh;
	param.ip_oid = oei->oei_oid;
	if (oei->oei_dkey.iov_len > 0)
		param.ip_dkey = oei->oei_dkey;
	if (oei->oei_akey.iov_len > 0)
		param.ip_akey = oei->oei_akey;

	/*
	 * Note that oei_epr may be reused for "epoch_first" and "epoch. See
	 * dc_obj_shard_list.
	 */
	if (oei->oei_flags & ORF_ENUM_WITHOUT_EPR)
		param.ip_epr.epr_lo = 0;
	else
		param.ip_epr.epr_lo = oei->oei_epr.epr_lo;
	param.ip_epr.epr_hi = oei->oei_epr.epr_hi;
	param.ip_epc_expr = VOS_IT_EPC_LE;

	if (opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		if (oei->oei_dkey.iov_len == 0 ||
		    oei->oei_akey.iov_len == 0)
			D_GOTO(failed, rc = -DER_PROTO);

		if (oei->oei_rec_type == DAOS_IOD_ARRAY)
			type = VOS_ITER_RECX;
		else
			type = VOS_ITER_SINGLE;

		param.ip_epc_expr = VOS_IT_EPC_RE;
		/** Only show visible records and skip punches */
		param.ip_flags = VOS_IT_RECX_VISIBLE | VOS_IT_RECX_SKIP_HOLES;
		enum_arg->fill_recxs = true;
	} else if (opc == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		type = VOS_ITER_DKEY;
	} else if (opc == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		type = VOS_ITER_AKEY;
	} else {
		/* object iteration for rebuild or consistency verification. */
		D_ASSERT(opc == DAOS_OBJ_RPC_ENUMERATE);
		type = VOS_ITER_DKEY;
		if (daos_anchor_get_flags(&anchors->ia_dkey) &
		      DIOF_WITH_SPEC_EPOCH) {
			/* For obj verification case. */
			param.ip_flags |= VOS_IT_RECX_VISIBLE;
			param.ip_epc_expr = VOS_IT_EPC_RR;
		} else {
			param.ip_epc_expr = VOS_IT_EPC_RE;
		}
		recursive = true;
		enum_arg->chk_key2big = 1;
		enum_arg->need_punch = 1;
		enum_arg->copy_data_cb = vos_iter_copy;
		fill_oid(oei->oei_oid, enum_arg);
	}

	/*
	 * FIXME: enumeration RPC uses one anchor for both SV and EV,
	 * that won't be able to support recursive iteration in our
	 * current data model (one akey can have both SV tree and EV
	 * tree).
	 *
	 * Need to use separate anchors for SV and EV, or return a
	 * 'type' to indicate the anchor is on SV tree or EV tree.
	 */
	if (type == VOS_ITER_SINGLE)
		anchors->ia_sv = anchors->ia_ev;
	else if (oei->oei_oid.id_shard % 3 == 1 &&
		 DAOS_FAIL_CHECK(DAOS_VC_LOST_REPLICA))
		D_GOTO(failed, rc = -DER_NONEXIST);

	if (oei->oei_flags & ORF_ENUM_WITHOUT_EPR) {
		epoch.oe_value = oei->oei_epr.epr_hi;
		epoch.oe_first = oei->oei_epr.epr_lo;
		epoch.oe_flags = orf_to_dtx_epoch_flags(oei->oei_flags);
	} else if (!daos_is_zero_dti(&oei->oei_dti)) {
		D_ERROR(DF_UOID": mutually exclusive transaction ID and epoch "
			"range specified\n", DP_UOID(oei->oei_oid));
		rc = -DER_PROTO;
		goto failed;
	}

	saved_anchors = *anchors;
	saved_arg = *enum_arg;

	if (oei->oei_flags & ORF_FOR_MIGRATION)
		flags = DTX_FOR_MIGRATION;
	if (oei->oei_flags & ORF_DTX_REFRESH)
		flags |= DTX_FORCE_REFRESH;

again:
	rc = dtx_begin(ioc->ioc_vos_coh, &oei->oei_dti, &epoch, 0,
		       oei->oei_map_ver, &oei->oei_oid, NULL, 0, flags,
		       NULL, &dth);
	if (rc != 0)
		goto failed;

re_pack:
	rc = dss_enum_pack(&param, type, recursive, anchors, enum_arg,
			   vos_iterate, &dth);
	if (obj_dtx_need_refresh(&dth, rc)) {
		rc = dtx_refresh(&dth, ioc->ioc_coc);
		if (rc == -DER_AGAIN) {
			*anchors = saved_anchors;
			obj_restore_enum_args(rpc, enum_arg, &saved_arg);

			goto re_pack;
		}
	}

	if ((rc == -DER_KEY2BIG) && opc == DAOS_OBJ_RPC_ENUMERATE &&
	    enum_arg->kds_len < 4) {
		/* let's query the total size for one update (oid/dkey/akey/rec)
		 * to make sure the migration/enumeration can go ahead.
		 */
		enum_arg->size_query = 1;
		enum_arg->kds_len = 0;
		enum_arg->kds[0].kd_key_len = 0;
		enum_arg->kds_cap = 4;
		goto re_pack;
	} else if (enum_arg->size_query) {
		D_DEBUG(DB_IO, DF_UOID "query size by kds %d total %zd\n",
			DP_UOID(oei->oei_oid), enum_arg->kds_len, enum_arg->kds[0].kd_key_len);
		rc = -DER_KEY2BIG;
	}

	/* dss_enum_pack may return 1. */
	rc_tmp = dtx_end(&dth, ioc->ioc_coc, rc > 0 ? 0 : rc);
	if (rc_tmp != 0)
		rc = rc_tmp;

	if (rc == -DER_INPROGRESS && dth.dth_local_retry) {
		if (++retry > 5)
			D_GOTO(out, rc = -DER_TX_BUSY);

		/* XXX: Currently, we commit the distributed transaction
		 *	synchronously. Normally it will be very quickly.
		 *	So let's yield then retry. If related distributed
		 *	transaction is still not committed after several
		 *	cycles, replies '-DER_TX_BUSY' to the client.
		 */
		D_DEBUG(DB_IO, "Hit non-commit DTX when enum "
			DF_UOID" (%d)\n", DP_UOID(oei->oei_oid), retry);
		ABT_thread_yield();

		*anchors = saved_anchors;
		obj_restore_enum_args(rpc, enum_arg, &saved_arg);

		goto again;
	}

out:
	if (type == VOS_ITER_SINGLE)
		anchors->ia_ev = anchors->ia_sv;

	D_DEBUG(DB_IO, ""DF_UOID" iterate "DF_X64"-"DF_X64" type %d tag %d"
		" rc %d\n", DP_UOID(oei->oei_oid), param.ip_epr.epr_lo,
		param.ip_epr.epr_hi, type, dss_get_module_info()->dmi_tgt_id,
		rc);
failed:
	*e_out = epoch.oe_value;
	return rc;
}

static int
obj_enum_reply_bulk(crt_rpc_t *rpc)
{
	d_sg_list_t	*sgls[2] = { 0 };
	d_sg_list_t	tmp_sgl;
	crt_bulk_t	bulks[2] = { 0 };
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int		idx = 0;
	d_iov_t		tmp_iov;
	int		rc;

	oei = crt_req_get(rpc);
	oeo = crt_reply_get(rpc);
	if (oei->oei_kds_bulk && oeo->oeo_kds.ca_count > 0) {
		tmp_iov.iov_buf = oeo->oeo_kds.ca_arrays;
		tmp_iov.iov_buf_len = oeo->oeo_kds.ca_count *
				      sizeof(daos_key_desc_t);
		tmp_iov.iov_len = oeo->oeo_kds.ca_count *
				      sizeof(daos_key_desc_t);
		tmp_sgl.sg_nr = 1;
		tmp_sgl.sg_nr_out = 1;
		tmp_sgl.sg_iovs = &tmp_iov;
		sgls[idx] = &tmp_sgl;
		bulks[idx] = oei->oei_kds_bulk;
		idx++;
		D_DEBUG(DB_IO, "reply kds bulk %zd\n", tmp_iov.iov_len);
	}

	if (oei->oei_bulk) {
		D_DEBUG(DB_IO, "reply bulk %zd nr %d nr_out %d\n",
			oeo->oeo_sgl.sg_iovs[0].iov_len,
			oeo->oeo_sgl.sg_nr, oeo->oeo_sgl.sg_nr_out);
		sgls[idx] = &oeo->oeo_sgl;
		bulks[idx] = oei->oei_bulk;
		idx++;
	}

	/* No need reply bulk */
	if (idx == 0)
		return 0;

	rc = obj_bulk_transfer(rpc, CRT_BULK_PUT, false, bulks, NULL,
			       DAOS_HDL_INVAL, sgls, idx, NULL);
	if (oei->oei_kds_bulk) {
		D_FREE(oeo->oeo_kds.ca_arrays);
		oeo->oeo_kds.ca_arrays = NULL;
		oeo->oeo_kds.ca_count = 0;
	}

	/* Free oeo_sgl here to avoid rpc reply the data inline */
	if (oei->oei_bulk)
		d_sgl_fini(&oeo->oeo_sgl, true);

	return rc;
}

void
ds_obj_enum_handler(crt_rpc_t *rpc)
{
	struct dss_enum_arg	enum_arg = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	struct obj_io_context	ioc;
	daos_epoch_t		epoch = 0;
	int			opc = opc_get(rpc->cr_opc);
	int			rc = 0;

	oei = crt_req_get(rpc);
	D_ASSERT(oei != NULL);
	oeo = crt_reply_get(rpc);
	D_ASSERT(oeo != NULL);
	/* prepare buffer for enumerate */

	rc = obj_ioc_begin(oei->oei_oid.id_pub, oei->oei_map_ver,
			   oei->oei_pool_uuid, oei->oei_co_hdl,
			   oei->oei_co_uuid, opc, oei->oei_flags, &ioc);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_IO, "rpc %p opc %d oid "DF_UOID" tag/xs %d/%d pmv %u/%u\n",
		rpc, opc, DP_UOID(oei->oei_oid),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id,
		oei->oei_map_ver, ioc.ioc_map_ver);

	anchors.ia_dkey = oei->oei_dkey_anchor;
	anchors.ia_akey = oei->oei_akey_anchor;
	anchors.ia_ev = oei->oei_anchor;

	/* TODO: Transfer the inline_thres from enumerate RPC */
	enum_arg.inline_thres = 32;

	if (opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		oeo->oeo_eprs.ca_count = 0;
		D_ALLOC(oeo->oeo_eprs.ca_arrays,
			oei->oei_nr * sizeof(daos_epoch_range_t));
		if (oeo->oeo_eprs.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		enum_arg.eprs = oeo->oeo_eprs.ca_arrays;
		enum_arg.eprs_cap = oei->oei_nr;
		enum_arg.eprs_len = 0;

		oeo->oeo_recxs.ca_count = 0;
		D_ALLOC(oeo->oeo_recxs.ca_arrays,
			oei->oei_nr * sizeof(daos_recx_t));
		if (oeo->oeo_recxs.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		enum_arg.recxs = oeo->oeo_recxs.ca_arrays;
		enum_arg.recxs_cap = oei->oei_nr;
		enum_arg.recxs_len = 0;
	} else {
		rc = daos_sgls_alloc(&oeo->oeo_sgl, &oei->oei_sgl, 1);
		if (rc != 0)
			D_GOTO(out, rc);
		enum_arg.sgl = &oeo->oeo_sgl;
		enum_arg.sgl_idx = 0;

		/* Prepare key descriptor buffer */
		oeo->oeo_kds.ca_count = 0;
		D_ALLOC(oeo->oeo_kds.ca_arrays,
			oei->oei_nr * sizeof(daos_key_desc_t));
		if (oeo->oeo_kds.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		enum_arg.kds = oeo->oeo_kds.ca_arrays;
		enum_arg.kds_cap = oei->oei_nr;
		enum_arg.kds_len = 0;
	}

	/* keep trying until the key_buffer is fully filled or reaching the
	 * end of the stream.
	 */
	rc = obj_local_enum(&ioc, rpc, &anchors, &enum_arg, &epoch);
	if (rc == 1) /* If the buffer is full, exit and reset failure. */
		rc = 0;

	if (rc)
		D_GOTO(out, rc);

	oeo->oeo_dkey_anchor = anchors.ia_dkey;
	oeo->oeo_akey_anchor = anchors.ia_akey;
	oeo->oeo_anchor = anchors.ia_ev;

	if (enum_arg.eprs)
		oeo->oeo_eprs.ca_count = enum_arg.eprs_len;

	if (opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		oeo->oeo_recxs.ca_count = enum_arg.recxs_len;
		oeo->oeo_num = enum_arg.rnum;
		oeo->oeo_size = enum_arg.rsize;
	} else {
		D_ASSERT(enum_arg.eprs_len == 0 ||
			 enum_arg.eprs_len == enum_arg.kds_len);
		oeo->oeo_kds.ca_count = enum_arg.kds_len;
		oeo->oeo_num = enum_arg.kds_len;
		oeo->oeo_size = oeo->oeo_sgl.sg_iovs[0].iov_len;
		oeo->oeo_csum_iov = enum_arg.csum_iov;
	}

	rc = obj_enum_reply_bulk(rpc);
out:
	/* for KEY2BIG case, just reuse the oeo_size to reply the key len */
	if (rc == -DER_KEY2BIG) {
		D_ASSERT(enum_arg.kds != NULL);
		oeo->oeo_size = enum_arg.kds[0].kd_key_len;
	}
	obj_enum_complete(rpc, rc, ioc.ioc_map_ver, epoch);
	obj_ioc_end(&ioc, rc);
}

static void
obj_punch_complete(crt_rpc_t *rpc, int status, uint32_t map_version)
{
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
}

static int
obj_local_punch(struct obj_punch_in *opi, crt_opcode_t opc,
		struct obj_io_context *ioc, struct dtx_handle *dth, bool pin)
{
	struct ds_cont_child *cont = ioc->ioc_coc;
	int	rc = 0;

	if (daos_is_zero_dti(&opi->opi_dti)) {
		D_DEBUG(DB_TRACE, "disable dtx\n");
		dth = NULL;
		pin = false;
	}

again:
	if (pin) {
		rc = vos_dtx_pin(dth, false);
		if (rc != 0)
			return rc;
	}

	rc = dtx_sub_init(dth, &opi->opi_oid, opi->opi_dkey_hash);
	if (rc != 0)
		goto out;

	switch (opc) {
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH:
		rc = vos_obj_punch(cont->sc_hdl, opi->opi_oid,
				   opi->opi_epoch, opi->opi_map_ver,
				   0, NULL, 0, NULL, dth);
		break;
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS: {
		daos_key_t *dkey;

		D_ASSERTF(opi->opi_dkeys.ca_count == 1,
			  "NOT punch multiple (%llu) dkeys via one RPC\n",
			  (unsigned long long)opi->opi_dkeys.ca_count);

		dkey = &((daos_key_t *)opi->opi_dkeys.ca_arrays)[0];
		rc = vos_obj_punch(cont->sc_hdl, opi->opi_oid,
				   opi->opi_epoch, opi->opi_map_ver,
				   opi->opi_api_flags,
				   dkey, opi->opi_akeys.ca_count,
				   opi->opi_akeys.ca_arrays, dth);
		break;
	}
	default:
		D_ERROR("opc %#x not supported\n", opc);
		D_GOTO(out, rc = -DER_NOSYS);
	}

	if (dth != NULL && obj_dtx_need_refresh(dth, rc)) {
		rc = dtx_refresh(dth, ioc->ioc_coc);
		if (rc == -DER_AGAIN)
			goto again;
	}

out:
	return rc;
}

/* Handle the punch requests on non-leader */
void
ds_obj_tgt_punch_handler(crt_rpc_t *rpc)
{
	struct dtx_handle		 dth = { 0 };
	struct obj_io_context		 ioc;
	struct obj_punch_in		*opi;
	struct dtx_memberships		*mbs = NULL;
	struct daos_shard_tgt		*tgts = NULL;
	uint32_t			 dtx_flags = 0;
	uint32_t			 tgt_cnt;
	struct dtx_epoch		 epoch;
	int				 rc;

	opi = crt_req_get(rpc);
	D_ASSERT(opi != NULL);
	rc = obj_ioc_begin(opi->opi_oid.id_pub, opi->opi_map_ver,
			   opi->opi_pool_uuid, opi->opi_co_hdl,
			   opi->opi_co_uuid, opc_get(rpc->cr_opc),
			   opi->opi_flags, &ioc);
	if (rc)
		goto out;

	/* Handle resend. */
	if (opi->opi_flags & ORF_RESEND) {
		rc = dtx_handle_resend(ioc.ioc_vos_coh, &opi->opi_dti,
				       &opi->opi_epoch, NULL);

		/* Do nothing if 'prepared' or 'committed'. */
		if (rc == -DER_ALREADY || rc == 0)
			D_GOTO(out, rc = 0);

		/* Abort it firstly if exist but with different epoch,
		 * then re-execute with new epoch.
		 */
		if (rc == -DER_MISMATCH)
			/* Abort it by force with MAX epoch to guarantee
			 * that it can be aborted.
			 */
			rc = vos_dtx_abort(ioc.ioc_vos_coh, DAOS_EPOCH_MAX,
					   &opi->opi_dti, 1);

		if (rc < 0 && rc != -DER_NONEXIST)
			D_GOTO(out, rc);

		dtx_flags |= DTX_RESEND;
	}

	tgts = opi->opi_shard_tgts.ca_arrays;
	tgt_cnt = opi->opi_shard_tgts.ca_count;

	if (!daos_is_zero_dti(&opi->opi_dti) && tgt_cnt != 0) {
		rc = obj_gen_dtx_mbs(tgts, opi->opi_flags & ORF_EC,
				     &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	epoch.oe_value = opi->opi_epoch;
	epoch.oe_first = epoch.oe_value; /* unused for TGT_PUNCH */
	epoch.oe_flags = orf_to_dtx_epoch_flags(opi->opi_flags);

	if (opi->opi_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	/* Start the local transaction */
	rc = dtx_begin(ioc.ioc_vos_coh, &opi->opi_dti, &epoch, 1,
		       opi->opi_map_ver, &opi->opi_oid,
		       opi->opi_dti_cos.ca_arrays,
		       opi->opi_dti_cos.ca_count, dtx_flags, mbs, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for punch "DF_RC".\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		D_GOTO(out, rc = -DER_IO);

	/* non-leader local RPC handler, do not need pin the DTX entry.  */
	rc = obj_local_punch(opi, opc_get(rpc->cr_opc), &ioc, &dth, false);
	if (rc != 0)
		D_CDEBUG(rc == -DER_INPROGRESS || rc == -DER_TX_RESTART,
			 DB_IO, DLOG_ERR,
			 DF_UOID": error="DF_RC".\n", DP_UOID(opi->opi_oid),
			 DP_RC(rc));

out:
	/* Stop the local transaction */
	rc = dtx_end(&dth, ioc.ioc_coc, rc);
	obj_punch_complete(rpc, rc, ioc.ioc_map_ver);
	D_FREE(mbs);
	obj_ioc_end(&ioc, rc);
}

static int
obj_punch_agg_cb(struct dtx_leader_handle *dlh, void *agg_arg)
{
	uint64_t	*flag = agg_arg;
	int		succeeds = 0;
	int		allow_failure = 0;
	int		allow_failure_cnt = 0;
	int		result = 0;
	int		i;

	D_ASSERT(flag != NULL);
	if (*flag & DAOS_COND_PUNCH)
		allow_failure = -DER_NONEXIST;

	for (i = 0; i < dlh->dlh_sub_cnt; i++) {
		struct dtx_sub_status	*sub = &dlh->dlh_subs[i];

		if (sub->dss_result == 0) {
			succeeds++;
		} else if (sub->dss_result == allow_failure) {
			allow_failure_cnt++;
		} else {
			/* Ignore INPROGRESS if there other failures */
			if (result == -DER_INPROGRESS || result == 0)
				result = sub->dss_result;
		}
	}

	D_DEBUG(DB_IO, DF_DTI" %d/%d shards flags "DF_X64" result %d\n",
		DP_DTI(&dlh->dlh_handle.dth_xid), allow_failure_cnt,
		succeeds, *flag, result);

	if (*flag & DAOS_COND_PUNCH) {
		/* For punch, let's ignore DER_NONEXIST if there are shards
		 * succeed, since the object may not exist on some shards
		 * due to EC partial update.
		 */
		if (result == 0 && succeeds == 0) {
			D_ASSERT(dlh->dlh_sub_cnt == allow_failure_cnt);
			return -DER_NONEXIST;
		}

		return result;
	}

	return result;
}

static int
obj_tgt_punch(struct dtx_leader_handle *dlh, void *arg, int idx,
	      dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg	*exec_arg = arg;

	/* handle local operation */
	if (idx == -1) {
		crt_rpc_t		*rpc = exec_arg->rpc;
		struct obj_punch_in	*opi = crt_req_get(rpc);
		int			rc = 0;

		if (DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
			D_GOTO(comp, rc = -DER_IO);

		if (dlh != NULL && dlh->dlh_handle.dth_prepared)
			goto comp;

		rc = obj_local_punch(opi, opc_get(rpc->cr_opc), exec_arg->ioc,
				     &dlh->dlh_handle,
				     !dlh->dlh_handle.dth_solo);

comp:
		if (comp_cb != NULL)
			comp_cb(dlh, idx, rc);

		return rc;
	}

	/* Handle the object remotely */
	return ds_obj_remote_punch(dlh, arg, idx, comp_cb);
}

/* Handle the punch requests on the leader */
void
ds_obj_punch_handler(crt_rpc_t *rpc)
{
	struct dtx_leader_handle	dlh;
	struct obj_punch_in		*opi;
	struct ds_obj_exec_arg		exec_arg = { 0 };
	struct obj_io_context		ioc = { 0 };
	struct dtx_memberships		*mbs = NULL;
	struct daos_shard_tgt		*tgts = NULL;
	struct dtx_id			*dti_cos = NULL;
	int				dti_cos_cnt;
	uint32_t			tgt_cnt;
	uint32_t			flags = 0;
	uint32_t			dtx_flags = 0;
	uint32_t			version = 0;
	struct dtx_epoch		epoch;
	int				rc;
	bool				need_abort = false;

	opi = crt_req_get(rpc);
	D_ASSERT(opi != NULL);
	rc = obj_ioc_begin(opi->opi_oid.id_pub, opi->opi_map_ver,
			   opi->opi_pool_uuid, opi->opi_co_hdl,
			   opi->opi_co_uuid, opc_get(rpc->cr_opc),
			   opi->opi_flags, &ioc);
	if (rc)
		goto out;

	if (opi->opi_dkeys.ca_count == 0)
		D_DEBUG(DB_TRACE,
			"punch obj %p oid "DF_UOID" tag/xs %d/%d epc "
			DF_X64", pmv %u/%u dti "DF_DTI".\n",
			rpc, DP_UOID(opi->opi_oid),
			dss_get_module_info()->dmi_tgt_id,
			dss_get_module_info()->dmi_xs_id, opi->opi_epoch,
			opi->opi_map_ver, ioc.ioc_map_ver,
			DP_DTI(&opi->opi_dti));
	else
		D_DEBUG(DB_TRACE,
			"punch key %p oid "DF_UOID" dkey "
			DF_KEY" tag/xs %d/%d epc "
			DF_X64", pmv %u/%u dti "DF_DTI".\n",
			rpc, DP_UOID(opi->opi_oid),
			DP_KEY(&opi->opi_dkeys.ca_arrays[0]),
			dss_get_module_info()->dmi_tgt_id,
			dss_get_module_info()->dmi_xs_id, opi->opi_epoch,
			opi->opi_map_ver, ioc.ioc_map_ver,
			DP_DTI(&opi->opi_dti));

	rc = process_epoch(&opi->opi_epoch, NULL /* epoch_first */,
			   &opi->opi_flags);
	if (rc == PE_OK_LOCAL)
		opi->opi_flags &= ~ORF_EPOCH_UNCERTAIN;

	version = opi->opi_map_ver;
	tgts = opi->opi_shard_tgts.ca_arrays;
	tgt_cnt = opi->opi_shard_tgts.ca_count;

	if (!daos_is_zero_dti(&opi->opi_dti) && tgt_cnt != 0) {
		rc = obj_gen_dtx_mbs(tgts, opi->opi_flags & ORF_EC,
				     &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	if (tgt_cnt == 0)
		dtx_flags |= DTX_SOLO;
	if (opi->opi_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	/* Handle resend. */
	if (opi->opi_flags & ORF_RESEND) {
		daos_epoch_t	e;

		dtx_flags |= DTX_RESEND;

again1:
		e = 0;
		rc = dtx_handle_resend(ioc.ioc_vos_coh, &opi->opi_dti,
				       &e, &version);
		switch (rc) {
		case -DER_ALREADY:
			D_GOTO(out, rc = 0);
		case 0:
			opi->opi_epoch = e;
			flags |= ORF_RESEND;
			/* TODO: Also recovery the epoch uncertainty. */
			break;
		case -DER_NONEXIST:
			rc = 0;
			break;
		default:
			D_GOTO(out, rc);
		}
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST) ||
		   DAOS_FAIL_CHECK(DAOS_DTX_LONG_TIME_RESEND)) {
		goto cleanup;
	}

again2:
	/* For leader case, we need to find out the potential conflict
	 * (or share the same non-committed object/dkey) DTX(s) in the
	 * CoS (committable) cache, piggyback them via the dispdatched
	 * RPC to non-leaders. Then the non-leader replicas can commit
	 * them before real modifications to avoid availability issues.
	 */
	D_FREE(dti_cos);
	dti_cos_cnt = dtx_list_cos(ioc.ioc_coc, &opi->opi_oid,
				   opi->opi_dkey_hash, DTX_THRESHOLD_COUNT,
				   &dti_cos);
	if (dti_cos_cnt < 0)
		D_GOTO(out, rc = dti_cos_cnt);

	epoch.oe_value = opi->opi_epoch;
	epoch.oe_first = epoch.oe_value; /* unused for PUNCH */
	epoch.oe_flags = orf_to_dtx_epoch_flags(opi->opi_flags);

	/* Since we do not know if other replicas execute the
	 * operation, so even the operation has been execute
	 * locally, we will start dtx and forward requests to
	 * all replicas.
	 *
	 * For new leader, even though the local replica
	 * has ever been modified before, but it doesn't
	 * know whether other replicas have also done the
	 * modification or not, so still need to dispatch
	 * the RPC to other replicas.
	 */

	if (flags & ORF_RESEND)
		dtx_flags |= DTX_PREPARED;
	else
		dtx_flags &= ~DTX_PREPARED;

	rc = dtx_leader_begin(ioc.ioc_vos_coh, &opi->opi_dti, &epoch, 1,
			      version, &opi->opi_oid, dti_cos, dti_cos_cnt,
			      tgts, tgt_cnt, dtx_flags, mbs, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for punch "DF_RC".\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.flags = flags;

	/* Execute the operation on all shards */
	rc = dtx_leader_exec_ops(&dlh, obj_tgt_punch, obj_punch_agg_cb,
				 &opi->opi_api_flags, &exec_arg);

	/* Stop the distribute transaction */
	rc = dtx_leader_end(&dlh, ioc.ioc_coc, rc);
	switch (rc) {
	case -DER_TX_RESTART:
		/*
		 * Only standalone punches use this RPC. Retry with newer
		 * epoch.
		 */
		opi->opi_epoch = crt_hlc_get();
		opi->opi_flags &= ~ORF_RESEND;
		flags = 0;
		goto again2;
	case -DER_AGAIN:
		opi->opi_flags |= ORF_RESEND;
		need_abort = true;
		goto again1;
	default:
		break;
	}

	if (!(opi->opi_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		goto cleanup;

out:
	if (rc != 0 && need_abort) {
		struct dtx_entry	 dte;
		struct dtx_entry	*pdte;
		int			 rc1;

		dte.dte_xid = opi->opi_dti;
		dte.dte_ver = version;
		dte.dte_refs = 1;
		dte.dte_mbs = mbs;
		pdte = &dte;
		rc1 = dtx_abort(ioc.ioc_coc, opi->opi_epoch, &pdte, 1);
		if (rc1 != 0 && rc1 != -DER_NONEXIST)
			D_WARN("Failed to abort DTX "DF_DTI": "DF_RC"\n",
			       DP_DTI(&opi->opi_dti), DP_RC(rc1));
	}

	obj_punch_complete(rpc, rc, ioc.ioc_map_ver);

cleanup:
	D_FREE(mbs);
	D_FREE(dti_cos);
	obj_ioc_end(&ioc, rc);
}

void
ds_obj_query_key_handler(crt_rpc_t *rpc)
{
	struct obj_query_key_in		*okqi;
	struct obj_query_key_out	*okqo;
	daos_key_t			*dkey;
	daos_key_t			*akey;
	struct obj_io_context		 ioc;
	struct dtx_handle		 dth = {0};
	struct dtx_epoch		 epoch = {0};
	uint32_t			 query_flags;
	daos_recx_t			 ec_recx[2] = {0};
	daos_recx_t			*query_recx;
	int				 retry = 0;
	int				 rc;

	okqi = crt_req_get(rpc);
	D_ASSERT(okqi != NULL);
	okqo = crt_reply_get(rpc);
	D_ASSERT(okqo != NULL);

	D_DEBUG(DB_IO, "flags = "DF_U64"\n", okqi->okqi_api_flags);

	rc = obj_ioc_begin(okqi->okqi_oid.id_pub, okqi->okqi_map_ver,
			   okqi->okqi_pool_uuid, okqi->okqi_co_hdl,
			   okqi->okqi_co_uuid, opc_get(rpc->cr_opc),
			   okqi->okqi_flags, &ioc);
	if (rc)
		D_GOTO(out, rc);

	rc = process_epoch(&okqi->okqi_epoch, &okqi->okqi_epoch_first,
			   &okqi->okqi_flags);
	if (rc == PE_OK_LOCAL)
		okqi->okqi_flags &= ~ORF_EPOCH_UNCERTAIN;

again:
	dkey = &okqi->okqi_dkey;
	akey = &okqi->okqi_akey;
	d_iov_set(&okqo->okqo_akey, NULL, 0);
	d_iov_set(&okqo->okqo_dkey, NULL, 0);
	if (okqi->okqi_api_flags & DAOS_GET_DKEY)
		dkey = &okqo->okqo_dkey;
	if (okqi->okqi_api_flags & DAOS_GET_AKEY)
		akey = &okqo->okqo_akey;

	epoch.oe_value = okqi->okqi_epoch;
	epoch.oe_first = okqi->okqi_epoch_first;
	epoch.oe_flags = orf_to_dtx_epoch_flags(okqi->okqi_flags);

	rc = dtx_begin(ioc.ioc_vos_coh, &okqi->okqi_dti, &epoch, 0,
		       okqi->okqi_map_ver, &okqi->okqi_oid, NULL, 0, 0, NULL,
		       &dth);
	if (rc != 0)
		goto out;

	query_flags = okqi->okqi_api_flags;
	if ((okqi->okqi_flags & ORF_EC) &&
	    (okqi->okqi_api_flags & DAOS_GET_RECX)) {
		query_flags |= VOS_GET_RECX_EC;
		query_recx = ec_recx;
	} else {
		query_recx = &okqo->okqo_recx;
	}

re_query:
	rc = vos_obj_query_key(ioc.ioc_vos_coh, okqi->okqi_oid, query_flags,
			       okqi->okqi_epoch, dkey, akey, query_recx, &dth);
	if (rc == 0 && (query_flags & VOS_GET_RECX_EC)) {
		okqo->okqo_recx = ec_recx[0];
		okqo->okqo_recx_parity = ec_recx[1];
	} else if (obj_dtx_need_refresh(&dth, rc)) {
		rc = dtx_refresh(&dth, ioc.ioc_coc);
		if (rc == -DER_AGAIN)
			goto re_query;
	}

	rc = dtx_end(&dth, ioc.ioc_coc, rc);

out:
	if (rc == -DER_INPROGRESS && dth.dth_local_retry) {
		if (++retry > 5)
			D_GOTO(failed, rc = -DER_TX_BUSY);

		/* XXX: Currently, we commit the distributed transaction
		 *	synchronously. Normally it will be very quickly.
		 *	So let's yield then retry. If related distributed
		 *	transaction is still not committed after several
		 *	cycles, then replies '-DER_TX_BUSY' to the client.
		 */
		D_DEBUG(DB_IO, "Hit non-commit DTX when query "
			DF_UOID" (%d)\n", DP_UOID(okqi->okqi_oid), retry);
		ABT_thread_yield();

		goto again;
	}

failed:
	obj_reply_set_status(rpc, rc);
	obj_reply_map_version_set(rpc, ioc.ioc_map_ver);
	okqo->okqo_epoch = epoch.oe_value;
	obj_ioc_end(&ioc, rc);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
}

void
ds_obj_sync_handler(crt_rpc_t *rpc)
{
	struct obj_sync_in	*osi;
	struct obj_sync_out	*oso;
	struct obj_io_context	 ioc;
	daos_epoch_t		 epoch = crt_hlc_get();
	int			 rc;

	osi = crt_req_get(rpc);
	D_ASSERT(osi != NULL);

	oso = crt_reply_get(rpc);
	D_ASSERT(oso != NULL);

	if (osi->osi_epoch == 0)
		oso->oso_epoch = epoch;
	else
		oso->oso_epoch = min(epoch, osi->osi_epoch);

	D_DEBUG(DB_IO, "obj_sync start: "DF_UOID", epc "DF_X64"\n",
		DP_UOID(osi->osi_oid), oso->oso_epoch);

	rc = obj_ioc_begin(osi->osi_oid.id_pub, osi->osi_map_ver,
			   osi->osi_pool_uuid, osi->osi_co_hdl,
			   osi->osi_co_uuid, opc_get(rpc->cr_opc), 0, &ioc);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dtx_obj_sync(ioc.ioc_coc, &osi->osi_oid, oso->oso_epoch);

out:
	obj_reply_map_version_set(rpc, ioc.ioc_map_ver);
	obj_reply_set_status(rpc, rc);
	obj_ioc_end(&ioc, rc);

	D_DEBUG(DB_IO, "obj_sync stop: "DF_UOID", epc "DF_X64", rd = %d\n",
		DP_UOID(osi->osi_oid), oso->oso_epoch, rc);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
}

static int
obj_verify_bio_csum(daos_obj_id_t oid, daos_iod_t *iods,
		    struct dcs_iod_csums *iod_csums, struct bio_desc *biod,
		    struct daos_csummer *csummer, uint32_t iods_nr)
{
	unsigned int	i;
	int		rc = 0;

	if (!daos_csummer_initialized(csummer) ||
	    csummer->dcs_skip_data_verify ||
	    !csummer->dcs_srv_verify)
		return 0;

	for (i = 0; i < iods_nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct bio_sglist	*bsgl = bio_iod_sgl(biod, i);
		d_sg_list_t		 sgl;

		if (!csum_iod_is_supported(iod))
			continue;

		if (!ci_is_valid(iod_csums[i].ic_data)) {
			D_ERROR("Checksums is enabled but the csum info is "
				"invalid for iod_csums %d/%d. ic_nr: %d, "
				"iod: "DF_C_IOD"\n",
				i, iods_nr, iod_csums[i].ic_nr, DP_C_IOD(iod));
			return -DER_CSUM;
		}

		rc = bio_sgl_convert(bsgl, &sgl);

		if (rc == 0)
			rc = daos_csummer_verify_iod(csummer, iod, &sgl,
						     &iod_csums[i], NULL, 0,
						     NULL);

		d_sgl_fini(&sgl, false);

		if (rc != 0) {
			if (iod->iod_type == DAOS_IOD_SINGLE) {
				D_ERROR("Data Verification failed (object: "
					DF_OID"): %d\n",
					DP_OID(oid), rc);
			} else if (iod->iod_type == DAOS_IOD_ARRAY) {
				D_ERROR("Data Verification failed (object: "
					DF_OID ", extent: "DF_RECX"): %d\n",
					DP_OID(oid), DP_RECX(iod->iod_recxs[i]),
					rc);
			}
			break;
		}
	}

	return rc;
}

static inline void
ds_obj_cpd_set_sub_result(struct obj_cpd_out *oco, int idx,
			  int result, daos_epoch_t epoch)
{
	uint64_t	*p_epoch;
	int		*p_ret;

	p_epoch = (uint64_t *)oco->oco_sub_epochs.ca_arrays + idx;
	*p_epoch = epoch;

	p_ret = (int *)oco->oco_sub_rets.ca_arrays + idx;
	*p_ret = result;
}

static void
obj_cpd_reply(crt_rpc_t *rpc, int status, uint32_t map_version)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct obj_cpd_out	*oco = crt_reply_get(rpc);
	int			 rc;

	if (!(oci->oci_flags & ORF_RESEND) &&
	    (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST) ||
	     DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY)))
		goto cleanup;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);

	D_DEBUG(DB_TRACE, "CPD rpc %p send reply, pmv %d, status %d.\n",
		rpc, map_version, status);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("Send CPD reply failed: "DF_RC"\n", DP_RC(rc));

cleanup:
	D_FREE(oco->oco_sub_rets.ca_arrays);
	oco->oco_sub_rets.ca_count = 0;

	D_FREE(oco->oco_sub_epochs.ca_arrays);
	oco->oco_sub_epochs.ca_count = 0;
}

/* Locally process the operations belong to one DTX.
 * Common logic, shared by both leader and non-leader.
 */
static int
ds_cpd_handle_one(crt_rpc_t *rpc, struct daos_cpd_sub_head *dcsh,
		  struct daos_cpd_disp_ent *dcde,
		  struct daos_cpd_sub_req *dcsrs,
		  struct obj_io_context *ioc, struct dtx_handle *dth)
{
	struct daos_cpd_req_idx		 *dcri = dcde->dcde_reqs;
	struct daos_cpd_sub_req		 *dcsr;
	struct daos_cpd_update		 *dcu;
	daos_handle_t			 *iohs = NULL;
	struct bio_desc			**biods = NULL;
	struct obj_bulk_args		 *bulks = NULL;
	struct dcs_iod_csums		 *csums;
	daos_iod_t			 *iods;
	uint64_t			 *offs = NULL;
	int				  rma = 0;
	int				  rma_idx = 0;
	int				  rc = 0;
	int				  i;
	uint64_t			  update_flags;

	if (dth->dth_flags & DTE_LEADER &&
	    DAOS_FAIL_CHECK(DAOS_DTX_RESTART))
		D_GOTO(out, rc = -DER_TX_RESTART);

	/* P1: Spread read TS. */
	for (i = 0; i < dcde->dcde_read_cnt; i++) {
		daos_handle_t	ioh;

		dcsr = &dcsrs[dcri[i].dcri_req_idx];
		if (dcsr->dcsr_opc != DCSO_READ) {
			D_ERROR(DF_DTI" expected sub read, but got opc %u\n",
				DP_DTI(&dcsh->dcsh_xid), dcsr->dcsr_opc);

			D_GOTO(out, rc = -DER_PROTO);
		}

		dcsr->dcsr_oid.id_shard = dcri[i].dcri_shard_id;
		rc = vos_fetch_begin(ioc->ioc_vos_coh, dcsr->dcsr_oid,
				     dcsh->dcsh_epoch.oe_value,
				     &dcsr->dcsr_dkey, dcsr->dcsr_nr,
				     dcsr->dcsr_read.dcr_iods,
				     VOS_OF_FETCH_SET_TS_ONLY, NULL, &ioh, dth);
		if (rc == 0)
			rc = vos_fetch_end(ioh, NULL, 0);
		else if (rc == -DER_NONEXIST)
			rc = 0;

		if (rc != 0) {
			D_ERROR("Failed to set read TS for obj "DF_UOID
				", DTX "DF_DTI": "DF_RC"\n",
				DP_UOID(dcsr->dcsr_oid),
				DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
			goto out;
		}
	}

	dcri += dcde->dcde_read_cnt;
	/* P2: vos_update_begin. */
	for (i = 0; i < dcde->dcde_write_cnt; i++) {
		dcsr = &dcsrs[dcri[i].dcri_req_idx];
		dcsr->dcsr_oid.id_shard = dcri[i].dcri_shard_id;

		if (dcsr->dcsr_opc != DCSO_UPDATE)
			continue;

		dcu = &dcsr->dcsr_update;
		if (dcsr->dcsr_nr != dcu->dcu_iod_array.oia_iod_nr) {
			D_ERROR("Unmatched iod NR %u vs %u for obj "DF_UOID
				", DTX "DF_DTI"\n", dcsr->dcsr_nr,
				dcu->dcu_iod_array.oia_iod_nr,
				DP_UOID(dcsr->dcsr_oid),
				DP_DTI(&dcsh->dcsh_xid));

			D_GOTO(out, rc = -DER_INVAL);
		}

		rc = csum_verify_keys(ioc->ioc_coc->sc_csummer,
				      &dcsr->dcsr_dkey, dcu->dcu_dkey_csum,
				      &dcu->dcu_iod_array, &dcsr->dcsr_oid);
		if (rc != 0) {
			if (rc == -DER_CSUM)
				obj_log_csum_err();

			goto out;
		}

		if (iohs == NULL) {
			D_ALLOC_ARRAY(iohs, dcde->dcde_write_cnt);
			if (iohs == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			D_ALLOC_ARRAY(biods, dcde->dcde_write_cnt);
			if (biods == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		if (dcu->dcu_flags & ORF_EC) {
			if (dcu->dcu_ec_split_req != NULL) {
				iods = dcu->dcu_ec_split_req->osr_iods;
				offs = dcu->dcu_ec_split_req->osr_offs;
				csums = dcu->dcu_ec_split_req->osr_iod_csums;
			} else {
				iods = dcu->dcu_iod_array.oia_iods;
				offs = dcu->dcu_iod_array.oia_offs;
				csums = dcu->dcu_iod_array.oia_iod_csums;
			}

			/* There is no object associated with this ioc while
			 * initializing, we have to do it at here.
			 */
			rc = obj_ioc_init_oca(ioc, dcsr->dcsr_oid.id_pub);
			if (rc)
				D_GOTO(out, rc);

			obj_singv_ec_rw_filter(dcsr->dcsr_oid, &ioc->ioc_oca,
					iods, offs, dcsh->dcsh_epoch.oe_value,
					dcu->dcu_flags, dcu->dcu_start_shard,
					dcsr->dcsr_nr, true, false, NULL);
		} else {
			iods = dcu->dcu_iod_array.oia_iods;
			csums = dcu->dcu_iod_array.oia_iod_csums;
		}

		update_flags = dcsr->dcsr_api_flags;
		if (dcu->dcu_flags & ORF_CPD_BULK &&
		    ioc->ioc_coc->sc_props.dcp_dedup_enabled) {
			update_flags |= VOS_OF_DEDUP;
			if (ioc->ioc_coc->sc_props.dcp_dedup_verify)
				update_flags |= VOS_OF_DEDUP_VERIFY;
		}

		rc = vos_update_begin(ioc->ioc_vos_coh,
				dcsr->dcsr_oid, dcsh->dcsh_epoch.oe_value,
				update_flags, &dcsr->dcsr_dkey,
				dcsr->dcsr_nr, iods, csums,
				ioc->ioc_coc->sc_props.dcp_dedup_size,
				&iohs[i], dth);
		if (rc != 0)
			goto out;

		biods[i] = vos_ioh2desc(iohs[i]);
		rc = bio_iod_prep(biods[i], BIO_CHK_TYPE_IO,
				  dcu->dcu_flags & ORF_CPD_BULK ?
					rpc->cr_ctx : NULL, CRT_BULK_RW);
		if (rc != 0) {
			D_ERROR("bio_iod_prep failed for obj "DF_UOID
				", DTX "DF_DTI": "DF_RC"\n",
				DP_UOID(dcsr->dcsr_oid),
				DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
			goto out;
		}

		if (dcu->dcu_flags & ORF_CPD_BULK) {
			if (bulks == NULL) {
				D_ALLOC_ARRAY(bulks, dcde->dcde_write_cnt);
				if (bulks == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}

			if (dcu->dcu_ec_split_req != NULL)
				offs = dcu->dcu_ec_split_req->osr_offs;
			else
				offs = dcu->dcu_iod_array.oia_offs;

			rc = obj_bulk_transfer(rpc, CRT_BULK_GET,
				dcu->dcu_flags & ORF_BULK_BIND, dcu->dcu_bulks,
				offs, iohs[i], NULL, dcsr->dcsr_nr, &bulks[i]);
			if (rc != 0) {
				D_ERROR("Bulk transfer failed for obj "
					DF_UOID", DTX "DF_DTI": "DF_RC"\n",
					DP_UOID(dcsr->dcsr_oid),
					DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
				goto out;
			}

			rma++;
		} else if (dcu->dcu_sgls != NULL) {
			rc = bio_iod_copy(biods[i], dcu->dcu_sgls,
					  dcsr->dcsr_nr);
			if (rc != 0) {
				D_ERROR("Non-bulk transfer failed for obj "
					DF_UOID", DTX "DF_DTI": "DF_RC"\n",
					DP_UOID(dcsr->dcsr_oid),
					DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
				if (rc == -DER_OVERFLOW)
					rc = -DER_REC2BIG;

				goto out;
			}
		}
	}

	/* P3: bulk data transafer. */
	for (i = 0; i < dcde->dcde_write_cnt && rma_idx < rma; i++) {
		int	*status;

		if (!bulks[i].inited)
			continue;

		rc = ABT_eventual_wait(bulks[i].eventual, (void **)&status);
		if (rc != 0)
			rc = dss_abterr2der(rc);
		if (rc == 0 && *status != 0)
			rc = *status;

		ABT_eventual_free(&bulks[i].eventual);
		bio_iod_flush(biods[i]);
		rma_idx++;

		if (rc != 0) {
			D_ERROR(DF_DTI" ABT_eventual_wait failed: "DF_RC"\n",
				DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));

			goto out;
		}
	}

	/* P4: punch and vos_update_end. */
	for (i = 0; i < dcde->dcde_write_cnt; i++) {
		dcsr = &dcsrs[dcri[i].dcri_req_idx];

		if (dcsr->dcsr_opc == DCSO_UPDATE) {
			dcu = &dcsr->dcsr_update;
			if (dcu->dcu_ec_split_req != NULL) {
				iods = dcu->dcu_ec_split_req->osr_iods;
				csums = dcu->dcu_ec_split_req->osr_iod_csums;
			} else {
				iods = dcu->dcu_iod_array.oia_iods;
				csums = dcu->dcu_iod_array.oia_iod_csums;
			}

			rc = vos_dedup_verify(iohs[i]);
			if (rc != 0) {
				D_ERROR("dedup_verify failed for obj "
					DF_UOID", DTX "DF_DTI": "DF_RC"\n",
					DP_UOID(dcsr->dcsr_oid),
					DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
					goto out;
			}

			rc = obj_verify_bio_csum(dcsr->dcsr_oid.id_pub,
						 iods, csums, biods[i],
						 ioc->ioc_coc->sc_csummer,
						 dcsr->dcsr_nr);
			if (rc != 0) {
				if (rc == -DER_CSUM)
					obj_log_csum_err();
				goto out;
			}

			rc = bio_iod_post(biods[i]);
			biods[i] = NULL;
			if (rc != 0) {
				D_ERROR("iod_post failed for obj "DF_UOID
					", DTX "DF_DTI": "DF_RC"\n",
					DP_UOID(dcsr->dcsr_oid),
					DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
				goto out;
			}

			rc = dtx_sub_init(dth, &dcsr->dcsr_oid,
					  dcsr->dcsr_dkey_hash);
			if (rc != 0)
				goto out;

			rc = vos_update_end(iohs[i], dth->dth_ver,
					    &dcsr->dcsr_dkey, rc, NULL, dth);
			iohs[i] = DAOS_HDL_INVAL;
			if (rc != 0)
				goto out;
		} else {
			daos_key_t	*dkey;

			if (dcsr->dcsr_opc == DCSO_PUNCH_OBJ) {
				dkey = NULL;
			} else if (dcsr->dcsr_opc == DCSO_PUNCH_DKEY ||
				   dcsr->dcsr_opc == DCSO_PUNCH_AKEY) {
				dkey = &dcsr->dcsr_dkey;
			} else {
				D_ERROR("Unknown sub request opc %u for obj "
					DF_UOID", DTX "DF_DTI":\n",
					dcsr->dcsr_opc, DP_UOID(dcsr->dcsr_oid),
					DP_DTI(&dcsh->dcsh_xid));

				D_GOTO(out, rc = -DER_PROTO);
			}

			rc = dtx_sub_init(dth, &dcsr->dcsr_oid,
					  dcsr->dcsr_dkey_hash);
			if (rc != 0)
				goto out;

			rc = vos_obj_punch(ioc->ioc_vos_coh, dcsr->dcsr_oid,
				dcsh->dcsh_epoch.oe_value, dth->dth_ver,
				dcsr->dcsr_api_flags, dkey,
				dkey != NULL ? dcsr->dcsr_nr : 0, dkey != NULL ?
				dcsr->dcsr_punch.dcp_akeys : NULL, dth);
			if (rc != 0)
				goto out;
		}
	}

out:
	if (rc != 0) {
		if (bulks != NULL) {
			for (i = 0;
			     i < dcde->dcde_write_cnt && rma_idx < rma; i++) {
				if (!bulks[i].inited)
					continue;

				ABT_eventual_wait(bulks[i].eventual, NULL);
				ABT_eventual_free(&bulks[i].eventual);
				rma_idx++;
			}
		}

		if (biods != NULL) {
			for (i = 0; i < dcde->dcde_write_cnt; i++) {
				if (biods[i] != NULL)
					bio_iod_post(biods[i]);
			}

		}

		if (iohs != NULL) {
			for (i = 0; i < dcde->dcde_write_cnt; i++) {
				if (daos_handle_is_inval(iohs[i]))
					continue;

				dcri = dcde->dcde_reqs + dcde->dcde_read_cnt;
				dcsr = &dcsrs[dcri[i].dcri_req_idx];
				vos_update_end(iohs[i], dth->dth_ver,
					       &dcsr->dcsr_dkey, rc, NULL, dth);
			}
		}
	}

	D_FREE(iohs);
	D_FREE(biods);
	D_FREE(bulks);

	return rc;
}

static int
ds_cpd_handle_one_wrap(crt_rpc_t *rpc, struct daos_cpd_sub_head *dcsh,
		       struct daos_cpd_disp_ent *dcde,
		       struct daos_cpd_sub_req *dcsrs,
		       struct obj_io_context *ioc,
		       struct dtx_handle *dth, bool pin)
{
	int	rc;

again:
	if (pin) {
		rc = vos_dtx_pin(dth, false);
		if (rc != 0)
			return rc;
	}

	rc = ds_cpd_handle_one(rpc, dcsh, dcde, dcsrs, ioc, dth);
	if (obj_dtx_need_refresh(dth, rc)) {
		rc = dtx_refresh(dth, ioc->ioc_coc);
		if (rc == -DER_AGAIN)
			goto again;
	}

	return rc;
}

static int
ds_obj_dtx_follower(crt_rpc_t *rpc, struct obj_io_context *ioc)
{
	struct dtx_handle		 dth = { 0 };
	struct obj_cpd_in		*oci = crt_req_get(rpc);
	struct daos_cpd_sub_head	*dcsh = ds_obj_cpd_get_dcsh(rpc, 0);
	struct daos_cpd_disp_ent	*dcde = ds_obj_cpd_get_dcde(rpc, 0, 0);
	struct daos_cpd_sub_req		*dcsr = ds_obj_cpd_get_dcsr(rpc, 0);
	uint32_t			 dtx_flags = DTX_DIST;
	int				 rc = 0;
	int				 rc1 = 0;

	D_DEBUG(DB_IO, "Handling DTX "DF_DTI" on non-leader\n",
		DP_DTI(&dcsh->dcsh_xid));

	D_ASSERT(dcsh->dcsh_epoch.oe_value != 0);
	D_ASSERT(dcsh->dcsh_epoch.oe_value != DAOS_EPOCH_MAX);

	if (oci->oci_flags & ORF_RESEND) {
		rc1 = dtx_handle_resend(ioc->ioc_vos_coh, &dcsh->dcsh_xid,
					&dcsh->dcsh_epoch.oe_value, NULL);

		/* Do nothing if 'prepared' or 'committed'. */
		if (rc1 == -DER_ALREADY || rc1 == 0)
			D_GOTO(out, rc = 0);

		dtx_flags |= DTX_RESEND;
	}

	/* Refuse any modification with old epoch. */
	if (dcde->dcde_write_cnt != 0 &&
	    dcsh->dcsh_epoch.oe_value < dss_get_start_epoch())
		D_GOTO(out, rc = -DER_TX_RESTART);

	/* The check for read capa has been done before handling the CPD RPC.
	 * So here, only need to check the write capa.
	 */
	if (dcde->dcde_write_cnt != 0) {
		rc = obj_capa_check(ioc->ioc_coh, true, false);
		if (rc != 0)
			goto out;
	}

	switch (rc1) {
	case -DER_NONEXIST:
	case 0:
		break;
	case -DER_MISMATCH:
		/* For resent RPC, abort it firstly if exist but with different
		 * (old) epoch, then re-execute with new epoch.
		 */
		rc = vos_dtx_abort(ioc->ioc_vos_coh, DAOS_EPOCH_MAX,
				   &dcsh->dcsh_xid, 1);

		if (rc < 0 && rc != -DER_NONEXIST)
			D_GOTO(out, rc);
		break;
	default:
		D_ASSERTF(rc1 < 0, "Resend check result: %d\n", rc1);
		D_GOTO(out, rc = rc1);
	}

	if (oci->oci_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	rc = dtx_begin(ioc->ioc_vos_coh, &dcsh->dcsh_xid, &dcsh->dcsh_epoch,
		       dcde->dcde_write_cnt, oci->oci_map_ver,
		       &dcsh->dcsh_leader_oid, NULL, 0, dtx_flags,
		       dcsh->dcsh_mbs, &dth);
	if (rc != 0)
		goto out;

	rc = ds_cpd_handle_one_wrap(rpc, dcsh, dcde, dcsr, ioc, &dth,
				    dth.dth_modification_cnt > 0 ?
				    true : false);

	/* For the case of only containing read sub operations, we will
	 * generate DTX entry for DTX recovery. Similarly for noop case.
	 */
	if (rc == 0 && (dth.dth_modification_cnt == 0 || !dth.dth_active))
		rc = vos_dtx_pin(&dth, true);

	rc = dtx_end(&dth, ioc->ioc_coc, rc);

out:
	D_CDEBUG(rc != 0 && rc != -DER_INPROGRESS && rc != -DER_TX_RESTART,
		 DLOG_ERR, DB_IO,
		 "Handled DTX "DF_DTI" on non-leader: "DF_RC"\n",
		 DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));

	return rc;
}

static int
obj_obj_dtx_leader(struct dtx_leader_handle *dlh, void *arg, int idx,
		   dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg	*exec_arg = arg;
	struct daos_cpd_args	*dca = exec_arg->args;
	int			 rc = 0;

	/* handle local operation */
	if (idx == -1) {
		if (!dlh->dlh_handle.dth_prepared) {
			struct obj_io_context		*ioc = dca->dca_ioc;
			struct daos_cpd_disp_ent	*dcde;
			struct daos_cpd_sub_head	*dcsh;
			struct daos_cpd_sub_req		*dcsrs;
			bool				 pin;

			dcde = ds_obj_cpd_get_dcde(dca->dca_rpc,
						   dca->dca_idx, 0);
			/* The check for read capa has been done before handling
			 * the CPD RPC. Here, only need to check the write capa.
			 */
			if (dcde->dcde_write_cnt != 0) {
				rc = obj_capa_check(ioc->ioc_coh, true, false);
				if (rc != 0) {
					comp_cb(dlh, idx, rc);

					return rc;
				}
			}

			dcsh = ds_obj_cpd_get_dcsh(dca->dca_rpc, dca->dca_idx);
			dcsrs = ds_obj_cpd_get_dcsr(dca->dca_rpc, dca->dca_idx);

			if (dlh->dlh_sub_cnt > 0 ||
			    dlh->dlh_handle.dth_modification_cnt > 0)
				pin = true;
			else
				pin = false;

			rc = ds_cpd_handle_one_wrap(dca->dca_rpc, dcsh, dcde,
						    dcsrs, ioc,
						    &dlh->dlh_handle, pin);
		}

		if (comp_cb != NULL)
			comp_cb(dlh, idx, rc);

		return rc;
	}

	/* Dispatch CPD RPC and handle sub requests remotely */
	return ds_obj_cpd_dispatch(dlh, arg, idx, comp_cb);
}

static int
ds_obj_dtx_leader_prep_handle(struct daos_cpd_sub_head *dcsh,
			      struct daos_cpd_sub_req *dcsrs,
			      struct daos_shard_tgt *tgts,
			      int tgt_cnt, int req_cnt, uint32_t *flags)
{
	struct dtx_daos_target	*ddt = &dcsh->dcsh_mbs->dm_tgts[0];
	int			 rc = 0;
	int			 i;

	for (i = 0; i < req_cnt; i++) {
		struct daos_cpd_sub_req		*dcsr;
		struct daos_cpd_update		*dcu;

		dcsr = &dcsrs[i];
		if (dcsr->dcsr_opc != DCSO_UPDATE)
			continue;

		dcu = &dcsr->dcsr_update;
		if (dcu->dcu_iod_array.oia_oiods == NULL)
			continue;

		rc = obj_ec_rw_req_split(dcsr->dcsr_oid, &dcu->dcu_iod_array,
					 dcsr->dcsr_nr, dcu->dcu_start_shard, 0,
					 ddt->ddt_id,
					 dcu->dcu_ec_tgts, dcsr->dcsr_ec_tgt_nr,
					 tgt_cnt, tgts, &dcu->dcu_ec_split_req);
		if (rc != 0) {
			D_ERROR("obj_ec_rw_req_split failed for obj "
				DF_UOID", DTX "DF_DTI": "DF_RC"\n",
				DP_UOID(dcsr->dcsr_oid),
				DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
			break;
		}

		if (dcu->dcu_ec_split_req != NULL)
			*flags |= ORF_HAS_EC_SPLIT;
	}

	return rc;
}

static void
ds_obj_dtx_leader_ult(void *arg)
{
	struct daos_cpd_args		*dca = arg;
	struct dtx_leader_handle	 dlh;
	struct ds_obj_exec_arg		 exec_arg;
	struct obj_cpd_in		*oci = crt_req_get(dca->dca_rpc);
	struct obj_cpd_out		*oco = crt_reply_get(dca->dca_rpc);
	struct daos_cpd_sub_head	*dcsh;
	struct daos_cpd_disp_ent	*dcde;
	struct daos_cpd_sub_req		*dcsrs = NULL;
	struct daos_shard_tgt		*tgts;
	uint32_t			 flags = 0;
	uint32_t			 dtx_flags = DTX_DIST;
	int				 tgt_cnt = 0;
	int				 req_cnt = 0;
	int				 rc = 0;
	bool				 need_abort = false;

	/* TODO: For the daos targets in the first redundancy (modification)
	 *	 group, they are the DTX leader candidates when DTX recovery.
	 *	 During DTX recovery, because the server that only holds read
	 *	 operations does not have DTX record, the new leader needs to
	 *	 re-dispatch related read-only sub requests to such server to
	 *	 handle the case of the old leader did not dispatch to it. So
	 *	 the DTX leader need to dispatch all read sub requests to the
	 *	 DTX leader candidates even if these sub requests will not be
	 *	 executed on DTX leader candidates. The DTX leader candidates
	 *	 will store related information the DTX entry.
	 */

	dcsh = ds_obj_cpd_get_dcsh(dca->dca_rpc, dca->dca_idx);

	D_DEBUG(DB_IO, "Handling DTX "DF_DTI" on leader, idx %u\n",
		DP_DTI(&dcsh->dcsh_xid), dca->dca_idx);

	if (daos_is_zero_dti(&dcsh->dcsh_xid)) {
		D_ERROR("DTX ID cannot be empty\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = process_epoch(&dcsh->dcsh_epoch.oe_value,
			   &dcsh->dcsh_epoch.oe_first,
			   &dcsh->dcsh_epoch.oe_rpc_flags);
	if (rc == PE_OK_LOCAL) {
		/*
		 * In this case, writes to local RDGs can use the chosen epoch
		 * without any uncertainty. This optimization is left to future
		 * work.
		 */
	}

	D_ASSERT(dcsh->dcsh_epoch.oe_value != 0);
	D_ASSERT(dcsh->dcsh_epoch.oe_value != DAOS_EPOCH_MAX);

	if (oci->oci_flags & ORF_RESEND) {
		dtx_flags |= DTX_RESEND;

again:
		/* For distributed transaction, the 'ORF_RESEND' may means
		 * that the DTX has been restarted with newer epoch.
		 */
		rc = dtx_handle_resend(dca->dca_ioc->ioc_vos_coh,
				       &dcsh->dcsh_xid,
				       &dcsh->dcsh_epoch.oe_value, NULL);
		switch (rc) {
		case -DER_ALREADY:
			/* Do nothing if 'committed'. */
			D_GOTO(out, rc = 0);
		case 0:
			/* For 'prepared' case, still need to dispatch. */
			flags = ORF_RESEND;
			break;
		case -DER_MISMATCH:
			/* XXX: For distributed transaction, there is race
			 *	between the client DTX commit with restart
			 *	and the DTX recovery on the new leader. It
			 *	is possible that the new leader is waiting
			 *	for others reply for related DTX recovery,
			 *	or the DTX recovery ULT is not started yet.
			 *
			 *	But we do not know whether the old leader
			 *	has ever committed related DTX before its
			 *	corruption or not. If yes, then abort DTX
			 *	with old epoch will break the semantics.
			 *
			 *	So here we need to wait the new leader to
			 *	recover such DTX: either commit or abort.
			 *	Let's return '-DER_INPROGRESS' to ask the
			 *	client to retry sometime later.
			 */
			D_GOTO(out, rc = -DER_INPROGRESS);
		default:
			if (rc < 0 && rc != -DER_NONEXIST)
				D_GOTO(out, rc);
			break;
		}
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST)) {
		D_GOTO(out, rc = 0);
	}

	dcde = ds_obj_cpd_get_dcde(dca->dca_rpc, dca->dca_idx, 0);
	dcsrs = ds_obj_cpd_get_dcsr(dca->dca_rpc, dca->dca_idx);
	tgts = ds_obj_cpd_get_tgts(dca->dca_rpc, dca->dca_idx);
	req_cnt = ds_obj_cpd_get_dcsr_cnt(dca->dca_rpc, dca->dca_idx);
	tgt_cnt = ds_obj_cpd_get_tgt_cnt(dca->dca_rpc, dca->dca_idx);

	if (dcde == NULL || dcsrs == NULL || tgts == NULL ||
	    req_cnt < 0 || tgt_cnt < 0)
		D_GOTO(out, rc = -DER_INVAL);

	/* Refuse any modification with old epoch. */
	if (dcde->dcde_write_cnt != 0 &&
	    dcsh->dcsh_epoch.oe_value < dss_get_start_epoch())
		D_GOTO(out, rc = -DER_TX_RESTART);

	rc = ds_obj_dtx_leader_prep_handle(dcsh, dcsrs, tgts, tgt_cnt,
					   req_cnt, &flags);
	if (rc != 0)
		goto out;

	/* 'tgts[0]' is for current dtx leader. */
	if (tgt_cnt == 1)
		tgts = NULL;
	else
		tgts++;

	if (tgt_cnt <= 1 && dcde->dcde_write_cnt <= 1)
		dtx_flags |= DTX_SOLO;
	if (flags & ORF_RESEND)
		dtx_flags |= DTX_PREPARED;
	else
		dtx_flags &= ~DTX_PREPARED;

	rc = dtx_leader_begin(dca->dca_ioc->ioc_vos_coh, &dcsh->dcsh_xid,
			      &dcsh->dcsh_epoch, dcde->dcde_write_cnt,
			      oci->oci_map_ver, &dcsh->dcsh_leader_oid,
			      NULL, 0, tgts, tgt_cnt - 1, dtx_flags,
			      dcsh->dcsh_mbs, &dlh);
	if (rc != 0)
		goto out;

	exec_arg.rpc = dca->dca_rpc;
	exec_arg.ioc = dca->dca_ioc;
	exec_arg.args = dca;
	exec_arg.flags = flags;

	/* Execute the operation on all targets */
	rc = dtx_leader_exec_ops(&dlh, obj_obj_dtx_leader, NULL, NULL, &exec_arg);

	/* Stop the distribute transaction */
	rc = dtx_leader_end(&dlh, dca->dca_ioc->ioc_coc, rc);

out:
	D_CDEBUG(rc != 0 && rc != -DER_INPROGRESS && rc != -DER_TX_RESTART &&
		 rc != -DER_AGAIN, DLOG_ERR, DB_IO,
		 "Handled DTX "DF_DTI" on leader, idx %u: "DF_RC"\n",
		 DP_DTI(&dcsh->dcsh_xid), dca->dca_idx, DP_RC(rc));

	if (tgt_cnt > 0) {
		struct daos_cpd_sub_req	*dcsr;
		int			 i;

		for (i = 0; i < req_cnt; i++) {
			dcsr = &dcsrs[i];
			if (dcsr->dcsr_opc != DCSO_UPDATE)
				continue;

			obj_ec_split_req_fini(
					dcsr->dcsr_update.dcu_ec_split_req);
		}
	}

	if (rc == -DER_AGAIN) {
		oci->oci_flags |= ORF_RESEND;
		need_abort = true;
		goto again;
	}

	if (rc != 0 && need_abort) {
		struct dtx_entry	 dte;
		struct dtx_entry	*pdte;
		int			 rc1;

		dte.dte_xid = dcsh->dcsh_xid;
		dte.dte_ver = oci->oci_map_ver;
		dte.dte_refs = 1;
		dte.dte_mbs = dcsh->dcsh_mbs;
		pdte = &dte;
		rc1 = dtx_abort(dca->dca_ioc->ioc_coc,
				dcsh->dcsh_epoch.oe_value, &pdte, 1);
		if (rc1 != 0 && rc1 != -DER_NONEXIST)
			D_WARN("Failed to abort DTX "DF_DTI": "DF_RC"\n",
			       DP_DTI(&dcsh->dcsh_xid), DP_RC(rc1));
	}

	ds_obj_cpd_set_sub_result(oco, dca->dca_idx, rc,
				  dcsh->dcsh_epoch.oe_value);

	rc = ABT_future_set(dca->dca_future, NULL);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_set failed %d.\n", rc);
}

void
ds_obj_cpd_handler(crt_rpc_t *rpc)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct obj_cpd_out	*oco = crt_reply_get(rpc);
	struct daos_cpd_args	*dcas = NULL;
	struct obj_io_context	 ioc;
	ABT_future		 future = ABT_FUTURE_NULL;
	int			 tx_count = oci->oci_sub_heads.ca_count;
	int			 rc = 0;
	int			 i;
	bool			 leader;

	D_ASSERT(oci != NULL);

	if (oci->oci_flags & ORF_CPD_LEADER)
		leader = true;
	else
		leader = false;

	D_DEBUG(DB_TRACE,
		"Handling CPD rpc %p on %s against "DF_UUID"/"DF_UUID"/"DF_UUID
		" with CPD count %u, flags %u\n",
		rpc, leader ? "leader" : "non-leader",
		DP_UUID(oci->oci_pool_uuid), DP_UUID(oci->oci_co_hdl),
		DP_UUID(oci->oci_co_uuid), tx_count, oci->oci_flags);

	rc = obj_ioc_begin_lite(oci->oci_map_ver, oci->oci_pool_uuid,
				oci->oci_co_hdl, oci->oci_co_uuid,
				opc_get(rpc->cr_opc), &ioc);
	if (rc != 0)
		goto reply;

	if (!leader) {
		if (tx_count != 1 || oci->oci_sub_reqs.ca_count != 1 ||
		    oci->oci_disp_ents.ca_count != 1 ||
		    oci->oci_disp_tgts.ca_count != 0) {
			D_ERROR("Unexpected CPD RPC format for non-leader: "
				"head %u, req set %lu, disp %lu, tgts %lu\n",
				tx_count, oci->oci_sub_reqs.ca_count,
				oci->oci_disp_ents.ca_count,
				oci->oci_disp_tgts.ca_count);

			D_GOTO(reply, rc = -DER_PROTO);
		}

		oco->oco_sub_rets.ca_arrays = NULL;
		oco->oco_sub_rets.ca_count = 0;
		rc = ds_obj_dtx_follower(rpc, &ioc);

		D_GOTO(reply, rc);
	}

	if (tx_count != oci->oci_sub_reqs.ca_count ||
	    tx_count != oci->oci_disp_ents.ca_count ||
	    tx_count != oci->oci_disp_tgts.ca_count || tx_count == 0) {
		D_ERROR("Unexpected CPD RPC format for leader: "
			"head %u, req set %lu, disp %lu, tgts %lu\n",
			tx_count, oci->oci_sub_reqs.ca_count,
			oci->oci_disp_ents.ca_count,
			oci->oci_disp_tgts.ca_count);

		D_GOTO(reply, rc = -DER_PROTO);
	}

	D_ALLOC(oco->oco_sub_rets.ca_arrays, sizeof(int32_t) * tx_count);
	if (oco->oco_sub_rets.ca_arrays == NULL)
		D_GOTO(reply, rc = -DER_NOMEM);

	D_ALLOC(oco->oco_sub_epochs.ca_arrays, sizeof(int64_t) * tx_count);
	if (oco->oco_sub_epochs.ca_arrays == NULL)
		D_GOTO(reply, rc = -DER_NOMEM);

	oco->oco_sub_rets.ca_count = tx_count;
	oco->oco_sub_epochs.ca_count = tx_count;

	/* TODO: optimize it if there is only single DTX in the CPD RPC. */

	D_ALLOC_ARRAY(dcas, tx_count);
	if (dcas == NULL)
		D_GOTO(reply, rc = -DER_NOMEM);

	rc = ABT_future_create(tx_count, NULL, &future);
	if (rc != ABT_SUCCESS)
		D_GOTO(reply, rc = dss_abterr2der(rc));

	for (i = 0; i < tx_count; i++) {
		dcas[i].dca_ioc = &ioc;
		dcas[i].dca_rpc = rpc;
		dcas[i].dca_future = future;
		dcas[i].dca_idx = i;

		rc = dss_ult_create(ds_obj_dtx_leader_ult, &dcas[i],
				    DSS_XS_SELF, 0, 0, NULL);
		if (rc != 0) {
			struct daos_cpd_sub_head	*dcsh;

			ABT_future_set(future, NULL);
			dcsh = ds_obj_cpd_get_dcsh(rpc, i);
			ds_obj_cpd_set_sub_result(oco, i, rc,
						  dcsh->dcsh_epoch.oe_value);
			/* Continue to handle other independent DTXs. */
			continue;
		}
	}

	rc = ABT_future_wait(future);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_wait failed %d.\n", rc);

	ABT_future_free(&future);

reply:
	D_FREE(dcas);
	obj_cpd_reply(rpc, rc, ioc.ioc_map_ver);
	obj_ioc_end(&ioc, rc);
}
