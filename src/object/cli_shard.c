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
 * object shard operations.
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/container.h>
#include <daos/pool.h>
#include <daos/pool_map.h>
#include <daos/rpc.h>
#include "obj_rpc.h"
#include "obj_internal.h"

static void
obj_shard_free(struct dc_obj_shard *shard)
{
	D_FREE_PTR(shard);
}

static struct dc_obj_shard *
obj_shard_alloc(daos_unit_oid_t id, uint32_t part_nr)
{
	struct dc_obj_shard *shard;

	D_ALLOC_PTR(shard);
	if (shard == NULL)
		return NULL;

	shard->do_part_nr = part_nr;
	shard->do_id	  = id;
	D_INIT_LIST_HEAD(&shard->do_co_list);

	return shard;
}

void
obj_shard_decref(struct dc_obj_shard *shard)
{
	bool do_free;

	D_ASSERT(shard != NULL && shard->do_ref > 0);

	D_SPIN_LOCK(&shard->do_obj->cob_spin);
	shard->do_ref--;
	do_free = (shard->do_ref == 0);
	D_SPIN_UNLOCK(&shard->do_obj->cob_spin);
	if (do_free) {
		shard->do_obj = NULL;
		obj_shard_free(shard);
	}
}

void
obj_shard_addref(struct dc_obj_shard *shard)
{
	D_SPIN_LOCK(&shard->do_obj->cob_spin);
	shard->do_ref++;
	D_SPIN_UNLOCK(&shard->do_obj->cob_spin);
}

int
dc_obj_shard_open(struct dc_object *obj, uint32_t shard_idx, daos_unit_oid_t id,
		  unsigned int mode, struct dc_obj_shard **shard)
{
	struct pl_obj_shard	*layout;
	struct dc_obj_shard	*obj_shard;
	int			i;

	D_ASSERT(obj != NULL && shard != NULL);
	D_ASSERT(obj->cob_layout != NULL);
	D_ASSERT(shard_idx < obj->cob_layout->ol_nr);
	layout = &obj->cob_layout->ol_shards[shard_idx];

	obj_shard = obj_shard_alloc(id, layout->po_shard_tgt_nr);
	if (obj_shard == NULL)
		return -DER_NOMEM;

	D_ALLOC(obj_shard->do_shard_tgts,
		layout->po_shard_tgt_nr * sizeof(obj_shard->do_shard_tgts));
	if (obj_shard->do_shard_tgts == NULL) {
		obj_shard_free(obj_shard);
		return -DER_NOMEM;
	}

	for (i = 0; i < layout->po_shard_tgt_nr; i++)
		obj_shard->do_shard_tgts[i].do_rank = (d_rank_t)(-1);

	obj_shard->do_layout = layout;
	obj_shard->do_obj = obj;
	obj_shard->do_co_hdl = obj->cob_coh;
	obj_shard_addref(obj_shard);
	*shard = obj_shard;

	return 0;
}

void
dc_obj_shard_close(struct dc_obj_shard *shard)
{
	obj_shard_decref(shard);
}

static void
obj_shard_rw_bulk_fini(crt_rpc_t *rpc)
{
	struct obj_rw_in	*orw;
	crt_bulk_t		*bulks;
	unsigned int		nr;
	int			i;

	orw = crt_req_get(rpc);
	bulks = orw->orw_bulks.ca_arrays;
	if (bulks == NULL)
		return;

	nr = orw->orw_bulks.ca_count;
	for (i = 0; i < nr; i++)
		crt_bulk_free(bulks[i]);

	D_FREE(bulks);
	orw->orw_bulks.ca_arrays = NULL;
	orw->orw_bulks.ca_count = 0;
}

struct obj_rw_args {
	crt_rpc_t	*rpc;
	daos_handle_t	*hdlp;
	daos_sg_list_t	*rwaa_sgls;
	struct dc_obj_shard	*dobj;
	unsigned int	*map_ver;
	uint32_t	 rwaa_nr;
};

static int
dc_rw_cb(tse_task_t *task, void *arg)
{
	struct obj_rw_args	*rw_args = arg;
	struct obj_rw_in	*orw;
	struct obj_rw_out	*orwo;
	int			opc;
	int                     ret = task->dt_result;
	int			rc = 0;

	opc = opc_get(rw_args->rpc->cr_opc);
	if (opc == DAOS_OBJ_RPC_FETCH &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_FETCH_TIMEOUT)) {
		D_ERROR("Inducing -DER_TIMEDOUT error on shard I/O fetch\n");
		D_GOTO(out, rc = -DER_TIMEDOUT);
	}
	if (opc == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_UPDATE_TIMEOUT)) {
		D_ERROR("Inducing -DER_TIMEDOUT error on shard I/O update\n");
		D_GOTO(out, rc = -DER_TIMEDOUT);
	}
	if (opc == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_OBJ_UPDATE_NOSPACE)) {
		D_ERROR("Inducing -DER_NOSPACE error on shard I/O update\n");
		D_GOTO(out, rc = -DER_NOSPACE);
	}

	orw = crt_req_get(rw_args->rpc);
	D_ASSERT(orw != NULL);
	if (ret != 0) {
		/*
		 * If any failure happens inside Cart, let's reset failure to
		 * TIMEDOUT, so the upper layer can retry.
		 */
		D_ERROR("RPC %d failed: %d\n",
			opc_get(rw_args->rpc->cr_opc), ret);
		D_GOTO(out, ret);
	}

	rc = obj_reply_get_status(rw_args->rpc);
	if (rc != 0) {
		D_ERROR("rpc %p RPC %d failed: %d\n", rw_args->rpc,
			 opc_get(rw_args->rpc->cr_opc), rc);
		D_GOTO(out, rc);
	}
	*rw_args->map_ver = obj_reply_map_version_get(rw_args->rpc);

	orwo = crt_reply_get(rw_args->rpc);
	rw_args->dobj->do_md.smd_attr = orwo->orw_attr;
	if (opc_get(rw_args->rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
		daos_iod_t	*iods;
		uint64_t	*sizes;
		int		 i;

		iods = orw->orw_iods.ca_arrays;
		sizes = orwo->orw_sizes.ca_arrays;

		if (orwo->orw_sizes.ca_count != orw->orw_nr) {
			D_ERROR("out:%u != in:%u\n",
				(unsigned)orwo->orw_sizes.ca_count,
				orw->orw_nr);
			D_GOTO(out, rc = -DER_PROTO);
		}

		/* update the sizes in iods */
		for (i = 0; i < orw->orw_nr; i++)
			iods[i].iod_size = sizes[i];

		if (orwo->orw_sgls.ca_count > 0) {
			/* inline transfer */
			rc = daos_sgls_copy_data_out(rw_args->rwaa_sgls,
						     rw_args->rwaa_nr,
						     orwo->orw_sgls.ca_arrays,
						     orwo->orw_sgls.ca_count);
		} else if (rw_args->rwaa_sgls != NULL) {
			/* for bulk transfer it needs to update sg_nr_out */
			daos_sg_list_t *sgls = rw_args->rwaa_sgls;
			uint32_t       *nrs;
			uint32_t	nrs_count;

			nrs = orwo->orw_nrs.ca_arrays;
			nrs_count = orwo->orw_nrs.ca_count;
			if (nrs_count != rw_args->rwaa_nr) {
				D_ERROR("Invalid nrs %u != %u\n", nrs_count,
					rw_args->rwaa_nr);
				D_GOTO(out, rc = -DER_PROTO);
			}

			/* update sgl_nr */
			for (i = 0; i < nrs_count; i++)
				sgls[i].sg_nr_out = nrs[i];
		}
	}
out:
	obj_shard_rw_bulk_fini(rw_args->rpc);
	crt_req_decref(rw_args->rpc);
	obj_shard_decref(rw_args->dobj);
	dc_pool_put((struct dc_pool *)rw_args->hdlp);

	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

/**
 * XXX: Only use dkey to distribute the data among targets for
 * now, and eventually, it should use dkey + akey, but then
 * it means the I/O descriptor might needs to be split into
 * mulitple requests in obj_shard_rw()
 */
static uint32_t
obj_shard_dkeyhash2tag(struct dc_obj_shard *obj_shard, uint64_t hash)
{
	return hash % obj_shard->do_part_nr;
}

static int
obj_shard_rw_bulk_prep(crt_rpc_t *rpc, unsigned int nr, daos_sg_list_t *sgls,
		       tse_task_t *task)
{
	struct obj_rw_in	*orw;
	crt_bulk_t		*bulks;
	crt_bulk_perm_t		 bulk_perm;
	int			 i;
	int			 rc = 0;

	bulk_perm = (opc_get(rpc->cr_opc) == DAOS_OBJ_RPC_UPDATE) ?
		    CRT_BULK_RO : CRT_BULK_RW;
	D_ALLOC(bulks, nr * sizeof(*bulks));
	if (bulks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* create bulk transfer for daos_sg_list */
	for (i = 0; i < nr; i++) {
		if (sgls != NULL && sgls[i].sg_iovs != NULL &&
		    sgls[i].sg_iovs[0].iov_buf != NULL) {
			rc = crt_bulk_create(daos_task2ctx(task),
					     daos2crt_sg(&sgls[i]),
					     bulk_perm, &bulks[i]);
			if (rc < 0) {
				int j;

				for (j = 0; j < i; j++)
					crt_bulk_free(bulks[j]);

				D_GOTO(out, rc);
			}
		}
	}

	orw = crt_req_get(rpc);
	D_ASSERT(orw != NULL);
	orw->orw_bulks.ca_count = nr;
	orw->orw_bulks.ca_arrays = bulks;
out:
	if (rc != 0 && bulks != NULL)
		D_FREE(bulks);

	return rc;
}

static struct dc_pool *
obj_shard_ptr2pool(struct dc_obj_shard *shard)
{
	daos_handle_t poh;

	poh = dc_cont_hdl2pool_hdl(shard->do_co_hdl);
	if (daos_handle_is_inval(poh))
		return NULL;

	return dc_hdl2pool(poh);
}

static int
obj_shard_rank_get(struct dc_obj_shard *shard, int tgt_idx, int opc)
{
	struct pool_target *tgt;
	int rc;

	D_ASSERT(shard->do_layout != NULL);
	D_ASSERT(shard->do_layout->po_shard_tgts != NULL);
	D_ASSERT(shard->do_layout->po_shard_tgt_nr > tgt_idx);
	/* If the current target is being rebuilt, let's return
	 * STALE, so rebuild the layout and retry
	 */
	if (opc != DAOS_OBJ_RPC_UPDATE &&
	    shard->do_layout->po_shard_tgts[tgt_idx].pot_rebuilding) {
		D_DEBUG(DB_IO, "obj "DF_UOID" tgt_idx %d rebuilding.\n",
			DP_UOID(shard->do_id), tgt_idx);
		if (obj_get_grp_size(shard->do_obj) > 1)
			return -DER_STALE;
		else
			return -DER_NONEXIST;
	}

	if (shard->do_shard_tgts[tgt_idx].do_rank != (d_rank_t)(-1))
		return shard->do_shard_tgts[tgt_idx].do_rank;

	rc = dc_cont_tgt_idx2ptr(shard->do_co_hdl,
		shard->do_layout->po_shard_tgts[tgt_idx].pot_target,
		&tgt);
	D_ASSERT(rc == 0);

	shard->do_shard_tgts[tgt_idx].do_rank = tgt->ta_comp.co_rank;

	return shard->do_shard_tgts[tgt_idx].do_rank;
}

static int
obj_shard_rw(struct dc_obj_shard *shard, enum obj_rpc_opc opc,
	     daos_epoch_t epoch, daos_key_t *dkey, unsigned int nr,
	     daos_iod_t *iods, daos_sg_list_t *sgls, unsigned int *map_ver,
	     tse_task_t *task)
{
	struct dc_pool	       *pool;
	crt_rpc_t	       *req;
	struct obj_rw_in       *orw;
	struct obj_rw_args	rw_args;
	crt_endpoint_t		tgt_ep;
	uuid_t			cont_hdl_uuid;
	uuid_t			cont_uuid;
	daos_size_t		iod_len;
	daos_size_t		sgl_len;
	uint64_t		dkey_hash;
	bool			do_bulk = false;
	int			rc;

	tse_task_stack_pop_data(task, &dkey_hash, sizeof(dkey_hash));
	obj_shard_addref(shard);
	rc = dc_cont_hdl2uuid(shard->do_co_hdl, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_obj, rc);

	pool = obj_shard_ptr2pool(shard);
	if (pool == NULL)
		D_GOTO(out_obj, rc);

	tgt_ep.ep_grp = pool->dp_group;
	tgt_ep.ep_tag = obj_shard_dkeyhash2tag(shard, dkey_hash);
	tgt_ep.ep_rank = obj_shard_rank_get(shard, tgt_ep.ep_tag, opc);
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out_pool, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_TRACE, "opc %d "DF_UOID" %.*s rank %d tag %d eph "DF_U64"\n",
		opc, DP_UOID(shard->do_id), (int)dkey->iov_len,
		(char *)dkey->iov_buf, tgt_ep.ep_rank, tgt_ep.ep_tag, epoch);
	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out_pool, rc);

	orw = crt_req_get(req);
	D_ASSERT(orw != NULL);

	orw->orw_map_ver = *map_ver;
	orw->orw_oid = shard->do_id;
	uuid_copy(orw->orw_co_hdl, cont_hdl_uuid);
	uuid_copy(orw->orw_co_uuid, cont_uuid);

	orw->orw_epoch = epoch;
	orw->orw_nr = nr;
	/** FIXME: large dkey should be transferred via bulk */
	orw->orw_dkey = *dkey;

	/* FIXME: if iods is too long, then we needs to do bulk transfer
	 * as well, but then we also needs to serialize the iods
	 **/
	orw->orw_iods.ca_count = nr;
	orw->orw_iods.ca_arrays = iods;

	iod_len = daos_iods_len(iods, nr);
	sgl_len = daos_sgls_buf_len(sgls, nr);
	/* If it is read, let's try to get the size from sg list */
	if (iod_len == -1 && opc == DAOS_OBJ_RPC_FETCH)
		iod_len = daos_sgls_buf_len(sgls, nr);
	if (iod_len != -1 && iod_len > sgl_len) {
		rc = -DER_REC2BIG;
		D_ERROR("Object "DF_UOID", iod_len "DF_U64", sgl_len "
			DF_U64", failed %d.\n", DP_UOID(shard->do_id),
			iod_len, sgl_len, rc);
		D_GOTO(out_req, rc);
	}

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_FAIL))
		D_GOTO(out_req, rc = -DER_INVAL);

	do_bulk = iod_len >= OBJ_BULK_LIMIT;
	if (do_bulk) {
		/* Transfer data by bulk */
		rc = obj_shard_rw_bulk_prep(req, nr, sgls, task);
		if (rc != 0)
			D_GOTO(out_req, rc);
		orw->orw_sgls.ca_count = 0;
		orw->orw_sgls.ca_arrays = NULL;
	} else {
		/* Transfer data inline */
		if (sgls != NULL)
			orw->orw_sgls.ca_count = nr;
		else
			orw->orw_sgls.ca_count = 0;
		orw->orw_sgls.ca_arrays = sgls;
		orw->orw_bulks.ca_count = 0;
		orw->orw_bulks.ca_arrays = NULL;
	}

	crt_req_addref(req);
	rw_args.rpc = req;
	rw_args.hdlp = (daos_handle_t *)pool;
	rw_args.map_ver = map_ver;
	rw_args.dobj = shard;

	if (opc == DAOS_OBJ_RPC_FETCH) {
		/* remember the sgl to copyout the data inline for fetch */
		rw_args.rwaa_nr = nr;
		rw_args.rwaa_sgls = sgls;
	} else {
		rw_args.rwaa_nr = 0;
		rw_args.rwaa_sgls = NULL;
	}

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_RW_CRT_ERROR))
		D_GOTO(out_args, rc = -DER_HG);

	rc = tse_task_register_comp_cb(task, dc_rw_cb, &rw_args,
				       sizeof(rw_args));
	if (rc != 0)
		D_GOTO(out_args, rc);

	if (cli_bypass_rpc) {
		rc = daos_rpc_complete(req, task);
	} else {
		rc = daos_rpc_send(req, task);
		if (rc != 0) {
			D_ERROR("update/fetch rpc failed rc %d\n", rc);
			D_GOTO(out_args, rc);
		}
	}
	return rc;

out_args:
	crt_req_decref(req);
	if (do_bulk)
		obj_shard_rw_bulk_fini(req);
out_req:
	crt_req_decref(req);
out_pool:
	dc_pool_put(pool);
out_obj:
	obj_shard_decref(shard);
	tse_task_complete(task, rc);
	return rc;
}

struct obj_punch_cb_args {
	crt_rpc_t	*rpc;
	unsigned int	*map_ver;
};

static int
obj_shard_punch_cb(tse_task_t *task, void *data)
{
	struct obj_punch_cb_args	*cb_args;
	crt_rpc_t			*rpc;

	cb_args = (struct obj_punch_cb_args *)data;
	rpc = cb_args->rpc;
	if (task->dt_result == 0) {
		task->dt_result = obj_reply_get_status(rpc);
		*cb_args->map_ver = obj_reply_map_version_get(rpc);
	}

	crt_req_decref(rpc);
	return task->dt_result;
}

int
dc_obj_shard_punch(struct dc_obj_shard *shard, uint32_t opc, daos_epoch_t epoch,
		   daos_key_t *dkey, daos_key_t *akeys, unsigned int akey_nr,
		   const uuid_t coh_uuid, const uuid_t cont_uuid,
		   unsigned int *map_ver, tse_task_t *task)
{
	struct dc_pool			*pool;
	struct obj_punch_in		*opi;
	crt_rpc_t			*req;
	struct obj_punch_cb_args	 cb_args;
	daos_unit_oid_t			 oid;
	crt_endpoint_t			 tgt_ep;
	uint64_t			 dkey_hash;
	int				 rc;

	tse_task_stack_pop_data(task, &dkey_hash, sizeof(dkey_hash));

	pool = obj_shard_ptr2pool(shard);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	oid = shard->do_id;
	tgt_ep.ep_grp	= pool->dp_group;
	tgt_ep.ep_tag	= obj_shard_dkeyhash2tag(shard, dkey_hash);
	tgt_ep.ep_rank = obj_shard_rank_get(shard, tgt_ep.ep_tag, opc);
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out, rc = (int)tgt_ep.ep_rank);

	dc_pool_put(pool);

	D_DEBUG(DB_IO, "opc=%d, rank=%d tag=%d.\n",
		 opc, tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_req_addref(req);
	cb_args.rpc = req;
	cb_args.map_ver = map_ver;
	rc = tse_task_register_comp_cb(task, obj_shard_punch_cb, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_req, rc);

	opi = crt_req_get(req);
	D_ASSERT(opi != NULL);

	opi->opi_map_ver	 = *map_ver;
	opi->opi_epoch		 = epoch;
	opi->opi_oid		 = oid;
	opi->opi_dkeys.ca_count  = (dkey == NULL) ? 0 : 1;
	opi->opi_dkeys.ca_arrays = dkey;
	opi->opi_akeys.ca_count	 = akey_nr;
	opi->opi_akeys.ca_arrays = akeys;
	uuid_copy(opi->opi_co_hdl, coh_uuid);
	uuid_copy(opi->opi_co_uuid, cont_uuid);

	rc = daos_rpc_send(req, task);
	if (rc != 0) {
		D_ERROR("punch rpc failed rc %d\n", rc);
		D_GOTO(out_req, rc);
	}
	return rc;

out_req:
	crt_req_decref(req);
out:
	tse_task_complete(task, rc);
	return rc;
}

int
dc_obj_shard_update(struct dc_obj_shard *shard, daos_epoch_t epoch,
		    daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		    daos_sg_list_t *sgls, unsigned int *map_ver,
		    tse_task_t *task)
{
	return obj_shard_rw(shard, DAOS_OBJ_RPC_UPDATE, epoch, dkey,
			    nr, iods, sgls, map_ver, task);
}

int
dc_obj_shard_fetch(struct dc_obj_shard *shard, daos_epoch_t epoch,
		   daos_key_t *dkey,  unsigned int nr, daos_iod_t *iods,
		   daos_sg_list_t *sgls, daos_iom_t *maps,
		   unsigned int *map_ver, tse_task_t *task)
{
	return obj_shard_rw(shard, DAOS_OBJ_RPC_FETCH, epoch, dkey,
			    nr, iods, sgls, map_ver, task);
}

struct obj_enum_args {
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
	uint32_t		*eaa_nr;
	daos_key_desc_t		*eaa_kds;
	daos_anchor_t		*eaa_anchor;
	daos_anchor_t		*eaa_dkey_anchor;
	daos_anchor_t		*eaa_akey_anchor;
	struct dc_obj_shard	*eaa_obj;
	daos_sg_list_t		*eaa_sgl;
	daos_recx_t		*eaa_recxs;
	daos_epoch_range_t	*eaa_eprs;
	daos_size_t		*eaa_size;
	unsigned int		*eaa_map_ver;
};

static int
dc_enumerate_cb(tse_task_t *task, void *arg)
{
	struct obj_enum_args	*enum_args = (struct obj_enum_args *)arg;
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int			 tgt_tag;
	int			 ret = task->dt_result;
	int			 rc = 0;

	oei = crt_req_get(enum_args->rpc);
	D_ASSERT(oei != NULL);

	if (ret != 0) {
		/* If any failure happens inside Cart, let's reset
		 * failure to TIMEDOUT, so the upper layer can retry
		 **/
		D_ERROR("RPC %d failed: %d\n",
			opc_get(enum_args->rpc->cr_opc), ret);
		D_GOTO(out, ret);
	}

	oeo = crt_reply_get(enum_args->rpc);
	rc = obj_reply_get_status(enum_args->rpc);
	if (rc != 0) {
		D_ERROR("rpc %p RPC %d failed: %d\n", enum_args->rpc,
			 opc_get(enum_args->rpc->cr_opc), rc);
		if (rc == -DER_KEY2BIG)
			enum_args->eaa_kds[0].kd_key_len = oeo->oeo_size;
		D_GOTO(out, rc);
	}
	*enum_args->eaa_map_ver = obj_reply_map_version_get(enum_args->rpc);

	if (enum_args->eaa_size)
		*enum_args->eaa_size = oeo->oeo_size;

	if (*enum_args->eaa_nr < oeo->oeo_num) {
		D_ERROR("key enumerate get %d > %d more kds, %d\n",
			oeo->oeo_num, *enum_args->eaa_nr, -DER_PROTO);
		D_GOTO(out, rc = -DER_PROTO);
	}

	*enum_args->eaa_nr = oeo->oeo_num;

	if (enum_args->eaa_kds && oeo->oeo_kds.ca_count > 0)
		memcpy(enum_args->eaa_kds, oeo->oeo_kds.ca_arrays,
		       sizeof(*enum_args->eaa_kds) *
		       oeo->oeo_kds.ca_count);

	if (enum_args->eaa_eprs && oeo->oeo_eprs.ca_count > 0) {
		D_ASSERT(*enum_args->eaa_nr >= oeo->oeo_eprs.ca_count);
		memcpy(enum_args->eaa_eprs, oeo->oeo_eprs.ca_arrays,
		       sizeof(*enum_args->eaa_eprs) *
		       oeo->oeo_eprs.ca_count);
	}

	if (enum_args->eaa_recxs && oeo->oeo_recxs.ca_count > 0) {
		D_ASSERT(*enum_args->eaa_nr >= oeo->oeo_recxs.ca_count);
		memcpy(enum_args->eaa_recxs, oeo->oeo_recxs.ca_arrays,
		       sizeof(*enum_args->eaa_recxs) *
		       oeo->oeo_recxs.ca_count);
	}

	if (enum_args->eaa_sgl && oeo->oeo_sgl.sg_nr > 0) {
		rc = daos_sgl_copy_data_out(enum_args->eaa_sgl, &oeo->oeo_sgl);
		if (rc)
			D_GOTO(out, rc);
	}

	/* Update dkey hash and tag */
	if (enum_args->eaa_dkey_anchor) {
		enum_anchor_copy(enum_args->eaa_dkey_anchor,
				 &oeo->oeo_dkey_anchor);
		tgt_tag = oeo->oeo_dkey_anchor.da_tag;
		enum_args->eaa_dkey_anchor->da_tag = tgt_tag;
	}

	if (enum_args->eaa_akey_anchor)
		enum_anchor_copy(enum_args->eaa_akey_anchor,
				 &oeo->oeo_akey_anchor);

	if (enum_args->eaa_anchor)
		enum_anchor_copy(enum_args->eaa_anchor,
				 &oeo->oeo_anchor);
out:
	if (enum_args->eaa_obj != NULL)
		obj_shard_decref(enum_args->eaa_obj);

	if (oei->oei_bulk != NULL)
		crt_bulk_free(oei->oei_bulk);
	if (oei->oei_kds_bulk != NULL)
		crt_bulk_free(oei->oei_kds_bulk);
	crt_req_decref(enum_args->rpc);
	dc_pool_put((struct dc_pool *)enum_args->hdlp);

	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

#define KDS_BULK_LIMIT	128

int
dc_obj_shard_list(struct dc_obj_shard *obj_shard, unsigned int opc,
		  daos_epoch_t epoch, daos_key_t *dkey, daos_key_t *akey,
		  daos_iod_type_t type, daos_size_t *size, uint32_t *nr,
		  daos_key_desc_t *kds, daos_sg_list_t *sgl,
		  daos_recx_t *recxs, daos_epoch_range_t *eprs,
		  daos_anchor_t *anchor, daos_anchor_t *dkey_anchor,
		  daos_anchor_t *akey_anchor, unsigned int *map_ver,
		  tse_task_t *task)
{
	crt_endpoint_t		tgt_ep;
	struct dc_pool	       *pool;
	crt_rpc_t	       *req;
	uuid_t			cont_hdl_uuid;
	uuid_t			cont_uuid;
	struct obj_key_enum_in	*oei;
	struct obj_enum_args	enum_args;
	uint64_t		dkey_hash;
	daos_size_t		sgl_len = 0;
	int			rc;

	D_ASSERT(obj_shard != NULL);
	obj_shard_addref(obj_shard);

	rc = dc_cont_hdl2uuid(obj_shard->do_co_hdl, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_put, rc);

	pool = obj_shard_ptr2pool(obj_shard);
	if (pool == NULL)
		D_GOTO(out_put, rc);

	tgt_ep.ep_grp = pool->dp_group;
	if (dkey == NULL) {
		tgt_ep.ep_tag = dkey_anchor->da_tag;
	} else {
		tse_task_stack_pop_data(task, &dkey_hash, sizeof(dkey_hash));
		tgt_ep.ep_tag = obj_shard_dkeyhash2tag(obj_shard, dkey_hash);
	}
	tgt_ep.ep_rank = obj_shard_rank_get(obj_shard, tgt_ep.ep_tag, opc);
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out_pool, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_IO, "opc %d "DF_UOID" rank %d tag %d\n",
		opc, DP_UOID(obj_shard->do_id), tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out_pool, rc);

	oei = crt_req_get(req);
	D_ASSERT(oei != NULL);

	if (dkey != NULL)
		oei->oei_dkey = *dkey;
	if (akey != NULL)
		oei->oei_akey = *akey;
	oei->oei_oid		= obj_shard->do_id;
	oei->oei_map_ver	= *map_ver;
	oei->oei_epoch		= epoch;
	oei->oei_nr		= *nr;
	oei->oei_rec_type	= type;
	uuid_copy(oei->oei_co_hdl, cont_hdl_uuid);
	uuid_copy(oei->oei_co_uuid, cont_uuid);

	if (anchor != NULL)
		enum_anchor_copy(&oei->oei_anchor, anchor);
	if (dkey_anchor != NULL)
		enum_anchor_copy(&oei->oei_dkey_anchor, dkey_anchor);
	if (akey_anchor != NULL)
		enum_anchor_copy(&oei->oei_akey_anchor, akey_anchor);

	if (sgl != NULL) {
		oei->oei_sgl = *sgl;
		sgl_len = daos_sgls_buf_len(sgl, 1);
		if (sgl_len >= OBJ_BULK_LIMIT) {
			/* Create bulk */
			rc = crt_bulk_create(daos_task2ctx(task),
					     daos2crt_sg(sgl), CRT_BULK_RW,
					     &oei->oei_bulk);
			if (rc < 0)
				D_GOTO(out_req, rc);
		}
	}

	if (*nr > KDS_BULK_LIMIT) {
		daos_sg_list_t	tmp_sgl = { 0 };
		d_iov_t		tmp_iov = { 0 };

		tmp_iov.iov_buf_len = sizeof(*kds) * (*nr);
		tmp_iov.iov_buf = kds;
		tmp_sgl.sg_nr_out = 1;
		tmp_sgl.sg_nr = 1;
		tmp_sgl.sg_iovs = &tmp_iov;

		rc = crt_bulk_create(daos_task2ctx(task),
				     daos2crt_sg(&tmp_sgl), CRT_BULK_RW,
				     &oei->oei_kds_bulk);
		if (rc < 0)
			D_GOTO(out_req, rc);
	}

	crt_req_addref(req);
	enum_args.rpc = req;
	enum_args.hdlp = (daos_handle_t *)pool;
	enum_args.eaa_nr = nr;
	enum_args.eaa_kds = kds;
	enum_args.eaa_anchor = anchor;
	enum_args.eaa_dkey_anchor = dkey_anchor;
	enum_args.eaa_akey_anchor = akey_anchor;
	enum_args.eaa_obj = obj_shard;
	enum_args.eaa_size = size;
	enum_args.eaa_sgl = sgl;
	enum_args.eaa_map_ver = map_ver;
	enum_args.eaa_recxs = recxs;
	enum_args.eaa_eprs = eprs;
	rc = tse_task_register_comp_cb(task, dc_enumerate_cb, &enum_args,
				       sizeof(enum_args));
	if (rc != 0)
		D_GOTO(out_eaa, rc);

	rc = daos_rpc_send(req, task);
	if (rc != 0) {
		D_ERROR("enumerate rpc failed rc %d\n", rc);
		D_GOTO(out_eaa, rc);
	}

	return rc;

out_eaa:
	crt_req_decref(req);
	if (sgl != NULL && sgl_len >= OBJ_BULK_LIMIT)
		crt_bulk_free(oei->oei_bulk);
out_req:
	crt_req_decref(req);
out_pool:
	dc_pool_put(pool);
out_put:
	obj_shard_decref(obj_shard);
	tse_task_complete(task, rc);
	return rc;
}

struct obj_key_query_cb_args {
	crt_rpc_t	*rpc;
	unsigned int	*map_ver;
	daos_unit_oid_t	oid;
	uint32_t	flags;
	daos_key_t	*dkey;
	daos_key_t	*akey;
	daos_recx_t	*recx;
};

static int
obj_shard_key_query_cb(tse_task_t *task, void *data)
{
	struct obj_key_query_cb_args	*cb_args;
	struct obj_key_query_in		*okqi;
	struct obj_key_query_out	*okqo;
	uint32_t			flags;
	int				ret = task->dt_result;
	int				rc = 0;
	crt_rpc_t			*rpc;

	cb_args = (struct obj_key_query_cb_args *)data;
	rpc = cb_args->rpc;

	okqi = crt_req_get(cb_args->rpc);
	D_ASSERT(okqi != NULL);

	flags = okqi->okqi_flags;

	if (ret != 0) {
		D_ERROR("RPC %d failed: %d\n",
			opc_get(cb_args->rpc->cr_opc), ret);
		D_GOTO(out, ret);
	}

	okqo = crt_reply_get(cb_args->rpc);
	rc = obj_reply_get_status(rpc);
	if (rc != 0) {
		D_ERROR("rpc %p RPC %d failed: %d\n", cb_args->rpc,
			 opc_get(cb_args->rpc->cr_opc), rc);
		D_GOTO(out, rc);
	}
	*cb_args->map_ver = obj_reply_map_version_get(rpc);

	bool check = true;
	bool changed = false;
	bool first = (cb_args->dkey->iov_len == 0);

	if (flags & DAOS_GET_DKEY) {
		uint64_t *val = (uint64_t *)okqo->okqo_dkey.iov_buf;
		uint64_t *cur = (uint64_t *)cb_args->dkey->iov_buf;

		if (okqo->okqo_dkey.iov_len != sizeof(uint64_t)) {
			D_ERROR("Invalid Dkey obtained\n");
			D_GOTO(out, rc = -DER_IO);
		}

		/** for first cb, just set the dkey */
		if (first) {
			*cur = *val;
			cb_args->dkey->iov_len = okqo->okqo_dkey.iov_len;
		} else if (flags & DAOS_GET_MAX) {
			if (*val > *cur) {
				*cur = *val;
				/** set to change akey and recx */
				changed = true;
			} else {
				/** no change, don't check akey and recx */
				check = false;
			}
		} else if (flags & DAOS_GET_MIN) {
			if (*val < *cur) {
				*cur = *val;
				/** set to change akey and recx */
				changed = true;
			} else {
				/** no change, don't check akey and recx */
				check = false;
			}
		} else {
			D_ASSERT(0);
		}
	}

	if (check && flags & DAOS_GET_AKEY) {
		uint64_t *val = (uint64_t *)okqo->okqo_akey.iov_buf;
		uint64_t *cur = (uint64_t *)cb_args->akey->iov_buf;

		/** if first cb, or dkey changed, set akey */
		if (first || changed)
			*cur = *val;
		else
			D_ASSERT(0);
	}

	if (check && flags & DAOS_GET_RECX) {
		/** if first cb, set recx */
		if (first || changed) {
			cb_args->recx->rx_nr = okqo->okqo_recx.rx_nr;
			cb_args->recx->rx_idx = okqo->okqo_recx.rx_idx;
		} else {
			D_ASSERT(0);
		}
	}

out:
	crt_req_decref(rpc);
	if (ret == 0 || obj_retry_error(rc))
		ret = rc;
	return ret;
}

int
dc_obj_shard_key_query(struct dc_obj_shard *shard, daos_epoch_t epoch,
		       uint32_t flags, daos_key_t *dkey, daos_key_t *akey,
		       daos_recx_t *recx, const uuid_t coh_uuid,
		       const uuid_t cont_uuid, unsigned int *map_ver,
		       tse_task_t *task)
{
	struct dc_pool			*pool;
	struct obj_key_query_in		*okqi;
	crt_rpc_t			*req;
	struct obj_key_query_cb_args	 cb_args;
	daos_unit_oid_t			 oid;
	crt_endpoint_t			 tgt_ep;
	uint64_t			 dkey_hash;
	int				 rc;

	tse_task_stack_pop_data(task, &dkey_hash, sizeof(dkey_hash));

	pool = obj_shard_ptr2pool(shard);
	if (pool == NULL)
		D_GOTO(out, rc = -DER_NO_HDL);

	oid = shard->do_id;
	tgt_ep.ep_grp	= pool->dp_group;
	tgt_ep.ep_tag	= obj_shard_dkeyhash2tag(shard, dkey_hash);
	tgt_ep.ep_rank = obj_shard_rank_get(shard, tgt_ep.ep_tag,
					    DAOS_OBJ_RPC_KEY_QUERY);
	dc_pool_put(pool);
	if ((int)tgt_ep.ep_rank < 0)
		D_GOTO(out, rc = (int)tgt_ep.ep_rank);

	D_DEBUG(DB_IO, "OBJ_KEY_QUERY_RPC, rank=%d tag=%d.\n",
		tgt_ep.ep_rank, tgt_ep.ep_tag);

	rc = obj_req_create(daos_task2ctx(task), &tgt_ep,
			    DAOS_OBJ_RPC_KEY_QUERY, &req);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_req_addref(req);
	cb_args.rpc	= req;
	cb_args.map_ver = map_ver;
	cb_args.oid	= shard->do_id;
	cb_args.flags	= flags;
	cb_args.dkey	= dkey;
	cb_args.akey	= akey;
	cb_args.recx	= recx;

	rc = tse_task_register_comp_cb(task, obj_shard_key_query_cb, &cb_args,
				       sizeof(cb_args));
	if (rc != 0)
		D_GOTO(out_req, rc);

	okqi = crt_req_get(req);
	D_ASSERT(okqi != NULL);

	okqi->okqi_map_ver		= *map_ver;
	okqi->okqi_epoch		= epoch;
	okqi->okqi_flags		= flags;
	okqi->okqi_oid			= oid;
	if (dkey != NULL)
		okqi->okqi_dkey		= *dkey;
	if (akey != NULL)
		okqi->okqi_akey		= *akey;
	uuid_copy(okqi->okqi_co_hdl, coh_uuid);
	uuid_copy(okqi->okqi_co_uuid, cont_uuid);

	rc = daos_rpc_send(req, task);
	if (rc != 0) {
		D_ERROR("key_query rpc failed rc %d\n", rc);
		D_GOTO(out_req, rc);
	}
	return rc;

out_req:
	crt_req_decref(req);
out:
	tse_task_complete(task, rc);
	return rc;
}
