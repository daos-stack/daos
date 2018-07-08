/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
#include <daos_srv/eio.h>
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

		rc = update ? vos_update_end(ioh, cont_hdl->sch_uuid,
					     map_version, &orwi->orw_dkey,
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
	int				rc = 0;

	rc = cb_info->bci_rc;
	if (rc != 0)
		D_ERROR("bulk transfer failed: rc = %d\n", rc);

	bulk_desc = cb_info->bci_bulk_desc;
	local_bulk_hdl = bulk_desc->bd_local_hdl;
	rpc = bulk_desc->bd_rpc;
	arg = (struct ds_bulk_async_args *)cb_info->bci_arg;
	/**
	 * Note: only one thread will access arg.result, so
	 * it should be safe here.
	 **/
	if (arg->result == 0)
		arg->result = rc;

	D_ASSERT(arg->bulks_inflight > 0);
	arg->bulks_inflight--;
	if (arg->bulks_inflight == 0)
		ABT_eventual_set(arg->eventual, &rc, sizeof(rc));

	crt_bulk_free(local_bulk_hdl);
	crt_req_decref(rpc);
	return rc;
}

/**
 * Simulate bulk transfer by memcpy, all data are actually dropped.
 */
static int
bulk_bypass(daos_sg_list_t *sgl, crt_bulk_op_t bulk_op)
{
	static const int  dummy_buf_len = 4096;
	static char	 *dummy_buf;
	int		  i;

	if (!dummy_buf) {
		dummy_buf = malloc(dummy_buf_len);
		if (!dummy_buf)
			return 0; /* ignore error */
	}

	for (i = 0; i < sgl->sg_nr_out; i++) {
		char	*buf;
		int	 total;
		int	 nob;

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
	return 0;
}

static int
ds_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t bulk_op,
		 crt_bulk_t *remote_bulks, daos_handle_t ioh,
		 daos_sg_list_t **sgls, int sgl_nr)
{
	crt_bulk_opid_t		bulk_opid;
	crt_bulk_perm_t		bulk_perm;
	struct ds_bulk_async_args arg = { 0 };
	int			i;
	int			rc;
	int			*status;

	bulk_perm = bulk_op == CRT_BULK_PUT ? CRT_BULK_RO : CRT_BULK_RW;
	rc = ABT_eventual_create(sizeof(*status), &arg.eventual);
	if (rc != 0)
		return dss_abterr2der(rc);

	D_DEBUG(DB_IO, "sgl nr is %d\n", sgl_nr);
	for (i = 0; i < sgl_nr; i++) {
		daos_sg_list_t		*sgl;
		struct crt_bulk_desc	 bulk_desc;
		crt_bulk_t		 local_bulk_hdl;
		int			 ret = 0;
		daos_size_t		 offset = 0;
		unsigned int		 idx = 0;

		if (remote_bulks[i] == NULL)
			continue;

		if (sgls != NULL) {
			sgl = sgls[i];
		} else {
			D_ASSERT(!daos_handle_is_inval(ioh));
			ret = vos_obj_zc_sgl_at(ioh, i, &sgl);
			if (ret) {
				if (rc == 0)
					rc = ret;
				continue;
			}
			D_ASSERT(sgl != NULL);
		}

		if (srv_bypass_bulk) {
			/* this mode will bypass network bulk transfer and
			 * only copy data from/to dummy buffer. This is for
			 * performance evaluation on low bandwidth network.
			 */
			ret = bulk_bypass(sgl, bulk_op);
			if (rc == 0)
				rc = ret;
			continue;
		}

		/**
		 * Let's walk through the sgl to check if the iov is empty,
		 * which is usually gotten from punched/empty records (see
		 * vos_recx_fetch()), and skip these empty iov during bulk
		 * transfer to avoid touching the input buffer.
		 *
		 */
		while (idx < sgl->sg_nr_out) {
			daos_sg_list_t	sgl_sent;
			daos_size_t	length = 0;
			unsigned int	start;

			/**
			 * Skip the punched/empty record, let's also skip the
			 * them record in the input buffer instead of memset
			 * it to 0.
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

			ret = crt_bulk_create(rpc->cr_ctx,
					      daos2crt_sg(&sgl_sent),
					      bulk_perm, &local_bulk_hdl);
			if (ret != 0) {
				D_ERROR("crt_bulk_create %d failed;rc: %d\n",
					 i, ret);
				if (rc == 0)
					rc = ret;
				offset += length;
				continue;
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
			ret = crt_bulk_transfer(&bulk_desc, bulk_complete_cb,
						&arg, &bulk_opid);
			if (ret < 0) {
				D_ERROR("crt_bulk_transfer failed, rc: %d.\n",
					ret);
				arg.bulks_inflight--;
				crt_bulk_free(local_bulk_hdl);
				crt_req_decref(rpc);
				if (rc == 0)
					rc = ret;
			}
			offset += length;
		}
	}

	if (arg.bulks_inflight == 0)
		ABT_eventual_set(arg.eventual, &rc, sizeof(rc));

	rc = ABT_eventual_wait(arg.eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));

	rc = *status;
	/* arg.result might not be set through bulk_complete_cb */
	if (rc == 0)
		rc = arg.result;
out_eventual:
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
		D_ALLOC(dst_sgls[i].sg_iovs,
			sgls[i].sg_nr * sizeof(*sgls[i].sg_iovs));
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
	D_ALLOC(sizes, size_count * sizeof(*sizes));
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
	int			rc = 0;

	if (nrs_count == 0)
		return 0;

	/* return num_out for sgl */
	orwo->orw_nrs.ca_count = nrs_count;
	D_ALLOC(orwo->orw_nrs.ca_arrays,
		nrs_count * sizeof(uint32_t));

	if (orwo->orw_nrs.ca_arrays == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	nrs = orwo->orw_nrs.ca_arrays;
	for (i = 0; i < nrs_count; i++) {
		daos_sg_list_t	*sgl;

		if (sgls != NULL) {
			sgl = &sgls[i];
		} else {
			rc = vos_obj_zc_sgl_at(ioh, i, &sgl);
			if (rc)
				D_GOTO(out, rc);
		}

		D_ASSERT(sgl != NULL);
		nrs[i] = sgl->sg_nr_out;
	}
out:
	return rc;
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
	int			i;
	int			rc = 0;

	D_DEBUG(DB_TRACE, "opc %d "DF_UOID" tag %d\n", opc_get(rpc->cr_opc),
		DP_UOID(orw->orw_oid), dss_get_module_info()->dmi_tid);

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
		rc = ds_obj_update_sizes_in_reply(rpc);
		if (rc)
			D_GOTO(out, rc);
	}

	/* Inline fetch/update */
	if (orw->orw_bulks.ca_arrays == NULL && orw->orw_bulks.ca_count == 0) {
		if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_FETCH)
			orwo->orw_sgls = orw->orw_sgls;
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

	rc = ds_bulk_transfer(rpc, bulk_op, orw->orw_bulks.ca_arrays,
			      DAOS_HDL_INVAL, &p_sgl, orw->orw_nr);

out:
	orwo->orw_ret = rc;
	orwo->orw_map_version = orw->orw_map_ver;
	rc = crt_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);
}

void
ds_obj_rw_handler(crt_rpc_t *rpc)
{
	struct ds_cont_hdl	*cont_hdl = NULL;
	struct ds_cont		*cont = NULL;
	struct obj_rw_in	*orw;
	daos_handle_t		 ioh = DAOS_HDL_INVAL;
	crt_bulk_op_t		 bulk_op;
	uint32_t		 map_ver = 0;
	bool			 rma, update;
	struct eio_desc		*eiod;
	int			 rc, err;

	orw = crt_req_get(rpc);
	D_ASSERT(orw != NULL);

	if (daos_obj_id2class(orw->orw_oid.id_pub) == DAOS_OC_ECHO_RW)
		return ds_obj_rw_echo_handler(rpc);

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
	}

	D_DEBUG(DB_TRACE, "opc %d "DF_UOID" dkey %.*s tag %d\n",
		opc_get(rpc->cr_opc), DP_UOID(orw->orw_oid),
		(int)orw->orw_dkey.iov_len, (char *)orw->orw_dkey.iov_buf,
		dss_get_module_info()->dmi_tid);

	rma = (orw->orw_bulks.ca_arrays != NULL ||
	       orw->orw_bulks.ca_count != 0);

	update = (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE);

	/* Prepare IO descriptor */
	if (update) {
		bulk_op = CRT_BULK_GET;
		rc = vos_update_begin(cont->sc_hdl, orw->orw_oid,
				      orw->orw_epoch, &orw->orw_dkey,
				      orw->orw_nr, orw->orw_iods.ca_arrays,
				      &ioh);
		if (rc) {
			D_ERROR(DF_UOID" Update begin failed: %d\n",
				DP_UOID(orw->orw_oid), rc);
			goto out;
		}
	} else {
		struct obj_rw_out *orwo = crt_reply_get(rpc);
		bool size_fetch = (!rma && orw->orw_sgls.ca_arrays == NULL);

		D_ASSERT(orwo != NULL);
		bulk_op = CRT_BULK_PUT;
		rc = vos_fetch_begin(cont->sc_hdl, orw->orw_oid, orw->orw_epoch,
				     &orw->orw_dkey, orw->orw_nr,
				     orw->orw_iods.ca_arrays, size_fetch, &ioh);
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

			rc = ds_obj_update_nrs_in_reply(rpc, ioh, NULL);
			if (rc != 0)
				goto out;
		} else {
			orwo->orw_sgls.ca_count = orw->orw_sgls.ca_count;
			orwo->orw_sgls.ca_arrays = orw->orw_sgls.ca_arrays;
		}
	}

	eiod = vos_ioh2desc(ioh);
	rc = eio_iod_prep(eiod);
	if (rc)
		goto out;

	if (rma)
		rc = ds_bulk_transfer(rpc, bulk_op, orw->orw_bulks.ca_arrays,
				      ioh, NULL, orw->orw_nr);
	else if (orw->orw_sgls.ca_arrays != NULL)
		rc = eio_iod_copy(eiod, orw->orw_sgls.ca_arrays, orw->orw_nr);

	err = eio_iod_post(eiod);
	if (!rc)
		rc = err;
out:
	ds_obj_rw_complete(rpc, cont_hdl, ioh, rc, map_ver);
	if (cont_hdl) {
		if (!cont_hdl->sch_cont)
			ds_cont_put(cont); /* -1 for rebuild container */
		ds_cont_hdl_put(cont_hdl);
	}
}

static void
ds_eu_complete(crt_rpc_t *rpc, int status, struct ds_iter_arg *arg)
{
	struct obj_key_enum_out *oeo;
	struct obj_key_enum_in *oei;
	int rc;

	obj_reply_set_status(rpc, status);
	obj_reply_map_version_set(rpc, arg->map_version);
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

#if 1 /* TODO: Move to dss. */
typedef int (*dss_vos_iterate_cb_t)(daos_handle_t ih, vos_iter_entry_t *entry,
				    vos_iter_type_t type,
				    vos_iter_param_t *param, void *arg);

/**
 * Iterate VOS entries (i.e., containers, objects, dkeys, etc.) and call \a
 * cb(\a arg) for each entry.
 *
 * If \a cb returns a nonzero (either > 0 or < 0) value that is not
 * -DER_NONEXIST, this function stops the iteration and returns that nonzero
 * value from \a cb. If \a cb returns -DER_NONEXIST, this function completes
 * the iteration and returns 0. If \a cb returns 0, the iteration continues.
 *
 * \param[in]		type	entry type
 * \param[in]		param	parameters for \a type
 * \param[in,out]	anchor	[in]: where to begin; [out]: where stopped
 * \param[in]		cb	callback called for each entry
 * \param[in]		arg	callback argument
 *
 * \retval		0	iteration complete
 * \retval		> 0	callback return value
 * \retval		-DER_*	error (but never -DER_NONEXIST)
 */
int
dss_vos_iterate(vos_iter_type_t type, vos_iter_param_t *param,
		daos_hash_out_t *anchor, dss_vos_iterate_cb_t cb, void *arg)
{
	daos_hash_out_t		*probe_hash = NULL;
	vos_iter_entry_t	key_ent;
	daos_handle_t		ih;
	int			rc;

	rc = vos_iter_prepare(type, param, &ih);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_hash_set_eof(anchor);
			rc = 0;
		} else {
			D_ERROR("failed to prepare iterator (type=%d): %d\n",
				type, rc);
		}
		D_GOTO(out, rc);
	}

	if (!daos_hash_is_zero(anchor))
		probe_hash = anchor;

	rc = vos_iter_probe(ih, probe_hash);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_AGAIN) {
			daos_hash_set_eof(anchor);
			rc = 0;
		} else {
			D_ERROR("failed to probe iterator (type=%d anchor=%p): "
				"%d\n", type, probe_hash, rc);
		}
		D_GOTO(out_iter_fini, rc);
	}

	while (1) {
		rc = vos_iter_fetch(ih, &key_ent, anchor);
		if (rc != 0) {
			D_ERROR("failed to fetch iterator (type=%d): %d\n",
				type, rc);
			break;
		}

		rc = cb(ih, &key_ent, type, param, arg);
		if (rc != 0)
			break;

		rc = vos_iter_next(ih);
		if (rc) {
			if (rc != -DER_NONEXIST)
				D_ERROR("failed to iterate next (type=%d): "
					"%d\n", type, rc);
			break;
		}
	}

	if (rc == -DER_NONEXIST) {
		daos_hash_set_eof(anchor);
		rc = 0;
	}

out_iter_fini:
	vos_iter_finish(ih);
out:
	return rc;
}

static int
fill_recxs_eprs(daos_handle_t ih, vos_iter_entry_t *key_ent,
		struct dss_enum_arg *arg, vos_iter_type_t type)
{
	/* check if recxs is full */
	if (arg->recxs_len >= arg->recxs_cap) {
		D_DEBUG(DB_IO, "recx_len %d recx_cap %d\n",
			arg->recxs_len, arg->recxs_cap);
		return 1;
	}

	arg->eprs[arg->eprs_len].epr_lo = key_ent->ie_epoch;
	arg->eprs[arg->eprs_len].epr_hi = DAOS_EPOCH_MAX;

	arg->eprs_len++;
	arg->recxs[arg->recxs_len] = key_ent->ie_recx;
	arg->recxs_len++;
	if (arg->rsize == 0) {
		arg->rsize = key_ent->ie_rsize;
	} else if (arg->rsize != key_ent->ie_rsize) {
		D_ERROR("different size "DF_U64" != "DF_U64"\n", arg->rsize,
			key_ent->ie_rsize);
		return -DER_INVAL;
	}

	D_DEBUG(DB_IO, "Pack recxs_eprs "DF_U64"/"DF_U64" recxs_len %d size "
		DF_U64"\n", key_ent->ie_recx.rx_idx, key_ent->ie_recx.rx_nr,
		arg->recxs_len, arg->rsize);

	arg->rnum++;
	return 0;
}

static int
fill_recxs_eprs_cb(daos_handle_t ih, vos_iter_entry_t *key_ent,
		   vos_iter_type_t type, vos_iter_param_t *param, void *arg)
{
	return fill_recxs_eprs(ih, key_ent, arg, type);
}

static int
is_sgl_kds_full(struct dss_enum_arg *arg, daos_size_t size)
{
	d_sg_list_t *sgl = arg->sgl;

	/* Find avaible iovs in sgl
	 * XXX this is buggy because key descriptors require keys are stored
	 * in sgl in the same order as descriptors, but it's OK for now because
	 * we only use one IOV.
	 */
	while (arg->sgl_idx < sgl->sg_nr) {
		daos_iov_t *iovs = sgl->sg_iovs;

		if (iovs[arg->sgl_idx].iov_len + size >=
		    iovs[arg->sgl_idx].iov_buf_len) {
			D_DEBUG(DB_IO, "current %dth iov buf is full"
				" iov_len %zd size "DF_U64" buf_len %zd\n",
				arg->sgl_idx, iovs[arg->sgl_idx].iov_len, size,
				iovs[arg->sgl_idx].iov_buf_len);
			arg->sgl_idx++;
			continue;
		}
		break;
	}

	/* Update sg_nr_out */
	if (arg->sgl_idx < sgl->sg_nr && sgl->sg_nr_out < arg->sgl_idx + 1)
		sgl->sg_nr_out = arg->sgl_idx + 1;

	/* Check if the sgl is full */
	if (arg->sgl_idx >= sgl->sg_nr || arg->kds_len >= arg->kds_cap) {
		D_DEBUG(DB_IO, "sgl or kds full sgl %d/%d kds %d/%d size "
			DF_U64"\n", arg->sgl_idx, sgl->sg_nr,
			arg->kds_len, arg->kds_cap, size);
		return 1;
	}

	return 0;
}

static int
fill_obj(daos_handle_t ih, vos_iter_entry_t *entry, struct dss_enum_arg *arg,
	 vos_iter_type_t type)
{
	daos_iov_t *iovs = arg->sgl->sg_iovs;

	D_ASSERTF(type == VOS_ITER_OBJ, "%d\n", type);

	if (is_sgl_kds_full(arg, sizeof(entry->ie_oid)))
		return 1;

	/* Append a new descriptor to kds. */
	D_ASSERT(arg->kds_len < arg->kds_cap);
	memset(&arg->kds[arg->kds_len], 0, sizeof(arg->kds[arg->kds_len]));
	arg->kds[arg->kds_len].kd_key_len = sizeof(entry->ie_oid);
	arg->kds[arg->kds_len].kd_val_types = type;
	arg->kds_len++;

	/* Append the object ID to iovs. */
	D_ASSERT(iovs[arg->sgl_idx].iov_len + sizeof(entry->ie_oid) <
		 iovs[arg->sgl_idx].iov_buf_len);
	memcpy(iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len,
	       &entry->ie_oid, sizeof(entry->ie_oid));
	iovs[arg->sgl_idx].iov_len += sizeof(entry->ie_oid);

	D_DEBUG(DB_IO, "Pack obj "DF_UOID" iov_len %zu kds_len %d\n",
		DP_UOID(entry->ie_oid), iovs[arg->sgl_idx].iov_len,
		arg->kds_len);
	return 0;
}

static int
fill_obj_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	    vos_iter_param_t *param, void *arg)
{
	return fill_obj(ih, key_ent, arg, type);
}

static int
fill_key(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
	 vos_iter_type_t type)
{
	daos_iov_t	*iovs = arg->sgl->sg_iovs;
	daos_size_t	 size;

	D_ASSERT(type == VOS_ITER_DKEY || type == VOS_ITER_AKEY);
	size = key_ent->ie_key.iov_len;

	if (is_sgl_kds_full(arg, size))
		return 1;

	D_ASSERT(arg->kds_len < arg->kds_cap);
	arg->kds[arg->kds_len].kd_key_len = size;
	arg->kds[arg->kds_len].kd_csum_len = 0;
	arg->kds[arg->kds_len].kd_val_types = type;
	arg->kds_len++;

	D_ASSERT(iovs[arg->sgl_idx].iov_len + key_ent->ie_key.iov_len <
		 iovs[arg->sgl_idx].iov_buf_len);
	memcpy(iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len,
	       key_ent->ie_key.iov_buf, key_ent->ie_key.iov_len);

	iovs[arg->sgl_idx].iov_len += key_ent->ie_key.iov_len;
	D_DEBUG(DB_IO, "Pack key %.*s iov total %zd"
		" kds len %d\n", (int)key_ent->ie_key.iov_len,
		(char *)key_ent->ie_key.iov_buf,
		iovs[arg->sgl_idx].iov_len, arg->kds_len - 1);

	return 0;
}

static int
fill_key_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	    vos_iter_param_t *param, void *arg)
{
	return fill_key(ih, key_ent, arg, type);
}

/* Copy the data of the recx into buf. */
/* TODO: Use entry->ie_eiov when implemented. */
static void
copy_data(vos_iter_type_t type, vos_iter_param_t *param,
	  vos_iter_entry_t *entry, void *buf, size_t len)
{
	daos_iod_t	iod;
	daos_iov_t	iov;
	daos_sg_list_t	sgl;
	int		rc;

	D_ASSERT(type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

	memset(&iod, 0, sizeof(iod));
	iod.iod_name = param->ip_akey;
	if (type == VOS_ITER_SINGLE)
		iod.iod_type = DAOS_IOD_SINGLE;
	else
		iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_nr = 1;
	iod.iod_recxs = &entry->ie_recx;
	iod.iod_eprs = NULL;

	iov.iov_buf = buf;
	iov.iov_buf_len = len;
	iov.iov_len = 0;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	rc = vos_obj_fetch(param->ip_hdl, param->ip_oid, entry->ie_epoch,
			   &param->ip_dkey, 1 /* iod_nr */, &iod, &sgl);
	/* This vos_obj_fetch call is a workaround anyway. */
	D_ASSERTF(rc == 0, "%d\n", rc);
	D_ASSERT(iod.iod_size == entry->ie_rsize);
}

/* Callers are responsible for incrementing arg->kds_len. See iter_akey_cb. */
static int
fill_rec(daos_handle_t ih, vos_iter_entry_t *key_ent, struct dss_enum_arg *arg,
	 vos_iter_type_t type, vos_iter_param_t *param)
{
	daos_iov_t		*iovs = arg->sgl->sg_iovs;
	struct obj_enum_rec	*rec;
	daos_size_t		 data_size;
	daos_size_t		 size = sizeof(*rec);
	bool			 inline_data = false;

	D_ASSERT(type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);

	/* Inline the data? A 0 threshold disables this completely. */
	data_size = key_ent->ie_rsize * key_ent->ie_recx.rx_nr;
	if (arg->inline_thres > 0 && data_size <= arg->inline_thres) {
		inline_data = true;
		size += data_size;
	}

	if (is_sgl_kds_full(arg, size))
		return 1;

	/* Grow the next new descriptor (instead of creating yet a new one). */
	arg->kds[arg->kds_len].kd_val_types = type;
	arg->kds[arg->kds_len].kd_key_len += sizeof(*rec);

	/* Append the recx record to iovs. */
	D_ASSERT(iovs[arg->sgl_idx].iov_len + sizeof(*rec) <
		 iovs[arg->sgl_idx].iov_buf_len);

	rec = iovs[arg->sgl_idx].iov_buf + iovs[arg->sgl_idx].iov_len;
	rec->rec_recx = key_ent->ie_recx;
	rec->rec_size = key_ent->ie_rsize;
	rec->rec_epr.epr_lo = key_ent->ie_epoch;
	rec->rec_epr.epr_hi = DAOS_EPOCH_MAX;

	uuid_copy(rec->rec_cookie, key_ent->ie_cookie);
	rec->rec_version = key_ent->ie_ver;
	rec->rec_flags = 0;
	iovs[arg->sgl_idx].iov_len += sizeof(*rec);

	/* If we've decided to inline the data, append the data to iovs. */
	if (inline_data) {
		arg->kds[arg->kds_len].kd_key_len += data_size;
		rec->rec_flags |= RECX_INLINE;
		/* Punched recxs do not have any data to copy. */
		if (data_size > 0)
			copy_data(type, param, key_ent,
				  iovs[arg->sgl_idx].iov_buf +
				  iovs[arg->sgl_idx].iov_len, data_size);
		iovs[arg->sgl_idx].iov_len += data_size;
	}

	D_DEBUG(DB_IO, "Pack rec "DF_U64"/"DF_U64
		" rsize "DF_U64" cookie "DF_UUID" ver %u"
		" kd_len "DF_U64" type %d sgl_idx %d kds_len %d inline "DF_U64
		" epr "DF_U64"/"DF_U64"\n", key_ent->ie_recx.rx_idx,
		key_ent->ie_recx.rx_nr, key_ent->ie_rsize,
		DP_UUID(rec->rec_cookie), rec->rec_version,
		arg->kds[arg->kds_len].kd_key_len, type, arg->sgl_idx,
		arg->kds_len, rec->rec_flags & RECX_INLINE ? data_size : 0,
		rec->rec_epr.epr_lo, rec->rec_epr.epr_hi);
	return 0;
}

static int
fill_rec_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	    vos_iter_param_t *param, void *arg)
{
	return fill_rec(ih, key_ent, arg, type, param);
}

static int
iter_akey_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	     vos_iter_param_t *param, void *varg)
{
	struct dss_enum_arg	*arg = varg;
	vos_iter_param_t	 iter_recx_param;
	daos_hash_out_t		 single_anchor = { 0 };
	int			 rc;

	D_DEBUG(DB_IO, "enum key %.*s type %d\n",
		(int)key_ent->ie_key.iov_len,
		(char *)key_ent->ie_key.iov_buf, type);

	/* Fill the current key */
	rc = fill_key(ih, key_ent, arg, VOS_ITER_AKEY);
	if (rc)
		goto out;

	iter_recx_param = *param;
	iter_recx_param.ip_akey = key_ent->ie_key;

	/* iterate array record */
	rc = dss_vos_iterate(VOS_ITER_RECX, &iter_recx_param, &arg->recx_anchor,
			     fill_rec_cb, arg);

	if (arg->kds[arg->kds_len].kd_key_len > 0)
		arg->kds_len++;

	/* Exit either failure or buffer is full */
	if (rc) {
		if (rc < 0)
			D_ERROR("failed to enumerate array recxs: %d\n", rc);
		goto out;
	}

	D_ASSERT(daos_hash_is_eof(&arg->recx_anchor));
	enum_anchor_reset_hkey(&arg->recx_anchor);

	/* iterate single record */
	rc = dss_vos_iterate(VOS_ITER_SINGLE, &iter_recx_param, &single_anchor,
			     fill_rec_cb, arg);

	if (rc) {
		if (rc < 0)
			D_ERROR("failed to enumerate single recxs: %d\n", rc);
		goto out;
	}

	if (arg->kds[arg->kds_len].kd_key_len > 0)
		arg->kds_len++;
out:
	return rc;
}

static int
iter_dkey_cb(daos_handle_t ih, vos_iter_entry_t *key_ent, vos_iter_type_t type,
	     vos_iter_param_t *param, void *varg)
{
	struct dss_enum_arg	*arg = varg;
	vos_iter_param_t	 iter_akey_param;
	int			 rc;

	D_DEBUG(DB_IO, "enum key %.*s type %d\n",
		(int)key_ent->ie_key.iov_len,
		(char *)key_ent->ie_key.iov_buf, type);

	/* Fill the current dkey */
	rc = fill_key(ih, key_ent, arg, VOS_ITER_DKEY);
	if (rc != 0)
		return rc;

	/* iterate akey */
	iter_akey_param = *param;
	iter_akey_param.ip_dkey = key_ent->ie_key;
	rc = dss_vos_iterate(VOS_ITER_AKEY, &iter_akey_param, &arg->akey_anchor,
			     iter_akey_cb, arg);
	if (rc) {
		if (rc < 0)
			D_ERROR("failed to enumerate akeys: %d\n", rc);
		return rc;
	}

	D_ASSERT(daos_hash_is_eof(&arg->akey_anchor));
	enum_anchor_reset_hkey(&arg->akey_anchor);
	enum_anchor_reset_hkey(&arg->recx_anchor);

	return rc;
}

static int
iter_obj_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
	    vos_iter_param_t *param, void *varg)
{
	struct dss_enum_arg	*arg = varg;
	vos_iter_param_t	 iter_dkey_param;
	int			 rc;

	D_ASSERTF(type == VOS_ITER_OBJ, "%d\n", type);
	D_DEBUG(DB_IO, "enum obj "DF_UOID"\n", DP_UOID(entry->ie_oid));

	rc = fill_obj(ih, entry, arg, type);
	if (rc != 0)
		return rc;

	iter_dkey_param = *param;
	iter_dkey_param.ip_oid = entry->ie_oid;
	rc = dss_vos_iterate(VOS_ITER_DKEY, &iter_dkey_param, &arg->dkey_anchor,
			     iter_dkey_cb, arg);
	if (rc != 0) {
		if (rc < 0)
			D_ERROR("failed to enumerate dkeys: %d\n", rc);
		return rc;
	}

	D_ASSERT(daos_hash_is_eof(&arg->dkey_anchor));
	enum_anchor_reset_hkey(&arg->dkey_anchor);
	enum_anchor_reset_hkey(&arg->akey_anchor);
	enum_anchor_reset_hkey(&arg->recx_anchor);

	return 0;
}

/**
 * Enumerate VOS objects, dkeys, akeys, and/or recxs and pack them into a set
 * of buffers.
 *
 * The buffers must be provided by the caller. They may contain existing data,
 * in which case this function appends to them.
 *
 * \param[in]		type	iteration type
 * \param[in,out]	arg	enumeration argument
 *
 * \retval		0	enumeration complete
 * \retval		1	buffer(s) full
 * \retval		-DER_*	error
 */
int
dss_enum_pack(vos_iter_type_t type, struct dss_enum_arg *arg)
{
	daos_hash_out_t	       *anchor;
	dss_vos_iterate_cb_t	cb;
	int			rc;

	D_ASSERT(!arg->recxs_eprs ||
		 (type == VOS_ITER_SINGLE || type == VOS_ITER_RECX));

	switch (type) {
	case VOS_ITER_OBJ:
		anchor = &arg->obj_anchor;
		cb = arg->recursive ? iter_obj_cb : fill_obj_cb;
		break;
	case VOS_ITER_DKEY:
		anchor = &arg->dkey_anchor;
		cb = arg->recursive ? iter_dkey_cb : fill_key_cb;
		break;
	case VOS_ITER_AKEY:
		anchor = &arg->akey_anchor;
		cb = arg->recursive ? iter_akey_cb : fill_key_cb;
		break;
	case VOS_ITER_SINGLE:
	case VOS_ITER_RECX:
		anchor = &arg->recx_anchor;
		cb = arg->recxs_eprs ? fill_recxs_eprs_cb : fill_rec_cb;
		break;
	default:
		D_ASSERTF(false, "unknown/unsupported type %d\n", type);
	}

	rc = dss_vos_iterate(type, &arg->param, anchor, cb, arg);

	D_DEBUG(DB_IO, "enum type %d tag %d rc %d\n", type,
		dss_get_module_info()->dmi_tid, rc);
	return rc;
}
#endif

static int
ds_iter_single_vos(void *data)
{
	struct ds_task_arg	*arg = data;
	struct ds_iter_arg	*iter_arg = &arg->u.iter_arg;
	struct dss_enum_arg	*enum_arg = &iter_arg->enum_arg;
	struct obj_key_enum_in	*oei = iter_arg->oei;
	struct ds_cont_hdl	*cont_hdl;
	struct ds_cont		*cont;
	int			type;
	int			rc;

	rc = ds_check_container(oei->oei_co_hdl, oei->oei_co_uuid,
				&cont_hdl, &cont);
	if (rc)
		D_GOTO(out, rc);

	D_ASSERT(cont_hdl->sch_pool != NULL);
	if (iter_arg->map_version == 0)
		iter_arg->map_version = cont_hdl->sch_pool->spc_map_version;

	if (oei->oei_map_ver < iter_arg->map_version)
		D_DEBUG(DB_IO, "stale version req %d map_version %d\n",
			oei->oei_map_ver, iter_arg->map_version);

	/* prepare enumeration parameters */
	memset(&enum_arg->param, 0, sizeof(enum_arg->param));
	enum_arg->param.ip_hdl = cont->sc_hdl;
	enum_arg->param.ip_oid = oei->oei_oid;
	if (oei->oei_dkey.iov_len > 0)
		enum_arg->param.ip_dkey = oei->oei_dkey;
	if (oei->oei_akey.iov_len > 0)
		enum_arg->param.ip_akey = oei->oei_akey;
	enum_arg->param.ip_epr.epr_lo = oei->oei_epoch;
	enum_arg->param.ip_epr.epr_hi = oei->oei_epoch;
	enum_arg->param.ip_epc_expr = VOS_IT_EPC_LE;

	if (arg->opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		if (oei->oei_dkey.iov_len == 0 ||
		    oei->oei_akey.iov_len == 0)
			D_GOTO(out_cont_hdl, rc = -DER_PROTO);

		if (oei->oei_rec_type == DAOS_IOD_ARRAY)
			type = VOS_ITER_RECX;
		else
			type = VOS_ITER_SINGLE;

		enum_arg->param.ip_epc_expr = VOS_IT_EPC_RE;
		enum_arg->recxs_eprs = true;
	} else if (arg->opc == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		type = VOS_ITER_DKEY;
	} else if (arg->opc == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		type = VOS_ITER_AKEY;
	} else {
		/* object iteration for rebuild */
		D_ASSERT(arg->opc == DAOS_OBJ_RPC_ENUMERATE);
		type = VOS_ITER_DKEY;
		enum_arg->param.ip_epr.epr_lo = 0;
		enum_arg->param.ip_epc_expr = VOS_IT_EPC_RE;
		enum_arg->recursive = true;
	}

	rc = dss_enum_pack(type, enum_arg);

	D_DEBUG(DB_IO, ""DF_UOID" iterate type %d tag %d rc %d\n",
		DP_UOID(oei->oei_oid), type, dss_get_module_info()->dmi_tid,
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

	rc = ds_bulk_transfer(rpc, CRT_BULK_PUT, bulks, DAOS_HDL_INVAL,
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
	struct ds_task_arg	task_arg;
	struct ds_iter_arg	*iter_arg = &task_arg.u.iter_arg;
	struct dss_enum_arg	*enum_arg = &iter_arg->enum_arg;
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int			rc = 0;
	int			tag;

	memset(&task_arg, 0, sizeof(task_arg));
	oei = crt_req_get(rpc);
	D_ASSERT(oei != NULL);
	oeo = crt_reply_get(rpc);
	D_ASSERT(oeo != NULL);
	/* prepare buffer for enumerate */
	task_arg.opc = opc_get(rpc->cr_opc);
	iter_arg->oei = crt_req_get(rpc);
	iter_arg->oeo = crt_reply_get(rpc);
	iter_arg->map_version = 0;

	enum_arg->dkey_anchor = oei->oei_dkey_anchor;
	enum_arg->akey_anchor = oei->oei_akey_anchor;
	enum_arg->recx_anchor = oei->oei_anchor;

	/* TODO: Modify the client side and enable inline recx data. */
	enum_arg->inline_thres = 0;

	if (task_arg.opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		oeo->oeo_eprs.ca_count = 0;
		D_ALLOC(oeo->oeo_eprs.ca_arrays,
			oei->oei_nr * sizeof(daos_epoch_range_t));
		if (oeo->oeo_eprs.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		enum_arg->eprs = oeo->oeo_eprs.ca_arrays;
		enum_arg->eprs_cap = oei->oei_nr;
		enum_arg->eprs_len = 0;

		oeo->oeo_recxs.ca_count = 0;
		D_ALLOC(oeo->oeo_recxs.ca_arrays,
			oei->oei_nr * sizeof(daos_recx_t));
		if (oeo->oeo_recxs.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		enum_arg->recxs = oeo->oeo_recxs.ca_arrays;
		enum_arg->recxs_cap = oei->oei_nr;
		enum_arg->recxs_len = 0;
	} else {
		rc = ds_sgls_prep(&oeo->oeo_sgl, &oei->oei_sgl, 1);
		if (rc != 0)
			D_GOTO(out, rc);
		enum_arg->sgl = &oeo->oeo_sgl;
		enum_arg->sgl_idx = 0;

		/* Prepare key desciptor buffer */
		oeo->oeo_kds.ca_count = 0;
		D_ALLOC(oeo->oeo_kds.ca_arrays,
			oei->oei_nr * sizeof(daos_key_desc_t));
		if (oeo->oeo_kds.ca_arrays == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		enum_arg->kds = oeo->oeo_kds.ca_arrays;
		enum_arg->kds_cap = oei->oei_nr;
		enum_arg->kds_len = 0;
	}

	/* keep trying until the key_buffer is fully filled or
	 * reaching the end of the stream
	 */
	tag = dss_get_module_info()->dmi_tid;
	while (1) {
		if (tag == dss_get_module_info()->dmi_tid ||
		    (task_arg.opc != DAOS_OBJ_DKEY_RPC_ENUMERATE &&
		     task_arg.opc != DAOS_OBJ_RPC_ENUMERATE))
			rc = ds_iter_single_vos(&task_arg);
		else
			rc = dss_ult_create_execute(ds_iter_single_vos,
						    &task_arg,
						    NULL /* user callback */,
						    NULL /* user cb args */,
						    tag/* async */, 0);
		if (rc != 0) {
			if (rc == 1) {
				/* If the buffer is full, exit and
				 * reset failure.
				 */
				rc = 0;
				break;
			}
			D_GOTO(out, rc);
		}

		/* If the enumeration does not cross the tag */
		if (task_arg.opc != DAOS_OBJ_DKEY_RPC_ENUMERATE &&
		    task_arg.opc != DAOS_OBJ_RPC_ENUMERATE)
			break;

		D_DEBUG(DB_IO, "try next tag %d\n", tag + 1);
		if (++tag >= dss_get_threads_number())
			break;

		enum_anchor_reset_hkey(&enum_arg->recx_anchor);
		enum_anchor_reset_hkey(&enum_arg->dkey_anchor);
		enum_anchor_reset_hkey(&enum_arg->akey_anchor);
	}

	enum_anchor_set_tag(&enum_arg->dkey_anchor, tag);
	oeo->oeo_dkey_anchor = enum_arg->dkey_anchor;
	oeo->oeo_akey_anchor = enum_arg->akey_anchor;
	oeo->oeo_anchor = enum_arg->recx_anchor;

	if (task_arg.opc == DAOS_OBJ_RECX_RPC_ENUMERATE) {
		oeo->oeo_eprs.ca_count = enum_arg->eprs_len;
		oeo->oeo_recxs.ca_count = enum_arg->recxs_len;
		oeo->oeo_num = enum_arg->rnum;
		oeo->oeo_size = enum_arg->rsize;
	} else {
		oeo->oeo_kds.ca_count = enum_arg->kds_len;
		oeo->oeo_num = enum_arg->kds_len;
		oeo->oeo_size = oeo->oeo_sgl.sg_iovs[0].iov_len;
	}

	rc = obj_enum_reply_bulk(rpc);
out:
	ds_eu_complete(rpc, rc, &task_arg.u.iter_arg);
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

struct obj_punch_args {
	struct obj_punch_in	*opi;
	crt_opcode_t		 opc;
	uint32_t		 map_version;
};

static int
ds_obj_punch(void *punch_args)
{
	struct obj_punch_args	*args = punch_args;
	struct obj_punch_in	*opi = args->opi;
	struct ds_cont_hdl	*cont_hdl = NULL;
	struct ds_cont		*cont = NULL;
	uint32_t		 map_version = 0;
	int			 i;
	int			 rc;

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
	}

	switch (opc_get(args->opc)) {
	default:
		D_ERROR("opc %#x not supported\n", opc_get(args->opc));
		D_GOTO(out, rc = -DER_NOSYS);

	case DAOS_OBJ_RPC_PUNCH:
		rc = vos_obj_punch(cont->sc_hdl, opi->opi_oid,
				   opi->opi_epoch, cont_hdl->sch_uuid,
				   opi->opi_map_ver, 0, NULL, 0, NULL);
		break;
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
		for (i = 0; i < opi->opi_dkeys.ca_count; i++) {
			daos_key_t *dkey;

			dkey = &((daos_key_t *)opi->opi_dkeys.ca_arrays)[i];
			rc = vos_obj_punch(cont->sc_hdl,
					   opi->opi_oid,
					   opi->opi_epoch,
					   cont_hdl->sch_uuid,
					   opi->opi_map_ver, 0, dkey,
					   opi->opi_akeys.ca_count,
					   opi->opi_akeys.ca_arrays);
			if (rc)
				D_GOTO(out, rc);
		}
		break;
	}

out:
	if (cont_hdl) {
		if (!cont_hdl->sch_cont)
			ds_cont_put(cont); /* -1 for rebuild container */
		ds_cont_hdl_put(cont_hdl);
	}
	args->map_version = map_version;
	return rc;
}

void
ds_obj_punch_handler(crt_rpc_t *rpc)
{
	struct obj_punch_in	*opi;
	struct obj_punch_args	 args;
	int			 rc;

	opi = crt_req_get(rpc);
	D_ASSERT(opi != NULL);
	args.opi = opi;
	args.opc = rpc->cr_opc;

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_PUNCH)
		rc = dss_task_collective(ds_obj_punch, &args);
	else
		rc = ds_obj_punch(&args);

	obj_punch_complete(rpc, rc, args.map_version);
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
	ABT_pool pool;

	if (opc_get(rpc->cr_opc) == DAOS_OBJ_DKEY_RPC_ENUMERATE ||
	    opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_PUNCH ||
	    opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_ENUMERATE)
		pool = pools[DSS_POOL_SHARE];
	else
		pool = pools[DSS_POOL_PRIV];

	return pool;
}
