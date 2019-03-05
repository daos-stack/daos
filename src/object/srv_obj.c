/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include "obj_rpc.h"
#include "obj_internal.h"

static inline struct obj_tls *
obj_tls_get()
{
	return dss_module_key_get(dss_tls_get(), &obj_module_key);
}

/**
 * After bulk finish, let's send reply, then release the resource.
 */
static void
ds_obj_rw_complete(crt_rpc_t *rpc, struct ds_cont_hdl *cont_hdl,
		   daos_handle_t ioh, int status, uint32_t map_version)
{
	struct obj_rw_in	*orwi;
	struct obj_rw_out	*orwo;
	int			rc;

	orwi = crt_req_get(rpc);
	orwo = crt_reply_get(rpc);

	if (!daos_handle_is_inval(ioh)) {
		bool update = (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE);

		rc = update ? vos_update_end(ioh, map_version, &orwi->orw_dkey,
					     status) :
			      vos_fetch_end(ioh, status);

		if (rc != 0) {
			D_ERROR(DF_UOID "%s end failed: %d\n",
				DP_UOID(orwi->orw_oid),
				update ? "Update" : "Fetch", rc);
			if (status == 0)
				status = rc;
		}
	}

	if (cont_hdl != NULL && cont_hdl->sch_cont != NULL) {
		rc = vos_oi_get_attr(cont_hdl->sch_cont->sc_hdl, orwi->orw_oid,
				     orwi->orw_epoch, &orwo->orw_attr);
		if (rc) {
			D_ERROR(DF_UOID" can not get status: rc %d\n",
				DP_UOID(orwi->orw_oid), rc);
			if (status == 0)
				status = rc;
		}
	}

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);

	D_DEBUG(DB_TRACE, "rpc %p opc %d send reply, status %d.\n",
		rpc, opc_get(rpc->cr_opc), status);
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
		if (orwo->orw_sizes.ca_arrays != NULL) {
			D_FREE(orwo->orw_sizes.ca_arrays);
			orwo->orw_sizes.ca_count = 0;
		}

		if (orwo->orw_nrs.ca_arrays != NULL) {
			D_FREE(orwo->orw_nrs.ca_arrays);
			orwo->orw_nrs.ca_count = 0;
		}
	}
}

struct ds_bulk_async_args {
	int		bulks_inflight;
	ABT_eventual	eventual;
	int		result;
};

static int
bulk_complete_cb(const struct crt_bulk_cb_info *cb_info)
{
	struct ds_bulk_async_args	*arg;
	struct crt_bulk_desc		*bulk_desc;
	crt_rpc_t			*rpc;
	crt_bulk_t			local_bulk_hdl;

	if (cb_info->bci_rc != 0)
		D_ERROR("bulk transfer failed: %d\n", cb_info->bci_rc);

	bulk_desc = cb_info->bci_bulk_desc;
	local_bulk_hdl = bulk_desc->bd_local_hdl;
	rpc = bulk_desc->bd_rpc;
	arg = (struct ds_bulk_async_args *)cb_info->bci_arg;
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
bulk_bypass(daos_sg_list_t *sgl, crt_bulk_op_t bulk_op)
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
ds_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op, bool bulk_bind,
		 crt_bulk_t *remote_bulks, daos_handle_t ioh,
		 daos_sg_list_t **sgls, int sgl_nr)
{
	struct ds_bulk_async_args arg = { 0 };
	crt_bulk_opid_t		bulk_opid;
	crt_bulk_perm_t		bulk_perm;
	int			i, rc, *status, ret;

	bulk_perm = bulk_op == CRT_BULK_PUT ? CRT_BULK_RO : CRT_BULK_RW;
	rc = ABT_eventual_create(sizeof(*status), &arg.eventual);
	if (rc != 0)
		return dss_abterr2der(rc);

	D_DEBUG(DB_IO, "bulk_op:%d sgl_nr%d\n", bulk_op, sgl_nr);

	for (i = 0; i < sgl_nr; i++) {
		daos_sg_list_t		*sgl, tmp_sgl;
		struct crt_bulk_desc	 bulk_desc;
		crt_bulk_t		 local_bulk_hdl;
		daos_size_t		 offset = 0;
		unsigned int		 idx = 0;

		if (remote_bulks[i] == NULL)
			continue;

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
			bulk_bypass(sgl, bulk_op);
			goto next;
		}

		/**
		 * Let's walk through the sgl to check if the iov is empty,
		 * which is usually gotten from punched/empty records (see
		 * akey_fetch()), and skip these empty iov during bulk
		 * transfer to avoid touching the input buffer.
		 */
		while (idx < sgl->sg_nr_out) {
			daos_sg_list_t	sgl_sent;
			daos_size_t	length = 0;
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

			sgl_sent.sg_nr = idx - start;
			sgl_sent.sg_nr_out = idx - start;

			rc = crt_bulk_create(rpc->cr_ctx,
					     daos2crt_sg(&sgl_sent),
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
					bulk_complete_cb, &arg, &bulk_opid);
			else
				rc = crt_bulk_transfer(&bulk_desc,
					bulk_complete_cb, &arg, &bulk_opid);
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

	if (arg.bulks_inflight == 0)
		ABT_eventual_set(arg.eventual, &rc, sizeof(rc));

	ret = ABT_eventual_wait(arg.eventual, (void **)&status);
	if (rc == 0)
		rc = ret ? dss_abterr2der(ret) : *status;

	ABT_eventual_free(&arg.eventual);
	return rc;
}

static int
ds_sgls_prep(daos_sg_list_t *dst_sgls, daos_sg_list_t *sgls, int number)
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

static int
ds_obj_update_sizes_in_reply(crt_rpc_t *rpc)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	daos_iod_t		*iods;
	uint64_t		*sizes;
	int			size_count;
	int			i;

	D_ASSERT(opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH);

	D_ASSERT(orwo != NULL);
	D_ASSERT(orw != NULL);

	iods = orw->orw_iods.ca_arrays;
	size_count = orw->orw_iods.ca_count;

	orwo->orw_sizes.ca_count = size_count;
	D_ALLOC_ARRAY(sizes, size_count);
	if (sizes == NULL)
		return -DER_NOMEM;

	for (i = 0; i < orw->orw_iods.ca_count; i++)
		sizes[i] = iods[i].iod_size;

	orwo->orw_sizes.ca_arrays = sizes;
	return 0;
}

/**
 * Pack nrs in sgls inside the reply, so the client can update
 * sgls before it returns to application. Note: this is only
 * needed for bulk transfer, for inline transfer, it will pack
 * the complete sgls inside the req/reply, see obj_shard_rw().
 */
static int
ds_obj_update_nrs_in_reply(crt_rpc_t *rpc, daos_handle_t ioh,
			   daos_sg_list_t *sgls)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	uint32_t		*nrs;
	uint32_t		nrs_count = orw->orw_nr;
	int			i;

	if (nrs_count == 0)
		return 0;

	/* return num_out for sgl */
	orwo->orw_nrs.ca_count = nrs_count;
	D_ALLOC(orwo->orw_nrs.ca_arrays,
		nrs_count * sizeof(uint32_t));

	if (orwo->orw_nrs.ca_arrays == NULL)
		return -DER_NOMEM;

	nrs = orwo->orw_nrs.ca_arrays;
	for (i = 0; i < nrs_count; i++) {
		struct bio_sglist	*bsgl;
		daos_sg_list_t		*sgl;

		if (sgls != NULL) {
			sgl = &sgls[i];
			D_ASSERT(sgl != NULL);
			nrs[i] = sgl->sg_nr_out;
		} else {
			bsgl = vos_iod_sgl_at(ioh, i);
			D_ASSERT(bsgl != NULL);
			nrs[i] = bsgl->bs_nr_out;
		}
	}

	return 0;
}

/**
 * Lookup and return the container handle, if it is a rebuild handle, which
 * will never associate a particular container, then the contaier structure
 * will be returned to \a contp.
 */
static int
ds_check_container(uuid_t cont_hdl_uuid, uuid_t cont_uuid,
		   struct ds_cont_hdl **hdlp, struct ds_cont **contp)
{
	struct ds_cont_hdl	*cont_hdl;
	int			 rc;

	cont_hdl = ds_cont_hdl_lookup(cont_hdl_uuid);
	if (cont_hdl == NULL) {
		D_DEBUG(DB_TRACE, "can not find "DF_UUID" hdl\n",
			 DP_UUID(cont_hdl_uuid));
		D_GOTO(failed, rc = -DER_NO_HDL);
	}

	if (cont_hdl->sch_cont != NULL) { /* a regular container */
		*contp = cont_hdl->sch_cont;
		D_GOTO(out, rc = 0);
	}

	if (!is_rebuild_container(cont_hdl->sch_pool->spc_uuid,
				  cont_hdl_uuid)) {
		D_ERROR("Empty container "DF_UUID" (ref=%d) handle?\n",
			DP_UUID(cont_uuid), cont_hdl->sch_ref);
		D_GOTO(failed, rc = -DER_NO_HDL);
	}

	/* rebuild handle is a dummy and never attached by a real container */
	if (DAOS_FAIL_CHECK(DAOS_REBUILD_NO_HDL))
		D_GOTO(failed, rc = -DER_NO_HDL);

	if (DAOS_FAIL_CHECK(DAOS_REBUILD_STALE_POOL))
		D_GOTO(failed, rc = -DER_STALE);

	D_DEBUG(DB_TRACE, DF_UUID"/%p is rebuild cont hdl\n",
		DP_UUID(cont_hdl_uuid), cont_hdl);

	/* load or create VOS container on demand */
	rc = ds_cont_lookup(cont_hdl->sch_pool->spc_uuid, cont_uuid, contp);
	if (rc)
		D_GOTO(failed, rc);
out:
	*hdlp = cont_hdl;
failed:
	if (cont_hdl != NULL && rc != 0)
		ds_cont_hdl_put(cont_hdl);

	return rc;
}

void
ds_obj_rw_echo_handler(crt_rpc_t *rpc)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	struct obj_tls		*tls;
	daos_iod_t		*iod;
	daos_sg_list_t		*p_sgl;
	crt_bulk_op_t		bulk_op;
	bool			bulk_bind;
	int			i;
	int			rc = 0;

	D_DEBUG(DB_TRACE, "opc %d "DF_UOID" dkey %d %s tgt/xs %d/%d eph "
		DF_U64".\n", opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid),
		(int)orw->orw_dkey.iov_len, (char *)orw->orw_dkey.iov_buf,
		dss_get_module_info()->dmi_tgt_id,
		dss_get_module_info()->dmi_xs_id,
		orw->orw_epoch);

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
		rc = ds_obj_update_sizes_in_reply(rpc);
		if (rc)
			D_GOTO(out, rc);
	}

	/* Inline fetch/update */
	if (orw->orw_bulks.ca_arrays == NULL && orw->orw_bulks.ca_count == 0) {
		if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
			orwo->orw_sgls.ca_count = orw->orw_sgls.ca_count;
			orwo->orw_sgls.ca_arrays = orw->orw_sgls.ca_arrays;
		}
		D_GOTO(out, rc);
	}

	/* Only support 1 iod now */
	D_ASSERT(orw->orw_iods.ca_count == 1);
	iod = orw->orw_iods.ca_arrays;

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
	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
		rc = ds_obj_update_nrs_in_reply(rpc, DAOS_HDL_INVAL, p_sgl);
		if (rc != 0)
			D_GOTO(out, rc);
		bulk_op = CRT_BULK_PUT;
	} else {
		bulk_op = CRT_BULK_GET;
	}

	bulk_bind = orw->orw_flags & ORW_FLAG_BULK_BIND;
	rc = ds_bulk_transfer(rpc, bulk_op, bulk_bind, orw->orw_bulks.ca_arrays,
			      DAOS_HDL_INVAL, &p_sgl, orw->orw_nr);

out:
	orwo->orw_ret = rc;
	orwo->orw_map_version = orw->orw_map_ver;
}

static int
obj_update_postfw(crt_rpc_t *req, uint32_t shard, void *arg)
{
	struct obj_rw_in	*orw_parent = arg;
	struct obj_rw_out	*orw_out = crt_reply_get(req);
	int			 rc = 0;

	if (orw_parent->orw_map_ver < orw_out->orw_map_version) {
		D_DEBUG(DB_IO, DF_UOID": map_ver stale (%d < %d).\n",
			DP_UOID(orw_parent->orw_oid), orw_parent->orw_map_ver,
			orw_out->orw_map_version);
		rc = -DER_STALE;
	}

	return rc;
}

static int
obj_update_prefw(crt_rpc_t *req, uint32_t shard, void *arg)
{
	struct obj_rw_in	*orw_parent = arg;
	struct obj_rw_in	*orw = crt_req_get(req);

	*orw = *orw_parent;
	orw->orw_oid.id_shard = shard;
	uuid_copy(orw->orw_co_hdl, orw_parent->orw_co_hdl);
	uuid_copy(orw->orw_co_uuid, orw_parent->orw_co_uuid);
	orw->orw_shard_tgts.ca_count	= 0;
	orw->orw_shard_tgts.ca_arrays	= NULL;
	orw->orw_flags			= ORW_FLAG_BULK_BIND;

	return 0;
}

static int
ds_obj_rw_local_hdlr(crt_rpc_t *rpc, uint32_t tag, struct ds_cont_hdl *cont_hdl,
		     struct ds_cont *cont, daos_handle_t *ioh, bool update)
{
	struct obj_rw_in	*orw = crt_req_get(rpc);
	struct obj_rw_out	*orwo = crt_reply_get(rpc);
	struct bio_desc		*biod;
	crt_bulk_op_t		 bulk_op;
	bool			 rma;
	bool			 bulk_bind;
	int			 rc, err;

	if (daos_oc_echo_type(daos_obj_id2class(orw->orw_oid.id_pub)) ||
	    (daos_io_bypass & IOBP_TARGET)) {
		ds_obj_rw_echo_handler(rpc);
		return 0;
	}

	D_DEBUG(DB_TRACE, "opc %d "DF_UOID" dkey %d %s tag %d eph "DF_U64".\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid),
		(int)orw->orw_dkey.iov_len, (char *)orw->orw_dkey.iov_buf,
		tag, orw->orw_epoch);
	rma = (orw->orw_bulks.ca_arrays != NULL ||
	       orw->orw_bulks.ca_count != 0);

	/* Prepare IO descriptor */
	if (update) {
		bulk_op = CRT_BULK_GET;
		rc = vos_update_begin(cont->sc_hdl, orw->orw_oid,
				      orw->orw_epoch, &orw->orw_dkey,
				      orw->orw_nr, orw->orw_iods.ca_arrays,
				      ioh);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: %d\n",
				DP_UOID(orw->orw_oid), rc);
			goto out;
		}
	} else {
		bool size_fetch = (!rma && orw->orw_sgls.ca_arrays == NULL);

		bulk_op = CRT_BULK_PUT;
		rc = vos_fetch_begin(cont->sc_hdl, orw->orw_oid, orw->orw_epoch,
				     &orw->orw_dkey, orw->orw_nr,
				     orw->orw_iods.ca_arrays, size_fetch, ioh);
		if (rc) {
			D_ERROR(DF_UOID" Fetch begin failed: %d\n",
				DP_UOID(orw->orw_oid), rc);
			goto out;
		}

		rc = ds_obj_update_sizes_in_reply(rpc);
		if (rc != 0)
			goto out;

		if (rma) {
			orwo->orw_sgls.ca_count = 0;
			orwo->orw_sgls.ca_arrays = NULL;

			rc = ds_obj_update_nrs_in_reply(rpc, *ioh, NULL);
			if (rc != 0)
				goto out;
		} else {
			orwo->orw_sgls.ca_count = orw->orw_sgls.ca_count;
			orwo->orw_sgls.ca_arrays = orw->orw_sgls.ca_arrays;
		}
	}

	biod = vos_ioh2desc(*ioh);
	rc = bio_iod_prep(biod);
	if (rc) {
		D_ERROR(DF_UOID" bio_iod_prep failed: %d.\n",
			DP_UOID(orw->orw_oid), rc);
		goto out;
	}

	if (rma) {
		bulk_bind = orw->orw_flags & ORW_FLAG_BULK_BIND;
		rc = ds_bulk_transfer(rpc, bulk_op, bulk_bind,
			orw->orw_bulks.ca_arrays, *ioh, NULL, orw->orw_nr);
	} else if (orw->orw_sgls.ca_arrays != NULL) {
		rc = bio_iod_copy(biod, orw->orw_sgls.ca_arrays, orw->orw_nr);
	}

	if (rc == -DER_OVERFLOW) {
		rc = -DER_REC2BIG;
		D_ERROR(DF_UOID" ds_bulk_transfer/bio_iod_copy failed, rc %d",
			DP_UOID(orw->orw_oid), rc);
	}

	err = bio_iod_post(biod);
	rc = rc ? : err;
out:
	return rc;
}

void
ds_obj_rw_handler(crt_rpc_t *rpc)
{
	struct obj_req_disp_arg		*obj_arg = NULL;
	struct obj_rw_in		*orw = crt_req_get(rpc);
	struct obj_rw_out		*orwo = crt_reply_get(rpc);
	struct ds_cont_hdl		*cont_hdl = NULL;
	struct ds_cont			*cont = NULL;
	daos_handle_t			 ioh = DAOS_HDL_INVAL;
	uint32_t			 map_ver = 0;
	int				 tag;
	bool				 update;
	bool				 dispatch;
	int				 dispatch_rc = 0;
	int				 rc;

	D_ASSERT(orw != NULL);
	D_ASSERT(orwo != NULL);
	update = (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE);
	dispatch = update && orw->orw_shard_tgts.ca_arrays != NULL;
	tag = dss_get_module_info()->dmi_tgt_id;

	D_DEBUG(DB_TRACE, "rpc %p opc %d "DF_UOID" dkey %d %s tag/xs %d/%d eph "
		DF_U64".\n", rpc, opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid),
		(int)orw->orw_dkey.iov_len, (char *)orw->orw_dkey.iov_buf,
		tag, dss_get_module_info()->dmi_xs_id, orw->orw_epoch);
	rc = ds_check_container(orw->orw_co_hdl, orw->orw_co_uuid,
				&cont_hdl, &cont);
	if (rc)
		goto out;

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE &&
	    !(cont_hdl->sch_capas & DAOS_COO_RW)) {
		D_ERROR("cont "DF_UUID" sch_capas "DF_U64", "
			"NO_PERM to update.\n",
			DP_UUID(orw->orw_co_uuid), cont_hdl->sch_capas);
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	D_ASSERT(cont_hdl->sch_pool != NULL);
	map_ver = cont_hdl->sch_pool->spc_map_version;
	if (orw->orw_map_ver < map_ver) {
		D_DEBUG(DB_IO, "stale version req %d map_version %d\n",
			orw->orw_map_ver, map_ver);
		if (update && dispatch)
			D_GOTO(out, rc = -DER_STALE);
	}

	/* dispatch to other tgts when needed */
	if (dispatch) {
		rc = ds_obj_req_disp_prepare(rpc->cr_opc,
			orw->orw_shard_tgts.ca_arrays,
			orw->orw_shard_tgts.ca_count,
			obj_update_prefw, orw, obj_update_postfw, orw,
			&obj_arg);
		if (rc != 0) {
			D_ERROR(DF_UOID": ds_obj_req_disp_prepare failed %d.\n",
				DP_UOID(orw->orw_oid), rc);
			D_GOTO(out, rc);
		}
		D_ASSERT(obj_arg != NULL);

		rc = dss_ult_create(ds_obj_req_dispatch, obj_arg,
				    DSS_ULT_IOFW, tag, 0, NULL);
		if (rc != 0) {
			D_ERROR(DF_UOID": ds_obj_update_dispatch failed %d.\n",
				DP_UOID(orw->orw_oid), rc);
			ds_obj_req_disp_arg_free(obj_arg);
			D_GOTO(out, rc);
		}
	}

	/* local RPC handler */
	rc = ds_obj_rw_local_hdlr(rpc, tag, cont_hdl, cont, &ioh, update);
	if (rc != 0)
		D_ERROR(DF_UOID": ds_obj_rw_local_hdlr failed %d.\n",
			DP_UOID(orw->orw_oid), rc);

	/* wait dispatched IO's completion when needed */
	if (dispatch)
		dispatch_rc = ds_obj_req_disp_wait(obj_arg);

	if (rc == 0)
		rc = dispatch_rc;

out:
	ds_obj_rw_complete(rpc, cont_hdl, ioh, rc, map_ver);
	if (cont_hdl) {
		if (!cont_hdl->sch_cont)
			ds_cont_put(cont); /* -1 for rebuild container */
		ds_cont_hdl_put(cont_hdl);
	}
}

static void
ds_eu_complete(crt_rpc_t *rpc, int status, int map_version)
{
	struct obj_key_enum_out *oeo;
	struct obj_key_enum_in *oei;
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

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
ds_iter_vos(crt_rpc_t *rpc, struct vos_iter_anchors *anchors,
	    struct dss_enum_arg *enum_arg, uint32_t *map_version)
{
	vos_iter_param_t	param = { 0 };
	struct obj_key_enum_in	*oei = crt_req_get(rpc);
	int			opc = opc_get(rpc->cr_opc);
	struct ds_cont_hdl	*cont_hdl;
	struct ds_cont		*cont;
	int			type;
	int			rc;
	bool			recursive = false;

	rc = ds_check_container(oei->oei_co_hdl, oei->oei_co_uuid,
				&cont_hdl, &cont);
	if (rc)
		D_GOTO(out, rc);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	*map_version = cont_hdl->sch_pool->spc_map_version;
	if (oei->oei_map_ver < *map_version)
		D_DEBUG(DB_IO, "stale version req %d map_version %d\n",
			oei->oei_map_ver, *map_version);

	/* prepare enumeration parameters */
	param.ip_hdl = cont->sc_hdl;
	param.ip_oid = oei->oei_oid;
	if (oei->oei_dkey.iov_len > 0)
		param.ip_dkey = oei->oei_dkey;
	if (oei->oei_akey.iov_len > 0)
		param.ip_akey = oei->oei_akey;
	param.ip_epr.epr_lo = oei->oei_epoch;
	param.ip_epr.epr_hi = oei->oei_epoch;
	param.ip_epc_expr = VOS_IT_EPC_LE;

	if (opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		if (oei->oei_dkey.iov_len == 0 ||
		    oei->oei_akey.iov_len == 0)
			D_GOTO(out_cont_hdl, rc = -DER_PROTO);

		if (oei->oei_rec_type == DAOS_IOD_ARRAY) {
			type = VOS_ITER_RECX;
			/* To capture everything visible, we must search from
			 * 0 to our epoch
			 */
			param.ip_epr.epr_lo = 0;
		} else {
			type = VOS_ITER_SINGLE;
		}

		param.ip_epc_expr = VOS_IT_EPC_RE;
		/** Only show visible records and skip punches */
		param.ip_flags = VOS_IT_RECX_VISIBLE | VOS_IT_RECX_SKIP_HOLES;
		enum_arg->fill_recxs = true;
	} else if (opc == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		type = VOS_ITER_DKEY;
	} else if (opc == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		type = VOS_ITER_AKEY;
	} else {
		/* object iteration for rebuild */
		D_ASSERT(opc == DAOS_OBJ_RPC_ENUMERATE);
		type = VOS_ITER_DKEY;
		param.ip_epr.epr_lo = 0;
		param.ip_epc_expr = VOS_IT_EPC_RE;
		recursive = true;
		enum_arg->chk_key2big = true;
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

	rc = dss_enum_pack(&param, type, recursive, anchors, enum_arg);

	if (type == VOS_ITER_SINGLE)
		anchors->ia_ev = anchors->ia_sv;

	D_DEBUG(DB_IO, ""DF_UOID" iterate type %d tag %d rc %d\n",
		DP_UOID(oei->oei_oid), type, dss_get_module_info()->dmi_tgt_id,
		rc);
out_cont_hdl:

	if (!cont_hdl->sch_cont)
		ds_cont_put(cont); /* -1 for rebuild container */
	ds_cont_hdl_put(cont_hdl);
out:
	return rc;
}

static int
obj_enum_reply_bulk(crt_rpc_t *rpc)
{
	daos_sg_list_t	*sgls[2] = { 0 };
	daos_sg_list_t  tmp_sgl;
	crt_bulk_t	bulks[2] = { 0 };
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int		idx = 0;
	d_iov_t		tmp_iov;
	int		rc;

	oei = crt_req_get(rpc);
	oeo = crt_reply_get(rpc);
	if (oei->oei_kds_bulk) {
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

	rc = ds_bulk_transfer(rpc, CRT_BULK_PUT, false, bulks, DAOS_HDL_INVAL,
			      sgls, idx);

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

void
ds_obj_enum_handler(crt_rpc_t *rpc)
{
	struct dss_enum_arg	enum_arg = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int			opc = opc_get(rpc->cr_opc);
	unsigned int		map_version = 0;
	int			rc = 0;

	oei = crt_req_get(rpc);
	D_ASSERT(oei != NULL);
	oeo = crt_reply_get(rpc);
	D_ASSERT(oeo != NULL);
	/* prepare buffer for enumerate */

	anchors.ia_dkey = oei->oei_dkey_anchor;
	anchors.ia_akey = oei->oei_akey_anchor;
	anchors.ia_ev = oei->oei_anchor;

	/* TODO: Transfer the inline_thres from enumerate RPC */
	enum_arg.inline_thres = 32;

	if (opc == DAOS_OBJ_RECX_RPC_ENUMERATE ||
	    opc == DAOS_OBJ_RPC_ENUMERATE) {
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
		rc = ds_sgls_prep(&oeo->oeo_sgl, &oei->oei_sgl, 1);
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

	/* keep trying until the key_buffer is fully filled or
	 * reaching the end of the stream
	 */
	rc = ds_iter_vos(rpc, &anchors, &enum_arg, &map_version);
	if (rc == 1) {
		/* If the buffer is full, exit and
		 * reset failure.
		 */
		rc = 0;
	}

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
	if (rc == -DER_KEY2BIG)
		oeo->oeo_size = enum_arg.kds[0].kd_key_len;
	ds_eu_complete(rpc, rc, map_version);
}

static int
obj_punch_postfw(crt_rpc_t *req, uint32_t shard, void *arg)
{
	struct obj_punch_in	*opi_parent = arg;
	struct obj_punch_out	*opo = crt_reply_get(req);
	int			 rc = 0;

	if (opi_parent->opi_map_ver < opo->opo_map_version) {
		D_DEBUG(DB_IO, DF_UOID": map_ver stale (%d < %d).\n",
			DP_UOID(opi_parent->opi_oid), opi_parent->opi_map_ver,
			opo->opo_map_version);
		rc = -DER_STALE;
	}

	return rc;
}

static int
obj_punch_prefw(crt_rpc_t *req, uint32_t shard, void *arg)
{
	struct obj_punch_in	*opi_parent = arg;
	struct obj_punch_in	*opi = crt_req_get(req);

	*opi = *opi_parent;
	opi->opi_oid.id_shard = shard;
	uuid_copy(opi->opi_co_hdl, opi_parent->opi_co_hdl);
	uuid_copy(opi->opi_co_uuid, opi_parent->opi_co_uuid);
	opi->opi_shard_tgts.ca_count = 0;
	opi->opi_shard_tgts.ca_arrays = NULL;

	return 0;
}

static void
obj_punch_complete(crt_rpc_t *rpc, int status, uint32_t map_version)
{
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, map_version);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);
}

static int
ds_obj_punch_local_hdlr(struct obj_punch_in *opi, crt_opcode_t opc,
			struct ds_cont_hdl *cont_hdl, struct ds_cont *cont)
{
	int	i;
	int	rc = 0;

	switch (opc) {
	default:
		D_ERROR("opc %#x not supported\n", opc);
		D_GOTO(out, rc = -DER_NOSYS);

	case DAOS_OBJ_RPC_PUNCH:
		rc = vos_obj_punch(cont->sc_hdl, opi->opi_oid,
				   opi->opi_epoch, opi->opi_map_ver, 0,
				   NULL, 0, NULL);
		break;
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		for (i = 0; i < opi->opi_dkeys.ca_count; i++) {
			daos_key_t *dkey;

			dkey = &((daos_key_t *)opi->opi_dkeys.ca_arrays)[i];
			rc = vos_obj_punch(cont->sc_hdl,
					   opi->opi_oid,
					   opi->opi_epoch,
					   opi->opi_map_ver, 0, dkey,
					   opi->opi_akeys.ca_count,
					   opi->opi_akeys.ca_arrays);
			if (rc)
				D_GOTO(out, rc);
		}
		break;
	}

out:
	return rc;
}

void
ds_obj_punch_handler(crt_rpc_t *rpc)
{
	struct obj_req_disp_arg		*obj_arg = NULL;
	struct ds_cont_hdl		*cont_hdl = NULL;
	struct ds_cont			*cont = NULL;
	struct obj_punch_in		*opi;
	uint32_t			 map_version = 0;
	int				 tag;
	bool				 dispatch;
	int				 dispatch_rc = 0;
	int				 rc;

	opi = crt_req_get(rpc);
	D_ASSERT(opi != NULL);
	dispatch = opi->opi_shard_tgts.ca_arrays != NULL;

	tag = dss_get_module_info()->dmi_tgt_id;
	rc = ds_check_container(opi->opi_co_hdl, opi->opi_co_uuid,
				&cont_hdl, &cont);
	if (rc)
		D_GOTO(out, rc);

	if (!(cont_hdl->sch_capas & DAOS_COO_RW))
		D_GOTO(out, rc = -DER_NO_PERM);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	map_version = cont_hdl->sch_pool->spc_map_version;

	if (opi->opi_map_ver < map_version) {
		D_DEBUG(DB_IO, "stale version req %d map_version %d\n",
			 opi->opi_map_ver, map_version);
		if (dispatch)
			D_GOTO(out, rc = -DER_STALE);
	}

	if (dispatch) {
		rc = ds_obj_req_disp_prepare(rpc->cr_opc,
			opi->opi_shard_tgts.ca_arrays,
			opi->opi_shard_tgts.ca_count,
			obj_punch_prefw, opi, obj_punch_postfw, opi,
			&obj_arg);
		if (rc != 0) {
			D_ERROR(DF_UOID": ds_obj_req_disp_prepare failed %d.\n",
				DP_UOID(opi->opi_oid), rc);
			D_GOTO(out, rc);
		}
		D_ASSERT(obj_arg != NULL);

		rc = dss_ult_create(ds_obj_req_dispatch, obj_arg,
				    DSS_ULT_IOFW, tag, 0, NULL);
		if (rc != 0) {
			D_ERROR(DF_UOID": ds_obj_req_dispatch failed %d.\n",
				DP_UOID(opi->opi_oid), rc);
			ds_obj_req_disp_arg_free(obj_arg);
			D_GOTO(out, rc);
		}
	}

	rc = ds_obj_punch_local_hdlr(opi, opc_get(rpc->cr_opc), cont_hdl, cont);
	if (rc != 0)
		D_ERROR(DF_UOID": ds_obj_punch_local_hdlr failed %d.\n",
			DP_UOID(opi->opi_oid), rc);

	if (dispatch)
		dispatch_rc = ds_obj_req_disp_wait(obj_arg);

	if (rc == 0)
		rc = dispatch_rc;

out:
	obj_punch_complete(rpc, rc, map_version);
	if (cont_hdl) {
		if (!cont_hdl->sch_cont)
			ds_cont_put(cont); /* -1 for rebuild container */
		ds_cont_hdl_put(cont_hdl);
	}
}

void
ds_obj_query_key_handler(crt_rpc_t *rpc)
{
	struct obj_query_key_in		*okqi;
	struct obj_query_key_out	*okqo;
	struct ds_cont_hdl		*cont_hdl = NULL;
	struct ds_cont			*cont = NULL;
	daos_key_t			*dkey;
	daos_key_t			*akey;
	uint32_t			map_version = 0;
	int				rc;

	okqi = crt_req_get(rpc);
	D_ASSERT(okqi != NULL);
	okqo = crt_reply_get(rpc);
	D_ASSERT(okqo != NULL);

	D_DEBUG(DB_IO, "ds_obj_query_key_handler: flags = %d\n",
		okqi->okqi_flags);

	rc = ds_check_container(okqi->okqi_co_hdl, okqi->okqi_co_uuid,
				&cont_hdl, &cont);
	if (rc)
		D_GOTO(out, rc);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	map_version = cont_hdl->sch_pool->spc_map_version;

	dkey = &okqi->okqi_dkey;
	akey = &okqi->okqi_akey;
	d_iov_set(&okqo->okqo_akey, NULL, 0);
	d_iov_set(&okqo->okqo_dkey, NULL, 0);
	if (okqi->okqi_flags & DAOS_GET_DKEY)
		dkey = &okqo->okqo_dkey;
	if (okqi->okqi_flags & DAOS_GET_AKEY)
		akey = &okqo->okqo_akey;

	rc = vos_obj_query_key(cont->sc_hdl, okqi->okqi_oid, okqi->okqi_flags,
			       okqi->okqi_epoch, dkey, akey, &okqo->okqo_recx);
out:
	if (cont_hdl) {
		if (!cont_hdl->sch_cont)
			ds_cont_put(cont); /* -1 for rebuild container */
		ds_cont_hdl_put(cont_hdl);
	}

	obj_reply_set_status(rpc, rc);
	obj_reply_map_version_set(rpc, map_version);

	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);
}

/**
 * Choose abt pools for object RPC. Because dkey enumeration might create ULT
 * on other xstream pools, so we have to put it to the shared pool. For other
 * RPC, it can be put to the private pool. XXX we might just instruct the
 * server create task for inline I/O.
 */
ABT_pool
ds_obj_abt_pool_choose_cb(crt_rpc_t *rpc, ABT_pool *pools)
{
	struct obj_rw_in	*orw;
	struct obj_punch_in	*opi;
	ABT_pool		 pool;

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_ENUMERATE:
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_PUNCH:
		pool = pools[DSS_POOL_SHARE];
		break;
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		/* if the update/punch need to dispatch to other tgts, schedules
		 * it in SHARE pool as need other ES to dispatch it.
		 */
		opi = crt_req_get(rpc);
		pool = (opi->opi_shard_tgts.ca_arrays != NULL) ?
		       pools[DSS_POOL_SHARE] : pools[DSS_POOL_PRIV];
		break;
	case DAOS_OBJ_RPC_UPDATE:
		orw = crt_req_get(rpc);
		pool = (orw->orw_shard_tgts.ca_arrays != NULL) ?
		       pools[DSS_POOL_SHARE] : pools[DSS_POOL_PRIV];
		break;
	default:
		pool = pools[DSS_POOL_PRIV];
		break;
	};

	return pool;
}

struct obj_req_disp_arg {
	ds_iofw_cb_t			 prefw_cb;
	void				*prefw_arg;
	ds_iofw_cb_t			 postfw_cb;
	void				*postfw_arg;
	ABT_future			 fw_future;
	crt_opcode_t			 fw_opc;
	uint32_t			 fw_cnt;
	int				 fw_result;
};

struct shard_req_fw_arg {
	struct daos_obj_shard_tgt	*fw_shard_tgt;
	struct obj_req_disp_arg		*fw_obj_arg;
	int				 fw_shard_rc;
};

static void
obj_req_dispatch_cb(void **arg)
{
	struct shard_req_fw_arg		*shard_arg;
	struct obj_req_disp_arg		*obj_arg;
	uint32_t			 i, fw_cnt;

	shard_arg = arg[0];
	obj_arg = shard_arg->fw_obj_arg;
	fw_cnt = obj_arg->fw_cnt;
	D_ASSERT(fw_cnt >= 1);
	for (i = 0; i < fw_cnt; i++) {
		shard_arg = arg[i];
		D_ASSERT(shard_arg->fw_obj_arg == obj_arg);
		if (obj_arg->fw_result == 0)
			obj_arg->fw_result = shard_arg->fw_shard_rc;
	}
}

static void
shard_req_fw_cb(const struct crt_cb_info *cb_info)
{
	crt_rpc_t		*req = cb_info->cci_rpc;
	struct shard_req_fw_arg	*shard_arg = cb_info->cci_arg;
	int			 rc = cb_info->cci_rc;

	if (rc == 0) {
		rc = shard_arg->fw_obj_arg->postfw_cb(
				req, shard_arg->fw_shard_tgt->st_shard,
				shard_arg->fw_obj_arg->postfw_arg);
		if (rc != 0)
			D_DEBUG(DB_TRACE, "opc:%#x, postfw_cb failed, rc %d.\n",
				req->cr_opc, rc);
	}

	shard_arg->fw_shard_rc = rc;
	rc = ABT_future_set(shard_arg->fw_obj_arg->fw_future, shard_arg);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_set failed %d.\n", rc);
	D_DEBUG(DB_TRACE, "forward req got reply from rank %d tag %d, rc %d.\n",
		shard_arg->fw_shard_tgt->st_rank,
		shard_arg->fw_shard_tgt->st_tgt_idx, rc);
}

static int
shard_req_forward(struct shard_req_fw_arg *fw_arg)
{
	struct daos_obj_shard_tgt	*shard_tgt = fw_arg->fw_shard_tgt;
	crt_rpc_t			*req;
	ABT_future			 future = fw_arg->fw_obj_arg->fw_future;
	crt_opcode_t			 opc = fw_arg->fw_obj_arg->fw_opc;
	crt_endpoint_t			 tgt_ep;
	int				 rc = 0;

	if (opc_get(opc) == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_OBJ_TGT_IDX_CHANGE)) {
		/* to trigger retry on all other shards */
		if (shard_tgt->st_shard != daos_fail_value_get()) {
			D_DEBUG(DB_TRACE, "complete shard %d update as "
				"-DER_TIMEDOUT.\n", shard_tgt->st_shard);
			rc = -DER_TIMEDOUT;
		}
	}

	if (rc != 0 || shard_tgt->st_rank == OBJ_TGTS_IGNORE) {
		D_DEBUG(DB_TRACE, "opc:%#x, ignore the forward tgt rank %d.\n",
			opc, shard_tgt->st_rank);
		fw_arg->fw_shard_rc = rc;
		rc = ABT_future_set(future, fw_arg);
		rc = dss_abterr2der(rc);
		return rc;
	}

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = shard_tgt->st_rank;
	tgt_ep.ep_tag = daos_rpc_tag(DAOS_REQ_IO, shard_tgt->st_tgt_idx);
	D_DEBUG(DB_TRACE, "opc:%#x, forwarding to rank:%d tag:%d.\n",
		opc, tgt_ep.ep_rank, tgt_ep.ep_tag);
	rc = crt_req_create(dss_get_module_info()->dmi_ctx, &tgt_ep, opc, &req);
	if (rc != 0) {
		D_ERROR("opc:%#x, crt_req_create failed, rc %d.\n", opc, rc);
		D_GOTO(out, rc);
	}

	rc = fw_arg->fw_obj_arg->prefw_cb(req, shard_tgt->st_shard,
					  fw_arg->fw_obj_arg->prefw_arg);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, "opc:%#x, prefw_cb failed, rc %d.\n",
			opc, rc);
		crt_req_decref(req);
		D_GOTO(out, rc);
	}

	rc = crt_req_send(req, shard_req_fw_cb, fw_arg);
	if (rc != 0) {
		D_ERROR("opc:%#x, crt_req_send failed, rc %d.\n", opc, rc);
		crt_req_decref(req);
		D_GOTO(out, rc);
	}

out:
	if (rc) {
		fw_arg->fw_shard_rc = rc;
		rc = ABT_future_set(future, fw_arg);
		rc = dss_abterr2der(rc);
	}
	return rc;
}

int
ds_obj_req_disp_prepare(crt_opcode_t opc,
			struct daos_obj_shard_tgt *fw_shard_tgts,
			uint32_t fw_cnt, ds_iofw_cb_t prefw_cb,
			void *prefw_arg, ds_iofw_cb_t postfw_cb,
			void *postfw_arg, struct obj_req_disp_arg **arg)
{
	struct obj_req_disp_arg		*obj_arg;
	struct shard_req_fw_arg		*shard_arg;
	ABT_future			 future;
	int				 i, rc;

	D_ASSERT(fw_cnt >= 1);
	D_ALLOC(obj_arg, sizeof(struct obj_req_disp_arg) +
			 fw_cnt * sizeof(struct shard_req_fw_arg));
	if (obj_arg == NULL)
		return -DER_NOMEM;

	rc = ABT_future_create(fw_cnt, obj_req_dispatch_cb, &future);
	if (rc != ABT_SUCCESS) {
		D_ERROR("ABT_future_create failed %d.\n", rc);
		D_FREE(obj_arg);
		return dss_abterr2der(rc);
	}

	obj_arg->prefw_cb	= prefw_cb;
	obj_arg->prefw_arg	= prefw_arg;
	obj_arg->postfw_cb	= postfw_cb;
	obj_arg->postfw_arg	= postfw_arg;
	obj_arg->fw_future	= future;
	obj_arg->fw_opc		= opc;
	obj_arg->fw_cnt		= fw_cnt;

	shard_arg = (struct shard_req_fw_arg *)(obj_arg + 1);
	for (i = 0; i < fw_cnt; i++, shard_arg++) {
		shard_arg->fw_shard_tgt	= fw_shard_tgts + i;
		shard_arg->fw_obj_arg	= obj_arg;
	}

	*arg = obj_arg;
	return 0;
}

void
ds_obj_req_dispatch(void *arg)
{
	struct obj_req_disp_arg		*obj_arg = arg;
	ABT_future			 future = obj_arg->fw_future;
	struct shard_req_fw_arg		*shard_arg;
	uint32_t			 i, fw_cnt;
	int				 rc = 0;

	fw_cnt = obj_arg->fw_cnt;
	D_ASSERT(fw_cnt >= 1);
	D_ASSERT(future != ABT_FUTURE_NULL);
	shard_arg = (struct shard_req_fw_arg *)(obj_arg + 1);
	for (i = 0; i < fw_cnt; i++, shard_arg++) {
		rc = shard_req_forward(shard_arg);
		if (rc != 0)
			break;
	}

	if (rc != 0) {
		D_ASSERT(i < fw_cnt);
		for (i++; i < fw_cnt; i++) {
			shard_arg++;
			shard_arg->fw_shard_rc = rc;
			rc = ABT_future_set(future, shard_arg);
			D_ASSERTF(rc == ABT_SUCCESS,
				  "ABT_future_set failed %d.\n", rc);
		}
	}
}

void
ds_obj_req_disp_arg_free(struct obj_req_disp_arg *obj_arg)
{
	ABT_future_free(&obj_arg->fw_future);
	D_FREE(obj_arg);
}

int
ds_obj_req_disp_wait(struct obj_req_disp_arg *obj_arg)
{
	ABT_future	future = obj_arg->fw_future;
	int		rc;

	D_ASSERT(future != ABT_FUTURE_NULL);
	rc = ABT_future_wait(future);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_future_wait failed %d.\n", rc);
	rc = obj_arg->fw_result;
	ds_obj_req_disp_arg_free(obj_arg);

	return rc;
};
