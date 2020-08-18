/**
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
#include <daos_srv/daos_server.h>
#include <daos_srv/dtx_srv.h>
#include <daos_srv/security.h>
#include <daos/checksum.h>
#include <gurt/mem.h>
#include "daos_srv/srv_csum.h"
#include "obj_rpc.h"
#include "obj_internal.h"

static int
obj_verify_bio_csum(crt_rpc_t *rpc, daos_iod_t *iods,
		    struct dcs_iod_csums *iod_csums, struct bio_desc *biod,
		    struct daos_csummer *csummer);

/* For single RDG based DTX, parse DTX participants information
 * from the client given dispatch targets information that does
 * NOT contains the original leader information.
 */
static int
obj_gen_dtx_mbs(struct daos_shard_tgt *tgts, uint32_t *tgt_cnt,
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
	}

	*p_mbs = mbs;

	return 0;
}

/**
 * After bulk finish, let's send reply, then release the resource.
 */
static int
obj_rw_complete(crt_rpc_t *rpc, unsigned int map_version,
		daos_handle_t ioh, int status, struct dtx_handle *dth)
{
	struct obj_rw_in	*orwi = crt_req_get(rpc);
	int			 rc;

	if (!daos_handle_is_inval(ioh)) {
		bool update = obj_rpc_is_update(rpc);

		if (update) {
			rc = dtx_sub_init(dth, &orwi->orw_oid,
					  orwi->orw_dkey_hash);
			if (rc == 0)
				rc = vos_update_end(ioh, map_version,
						&orwi->orw_dkey, status, dth);
		} else {
			rc = vos_fetch_end(ioh, status);
		}

		if (rc != 0) {
			D_CDEBUG(rc == -DER_REC2BIG, DLOG_DBG, DLOG_ERR,
				 DF_UOID " %s end failed: %d\n",
				 DP_UOID(orwi->orw_oid),
				 update ? "Update" : "Fetch", rc);
			if (status == 0)
				status = rc;
		}
	}

	return status;
}

static void
obj_rw_reply(crt_rpc_t *rpc, int status, uint32_t map_version, uint64_t epoch,
	     struct ds_cont_child *cont)
{
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	int			 rc;
	int			 i;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	orwo->orw_epoch = epoch;

	D_DEBUG(DB_IO, "rpc %p opc %d send reply, pmv %d, epoch "DF_U64
		", status %d\n", rpc, opc_get(rpc->cr_opc), map_version, epoch,
		status);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));

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

		if (cont) {
			daos_csummer_free_ic(cont->sc_csummer,
				&orwo->orw_iod_csums.ca_arrays);
			orwo->orw_iod_csums.ca_count = 0;
		}

		daos_recx_ep_list_free(orwo->orw_rels.ca_arrays,
				       orwo->orw_rels.ca_count);
	}
}

struct obj_bulk_args {
	int		bulks_inflight;
	int		result;
	ABT_eventual	eventual;
};

static int
obj_bulk_comp_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct obj_bulk_args	*arg;
	struct crt_bulk_desc	*bulk_desc;
	crt_rpc_t		*rpc;
	crt_bulk_t		 local_bulk_hdl;

	if (cb_info->bci_rc != 0)
		D_ERROR("bulk transfer failed: %d\n", cb_info->bci_rc);

	bulk_desc = cb_info->bci_bulk_desc;
	local_bulk_hdl = bulk_desc->bd_local_hdl;
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

	crt_bulk_free(local_bulk_hdl);
	crt_req_decref(rpc);
	return cb_info->bci_rc;
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
obj_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op, bool bulk_bind,
		  crt_bulk_t *remote_bulks, uint64_t *remote_offs,
		  daos_handle_t ioh, d_sg_list_t **sgls,
		  struct bio_sglist *bsgls_dup, int sgl_nr)
{
	struct obj_bulk_args	arg = { 0 };
	crt_bulk_opid_t		bulk_opid;
	crt_bulk_perm_t		bulk_perm;
	int			i, rc, *status, ret;

	if (remote_bulks == NULL) {
		D_ERROR("No remote bulks provided\n");
		return -DER_INVAL;
	}

	bulk_perm = bulk_op == CRT_BULK_PUT ? CRT_BULK_RO : CRT_BULK_RW;
	rc = ABT_eventual_create(sizeof(*status), &arg.eventual);
	if (rc != 0)
		return dss_abterr2der(rc);

	D_DEBUG(DB_IO, "bulk_op %d sgl_nr %d\n", bulk_op, sgl_nr);

	arg.bulks_inflight++;
	for (i = 0; i < sgl_nr; i++) {
		d_sg_list_t		*sgl, tmp_sgl;
		struct crt_bulk_desc	 bulk_desc;
		crt_bulk_t		 local_bulk_hdl;
		daos_size_t		 offset;
		unsigned int		 idx = 0;

		if (remote_bulks[i] == NULL)
			continue;

		offset = remote_offs != NULL ? remote_offs[i] : 0;
		if (sgls != NULL) {
			D_ASSERT(bsgls_dup == NULL);
			sgl = sgls[i];
		} else {
			struct bio_sglist *bsgl;
			bool deduped_skip = true;

			D_ASSERT(!daos_handle_is_inval(ioh));
			bsgl = vos_iod_sgl_at(ioh, i);
			if (bsgls_dup) {	/* dedup verify case */
				rc = vos_dedup_dup_bsgl(ioh, bsgl,
							&bsgls_dup[i]);
				if (rc)
					break;
				bsgl = &bsgls_dup[i];
				deduped_skip = false;
			}
			D_ASSERT(bsgl != NULL);

			sgl = &tmp_sgl;
			rc = bio_sgl_convert(bsgl, sgl, deduped_skip);
			if (rc)
				break;
		}

		if (daos_io_bypass & IOBP_SRV_BULK) {
			/* this mode will bypass network bulk transfer and
			 * only copy data from/to dummy buffer. This is for
			 * performance evaluation on low bandwidth network.
			 */
			obj_bulk_bypass(sgl, bulk_op);
			goto next;
		}

		/**
		 * Let's walk through the sgl to check if the iov is empty,
		 * which is usually gotten from punched/empty records (see
		 * akey_fetch()), and skip these empty iov during bulk
		 * transfer to avoid touching the input buffer.
		 */
		while (idx < sgl->sg_nr_out) {
			d_sg_list_t	sgl_sent;
			daos_size_t	length = 0;
			size_t		remote_bulk_size;
			unsigned int	start;

			/**
			 * Skip the punched/empty record, let's also skip the
			 * record in the input buffer instead of memset to 0.
			 */
			while (idx < sgl->sg_nr_out &&
				sgl->sg_iovs[idx].iov_buf == NULL) {
				offset += sgl->sg_iovs[idx].iov_len;
				idx++;
			}

			if (idx == sgl->sg_nr_out)
				break;

			start = idx;
			sgl_sent.sg_iovs = &sgl->sg_iovs[start];
			/* Find the end of the non-empty record */
			while (sgl->sg_iovs[idx].iov_buf != NULL &&
			       idx < sgl->sg_nr_out) {
				length += sgl->sg_iovs[idx].iov_len;
				idx++;
			}

			rc = crt_bulk_get_len(remote_bulks[i],
						&remote_bulk_size);
			if (rc)
				break;

			if (length > remote_bulk_size) {
				D_DEBUG(DLOG_DBG, DF_U64 " > %zu : %d\n",
					length,	remote_bulk_size,
					-DER_OVERFLOW);
				rc = -DER_OVERFLOW;
				break;
			}
			sgl_sent.sg_nr = idx - start;
			sgl_sent.sg_nr_out = idx - start;

			rc = crt_bulk_create(rpc->cr_ctx, &sgl_sent,
					     bulk_perm, &local_bulk_hdl);
			if (rc != 0) {
				D_ERROR("crt_bulk_create %d error (%d).\n",
					i, rc);
				break;
			}

			crt_req_addref(rpc);

			bulk_desc.bd_rpc	= rpc;
			bulk_desc.bd_bulk_op	= bulk_op;
			bulk_desc.bd_remote_hdl	= remote_bulks[i];
			bulk_desc.bd_local_hdl	= local_bulk_hdl;
			bulk_desc.bd_len	= length;
			bulk_desc.bd_remote_off	= offset;
			bulk_desc.bd_local_off	= 0;

			arg.bulks_inflight++;
			if (bulk_bind)
				rc = crt_bulk_bind_transfer(&bulk_desc,
					obj_bulk_comp_cb, &arg, &bulk_opid);
			else
				rc = crt_bulk_transfer(&bulk_desc,
					obj_bulk_comp_cb, &arg, &bulk_opid);
			if (rc < 0) {
				D_ERROR("crt_bulk_transfer %d error (%d).\n",
					i, rc);
				arg.bulks_inflight--;
				crt_bulk_free(local_bulk_hdl);
				crt_req_decref(rpc);
				break;
			}
			offset += length;
		}
	next:
		if (sgls == NULL)
			daos_sgl_fini(sgl, false);
		if (rc)
			break;
	}

	if (--arg.bulks_inflight == 0)
		ABT_eventual_set(arg.eventual, &rc, sizeof(rc));

	ret = ABT_eventual_wait(arg.eventual, (void **)&status);
	if (rc == 0)
		rc = ret ? dss_abterr2der(ret) : *status;

	ABT_eventual_free(&arg.eventual);
	/* After RDMA is done, corrupt the server data */
	if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_DISK)) {
		struct obj_rw_in	*orw = crt_req_get(rpc);
		struct ds_pool		*pool;
		struct bio_sglist	*fbsgl;
		d_sg_list_t		 fsgl;
		int			*fbuffer;

		pool = ds_pool_lookup(orw->orw_pool_uuid);
		if (pool == NULL)
			return -DER_NONEXIST;

		D_DEBUG(DB_IO, "Data corruption after RDMA\n");
		fbsgl = vos_iod_sgl_at(ioh, 0);
		bio_sgl_convert(fbsgl, &fsgl, false);
		fbuffer = (int *)fsgl.sg_iovs[0].iov_buf;
		*fbuffer += 0x2;
		daos_sgl_fini(&fsgl, false);
		ds_pool_put(pool);
	}
	return rc;
}

static int
obj_set_reply_sizes(crt_rpc_t *rpc)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	daos_iod_t		*iods;
	uint64_t		*sizes;
	int			size_count;
	int			i;

	D_ASSERT(obj_rpc_is_fetch(rpc));
	D_ASSERT(orwo != NULL);
	D_ASSERT(orw != NULL);

	iods = orw->orw_iod_array.oia_iods;
	size_count = orw->orw_iod_array.oia_iod_nr;

	if (size_count <= 0) {
		D_ERROR("rpc %p contains invalid sizes count %d for "
			DF_UOID" with epc "DF_U64".\n",
			rpc, size_count, DP_UOID(orw->orw_oid), orw->orw_epoch);
		return -DER_INVAL;
	}

	orwo->orw_iod_sizes.ca_count = size_count;
	D_ALLOC_ARRAY(sizes, size_count);
	if (sizes == NULL)
		return -DER_NOMEM;

	for (i = 0; i < orw->orw_iod_array.oia_iod_nr; i++)
		sizes[i] = iods[i].iod_size;

	orwo->orw_iod_sizes.ca_arrays = sizes;

	D_DEBUG(DB_TRACE, "rpc %p set sizes count as %d for "
		DF_UOID" with epc "DF_U64".\n",
		rpc, size_count, DP_UOID(orw->orw_oid), orw->orw_epoch);

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

	/* return sg_nr_out and data size for sgl */
	orwo->orw_nrs.ca_count = nrs_count;
	D_ALLOC(orwo->orw_nrs.ca_arrays,
		nrs_count * (sizeof(uint32_t) + sizeof(daos_size_t)));

	if (orwo->orw_nrs.ca_arrays == NULL)
		return -DER_NOMEM;
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
		" tgt/xs %d/%d epc "DF_U64".\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid),
		DP_KEY(&orw->orw_dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id,
		orw->orw_epoch);

	if (obj_rpc_is_fetch(rpc)) {
		rc = obj_set_reply_sizes(rpc);
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
		daos_sgl_fini(p_sgl, true);
		rc = daos_sgl_init(p_sgl, iod->iod_nr);
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
			       DAOS_HDL_INVAL, &p_sgl, NULL, orw->orw_nr);
out:
	orwo->orw_ret = rc;
	orwo->orw_map_version = orw->orw_map_ver;
}


int
ec_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op, bool bulk_bind,
		 crt_bulk_t *remote_bulks, daos_handle_t ioh,
		 struct ec_bulk_spec **skip_list,
		 int sgl_nr)
{
	struct obj_bulk_args	arg = { 0 };
	crt_bulk_opid_t		bulk_opid;
	crt_bulk_perm_t		bulk_perm = CRT_BULK_RW;
	int			i, rc, *status, ret;

	D_ASSERT(skip_list != NULL);

	rc = ABT_eventual_create(sizeof(*status), &arg.eventual);
	if (rc != 0)
		return dss_abterr2der(rc);

	arg.bulks_inflight++;
	for (i = 0; i < sgl_nr; i++) {
		d_sg_list_t		*sgl, tmp_sgl;
		struct crt_bulk_desc	 bulk_desc;
		crt_bulk_t		 local_bulk_hdl;
		struct bio_sglist	*bsgl;
		daos_size_t		 offset = 0;
		unsigned int		 idx = 0;
		unsigned int		 sl_idx = 0;

		if (remote_bulks[i] == NULL)
			continue;

		D_ASSERT(!daos_handle_is_inval(ioh));
		bsgl = vos_iod_sgl_at(ioh, i);
		D_ASSERT(bsgl != NULL);

		sgl = &tmp_sgl;
		rc = bio_sgl_convert(bsgl, sgl, true);
		if (rc)
			break;

		if (daos_io_bypass & IOBP_SRV_BULK) {
			/* this mode will bypass network bulk transfer and
			 * only copy data from/to dummy buffer. This is for
			 * performance evaluation on low bandwidth network.
			 */
			obj_bulk_bypass(sgl, bulk_op);
			goto next;
		}

		while (idx < sgl->sg_nr_out) {
			d_sg_list_t	sgl_sent;
			daos_size_t	length = 0;
			unsigned int	start = idx;

			sgl_sent.sg_iovs = &sgl->sg_iovs[start];
			if (skip_list[i] == NULL) {
				/* Find the end of the non-empty record */
				while (sgl->sg_iovs[idx].iov_buf != NULL &&
					idx < sgl->sg_nr_out)
					length += sgl->sg_iovs[idx++].iov_len;
			} else {
				while (ec_bulk_spec_get_skip(sl_idx,
							     skip_list[i]))
					offset +=
					ec_bulk_spec_get_len(sl_idx++,
							     skip_list[i]);
				length += sgl->sg_iovs[idx++].iov_len;
				D_ASSERT(ec_bulk_spec_get_len(sl_idx,
							      skip_list[i]) ==
					 length);
				sl_idx++;
			}
			sgl_sent.sg_nr = idx - start;
			sgl_sent.sg_nr_out = idx - start;

			rc = crt_bulk_create(rpc->cr_ctx, &sgl_sent,
					     bulk_perm, &local_bulk_hdl);
			if (rc != 0) {
				D_ERROR("crt_bulk_create %d error (%d).\n",
					i, rc);
				break;
			}

			crt_req_addref(rpc);
			bulk_desc.bd_rpc	= rpc;
			bulk_desc.bd_bulk_op	= bulk_op;
			bulk_desc.bd_remote_hdl	= remote_bulks[i];
			bulk_desc.bd_local_hdl	= local_bulk_hdl;
			bulk_desc.bd_len	= length;
			bulk_desc.bd_remote_off	= offset;
			bulk_desc.bd_local_off	= 0;

			arg.bulks_inflight++;
			if (bulk_bind)
				rc = crt_bulk_bind_transfer(&bulk_desc,
					obj_bulk_comp_cb, &arg, &bulk_opid);
			else
				rc = crt_bulk_transfer(&bulk_desc,
					obj_bulk_comp_cb, &arg, &bulk_opid);
			if (rc < 0) {
				D_ERROR("crt_bulk_transfer %d error (%d).\n",
					i, rc);
				arg.bulks_inflight--;
				crt_bulk_free(local_bulk_hdl);
				crt_req_decref(rpc);
				break;
			}
			offset += length;
		}
next:
		daos_sgl_fini(sgl, false);
		if (rc)
			break;
		D_FREE(skip_list[i]);
	}

	if (--arg.bulks_inflight == 0)
		ABT_eventual_set(arg.eventual, &rc, sizeof(rc));

	ret = ABT_eventual_wait(arg.eventual, (void **)&status);
	if (rc == 0)
		rc = ret ? dss_abterr2der(ret) : *status;

	ABT_eventual_free(&arg.eventual);
	return rc;
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
	      struct dcs_iod_csums *iod_csums)
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

int csum_verify_keys(struct daos_csummer *csummer, struct obj_rw_in *orw)
{
	uint32_t	i;
	int		rc;

	if (!daos_csummer_initialized(csummer) || csummer->dcs_skip_key_verify)
		return 0;

	rc = daos_csummer_verify_key(csummer, &orw->orw_dkey,
				     orw->orw_dkey_csum);
	if (rc != 0) {
		D_ERROR("daos_csummer_verify_key error for dkey: %d", rc);
		return rc;
	}

	for (i = 0; i < orw->orw_iod_array.oia_iod_nr; i++) {
		daos_iod_t		*iod = &orw->orw_iod_array.oia_iods[i];
		struct dcs_iod_csums	*csum =
					&orw->orw_iod_array.oia_iod_csums[i];

		if (!csum_iod_is_supported(iod))
			continue;
		rc = daos_csummer_verify_key(csummer,
					     &iod->iod_name,
					     &csum->ic_akey);
		if (rc != 0) {
			D_ERROR("daos_csummer_verify_key error for akey: %d",
				rc);
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
static int
obj_singv_ec_rw_filter(struct obj_rw_in *orw, daos_iod_t *iods, uint64_t *offs,
		       bool for_update, bool deg_fetch,
		       struct daos_recx_ep_list **recov_lists_ptr)
{
	struct daos_oclass_attr		*oca = NULL;
	daos_iod_t			*iod;
	struct obj_ec_singv_local	 loc;
	uint32_t			 tgt_idx;
	uint32_t			 i;
	int				 rc = 0;

	tgt_idx = orw->orw_oid.id_shard - orw->orw_start_shard;
	for (i = 0; i < orw->orw_nr; i++) {
		iod = &iods[i];
		if (iod->iod_type != DAOS_IOD_SINGLE ||
		    (orw->orw_flags & ORF_EC) == 0)
			continue;
		/* for singv EC */
		D_ASSERT(iod->iod_recxs == NULL);
		if (iod->iod_size == DAOS_REC_ANY) /* punch */
			continue;
		if (oca == NULL) {
			oca = daos_oclass_attr_find(orw->orw_oid.id_pub);
			D_ASSERT(oca != NULL && DAOS_OC_IS_EC(oca));
		}
		/* using iod_recxs to pass ir_gsize (akey_update_single) */
		if (for_update)
			iod->iod_recxs = (void *)iod->iod_size;
		if (!obj_ec_singv_one_tgt(iod, NULL, oca)) {
			obj_ec_singv_local_sz(iod->iod_size, oca, tgt_idx,
					      &loc);
			offs[i] = loc.esl_off;
			if (for_update)
				iod->iod_size = loc.esl_size;
			if (deg_fetch)
				rc = obj_singv_ec_add_recov(orw->orw_nr, i,
							    iod->iod_size,
							    orw->orw_epoch,
							    recov_lists_ptr);
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
obj_fetch_create_maps(crt_rpc_t *rpc, struct bio_desc *biod)
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
	D_ALLOC_ARRAY(maps, iods_nr);
	if (maps == NULL)
		return -DER_NOMEM;
	for (i = 0; i <  iods_nr; i++) {
		bsgl = bio_iod_sgl(biod, i); /** 1 bsgl per iod */
		iod = &orw->orw_iod_array.oia_iods[i];
		map = &maps[i];
		map->iom_nr = bsgl->bs_nr_out - bio_sgl_holes(bsgl);

		/** will be freed in obj_rw_reply */
		D_ALLOC_ARRAY(map->iom_recxs, map->iom_nr);
		if (map->iom_recxs == NULL) {
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

		/** allocated and used should be the same */
		D_ASSERTF(map->iom_nr == map->iom_nr_out,
			  "map->iom_nr(%d) == map->iom_nr_out(%d)",
			  map->iom_nr, map->iom_nr_out);
		map->iom_recx_lo = map->iom_recxs[0];
		map->iom_recx_hi = map->iom_recxs[map->iom_nr - 1];
	}

	orwo->orw_maps.ca_count = iods_nr;
	orwo->orw_maps.ca_arrays = maps;

	return 0;
}

/*
 * Check if the dedup data is identical to the RDMA data in a temporal
 * allocated SCM extent, if memcmp fails, update the temporal SCM address
 * in VOS tree, otherwise, keep using the original dedup data address
 * in VOS tree and free the temporal SCM extent.
 */
static int
obj_dedup_verify(daos_handle_t ioh, struct bio_sglist *bsgls_dup, int sgl_nr)
{
	struct bio_sglist	*bsgl, *bsgl_dup;
	int			 i, j, rc;

	D_ASSERT(!daos_handle_is_inval(ioh));
	D_ASSERT(bsgls_dup != NULL);

	for (i = 0; i < sgl_nr; i++) {
		bsgl = vos_iod_sgl_at(ioh, i);
		D_ASSERT(bsgl != NULL);
		bsgl_dup = &bsgls_dup[i];

		D_ASSERT(bsgl->bs_nr_out == bsgl_dup->bs_nr_out);
		for (j = 0; j < bsgl->bs_nr_out; j++) {
			struct bio_iov	*biov = &bsgl->bs_iovs[j];
			struct bio_iov	*biov_dup = &bsgl_dup->bs_iovs[j];

			if (bio_iov2buf(biov) == NULL) {
				D_ASSERT(bio_iov2buf(biov_dup) == NULL);
				continue;
			}

			/* Didn't use deduped extent */
			if (!biov->bi_addr.ba_dedup) {
				D_ASSERT(!biov_dup->bi_addr.ba_dedup);
				continue;
			}
			D_ASSERT(biov_dup->bi_addr.ba_dedup);

			D_ASSERT(bio_iov2len(biov) == bio_iov2len(biov_dup));
			rc = d_memcmp(bio_iov2buf(biov), bio_iov2buf(biov_dup),
				      bio_iov2len(biov));

			if (rc == 0) {	/* verify succeeded */
				D_DEBUG(DB_IO, "Verify dedup succeeded\n");
				continue;
			}

			/*
			 * Replace the dedup addr in VOS tree with the temporal
			 * SCM extent, ignore space leak if later transaction
			 * failed to commit.
			 */
			biov->bi_addr.ba_off = biov_dup->bi_addr.ba_off;
			biov->bi_addr.ba_dedup = false;
			biov_dup->bi_addr.ba_off = UMOFF_NULL;

			D_DEBUG(DB_IO, "Verify dedup extents failed, "
				"use newly allocated extent\n");
		}
	}

	return 0;
}

static int
obj_fetch_shadow(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t cond_flags, daos_key_t *dkey, unsigned int iod_nr,
		daos_iod_t *iods, uint32_t tgt_idx,
		struct daos_recx_ep_list **pshadows)
{
	struct daos_oclass_attr		*oca;
	daos_handle_t			 ioh = DAOS_HDL_INVAL;
	int				 rc;

	oca = daos_oclass_attr_find(oid.id_pub);
	if (oca == NULL || !DAOS_OC_IS_EC(oca)) {
		rc = -DER_INVAL;
		D_ERROR(DF_UOID" oca not found or not EC obj: "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
		goto out;
	}

	obj_iod_idx_vos2parity(iod_nr, iods);
	rc = vos_fetch_begin(coh, oid, epoch, cond_flags, dkey, iod_nr, iods,
			     VOS_FETCH_RECX_LIST, NULL, &ioh, NULL);
	if (rc) {
		D_ERROR(DF_UOID" Fetch begin failed: "DF_RC"\n",
			DP_UOID(oid), DP_RC(rc));
		goto out;
	}

	*pshadows = vos_ioh2recx_list(ioh);
	vos_fetch_end(ioh, 0);

out:
	obj_iod_idx_parity2vos(iod_nr, iods);
	if (rc == 0) {
		obj_shadow_list_vos2daos(iod_nr, *pshadows, oca);
		rc = obj_iod_recx_vos2daos(iod_nr, iods, tgt_idx, oca);
	}
	return rc;
}

static int
obj_local_rw(crt_rpc_t *rpc, struct obj_io_context *ioc,
	     daos_iod_t *split_iods, struct dcs_iod_csums *split_csums,
	     uint64_t *split_offs, struct dtx_handle *dth)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	struct dcs_iod_csums		*iod_csums;
	uint32_t			tag = dss_get_module_info()->dmi_tgt_id;
	daos_handle_t			ioh = DAOS_HDL_INVAL;
	uint64_t			time_start = 0;
	struct bio_desc			*biod;
	daos_key_t			*dkey;
	crt_bulk_op_t			bulk_op;
	bool				rma;
	bool				bulk_bind;
	bool				create_map;
	bool				spec_fetch = false;
	struct daos_recx_ep_list	*recov_lists = NULL;
	daos_iod_t			*iods;
	uint64_t			*offs;
	struct bio_sglist		*bsgls_dup = NULL; /* dedup verify */
	int				err, rc = 0;

	D_TIME_START(time_start, OBJ_PF_UPDATE_LOCAL);

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

	rc = csum_verify_keys(ioc->ioc_coc->sc_csummer, orw);
	if (rc != 0) {
		D_ERROR("csum_verify_keys error: %d", rc);
		if (rc == -DER_CSUM)
			obj_log_csum_err();
		return rc;
	}
	dkey = (daos_key_t *)&orw->orw_dkey;
	D_DEBUG(DB_IO,
		"opc %d oid "DF_UOID" dkey "DF_KEY" tag %d epc "DF_U64".\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid), DP_KEY(dkey),
		tag, orw->orw_epoch);

	rma = (orw->orw_bulks.ca_arrays != NULL ||
	       orw->orw_bulks.ca_count != 0);

	/* Prepare IO descriptor */
	if (obj_rpc_is_update(rpc)) {
		obj_singv_ec_rw_filter(orw, iods, offs, true, false, NULL);
		bulk_op = CRT_BULK_GET;

		/** Fault injection - corrupt data from network */
		if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_UPDATE)  && !rma) {
			D_ERROR("csum: Corrupting data (network)\n");
			dcf_corrupt(orw->orw_sgls.ca_arrays,
				    orw->orw_sgls.ca_count);
		}

		if (rma && ioc->ioc_coc->sc_props.dcp_dedup &&
		    ioc->ioc_coc->sc_props.dcp_dedup_verify) {
			/**
			 * If deduped data need to be compared, then perform
			 * the I/O are a regular one, we will check for dedup
			 * data after RDMA
			 */
			D_ALLOC_ARRAY(bsgls_dup, orw->orw_nr);
			if (bsgls_dup == NULL) {
				rc = -DER_NOMEM;
				goto out;
			}
		}

		rc = vos_update_begin(ioc->ioc_coc->sc_hdl, orw->orw_oid,
			      orw->orw_epoch, orw->orw_api_flags,
			      dkey, orw->orw_nr, iods,
			      iod_csums,
			      ioc->ioc_coc->sc_props.dcp_dedup,
			      ioc->ioc_coc->sc_props.dcp_dedup_size,
			      &ioh, dth);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
				DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}
	} else {
		uint64_t			 cond_flags;
		uint32_t			 fetch_flags = 0;
		bool				 ec_deg_fetch;
		struct daos_recx_ep_list	*shadows = NULL;

		cond_flags = orw->orw_api_flags;
		bulk_op = CRT_BULK_PUT;
		if (!rma && orw->orw_sgls.ca_arrays == NULL) {
			spec_fetch = true;
			if (orw->orw_api_flags & VOS_COND_FETCH_MASK)
				fetch_flags = VOS_FETCH_CHECK_EXISTENCE;
			else
				fetch_flags = VOS_FETCH_SIZE_ONLY;
		}

		ec_deg_fetch = orw->orw_flags & ORF_EC_DEGRADED;
		if (ec_deg_fetch && !spec_fetch) {
			rc = obj_fetch_shadow(ioc->ioc_coc->sc_hdl,
				orw->orw_oid, orw->orw_epoch, cond_flags,
				dkey, orw->orw_nr, iods, orw->orw_tgt_idx,
				&shadows);
			if (rc) {
				D_ERROR(DF_UOID" Fetch shadow failed: "DF_RC
					"\n", DP_UOID(orw->orw_oid), DP_RC(rc));
				goto out;
			}
		}

		rc = vos_fetch_begin(ioc->ioc_coc->sc_hdl, orw->orw_oid,
				     orw->orw_epoch,
				     cond_flags, dkey, orw->orw_nr, iods,
				     fetch_flags, shadows, &ioh, dth);
		daos_recx_ep_list_free(shadows, orw->orw_nr);
		if (rc) {
			D_CDEBUG(rc == -DER_INPROGRESS, DB_IO, DLOG_ERR,
				 "Fetch begin for "DF_UOID" failed: "DF_RC"\n",
				 DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}

		rc = obj_set_reply_sizes(rpc);
		if (rc != 0)
			goto out;

		if (rma) {
			orwo->orw_sgls.ca_count = 0;
			orwo->orw_sgls.ca_arrays = NULL;

			rc = obj_set_reply_nrs(rpc, ioh, NULL);
			if (rc != 0)
				goto out;
		} else {
			orwo->orw_sgls.ca_count = orw->orw_sgls.ca_count;
			orwo->orw_sgls.ca_arrays = orw->orw_sgls.ca_arrays;
		}
		recov_lists = vos_ioh2recx_list(ioh);
		rc = obj_singv_ec_rw_filter(orw, iods, offs, false,
					    ec_deg_fetch, &recov_lists);
		if (rc != 0) {
			D_ERROR(DF_UOID" obj_singv_ec_rw_filter failed: "
				DF_RC".\n", DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}
		if (recov_lists != NULL) {
			daos_recx_ep_list_set_ep_valid(recov_lists,
						       orw->orw_nr);
			orwo->orw_rels.ca_arrays = recov_lists;
			orwo->orw_rels.ca_count = orw->orw_nr;
		}
	}

	biod = vos_ioh2desc(ioh);
	rc = bio_iod_prep(biod);
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
					   orwo->orw_iod_csums.ca_arrays);
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
				       ioh, NULL, bsgls_dup, orw->orw_nr);
		if (!rc)
			bio_iod_flush(biod);
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
		if (bsgls_dup != NULL) {	/* dedup verify */
			rc = obj_dedup_verify(ioh, bsgls_dup, orw->orw_nr);
			D_GOTO(post, rc);
		}

		rc = obj_verify_bio_csum(rpc, iods, iod_csums, biod,
					 ioc->ioc_coc->sc_csummer);
		/** CSUM Verified on update, now corrupt to fake corruption
		 * on disk
		 */
		if (DAOS_FAIL_CHECK(DAOS_CSUM_CORRUPT_DISK) && !rma) {
			D_ERROR("csum: Corrupting data (DISK)\n");
			dcf_corrupt(orw->orw_sgls.ca_arrays,
				    orw->orw_sgls.ca_count);
		}
	}
	if (obj_rpc_is_fetch(rpc) && create_map)
		rc = obj_fetch_create_maps(rpc, biod);

	if (rc == -DER_CSUM)
		obj_log_csum_err();
post:
	err = bio_iod_post(biod);
	if (bsgls_dup != NULL) {
		int	i;

		for (i = 0; i < orw->orw_nr; i++) {
			vos_dedup_free_bsgl(ioh, &bsgls_dup[i]);
			bio_sgl_fini(&bsgls_dup[i]);
		}
		D_FREE(bsgls_dup);
	}
	rc = rc ? : err;
out:
	rc = obj_rw_complete(rpc, ioc->ioc_map_ver, ioh, rc, dth);
	D_TIME_END(time_start, OBJ_PF_UPDATE_LOCAL);
	return rc;
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

	memset(ioc, 0, sizeof(*ioc));
	rc = ds_cont_find_hdl(pool_uuid, coh_uuid, &coh);
	if (rc) {
		if (rc == -DER_NONEXIST)
			rc = -DER_NO_HDL;
		return rc;
	}

	if (!obj_is_modification_opc(opc) &&
	    !ds_sec_cont_can_read_data(coh->sch_sec_capas)) {
		D_ERROR("cont "DF_UUID" hdl "DF_UUID" sec_capas "DF_U64", "
			"NO_PERM to read.\n", DP_UUID(cont_uuid),
			DP_UUID(coh_uuid), coh->sch_sec_capas);
		D_GOTO(failed, rc = -DER_NO_PERM);
	}

	if (obj_is_modification_opc(opc) &&
	    !ds_sec_cont_can_write_data(coh->sch_sec_capas)) {
		D_ERROR("cont "DF_UUID" hdl "DF_UUID" sec_capas "DF_U64", "
			"NO_PERM to update.\n", DP_UUID(cont_uuid),
			DP_UUID(coh_uuid), coh->sch_sec_capas);
		D_GOTO(failed, rc = -DER_NO_PERM);
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
	}

	if (!is_container_from_srv(pool_uuid, coh_uuid)) {
		D_ERROR("Empty container "DF_UUID" (ref=%d) handle?\n",
			DP_UUID(cont_uuid), coh->sch_ref);
		D_GOTO(failed, rc = -DER_NO_HDL);
	}

	/* rebuild handle is a dummy and never attached by a real container */
	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_HDL))
		D_GOTO(failed, rc = -DER_NO_HDL);

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_STALE_POOL))
		D_GOTO(failed, rc = -DER_STALE);

	D_DEBUG(DB_TRACE, DF_UUID"/%p is rebuild cont hdl\n",
		DP_UUID(coh_uuid), coh);

	/* load VOS container on demand for rebuild */
	rc = ds_cont_child_lookup(pool_uuid, cont_uuid, &coc);
	if (rc)
		D_GOTO(failed, rc);

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

/* Various check before access VOS */
static int
obj_ioc_begin(daos_unit_oid_t oid, uint32_t rpc_map_ver, uuid_t pool_uuid,
	      uuid_t coh_uuid, uuid_t cont_uuid, uint32_t opc,
	      struct obj_io_context *ioc)
{
	struct ds_pool_child *poc;
	int		      rc;

	rc = obj_ioc_init(pool_uuid, coh_uuid, cont_uuid, opc, ioc);
	if (rc)
		return rc;

	poc = ioc->ioc_coc->sc_pool;
	D_ASSERT(poc != NULL);

	if (rpc_map_ver > ioc->ioc_map_ver || poc->spc_pool->sp_map == NULL ||
	    DAOS_FAIL_CHECK(DAOS_FORCE_REFRESH_POOL_MAP)) {
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
		/*
		 * Though maybe harmless for now, but let's refresh the server
		 * pool map to avoid any possible issue
		 */
		D_DEBUG(DB_IO, "stale server map_version %d req %d\n",
			ioc->ioc_map_ver, rpc_map_ver);
		rc = ds_pool_child_map_refresh_async(poc);
		if (rc == 0) {
			ioc->ioc_map_ver = poc->spc_map_version;
			rc = -DER_STALE;
		}

		D_GOTO(out, rc);
	} else if (rpc_map_ver < ioc->ioc_map_ver) {
		D_DEBUG(DB_IO, "stale version req %d map_version %d\n",
			rpc_map_ver, ioc->ioc_map_ver);
		if (obj_is_modification_opc(opc))
			D_GOTO(out, rc = -DER_STALE);
		/* It is harmless if fetch with old pool map version. */
	}
out:
	dss_rpc_cntr_enter(DSS_RC_OBJ);
	ioc->ioc_began = 1;
	return rc;
}

void
obj_ioc_end(struct obj_io_context *ioc, int err)
{
	if (ioc->ioc_began) {
		dss_rpc_cntr_exit(DSS_RC_OBJ, !!err);
		ioc->ioc_began = 0;
	}
	obj_ioc_fini(ioc);
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
	struct dtx_epoch		 epoch;
	int				 rc;

	D_ASSERT(orw != NULL);
	D_ASSERT(orwo != NULL);

	rc = obj_ioc_begin(orw->orw_oid, orw->orw_map_ver,
			   orw->orw_pool_uuid, orw->orw_co_hdl,
			   orw->orw_co_uuid, opc_get(rpc->cr_opc), &ioc);
	if (rc)
		goto out;

	if (DAOS_FAIL_CHECK(DAOS_VC_DIFF_DKEY)) {
		unsigned char	*buf = dkey->iov_buf;

		buf[0] += orw->orw_oid.id_shard + 1;
		orw->orw_dkey_hash = obj_dkey2hash(dkey);
	}

	D_DEBUG(DB_IO,
		"rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_U64", pmv %u/%u dti "DF_DTI".\n",
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
		rc = obj_gen_dtx_mbs(tgts, &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	epoch.oe_value = orw->orw_epoch;
	epoch.oe_first = orw->orw_epoch_first;
	epoch.oe_flags = orf_to_dtx_epoch_flags(orw->orw_flags);

	rc = dtx_begin(ioc.ioc_coc, &orw->orw_dti, &epoch, 1,
		       orw->orw_map_ver, &orw->orw_oid,
		       orw->orw_dti_cos.ca_arrays,
		       orw->orw_dti_cos.ca_count, mbs, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for update "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = obj_local_rw(rpc, &ioc, NULL, NULL, NULL, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": error="DF_RC".\n", DP_UOID(orw->orw_oid),
			DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	if (opc == DAOS_OBJ_RPC_TGT_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		rc = -DER_IO;

	rc = dtx_end(&dth, ioc.ioc_coc, rc);
	obj_rw_reply(rpc, rc, ioc.ioc_map_ver, 0, ioc.ioc_coc);
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
		struct obj_ec_split_req	*split_req = exec_arg->args;
		daos_iod_t		*split_iods;
		struct dcs_iod_csums	*split_csums;
		uint64_t		*split_offs;
		int			 rc = 0;

		/* No need re-exec local update */
		if (!(exec_arg->flags & ORF_RESEND)) {
			split_iods = split_req != NULL ? split_req->osr_iods :
							 NULL;
			split_offs = split_req != NULL ? split_req->osr_offs :
							 NULL;
			split_csums = split_req != NULL ?
				      split_req->osr_iod_csums :
				      NULL;
			rc = obj_local_rw(exec_arg->rpc, exec_arg->ioc,
					  split_iods, split_csums, split_offs,
					  &dlh->dlh_handle);
		}
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

	D_DEBUG(DB_IO, "overwrite epoch "DF_U64"\n", *epoch);
	return PE_OK_LOCAL;
}

void
ds_obj_rw_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	struct dtx_leader_handle	dlh;
	struct ds_obj_exec_arg		exec_arg = { 0 };
	struct obj_io_context		ioc;
	uint64_t			time_start = 0;
	uint32_t			flags = 0;
	uint32_t			opc = opc_get(rpc->cr_opc);
	struct obj_ec_split_req		*split_req = NULL;
	struct dtx_memberships		*mbs = NULL;
	struct daos_shard_tgt		*tgts = NULL;
	struct dtx_id			*dti_cos = NULL;
	struct dss_sleep_ult		*sleep_ult = NULL;
	int				dti_cos_cnt;
	uint32_t			tgt_cnt;
	uint32_t			version;
	struct dtx_epoch		epoch = {0};
	int				rc;

	D_ASSERT(orw != NULL);
	D_ASSERT(orwo != NULL);

	rc = obj_ioc_begin(orw->orw_oid, orw->orw_map_ver,
			   orw->orw_pool_uuid, orw->orw_co_hdl,
			   orw->orw_co_uuid, opc_get(rpc->cr_opc), &ioc);
	if (rc != 0) {
		D_ASSERTF(rc < 0, "unexpected error# "DF_RC"\n", DP_RC(rc));
		goto out;
	}

	D_DEBUG(DB_IO,
		"rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_U64", pmv %u/%u dti "DF_DTI".\n",
		rpc, opc, DP_UOID(orw->orw_oid), DP_KEY(&orw->orw_dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id, orw->orw_epoch,
		orw->orw_map_ver, ioc.ioc_map_ver, DP_DTI(&orw->orw_dti));

	rc = process_epoch(&orw->orw_epoch, &orw->orw_epoch_first,
			   &orw->orw_flags);
	if (rc == PE_OK_LOCAL)
		orw->orw_flags &= ~ORF_EPOCH_UNCERTAIN;

	if (obj_rpc_is_fetch(rpc)) {
		struct dtx_handle dth = {0};
		int		  retry = 0;

		if (orw->orw_flags & ORF_CSUM_REPORT) {
			obj_log_csum_err();
			D_GOTO(out, rc = -DER_CSUM);
		}

		epoch.oe_value = orw->orw_epoch;
		epoch.oe_first = orw->orw_epoch_first;
		epoch.oe_flags = orf_to_dtx_epoch_flags(orw->orw_flags);

re_fetch:
		rc = dtx_begin(ioc.ioc_coc, &orw->orw_dti, &epoch, 0,
			       orw->orw_map_ver, &orw->orw_oid,
			       NULL, 0, NULL, &dth);
		if (rc != 0)
			goto out;

		rc = obj_local_rw(rpc, &ioc, NULL, NULL, NULL, &dth);
		rc = dtx_end(&dth, ioc.ioc_coc, rc);

		if (rc == -DER_INPROGRESS && dth.dth_local_retry) {
			if (++retry > 3)
				D_GOTO(out, rc = -DER_TX_BUSY);

			/* XXX: Currently, we commit the distributed transaction
			 *	sychronously. Normally, it will be very quickly.
			 *	So let's wait on the server for a while (30 ms),
			 *	then retry. If related distributed transaction
			 *	is still not committed after several cycles try,
			 *	then replies '-DER_TX_BUSY' to the client.
			 */
			if (sleep_ult == NULL) {
				sleep_ult = dss_sleep_ult_create();
				if (sleep_ult == NULL)
					D_GOTO(out, rc = -DER_TX_BUSY);
			}

			D_DEBUG(DB_IO, "Hit non-commit DTX when fetch "
				DF_UOID" (%d)\n", DP_UOID(orw->orw_oid), retry);
			dss_ult_sleep(sleep_ult, 30000);

			goto re_fetch;
		}

		D_GOTO(out, rc);
	}

	tgts = orw->orw_shard_tgts.ca_arrays;
	tgt_cnt = orw->orw_shard_tgts.ca_count;

	if (!daos_is_zero_dti(&orw->orw_dti) && tgt_cnt != 0) {
		rc = obj_gen_dtx_mbs(tgts, &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	version = orw->orw_map_ver;

again:
	/* Handle resend. */
	if (orw->orw_flags & ORF_RESEND) {
		daos_epoch_t	e = 0;

		rc = dtx_handle_resend(ioc.ioc_vos_coh, &orw->orw_dti,
				       &e, &version);
		if (rc == -DER_ALREADY)
			D_GOTO(out, rc = 0);

		if (rc == 0) {
			flags |= ORF_RESEND;
			orw->orw_epoch = e;
			/* TODO: Also recover the epoch uncertainty. */
		} else if (rc == -DER_NONEXIST) {
			rc = 0;
		} else {
			D_GOTO(out, rc);
		}
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST)) {
		goto cleanup;
	}

	if (orw->orw_iod_array.oia_oiods != NULL && split_req == NULL) {
		rc = obj_ec_rw_req_split(orw, &split_req);
		if (rc != 0) {
			D_ERROR(DF_UOID": obj_ec_rw_req_split failed, rc %d.\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out, rc);
		}
	}

	D_TIME_START(time_start, OBJ_PF_UPDATE);

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
	rc = dtx_leader_begin(ioc.ioc_coc, &orw->orw_dti, &epoch, 1, version,
			      &orw->orw_oid, dti_cos, dti_cos_cnt,
			      tgts, tgt_cnt, mbs, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for update "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.args = split_req;
	exec_arg.flags = flags;

	if (orw->orw_flags & ORF_DTX_SYNC)
		dlh.dlh_handle.dth_sync = 1;

	if (flags & ORF_RESEND)
		dlh.dlh_handle.dth_resent = 1;

	/* Execute the operation on all targets */
	rc = dtx_leader_exec_ops(&dlh, obj_tgt_update, &exec_arg);

	if (DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
		rc = -DER_IO;

	/* Stop the distribute transaction */
	rc = dtx_leader_end(&dlh, ioc.ioc_coc, rc);
	switch (rc) {
	case -DER_TX_RESTART:
		/*
		 * If this is a standalone operation, we can restart the
		 * internal transaction right here. Otherwise, we have to defer
		 * the restart to the RPC client.
		 */
		if (opc == DAOS_OBJ_RPC_UPDATE) {
			/*
			 * Only standalone updates use this RPC. Retry with
			 * newer epoch.
			 */
			orw->orw_epoch = crt_hlc_get();
			orw->orw_flags &= ~ORF_RESEND;
			flags = 0;
			goto again;
		}

		/* Standalone fetches do not get -DER_TX_RESTART. */
		D_ASSERT(!daos_is_zero_dti(&orw->orw_dti));

		break;
	case -DER_AGAIN:
		orw->orw_flags |= ORF_RESEND;
		goto again;
	default:
		break;
	}

	if (opc == DAOS_OBJ_RPC_UPDATE && !(orw->orw_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		goto cleanup;

out:
	obj_rw_reply(rpc, rc, ioc.ioc_map_ver, epoch.oe_value, ioc.ioc_coc);

cleanup:
	D_TIME_END(time_start, OBJ_PF_UPDATE);
	if (sleep_ult != NULL)
		dss_sleep_ult_destroy(sleep_ult);

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

	if (oeo->oeo_kds.ca_arrays != NULL)
		D_FREE(oeo->oeo_kds.ca_arrays);

	if (oeo->oeo_sgl.sg_iovs != NULL)
		daos_sgl_fini(&oeo->oeo_sgl, true);

	if (oeo->oeo_eprs.ca_arrays != NULL)
		D_FREE(oeo->oeo_eprs.ca_arrays);

	if (oeo->oeo_recxs.ca_arrays != NULL)
		D_FREE(oeo->oeo_recxs.ca_arrays);

	if (oeo->oeo_csum_iov.iov_buf != NULL)
		D_FREE(oeo->oeo_csum_iov.iov_buf);
}

static int
obj_local_enum(struct obj_io_context *ioc, crt_rpc_t *rpc,
	       struct vos_iter_anchors *anchors, struct dss_enum_arg *enum_arg,
	       daos_epoch_t *e_out)
{
	vos_iter_param_t	param = { 0 };
	struct obj_key_enum_in	*oei = crt_req_get(rpc);
	struct dss_sleep_ult	*sleep_ult = NULL;
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
		enum_arg->chk_key2big = true;
		enum_arg->need_punch = true;
		enum_arg->copy_data_cb = vos_iter_copy;
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
	else if (oei->oei_oid.id_shard % 2 == 0 &&
		 DAOS_FAIL_CHECK(DAOS_VC_LOST_REPLICA))
		D_GOTO(failed, rc =  -DER_NONEXIST);

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

again:
	rc = dtx_begin(ioc->ioc_coc, &oei->oei_dti, &epoch, 0,
		       oei->oei_map_ver, &oei->oei_oid, NULL, 0, NULL, &dth);
	if (rc != 0)
		goto failed;

	rc = dss_enum_pack(&param, type, recursive, anchors, enum_arg,
			   vos_iterate, &dth);

	/* dss_enum_pack may return 1. */
	rc_tmp = dtx_end(&dth, ioc->ioc_coc, rc > 0 ? 0 : rc);
	if (rc_tmp != 0)
		rc = rc_tmp;

	if (rc == -DER_INPROGRESS && dth.dth_local_retry) {
		if (++retry > 3)
			D_GOTO(out, rc = -DER_TX_BUSY);

		/* XXX: Currently, we commit the distributed transaction
		 *	sychronously. Normally, it will be very quickly.
		 *	So let's wait on the server for a while (30 ms),
		 *	then retry. If related distributed transaction
		 *	is still not committed after several cycles try,
		 *	then replies '-DER_TX_BUSY' to the client.
		 */
		if (sleep_ult == NULL) {
			sleep_ult = dss_sleep_ult_create();
			if (sleep_ult == NULL)
				D_GOTO(out, rc = -DER_TX_BUSY);
		}

		D_DEBUG(DB_IO, "Hit non-commit DTX when enum "
			DF_UOID" (%d)\n", DP_UOID(oei->oei_oid), retry);
		dss_ult_sleep(sleep_ult, 30000);

		/* re-enumeration from the last -DER_TX_BUSY. */
		goto again;
	}

out:
	if (type == VOS_ITER_SINGLE)
		anchors->ia_ev = anchors->ia_sv;

	D_DEBUG(DB_IO, ""DF_UOID" iterate "DF_U64"-"DF_U64" type %d tag %d"
		" rc %d\n", DP_UOID(oei->oei_oid), param.ip_epr.epr_lo,
		param.ip_epr.epr_hi, type, dss_get_module_info()->dmi_tgt_id,
		rc);
failed:
	if (sleep_ult != NULL)
		dss_sleep_ult_destroy(sleep_ult);

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
		D_DEBUG(DB_IO, "reply bulk %zd nr_out %d\n",
			oeo->oeo_sgl.sg_iovs[0].iov_len,
			oeo->oeo_sgl.sg_nr_out);
		sgls[idx] = &oeo->oeo_sgl;
		bulks[idx] = oei->oei_bulk;
		idx++;
	}

	/* No need reply bulk */
	if (idx == 0)
		return 0;

	rc = obj_bulk_transfer(rpc, CRT_BULK_PUT, false, bulks, NULL,
			       DAOS_HDL_INVAL, sgls, NULL, idx);
	if (oei->oei_kds_bulk) {
		D_FREE(oeo->oeo_kds.ca_arrays);
		oeo->oeo_kds.ca_arrays = NULL;
		oeo->oeo_kds.ca_count = 0;
	}

	/* Free oeo_sgl here to avoid rpc reply the data inline */
	if (oei->oei_bulk)
		daos_sgl_fini(&oeo->oeo_sgl, true);

	return rc;
}

static int
obj_enum_prep_sgls(d_sg_list_t *dst_sgls, d_sg_list_t *sgls, int number)
{
	int i;
	int j;
	int rc = 0;

	for (i = 0; i < number; i++) {
		dst_sgls[i].sg_nr = sgls[i].sg_nr;
		D_ALLOC_ARRAY(dst_sgls[i].sg_iovs, sgls[i].sg_nr);
		if (dst_sgls[i].sg_iovs == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		for (j = 0; j < dst_sgls[i].sg_nr; j++) {
			dst_sgls[i].sg_iovs[j].iov_buf_len =
				sgls[i].sg_iovs[j].iov_buf_len;

			D_ALLOC(dst_sgls[i].sg_iovs[j].iov_buf,
				dst_sgls[i].sg_iovs[j].iov_buf_len);
			if (dst_sgls[i].sg_iovs[j].iov_buf == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}
out:
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

	rc = obj_ioc_begin(oei->oei_oid, oei->oei_map_ver, oei->oei_pool_uuid,
			   oei->oei_co_hdl, oei->oei_co_uuid, opc, &ioc);
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
	}

	if (opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		oeo->oeo_recxs.ca_count = 0;
		D_ALLOC(oeo->oeo_recxs.ca_arrays,
			oei->oei_nr * sizeof(daos_recx_t));
		if (oeo->oeo_recxs.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		enum_arg.recxs = oeo->oeo_recxs.ca_arrays;
		enum_arg.recxs_cap = oei->oei_nr;
		enum_arg.recxs_len = 0;
	} else {
		rc = obj_enum_prep_sgls(&oeo->oeo_sgl, &oei->oei_sgl, 1);
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
		struct obj_io_context *ioc, struct dtx_handle *dth)
{
	struct ds_cont_child *cont = ioc->ioc_coc;
	int	rc = 0;

	if (daos_is_zero_dti(&opi->opi_dti)) {
		D_DEBUG(DB_TRACE, "disable dtx\n");
		dth = NULL;
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
	uint32_t			 tgt_cnt;
	struct dtx_epoch		 epoch;
	int				 rc;

	opi = crt_req_get(rpc);
	D_ASSERT(opi != NULL);
	rc = obj_ioc_begin(opi->opi_oid, opi->opi_map_ver,
			    opi->opi_pool_uuid, opi->opi_co_hdl,
			    opi->opi_co_uuid, opc_get(rpc->cr_opc), &ioc);
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
	}

	tgts = opi->opi_shard_tgts.ca_arrays;
	tgt_cnt = opi->opi_shard_tgts.ca_count;

	if (!daos_is_zero_dti(&opi->opi_dti) && tgt_cnt != 0) {
		rc = obj_gen_dtx_mbs(tgts, &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	epoch.oe_value = opi->opi_epoch;
	epoch.oe_first = epoch.oe_value; /* unused for TGT_PUNCH */
	epoch.oe_flags = orf_to_dtx_epoch_flags(opi->opi_flags);

	/* Start the local transaction */
	rc = dtx_begin(ioc.ioc_coc, &opi->opi_dti, &epoch, 1,
		       opi->opi_map_ver, &opi->opi_oid,
		       opi->opi_dti_cos.ca_arrays,
		       opi->opi_dti_cos.ca_count, mbs, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for punch "DF_RC".\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* local RPC handler */
	rc = obj_local_punch(opi, opc_get(rpc->cr_opc), &ioc, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": error="DF_RC".\n", DP_UOID(opi->opi_oid),
			DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	if (DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		rc = -DER_IO;

	/* Stop the local transaction */
	rc = dtx_end(&dth, ioc.ioc_coc, rc);
	obj_punch_complete(rpc, rc, ioc.ioc_map_ver);
	D_FREE(mbs);
	obj_ioc_end(&ioc, rc);
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

		if (!(exec_arg->flags & ORF_RESEND)) {
			rc = obj_local_punch(opi, opc_get(rpc->cr_opc),
					     exec_arg->ioc, &dlh->dlh_handle);
		}
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
	struct obj_io_context		ioc;
	struct dtx_memberships		*mbs = NULL;
	struct daos_shard_tgt		*tgts = NULL;
	struct dtx_id			*dti_cos = NULL;
	int				dti_cos_cnt;
	uint32_t			tgt_cnt;
	uint32_t			flags = 0;
	uint32_t			version;
	struct dtx_epoch		epoch;
	int				rc;

	opi = crt_req_get(rpc);
	D_ASSERT(opi != NULL);
	rc = obj_ioc_begin(opi->opi_oid, opi->opi_map_ver,
			   opi->opi_pool_uuid, opi->opi_co_hdl,
			   opi->opi_co_uuid, opc_get(rpc->cr_opc), &ioc);
	if (rc)
		goto out;

	if (opi->opi_dkeys.ca_count == 0)
		D_DEBUG(DB_TRACE,
			"punch obj %p oid "DF_UOID" tag/xs %d/%d epc "
			DF_U64", pmv %u/%u dti "DF_DTI".\n",
			rpc, DP_UOID(opi->opi_oid),
			dss_get_module_info()->dmi_tgt_id,
			dss_get_module_info()->dmi_xs_id, opi->opi_epoch,
			opi->opi_map_ver, ioc.ioc_map_ver,
			DP_DTI(&opi->opi_dti));
	else
		D_DEBUG(DB_TRACE,
			"punch key %p oid "DF_UOID" dkey "
			DF_KEY" tag/xs %d/%d epc "
			DF_U64", pmv %u/%u dti "DF_DTI".\n",
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
		rc = obj_gen_dtx_mbs(tgts, &tgt_cnt, &mbs);
		if (rc != 0)
			D_GOTO(out, rc);
	}

again:
	/* Handle resend. */
	if (opi->opi_flags & ORF_RESEND) {
		daos_epoch_t	e = 0;

		rc = dtx_handle_resend(ioc.ioc_vos_coh, &opi->opi_dti,
				       &e, &version);
		if (rc == -DER_ALREADY)
			D_GOTO(out, rc = 0);

		if (rc == 0) {
			opi->opi_epoch = e;
			/* TODO: Also recovery the epoch uncertainty. */
			flags |= ORF_RESEND;
		} else if (rc == -DER_NONEXIST) {
			rc = 0;
		} else {
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
	rc = dtx_leader_begin(ioc.ioc_coc, &opi->opi_dti, &epoch, 1, version,
			      &opi->opi_oid, dti_cos, dti_cos_cnt,
			      tgts, tgt_cnt, mbs, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for punch "DF_RC".\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	exec_arg.rpc = rpc;
	exec_arg.ioc = &ioc;
	exec_arg.flags = flags;

	if (opi->opi_flags & ORF_DTX_SYNC)
		dlh.dlh_handle.dth_sync = 1;

	if (flags & ORF_RESEND)
		dlh.dlh_handle.dth_resent = 1;

	/* Execute the operation on all shards */
	rc = dtx_leader_exec_ops(&dlh, obj_tgt_punch, &exec_arg);

	if (DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
		rc = -DER_IO;

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
		goto again;
	case -DER_AGAIN:
		opi->opi_flags |= ORF_RESEND;
		goto again;
	default:
		break;
	}

	if (!(opi->opi_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		goto cleanup;

out:
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
	struct dss_sleep_ult		*sleep_ult = NULL;
	int				 retry = 0;
	int				 rc;

	okqi = crt_req_get(rpc);
	D_ASSERT(okqi != NULL);
	okqo = crt_reply_get(rpc);
	D_ASSERT(okqo != NULL);

	D_DEBUG(DB_IO, "flags = "DF_U64"\n", okqi->okqi_api_flags);

	rc = obj_ioc_begin(okqi->okqi_oid, okqi->okqi_map_ver,
			   okqi->okqi_pool_uuid, okqi->okqi_co_hdl,
			   okqi->okqi_co_uuid, opc_get(rpc->cr_opc), &ioc);
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

	rc = dtx_begin(ioc.ioc_coc, &okqi->okqi_dti, &epoch, 0,
		       okqi->okqi_map_ver, &okqi->okqi_oid, NULL, 0, NULL,
		       &dth);
	if (rc != 0)
		goto out;

	rc = vos_obj_query_key(ioc.ioc_vos_coh, okqi->okqi_oid,
			       okqi->okqi_api_flags, okqi->okqi_epoch, dkey,
			       akey, &okqo->okqo_recx, &dth);

	rc = dtx_end(&dth, ioc.ioc_coc, rc);

out:
	if (rc == -DER_INPROGRESS && dth.dth_local_retry) {
		if (++retry > 3)
			D_GOTO(failed, rc = -DER_TX_BUSY);

		/* XXX: Currently, we commit the distributed transaction
		 *	sychronously. Normally, it will be very quickly.
		 *	So let's wait on the server for a while (30 ms),
		 *	then retry. If related distributed transaction
		 *	is still not committed after several cycles try,
		 *	then replies '-DER_TX_BUSY' to the client.
		 */
		if (sleep_ult == NULL) {
			sleep_ult = dss_sleep_ult_create();
			if (sleep_ult == NULL)
				D_GOTO(failed, rc = -DER_TX_BUSY);
		}

		D_DEBUG(DB_IO, "Hit non-commit DTX when query "
			DF_UOID" (%d)\n", DP_UOID(okqi->okqi_oid), retry);
		dss_ult_sleep(sleep_ult, 30000);

		goto again;
	}

failed:
	if (sleep_ult != NULL)
		dss_sleep_ult_destroy(sleep_ult);

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

	D_DEBUG(DB_IO, "obj_sync start: "DF_UOID", epc "DF_U64"\n",
		DP_UOID(osi->osi_oid), oso->oso_epoch);

	rc = obj_ioc_begin(osi->osi_oid, osi->osi_map_ver,
			   osi->osi_pool_uuid, osi->osi_co_hdl,
			   osi->osi_co_uuid, opc_get(rpc->cr_opc), &ioc);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dtx_obj_sync(osi->osi_pool_uuid, osi->osi_co_uuid, ioc.ioc_coc,
			  &osi->osi_oid, oso->oso_epoch);

out:
	obj_reply_map_version_set(rpc, ioc.ioc_map_ver);
	obj_reply_set_status(rpc, rc);
	obj_ioc_end(&ioc, rc);

	D_DEBUG(DB_IO, "obj_sync stop: "DF_UOID", epc "DF_U64", rd = %d\n",
		DP_UOID(osi->osi_oid), oso->oso_epoch, rc);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
}

static int
obj_verify_bio_csum(crt_rpc_t *rpc, daos_iod_t *iods,
		    struct dcs_iod_csums *iod_csums, struct bio_desc *biod,
		    struct daos_csummer *csummer)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	uint64_t		 iods_nr = orw->orw_iod_array.oia_iod_nr;
	unsigned int		 i;
	int			 rc = 0;

	if (!obj_rpc_is_update(rpc) ||
	    !daos_csummer_initialized(csummer) ||
	    csummer->dcs_skip_data_verify ||
	    !csummer->dcs_srv_verify)
		return 0;

	for (i = 0; i < iods_nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct bio_sglist	*bsgl = bio_iod_sgl(biod, i);
		d_sg_list_t		 sgl;

		if (!ci_is_valid(iod_csums[i].ic_data)) {
			D_ERROR("Checksums is enabled but the csum info is "
				"invalid.");
			return -DER_CSUM;
		}

		rc = bio_sgl_convert(bsgl, &sgl, false);

		if (rc == 0)
			rc = daos_csummer_verify_iod(csummer, iod, &sgl,
						     &iod_csums[i], NULL, 0,
						     NULL);

		daos_sgl_fini(&sgl, false);

		if (rc != 0) {
			if (iod->iod_type == DAOS_IOD_SINGLE) {
				D_ERROR("Data Verification failed (object: "
					DF_OID"): %d\n",
					DP_OID(orw->orw_oid.id_pub), rc);
			}
			if (iod->iod_type == DAOS_IOD_ARRAY) {
				D_ERROR("Data Verification failed (object: "
						DF_OID ", "
						"extent: "DF_RECX"): %d\n",
					DP_OID(orw->orw_oid.id_pub),
					DP_RECX(iod->iod_recxs[i]),
					rc);
			}
			break;
		}
	}

	return rc;
}
