/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include <daos_srv/srv_csum.h>
#include "obj_rpc.h"
#include "srv_internal.h"

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
obj_gen_dtx_mbs(uint32_t flags, uint32_t *tgt_cnt, struct daos_shard_tgt **p_tgts,
		struct dtx_memberships **p_mbs)
{
	struct daos_shard_tgt	*tgts = *p_tgts;
	struct dtx_memberships	*mbs = NULL;
	size_t			 size;
	int			 i;
	int			 j;

	if (*tgt_cnt == 0)
		return 0;

	D_ASSERT(tgts != NULL);

	if (!(flags & ORF_CONTAIN_LEADER)) {
		D_ERROR("Miss DTX leader information, flags %x\n", flags);
		return -DER_PROTO;
	}

	if (*tgt_cnt == 1) {
		*tgt_cnt = 0;
		*p_tgts = NULL;
		goto out;
	}

	size = sizeof(struct dtx_daos_target) * *tgt_cnt;
	D_ALLOC(mbs, sizeof(*mbs) + size);
	if (mbs == NULL)
		return -DER_NOMEM;

	for (i = 0, j = 0; i < *tgt_cnt; i++) {
		if (tgts[i].st_rank == DAOS_TGT_IGNORE)
			continue;

		mbs->dm_tgts[j++].ddt_id = tgts[i].st_tgt_id;
	}

	D_ASSERT(j > 0);

	if (j == 1) {
		D_FREE(mbs);
		*tgt_cnt = 0;
		*p_tgts = NULL;
		goto out;
	}

	mbs->dm_tgt_cnt = j;
	mbs->dm_grp_cnt = 1;
	mbs->dm_data_size = size;
	mbs->dm_flags = DMF_CONTAIN_LEADER;

	--(*tgt_cnt);
	*p_tgts = ++tgts;

	if (!(flags & ORF_EC))
		mbs->dm_flags |= DMF_SRDG_REP;

out:
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
			uint64_t time;

			if (status == 0)
				status = dtx_sub_init(dth, &orwi->orw_oid,
						      orwi->orw_dkey_hash);
			time = daos_get_ntime();
			rc = vos_update_end(ioh, ioc->ioc_map_ver,
					    &orwi->orw_dkey, status,
					    &ioc->ioc_io_size, dth);
			if (rc == 0)
				obj_update_latency(ioc->ioc_opc, VOS_LATENCY,
						   daos_get_ntime() - time, ioc->ioc_io_size);
		} else {
			rc = vos_fetch_end(ioh, &ioc->ioc_io_size, status);
		}

		if (rc != 0) {
			if (rc == -DER_VOS_PARTIAL_UPDATE)
				rc = -DER_NO_PERM;
			DL_CDEBUG(rc == -DER_REC2BIG || rc == -DER_INPROGRESS ||
				      rc == -DER_TX_RESTART || rc == -DER_EXIST ||
				      rc == -DER_NONEXIST || rc == -DER_ALREADY ||
				      rc == -DER_CHKPT_BUSY,
				  DLOG_DBG, DLOG_ERR, rc, DF_UOID " %s end failed",
				  DP_UOID(orwi->orw_oid), update ? "Update" : "Fetch");
			if (status == 0)
				status = rc;
		}
	}

	return status;
}

static void
obj_rw_reply(crt_rpc_t *rpc, int status, uint64_t epoch, bool release_input,
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
				  d_hlc_epsilon_get() * 3;
	} else {
		/* orwo->orw_epoch possibly updated in obj_ec_recov_need_try_again(), reply
		 * the max so client can fetch from that epoch.
		 */
		orwo->orw_epoch = max(epoch, orwo->orw_epoch);
	}

	D_DEBUG(DB_IO, "rpc %p opc %d send reply, pmv %d, epoch "DF_X64
		", status %d\n", rpc, opc_get(rpc->cr_opc),
		ioc->ioc_map_ver, orwo->orw_epoch, status);

	if (!ioc->ioc_lost_reply) {
		if (release_input)
			rc = crt_reply_send_input_free(rpc);
		else
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
			ds_iom_free(&orwo->orw_maps.ca_arrays, orwo->orw_maps.ca_count);
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

#define MAX_BULK_IOVS	1024
#define BULK_DELAY_MAX  3000
#define BULK_DELAY_STEP 1000

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
	int			rc, bulk_iovs = 0;

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
		rc = -DER_OVERFLOW;
		D_ERROR("remote_bulk_off %zu >= remote_bulk_size %zu, "DF_RC"\n",
			remote_off, remote_size, DP_RC(rc));
		return rc;
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
		uint32_t        delay_tot = 0;
		uint32_t        delay_cur;
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
			rc = -DER_OVERFLOW;
			D_ERROR("Remote bulk is used up. off:%zu, size:%zu, "DF_RC"\n",
				remote_off, remote_size, DP_RC(rc));
			break;
		}

		local_bulk = vos_iod_bulk_at(ioh, sgl_idx, iov_idx, &local_off);
		if (local_bulk != NULL) {
			unsigned int tmp_off;

			length = sgl->sg_iovs[iov_idx].iov_len;
			iov_idx++;
			cached_bulk = true;

			/* Check if following IOVs are contiguous and from same bulk handle */
			while (iov_idx < sgl->sg_nr_out &&
			       sgl->sg_iovs[iov_idx].iov_buf != NULL &&
			       vos_iod_bulk_at(ioh, sgl_idx, iov_idx, &tmp_off) == local_bulk &&
			       tmp_off == local_off) {
				length += sgl->sg_iovs[iov_idx].iov_len;
				iov_idx++;
			};
			bulk_iovs += 1;
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

				/* Don't create bulk handle with too many IOVs */
				if ((iov_idx - start) >= MAX_BULK_IOVS)
					break;
			};
			D_ASSERT(iov_idx > start);

			local_off = 0;
			sgl_sent.sg_nr = sgl_sent.sg_nr_out = iov_idx - start;
			bulk_iovs += sgl_sent.sg_nr;

again:
			rc = crt_bulk_create(rpc->cr_ctx, &sgl_sent, bulk_perm,
					     &local_bulk);
			if (rc == -DER_NOMEM) {
				if (delay_tot >= BULK_DELAY_MAX) {
					D_ERROR("Too many in-flight bulk handles on %d\n", sgl_idx);
					break;
				}

				/*
				 * If there are too many in-flight bulk handles, then current
				 * crt_bulk_create() may hit -DER_NOMEM failure. Let it sleep
				 * for a while (at most 3 seconds) to give cart progress some
				 * chance to complete some in-flight bulk transfers.
				 */
				delay_cur = BULK_DELAY_MAX - delay_tot;
				if (delay_cur >= BULK_DELAY_STEP)
					delay_cur = d_rand() % BULK_DELAY_STEP + 1;
				dss_sleep(delay_cur);
				delay_tot += delay_cur;
				bulk_iovs = 0;
				goto again;
			}

			if (rc != 0) {
				D_ERROR("crt_bulk_create %d error " DF_RC "\n", sgl_idx, DP_RC(rc));
				break;
			}
			D_ASSERT(local_bulk != NULL);
		}

		D_ASSERT(remote_size > remote_off);
		if (length > (remote_size - remote_off)) {
			rc = -DER_OVERFLOW;
			D_ERROR("Remote bulk isn't large enough. local_sz:%zu, remote_sz:%zu, "
				"remote_off:%zu, "DF_RC"\n", length, remote_size, remote_off,
				DP_RC(rc));
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

		p_arg->bulk_size += length;
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
			D_ERROR("crt_bulk_transfer %d error " DF_RC "\n", sgl_idx, DP_RC(rc));
			p_arg->bulks_inflight--;
			if (!cached_bulk)
				crt_bulk_free(local_bulk);
			crt_req_decref(rpc);
			break;
		}
		remote_off += length;

		/* Give cart progress a chance to complete some in-flight bulk transfers */
		if (bulk_iovs >= MAX_BULK_IOVS) {
			bulk_iovs = 0;
			ABT_thread_yield();
		}
	}

	return rc;
}

int
obj_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op, bool bulk_bind, crt_bulk_t *remote_bulks,
		  uint64_t *remote_offs, uint8_t *skips, daos_handle_t ioh, d_sg_list_t **sgls,
		  int sgl_nr, int bulk_nr, struct obj_bulk_args *p_arg)
{
	struct obj_bulk_args	arg = { 0 };
	int			i, rc, *status, ret;
	int			skip_nr = 0;
	bool			async = true;
	uint64_t		time = daos_get_ntime();

	if (unlikely(sgl_nr > bulk_nr)) {
		D_ERROR("Invalid sgl_nr vs bulk_nr: %d/%d\n", sgl_nr, bulk_nr);
		return -DER_INVAL;
	}

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
	D_DEBUG(DB_IO, "bulk_op %d, sgl_nr %d, bulk_nr %d\n", bulk_op, sgl_nr, bulk_nr);

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

		/* Need to consider the akey skip case as client-side bulk array with one bulk for
		 * every akey, but some akeys possibly be skipped in obj_get_iods_offs (number of
		 * akeys possibly reduced). So here need to find the corresponding bulk handle
		 * for non-skipped akey. The akey skip case only possible for EC object and targeted
		 * for multiple data shards.
		 * For RPC inline (non-bulk) case (bio_iod_copy) need not consider the akey skip
		 * because we always create bulk handle if EC object IO targeted for multiple
		 * data shards.
		 */
		while (skips != NULL && isset(skips, i + skip_nr))
			skip_nr++;

		D_ASSERTF(i + skip_nr < bulk_nr, "i %d, skip_nr %d, sgl_nr %d, bulk_nr %d\n",
			  i, skip_nr, sgl_nr, bulk_nr);

		if (remote_bulks[i + skip_nr] == NULL)
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

		rc = bulk_transfer_sgl(ioh, rpc, remote_bulks[i + skip_nr],
				       remote_offs ? remote_offs[i] : 0,
				       bulk_op, bulk_bind, sgl, i, p_arg);
		if (sgls == NULL)
			d_sgl_fini(sgl, false);
		if (rc) {
			D_ERROR("bulk_transfer_sgl i %d, skip_nr %d failed, "DF_RC"\n",
				i, skip_nr, DP_RC(rc));
			break;
		}
	}

	if (skips != NULL)
		D_ASSERTF(skip_nr + sgl_nr <= bulk_nr,
			  "Unmatched skip_nr %d, sgl_nr %d, bulk_nr %d\n",
			  skip_nr, sgl_nr, bulk_nr);

done:
	if (--(p_arg->bulks_inflight) == 0)
		ABT_eventual_set(p_arg->eventual, &rc, sizeof(rc));

	if (async)
		return rc;

	ret = ABT_eventual_wait(p_arg->eventual, (void **)&status);
	if (rc == 0)
		rc = ret ? dss_abterr2der(ret) : *status;

	ABT_eventual_free(&p_arg->eventual);

	if (rc == 0)
		obj_update_latency(opc_get(rpc->cr_opc), BULK_LATENCY, daos_get_ntime() - time,
				   arg.bulk_size);

	if (rc == 0 && p_arg->result != 0)
		rc = p_arg->result;

	/* After RDMA is done, corrupt the server data */
	if (rc == 0 && DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_DISK)) {
		struct bio_sglist	*fbsgl;
		d_sg_list_t		 fsgl;
		int			*fbuffer;

		D_ERROR("csum: Corrupting data after RDMA\n");
		fbsgl = vos_iod_sgl_at(ioh, 0);
		bio_sgl_convert(fbsgl, &fsgl);
		fbuffer = (int *)fsgl.sg_iovs[0].iov_buf;
		*fbuffer += 0x2;
		d_sgl_fini(&fsgl, false);
	}
	return rc;
}

static int
obj_set_reply_sizes(crt_rpc_t *rpc, daos_iod_t *iods, int iod_nr, uint8_t *skips)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	uint64_t		*sizes = NULL;
	int			 orw_iod_nr = orw->orw_nr;
	int			 i, idx;

	D_ASSERT(obj_rpc_is_fetch(rpc));
	D_ASSERT(orwo != NULL);
	D_ASSERT(orw != NULL);

	if (orw->orw_flags & ORF_CHECK_EXISTENCE)
		goto out;

	if (orw_iod_nr <= 0) {
		D_ERROR("rpc %p contains invalid sizes count %d for "
			DF_UOID" with epc "DF_X64".\n",
			rpc, orw_iod_nr, DP_UOID(orw->orw_oid), orw->orw_epoch);
		return -DER_INVAL;
	}

	/* Re-entry case.*/
	if (orwo->orw_iod_sizes.ca_count != 0) {
		D_ASSERT(orwo->orw_iod_sizes.ca_count == orw_iod_nr);
		D_ASSERT(orwo->orw_iod_sizes.ca_arrays != NULL);

		sizes = orwo->orw_iod_sizes.ca_arrays;
	} else {
		D_ALLOC_ARRAY(sizes, orw_iod_nr);
		if (sizes == NULL)
			return -DER_NOMEM;
	}

	for (i = 0, idx = 0; i < orw_iod_nr; i++) {
		if (skips != NULL && isset(skips, i)) {
			sizes[i] = 0;
			continue;
		}
		sizes[i] = iods[idx].iod_size;
		D_DEBUG(DB_IO, DF_UOID" %d:"DF_U64"\n", DP_UOID(orw->orw_oid),
			i, iods[idx].iod_size);
		idx++;
	}

	D_ASSERTF(idx == iod_nr, "idx %d, iod_nr %d\n", idx, iod_nr);

out:
	if (sizes == NULL)
		orw_iod_nr = 0;
	orwo->orw_iod_sizes.ca_count = orw_iod_nr;
	orwo->orw_iod_sizes.ca_arrays = sizes;

	D_DEBUG(DB_TRACE, "rpc %p set sizes count as %d for "
		DF_UOID" with epc "DF_X64".\n",
		rpc, orw_iod_nr, DP_UOID(orw->orw_oid), orw->orw_epoch);

	return 0;
}

/**
 * Pack nrs in sgls inside the reply, so the client can update
 * sgls before it returns to application.
 * Pack sgl's data size in the reply, client fetch can based on
 * it to update sgl's iov_len.
 *
 * @echo_sgl is set only for echo_rw()
 *
 * Note: this is only needed for bulk transfer, for inline transfer,
 * it will pack the complete sgls inside the req/reply, see obj_shard_rw().
 */
static int
obj_set_reply_nrs(crt_rpc_t *rpc, daos_handle_t ioh, d_sg_list_t *echo_sgl, uint8_t *skips)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	uint32_t		*nrs;
	daos_size_t		*data_sizes;
	uint32_t		 nrs_count = orw->orw_nr;
	int			 i, j, idx;

	if (nrs_count == 0 || (orw->orw_flags & ORF_CHECK_EXISTENCE))
		return 0;

	/* Re-entry case. */
	if (orwo->orw_nrs.ca_count != 0) {
		D_ASSERT(orwo->orw_nrs.ca_count == nrs_count);
		D_ASSERT(orwo->orw_data_sizes.ca_count == nrs_count);
		D_ASSERT(orwo->orw_nrs.ca_arrays != NULL);
		D_ASSERT(orwo->orw_data_sizes.ca_arrays != NULL);
	} else {
		/* return sg_nr_out and data size for sgl */
		D_ALLOC(orwo->orw_nrs.ca_arrays,
			nrs_count * (sizeof(uint32_t) + sizeof(daos_size_t)));
		if (orwo->orw_nrs.ca_arrays == NULL)
			return -DER_NOMEM;

		orwo->orw_nrs.ca_count = nrs_count;
		orwo->orw_data_sizes.ca_count = nrs_count;
		orwo->orw_data_sizes.ca_arrays = (void *)((char *)orwo->orw_nrs.ca_arrays +
						 nrs_count * (sizeof(uint32_t)));
	}

	nrs = orwo->orw_nrs.ca_arrays;
	data_sizes = orwo->orw_data_sizes.ca_arrays;
	for (i = 0, idx = 0; i < nrs_count; i++) {
		if (skips != NULL && isset(skips, i)) {
			nrs[i] = 0;
			data_sizes[i] = 0;
			continue;
		}
		if (echo_sgl != NULL) {
			nrs[i] = echo_sgl->sg_nr_out;
		} else {
			struct bio_sglist *bsgl;

			bsgl = vos_iod_sgl_at(ioh, idx);
			D_ASSERT(bsgl != NULL);
			nrs[i] = bsgl->bs_nr_out;
			/* tail holes trimmed by ioc_trim_tail_holes() */
			for (j = 0; j < bsgl->bs_nr_out; j++)
				data_sizes[i] += bio_iov2req_len(
					&bsgl->bs_iovs[j]);
		}
		idx++;
	}

	return 0;
}

static void
obj_echo_rw(crt_rpc_t *rpc, daos_iod_t *iod, uint64_t *off)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	struct obj_tls		*tls;
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
					 orw->orw_iod_array.oia_iod_nr, NULL);
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
		rc = obj_set_reply_nrs(rpc, DAOS_HDL_INVAL, p_sgl, NULL);
		if (rc != 0)
			D_GOTO(out, rc);
		bulk_op = CRT_BULK_PUT;
	} else {
		bulk_op = CRT_BULK_GET;
	}

	/* Only support 1 iod now */
	bulk_bind = orw->orw_flags & ORF_BULK_BIND;
	rc        = obj_bulk_transfer(rpc, bulk_op, bulk_bind, orw->orw_bulks.ca_arrays, off, NULL,
				      DAOS_HDL_INVAL, &p_sgl, 1, 1, NULL);
out:
	orwo->orw_ret = rc;
	orwo->orw_map_version = orw->orw_map_ver;
}

/** if checksums are enabled, fetch needs to allocate the memory that will be
 * used for the csum structures.
 */
static int
obj_fetch_csum_init(struct ds_cont_child *cont, struct obj_rw_in *orw, struct obj_rw_out *orwo)
{
	int rc;

	/**
	 * Allocate memory for the csum structures.
	 * This memory and information will be used by VOS to put the checksums
	 * in as it fetches the data's metadata from the btree/evtree.
	 *
	 * The memory will be freed in obj_rw_reply
	 */

	/* Re-entry case. */
	if (orwo->orw_iod_csums.ca_count != 0) {
		D_ASSERT(orwo->orw_iod_csums.ca_arrays != NULL);
		rc = 0;
	} else {
		rc = daos_csummer_alloc_iods_csums(cont->sc_csummer, orw->orw_iod_array.oia_iods,
						   orw->orw_iod_array.oia_iod_nr, false, NULL,
						   &orwo->orw_iod_csums.ca_arrays);

		if (rc >= 0) {
			orwo->orw_iod_csums.ca_count = rc;
			rc = 0;
		}
	}

	return rc;
}

static inline struct dcs_iod_csums *
get_iod_csum(struct dcs_iod_csums *iod_csums, int i)
{
	if (iod_csums == NULL)
		return NULL;
	return &iod_csums[i];
}

static int
csum_add2iods(daos_handle_t ioh, daos_iod_t *iods, uint32_t iods_nr,
	      uint8_t *skips, struct daos_csummer *csummer,
	      struct dcs_iod_csums *iod_csums, daos_unit_oid_t oid,
	      daos_key_t *dkey)
{
	int	 rc = 0;
	uint32_t biov_csums_idx = 0;
	size_t	 biov_csums_used = 0;
	int	 i, idx;

	struct bio_desc *biod = vos_ioh2desc(ioh);
	struct dcs_ci_list *csum_infos = vos_ioh2ci(ioh);
	uint32_t csum_info_nr = vos_ioh2ci_nr(ioh);

	for (i = 0, idx = 0; i < iods_nr; i++) {
		if (skips != NULL && isset(skips, i))
			continue;
		if (biov_csums_idx >= csum_info_nr)
			break; /** no more csums to add */
		csum_infos->dcl_csum_offset += biov_csums_used;
		rc = ds_csum_add2iod(
			&iods[i], csummer,
			bio_iod_sgl(biod, idx), csum_infos,
			&biov_csums_used, get_iod_csum(iod_csums, i));
		idx++;
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
	return ds_csum_verify_keys(csummer, dkey, dkey_csum, oia->oia_iods, oia->oia_iod_csums,
				   oia->oia_iod_nr, uoid);
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
		       uint32_t tgt_off, daos_iod_t *iods, uint64_t *offs,
		       daos_epoch_t epoch, uint32_t flags, uint32_t nr,
		       bool for_update, bool deg_fetch,
		       struct daos_recx_ep_list **recov_lists_ptr)
{
	daos_iod_t			*iod;
	struct obj_ec_singv_local	 loc;
	uint32_t			 i;
	int				 rc = 0;
	bool				 reentry = false;

	if (!(flags & ORF_EC))
		return rc;

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

		obj_ec_singv_local_sz(gsize, oca, tgt_off, &loc, for_update);
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

	bio_log_data_csum_err(bxc);
}

/**
 * Create maps for actually written to extents.
 * Memory allocated here will be freed in obj_rw_reply.
 */

/** create maps for actually written to extents. */
static int
obj_fetch_create_maps(crt_rpc_t *rpc, struct bio_desc *biod, daos_iod_t *iods, uint32_t iods_nr,
		      uint8_t *skips)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	daos_iom_t		*maps;
	daos_iom_t		*result_maps = NULL;
	uint32_t		 flags = orw->orw_flags;
	uint32_t		 total_nr;
	uint32_t		 i, idx;
	int rc;

	/**
	 * Allocate memory for the maps. There will be 1 per iod
	 * Will be freed in obj_rw_reply
	 */
	total_nr = orw->orw_iod_array.oia_iod_nr;
	if (skips == NULL)
		D_ASSERTF(total_nr == iods_nr, "total nr %d, iods_nr %d\n", total_nr, iods_nr);

	/* Re-entry case, iods may be changed, let's re-generate the maps. */
	if (orwo->orw_maps.ca_arrays != NULL) {
		ds_iom_free(&orwo->orw_maps.ca_arrays, orwo->orw_maps.ca_count);
		orwo->orw_maps.ca_count = 0;
	}

	rc = ds_iom_create(biod, iods, iods_nr, flags, &maps);
	if (rc != 0)
		return rc;

	/* need some post process for iom if some akeys skipped */
	if (total_nr > iods_nr) {
		D_ASSERT(skips != NULL);
		D_ALLOC_ARRAY(result_maps, total_nr);
		if (result_maps == NULL) {
			ds_iom_free(&maps, iods_nr);
			return -DER_NOMEM;
		}
		for (i = 0, idx = 0; i < total_nr; i++) {
			if (!isset(skips, i))
				result_maps[i] = maps[idx++];
		}
		D_ASSERTF(idx == iods_nr, "idx %d, iods_nr %d\n", idx, iods_nr);
		/* maps' iom_recxs is assigned to result maps */
		D_FREE(maps);
	} else {
		result_maps = maps;
	}

	orwo->orw_maps.ca_count = total_nr;
	orwo->orw_maps.ca_arrays = result_maps;

	return 0;
}

static int
obj_fetch_shadow(struct obj_io_context *ioc, daos_unit_oid_t oid,
		 daos_epoch_t epoch, uint64_t cond_flags, daos_key_t *dkey,
		 uint64_t dkey_hash, unsigned int iod_nr, daos_iod_t *iods,
		 uint32_t tgt_idx, struct dtx_handle *dth,
		 struct daos_recx_ep_list **pshadows)
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
		uint32_t tgt_off;

		tgt_off = obj_ec_shard_off_by_layout_ver(ioc->ioc_layout_ver, dkey_hash,
							 &ioc->ioc_oca, tgt_idx);
		obj_shadow_list_vos2daos(iod_nr, *pshadows, &ioc->ioc_oca);
		rc = obj_iod_recx_vos2daos(iod_nr, iods, tgt_off, &ioc->ioc_oca);
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

	/* Re-entry case. */
	if (ioc->ioc_free_sgls)
		return 0;

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
obj_ec_recov_need_try_again(struct obj_rw_in *orw, struct obj_rw_out *orwo,
			    struct obj_io_context *ioc)
{
	D_ASSERT(orw->orw_flags & ORF_EC_RECOV);

	if (DAOS_FAIL_CHECK(DAOS_FAIL_AGG_BOUNDRY_MOVED))
		return true;

	/* agg_eph_boundary advanced, possibly cause epoch of EC data recovery
	 * cannot get corresponding parity/data exts, need to retry the degraded
	 * fetch from beginning. For ORF_EC_RECOV_SNAP case, need not retry as
	 * that flag was only set when (snapshot_epoch < sc_ec_agg_eph_boundary).
	 */
	if ((orw->orw_flags & ORF_EC_RECOV_SNAP) == 0 &&
	    (orw->orw_flags & ORF_FOR_MIGRATION) == 0 &&
	    orw->orw_epoch < ioc->ioc_coc->sc_ec_agg_eph_boundary) {
		orwo->orw_epoch = ioc->ioc_coc->sc_ec_agg_eph_boundary;
		return true;
	}

	return false;
}

static inline uint64_t
orf_to_dtx_epoch_flags(enum obj_rpc_flags orf_flags)
{
	uint64_t flags = 0;

	if (orf_flags & ORF_EPOCH_UNCERTAIN)
		flags |= DTX_EPOCH_UNCERTAIN;
	return flags;
}

static int
obj_rw_recx_list_post(struct obj_rw_in *orw, struct obj_rw_out *orwo, uint8_t *skips, int rc)
{
	struct daos_recx_ep_list	*lists;
	struct daos_recx_ep_list	*old_lists;
	int				 list_nr, i, idx;

	list_nr = orwo->orw_rels.ca_count;
	D_ASSERTF(list_nr != orw->orw_nr, "bad list_nr %d\n", list_nr);
	D_ALLOC_ARRAY(lists, orw->orw_nr);
	if (lists == NULL)
		return rc ? rc : -DER_NOMEM;

	old_lists = orwo->orw_rels.ca_arrays;
	for (i = 0, idx = 0; i < orw->orw_nr; i++) {
		if (isset(skips, i)) {
			lists[i].re_ep_valid = 1;
			continue;
		}
		lists[i] = old_lists[idx++];
	}

	D_ASSERTF(idx == orwo->orw_rels.ca_count, "idx %d, ca_count %zu\n",
		  idx, orwo->orw_rels.ca_count);
	D_FREE(old_lists);
	orwo->orw_rels.ca_arrays = lists;
	orwo->orw_rels.ca_count = orw->orw_nr;

	return rc;
}

static int
obj_local_rw_internal(crt_rpc_t *rpc, struct obj_io_context *ioc, daos_iod_t *iods,
		      struct dcs_iod_csums *iod_csums, uint64_t *offs, uint8_t *skips,
		      uint32_t iods_nr, struct dtx_handle *dth)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
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
	uint64_t			 cond_flags;
	uint64_t			 sched_seq = sched_cur_seq();
	daos_iod_t			*iods_dup = NULL; /* for EC deg fetch */
	bool				 get_parity_list = false;
	struct daos_recx_ep_list	*parity_list = NULL;
	uint64_t			time;
	uint64_t			bio_pre_latency = 0;
	uint64_t			bio_post_latency = 0;
	uint32_t			tgt_off = 0;
	int				rc = 0;

	create_map = orw->orw_flags & ORF_CREATE_MAP;
	if (daos_obj_is_echo(orw->orw_oid.id_pub) ||
	    (daos_io_bypass & IOBP_TARGET)) {
		obj_echo_rw(rpc, iods, offs);
		D_GOTO(out, rc = 0);
	}

	rc = csum_verify_keys(ioc->ioc_coc->sc_csummer, &orw->orw_dkey,
			      orw->orw_dkey_csum, &orw->orw_iod_array,
			      &orw->orw_oid);
	if (rc != 0) {
		D_ERROR(DF_C_UOID_DKEY "verify_keys error: " DF_RC "\n",
			DP_C_UOID_DKEY(orw->orw_oid, &orw->orw_dkey), DP_RC(rc));
		return rc;
	}

	dkey = (daos_key_t *)&orw->orw_dkey;
	D_DEBUG(DB_IO,
		"opc %d oid "DF_UOID" dkey "DF_KEY" tag %d epc "DF_X64" flags %x.\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid), DP_KEY(dkey),
		tag, orw->orw_epoch, orw->orw_flags);

	rma = (orw->orw_bulks.ca_arrays != NULL ||
	       orw->orw_bulks.ca_count != 0);
	cond_flags = orw->orw_api_flags;
	if (daos_oclass_is_ec(&ioc->ioc_oca))
		tgt_off = obj_ec_shard_off_by_layout_ver(ioc->ioc_layout_ver, orw->orw_dkey_hash,
							 &ioc->ioc_oca, orw->orw_oid.id_shard);
	/* Prepare IO descriptor */
	if (obj_rpc_is_update(rpc)) {
		obj_singv_ec_rw_filter(orw->orw_oid, &ioc->ioc_oca, tgt_off,
				       iods, offs, orw->orw_epoch, orw->orw_flags,
				       iods_nr, true, false, NULL);
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

		if (orw->orw_flags & ORF_EC)
			cond_flags |= VOS_OF_EC;

		rc = vos_update_begin(ioc->ioc_vos_coh, orw->orw_oid,
			      orw->orw_epoch, cond_flags, dkey,
			      iods_nr, iods, iod_csums,
			      ioc->ioc_coc->sc_props.dcp_dedup_size,
			      &ioh, dth);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
				DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}
	} else {
		uint32_t			fetch_flags = 0;
		bool				ec_deg_fetch;
		bool				ec_recov;
		bool				is_parity_shard;
		struct daos_recx_ep_list	*shadows = NULL;

		bulk_op = CRT_BULK_PUT;
		if (orw->orw_flags & ORF_CHECK_EXISTENCE)
			fetch_flags = VOS_OF_FETCH_CHECK_EXISTENCE;
		if (!rma && orw->orw_sgls.ca_arrays == NULL) {
			spec_fetch = true;
			if (!(orw->orw_flags & ORF_CHECK_EXISTENCE))
				fetch_flags = VOS_OF_FETCH_SIZE_ONLY;
		}

		ec_deg_fetch = orw->orw_flags & ORF_EC_DEGRADED;
		ec_recov = orw->orw_flags & ORF_EC_RECOV;
		D_ASSERTF(ec_recov == false || ec_deg_fetch == false,
			  "ec_recov %d, ec_deg_fetch %d.\n",
			  ec_recov, ec_deg_fetch);
		if (ec_recov) {
			D_ASSERT(obj_ec_tgt_nr(&ioc->ioc_oca) > 0);
			is_parity_shard = is_ec_parity_shard_by_tgt_off(tgt_off, &ioc->ioc_oca);
			get_parity_list = ec_recov && is_parity_shard &&
					  ((orw->orw_flags & ORF_EC_RECOV_SNAP) == 0);
		}
		if (get_parity_list) {
			D_ASSERT(!ec_deg_fetch);
			fetch_flags |= VOS_OF_FETCH_RECX_LIST;
		}
		if (unlikely(ec_recov &&
			     obj_ec_recov_need_try_again(orw, orwo, ioc))) {
			rc = -DER_FETCH_AGAIN;
			D_DEBUG(DB_IO,
				DF_UOID " " DF_X64 "<" DF_X64 " ec_recov needs redo, " DF_RC "\n",
				DP_UOID(orw->orw_oid), orw->orw_epoch,
				ioc->ioc_coc->sc_ec_agg_eph_boundary, DP_RC(rc));
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
			rc = daos_iod_recx_dup(iods, iods_nr, &iods_dup);
			if (rc != 0) {
				D_ERROR(DF_UOID ": iod_recx_dup failed: " DF_RC "\n",
					DP_UOID(orw->orw_oid), DP_RC(rc));
				goto out;
			}

			D_ASSERT(iods_dup != NULL);
			iods = iods_dup;

			rc = obj_fetch_shadow(ioc, orw->orw_oid, orw->orw_epoch, cond_flags, dkey,
					      orw->orw_dkey_hash, iods_nr, iods,
					      orw->orw_tgt_idx, dth, &shadows);
			if (rc) {
				D_ERROR(DF_UOID" Fetch shadow failed: "DF_RC
					"\n", DP_UOID(orw->orw_oid), DP_RC(rc));
				goto out;
			}
			iod_converted = true;

			if (orw->orw_flags & ORF_EC_RECOV_FROM_PARITY) {
				if (shadows == NULL) {
					rc = -DER_DATA_LOSS;
					D_ERROR(DF_UOID" ORF_EC_RECOV_FROM_PARITY should not with "
						"NULL shadows, "DF_RC"\n", DP_UOID(orw->orw_oid),
						DP_RC(rc));
					goto out;
				}
				fetch_flags |= VOS_OF_SKIP_FETCH;
			}
		}

		time = daos_get_ntime();
		rc = vos_fetch_begin(ioc->ioc_vos_coh, orw->orw_oid,
				     orw->orw_epoch, dkey, iods_nr, iods,
				     cond_flags | fetch_flags, shadows, &ioh, dth);
		daos_recx_ep_list_free(shadows, iods_nr);
		if (rc) {
			DL_CDEBUG(rc == -DER_INPROGRESS || rc == -DER_NONEXIST ||
				      rc == -DER_TX_RESTART,
				  DB_IO, DLOG_ERR, rc, "Fetch begin for " DF_UOID " failed",
				  DP_UOID(orw->orw_oid));
			goto out;
		}

		obj_update_latency(ioc->ioc_opc, VOS_LATENCY, daos_get_ntime() - time,
				   vos_get_io_size(ioh));

		if (get_parity_list) {
			parity_list = vos_ioh2recx_list(ioh);
			if (parity_list != NULL) {
				daos_recx_ep_list_set(parity_list, iods_nr,
						      ioc->ioc_coc->sc_ec_agg_eph_boundary, 0);
				daos_recx_ep_list_merge(parity_list, iods_nr);
				orwo->orw_rels.ca_arrays = parity_list;
				orwo->orw_rels.ca_count = iods_nr;
			}
		}

		rc = obj_set_reply_sizes(rpc, iods, iods_nr, skips);
		if (rc != 0)
			goto out;

		if (rma) {
			orwo->orw_sgls.ca_count = 0;
			orwo->orw_sgls.ca_arrays = NULL;

			rc = obj_set_reply_nrs(rpc, ioh, NULL, skips);
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

		rc = obj_singv_ec_rw_filter(orw->orw_oid, &ioc->ioc_oca, tgt_off,
					    iods, offs, orw->orw_epoch, orw->orw_flags,
					    iods_nr, false, ec_deg_fetch, &recov_lists);
		if (rc != 0) {
			D_ERROR(DF_UOID " obj_singv_ec_rw_filter failed: " DF_RC "\n",
				DP_UOID(orw->orw_oid), DP_RC(rc));
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
			vos_agg_epoch = ioc->ioc_coc->sc_ec_agg_eph_boundary;
			if (ioc->ioc_fetch_snap &&
			    orw->orw_epoch < vos_agg_epoch) {
				recov_epoch = orw->orw_epoch;
				recov_snap =  true;
			} else {
				recov_epoch = vos_agg_epoch;
			}
			daos_recx_ep_list_set(recov_lists, iods_nr,
					      recov_epoch, recov_snap);
			daos_recx_ep_list_merge(recov_lists, iods_nr);
			orwo->orw_rels.ca_arrays = recov_lists;
			orwo->orw_rels.ca_count = iods_nr;
		}
	}

	if (orw->orw_flags & ORF_CHECK_EXISTENCE)
		goto out;

	time = daos_get_ntime();
	biod = vos_ioh2desc(ioh);
	rc   = bio_iod_prep(biod, BIO_CHK_TYPE_IO, rma ? rpc->cr_ctx : NULL, CRT_BULK_RW);
	if (rc) {
		D_ERROR(DF_UOID " bio_iod_prep failed: " DF_RC "\n", DP_UOID(orw->orw_oid),
			DP_RC(rc));
		goto out;
	}

	if (obj_rpc_is_fetch(rpc) && !spec_fetch &&
	    daos_csummer_initialized(ioc->ioc_coc->sc_csummer)) {
		if (orw->orw_iod_array.oia_iods != iods) {
			/* Need to copy iod sizes for checksums */
			int i, j;

			for (i = 0, j = 0; i < orw->orw_iod_array.oia_iod_nr; i++) {
				if (skips != NULL && isset(skips, i)) {
					orw->orw_iod_array.oia_iods[i].iod_size = 0;
					continue;
				}
				orw->orw_iod_array.oia_iods[i].iod_size = iods[j].iod_size;
				j++;
			}
		}

		rc = obj_fetch_csum_init(ioc->ioc_coc, orw, orwo);
		if (rc) {
			D_ERROR(DF_UOID " fetch csum init failed: %d.\n", DP_UOID(orw->orw_oid),
				rc);
			goto post;
		}

		if (ioc->ioc_coc->sc_props.dcp_csum_enabled) {
			rc = csum_add2iods(ioh, orw->orw_iod_array.oia_iods,
					   orw->orw_iod_array.oia_iod_nr, skips,
					   ioc->ioc_coc->sc_csummer, orwo->orw_iod_csums.ca_arrays,
					   orw->orw_oid, &orw->orw_dkey);
			if (rc) {
				D_ERROR(DF_UOID" fetch verify failed: %d.\n",
					DP_UOID(orw->orw_oid), rc);
				goto post;
			}
		}
	}
	bio_pre_latency = daos_get_ntime() - time;

	if (obj_rpc_is_fetch(rpc) && DAOS_FAIL_CHECK(DAOS_OBJ_FAIL_NVME_IO)) {
		D_ERROR(DF_UOID " fetch failed: %d\n", DP_UOID(orw->orw_oid), -DER_NVME_IO);
		rc = -DER_NVME_IO;
		goto post;
	}

	if (rma) {
		bulk_bind = orw->orw_flags & ORF_BULK_BIND;
		rc = obj_bulk_transfer(rpc, bulk_op, bulk_bind, orw->orw_bulks.ca_arrays, offs,
				       skips, ioh, NULL, iods_nr, orw->orw_bulks.ca_count, NULL);
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
		rc = bio_iod_copy(biod, orw->orw_sgls.ca_arrays, iods_nr);
	}

	if (rc) {
		if (rc == -DER_OVERFLOW)
			rc = -DER_REC2BIG;

		DL_CDEBUG(rc == -DER_REC2BIG, DLOG_DBG, DLOG_ERR, rc,
			  DF_UOID " data transfer failed, dma %d", DP_UOID(orw->orw_oid), rma);
		D_GOTO(post, rc);
	}

	if (obj_rpc_is_update(rpc)) {
		rc = vos_dedup_verify(ioh);
		if (rc)
			goto post;

		rc = obj_verify_bio_csum(orw->orw_oid.id_pub, iods, iod_csums,
					 biod, ioc->ioc_coc->sc_csummer, iods_nr);
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
			obj_iod_recx_daos2vos(iods_nr, iods, &ioc->ioc_oca);

		rc = obj_fetch_create_maps(rpc, biod, iods, iods_nr, skips);
	}

	if (rc == -DER_CSUM)
		obj_log_csum_err();
post:
	time = daos_get_ntime();
	rc = bio_iod_post_async(biod, rc);
	bio_post_latency = daos_get_ntime() - time;
out:
	/* The DTX has been aborted during long time bulk data transfer. */
	if (unlikely(dth->dth_aborted))
		rc = -DER_CANCELED;

	/* There is CPU yield after DTX start, and the resent RPC may be handled during that.
	 * Let's check resent again before further process.
	 */
	if (rc == 0 && obj_rpc_is_update(rpc) && sched_cur_seq() != sched_seq) {
		if (dth->dth_need_validation) {
			daos_epoch_t	epoch = 0;
			int		rc1;

			rc1 = dtx_handle_resend(ioc->ioc_vos_coh, &orw->orw_dti, &epoch, NULL);
			switch (rc1) {
			case 0:
				orw->orw_epoch = epoch;
				/* Fall through */
			case -DER_ALREADY:
				rc = -DER_ALREADY;
				break;
			case -DER_NONEXIST:
			case -DER_EP_OLD:
				break;
			default:
				rc = rc1;
				break;
			}
		}

		/* For solo update, it will be handled via one-phase transaction.
		 * If there is CPU yield after its epoch generated, we will renew
		 * the epoch, then we can use the epoch to sort related solo DTXs
		 * based on their epochs.
		 */
		if (rc == 0 && dth->dth_solo) {
			struct dtx_epoch	epoch;

			epoch.oe_value = d_hlc_get();
			epoch.oe_first = orw->orw_epoch_first;
			epoch.oe_flags = orf_to_dtx_epoch_flags(orw->orw_flags);

			dtx_renew_epoch(&epoch, dth);
			vos_update_renew_epoch(ioh, dth);

			D_DEBUG(DB_IO,
				"update rpc %p renew epoch "DF_X64" => "DF_X64" for "DF_DTI"\n",
				rpc, orw->orw_epoch, dth->dth_epoch, DP_DTI(&orw->orw_dti));

			orw->orw_epoch = dth->dth_epoch;
		}
	}

	/* re-generate the recx_list if some akeys skipped */
	if (skips != NULL && orwo->orw_rels.ca_arrays != NULL && orw->orw_nr != iods_nr)
		rc = obj_rw_recx_list_post(orw, orwo, skips, rc);

	rc = obj_rw_complete(rpc, ioc, ioh, rc, dth);
	if (rc == 0) {
		/* Update latency after getting fetch/update IO size by obj_rw_complete */
		if (obj_rpc_is_update(rpc))
			obj_update_latency(ioc->ioc_opc, BIO_LATENCY, bio_post_latency,
					   ioc->ioc_io_size);
		else
			obj_update_latency(ioc->ioc_opc, BIO_LATENCY, bio_pre_latency,
					   ioc->ioc_io_size);
	}
	if (iods_dup != NULL)
		daos_iod_recx_free(iods_dup, iods_nr);
	return unlikely(rc == -DER_ALREADY) ? 0 : rc;
}

/* Extract local iods/offs/csums by orw_oid.id_shard from @orw */
/* local bitmap defined as uint64_t which includes 64 bits */
#define LOCAL_SKIP_BITS_NUM	(64)
static int
obj_get_iods_offs_by_oid(daos_unit_oid_t uoid, struct obj_iod_array *iod_array,
			 struct daos_oclass_attr *oca, uint64_t dkey_hash,
			 uint32_t layout_ver, daos_iod_t **iods, uint64_t **offs,
			 uint8_t **skips, struct dcs_iod_csums **csums, uint32_t *nr)
{
	struct obj_shard_iod	*siod;
	uint32_t		local_tgt;
	uint32_t		oiod_nr;
	uint32_t		tgt_off;
	int			i;
	int			idx = 0;
	int			rc = 0;

	oiod_nr = iod_array->oia_iod_nr;
	D_ASSERT(oiod_nr > 0);
	if (oiod_nr > 1 || *iods == NULL) {
		D_ALLOC_ARRAY(*iods, oiod_nr);
		if (*iods == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_ALLOC_ARRAY(*offs, oiod_nr);
		if (*offs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		if (csums != NULL) {
			D_ALLOC_ARRAY(*csums, oiod_nr);
			if (*csums == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}
	if (oiod_nr > LOCAL_SKIP_BITS_NUM || *skips == NULL) {
		D_ALLOC(*skips, (oiod_nr + NBBY - 1) / NBBY);
		if (*skips == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	local_tgt = uoid.id_shard % obj_ec_tgt_nr(oca);
	for (i = 0; i < oiod_nr; i++) {
		daos_iod_t		*iod_parent;
		struct dcs_iod_csums	*iod_pcsum = NULL;
		struct obj_io_desc	*oiod;
		bool			 skip;

		iod_parent = &iod_array->oia_iods[i];
		oiod = NULL;
		siod = NULL;
		skip = false;
		/* EC obj fetch request with NULL oia_oiods */
		if (iod_array->oia_oiods != NULL)
			oiod = &iod_array->oia_oiods[i];
		if (iod_parent->iod_type == DAOS_IOD_ARRAY) {
			if (oiod != NULL) {
				siod = obj_shard_iod_get(oiod, local_tgt);
				skip = (siod == NULL);
			} else {
				skip = (iod_parent->iod_nr == 0 || iod_parent->iod_recxs == NULL);
			}
			if (skip) {
				D_DEBUG(DB_IO, "akey[%d] "DF_KEY" array skipped.\n",
					i, DP_KEY(&iod_parent->iod_name));
				setbit(*skips, i);
				continue;
			}
		}

		memcpy(&(*iods)[idx], iod_parent, sizeof(daos_iod_t));
		if (csums != NULL) {
			iod_pcsum = &iod_array->oia_iod_csums[i];
			memcpy(&(*csums)[idx].ic_akey, &iod_pcsum->ic_akey,
			       sizeof(struct dcs_csum_info));
			(*csums)[idx].ic_nr = iod_pcsum->ic_nr;
		}

		if ((*iods)[idx].iod_type == DAOS_IOD_ARRAY) {
			if (oiod != NULL) {
				D_ASSERTF(siod != NULL, "local_tgt %u\n", local_tgt);
				(*offs)[idx] = siod->siod_off;
				(*iods)[idx].iod_recxs = &iod_parent->iod_recxs[siod->siod_idx];
				(*iods)[idx].iod_nr = siod->siod_nr;
				if (csums != NULL) {
					(*csums)[idx].ic_data = &iod_pcsum->ic_data[siod->siod_idx];
					(*csums)[idx].ic_nr = siod->siod_nr;
				}
			} else {
				/* iod_recxs/iod_nr/csums copied from iod_parent by above memcpy */
				if (iod_array->oia_offs)
					(*offs)[idx] = iod_array->oia_offs[i];
			}
		} else {
			tgt_off = obj_ec_shard_off_by_layout_ver(layout_ver, dkey_hash, oca,
								 local_tgt);
			/* Some cases need to skip this akey, for example update 2 akeys
			 * singv in one IO, first long singv distributed to all shards,
			 * second short singv only stored on one data shard and all parity
			 * shard, should skip the second update on some data shards.
			 */
			if (oiod_nr > 1 && tgt_off != OBJ_EC_SHORT_SINGV_IDX &&
			    is_ec_data_shard_by_tgt_off(tgt_off, oca) &&
			    (*iods)[idx].iod_size != DAOS_REC_ANY &&
			    (*iods)[idx].iod_size <=
			    OBJ_EC_SINGV_EVENDIST_SZ(obj_ec_data_tgt_nr(oca))) {
				D_DEBUG(DB_IO, "akey[%d] "DF_KEY" singv skipped, size %zu, "
					"tgt_off %d, data_tgt_nr %d.\n", i,
					DP_KEY(&iod_parent->iod_name), (*iods)[idx].iod_size,
					tgt_off, obj_ec_data_tgt_nr(oca));
				setbit(*skips, i);
				continue;
			}

			if (iod_parent->iod_recxs != NULL)
				(*iods)[idx].iod_recxs = &iod_parent->iod_recxs[0];
			else
				(*iods)[idx].iod_recxs = NULL;
			(*iods)[idx].iod_nr = 1;
			if (csums != NULL && iod_pcsum->ic_nr > 0) {
				struct dcs_csum_info	*ci, *split_ci;

				D_ASSERT(iod_pcsum->ic_nr == 1);
				ci = &iod_pcsum->ic_data[0];
				D_ALLOC_PTR((*csums)[idx].ic_data);
				if ((*csums)[idx].ic_data == NULL)
					D_GOTO(out, rc = -DER_NOMEM);

				split_ci = (*csums)[idx].ic_data;
				*split_ci = *ci;
				if (ci->cs_nr > 1) {
					/* evenly distributed singv */
					split_ci->cs_nr = 1;
					split_ci->cs_csum +=
					obj_ec_shard_off_by_layout_ver(layout_ver, dkey_hash,
								       oca, local_tgt) * ci->cs_len;
					split_ci->cs_buf_len = ci->cs_len;
				}
			}
		}
		idx++;
	}
	if (nr)
		*nr = idx;
out:
	return rc;
}

static int
obj_get_iods_offs(daos_unit_oid_t uoid, struct obj_iod_array *iod_array,
		  struct daos_oclass_attr *oca, uint64_t dkey_hash,
		  uint32_t layout_ver, daos_iod_t **iods,
		  uint64_t **offs, uint8_t **skips, struct dcs_iod_csums **p_csums,
		  struct dcs_csum_info *csum_info, uint32_t *nr)
{
	int rc;

	/* For EC object, possibly need to skip some akeys/iods by obj_get_iods_offs_by_oid().
	 * EC obj fetch request with NULL oia_oiods, need not skip handling if with only one akey.
	 */
	if (!daos_oclass_is_ec(oca) ||
	    (iod_array->oia_iod_nr < 2 && iod_array->oia_oiods == NULL)) {
		*iods = iod_array->oia_iods;
		*offs = iod_array->oia_offs;
		*skips = NULL;
		*p_csums = iod_array->oia_iod_csums;
		if (nr)
			*nr = iod_array->oia_iod_nr;
		return 0;
	}

	if (iod_array->oia_iod_csums != NULL)
		(*p_csums)->ic_data = csum_info;
	else
		*p_csums = NULL;

	rc = obj_get_iods_offs_by_oid(uoid, iod_array, oca, dkey_hash, layout_ver, iods, offs,
				      skips, iod_array->oia_iod_csums == NULL ? NULL : p_csums, nr);

	return rc;
}

static int
obj_local_rw_internal_wrap(crt_rpc_t *rpc, struct obj_io_context *ioc, struct dtx_handle *dth)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	daos_iod_t		iod = { 0 };
	daos_iod_t		*iods = &iod;
	struct dcs_iod_csums	csum = { 0 };
	struct dcs_csum_info	csum_info = { 0 };
	struct dcs_iod_csums	*csums = &csum;
	uint64_t		off = 0;
	uint64_t		*offs = &off;
	uint64_t		local_skips = 0;
	uint8_t			*skips = (uint8_t *)&local_skips;
	uint32_t		nr = 0;
	int			rc;

	rc = obj_get_iods_offs(orw->orw_oid, &orw->orw_iod_array, &ioc->ioc_oca,
			       orw->orw_dkey_hash, ioc->ioc_layout_ver, &iods,
			       &offs, &skips, &csums, &csum_info, &nr);
	if (rc == 0)
		rc = obj_local_rw_internal(rpc, ioc, iods, csums, offs, skips, nr, dth);

	if (csums != NULL && csums != &csum && csums != orw->orw_iod_array.oia_iod_csums) {
		int i;

		for (i = 0; i < nr; i++) {
			if (iods[i].iod_type == DAOS_IOD_SINGLE && csums[i].ic_data != NULL)
				D_FREE(csums[i].ic_data);
		}
		D_FREE(csums);
	}
	if (iods != NULL && iods != &iod && iods != orw->orw_iod_array.oia_iods)
		D_FREE(iods);
	if (offs != NULL && offs != &off && offs != orw->orw_iod_array.oia_offs)
		D_FREE(offs);
	if (skips != NULL && skips != (uint8_t *)&local_skips)
		D_FREE(skips);

	return rc;
}

static int
obj_local_rw(crt_rpc_t *rpc, struct obj_io_context *ioc, struct dtx_handle *dth)
{
	struct obj_rw_in        *orw = crt_req_get(rpc);
	struct dtx_share_peer	*dsp;
	uint32_t		 retry = 0;
	int			 rc;

again:
	rc = obj_local_rw_internal_wrap(rpc, ioc, dth);
	if (dth != NULL && obj_dtx_need_refresh(dth, rc)) {
		if (++retry < 3) {
			rc = dtx_refresh(dth, ioc->ioc_coc);
			if (rc == -DER_AGAIN)
				goto again;
		} else if (orw->orw_flags & ORF_MAYBE_STARVE) {
			dsp = d_list_entry(dth->dth_share_tbd_list.next, struct dtx_share_peer,
					   dsp_link);
			D_WARN(
			    "DTX refresh for " DF_DTI " because of " DF_DTI " (%d), maybe starve\n",
			    DP_DTI(&dth->dth_xid), DP_DTI(&dsp->dsp_xid), dth->dth_share_tbd_count);
		}
	}

	return rc;
}

static int
obj_capa_check(struct ds_cont_hdl *coh, bool is_write, bool is_agg_migrate)
{
	if (!is_agg_migrate && !is_write && !ds_sec_cont_can_read_data(coh->sch_sec_capas)) {
		D_ERROR("cont hdl "DF_UUID" sec_capas "DF_U64", "
			"NO_PERM to read.\n",
			DP_UUID(coh->sch_uuid), coh->sch_sec_capas);
		return -DER_NO_PERM;
	}

	if (!is_agg_migrate && is_write && !ds_sec_cont_can_write_data(coh->sch_sec_capas)) {
		D_ERROR("cont hdl "DF_UUID" sec_capas "DF_U64", "
			"NO_PERM to update.\n",
			DP_UUID(coh->sch_uuid), coh->sch_sec_capas);
		return -DER_NO_PERM;
	}

	if (!is_agg_migrate && coh->sch_cont && coh->sch_cont->sc_rw_disabled) {
		D_ERROR("cont hdl "DF_UUID" exceeds rf\n", DP_UUID(coh->sch_uuid));
		return -DER_RF;
	}

	if (is_write && coh->sch_cont &&
	    coh->sch_cont->sc_pool->spc_reint_mode == DAOS_REINT_MODE_NO_DATA_SYNC) {
		D_ERROR("pool "DF_UUID" no_data_sync reint mode,"
			" cont hdl "DF_UUID" NO_PERM to update.\n",
			DP_UUID(coh->sch_cont->sc_pool->spc_uuid), DP_UUID(coh->sch_uuid));
		return -DER_NO_PERM;
	}

	return 0;
}

/**
 * Lookup and return the container handle, if it is a rebuild handle, which
 * will never associate a particular container, then the container structure
 * will be returned to \a ioc::ioc_coc.
 */
static int
obj_ioc_init(uuid_t pool_uuid, uuid_t coh_uuid, uuid_t cont_uuid, crt_rpc_t *rpc,
	     struct obj_io_context *ioc)
{
	struct ds_cont_hdl   *coh;
	struct ds_cont_child *coc = NULL;
	int		      rc;

	D_ASSERT(ioc != NULL);
	memset(ioc, 0, sizeof(*ioc));

	crt_req_addref(rpc);
	ioc->ioc_rpc = rpc;
	ioc->ioc_opc = opc_get(rpc->cr_opc);
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

out:
	/* load csummer on demand for rebuild if not already loaded */
	rc = ds_cont_csummer_init(coc);
	if (rc)
		D_GOTO(failed, rc);
	D_ASSERT(coc->sc_pool != NULL);
	ioc->ioc_map_ver = coc->sc_pool->spc_map_version;
	ioc->ioc_vos_coh = coc->sc_hdl;
	ioc->ioc_coc	 = coc;
	ioc->ioc_coh	 = coh;
	ioc->ioc_layout_ver = coc->sc_props.dcp_obj_version;
	return 0;
failed:
	if (coc != NULL)
		ds_cont_child_put(coc);
	ds_cont_hdl_put(coh);
	return rc;
}

static void
obj_ioc_fini(struct obj_io_context *ioc, int err)
{
	if (ioc->ioc_coh != NULL) {
		ds_cont_hdl_put(ioc->ioc_coh);
		ioc->ioc_coh = NULL;
	}

	if (ioc->ioc_coc != NULL) {
		if (ioc->ioc_update_ec_ts && err == 0)
			ds_cont_ec_timestamp_update(ioc->ioc_coc);
		ds_cont_child_put(ioc->ioc_coc);
		ioc->ioc_coc = NULL;
	}
	if (ioc->ioc_rpc) {
		crt_req_decref(ioc->ioc_rpc);
		ioc->ioc_rpc = NULL;
	}
}

/* Setup lite IO context, it is only for compound RPC so far:
 * - no associated object yet
 * - no permission check (not sure it's read/write)
 */
static int
obj_ioc_begin_lite(uint32_t rpc_map_ver, uuid_t pool_uuid,
		   uuid_t coh_uuid, uuid_t cont_uuid,
		   crt_rpc_t *rpc, struct obj_io_context *ioc)
{
	struct obj_tls		*tls;
	struct ds_pool_child	*poc;
	int			rc;

	rc = obj_ioc_init(pool_uuid, coh_uuid, cont_uuid, rpc, ioc);
	if (rc) {
		DL_ERROR(rc, "Failed to initialize object I/O context.");

		/*
		 * Client with stale pool map may try to send RPC to a DOWN target, if the
		 * target was brought DOWN due to faulty NVMe device, the ds_pool_child could
		 * have been stopped on the NVMe faulty reaction, then above obj_io_init()
		 * will fail with -DER_NO_HDL.
		 *
		 * We'd ensure proper error code is returned for such case.
		 */
		poc = ds_pool_child_find(pool_uuid);
		if (poc == NULL) {
			D_ERROR("Failed to find pool:"DF_UUID"\n", DP_UUID(pool_uuid));
			return rc;
		}

		if (rpc_map_ver < poc->spc_pool->sp_map_version) {
			D_ERROR("Stale pool map version %u < %u from client.\n",
				rpc_map_ver, poc->spc_pool->sp_map_version);

			/* Restart the DTX if using stale pool map */
			if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_CPD)
				rc = -DER_TX_RESTART;
			else
				rc = -DER_STALE;
		}

		ds_pool_child_put(poc);
		return rc;
	}

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
		 *	   In the subsequent modification, it may hit
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
		if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_CPD)
			D_GOTO(out, rc = -DER_TX_RESTART);

		D_GOTO(out, rc = -DER_STALE);
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_STALE_PM)) {
		D_GOTO(out, rc = -DER_STALE);
	}

out:
	dss_rpc_cntr_enter(DSS_RC_OBJ);
	/** increment active request counter and start the chrono */
	tls = obj_tls_get();
	d_tm_inc_gauge(tls->ot_op_active[opc_get(rpc->cr_opc)], 1);
	ioc->ioc_start_time = daos_get_ntime();
	ioc->ioc_began = 1;
	return rc;
}

static inline void
obj_update_sensors(struct obj_io_context *ioc, int err)
{
	struct obj_tls		*tls = obj_tls_get();
	struct obj_pool_metrics	*opm;
	struct obj_rw_in	*orw;
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
		d_tm_inc_counter(opm->opm_update_bytes, ioc->ioc_io_size);
		lat = tls->ot_update_lat[lat_bucket(ioc->ioc_io_size)];
		orw = crt_req_get(ioc->ioc_rpc);
		if (orw->orw_iod_array.oia_iods != NULL)
			obj_ec_metrics_process(&orw->orw_iod_array, ioc);

		break;
	case DAOS_OBJ_RPC_TGT_UPDATE:
		d_tm_inc_counter(opm->opm_update_bytes, ioc->ioc_io_size);
		lat = tls->ot_tgt_update_lat[lat_bucket(ioc->ioc_io_size)];
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
	obj_ioc_fini(ioc, err);
}

static int
obj_ioc_init_oca(struct obj_io_context *ioc, daos_obj_id_t oid, bool for_modify)
{
	struct daos_oclass_attr *oca;
	uint32_t                 nr_grps;

	oca = daos_oclass_attr_find(oid, &nr_grps);
	if (!oca)
		return -DER_INVAL;

	ioc->ioc_oca = *oca;
	ioc->ioc_oca.ca_grp_nr = nr_grps;
	D_ASSERT(ioc->ioc_coc != NULL);
	if (daos_oclass_is_ec(oca)) {
		ioc->ioc_oca.u.ec.e_len = ioc->ioc_coc->sc_props.dcp_ec_cell_sz;
		D_ASSERT(ioc->ioc_oca.u.ec.e_len != 0);
		if (for_modify)
			ioc->ioc_update_ec_ts = 1;
	}

	return 0;
}

static int
obj_inflight_io_check(struct ds_cont_child *child, uint32_t opc,
		      uint32_t rpc_map_ver, uint32_t flags)
{
	if (opc == DAOS_OBJ_RPC_ENUMERATE && flags & ORF_FOR_MIGRATION) {
		if (child->sc_ec_agg_active) {
			D_ERROR(DF_CONT" ec aggregate still active, rebuilding %d\n",
				DP_CONT(child->sc_pool->spc_uuid, child->sc_uuid),
				child->sc_pool->spc_pool->sp_rebuilding);
			return -DER_UPDATE_AGAIN;
		}
	}

	if (!obj_is_modification_opc(opc) && (opc != DAOS_OBJ_RPC_CPD || flags & ORF_CPD_RDONLY))
		return 0;

	if (child->sc_pool->spc_pool->sp_rebuilding) {
		uint32_t version;

		ds_rebuild_running_query(child->sc_pool_uuid, RB_OP_REBUILD,
					 &version, NULL, NULL);
		if (version != 0 && version < rpc_map_ver) {
			D_DEBUG(DB_IO, DF_UUID" retry rpc ver %u > rebuilding %u\n",
				DP_UUID(child->sc_pool_uuid), rpc_map_ver,
			        version);
			return -DER_UPDATE_AGAIN;
		}
	}

	/* If the incoming I/O is during integration, then it needs to wait the
	 * vos discard to finish, which otherwise might discard these new in-flight
	 * I/O update.
	 */
	if ((flags & ORF_REINTEGRATING_IO) &&
	    (child->sc_pool->spc_pool->sp_need_discard &&
	     child->sc_pool->spc_discard_done == 0)) {
		D_ERROR("reintegrating "DF_UUID" retry.\n", DP_UUID(child->sc_pool->spc_uuid));
		return -DER_UPDATE_AGAIN;
	}

	/* All I/O during rebuilding, needs to wait for the rebuild fence to
	 * be generated (see rebuild_prepare_one()), which will create a boundary
	 * for rebuild, so the data after boundary(epoch) should not be rebuilt,
	 * which otherwise might be written duplicately, which might cause
	 * the failure in VOS.
	 */
	if ((flags & ORF_REBUILDING_IO) &&
	    (!child->sc_pool->spc_pool->sp_disable_rebuild &&
	      child->sc_pool->spc_rebuild_fence == 0)) {
		D_ERROR("rebuilding "DF_UUID" retry.\n", DP_UUID(child->sc_pool->spc_uuid));
		return -DER_UPDATE_AGAIN;
	}

	return 0;
}

/* Various check before access VOS */
static int
obj_ioc_begin(daos_obj_id_t oid, uint32_t rpc_map_ver, uuid_t pool_uuid,
	      uuid_t coh_uuid, uuid_t cont_uuid, crt_rpc_t *rpc, uint32_t flags,
	      struct obj_io_context *ioc)
{
	uint32_t	opc = opc_get(rpc->cr_opc);
	int		rc;

	rc = obj_ioc_begin_lite(rpc_map_ver, pool_uuid, coh_uuid, cont_uuid,
				rpc, ioc);
	if (rc != 0)
		return rc;

	rc = obj_capa_check(ioc->ioc_coh, obj_is_modification_opc(opc),
			    obj_is_ec_agg_opc(opc) ||
			    (flags & ORF_FOR_MIGRATION) ||
			    (flags & ORF_FOR_EC_AGG));
	if (rc != 0)
		goto failed;

	rc = obj_inflight_io_check(ioc->ioc_coc, opc, rpc_map_ver, flags);
	if (rc != 0)
		goto failed;

	rc = obj_ioc_init_oca(ioc, oid, obj_is_modification_opc(opc));
	if (rc != 0)
		goto failed;
	return 0;
failed:
	obj_ioc_end(ioc, rc);
	return rc;
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
	struct obj_io_context	 ioc;
	daos_handle_t		 ioh = DAOS_HDL_INVAL;
	int			 rc;

	D_ASSERT(oer != NULL);
	D_ASSERT(oero != NULL);

	rc = obj_ioc_begin(oer->er_oid.id_pub, oer->er_map_ver,
			   oer->er_pool_uuid, oer->er_coh_uuid,
			   oer->er_cont_uuid, rpc, 0, &ioc);
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
	if (iod->iod_nr == 0) /* nothing to replicate, directly remove parity */
		goto remove_parity;
	iod_csums = oer->er_iod_csums.ca_arrays;
	rc = vos_update_begin(ioc.ioc_coc->sc_hdl, oer->er_oid, oer->er_epoch_range.epr_hi,
			      VOS_OF_REBUILD, dkey, 1, iod, iod_csums, 0, &ioh, NULL);
	if (rc) {
		D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
			DP_UOID(oer->er_oid), DP_RC(rc));
		goto out;
	}
	biod = vos_ioh2desc(ioh);
	rc = bio_iod_prep(biod, BIO_CHK_TYPE_IO, rpc->cr_ctx, CRT_BULK_RW);
	if (rc) {
		D_ERROR(DF_UOID " bio_iod_prep failed: " DF_RC "\n", DP_UOID(oer->er_oid),
			DP_RC(rc));
		goto end;
	}
	rc = obj_bulk_transfer(rpc, CRT_BULK_GET, false, &oer->er_bulk, NULL, NULL, ioh, NULL, 1, 1,
			       NULL);
	if (rc)
		D_ERROR(DF_UOID " bulk transfer failed: " DF_RC "\n", DP_UOID(oer->er_oid),
			DP_RC(rc));

	rc = bio_iod_post(biod, rc);
	if (rc)
		D_ERROR(DF_UOID " bio_iod_post failed: " DF_RC "\n", DP_UOID(oer->er_oid),
			DP_RC(rc));
end:
	rc = vos_update_end(ioh, ioc.ioc_map_ver, dkey, rc, &ioc.ioc_io_size, NULL);
	if (rc) {
		D_ERROR(DF_UOID " vos_update_end failed: " DF_RC "\n", DP_UOID(oer->er_oid),
			DP_RC(rc));
		goto out;
	}
remove_parity:
	recx.rx_nr = obj_ioc2ec_cs(&ioc);
	recx.rx_idx = (oer->er_stripenum * recx.rx_nr) | PARITY_INDICATOR;
	rc = vos_obj_array_remove(ioc.ioc_coc->sc_hdl, oer->er_oid, &oer->er_epoch_range, dkey,
				  &iod->iod_name, &recx);
out:
	obj_rw_reply(rpc, rc, 0, false, &ioc);
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
			   oea->ea_cont_uuid, rpc, 0, &ioc);

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
		rc = vos_update_begin(ioc.ioc_coc->sc_hdl, oea->ea_oid, oea->ea_epoch_range.epr_hi,
				      VOS_OF_REBUILD, dkey, 1, iod, iod_csums, 0, &ioh, NULL);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
				DP_UOID(oea->ea_oid), DP_RC(rc));
			goto out;
		}
		biod = vos_ioh2desc(ioh);
		rc = bio_iod_prep(biod, BIO_CHK_TYPE_IO, rpc->cr_ctx,
				  CRT_BULK_RW);
		if (rc) {
			D_ERROR(DF_UOID " bio_iod_prep failed: " DF_RC "\n", DP_UOID(oea->ea_oid),
				DP_RC(rc));
			goto end;
		}
		rc = obj_bulk_transfer(rpc, CRT_BULK_GET, false, &oea->ea_bulk, NULL, NULL, ioh,
				       NULL, 1, 1, NULL);
		if (rc)
			D_ERROR(DF_UOID " bulk transfer failed: " DF_RC "\n", DP_UOID(oea->ea_oid),
				DP_RC(rc));

		rc = bio_iod_post(biod, rc);
		if (rc)
			D_ERROR(DF_UOID " bio_iod_post failed: " DF_RC "\n", DP_UOID(oea->ea_oid),
				DP_RC(rc));
end:
		rc = vos_update_end(ioh, ioc.ioc_map_ver, dkey, rc, &ioc.ioc_io_size, NULL);
		if (rc) {
			if (rc == -DER_NO_PERM) {
				/* Parity already exists, May need a
				 * different error code.
				 */
				D_DEBUG(DB_EPC, DF_UOID " parity already exists\n",
					DP_UOID(oea->ea_oid));
				rc = 0;
			} else {
				D_ERROR(DF_UOID " vos_update_end failed: " DF_RC "\n",
					DP_UOID(oea->ea_oid), DP_RC(rc));
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
		D_ERROR(DF_UOID ": array_remove failed: " DF_RC "\n", DP_UOID(oea->ea_oid),
			DP_RC(rc1));
out:
	obj_rw_reply(rpc, rc, 0, false, &ioc);
	obj_ioc_end(&ioc, rc);
}

void
ds_obj_tgt_update_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	daos_key_t			*dkey = &orw->orw_dkey;
	struct obj_io_context		 ioc;
	struct dtx_handle               *dth = NULL;
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
			   orw->orw_co_uuid, rpc, orw->orw_flags, &ioc);
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
		daos_epoch_t	e = orw->orw_epoch;

		rc = dtx_handle_resend(ioc.ioc_vos_coh, &orw->orw_dti, &e, NULL);
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
			rc = vos_dtx_abort(ioc.ioc_vos_coh, &orw->orw_dti, e);

		if (rc < 0 && rc != -DER_NONEXIST)
			D_GOTO(out, rc);
	}

	/* Inject failure for test to simulate the case of lost some
	 * record/akey/dkey on some non-leader.
	 */
	if (DAOS_FAIL_CHECK(DAOS_VC_LOST_DATA)) {
		if (orw->orw_dti_cos.ca_count > 0) {
			rc = vos_dtx_commit(ioc.ioc_vos_coh,
					    orw->orw_dti_cos.ca_arrays,
					    orw->orw_dti_cos.ca_count, false, NULL);
			if (rc < 0) {
				D_WARN(DF_UOID ": Failed to DTX CoS commit " DF_RC "\n",
				       DP_UOID(orw->orw_oid), DP_RC(rc));
			} else if (rc < orw->orw_dti_cos.ca_count) {
				D_WARN(DF_UOID": Incomplete DTX CoS commit rc = %d expected "
				       "%" PRIu64 ".\n", DP_UOID(orw->orw_oid),
				       rc, orw->orw_dti_cos.ca_count);
			}
		}
		D_GOTO(out, rc = 0);
	}

	tgts = orw->orw_shard_tgts.ca_arrays;
	tgt_cnt = orw->orw_shard_tgts.ca_count;

	rc = obj_gen_dtx_mbs(orw->orw_flags, &tgt_cnt, &tgts, &mbs);
	if (rc != 0)
		D_GOTO(out, rc);

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
		D_ERROR(DF_UOID ": Failed to start DTX for update " DF_RC "\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		D_GOTO(out, rc = -DER_IO);

	/* RPC may be resent during current update bulk data transfer.
	 * Pre-allocate DTX entry for handling resend under such case.
	 */
	rc = obj_local_rw(rpc, &ioc, dth);
	if (rc != 0)
		DL_CDEBUG(
		    rc == -DER_INPROGRESS || rc == -DER_TX_RESTART ||
			(rc == -DER_EXIST &&
			 (orw->orw_api_flags & (DAOS_COND_DKEY_INSERT | DAOS_COND_AKEY_INSERT))) ||
			(rc == -DER_NONEXIST &&
			 (orw->orw_api_flags & (DAOS_COND_DKEY_UPDATE | DAOS_COND_AKEY_UPDATE))),
		    DB_IO, DLOG_ERR, rc, DF_UOID, DP_UOID(orw->orw_oid));

out:
	if (dth != NULL)
		rc = dtx_end(dth, ioc.ioc_coc, rc);
	obj_rw_reply(rpc, rc, 0, true, &ioc);
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
		int			 rc = 0;

		if (DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
			D_GOTO(comp, rc = -DER_IO);

		/* No need re-exec local update */
		if (dlh->dlh_handle.dth_prepared)
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
		rc = obj_local_rw(exec_arg->rpc, exec_arg->ioc, &dlh->dlh_handle);
		if (rc != 0)
			DL_CDEBUG(rc == -DER_INPROGRESS || rc == -DER_TX_RESTART ||
				      (rc == -DER_EXIST &&
				       (orw->orw_api_flags &
					(DAOS_COND_DKEY_INSERT | DAOS_COND_AKEY_INSERT))) ||
				      (rc == -DER_NONEXIST &&
				       (orw->orw_api_flags &
					(DAOS_COND_DKEY_UPDATE | DAOS_COND_AKEY_UPDATE))),
				  DB_IO, DLOG_ERR, rc, DF_UOID, DP_UOID(orw->orw_oid));

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
		*epoch = d_hlc_get();
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
	struct dtx_leader_handle	*dlh = NULL;
	struct ds_obj_exec_arg		exec_arg = { 0 };
	struct obj_io_context		ioc = { 0 };
	uint32_t			flags = 0;
	uint32_t			dtx_flags = 0;
	uint32_t			opc = opc_get(rpc->cr_opc);
	struct dtx_memberships		*mbs = NULL;
	struct daos_shard_tgt		*tgts = NULL;
	struct dtx_id			*dti_cos = NULL;
	struct obj_pool_metrics		*opm;
	int				dti_cos_cnt;
	uint32_t			tgt_cnt;
	uint32_t			version = 0;
	uint32_t			max_ver = 0;
	struct dtx_epoch		epoch = {0};
	int				rc;
	bool				need_abort = false;

	D_ASSERT(orw != NULL);
	D_ASSERT(orwo != NULL);

	rc = obj_ioc_begin(orw->orw_oid.id_pub, orw->orw_map_ver,
			   orw->orw_pool_uuid, orw->orw_co_hdl,
			   orw->orw_co_uuid, rpc, orw->orw_flags, &ioc);
	if (rc != 0) {
		D_ASSERTF(rc < 0, "unexpected error# "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	D_DEBUG(DB_IO,
		"rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_X64", pmv %u/%u dti "DF_DTI" layout %u.\n",
		rpc, opc, DP_UOID(orw->orw_oid), DP_KEY(&orw->orw_dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id, orw->orw_epoch,
		orw->orw_map_ver, ioc.ioc_map_ver, DP_DTI(&orw->orw_dti), ioc.ioc_layout_ver);

	if (obj_rpc_is_fetch(rpc) && !(orw->orw_flags & ORF_EC_RECOV) &&
	    (orw->orw_epoch != 0 && orw->orw_epoch != DAOS_EPOCH_MAX))
		ioc.ioc_fetch_snap = 1;

	rc = process_epoch(&orw->orw_epoch, &orw->orw_epoch_first,
			   &orw->orw_flags);
	if (rc == PE_OK_LOCAL) {
		orw->orw_flags &= ~ORF_EPOCH_UNCERTAIN;
		dtx_flags |= DTX_EPOCH_OWNER;
	}

	if (obj_rpc_is_fetch(rpc)) {
		struct dtx_handle	*dth;

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

		rc = dtx_begin(ioc.ioc_vos_coh, &orw->orw_dti, &epoch, 0, orw->orw_map_ver,
			       &orw->orw_oid, NULL, 0, dtx_flags, NULL, &dth);
		if (rc == 0) {
			rc = obj_local_rw(rpc, &ioc, dth);
			rc = dtx_end(dth, ioc.ioc_coc, rc);
		}

		D_GOTO(out, rc);
	}

	tgts = orw->orw_shard_tgts.ca_arrays;
	tgt_cnt = orw->orw_shard_tgts.ca_count;

	rc = obj_gen_dtx_mbs(orw->orw_flags, &tgt_cnt, &tgts, &mbs);
	if (rc != 0)
		D_GOTO(out, rc);

	version = orw->orw_map_ver;
	max_ver = orw->orw_map_ver;

	if (tgt_cnt == 0) {
		if (!(orw->orw_api_flags & DAOS_COND_MASK))
			dtx_flags |= DTX_DROP_CMT;
		dtx_flags |= DTX_SOLO;
	}

	if (orw->orw_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	opm = ioc.ioc_coc->sc_pool->spc_metrics[DAOS_OBJ_MODULE];

	/* Handle resend. */
	if (orw->orw_flags & ORF_RESEND) {
		daos_epoch_t		 e;

		d_tm_inc_counter(opm->opm_update_resent, 1);

again:
		if (flags & ORF_RESEND)
			e = orw->orw_epoch;
		else
			e = 0;
		version = orw->orw_map_ver;
		rc      = dtx_handle_resend(ioc.ioc_vos_coh, &orw->orw_dti, &e, &version);
		switch (rc) {
		case -DER_ALREADY:
			D_GOTO(out, rc = 0);
		case 0:
			flags |= ORF_RESEND;
			orw->orw_epoch = e;
			/* TODO: Also recover the epoch uncertainty. */
			break;
		case -DER_MISMATCH:
			rc = vos_dtx_abort(ioc.ioc_vos_coh, &orw->orw_dti, e);
			if (rc < 0 && rc != -DER_NONEXIST)
				D_GOTO(out, rc);
			/* Fall through */
		case -DER_NONEXIST:
			flags = 0;
			break;
		default:
			D_GOTO(out, rc);
		}
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST)) {
		ioc.ioc_lost_reply = 1;
		D_GOTO(out, rc);
	}

	/* For leader case, we need to find out the potential conflict
	 * (or share the same non-committed object/dkey) DTX(s) in the
	 * CoS (committable) cache, piggyback them via the dispdatched
	 * RPC to non-leaders. Then the non-leader replicas can commit
	 * them before real modifications to avoid availability issues.
	 */
	D_FREE(dti_cos);
	dti_cos_cnt = dtx_cos_get_piggyback(ioc.ioc_coc, &orw->orw_oid, orw->orw_dkey_hash,
					    DTX_THRESHOLD_COUNT, &dti_cos);
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
			      tgts, tgt_cnt, dtx_flags, mbs, NULL /* dce */, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID ": Failed to start DTX for update " DF_RC "\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.flags |= flags;
	exec_arg.start = orw->orw_start_shard;

	/* Execute the operation on all targets */
	rc = dtx_leader_exec_ops(dlh, obj_tgt_update, NULL, 0, &exec_arg);

	if (max_ver < dlh->dlh_rmt_ver)
		max_ver = dlh->dlh_rmt_ver;

	/* Stop the distributed transaction */
	rc = dtx_leader_end(dlh, ioc.ioc_coc, rc);
	switch (rc) {
	case -DER_TX_RESTART:
		/*
		 * If this is a standalone operation, we can restart the
		 * internal transaction right here. Otherwise we have to
		 * defer the restart to the RPC sponsor.
		 */
		if (opc != DAOS_OBJ_RPC_UPDATE)
			break;

		/* Only standalone updates use this RPC. Retry with newer epoch. */
		orw->orw_epoch = d_hlc_get();
		exec_arg.flags |= ORF_RESEND;
		flags = ORF_RESEND;
		d_tm_inc_counter(opm->opm_update_restart, 1);
		goto again;
	case -DER_AGAIN:
		need_abort = true;
		exec_arg.flags |= ORF_RESEND;
		flags = ORF_RESEND;
		d_tm_inc_counter(opm->opm_update_retry, 1);
		ABT_thread_yield();
		goto again;
	default:
		break;
	}

	if (opc == DAOS_OBJ_RPC_UPDATE && !(orw->orw_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		ioc.ioc_lost_reply = 1;
out:
	if (unlikely(rc != 0 && need_abort)) {
		struct dtx_entry	 dte;
		int			 rc1;

		dte.dte_xid = orw->orw_dti;
		dte.dte_ver = version;
		dte.dte_refs = 1;
		dte.dte_mbs = mbs;
		rc1 = dtx_abort(ioc.ioc_coc, &dte, orw->orw_epoch);
		if (rc1 != 0 && rc1 != -DER_NONEXIST)
			D_WARN("Failed to abort DTX "DF_DTI": "DF_RC"\n",
			       DP_DTI(&orw->orw_dti), DP_RC(rc1));
	}

	if (ioc.ioc_map_ver < max_ver)
		ioc.ioc_map_ver = max_ver;

	obj_rw_reply(rpc, rc, epoch.oe_value, false, &ioc);
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
obj_local_enum(struct obj_io_context *ioc, crt_rpc_t *rpc,
	       struct vos_iter_anchors *anchors, struct ds_obj_enum_arg *enum_arg,
	       daos_epoch_t *e_out)
{
	vos_iter_param_t	param = { 0 };
	struct obj_key_enum_in	*oei = crt_req_get(rpc);
	struct dtx_handle	*dth = NULL;
	uint32_t		flags = 0;
	int			opc = opc_get(rpc->cr_opc);
	int			type;
	int			rc;
	int			rc_tmp;
	bool			recursive = false;
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
		if (oei->oei_flags & ORF_DESCENDING_ORDER)
			param.ip_flags |= VOS_IT_RECX_REVERSE;
		enum_arg->fill_recxs = true;
	} else if (opc == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		type = VOS_ITER_DKEY;
	} else if (opc == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		type = VOS_ITER_AKEY;
	} else {
		/* object iteration for rebuild or consistency verification. */
		D_ASSERT(opc == DAOS_OBJ_RPC_ENUMERATE);
		type = VOS_ITER_DKEY;
		param.ip_flags |= VOS_IT_RECX_VISIBLE;
		if (daos_anchor_get_flags(&anchors->ia_dkey) &
		      DIOF_WITH_SPEC_EPOCH) {
			/* For obj verification case. */
			param.ip_epc_expr = VOS_IT_EPC_RR;
		} else {
			param.ip_epc_expr = VOS_IT_EPC_RE;
		}
		recursive = true;

		if (oei->oei_flags & ORF_DESCENDING_ORDER)
			param.ip_flags |= VOS_IT_RECX_REVERSE;

		if (daos_oclass_is_ec(&ioc->ioc_oca))
			enum_arg->ec_cell_sz = ioc->ioc_oca.u.ec.e_len;
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

	if (oei->oei_flags & ORF_FOR_MIGRATION)
		flags = DTX_FOR_MIGRATION;

	rc = dtx_begin(ioc->ioc_vos_coh, &oei->oei_dti, &epoch, 0,
		       oei->oei_map_ver, &oei->oei_oid, NULL, 0, flags,
		       NULL, &dth);
	if (rc != 0)
		goto failed;

re_pack:
	rc = ds_obj_enum_pack(&param, type, recursive, anchors, enum_arg, vos_iterate, dth);
	if (obj_dtx_need_refresh(dth, rc)) {
		rc = dtx_refresh(dth, ioc->ioc_coc);
		/* After DTX refresh, re_pack will resume from the position at \@anchors. */
		if (rc == -DER_AGAIN)
			goto re_pack;
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
		fill_oid(oei->oei_oid, enum_arg);
		goto re_pack;
	} else if (enum_arg->size_query) {
		D_DEBUG(DB_IO, DF_UOID "query size by kds %d total %zd\n",
			DP_UOID(oei->oei_oid), enum_arg->kds_len, enum_arg->kds[0].kd_key_len);
		rc = -DER_KEY2BIG;
	}

	/* ds_obj_enum_pack may return 1. */
	rc_tmp = dtx_end(dth, ioc->ioc_coc, rc > 0 ? 0 : rc);
	if (rc_tmp != 0)
		rc = rc_tmp;

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

	rc = obj_bulk_transfer(rpc, CRT_BULK_PUT, false, bulks, NULL, NULL, DAOS_HDL_INVAL, sgls,
			       idx, idx, NULL);
	if (oei->oei_kds_bulk) {
		D_FREE(oeo->oeo_kds.ca_arrays);
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
	struct ds_obj_enum_arg	enum_arg = { 0 };
	struct vos_iter_anchors	*anchors = NULL;
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
			   oei->oei_co_uuid, rpc, oei->oei_flags, &ioc);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_IO, "rpc %p opc %d oid "DF_UOID" tag/xs %d/%d pmv %u/%u\n",
		rpc, opc, DP_UOID(oei->oei_oid),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id,
		oei->oei_map_ver, ioc.ioc_map_ver);

	D_ALLOC_PTR(anchors);
	if (anchors == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	anchors->ia_dkey = oei->oei_dkey_anchor;
	anchors->ia_akey = oei->oei_akey_anchor;
	anchors->ia_ev = oei->oei_anchor;

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
	rc = obj_local_enum(&ioc, rpc, anchors, &enum_arg, &epoch);
	if (rc == 1) /* If the buffer is full, exit and reset failure. */
		rc = 0;

	if (rc)
		D_GOTO(out, rc);

	oeo->oeo_dkey_anchor = anchors->ia_dkey;
	oeo->oeo_akey_anchor = anchors->ia_akey;
	oeo->oeo_anchor = anchors->ia_ev;

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
		if (oeo->oeo_sgl.sg_iovs != NULL)
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
	D_FREE(anchors);
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
obj_punch_one(struct obj_punch_in *opi, crt_opcode_t opc,
	      struct obj_io_context *ioc, struct dtx_handle *dth)
{
	struct ds_cont_child	*cont = ioc->ioc_coc;
	int			 rc;

	rc = dtx_sub_init(dth, &opi->opi_oid, opi->opi_dkey_hash);
	if (rc != 0)
		goto out;

	switch (opc) {
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_COLL_PUNCH:
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

out:
	return rc;
}

static int
obj_local_punch(struct obj_punch_in *opi, crt_opcode_t opc, uint32_t shard_nr, uint32_t *shards,
		struct obj_io_context *ioc, struct dtx_handle *dth)
{
	struct dtx_share_peer	*dsp;
	uint64_t		 sched_seq;
	uint32_t		 retry = 0;
	int			 rc = 0;
	int			 i;

again:
	sched_seq = sched_cur_seq();

	/* There may be multiple shards reside on the same VOS target. */
	for (i = 0; i < shard_nr; i++) {
		opi->opi_oid.id_shard = shards[i];
		rc = obj_punch_one(opi, opc, ioc, dth);
		if (rc != 0)
			break;
	}

	if (obj_dtx_need_refresh(dth, rc)) {
		if (++retry >= 3) {
			if (opi->opi_flags & ORF_MAYBE_STARVE) {
				dsp = d_list_entry(dth->dth_share_tbd_list.next,
						   struct dtx_share_peer, dsp_link);
				D_WARN("DTX refresh for " DF_DTI " because of " DF_DTI
				       " (%d), maybe starve\n",
				       DP_DTI(&dth->dth_xid), DP_DTI(&dsp->dsp_xid),
				       dth->dth_share_tbd_count);
			}
			goto out;
		}

		rc = dtx_refresh(dth, ioc->ioc_coc);
		if (rc != -DER_AGAIN)
			goto out;

		if (unlikely(sched_cur_seq() == sched_seq))
			goto again;

		/*
		 * There is CPU yield after DTX start, and the resent RPC may be handled
		 * during that. Let's check resent again before further process.
		 */

		if (dth->dth_need_validation) {
			daos_epoch_t	epoch = 0;
			int		rc1;

			rc1 = dtx_handle_resend(ioc->ioc_vos_coh, &opi->opi_dti, &epoch, NULL);
			switch (rc1) {
			case 0:
				opi->opi_epoch = epoch;
				/* Fall through */
			case -DER_ALREADY:
				rc = -DER_ALREADY;
				break;
			case -DER_NONEXIST:
			case -DER_EP_OLD:
				break;
			default:
				rc = rc1;
				break;
			}
		}

		/*
		 * For solo punch, it will be handled via one-phase transaction. If there is CPU
		 * yield after its epoch generated, we will renew the epoch, then we can use the
		 * epoch to sort related solo DTXs based on their epochs.
		 */
		if (rc == -DER_AGAIN && dth->dth_solo) {
			struct dtx_epoch	epoch;

			epoch.oe_value = d_hlc_get();
			epoch.oe_first = epoch.oe_value;
			epoch.oe_flags = orf_to_dtx_epoch_flags(opi->opi_flags);

			dtx_renew_epoch(&epoch, dth);

			D_DEBUG(DB_IO,
				"punch rpc %u renew epoch "DF_X64" => "DF_X64" for "DF_DTI"\n",
				opc, opi->opi_epoch, dth->dth_epoch, DP_DTI(&opi->opi_dti));

			opi->opi_epoch = dth->dth_epoch;
		}

		goto again;
	}

out:
	return rc;
}

int
obj_tgt_punch(struct obj_tgt_punch_args *otpa, uint32_t *shards, uint32_t count)
{
	struct obj_io_context	 ioc = { 0 };
	struct obj_io_context	*p_ioc = otpa->sponsor_ioc;
	struct dtx_handle	*dth = otpa->sponsor_dth;
	struct obj_punch_in	*opi = otpa->opi;
	struct dtx_epoch	 epoch;
	daos_epoch_t		 tmp;
	uint32_t		 dtx_flags = 0;
	int			 rc = 0;

	if (p_ioc == NULL) {
		p_ioc = &ioc;
		rc = obj_ioc_begin(opi->opi_oid.id_pub, opi->opi_map_ver, opi->opi_pool_uuid,
				   opi->opi_co_hdl, opi->opi_co_uuid, otpa->data, opi->opi_flags,
				   p_ioc);
		if (rc != 0)
			goto out;
	}

	if (dth != NULL) {
		if (dth->dth_prepared)
			D_GOTO(out, rc = 0);

		goto exec;
	}

	if (opi->opi_flags & ORF_RESEND) {
		tmp = opi->opi_epoch;
		rc = dtx_handle_resend(p_ioc->ioc_vos_coh, &opi->opi_dti, &tmp, NULL);
		/* Do nothing if 'prepared' or 'committed'. */
		if (rc == -DER_ALREADY || rc == 0)
			D_GOTO(out, rc = 0);

		/* Abort old one with different epoch, then re-execute with new epoch. */
		if (rc == -DER_MISMATCH)
			/* Abort it by force with MAX epoch to guarantee
			 * that it can be aborted.
			 */
			rc = vos_dtx_abort(p_ioc->ioc_vos_coh, &opi->opi_dti, tmp);

		if (rc < 0 && rc != -DER_NONEXIST)
			D_GOTO(out, rc);
	}

	epoch.oe_value = opi->opi_epoch;
	epoch.oe_first = epoch.oe_value; /* unused for TGT_PUNCH */
	epoch.oe_flags = orf_to_dtx_epoch_flags(opi->opi_flags);

	if (opi->opi_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	/* Start the local transaction */
	rc = dtx_begin(p_ioc->ioc_vos_coh, &opi->opi_dti, &epoch, count, opi->opi_map_ver,
		       &opi->opi_oid, opi->opi_dti_cos.ca_arrays, opi->opi_dti_cos.ca_count,
		       dtx_flags, otpa->mbs, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID ": Failed to start DTX for punch " DF_RC "\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		D_GOTO(out, rc = -DER_IO);

exec:
	rc = obj_local_punch(opi, otpa->opc, count, shards, p_ioc, dth);
	if (rc != 0)
		DL_CDEBUG(rc == -DER_INPROGRESS || rc == -DER_TX_RESTART ||
			  (rc == -DER_NONEXIST && (opi->opi_api_flags & DAOS_COND_PUNCH)),
			  DB_IO, DLOG_ERR, rc, DF_UOID, DP_UOID(opi->opi_oid));

out:
	if (otpa->ver != NULL)
		*otpa->ver = p_ioc->ioc_map_ver;

	if (dth != NULL && dth != otpa->sponsor_dth)
		rc = dtx_end(dth, p_ioc->ioc_coc, rc);

	if (p_ioc == &ioc)
		obj_ioc_end(p_ioc, rc);

	return rc;
}

/* Handle the punch requests on non-leader */
void
ds_obj_tgt_punch_handler(crt_rpc_t *rpc)
{
	struct obj_tgt_punch_args	 otpa = { 0 };
	struct obj_punch_in		*opi = crt_req_get(rpc);
	struct daos_shard_tgt		*tgts = opi->opi_shard_tgts.ca_arrays;
	uint32_t			 tgt_cnt = opi->opi_shard_tgts.ca_count;
	uint32_t			 version = 0;
	int				 rc;

	rc = obj_gen_dtx_mbs(opi->opi_flags, &tgt_cnt, &tgts, &otpa.mbs);
	if (rc != 0)
		D_GOTO(out, rc);

	otpa.opc = opc_get(rpc->cr_opc);
	otpa.opi = opi;
	otpa.ver = &version;
	otpa.data = rpc;

	rc = obj_tgt_punch(&otpa, &opi->opi_oid.id_shard, 1);

out:
	obj_punch_complete(rpc, rc, version);
	D_FREE(otpa.mbs);
}

static int
obj_punch_agg_cb(struct dtx_leader_handle *dlh, void *arg)
{
	struct dtx_sub_status	*sub;
	uint32_t		 sub_cnt = dlh->dlh_normal_sub_cnt + dlh->dlh_delay_sub_cnt;
	int			 allow_failure = dlh->dlh_allow_failure;
	int			 allow_failure_cnt;
	int			 succeeds;
	int			 result = 0;
	int			 i;

	/*
	 * For conditional punch, let's ignore DER_NONEXIST if some shard succeed,
	 * since the object may not exist on some shards due to EC partial update.
	 */
	D_ASSERTF(allow_failure == -DER_NONEXIST, "Unexpected allow failure %d\n", allow_failure);

	for (i = 0, allow_failure_cnt = 0, succeeds = 0; i < sub_cnt; i++) {
		sub = &dlh->dlh_subs[i];
		if (sub->dss_tgt.st_rank != DAOS_TGT_IGNORE && sub->dss_comp) {
			if (sub->dss_result == 0) {
				succeeds++;
			} else if (sub->dss_result == allow_failure) {
				allow_failure_cnt++;
			} else if (result == -DER_INPROGRESS || result == -DER_AGAIN ||
				   result == 0) {
				/* Ignore INPROGRESS and AGAIN if there is other failure. */
				result = sub->dss_result;

				if (dlh->dlh_rmt_ver < sub->dss_version)
					dlh->dlh_rmt_ver = sub->dss_version;
			}
		}
	}

	D_DEBUG(DB_IO, DF_DTI" sub_requests %d/%d, allow_failure %d, result %d\n",
		DP_DTI(&dlh->dlh_handle.dth_xid),
		allow_failure_cnt, succeeds, allow_failure, result);

	if (allow_failure_cnt > 0 && result == 0 && succeeds == 0)
		result = allow_failure;

	return result;
}

static int
obj_tgt_punch_disp(struct dtx_leader_handle *dlh, void *arg, int idx, dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg	*exec_arg = arg;

	/* handle local operation */
	if (idx == -1) {
		crt_rpc_t		*rpc = exec_arg->rpc;
		struct obj_punch_in	*opi = crt_req_get(rpc);
		int			rc = 0;

		if (DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
			D_GOTO(comp, rc = -DER_IO);

		if (dlh->dlh_handle.dth_prepared)
			goto comp;

		rc = obj_local_punch(opi, opc_get(rpc->cr_opc), 1, &opi->opi_oid.id_shard,
				     exec_arg->ioc, &dlh->dlh_handle);
		if (rc != 0)
			DL_CDEBUG(rc == -DER_INPROGRESS || rc == -DER_TX_RESTART ||
				  (rc == -DER_NONEXIST && (opi->opi_api_flags & DAOS_COND_PUNCH)),
				  DB_IO, DLOG_ERR, rc, DF_UOID, DP_UOID(opi->opi_oid));

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
	struct dtx_leader_handle	*dlh = NULL;
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
	uint32_t			max_ver = 0;
	struct dtx_epoch		epoch;
	int				rc;
	bool				need_abort = false;

	opi = crt_req_get(rpc);
	D_ASSERT(opi != NULL);
	rc = obj_ioc_begin(opi->opi_oid.id_pub, opi->opi_map_ver,
			   opi->opi_pool_uuid, opi->opi_co_hdl,
			   opi->opi_co_uuid, rpc, opi->opi_flags, &ioc);
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
	if (rc == PE_OK_LOCAL) {
		opi->opi_flags &= ~ORF_EPOCH_UNCERTAIN;
		dtx_flags |= DTX_EPOCH_OWNER;
	}

	version = opi->opi_map_ver;
	max_ver = opi->opi_map_ver;
	tgts = opi->opi_shard_tgts.ca_arrays;
	tgt_cnt = opi->opi_shard_tgts.ca_count;

	rc = obj_gen_dtx_mbs(opi->opi_flags, &tgt_cnt, &tgts, &mbs);
	if (rc != 0)
		D_GOTO(out, rc);

	if (tgt_cnt == 0) {
		if (!(opi->opi_api_flags & DAOS_COND_MASK))
			dtx_flags |= DTX_DROP_CMT;
		dtx_flags |= DTX_SOLO;
	}
	if (opi->opi_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	/* Handle resend. */
	if (opi->opi_flags & ORF_RESEND) {
		daos_epoch_t	e;

again:
		if (flags & ORF_RESEND)
			e = opi->opi_epoch;
		else
			e = 0;
		version = opi->opi_map_ver;
		rc      = dtx_handle_resend(ioc.ioc_vos_coh, &opi->opi_dti, &e, &version);
		switch (rc) {
		case -DER_ALREADY:
			D_GOTO(out, rc = 0);
		case 0:
			opi->opi_epoch = e;
			flags |= ORF_RESEND;
			/* TODO: Also recovery the epoch uncertainty. */
			break;
		case -DER_MISMATCH:
			rc = vos_dtx_abort(ioc.ioc_vos_coh, &opi->opi_dti, e);
			if (rc < 0 && rc != -DER_NONEXIST)
				D_GOTO(out, rc);
			/* Fall through */
		case -DER_NONEXIST:
			flags = 0;
			break;
		default:
			D_GOTO(out, rc);
		}
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST) ||
		   DAOS_FAIL_CHECK(DAOS_DTX_LONG_TIME_RESEND)) {
		goto cleanup;
	}

	/* For leader case, we need to find out the potential conflict
	 * (or share the same non-committed object/dkey) DTX(s) in the
	 * CoS (committable) cache, piggyback them via the dispdatched
	 * RPC to non-leaders. Then the non-leader replicas can commit
	 * them before real modifications to avoid availability issues.
	 */
	D_FREE(dti_cos);
	dti_cos_cnt = dtx_cos_get_piggyback(ioc.ioc_coc, &opi->opi_oid, opi->opi_dkey_hash,
					    DTX_THRESHOLD_COUNT, &dti_cos);
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
			      tgts, tgt_cnt, dtx_flags, mbs, NULL /* dce */, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID ": Failed to start DTX for punch " DF_RC "\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.flags |= flags;

	/* Execute the operation on all shards */
	if (opi->opi_api_flags & DAOS_COND_PUNCH)
		rc = dtx_leader_exec_ops(dlh, obj_tgt_punch_disp, obj_punch_agg_cb, -DER_NONEXIST,
					 &exec_arg);
	else
		rc = dtx_leader_exec_ops(dlh, obj_tgt_punch_disp, NULL, 0, &exec_arg);

	if (max_ver < dlh->dlh_rmt_ver)
		max_ver = dlh->dlh_rmt_ver;

	/* Stop the distribute transaction */
	rc = dtx_leader_end(dlh, ioc.ioc_coc, rc);
	switch (rc) {
	case -DER_TX_RESTART:
		/* Only standalone punches use this RPC. Retry with newer epoch. */
		opi->opi_epoch = d_hlc_get();
		exec_arg.flags |= ORF_RESEND;
		flags = ORF_RESEND;
		goto again;
	case -DER_AGAIN:
		need_abort = true;
		exec_arg.flags |= ORF_RESEND;
		flags = ORF_RESEND;
		ABT_thread_yield();
		goto again;
	default:
		break;
	}

	if (!(opi->opi_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		goto cleanup;

out:
	if (rc != 0 && need_abort) {
		struct dtx_entry	 dte;
		int			 rc1;

		dte.dte_xid = opi->opi_dti;
		dte.dte_ver = version;
		dte.dte_refs = 1;
		dte.dte_mbs = mbs;
		rc1 = dtx_abort(ioc.ioc_coc, &dte, opi->opi_epoch);
		if (rc1 != 0 && rc1 != -DER_NONEXIST)
			D_WARN("Failed to abort DTX "DF_DTI": "DF_RC"\n",
			       DP_DTI(&opi->opi_dti), DP_RC(rc1));
	}

	obj_punch_complete(rpc, rc, max_ver);

cleanup:
	D_FREE(mbs);
	D_FREE(dti_cos);
	obj_ioc_end(&ioc, rc);
}

static int
obj_local_query(struct obj_tgt_query_args *otqa, struct obj_io_context *ioc, daos_unit_oid_t oid,
		daos_epoch_t epoch, uint64_t api_flags, uint32_t map_ver, uint32_t opc,
		uint32_t count, uint32_t *shards, struct dtx_handle *dth)
{
	struct obj_query_merge_args	 oqma = { 0 };
	daos_key_t			 dkey;
	daos_key_t			 akey;
	daos_key_t			*p_dkey;
	daos_key_t			*p_akey;
	daos_recx_t			*p_recx;
	daos_epoch_t			*p_epoch;
	daos_unit_oid_t			 t_oid = oid;
	uint32_t			 query_flags = api_flags;
	uint32_t			 cell_size = 0;
	uint64_t			 stripe_size = 0;
	daos_epoch_t			 max_epoch = 0;
	daos_recx_t			 recx = { 0 };
	int				 succeeds;
	int				 rc = 0;
	int				 i;

	if (count > 1)
		D_ASSERT(otqa->otqa_need_copy);

	if (daos_oclass_is_ec(&ioc->ioc_oca) && api_flags & DAOS_GET_RECX) {
		query_flags |= VOS_GET_RECX_EC;
		cell_size = obj_ec_cell_rec_nr(&ioc->ioc_oca);
		stripe_size = obj_ec_stripe_rec_nr(&ioc->ioc_oca);
	}

	otqa->otqa_shard = shards[0];

	if (otqa->otqa_need_copy) {
		oqma.oqma_oca = &ioc->ioc_oca;
		oqma.oqma_oid = oid;
		oqma.oqma_oid.id_shard = shards[0];
		oqma.oqma_in_dkey = otqa->otqa_in_dkey;
		oqma.oqma_tgt_dkey = &otqa->otqa_dkey_copy;
		oqma.oqma_tgt_akey = &otqa->otqa_akey_copy;
		oqma.oqma_tgt_recx = &otqa->otqa_recx;
		oqma.oqma_tgt_epoch = &otqa->otqa_max_epoch;
		oqma.oqma_tgt_map_ver = &otqa->otqa_version;
		oqma.oqma_shard = &otqa->otqa_shard;
		oqma.oqma_flags = api_flags;
		oqma.oqma_opc = opc;
		oqma.oqma_src_map_ver = map_ver;
	}

	for (i = 0, succeeds = 0; i < count; i++ ) {
		if (api_flags & DAOS_GET_DKEY) {
			if (otqa->otqa_need_copy)
				p_dkey = &dkey;
			else
				p_dkey = otqa->otqa_out_dkey;
			d_iov_set(p_dkey, NULL, 0);
		} else {
			p_dkey = otqa->otqa_in_dkey;
		}

		if (api_flags & DAOS_GET_AKEY) {
			if (otqa->otqa_need_copy)
				p_akey = &akey;
			else
				p_akey = otqa->otqa_out_akey;
			d_iov_set(p_akey, NULL, 0);
		} else {
			p_akey = otqa->otqa_in_akey;
		}

		if (otqa->otqa_need_copy) {
			p_recx = &recx;
			p_epoch = &max_epoch;
		} else {
			p_recx = &otqa->otqa_recx;
			p_epoch = &otqa->otqa_max_epoch;
		}

		t_oid.id_shard = shards[i];

again:
		rc = vos_obj_query_key(ioc->ioc_vos_coh, t_oid, query_flags, epoch, p_dkey, p_akey,
				       p_recx, p_epoch, cell_size, stripe_size, dth);
		if (obj_dtx_need_refresh(dth, rc)) {
			rc = dtx_refresh(dth, ioc->ioc_coc);
			if (rc == -DER_AGAIN)
				goto again;
		}

		if (rc == -DER_NONEXIST) {
			if (otqa->otqa_need_copy && otqa->otqa_max_epoch < *p_epoch)
				otqa->otqa_max_epoch = *p_epoch;
			continue;
		}

		if (rc != 0)
			goto out;

		succeeds++;

		if (!otqa->otqa_need_copy) {
			otqa->otqa_shard = shards[i];
			goto out;
		}

		if (succeeds == 1) {
			rc = daos_iov_copy(&otqa->otqa_dkey_copy, p_dkey);
			if (rc != 0)
				goto out;

			rc = daos_iov_copy(&otqa->otqa_akey_copy, p_akey);
			if (rc != 0)
				goto out;

			otqa->otqa_recx = *p_recx;
			if (otqa->otqa_max_epoch < *p_epoch)
				otqa->otqa_max_epoch = *p_epoch;
			otqa->otqa_shard = shards[i];
			otqa->otqa_keys_allocated = 1;

			if (otqa->otqa_raw_recx && daos_oclass_is_ec(&ioc->ioc_oca)) {
				obj_ec_recx_vos2daos(&ioc->ioc_oca, t_oid, p_dkey, &otqa->otqa_recx,
						     api_flags & DAOS_GET_MAX ? true : false);
				otqa->otqa_raw_recx = 0;
			}
		} else {
			oqma.oqma_oid.id_shard = shards[i];
			oqma.oqma_src_epoch = *p_epoch;
			oqma.oqma_src_dkey = p_dkey;
			oqma.oqma_src_akey = p_akey;
			oqma.oqma_src_recx = p_recx;
			oqma.oqma_raw_recx = 1;
			/*
			 * Merge (L1) the results from different shards on the same VOS target
			 * into current otqa that stands for the result for current VOS target.
			 */
			rc = daos_obj_query_merge(&oqma);
			if (rc != 0)
				goto out;
		}
	}

	if (rc == -DER_NONEXIST && succeeds > 0)
		rc = 0;

out:
	if (rc == -DER_NONEXIST && otqa->otqa_need_copy && !otqa->otqa_keys_allocated) {
		/* Allocate key buffer for subsequent merge. */
		rc = daos_iov_alloc(&otqa->otqa_dkey_copy, sizeof(uint64_t), true);
		if (rc != 0)
			goto out;

		rc = daos_iov_alloc(&otqa->otqa_akey_copy, sizeof(uint64_t), true);
		if (rc != 0)
			goto out;

		otqa->otqa_keys_allocated = 1;
	}

	otqa->otqa_result = rc;
	otqa->otqa_completed = 1;

	return rc;
}

int
obj_tgt_query(struct obj_tgt_query_args *otqa, uuid_t po_uuid, uuid_t co_hdl, uuid_t co_uuid,
	      daos_unit_oid_t oid, daos_epoch_t epoch, daos_epoch_t epoch_first,
	      uint64_t api_flags, uint32_t rpc_flags, uint32_t *map_ver, crt_rpc_t *rpc,
	      uint32_t count, uint32_t *shards, struct dtx_id *xid)
{
	struct dtx_epoch	 dtx_epoch = { 0 };
	struct obj_io_context	 ioc = { 0 };
	struct obj_io_context	*p_ioc = otqa->otqa_ioc;
	struct dtx_handle	*dth = otqa->otqa_dth;
	int			 rc = 0;

	if (p_ioc == NULL)
		p_ioc = &ioc;

	if (!p_ioc->ioc_began) {
		rc = obj_ioc_begin(oid.id_pub, *map_ver, po_uuid, co_hdl, co_uuid, rpc, rpc_flags,
				   p_ioc);
		if (rc != 0)
			goto out;
	}

	if (dth == NULL) {
		dtx_epoch.oe_value = epoch;
		dtx_epoch.oe_first = epoch_first;
		dtx_epoch.oe_flags = orf_to_dtx_epoch_flags(rpc_flags);

		rc = dtx_begin(p_ioc->ioc_vos_coh, xid, &dtx_epoch, 0, *map_ver, &oid, NULL, 0, 0,
			       NULL, &dth);
		if (rc != 0)
			goto out;
	}

	rc = obj_local_query(otqa, p_ioc, oid, epoch, api_flags, *map_ver, opc_get(rpc->cr_opc),
			     count, shards, dth);

	if (dth != otqa->otqa_dth)
		rc = dtx_end(dth, p_ioc->ioc_coc, rc);

out:
	*map_ver = p_ioc->ioc_map_ver;
	if (p_ioc != otqa->otqa_ioc)
		obj_ioc_end(p_ioc, rc);

	return rc;
}

void
ds_obj_query_key_handler(crt_rpc_t *rpc)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct obj_query_key_in		*okqi = crt_req_get(rpc);
	struct obj_query_key_out	*okqo = crt_reply_get(rpc);
	struct obj_tgt_query_args	 otqa = { 0 };
	uint32_t			 version = okqi->okqi_map_ver;
	int				 rc;

	rc = process_epoch(&okqi->okqi_epoch, &okqi->okqi_epoch_first, &okqi->okqi_flags);
	if (rc == PE_OK_LOCAL)
		okqi->okqi_flags &= ~ORF_EPOCH_UNCERTAIN;

	otqa.otqa_in_dkey = &okqi->okqi_dkey;
	otqa.otqa_in_akey = &okqi->okqi_akey;
	otqa.otqa_out_dkey = &okqo->okqo_dkey;
	otqa.otqa_out_akey = &okqo->okqo_akey;

	rc = obj_tgt_query(&otqa, okqi->okqi_pool_uuid, okqi->okqi_co_hdl, okqi->okqi_co_uuid,
			   okqi->okqi_oid, okqi->okqi_epoch, okqi->okqi_epoch_first,
			   okqi->okqi_api_flags, okqi->okqi_flags, &version, rpc, 1,
			   &okqi->okqi_oid.id_shard, &okqi->okqi_dti);
	okqo->okqo_max_epoch = otqa.otqa_max_epoch;
	if (rc == 0)
		okqo->okqo_recx = otqa.otqa_recx;
	else
		DL_CDEBUG(rc != -DER_NONEXIST && rc != -DER_INPROGRESS && rc != -DER_TX_RESTART,
			  DLOG_ERR, DB_IO, rc, "Failed to handle reqular query RPC %p on XS %u/%u "
			  "for obj "DF_UOID" epc "DF_X64" pmv %u/%u, api_flags "DF_X64" with dti "
			  DF_DTI, rpc, dmi->dmi_xs_id, dmi->dmi_tgt_id, DP_UOID(okqi->okqi_oid),
			  okqi->okqi_epoch, okqi->okqi_map_ver, version, okqi->okqi_api_flags,
			  DP_DTI(&okqi->okqi_dti));

	obj_reply_set_status(rpc, rc);
	obj_reply_map_version_set(rpc, version);
	okqo->okqo_epoch = okqi->okqi_epoch;

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
	daos_epoch_t		 epoch = d_hlc_get();
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
			   osi->osi_co_uuid, rpc, 0, &ioc);
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
					DF_OID", extent: "DF_RECX"): %d\n",
					DP_OID(oid), DP_RECX(iod->iod_recxs[i]), rc);
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

static inline void
cpd_unpin_objects(daos_handle_t coh, struct vos_pin_handle *pin_hdl)
{
	if (pin_hdl != NULL)
		vos_unpin_objects(coh, pin_hdl);
}

static int
cpd_pin_objects(daos_handle_t coh, struct daos_cpd_sub_req *dcsrs,
		struct daos_cpd_req_idx *dcri, int count, struct vos_pin_handle **pin_hdl)
{
	struct daos_cpd_sub_req	*dcsr;
	daos_unit_oid_t		*oids;
	int			 i, rc;

	if (count == 0)
		return 0;

	D_ALLOC_ARRAY(oids, count);
	if (oids == NULL)
		return -DER_NOMEM;

	for (i = 0; i < count; i++) {
		dcsr = &dcsrs[dcri[i].dcri_req_idx];
		dcsr->dcsr_oid.id_shard = dcri[i].dcri_shard_id;

		D_ASSERT(dcsr->dcsr_opc != DCSO_READ);
		oids[i] = dcsr->dcsr_oid;
	}

	rc = vos_pin_objects(coh, oids, count, pin_hdl);
	if (rc)
		DL_ERROR(rc, "Failed to pin CPD objects.");

	D_FREE(oids);
	return rc;
}

/* Locally process the operations belong to one DTX.
 * Common logic, shared by both leader and non-leader.
 */
#define LOCAL_STACK_NUM		2
static int
ds_cpd_handle_one(crt_rpc_t *rpc, struct daos_cpd_sub_head *dcsh, struct daos_cpd_disp_ent *dcde,
		  struct daos_cpd_sub_req *dcsrs, struct obj_io_context *ioc,
		  struct dtx_handle *dth)
{
	struct daos_cpd_req_idx *dcri = dcde->dcde_reqs;
	struct daos_cpd_sub_req *dcsr;
	struct daos_cpd_update  *dcu;
	daos_handle_t           *iohs                             = NULL;
	struct bio_desc        **biods                            = NULL;
	struct obj_bulk_args    *bulks                            = NULL;
	daos_iod_t               local_iods[LOCAL_STACK_NUM]      = {0};
	uint32_t                 local_iod_nrs[LOCAL_STACK_NUM]   = {0};
	struct dcs_iod_csums     local_csums[LOCAL_STACK_NUM]     = {0};
	struct dcs_csum_info     local_csum_info[LOCAL_STACK_NUM] = {0};
	uint64_t                 local_offs[LOCAL_STACK_NUM]      = {0};
	uint64_t                 local_skips[LOCAL_STACK_NUM]     = {0};
	daos_iod_t              *local_p_iods[LOCAL_STACK_NUM]    = {0};
	struct dcs_iod_csums    *local_p_csums[LOCAL_STACK_NUM]   = {0};
	uint64_t                *local_p_offs[LOCAL_STACK_NUM]    = {0};
	uint8_t                 *local_p_skips[LOCAL_STACK_NUM]   = {0};
	uint8_t                **pskips                           = NULL;
	daos_iod_t             **piods                            = NULL;
	uint32_t                *piod_nrs                         = NULL;
	struct dcs_iod_csums   **pcsums                           = NULL;
	uint64_t               **poffs                            = NULL;
	struct dcs_csum_info    *pcsum_info                       = NULL;
	int                      rma                              = 0;
	int                      rma_idx                          = 0;
	int                      rc                               = 0;
	int                      i;
	uint64_t                 update_flags;
	uint64_t                 sched_seq = sched_cur_seq();
	struct vos_pin_handle	*pin_hdl = NULL;

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
			DL_CDEBUG(rc != -DER_INPROGRESS && rc != -DER_TX_RESTART, DLOG_ERR, DB_IO,
				  rc, "Failed to set read TS for obj " DF_UOID ", DTX " DF_DTI,
				  DP_UOID(dcsr->dcsr_oid), DP_DTI(&dcsh->dcsh_xid));
			goto out;
		}
	}

	dcri += dcde->dcde_read_cnt;
	if (dcde->dcde_write_cnt > LOCAL_STACK_NUM) {
		D_ALLOC_ARRAY(piods, dcde->dcde_write_cnt);
		D_ALLOC_ARRAY(piod_nrs, dcde->dcde_write_cnt);
		D_ALLOC_ARRAY(pcsums, dcde->dcde_write_cnt);
		D_ALLOC_ARRAY(poffs, dcde->dcde_write_cnt);
		D_ALLOC_ARRAY(pcsum_info, dcde->dcde_write_cnt);
		D_ALLOC_ARRAY(pskips, dcde->dcde_write_cnt);
		if (piods == NULL || piod_nrs == NULL || pcsums == NULL || poffs == NULL ||
		    pcsum_info == NULL || pskips == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	} else {
		piods = local_p_iods;
		pcsums = local_p_csums;
		poffs = local_p_offs;
		piod_nrs = local_iod_nrs;
		pcsum_info = local_csum_info;
		pskips = local_p_skips;
		for (i = 0; i < dcde->dcde_write_cnt; i++) {
			piods[i] = &local_iods[i];
			pcsums[i] = &local_csums[i];
			poffs[i] = &local_offs[i];
			pskips[i] = (uint8_t *)&local_skips[i];
		}
	}

	rc = cpd_pin_objects(ioc->ioc_vos_coh, dcsrs, dcri, dcde->dcde_write_cnt, &pin_hdl);
	if (rc) {
		DL_ERROR(rc, "Failed to pin objects.");
		goto out;
	}

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

		/* There is no object associated with this ioc while
		 * initializing, we have to do it at here.
		 */
		rc = obj_ioc_init_oca(ioc, dcsr->dcsr_oid.id_pub, true);
		if (rc)
			D_GOTO(out, rc);

		rc = obj_get_iods_offs(dcsr->dcsr_oid, &dcu->dcu_iod_array, &ioc->ioc_oca,
				       dcsr->dcsr_dkey_hash, ioc->ioc_layout_ver, &piods[i],
				       &poffs[i], &pskips[i], &pcsums[i], &pcsum_info[i],
				       &piod_nrs[i]);
		if (rc != 0)
			D_GOTO(out, rc);

		rc = csum_verify_keys(ioc->ioc_coc->sc_csummer,
				      &dcsr->dcsr_dkey, dcu->dcu_dkey_csum,
				      &dcu->dcu_iod_array, &dcsr->dcsr_oid);
		if (rc != 0)
			goto out;

		if (iohs == NULL) {
			D_ALLOC_ARRAY(iohs, dcde->dcde_write_cnt);
			if (iohs == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			D_ALLOC_ARRAY(biods, dcde->dcde_write_cnt);
			if (biods == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		if (dcu->dcu_flags & ORF_EC) {
			uint32_t tgt_off;

			tgt_off = obj_ec_shard_off_by_layout_ver(ioc->ioc_layout_ver,
								 dcsr->dcsr_dkey_hash,
								 &ioc->ioc_oca,
								 dcsr->dcsr_oid.id_shard);
			obj_singv_ec_rw_filter(dcsr->dcsr_oid, &ioc->ioc_oca, tgt_off,
					       piods[i], poffs[i], dcsh->dcsh_epoch.oe_value,
					       dcu->dcu_flags, piod_nrs[i], true, false, NULL);
		} else {
			piods[i] = dcu->dcu_iod_array.oia_iods;
			pcsums[i] = dcu->dcu_iod_array.oia_iod_csums;
		}

		update_flags = dcsr->dcsr_api_flags;
		if (dcu->dcu_flags & ORF_CPD_BULK &&
		    ioc->ioc_coc->sc_props.dcp_dedup_enabled) {
			update_flags |= VOS_OF_DEDUP;
			if (ioc->ioc_coc->sc_props.dcp_dedup_verify)
				update_flags |= VOS_OF_DEDUP_VERIFY;
		}
		if (dcu->dcu_flags & ORF_EC)
			update_flags |= VOS_OF_EC;

		rc = vos_update_begin(ioc->ioc_vos_coh,
				dcsr->dcsr_oid, dcsh->dcsh_epoch.oe_value,
				update_flags, &dcsr->dcsr_dkey,
				piod_nrs[i], piods[i], pcsums[i],
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

			rc = obj_bulk_transfer(rpc, CRT_BULK_GET, dcu->dcu_flags & ORF_BULK_BIND,
					       dcu->dcu_bulks, poffs[i], pskips[i], iohs[i], NULL,
					       piod_nrs[i], dcsr->dcsr_nr, &bulks[i]);
			if (rc != 0) {
				D_ERROR("Bulk transfer failed for obj "
					DF_UOID", DTX "DF_DTI": "DF_RC"\n",
					DP_UOID(dcsr->dcsr_oid),
					DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
				goto out;
			}

			rma++;
		} else if (dcu->dcu_sgls != NULL) {
			/* no akey skip for non-bulk case (only with one data target) */
			D_ASSERTF(piod_nrs[i] == dcsr->dcsr_nr,
				  "piod_nrs[%d] %d, dcsr->dcsr_nr %d\n",
				  i, piod_nrs[i], dcsr->dcsr_nr);
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
		if (rc == 0 && bulks[i].result != 0)
			rc = bulks[i].result;

		ABT_eventual_free(&bulks[i].eventual);
		bio_iod_flush(biods[i]);
		rma_idx++;

		if (rc != 0) {
			D_ERROR(DF_DTI" ABT_eventual_wait failed: "DF_RC"\n",
				DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));

			goto out;
		}
	}

	/* P4: data verification and copy. */
	for (i = 0; i < dcde->dcde_write_cnt; i++) {
		dcsr = &dcsrs[dcri[i].dcri_req_idx];
		if (dcsr->dcsr_opc != DCSO_UPDATE)
			continue;

		dcu = &dcsr->dcsr_update;
		rc = vos_dedup_verify(iohs[i]);
		if (rc != 0) {
			D_ERROR("dedup_verify failed for obj "DF_UOID", DTX "DF_DTI": "DF_RC"\n",
				DP_UOID(dcsr->dcsr_oid), DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
			goto out;
		}

		rc = obj_verify_bio_csum(dcsr->dcsr_oid.id_pub, piods[i], pcsums[i], biods[i],
					 ioc->ioc_coc->sc_csummer, piod_nrs[i]);
		if (rc != 0) {
			if (rc == -DER_CSUM)
				obj_log_csum_err();
			goto out;
		}

		rc = bio_iod_post(biods[i], 0);
		biods[i] = NULL;
		if (rc != 0) {
			D_ERROR("iod_post failed for obj "DF_UOID", DTX "DF_DTI": "DF_RC"\n",
				DP_UOID(dcsr->dcsr_oid), DP_DTI(&dcsh->dcsh_xid), DP_RC(rc));
			goto out;
		}
	}

	/* The DTX has been aborted during long time bulk data transfer. */
	if (unlikely(dth->dth_aborted))
		D_GOTO(out, rc = -DER_CANCELED);

	/* There is CPU yield after DTX start, and the resent RPC may be handled during that.
	 * Let's check resent again before further process.
	 */
	if (rc == 0 && dth->dth_modification_cnt > 0 && sched_cur_seq() != sched_seq) {
		if (dth->dth_need_validation) {
			daos_epoch_t	epoch = 0;
			int		rc1;

			rc1 = dtx_handle_resend(ioc->ioc_vos_coh, &dcsh->dcsh_xid, &epoch, NULL);
			switch (rc1) {
			case 0:
			case -DER_ALREADY:
				D_GOTO(out, rc = -DER_ALREADY);
			case -DER_NONEXIST:
			case -DER_EP_OLD:
				break;
			default:
				D_GOTO(out, rc = rc1);
			}
		}

		if (rc == 0 && dth->dth_solo) {
			daos_epoch_t	epoch = dcsh->dcsh_epoch.oe_value;

			D_ASSERT(dcde->dcde_read_cnt == 0);
			D_ASSERT(dcde->dcde_write_cnt == 1);

			dcsh->dcsh_epoch.oe_value = d_hlc_get();

			dtx_renew_epoch(&dcsh->dcsh_epoch, dth);
			if (daos_handle_is_valid(iohs[0]))
				vos_update_renew_epoch(iohs[0], dth);

			D_DEBUG(DB_IO,
				"CPD rpc %p renew epoch "DF_X64" => "DF_X64" for "DF_DTI"\n",
				rpc, epoch, dcsh->dcsh_epoch.oe_value, DP_DTI(&dcsh->dcsh_xid));
		}
	}

	/* P5: punch and vos_update_end. */
	for (i = 0; i < dcde->dcde_write_cnt; i++) {
		dcsr = &dcsrs[dcri[i].dcri_req_idx];

		if (dcsr->dcsr_opc == DCSO_UPDATE) {
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
			for (i = 0; i < dcde->dcde_write_cnt; i++) {
				if (!bulks[i].inited)
					continue;

				if (bulks[i].eventual != ABT_EVENTUAL_NULL) {
					ABT_eventual_wait(bulks[i].eventual, NULL);
					ABT_eventual_free(&bulks[i].eventual);
				}
			}
		}

		if (biods != NULL) {
			for (i = 0; i < dcde->dcde_write_cnt; i++) {
				if (biods[i] != NULL)
					bio_iod_post(biods[i], rc);
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

	cpd_unpin_objects(ioc->ioc_vos_coh, pin_hdl);

	D_FREE(iohs);
	D_FREE(biods);
	D_FREE(bulks);

	for (i = 0; i < dcde->dcde_write_cnt; i++) {
		dcsr = &dcsrs[dcri[i].dcri_req_idx];
		if (dcsr->dcsr_opc != DCSO_UPDATE)
			continue;

		dcu = &dcsr->dcsr_update;
		if (piods!= NULL && piods[i] != NULL && piods[i] != &local_iods[i] &&
		    piods[i] != dcu->dcu_iod_array.oia_iods)
			D_FREE(piods[i]);

		if (poffs != NULL && poffs[i] != NULL && poffs[i] != &local_offs[i] &&
		    poffs[i] != dcu->dcu_iod_array.oia_offs)
			D_FREE(poffs[i]);

		if (pskips != NULL && pskips[i] != NULL && pskips[i] != (uint8_t *)&local_skips[i])
			D_FREE(pskips[i]);

		if (pcsums != NULL && pcsums[i] != NULL && pcsums[i] != &local_csums[i] &&
		    pcsums[i] != dcu->dcu_iod_array.oia_iod_csums) {
			struct dcs_iod_csums	*csum = pcsums[i];
			int j;

			for (j = 0; j < dcu->dcu_iod_array.oia_oiod_nr; i++) {
				if (dcu->dcu_iod_array.oia_iods[j].iod_type == DAOS_IOD_SINGLE &&
				    csum[j].ic_data != NULL)
					D_FREE(csum[j].ic_data);
			}

			D_FREE(csum);
		}
	}

	if (piods != local_p_iods && piods != NULL)
		D_FREE(piods);
	if (piod_nrs != local_iod_nrs && piod_nrs != NULL)
		D_FREE(piod_nrs);
	if (poffs != local_p_offs && poffs != NULL)
		D_FREE(poffs);
	if (pskips != local_p_skips && pskips != NULL)
		D_FREE(pskips);
	if (pcsums != local_p_csums && pcsums != NULL)
		D_FREE(pcsums);
	if (pcsum_info != local_csum_info && pcsum_info != NULL)
		D_FREE(pcsum_info);

	return unlikely(rc == -DER_ALREADY) ? 0 : rc;
}

static int
ds_cpd_handle_one_wrap(crt_rpc_t *rpc, struct daos_cpd_sub_head *dcsh,
		       struct daos_cpd_disp_ent *dcde, struct daos_cpd_sub_req *dcsrs,
		       struct obj_io_context *ioc, struct dtx_handle *dth)
{
	struct obj_cpd_in       *oci = crt_req_get(rpc);
	struct dtx_share_peer	*dsp;
	uint32_t		 retry = 0;
	int			 rc;

again:
	rc = ds_cpd_handle_one(rpc, dcsh, dcde, dcsrs, ioc, dth);
	if (obj_dtx_need_refresh(dth, rc)) {
		if (++retry < 3) {
			rc = dtx_refresh(dth, ioc->ioc_coc);
			if (rc == -DER_AGAIN)
				goto again;
		} else if (oci->oci_flags & ORF_MAYBE_STARVE) {
			dsp = d_list_entry(dth->dth_share_tbd_list.next,
					   struct dtx_share_peer, dsp_link);
			D_WARN(
			    "DTX refresh for " DF_DTI " because of " DF_DTI " (%d), maybe starve\n",
			    DP_DTI(&dth->dth_xid), DP_DTI(&dsp->dsp_xid), dth->dth_share_tbd_count);
		}
	}

	return rc;
}

static int
ds_obj_dtx_follower(crt_rpc_t *rpc, struct obj_io_context *ioc)
{
	struct dtx_handle		*dth = NULL;
	struct obj_cpd_in		*oci = crt_req_get(rpc);
	struct daos_cpd_sub_head	*dcsh = ds_obj_cpd_get_head(rpc, 0);
	struct daos_cpd_disp_ent	*dcde = ds_obj_cpd_get_ents(rpc, 0, -1);
	struct daos_cpd_sub_req		*dcsr = ds_obj_cpd_get_reqs(rpc, 0);
	daos_epoch_t			 e = dcsh->dcsh_epoch.oe_value;
	uint32_t			 dtx_flags = DTX_DIST;
	int				 rc = 0;
	int				 rc1 = 0;

	D_DEBUG(DB_IO, "Handling DTX "DF_DTI" on non-leader\n",
		DP_DTI(&dcsh->dcsh_xid));

	D_ASSERT(dcsh->dcsh_epoch.oe_value != 0);
	D_ASSERT(dcsh->dcsh_epoch.oe_value != DAOS_EPOCH_MAX);

	if (oci->oci_flags & ORF_RESEND) {
		rc1 = dtx_handle_resend(ioc->ioc_vos_coh, &dcsh->dcsh_xid, &e, NULL);
		/* Do nothing if 'prepared' or 'committed'. */
		if (rc1 == -DER_ALREADY || rc1 == 0)
			D_GOTO(out, rc = 0);
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
		rc = vos_dtx_abort(ioc->ioc_vos_coh, &dcsh->dcsh_xid, e);
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

	rc = ds_cpd_handle_one_wrap(rpc, dcsh, dcde, dcsr, ioc, dth);

	rc = dtx_end(dth, ioc->ioc_coc, rc);

out:
	DL_CDEBUG(rc != 0 && rc != -DER_INPROGRESS && rc != -DER_TX_RESTART, DLOG_ERR, DB_IO, rc,
		  "Handled DTX " DF_DTI " on non-leader", DP_DTI(&dcsh->dcsh_xid));

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

			dcde = ds_obj_cpd_get_ents(dca->dca_rpc, dca->dca_idx, 0);
			/* The check for read capa has been done before handling
			 * the CPD RPC. Here, only need to check the write capa.
			 */
			if (dcde->dcde_write_cnt != 0) {
				rc = obj_capa_check(ioc->ioc_coh, true, false);
				if (rc != 0)
					goto comp;
			}

			dcsh = ds_obj_cpd_get_head(dca->dca_rpc, dca->dca_idx);
			dcsrs = ds_obj_cpd_get_reqs(dca->dca_rpc, dca->dca_idx);
			rc = ds_cpd_handle_one_wrap(dca->dca_rpc, dcsh, dcde,
						    dcsrs, ioc, &dlh->dlh_handle);
		}

comp:
		if (comp_cb != NULL)
			comp_cb(dlh, idx, rc);

		return rc;
	}

	/* Dispatch CPD RPC and handle sub requests remotely */
	return ds_obj_cpd_dispatch(dlh, arg, idx, comp_cb);
}

static void
ds_obj_dtx_leader(struct daos_cpd_args *dca)
{
	struct dtx_leader_handle	*dlh = NULL;
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

	dcsh = ds_obj_cpd_get_head(dca->dca_rpc, dca->dca_idx);

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
		dtx_flags |= DTX_EPOCH_OWNER;
		/*
		 * In this case, writes to local RDGs can use the chosen epoch
		 * without any uncertainty. This optimization is left to future
		 * work.
		 */
	}

	D_ASSERT(dcsh->dcsh_epoch.oe_value != 0);
	D_ASSERT(dcsh->dcsh_epoch.oe_value != DAOS_EPOCH_MAX);

	if (oci->oci_flags & ORF_RESEND) {
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

	dcde = ds_obj_cpd_get_ents(dca->dca_rpc, dca->dca_idx, 0);
	dcsrs = ds_obj_cpd_get_reqs(dca->dca_rpc, dca->dca_idx);
	tgts = ds_obj_cpd_get_tgts(dca->dca_rpc, dca->dca_idx);
	req_cnt = ds_obj_cpd_get_reqs_cnt(dca->dca_rpc, dca->dca_idx);
	tgt_cnt = ds_obj_cpd_get_tgts_cnt(dca->dca_rpc, dca->dca_idx);

	if (dcde == NULL || dcsrs == NULL || tgts == NULL ||
	    req_cnt < 0 || tgt_cnt < 0)
		D_GOTO(out, rc = -DER_INVAL);

	/* Refuse any modification with old epoch. */
	if (dcde->dcde_write_cnt != 0 &&
	    dcsh->dcsh_epoch.oe_value < dss_get_start_epoch())
		D_GOTO(out, rc = -DER_TX_RESTART);

	/* 'tgts[0]' is for current dtx leader. */
	if (tgt_cnt == 1)
		tgts = NULL;
	else
		tgts++;

	if (tgt_cnt <= 1 && dcde->dcde_write_cnt == 1 && dcde->dcde_read_cnt == 0)
		dtx_flags |= DTX_SOLO;
	if (flags & ORF_RESEND)
		dtx_flags |= DTX_PREPARED;
	else
		dtx_flags &= ~DTX_PREPARED;

	rc = dtx_leader_begin(dca->dca_ioc->ioc_vos_coh, &dcsh->dcsh_xid, &dcsh->dcsh_epoch,
			      dcde->dcde_write_cnt, oci->oci_map_ver, &dcsh->dcsh_leader_oid,
			      NULL /* dti_cos */, 0 /* dti_cos_cnt */, tgts, tgt_cnt - 1,
			      dtx_flags, dcsh->dcsh_mbs, NULL /* dce */, &dlh);
	if (rc != 0)
		goto out;

	exec_arg.rpc = dca->dca_rpc;
	exec_arg.ioc = dca->dca_ioc;
	exec_arg.args = dca;
	exec_arg.flags = flags;

	/* Execute the operation on all targets */
	rc = dtx_leader_exec_ops(dlh, obj_obj_dtx_leader, NULL, 0, &exec_arg);

	/* Stop the distribute transaction */
	rc = dtx_leader_end(dlh, dca->dca_ioc->ioc_coc, rc);

out:
	DL_CDEBUG(rc != 0 && rc != -DER_INPROGRESS && rc != -DER_TX_RESTART && rc != -DER_AGAIN,
		  DLOG_ERR, DB_IO, rc, "Handled DTX " DF_DTI " on leader, idx %u",
		  DP_DTI(&dcsh->dcsh_xid), dca->dca_idx);

	if (rc == -DER_AGAIN) {
		oci->oci_flags |= ORF_RESEND;
		need_abort = true;
		ABT_thread_yield();
		goto again;
	}

	if (rc != 0 && need_abort) {
		struct dtx_entry	 dte;
		int			 rc1;

		dte.dte_xid = dcsh->dcsh_xid;
		dte.dte_ver = oci->oci_map_ver;
		dte.dte_refs = 1;
		dte.dte_mbs = dcsh->dcsh_mbs;
		rc1 = dtx_abort(dca->dca_ioc->ioc_coc, &dte, dcsh->dcsh_epoch.oe_value);
		if (rc1 != 0 && rc1 != -DER_NONEXIST)
			D_WARN("Failed to abort DTX "DF_DTI": "DF_RC"\n",
			       DP_DTI(&dcsh->dcsh_xid), DP_RC(rc1));
	}

	ds_obj_cpd_set_sub_result(oco, dca->dca_idx, rc,
				  dcsh->dcsh_epoch.oe_value);
}

static void
ds_obj_dtx_leader_ult(void *arg)
{
	struct daos_cpd_args	*dca = arg;
	int			 rc;

	ds_obj_dtx_leader(dca);

	rc = ABT_future_set(dca->dca_future, NULL);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_set failed %d.\n", rc);
}

static int
ds_obj_cpd_body_prep(struct daos_cpd_bulk *dcb, uint32_t type, uint32_t nr)
{
	int	rc = 0;

	if (dcb->dcb_size == 0)
		D_GOTO(out, rc = -DER_INVAL);

	D_ASSERT(dcb->dcb_iov.iov_buf == NULL);

	D_ALLOC(dcb->dcb_iov.iov_buf, dcb->dcb_size);
	if (dcb->dcb_iov.iov_buf == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	dcb->dcb_iov.iov_buf_len = dcb->dcb_size;
	dcb->dcb_iov.iov_len = dcb->dcb_size;

	dcb->dcb_sgl.sg_nr = 1;
	dcb->dcb_sgl.sg_nr_out = 1;
	dcb->dcb_sgl.sg_iovs = &dcb->dcb_iov;

	dcb->dcb_type = type;
	dcb->dcb_item_nr = nr;

out:
	return rc;
}

/* Handle the bulk for CPD RPC body. */
static int
ds_obj_cpd_body_bulk(crt_rpc_t *rpc, struct obj_io_context *ioc, bool leader,
		     struct daos_cpd_bulk ***p_dcbs, uint32_t *dcb_nr)
{
	struct obj_cpd_in		 *oci = crt_req_get(rpc);
	struct daos_cpd_bulk		**dcbs = NULL;
	struct daos_cpd_bulk		 *dcb = NULL;
	crt_bulk_t			 *bulks = NULL;
	d_sg_list_t			**sgls = NULL;
	struct daos_cpd_sub_head	 *dcsh;
	struct daos_cpd_disp_ent	 *dcde;
	struct daos_cpd_req_idx		 *dcri;
	void				 *end;
	uint32_t			  total = 0;
	uint32_t			  count = 0;
	int				  rc = 0;
	int				  i;
	int				  j;

	total += oci->oci_sub_heads.ca_count;
	total += oci->oci_sub_reqs.ca_count;
	total += oci->oci_disp_ents.ca_count;
	if (leader)
		total += oci->oci_disp_tgts.ca_count;

	D_ALLOC_ARRAY(dcbs, total);
	if (dcbs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	*p_dcbs = dcbs;
	*dcb_nr = total;

	for (i = 0; i < oci->oci_sub_reqs.ca_count; i++) {
		dcb = ds_obj_cpd_get_reqs_bulk(rpc, i);
		if (dcb != NULL) {
			rc = ds_obj_cpd_body_prep(dcb, DCST_BULK_REQ,
						  ds_obj_cpd_get_reqs_cnt(rpc, i));
			if (rc != 0)
				goto out;

			dcbs[count++] = dcb;
		}
	}

	for (i = 0; i < oci->oci_sub_heads.ca_count; i++) {
		dcb = ds_obj_cpd_get_head_bulk(rpc, i);
		if (dcb != NULL) {
			rc = ds_obj_cpd_body_prep(dcb, DCST_BULK_HEAD,
						  ds_obj_cpd_get_head_cnt(rpc, i));
			if (rc != 0)
				goto out;

			dcbs[count++] = dcb;
		}
	}

	for (i = 0; i < oci->oci_disp_ents.ca_count; i++) {
		dcb = ds_obj_cpd_get_ents_bulk(rpc, i);
		if (dcb != NULL) {
			rc = ds_obj_cpd_body_prep(dcb, DCST_BULK_ENT,
						  ds_obj_cpd_get_ents_cnt(rpc, i));
			if (rc != 0)
				goto out;

			dcbs[count++] = dcb;
		}
	}

	if (leader) {
		for (i = 0; i < oci->oci_disp_tgts.ca_count; i++) {
			dcb = ds_obj_cpd_get_tgts_bulk(rpc, i);
			if (dcb != NULL) {
				rc = ds_obj_cpd_body_prep(dcb, DCST_BULK_TGT,
							  ds_obj_cpd_get_tgts_cnt(rpc, i));
				if (rc != 0)
					goto out;

				dcbs[count++] = dcb;
			}
		}
	}

	/* no bulk for this CPD RPC body. */
	if (count == 0)
		D_GOTO(out, rc = 0);

	D_ALLOC_ARRAY(bulks, count);
	if (bulks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC_ARRAY(sgls, count);
	if (sgls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < count; i++) {
		bulks[i] = *dcbs[i]->dcb_bulk;
		sgls[i] = &dcbs[i]->dcb_sgl;
	}

	rc = obj_bulk_transfer(rpc, CRT_BULK_GET, ORF_BULK_BIND, bulks, NULL, NULL, DAOS_HDL_INVAL,
			       sgls, count, count, NULL);
	if (rc != 0)
		goto out;

	for (i = 0; i < count; i++) {
		switch (dcbs[i]->dcb_type) {
		case DCST_BULK_HEAD:
			dcsh = &dcbs[i]->dcb_head;
			dcsh->dcsh_mbs = dcbs[i]->dcb_iov.iov_buf;
			break;
		case DCST_BULK_REQ:
			rc = crt_proc_create(dss_get_module_info()->dmi_ctx,
					     dcbs[i]->dcb_iov.iov_buf, dcbs[i]->dcb_iov.iov_len,
					     CRT_PROC_DECODE, &dcbs[i]->dcb_proc);
			if (rc != 0)
				goto out;

			D_ALLOC_ARRAY(dcbs[i]->dcb_reqs, dcbs[i]->dcb_item_nr);
			if (dcbs[i]->dcb_reqs == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			for (j = 0; j < dcbs[i]->dcb_item_nr; j++) {
				rc = crt_proc_struct_daos_cpd_sub_req(dcbs[i]->dcb_proc,
								      CRT_PROC_DECODE,
								      &dcbs[i]->dcb_reqs[j], true);
				if (rc != 0)
					goto out;
			}
			break;
		case DCST_BULK_ENT:
			dcde = dcbs[i]->dcb_iov.iov_buf;
			dcri = dcbs[i]->dcb_iov.iov_buf + sizeof(*dcde) * dcbs[i]->dcb_item_nr;
			end = dcbs[i]->dcb_iov.iov_buf + dcbs[i]->dcb_iov.iov_len;

			for (j = 0; j < dcbs[i]->dcb_item_nr; j++) {
				dcde[j].dcde_reqs = dcri;
				dcri += dcde[j].dcde_read_cnt + dcde[j].dcde_write_cnt;
				D_ASSERT((void *)dcri <= end);
			}
			break;
		default:
			break;
		}
	}

out:
	if (rc != 0)
		D_ERROR("Failed to bulk transfer CPD RPC body for %p: "DF_RC"\n", rpc, DP_RC(rc));

	D_FREE(sgls);
	D_FREE(bulks);

	return rc;
}

void
ds_obj_cpd_handler(crt_rpc_t *rpc)
{
	struct obj_cpd_in	*oci = crt_req_get(rpc);
	struct obj_cpd_out	*oco = crt_reply_get(rpc);
	struct daos_cpd_args	*dcas = NULL;
	struct obj_io_context	 ioc;
	ABT_future		 future = ABT_FUTURE_NULL;
	struct daos_cpd_bulk   **dcbs = NULL;
	uint32_t		 dcb_nr = 0;
	int			 tx_count = oci->oci_sub_heads.ca_count;
	int			 rc = 0;
	int			 i;
	int			 j;
	bool			 leader;

	D_ASSERT(oci != NULL);

	if (oci->oci_flags & ORF_LEADER)
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
				oci->oci_co_hdl, oci->oci_co_uuid, rpc, &ioc);
	if (rc != 0)
		goto reply;


	rc = obj_inflight_io_check(ioc.ioc_coc, opc_get(rpc->cr_opc), oci->oci_map_ver,
				   oci->oci_flags);
	if (rc != 0)
		goto reply;

	if (!leader) {
		if (tx_count != 1 || oci->oci_sub_reqs.ca_count != 1 ||
		    oci->oci_disp_ents.ca_count != 1 || oci->oci_disp_tgts.ca_count != 0) {
			D_ERROR("Unexpected CPD RPC format for non-leader: "
				"head %u, req set %lu, disp %lu, tgts %lu\n",
				tx_count, oci->oci_sub_reqs.ca_count,
				oci->oci_disp_ents.ca_count, oci->oci_disp_tgts.ca_count);

			D_GOTO(reply, rc = -DER_PROTO);
		}
	} else {
		if (tx_count != oci->oci_sub_reqs.ca_count ||
		    tx_count != oci->oci_disp_ents.ca_count ||
		    tx_count != oci->oci_disp_tgts.ca_count || tx_count == 0) {
			D_ERROR("Unexpected CPD RPC format for leader: "
				"head %u, req set %lu, disp %lu, tgts %lu\n",
				tx_count, oci->oci_sub_reqs.ca_count,
				oci->oci_disp_ents.ca_count, oci->oci_disp_tgts.ca_count);

			D_GOTO(reply, rc = -DER_PROTO);
		}
	}

	rc = ds_obj_cpd_body_bulk(rpc, &ioc, leader, &dcbs, &dcb_nr);
	if (rc != 0)
		goto reply;

	if (!leader) {
		oco->oco_sub_rets.ca_arrays = NULL;
		oco->oco_sub_rets.ca_count = 0;
		rc = ds_obj_dtx_follower(rpc, &ioc);

		D_GOTO(reply, rc);
	}

	D_ALLOC(oco->oco_sub_rets.ca_arrays, sizeof(int32_t) * tx_count);
	if (oco->oco_sub_rets.ca_arrays == NULL)
		D_GOTO(reply, rc = -DER_NOMEM);

	D_ALLOC(oco->oco_sub_epochs.ca_arrays, sizeof(int64_t) * tx_count);
	if (oco->oco_sub_epochs.ca_arrays == NULL)
		D_GOTO(reply, rc = -DER_NOMEM);

	oco->oco_sub_rets.ca_count = tx_count;
	oco->oco_sub_epochs.ca_count = tx_count;

	if (tx_count == 1) {
		struct daos_cpd_args	dca;

		dca.dca_ioc = &ioc;
		dca.dca_rpc = rpc;
		dca.dca_future = ABT_FUTURE_NULL;
		dca.dca_idx = 0;
		ds_obj_dtx_leader(&dca);

		D_GOTO(reply, rc = 0);
	}

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
			dcsh = ds_obj_cpd_get_head(rpc, i);
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
	if (dcbs != NULL) {
		for (i = 0; i < dcb_nr; i++) {
			if (dcbs[i] == NULL)
				continue;

			if (dcbs[i]->dcb_reqs != NULL) {
				D_ASSERT(dcbs[i]->dcb_proc != NULL);

				crt_proc_reset(dcbs[i]->dcb_proc, dcbs[i]->dcb_iov.iov_buf,
					       dcbs[i]->dcb_iov.iov_len, CRT_PROC_FREE);
				for (j = 0; j < dcbs[i]->dcb_item_nr; j++) {
					crt_proc_struct_daos_cpd_sub_req(dcbs[i]->dcb_proc,
									 CRT_PROC_FREE,
									 &dcbs[i]->dcb_reqs[j],
									 true);
				}
				D_FREE(dcbs[i]->dcb_reqs);
			}

			if (dcbs[i]->dcb_proc != NULL)
				crt_proc_destroy(dcbs[i]->dcb_proc);

			D_FREE(dcbs[i]->dcb_iov.iov_buf);
		}
		D_FREE(dcbs);
	}
	obj_cpd_reply(rpc, rc, ioc.ioc_map_ver);
	obj_ioc_end(&ioc, rc);
}

void
ds_obj_key2anchor_handler(crt_rpc_t *rpc)
{
	struct obj_key2anchor_in	*oki;
	struct obj_key2anchor_out	*oko;
	struct obj_io_context		ioc;
	daos_key_t			*akey = NULL;
	int				rc = 0;

	oki = crt_req_get(rpc);
	D_ASSERT(oki != NULL);
	oko = crt_reply_get(rpc);
	D_ASSERT(oko != NULL);

	rc = obj_ioc_begin(oki->oki_oid.id_pub, oki->oki_map_ver,
			   oki->oki_pool_uuid, oki->oki_co_hdl,
			   oki->oki_co_uuid, rpc, oki->oki_flags, &ioc);
	if (rc)
		D_GOTO(out, rc);

	D_DEBUG(DB_IO, "rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_X64", pmv %u/%u dti "DF_DTI".\n",
		rpc, DAOS_OBJ_RPC_KEY2ANCHOR, DP_UOID(oki->oki_oid), DP_KEY(&oki->oki_dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id, oki->oki_epoch,
		oki->oki_map_ver, ioc.ioc_map_ver, DP_DTI(&oki->oki_dti));

	rc = process_epoch(&oki->oki_epoch, NULL, &oki->oki_flags);
	if (rc == PE_OK_LOCAL)
		oki->oki_flags &= ~ORF_EPOCH_UNCERTAIN;

	if (oki->oki_akey.iov_len > 0)
		akey = &oki->oki_akey;
	rc = vos_obj_key2anchor(ioc.ioc_vos_coh, oki->oki_oid, &oki->oki_dkey, akey,
				&oko->oko_anchor);

out:
	obj_reply_set_status(rpc, rc);
	obj_reply_map_version_set(rpc, ioc.ioc_map_ver);
	obj_ioc_end(&ioc, rc);
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
}

void
ds_obj_coll_punch_handler(crt_rpc_t *rpc)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct dtx_leader_handle	*dlh = NULL;
	struct obj_coll_punch_in	*ocpi = crt_req_get(rpc);
	struct obj_dtx_mbs		*odm = &ocpi->ocpi_odm;
	struct ds_obj_exec_arg		 exec_arg = { 0 };
	struct obj_io_context		 ioc = { 0 };
	struct dtx_coll_entry		*dce = NULL;
	struct daos_coll_target		*dcts = NULL;
	d_iov_t				 iov = { 0 };
	crt_proc_t			 proc = NULL;
	uint32_t			 dct_nr = 0;
	uint32_t			 flags = 0;
	uint32_t			 dtx_flags = DTX_TGT_COLL;
	uint32_t			 version = 0;
	uint32_t			 max_ver = 0;
	struct dtx_epoch		 epoch;
	daos_epoch_t			 tmp;
	int				 rc;
	int				 rc1;
	int				 i;
	bool				 need_abort = false;

	D_DEBUG(DB_IO, "(%s) handling collective punch RPC %p for obj "
		DF_UOID" on XS %u/%u epc "DF_X64" pmv %u, with dti "
		DF_DTI", forward width %u, forward depth %u, flags %x\n",
		(ocpi->ocpi_flags & ORF_LEADER) ? "leader" :
		(ocpi->ocpi_tgts.ca_count == 1 ? "non-leader" : "relay-engine"),
		rpc, DP_UOID(ocpi->ocpi_oid), dmi->dmi_xs_id, dmi->dmi_tgt_id,
		ocpi->ocpi_epoch, ocpi->ocpi_map_ver, DP_DTI(&ocpi->ocpi_xid),
		ocpi->ocpi_disp_width, ocpi->ocpi_disp_depth, ocpi->ocpi_flags);

	D_ASSERT(dmi->dmi_xs_id != 0);

	rc = obj_ioc_begin(ocpi->ocpi_oid.id_pub, ocpi->ocpi_map_ver, ocpi->ocpi_po_uuid,
			   ocpi->ocpi_co_hdl, ocpi->ocpi_co_uuid, rpc, ocpi->ocpi_flags, &ioc);
	if (rc != 0)
		goto out;

	if (ocpi->ocpi_flags & ORF_LEADER && ocpi->ocpi_bulk_tgt_sz > 0) {
		rc = obj_coll_punch_bulk(rpc, &iov, &proc, &dcts, &dct_nr);
		if (rc != 0)
			goto out;
	} else {
		dcts = ocpi->ocpi_tgts.ca_arrays;
		dct_nr = ocpi->ocpi_tgts.ca_count;
	}

	rc = obj_coll_punch_prep(ocpi, dcts, dct_nr, &dce);
	if (rc != 0)
		goto out;

	if (ocpi->ocpi_flags & ORF_LEADER) {
		rc = process_epoch(&ocpi->ocpi_epoch, NULL /* epoch_first */, &ocpi->ocpi_flags);
		if (rc == PE_OK_LOCAL) {
			ocpi->ocpi_flags &= ~ORF_EPOCH_UNCERTAIN;
			dtx_flags |= DTX_EPOCH_OWNER;
		}
	} else if (dct_nr == 1) {
		rc = obj_coll_local(rpc, dcts[0].dct_shards, dce, &version, &ioc, NULL,
				    odm->odm_mbs, obj_coll_tgt_punch);
		goto out;
	}

	version = ocpi->ocpi_map_ver;
	max_ver = ocpi->ocpi_map_ver;

	if (ocpi->ocpi_flags & ORF_DTX_SYNC)
		dtx_flags |= DTX_SYNC;

	if (!(ocpi->ocpi_flags & ORF_LEADER))
		dtx_flags |= DTX_RELAY;

	if (ocpi->ocpi_flags & ORF_RESEND) {

again:
		if (!(ocpi->ocpi_flags & ORF_LEADER) || (flags & ORF_RESEND))
			tmp = ocpi->ocpi_epoch;
		else
			tmp = 0;
		version = ocpi->ocpi_map_ver;
		rc      = dtx_handle_resend(ioc.ioc_vos_coh, &ocpi->ocpi_xid, &tmp, &version);
		switch (rc) {
		case -DER_ALREADY:
			D_GOTO(out, rc = 0);
		case 0:
			ocpi->ocpi_epoch = tmp;
			flags |= ORF_RESEND;
			/* TODO: Also recovery the epoch uncertainty. */
			break;
		case -DER_MISMATCH:
			rc = vos_dtx_abort(ioc.ioc_vos_coh, &ocpi->ocpi_xid, tmp);
			if (rc < 0 && rc != -DER_NONEXIST)
				D_GOTO(out, rc);
			/* Fall through */
		case -DER_NONEXIST:
			flags = 0;
			break;
		default:
			D_GOTO(out, rc);
		}

		dce->dce_ver = version;
	}

	epoch.oe_value = ocpi->ocpi_epoch;
	epoch.oe_first = epoch.oe_value;
	epoch.oe_flags = orf_to_dtx_epoch_flags(ocpi->ocpi_flags);

	if (flags & ORF_RESEND)
		dtx_flags |= DTX_PREPARED;
	else
		dtx_flags &= ~DTX_PREPARED;

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.flags |= flags;
	exec_arg.coll_shards = dcts[0].dct_shards;
	exec_arg.coll_tgts = dcts;
	obj_coll_disp_init(dct_nr, ocpi->ocpi_max_tgt_sz,
			   sizeof(*ocpi) + sizeof(*odm->odm_mbs) + odm->odm_mbs->dm_data_size,
			   1 /* start, [0] is for current engine */, ocpi->ocpi_disp_width,
			   &exec_arg.coll_cur);

	rc = dtx_leader_begin(ioc.ioc_vos_coh, &odm->odm_xid, &epoch,
			      dcts[0].dct_shards[dmi->dmi_tgt_id].dcs_nr, version,
			      &ocpi->ocpi_oid, NULL /* dti_cos */, 0 /* dti_cos_cnt */,
			      NULL /* tgts */, exec_arg.coll_cur.grp_nr /* tgt_cnt */,
			      dtx_flags, odm->odm_mbs, dce, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID ": Failed to start DTX for collective punch: "DF_RC"\n",
			DP_UOID(ocpi->ocpi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Execute the operation on all shards */
	rc = dtx_leader_exec_ops(dlh, obj_coll_punch_disp, NULL, 0, &exec_arg);

	if (max_ver < dlh->dlh_rmt_ver)
		max_ver = dlh->dlh_rmt_ver;

	rc = dtx_leader_end(dlh, ioc.ioc_coc, rc);

	if (dtx_flags & DTX_RELAY)
		goto out;

	switch (rc) {
	case -DER_TX_RESTART:
		ocpi->ocpi_epoch = d_hlc_get();
		exec_arg.flags |= ORF_RESEND;
		flags = ORF_RESEND;
		goto again;
	case -DER_AGAIN:
		need_abort = true;
		exec_arg.flags |= ORF_RESEND;
		flags = ORF_RESEND;
		ABT_thread_yield();
		goto again;
	default:
		break;
	}

out:
	if (rc != 0 && need_abort) {
		rc1 = dtx_coll_abort(ioc.ioc_coc, dce, ocpi->ocpi_epoch);
		if (rc1 != 0 && rc1 != -DER_NONEXIST)
			D_WARN("Failed to collective abort DTX "DF_DTI": "DF_RC"\n",
			       DP_DTI(&ocpi->ocpi_xid), DP_RC(rc1));
	}

	if (max_ver < ioc.ioc_map_ver)
		max_ver = ioc.ioc_map_ver;

	if (max_ver < version)
		max_ver = version;

	DL_CDEBUG(rc != 0 && rc != -DER_INPROGRESS && rc != -DER_TX_RESTART, DLOG_ERR, DB_IO, rc,
		  "(%s) handled collective punch RPC %p for obj "DF_UOID" on XS %u/%u in "DF_UUID"/"
		  DF_UUID"/"DF_UUID" with epc "DF_X64", pmv %u/%u, dti "DF_DTI", bulk_tgt_sz %u, "
		  "bulk_tgt_nr %u, tgt_nr %u, forward width %u, forward depth %u, flags %x",
		  (ocpi->ocpi_flags & ORF_LEADER) ? "leader" :
		  (ocpi->ocpi_tgts.ca_count == 1 ? "non-leader" : "relay-engine"), rpc,
		  DP_UOID(ocpi->ocpi_oid), dmi->dmi_xs_id, dmi->dmi_tgt_id,
		  DP_UUID(ocpi->ocpi_po_uuid), DP_UUID(ocpi->ocpi_co_hdl),
		  DP_UUID(ocpi->ocpi_co_uuid), ocpi->ocpi_epoch,
		  ocpi->ocpi_map_ver, max_ver, DP_DTI(&ocpi->ocpi_xid), ocpi->ocpi_bulk_tgt_sz,
		  ocpi->ocpi_bulk_tgt_nr, (unsigned int)ocpi->ocpi_tgts.ca_count,
		  ocpi->ocpi_disp_width, ocpi->ocpi_disp_depth, ocpi->ocpi_flags);

	obj_punch_complete(rpc, rc, max_ver);

	dtx_coll_entry_put(dce);
	if (proc != NULL) {
		D_ASSERT(dcts != NULL);

		crt_proc_reset(proc, iov.iov_buf, iov.iov_len, CRT_PROC_FREE);
		for (i = 0; i < dct_nr; i++)
			crt_proc_struct_daos_coll_target(proc, CRT_PROC_FREE, &dcts[i]);
		crt_proc_destroy(proc);
		D_FREE(dcts);
		daos_iov_free(&iov);
	}

	/* It is no matter even if obj_ioc_begin() was not called. */
	obj_ioc_end(&ioc, rc);
}

void
ds_obj_coll_query_handler(crt_rpc_t *rpc)
{
	struct dss_module_info		*dmi = dss_get_module_info();
	struct obj_coll_query_in	*ocqi = crt_req_get(rpc);
	struct obj_coll_query_out	*ocqo = crt_reply_get(rpc);
	struct daos_coll_target		*dcts;
	struct dtx_leader_handle	*dlh = NULL;
	struct ds_obj_exec_arg		 exec_arg = { 0 };
	struct dtx_coll_entry		 dce = { 0 };
	struct obj_tgt_query_args	*otqas = NULL;
	struct obj_tgt_query_args	*otqa = NULL;
	struct obj_io_context		 ioc = { 0 };
	struct dtx_epoch		 epoch = { 0 };
	uint32_t			 dct_nr;
	uint32_t			 version = 0;
	uint32_t			 tgt_id = dmi->dmi_tgt_id;
	d_rank_t			 myrank = dss_self_rank();
	int				 rc = 0;
	int				 i;

	D_ASSERT(ocqi != NULL);
	D_ASSERT(ocqo != NULL);

	D_DEBUG(DB_IO, "Handling collective query RPC %p %s forwarding for obj "
		DF_UOID" on rank %d XS %u/%u epc "DF_X64" pmv %u, with dti "DF_DTI
		", dct_nr %u, forward width %u, forward depth %u\n",
		rpc, ocqi->ocqi_tgts.ca_count <= 1 ? "without" : "with", DP_UOID(ocqi->ocqi_oid),
		myrank, dmi->dmi_xs_id, tgt_id, ocqi->ocqi_epoch, ocqi->ocqi_map_ver,
		DP_DTI(&ocqi->ocqi_xid), (unsigned int)ocqi->ocqi_tgts.ca_count,
		ocqi->ocqi_disp_width, ocqi->ocqi_disp_depth);

	D_ASSERT(dmi->dmi_xs_id != 0);

	if (unlikely(ocqi->ocqi_tgts.ca_count <= 0 || ocqi->ocqi_tgts.ca_arrays == NULL))
		D_GOTO(out, rc = -DER_INVAL);

	dcts = ocqi->ocqi_tgts.ca_arrays;
	dct_nr = ocqi->ocqi_tgts.ca_count;

	if (unlikely(dcts[0].dct_bitmap == NULL || dcts[0].dct_bitmap_sz == 0 ||
		     dcts[0].dct_shards == NULL || dcts[0].dct_tgt_nr == 0))
		D_GOTO(out, rc = -DER_INVAL);

	rc = obj_ioc_begin(ocqi->ocqi_oid.id_pub, ocqi->ocqi_map_ver, ocqi->ocqi_po_uuid,
			   ocqi->ocqi_co_hdl, ocqi->ocqi_co_uuid, rpc, ocqi->ocqi_flags, &ioc);
	if (rc != 0)
		goto out;

	rc = process_epoch(&ocqi->ocqi_epoch, &ocqi->ocqi_epoch_first, &ocqi->ocqi_flags);
	if (rc == PE_OK_LOCAL)
		ocqi->ocqi_flags &= ~ORF_EPOCH_UNCERTAIN;

	D_ALLOC_ARRAY(otqas, dss_tgt_nr);
	if (otqas == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < dss_tgt_nr; i++)
		otqas[i].otqa_raw_recx = 1;

	otqa = &otqas[tgt_id];

	dce.dce_xid = ocqi->ocqi_xid;
	dce.dce_ver = ocqi->ocqi_map_ver;
	dce.dce_refs = 1;
	dce.dce_bitmap = dcts[0].dct_bitmap;
	dce.dce_bitmap_sz = dcts[0].dct_bitmap_sz;

	if (ocqi->ocqi_tgts.ca_count == 1) {
		rc = obj_coll_local(rpc, dcts[0].dct_shards, &dce, &version, &ioc, NULL, otqas,
				    obj_coll_tgt_query);
		if (otqa->otqa_completed && otqa->otqa_keys_allocated &&
		    (rc == 0 || rc == -DER_NONEXIST)) {
			D_ASSERT(ioc.ioc_began);
			rc = obj_coll_query_merge_tgts(ocqi, &ioc.ioc_oca, otqas, dce.dce_bitmap,
						       dce.dce_bitmap_sz, tgt_id, -DER_NONEXIST);
		}

		goto out;
	}

	version = ioc.ioc_map_ver;

	epoch.oe_value = ocqi->ocqi_epoch;
	epoch.oe_first = ocqi->ocqi_epoch_first;
	epoch.oe_flags = orf_to_dtx_epoch_flags(ocqi->ocqi_flags);

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.args = otqas;
	exec_arg.coll_shards = dcts[0].dct_shards;
	exec_arg.coll_tgts = dcts;
	obj_coll_disp_init(dct_nr, ocqi->ocqi_max_tgt_sz, sizeof(*ocqi),
			   1 /* start, [0] is for current engine */, ocqi->ocqi_disp_width,
			   &exec_arg.coll_cur);

	rc = dtx_leader_begin(ioc.ioc_vos_coh, &ocqi->ocqi_xid, &epoch, 0, ocqi->ocqi_map_ver,
			      &ocqi->ocqi_oid, NULL /* dti_cos */, 0 /* dti_cos_cnt */,
			      NULL /* tgts */, exec_arg.coll_cur.grp_nr /* tgt_cnt */,
			      DTX_TGT_COLL | DTX_RELAY, NULL /* mbs */, &dce, &dlh);
	if (rc != 0)
		goto out;

	rc = dtx_leader_exec_ops(dlh, obj_coll_query_disp, obj_coll_query_agg_cb, -DER_NONEXIST,
				 &exec_arg);

	if (version < dlh->dlh_rmt_ver)
		version = dlh->dlh_rmt_ver;

	rc = dtx_leader_end(dlh, ioc.ioc_coc, rc);

out:
	D_DEBUG(DB_IO, "Handled collective query RPC %p %s forwarding for obj "DF_UOID
		" on rank %u XS %u/%u epc "DF_X64" pmv %u, with dti "DF_DTI", dct_nr %u, "
		"forward width %u, forward depth %u\n: "DF_RC"\n", rpc,
		ocqi->ocqi_tgts.ca_count <= 1 ? "without" : "with", DP_UOID(ocqi->ocqi_oid),
		myrank, dmi->dmi_xs_id, tgt_id, ocqi->ocqi_epoch, ocqi->ocqi_map_ver,
		DP_DTI(&ocqi->ocqi_xid), (unsigned int)ocqi->ocqi_tgts.ca_count,
		ocqi->ocqi_disp_width, ocqi->ocqi_disp_depth, DP_RC(rc));

	obj_reply_set_status(rpc, rc);
	obj_reply_map_version_set(rpc, version);
	ocqo->ocqo_epoch = epoch.oe_value;

	if (rc == 0 || rc == -DER_NONEXIST) {
		D_ASSERT(otqa != NULL);

		ocqo->ocqo_shard = otqa->otqa_shard;
		ocqo->ocqo_recx = otqa->otqa_recx;
		ocqo->ocqo_max_epoch = otqa->otqa_max_epoch;
		if (otqa->otqa_keys_allocated) {
			ocqo->ocqo_dkey = otqa->otqa_dkey_copy;
			ocqo->ocqo_akey = otqa->otqa_akey_copy;
		}
		if (otqa->otqa_raw_recx)
			ocqo->ocqo_flags |= OCRF_RAW_RECX;
	}

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));

	/* Keep otqas until RPC replied, because the reply may use some keys in otqas array. */
	if (otqas != NULL) {
		for (i = 0; i < dss_tgt_nr; i++)
			obj_tgt_query_cleanup(&otqas[i]);
		D_FREE(otqas);
	}

	obj_ioc_end(&ioc, rc);
}
