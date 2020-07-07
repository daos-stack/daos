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
#include <daos_srv/pool.h>
#include <daos_srv/rebuild.h>
#include <daos_srv/container.h>
#include <daos_srv/vos.h>
#include <daos_srv/bio.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/dtx_srv.h>
#include <daos/checksum.h>
#include "daos_srv/srv_csum.h"
#include "obj_rpc.h"
#include "obj_internal.h"

/* handles, pointers for handling I/O */
struct obj_io_context {
	struct ds_cont_hdl	*ioc_coh;
	struct ds_cont_child	*ioc_coc;
	daos_handle_t		 ioc_vos_coh;
	uint32_t		 ioc_map_ver;
	bool			 ioc_began;
};

static int
obj_verify_bio_csum(crt_rpc_t *rpc, struct bio_desc *biod,
		    struct daos_csummer *csummer);

static bool
obj_rpc_is_update(crt_rpc_t *rpc)
{
	return opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE ||
	       opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_TGT_UPDATE;
}

static bool
obj_rpc_is_fetch(crt_rpc_t *rpc)
{
	return opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH;
}

/**
 * After bulk finish, let's send reply, then release the resource.
 */
static int
obj_rw_complete(crt_rpc_t *rpc, struct ds_cont_child *cont,
		daos_handle_t ioh, int status, struct dtx_handle *dth)
{
	struct obj_rw_in	*orwi = crt_req_get(rpc);
	int			 rc;

	if (!daos_handle_is_inval(ioh)) {
		uint32_t map_version;
		bool update = obj_rpc_is_update(rpc);

		D_ASSERT(cont != NULL);
		map_version = cont->sc_pool->spc_map_version;
		rc = update ? vos_update_end(ioh, map_version, &orwi->orw_dkey,
					     status, dth) :
			      vos_fetch_end(ioh, status);

		if (rc != 0) {
			D_ERROR(DF_UOID "%s end failed: %d\n",
				DP_UOID(orwi->orw_oid),
				update ? "Update" : "Fetch", rc);
			if (status == 0)
				status = rc;
		}
	}

	return status;
}

static void
obj_rw_reply(crt_rpc_t *rpc, int status, uint32_t map_version,
	     struct dtx_conflict_entry *dce, struct ds_cont_hdl *cont_hdl)
{
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	if (dce != NULL)
		obj_reply_dtx_conflict_set(rpc, dce);

	D_DEBUG(DB_TRACE, "rpc %p opc %d send reply, pmv %d, status %d.\n",
		rpc, opc_get(rpc->cr_opc), map_version, status);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));

	if (obj_rpc_is_fetch(rpc)) {
		struct obj_rw_out	*orwo = crt_reply_get(rpc);

		if (orwo->orw_iod_sizes.ca_arrays != NULL) {
			D_FREE(orwo->orw_iod_sizes.ca_arrays);
			orwo->orw_iod_sizes.ca_count = 0;
		}

		if (orwo->orw_nrs.ca_arrays != NULL) {
			D_FREE(orwo->orw_nrs.ca_arrays);
			orwo->orw_nrs.ca_count = 0;
		}

		if (orwo->orw_iod_csum.ca_arrays != NULL) {
			D_FREE(orwo->orw_iod_csum.ca_arrays);
			orwo->orw_iod_csum.ca_count = 0;
		}

		if (cont_hdl) {
			daos_csummer_free_ic(cont_hdl->sch_csummer,
				&orwo->orw_iod_csum.ca_arrays);
			orwo->orw_iod_csum.ca_count = 0;
		}
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

bool
cont_prop_csum_enabled(struct ds_iv_ns *ns, uuid_t co_hdl)
{
	int			rc;
	daos_prop_t		cont_prop = {0};
	struct daos_prop_entry entry = {0};
	uint32_t		csum_val;

	entry.dpe_type = DAOS_PROP_CO_CSUM;
	cont_prop.dpp_entries = &entry;
	cont_prop.dpp_nr = 1;

	rc = cont_iv_prop_fetch(ns, co_hdl, &cont_prop);
	if (rc != 0)
		return false;
	csum_val = daos_cont_prop2csum(&cont_prop);
	if (daos_cont_csum_prop_is_enabled(csum_val))
		return true;
	else
		return false;
}

static int
obj_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op, bool bulk_bind,
		  crt_bulk_t *remote_bulks, uint64_t *remote_offs,
		  daos_handle_t ioh, d_sg_list_t **sgls, int sgl_nr)
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
			sgl = sgls[i];
		} else {
			struct bio_sglist *bsgl;

			D_ASSERT(!daos_handle_is_inval(ioh));
			bsgl = vos_iod_sgl_at(ioh, i);
			D_ASSERT(bsgl != NULL);

			sgl = &tmp_sgl;
			rc = bio_sgl_convert(bsgl, sgl);
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
			while (sgl->sg_iovs[idx].iov_buf == NULL &&
			       idx < sgl->sg_nr_out) {
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
				D_ERROR(DF_U64 "> %zu : %d\n", length,
					remote_bulk_size, -DER_OVERFLOW);
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
	if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_SDATA_CORRUPT)) {
		struct obj_rw_in	*orw = crt_req_get(rpc);
		struct ds_pool		*pool;

		pool = ds_pool_lookup(orw->orw_pool_uuid);
		if (pool == NULL)
			return -DER_NONEXIST;
		if (cont_prop_csum_enabled(pool->sp_iv_ns, orw->orw_co_hdl)) {
			struct bio_sglist	*fbsgl;
			d_sg_list_t		 fsgl;
			int			*fbuffer;

			D_DEBUG(DB_IO, "Data corruption after RDMA\n");
			fbsgl = vos_iod_sgl_at(ioh, 0);
			bio_sgl_convert(fbsgl, &fsgl);
			fbuffer = (int *)fsgl.sg_iovs[0].iov_buf;
			*fbuffer += 0x2;
			daos_sgl_fini(&fsgl, false);
		}
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
			       DAOS_HDL_INVAL, &p_sgl, orw->orw_nr);
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
		rc = bio_sgl_convert(bsgl, sgl);
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
obj_fetch_csum_init(struct ds_cont_hdl *cont_hdl,
			 struct obj_rw_in *orw,
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
	rc = daos_csummer_alloc_iods_csums(cont_hdl->sch_csummer,
					   orw->orw_iod_array.oia_iods,
					   orw->orw_iod_array.oia_iod_nr,
					   &orwo->orw_iod_csum.ca_arrays);

	if (rc >= 0) {
		orwo->orw_iod_csum.ca_count = (uint64_t)rc;
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

	for (i = 0; i < iods_nr; i++) {
		rc = ds_csum_add2iod(
			&iods[i], csummer,
			bio_iod_sgl(biod, i),
			&csum_infos[biov_csums_idx],
			&biov_csums_used, get_iod_csum(iod_csums, i));

		if (rc != 0)
			return rc;
		biov_csums_idx += biov_csums_used;
	}

	return rc;
}

/** Filter and prepare for the sing value EC update/fetch */
static void
obj_singv_ec_rw_filter(struct obj_rw_in *orw, daos_iod_t *iods, uint64_t *offs,
		       bool for_update)
{
	struct daos_oclass_attr		*oca = NULL;
	daos_iod_t			*iod;
	struct obj_ec_singv_local	 loc;
	uint32_t			 tgt_idx;
	uint32_t			 i;

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
		}
	}
}

static int
obj_local_rw(crt_rpc_t *rpc, struct ds_cont_hdl *cont_hdl,
	     struct ds_cont_child *cont, daos_iod_t *split_iods,
	     uint64_t *split_offs, struct dtx_handle *dth)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	uint32_t		tag = dss_get_module_info()->dmi_tgt_id;
	daos_handle_t		ioh = DAOS_HDL_INVAL;
	uint64_t		time_start = 0;
	struct obj_tls		*tls = obj_tls_get();
	struct bio_desc		*biod;
	daos_key_t		*dkey;
	crt_bulk_op_t		bulk_op;
	bool			rma;
	bool			bulk_bind;
	bool			size_fetch = false;
	daos_iod_t		*iods;
	uint64_t		*offs;
	int			err, rc = 0;

	D_TIME_START(tls->ot_sp, time_start, OBJ_PF_UPDATE_LOCAL);

	if (daos_is_zero_dti(&orw->orw_dti)) {
		D_DEBUG(DB_TRACE, "disable dtx\n");
		dth = NULL;
	}

	iods = split_iods == NULL ? orw->orw_iod_array.oia_iods : split_iods;
	offs = split_offs == NULL ? orw->orw_iod_array.oia_offs : split_offs;

	if (daos_obj_is_echo(orw->orw_oid.id_pub) ||
	    (daos_io_bypass & IOBP_TARGET)) {
		obj_echo_rw(rpc, split_iods, split_offs);
		D_GOTO(out, rc = 0);
	}

	dkey = (daos_key_t *)&orw->orw_dkey;
	D_DEBUG(DB_TRACE,
		"opc %d oid "DF_UOID" dkey "DF_KEY" tag %d epc "DF_U64".\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid), DP_KEY(dkey),
		tag, orw->orw_epoch);

	rma = (orw->orw_bulks.ca_arrays != NULL ||
	       orw->orw_bulks.ca_count != 0);

	/* Prepare IO descriptor */
	if (obj_rpc_is_update(rpc)) {
		obj_singv_ec_rw_filter(orw, iods, offs, true);
		bulk_op = CRT_BULK_GET;
		rc = vos_update_begin(cont->sc_hdl, orw->orw_oid,
				      orw->orw_epoch, dkey, orw->orw_nr, iods,
				      orw->orw_iod_csums.ca_arrays, &ioh, dth);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: "DF_RC"\n",
				DP_UOID(orw->orw_oid), DP_RC(rc));
			goto out;
		}
	} else {
		size_fetch = (!rma && orw->orw_sgls.ca_arrays == NULL);
		bulk_op = CRT_BULK_PUT;

		rc = vos_fetch_begin(cont->sc_hdl, orw->orw_oid, orw->orw_epoch,
				     dkey, orw->orw_nr, iods, size_fetch, &ioh);

		if (rc) {
			D_CDEBUG(rc == -DER_INPROGRESS, DB_IO, DLOG_ERR,
				 " Fetch begin for "DF_UOID" failed: "DF_RC"\n",
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
		obj_singv_ec_rw_filter(orw, iods, offs, false);
	}

	biod = vos_ioh2desc(ioh);
	rc = bio_iod_prep(biod);
	if (rc) {
		D_ERROR(DF_UOID" bio_iod_prep failed: "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		goto out;
	}

	if (obj_rpc_is_fetch(rpc) && !size_fetch) {
		rc = obj_fetch_csum_init(cont_hdl, orw, orwo);
		if (rc) {
			D_ERROR(DF_UOID" fetch csum init failed: %d.\n",
				DP_UOID(orw->orw_oid), rc);
			goto post;
		}
		rc = csum_add2iods(ioh,
				   orw->orw_iod_array.oia_iods,
				   orw->orw_iod_array.oia_iod_nr,
				   cont_hdl->sch_csummer,
				   orwo->orw_iod_csum.ca_arrays);

		if (rc) {
			D_ERROR(DF_UOID" fetch verify failed: %d.\n",
				DP_UOID(orw->orw_oid), rc);
			goto post;
		}
	}

	if (rma) {
		bulk_bind = orw->orw_flags & ORF_BULK_BIND;
		rc = obj_bulk_transfer(rpc, bulk_op, bulk_bind,
				       orw->orw_bulks.ca_arrays, offs,
				       ioh, NULL, orw->orw_nr);
	} else if (orw->orw_sgls.ca_arrays != NULL) {
		int feats = daos_obj_id2feat(orw->orw_oid.id_pub);

		rc = bio_iod_copy(biod, orw->orw_sgls.ca_arrays, orw->orw_nr,
				  (feats & DAOS_OF_NO_INL_COPY) &&
				  (daos_io_bypass & IOBP_INLINE_COPY));
	}

	if (rc) {
		if (rc == -DER_OVERFLOW)
			rc = -DER_REC2BIG;

		D_ERROR(DF_UOID" data transfer failed, dma %d rc "DF_RC"",
			DP_UOID(orw->orw_oid), rma, DP_RC(rc));
		D_GOTO(post, rc);
	}

	rc = obj_verify_bio_csum(rpc, biod, cont_hdl->sch_csummer);
post:
	err = bio_iod_post(biod);
	rc = rc ? : err;
out:
	rc = obj_rw_complete(rpc, cont, ioh, rc, dth);
	D_TIME_END(tls->ot_sp, time_start, OBJ_PF_UPDATE_LOCAL);
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
	rc = cont_iv_capa_fetch(pool_uuid, coh_uuid, cont_uuid, &coh);
	if (rc) {
		if (rc == -DER_NONEXIST)
			rc = -DER_NO_HDL;
		return rc;
	}

	if (obj_is_modification_opc(opc) && !(coh->sch_capas & DAOS_COO_RW)) {
		D_ERROR("cont "DF_UUID" hdl "DF_UUID" sch_capas "DF_U64", "
			"NO_PERM to update.\n", DP_UUID(cont_uuid),
			DP_UUID(coh_uuid), coh->sch_capas);
		D_GOTO(failed, rc = -DER_NO_PERM);
	}

	/* normal container open handle with ds_cont_child attached */
	if (coh->sch_cont != NULL) {
		ds_cont_child_get(coh->sch_cont);
		coc = coh->sch_cont;
		D_GOTO(out, rc = 0);
	}

	if (!is_rebuild_container(pool_uuid, coh_uuid)) {
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
	ioc->ioc_map_ver = 0;
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
		 *	current replica. Two possibile cases:
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

		D_GOTO(out_put, rc);
	} else if (rpc_map_ver < ioc->ioc_map_ver) {
		D_DEBUG(DB_IO, "stale version req %d map_version %d\n",
			rpc_map_ver, ioc->ioc_map_ver);
		if (obj_is_modification_opc(opc))
			D_GOTO(out_put, rc = -DER_STALE);
		/* It is harmless if fetch with old pool map version. */
	}
	dss_rpc_cntr_enter(DSS_RC_OBJ);
	ioc->ioc_began = true;
	return 0;

out_put:
	obj_ioc_fini(ioc);
	return rc;
}

void
obj_ioc_end(struct obj_io_context *ioc, int err)
{
	if (ioc->ioc_began) {
		dss_rpc_cntr_exit(DSS_RC_OBJ, !!err);
		ioc->ioc_began = false;
	}
	obj_ioc_fini(ioc);
}

void
ds_obj_tgt_update_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	daos_key_t			*dkey = &orw->orw_dkey;
	struct obj_io_context		 ioc;
	struct dtx_handle                dth = { 0 };
	struct dtx_conflict_entry	 conflict = { 0 };
	uint32_t			 opc = opc_get(rpc->cr_opc);
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

	D_DEBUG(DB_TRACE,
		"rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_U64", pmv %u/%u dti "DF_DTI".\n",
		rpc, opc, DP_UOID(orw->orw_oid), DP_KEY(dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id, orw->orw_epoch,
		orw->orw_map_ver, ioc.ioc_map_ver, DP_DTI(&orw->orw_dti));

	/* Handle resend. */
	if (orw->orw_flags & ORF_RESEND) {
		rc = dtx_handle_resend(ioc.ioc_vos_coh, &orw->orw_oid,
				       &orw->orw_dti,
				       orw->orw_dkey_hash, false,
				       &orw->orw_epoch);

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

		if (rc != 0 && rc != -DER_NONEXIST)
			D_GOTO(out, rc);
	}

	/* Inject failure for test to simulate the case of lost some
	 * record/akey/dkey on some non-leader.
	 */
	if (DAOS_FAIL_CHECK(DAOS_VC_LOST_DATA)) {
		if (orw->orw_dti_cos.ca_count > 0)
			vos_dtx_commit(ioc.ioc_vos_coh,
				       orw->orw_dti_cos.ca_arrays,
				       orw->orw_dti_cos.ca_count);

		D_GOTO(out, rc = 0);
	}

	rc = dtx_begin(&orw->orw_dti, &orw->orw_oid, ioc.ioc_vos_coh,
		       orw->orw_epoch, orw->orw_dkey_hash,
		       &conflict, orw->orw_dti_cos.ca_arrays,
		       orw->orw_dti_cos.ca_count, orw->orw_map_ver,
		       DAOS_INTENT_UPDATE, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for update "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = obj_local_rw(rpc, ioc.ioc_coh, ioc.ioc_coc, NULL, NULL, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": error="DF_RC".\n", DP_UOID(orw->orw_oid),
			DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	if (opc == DAOS_OBJ_RPC_TGT_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		rc = -DER_IO;

	rc = dtx_end(&dth, ioc.ioc_coh, ioc.ioc_coc, rc);
	obj_rw_reply(rpc, rc, ioc.ioc_map_ver, &conflict, ioc.ioc_coh);
	obj_ioc_end(&ioc, rc);
}

static int
obj_tgt_update(struct dtx_leader_handle *dlh, void *arg, int idx,
		  dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg	*exec_arg = arg;

	/* handle local operaion */
	if (idx == -1) {
		struct obj_ec_split_req	*split_req = exec_arg->args;
		daos_iod_t		*split_iods;
		uint64_t		*split_offs;
		int			 rc = 0;

		/* No need re-exec local update */
		if (!(exec_arg->flags & ORF_RESEND)) {
			split_iods = split_req != NULL ? split_req->osr_iods :
							 NULL;
			split_offs = split_req != NULL ? split_req->osr_offs :
							 NULL;
			rc = obj_local_rw(exec_arg->rpc, exec_arg->cont_hdl,
					  exec_arg->cont, split_iods,
					  split_offs, &dlh->dlh_handle);
		}
		if (comp_cb != NULL)
			comp_cb(dlh, idx, rc);

		return rc;
	}

	/* Handle the object remotely */
	return ds_obj_remote_update(dlh, arg, idx, comp_cb);
}

void
ds_obj_rw_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	struct obj_tls			*tls = obj_tls_get();
	struct dtx_leader_handle	dlh = { 0 };
	struct ds_obj_exec_arg		exec_arg = { 0 };
	struct obj_io_context		ioc;
	uint64_t			time_start = 0;
	uint32_t			flags = 0;
	uint32_t			opc = opc_get(rpc->cr_opc);
	struct obj_ec_split_req		*split_req = NULL;
	int				rc;

	D_ASSERT(orw != NULL);
	D_ASSERT(orwo != NULL);

	rc = obj_ioc_begin(orw->orw_oid, orw->orw_map_ver,
			   orw->orw_pool_uuid, orw->orw_co_hdl,
			   orw->orw_co_uuid, opc_get(rpc->cr_opc), &ioc);
	if (rc != 0) {
		D_ASSERTF(rc < 0, "unexpected error# "DF_RC"\n", DP_RC(rc));
		goto reply;
	}

	D_DEBUG(DB_TRACE,
		"rpc %p opc %d oid "DF_UOID" dkey "DF_KEY" tag/xs %d/%d epc "
		DF_U64", pmv %u/%u dti "DF_DTI".\n",
		rpc, opc, DP_UOID(orw->orw_oid), DP_KEY(&orw->orw_dkey),
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id, orw->orw_epoch,
		orw->orw_map_ver, ioc.ioc_map_ver, DP_DTI(&orw->orw_dti));

	/* FIXME: until distributed transaction. */
	if (orw->orw_epoch == DAOS_EPOCH_MAX) {
		orw->orw_epoch = crt_hlc_get();
		D_DEBUG(DB_IO, "overwrite epoch "DF_U64"\n", orw->orw_epoch);
	}

	if (obj_rpc_is_fetch(rpc)) {
		rc = obj_local_rw(rpc, ioc.ioc_coh, ioc.ioc_coc,
				  NULL, NULL, NULL);
		D_GOTO(out, rc);
	} else if (orw->orw_iod_array.oia_oiods != NULL) {
		rc = obj_ec_rw_req_split(orw, &split_req);
		if (rc != 0) {
			D_ERROR(DF_UOID": obj_ec_rw_req_split failed, rc %d.\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out, rc);
		}
	}

	/* Handle resend. */
	if (orw->orw_flags & ORF_RESEND) {
		daos_epoch_t	tmp = 0;

		rc = dtx_handle_resend(ioc.ioc_vos_coh, &orw->orw_oid,
				       &orw->orw_dti, orw->orw_dkey_hash,
				       false, &tmp);
		if (rc == -DER_ALREADY)
			D_GOTO(out, rc = 0);

		if (rc == 0) {
			flags |= ORF_RESEND;
			orw->orw_epoch = tmp;
		} else if (rc == -DER_NONEXIST) {
			rc = 0;
		} else {
			D_GOTO(out, rc);
		}
	} else if (DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REQUEST)) {
		goto cleanup;
	}

	D_TIME_START(tls->ot_sp, time_start, OBJ_PF_UPDATE);

renew:
	/*
	 * Since we do not know if other replicas execute the
	 * operation, so even the operation has been execute
	 * locally, we will start dtx and forward reqests to
	 * all replicas.
	 *
	 * For new leader, even though the local replica
	 * has ever been modified before, but it doesn't
	 * know whether other replicas have also done the
	 * modification or not, so still need to dispatch
	 * the RPC to other replicas.
	 */
	rc = dtx_leader_begin(&orw->orw_dti, &orw->orw_oid, ioc.ioc_vos_coh,
			      orw->orw_epoch, orw->orw_dkey_hash,
			      orw->orw_map_ver, DAOS_INTENT_UPDATE,
			      orw->orw_shard_tgts.ca_arrays,
			      orw->orw_shard_tgts.ca_count, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for update "DF_RC".\n",
			DP_UOID(orw->orw_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (orw->orw_flags & ORF_DTX_SYNC)
		dlh.dlh_handle.dth_sync = 1;

	exec_arg.rpc	  = rpc;
	exec_arg.cont_hdl = ioc.ioc_coh;
	exec_arg.cont	  = ioc.ioc_coc;
	exec_arg.args	  = split_req;
again:
	exec_arg.flags	  = flags;
	/* Execute the operation on all targets */
	rc = dtx_leader_exec_ops(&dlh, obj_tgt_update, &exec_arg);
out:
	if (opc == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
		rc = -DER_IO;

	/* Stop the distribute transaction */
	rc = dtx_leader_end(&dlh, ioc.ioc_coc, rc);
	if (rc == -DER_AGAIN) {
		if (dlh.dlh_handle.dth_renew) {
			/* epoch conflict, renew it and retry. */
			orw->orw_epoch = crt_hlc_get();
			flags &= ~ORF_RESEND;
			memset(&dlh, 0, sizeof(dlh));
			D_GOTO(renew, rc);
		}

		flags |= ORF_RESEND;
		D_GOTO(again, rc);
	}

	if (opc == DAOS_OBJ_RPC_UPDATE && !(orw->orw_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		goto cleanup;

reply:
	obj_rw_reply(rpc, rc, ioc.ioc_map_ver, NULL, ioc.ioc_coh);

cleanup:
	D_TIME_END(tls->ot_sp, time_start, OBJ_PF_UPDATE);
	obj_ec_split_req_fini(split_req);
	obj_ioc_end(&ioc, rc);
}

static void
obj_enum_complete(crt_rpc_t *rpc, int status, int map_version)
{
	struct obj_key_enum_out *oeo;
	struct obj_key_enum_in *oei;
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));

	oei = crt_req_get(rpc);
	D_ASSERT(oei != NULL);
	oeo = crt_reply_get(rpc);
	D_ASSERT(oeo != NULL);

	if (oeo->oeo_kds.ca_arrays != NULL)
		D_FREE(oeo->oeo_kds.ca_arrays);

	if (oeo->oeo_sgl.sg_iovs != NULL)
		daos_sgl_fini(&oeo->oeo_sgl, true);

	if (oeo->oeo_eprs.ca_arrays != NULL)
		D_FREE(oeo->oeo_eprs.ca_arrays);

	if (oeo->oeo_recxs.ca_arrays != NULL)
		D_FREE(oeo->oeo_recxs.ca_arrays);
}

static int
obj_local_enum(struct obj_io_context *ioc, crt_rpc_t *rpc,
	       struct vos_iter_anchors *anchors, struct dss_enum_arg *enum_arg)
{
	vos_iter_param_t	param = { 0 };
	struct obj_key_enum_in	*oei = crt_req_get(rpc);
	int			opc = opc_get(rpc->cr_opc);
	int			type;
	int			rc;
	bool			recursive = false;

	/* prepare enumeration parameters */
	param.ip_hdl = ioc->ioc_vos_coh;
	param.ip_oid = oei->oei_oid;
	if (oei->oei_dkey.iov_len > 0)
		param.ip_dkey = oei->oei_dkey;
	if (oei->oei_akey.iov_len > 0)
		param.ip_akey = oei->oei_akey;

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

	rc = dss_enum_pack(&param, type, recursive, anchors, enum_arg);

	if (type == VOS_ITER_SINGLE)
		anchors->ia_ev = anchors->ia_sv;

	D_DEBUG(DB_IO, ""DF_UOID" iterate "DF_U64"-"DF_U64" type %d tag %d"
		" rc %d\n", DP_UOID(oei->oei_oid), param.ip_epr.epr_lo,
		param.ip_epr.epr_hi, type, dss_get_module_info()->dmi_tgt_id,
		rc);
failed:
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
			       DAOS_HDL_INVAL, sgls, idx);
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

		/* Prepare key desciptor buffer */
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
	rc = obj_local_enum(&ioc, rpc, &anchors, &enum_arg);
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
	}

	rc = obj_enum_reply_bulk(rpc);
out:
	/* for KEY2BIG case, just reuse the oeo_size to reply the key len */
	if (rc == -DER_KEY2BIG) {
		D_ASSERT(enum_arg.kds != NULL);
		oeo->oeo_size = enum_arg.kds[0].kd_key_len;
	}
	obj_enum_complete(rpc, rc, ioc.ioc_map_ver);
	obj_ioc_end(&ioc, rc);
}

static void
obj_punch_complete(crt_rpc_t *rpc, int status, uint32_t map_version,
		   struct dtx_conflict_entry *dce)
{
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	if (dce != NULL)
		obj_reply_dtx_conflict_set(rpc, dce);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: "DF_RC"\n", DP_RC(rc));
}

static int
obj_local_punch(struct obj_punch_in *opi, crt_opcode_t opc,
		struct ds_cont_hdl *cont_hdl, struct ds_cont_child *cont,
		struct dtx_handle *dth)
{
	int	rc = 0;

	if (daos_is_zero_dti(&opi->opi_dti)) {
		D_DEBUG(DB_TRACE, "disable dtx\n");
		dth = NULL;
	}

	switch (opc) {
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH:
		rc = vos_obj_punch(cont->sc_hdl, opi->opi_oid,
				   opi->opi_epoch, opi->opi_map_ver, 0,
				   NULL, 0, NULL, dth);
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
				   opi->opi_epoch, opi->opi_map_ver, 0, dkey,
				   opi->opi_akeys.ca_count,
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
	struct dtx_conflict_entry	 conflict = { 0 };
	struct obj_io_context		 ioc;
	struct obj_punch_in		*opi;
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
		rc = dtx_handle_resend(ioc.ioc_vos_coh, &opi->opi_oid,
				       &opi->opi_dti, opi->opi_dkey_hash,
				       true, &opi->opi_epoch);

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

		if (rc != 0 && rc != -DER_NONEXIST)
			D_GOTO(out, rc);
	}

	/* Start the local transaction */
	rc = dtx_begin(&opi->opi_dti, &opi->opi_oid, ioc.ioc_vos_coh,
		       opi->opi_epoch, opi->opi_dkey_hash,
		       &conflict, opi->opi_dti_cos.ca_arrays,
		       opi->opi_dti_cos.ca_count, opi->opi_map_ver,
		       DAOS_INTENT_PUNCH, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for punch "DF_RC".\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* local RPC handler */
	rc = obj_local_punch(opi, opc_get(rpc->cr_opc), ioc.ioc_coh,
			     ioc.ioc_coc, &dth);
	if (rc != 0) {
		D_ERROR(DF_UOID": error="DF_RC".\n", DP_UOID(opi->opi_oid),
			DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	if (DAOS_FAIL_CHECK(DAOS_DTX_NONLEADER_ERROR))
		rc = -DER_IO;

	/* Stop the local transaction */
	rc = dtx_end(&dth, ioc.ioc_coh, ioc.ioc_coc, rc);
	obj_punch_complete(rpc, rc, ioc.ioc_map_ver, &conflict);
	obj_ioc_end(&ioc, rc);
}

static int
obj_tgt_punch(struct dtx_leader_handle *dlh, void *arg, int idx,
		 dtx_sub_comp_cb_t comp_cb)
{
	struct ds_obj_exec_arg	*exec_arg = arg;

	/* handle local operaion */
	if (idx == -1) {
		crt_rpc_t		*rpc = exec_arg->rpc;
		struct obj_punch_in	*opi = crt_req_get(rpc);
		int			rc = 0;

		if (!(exec_arg->flags & ORF_RESEND)) {
			rc = obj_local_punch(opi, opc_get(rpc->cr_opc),
					     exec_arg->cont_hdl,
					     exec_arg->cont, &dlh->dlh_handle);
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
	struct dtx_leader_handle	dlh = { 0 };
	struct obj_punch_in		*opi;
	struct ds_obj_exec_arg		exec_arg = { 0 };
	struct obj_io_context		ioc;
	uint32_t			flags = 0;
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

	/* FIXME: until distributed transaction. */
	if (opi->opi_epoch == DAOS_EPOCH_MAX) {
		opi->opi_epoch = crt_hlc_get();
		D_DEBUG(DB_IO, "overwrite epoch "DF_U64"\n", opi->opi_epoch);
	}

	if (opi->opi_shard_tgts.ca_arrays == NULL) {
		/* local RPC handler */
		rc = obj_local_punch(opi, opc_get(rpc->cr_opc), ioc.ioc_coh,
				     ioc.ioc_coc, NULL);
		if (rc != 0) {
			D_ERROR(DF_UOID": error="DF_RC".\n",
				DP_UOID(opi->opi_oid), DP_RC(rc));
			D_GOTO(out, rc);
		}

		D_GOTO(out, rc);
	}

	/* Handle resend. */
	if (opi->opi_flags & ORF_RESEND) {
		daos_epoch_t	tmp = 0;

		rc = dtx_handle_resend(ioc.ioc_vos_coh, &opi->opi_oid,
				       &opi->opi_dti, opi->opi_dkey_hash,
				       true, &tmp);
		if (rc == -DER_ALREADY)
			D_GOTO(out, rc = 0);

		if (rc == 0) {
			opi->opi_epoch = tmp;
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

renew:
	/*
	 * Since we do not know if other replicas execute the
	 * operation, so even the operation has been execute
	 * locally, we will start dtx and forward reqests to
	 * all replicas.
	 *
	 * For new leader, even though the local replica
	 * has ever been modified before, but it doesn't
	 * know whether other replicas have also done the
	 * modification or not, so still need to dispatch
	 * the RPC to other replicas.
	 */
	rc = dtx_leader_begin(&opi->opi_dti, &opi->opi_oid, ioc.ioc_vos_coh,
			      opi->opi_epoch, opi->opi_dkey_hash,
			      opi->opi_map_ver, DAOS_INTENT_PUNCH,
			      opi->opi_shard_tgts.ca_arrays,
			      opi->opi_shard_tgts.ca_count, &dlh);
	if (rc != 0) {
		D_ERROR(DF_UOID": Failed to start DTX for punch "DF_RC".\n",
			DP_UOID(opi->opi_oid), DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (opi->opi_flags & ORF_DTX_SYNC)
		dlh.dlh_handle.dth_sync = 1;

	exec_arg.rpc = rpc;
	exec_arg.cont_hdl = ioc.ioc_coh;
	exec_arg.cont = ioc.ioc_coc;
again:
	exec_arg.flags = flags;
	/* Execute the operation on all shards */
	rc = dtx_leader_exec_ops(&dlh, obj_tgt_punch, &exec_arg);
out:
	if (DAOS_FAIL_CHECK(DAOS_DTX_LEADER_ERROR))
		rc = -DER_IO;

	/* Stop the distribute transaction */
	rc = dtx_leader_end(&dlh, ioc.ioc_coc, rc);
	if (rc == -DER_AGAIN) {
		if (dlh.dlh_handle.dth_renew) {
			/* epoch conflict, renew it and retry. */
			opi->opi_epoch = crt_hlc_get();
			flags &= ~ORF_RESEND;
			memset(&dlh, 0, sizeof(dlh));
			D_GOTO(renew, rc);
		}

		flags |= ORF_RESEND;
		D_GOTO(again, rc);
	}

	if (!(opi->opi_flags & ORF_RESEND) &&
	    DAOS_FAIL_CHECK(DAOS_DTX_LOST_RPC_REPLY))
		goto cleanup;

	obj_punch_complete(rpc, rc, ioc.ioc_map_ver, NULL);
cleanup:
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
	int				 rc;

	okqi = crt_req_get(rpc);
	D_ASSERT(okqi != NULL);
	okqo = crt_reply_get(rpc);
	D_ASSERT(okqo != NULL);

	D_DEBUG(DB_IO, "flags = %d\n", okqi->okqi_flags);

	/* FIXME: until distributed transaction. */
	if (okqi->okqi_epoch == DAOS_EPOCH_MAX) {
		okqi->okqi_epoch = crt_hlc_get();
		D_DEBUG(DB_IO, "overwrite epoch "DF_U64"\n", okqi->okqi_epoch);
	}

	rc = obj_ioc_begin(okqi->okqi_oid, okqi->okqi_map_ver,
			   okqi->okqi_pool_uuid, okqi->okqi_co_hdl,
			   okqi->okqi_co_uuid, opc_get(rpc->cr_opc), &ioc);
	if (rc)
		D_GOTO(out, rc);

	dkey = &okqi->okqi_dkey;
	akey = &okqi->okqi_akey;
	d_iov_set(&okqo->okqo_akey, NULL, 0);
	d_iov_set(&okqo->okqo_dkey, NULL, 0);
	if (okqi->okqi_flags & DAOS_GET_DKEY)
		dkey = &okqo->okqo_dkey;
	if (okqi->okqi_flags & DAOS_GET_AKEY)
		akey = &okqo->okqo_akey;

	rc = vos_obj_query_key(ioc.ioc_vos_coh, okqi->okqi_oid,
			       okqi->okqi_flags, okqi->okqi_epoch,
			       dkey, akey, &okqo->okqo_recx);
out:
	obj_reply_set_status(rpc, rc);
	obj_reply_map_version_set(rpc, ioc.ioc_map_ver);
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

	rc = dtx_obj_sync(osi->osi_pool_uuid, osi->osi_co_uuid, ioc.ioc_vos_coh,
			  osi->osi_oid, oso->oso_epoch, ioc.ioc_map_ver);

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
cont_prop_srv_verify(struct ds_iv_ns *ns, uuid_t co_hdl)
{
	int			rc;
	daos_prop_t		cont_prop = {0};
	struct daos_prop_entry	entry = {0};

	entry.dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	cont_prop.dpp_entries = &entry;
	cont_prop.dpp_nr = 1;

	rc = cont_iv_prop_fetch(ns, co_hdl, &cont_prop);
	if (rc != 0)
		return false;
	return daos_cont_prop2serververify(&cont_prop);
}

static int
obj_verify_bio_csum(crt_rpc_t *rpc, struct bio_desc *biod,
		    struct daos_csummer *csummer)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct ds_pool		*pool;
	daos_iod_t		*iods = orw->orw_iod_array.oia_iods;
	uint64_t		 iods_nr = orw->orw_iod_array.oia_iod_nr;
	struct dcs_iod_csums	*iods_csums = orw->orw_iod_csums.ca_arrays;
	unsigned int		 i;
	int			 rc = 0;

	if (!daos_csummer_initialized(csummer))
		return 0;

	pool = ds_pool_lookup(orw->orw_pool_uuid);
	if (pool == NULL)
		return -DER_NONEXIST;

	if (!obj_rpc_is_update(rpc) ||
	    !cont_prop_srv_verify(pool->sp_iv_ns, orw->orw_co_hdl)) {
		ds_pool_put(pool);
		return 0;
	}

	for (i = 0; i < iods_nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct bio_sglist	*bsgl = bio_iod_sgl(biod, i);
		d_sg_list_t		 sgl;

		if (!ci_is_valid(iods_csums[i].ic_data)) {
			D_ERROR("Checksums is enabled but the csum info is "
				"invalid.");
			return -DER_CSUM;
		}

		rc = bio_sgl_convert(bsgl, &sgl);

		if (rc == 0)
			rc = daos_csummer_verify(csummer, iod, &sgl,
						 &iods_csums[i]);

		daos_sgl_fini(&sgl, false);

		if (rc != 0) {
			D_ERROR("Verify failed: %d\n", rc);
			break;
		}
	}

	ds_pool_put(pool);
	return rc;
}
