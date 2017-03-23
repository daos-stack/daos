/**
 * (C) Copyright 2016 Intel Corporation.
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
#define DD_SUBSYS	DD_FAC(object)

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
obj_shard_alloc(daos_rank_t rank, daos_unit_oid_t id, uint32_t part_nr)
{
	struct dc_obj_shard *shard;

	D_ALLOC_PTR(shard);
	if (shard == NULL)
		return NULL;

	shard->do_rank	  = rank;
	shard->do_part_nr = part_nr;
	shard->do_id	  = id;
	DAOS_INIT_LIST_HEAD(&shard->do_co_list);

	return shard;
}

static void
obj_shard_decref(struct dc_obj_shard *shard)
{
	D_ASSERT(shard->do_ref > 0);
	shard->do_ref--;
	if (shard->do_ref == 0)
		obj_shard_free(shard);
}

static void
obj_shard_addref(struct dc_obj_shard *shard)
{
	shard->do_ref++;
}

static void
obj_shard_hdl_link(struct dc_obj_shard *shard)
{
	obj_shard_addref(shard);
}

static void
obj_shard_hdl_unlink(struct dc_obj_shard *shard)
{
	obj_shard_decref(shard);
}

struct dc_obj_shard*
obj_shard_hdl2ptr(daos_handle_t hdl)
{
	struct dc_obj_shard *shard;

	if (hdl.cookie == 0)
		return NULL;

	shard = (struct dc_obj_shard *)hdl.cookie;
	obj_shard_addref(shard);
	return shard;
}

static daos_handle_t
obj_shard_ptr2hdl(struct dc_obj_shard *shard)
{
	daos_handle_t oh;

	oh.cookie = (uint64_t)shard;
	return oh;
}

int
dc_obj_shard_open(daos_handle_t coh, uint32_t tgt, daos_unit_oid_t id,
		  unsigned int mode, daos_handle_t *oh)
{
	struct dc_obj_shard	*dobj;
	struct pool_target	*map_tgt;
	int			rc;

	rc = dc_cont_tgt_idx2ptr(coh, tgt, &map_tgt);
	if (rc != 0)
		return rc;

	dobj = obj_shard_alloc(map_tgt->ta_comp.co_rank, id,
			       map_tgt->ta_comp.co_nr);
	if (dobj == NULL)
		return -DER_NOMEM;

	dobj->do_co_hdl = coh;
	obj_shard_hdl_link(dobj);
	*oh = obj_shard_ptr2hdl(dobj);

	return 0;
}

int
dc_obj_shard_close(daos_handle_t oh)
{
	struct dc_obj_shard *dobj;

	dobj = obj_shard_hdl2ptr(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	obj_shard_hdl_unlink(dobj);
	obj_shard_decref(dobj);
	return 0;
}

static void
obj_shard_rw_bulk_fini(crt_rpc_t *rpc)
{
	struct obj_rw_in	*orw;
	crt_bulk_t		*bulks;
	unsigned int		nr;
	int			i;

	orw = crt_req_get(rpc);
	bulks = orw->orw_bulks.da_arrays;
	if (bulks == NULL)
		return;

	nr = orw->orw_bulks.da_count;
	for (i = 0; i < nr; i++)
		crt_bulk_free(bulks[i]);

	D_FREE(bulks, nr * sizeof(crt_bulk_t));
	orw->orw_bulks.da_arrays = NULL;
	orw->orw_bulks.da_count = 0;
}

static int
dc_obj_shard_sgl_copy(daos_sg_list_t *dst_sgl, uint32_t dst_nr,
		      daos_sg_list_t *src_sgl, uint32_t src_nr)
{
	int i;
	int j;

	if (src_nr > dst_nr) {
		D_ERROR("%u > %u\n", src_nr, dst_nr);
		return -DER_INVAL;
	}

	for (i = 0; i < src_nr; i++) {
		if (src_sgl[i].sg_nr.num == 0)
			continue;

		if (src_sgl[i].sg_nr.num > dst_sgl[i].sg_nr.num) {
			D_ERROR("%d : %u > %u\n", i,
				src_sgl[i].sg_nr.num, dst_sgl[i].sg_nr.num);
			return -DER_INVAL;
		}

		dst_sgl[i].sg_nr.num_out = src_sgl[i].sg_nr.num_out;
		for (j = 0; j < src_sgl[i].sg_nr.num_out; j++) {
			if (src_sgl[i].sg_iovs[j].iov_len == 0)
				continue;

			if (src_sgl[i].sg_iovs[j].iov_len >
			    dst_sgl[i].sg_iovs[j].iov_buf_len) {
				D_ERROR("%d:%d "DF_U64" > "DF_U64"\n",
					i, j, src_sgl[i].sg_iovs[j].iov_len,
					src_sgl[i].sg_iovs[j].iov_buf_len);
				return -DER_INVAL;
			}
			memcpy(dst_sgl[i].sg_iovs[j].iov_buf,
			       src_sgl[i].sg_iovs[j].iov_buf,
			       src_sgl[i].sg_iovs[j].iov_len);
			dst_sgl[i].sg_iovs[j].iov_len =
				src_sgl[i].sg_iovs[j].iov_len;
		}
	}
	return 0;
}

struct rw_args_t {
	crt_rpc_t	*rpc;
	daos_handle_t	*hdlp;
	daos_sg_list_t	*rwaa_sgls;
	uint32_t	rwaa_nr;
};

static int
dc_rw_cb(struct daos_task *task, void *arg)
{
	struct rw_args_t	*rw_args = (struct rw_args_t *)arg;
	struct obj_rw_in	*orw;
	int			opc;
	int                     ret = task->dt_result;
	int			rc = 0;

	opc = opc_get(rw_args->rpc->cr_opc);
	if ((opc == DAOS_OBJ_RPC_FETCH &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_FETCH_TIMEOUT)) ||
	    (opc == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_UPDATE_TIMEOUT)))
		D_GOTO(out, rc = -DER_TIMEDOUT);

	if (opc == DAOS_OBJ_RPC_UPDATE &&
	    DAOS_FAIL_CHECK(DAOS_OBJ_UPDATE_NOSPACE))
		D_GOTO(out, rc = -DER_NOSPACE);

	orw = crt_req_get(rw_args->rpc);
	D_ASSERT(orw != NULL);
	if (ret != 0) {
		/* If any failure happens inside Cart, let's reset
		 * failure to TIMEDOUT, so the upper layer can retry
		 **/
		D_ERROR("RPC %d failed: %d\n",
			opc_get(rw_args->rpc->cr_opc), ret);
		D_GOTO(out, ret);
	}

	rc = obj_reply_get_status(rw_args->rpc);
	if (rc != 0)
		D_GOTO(out, rc);

	if (opc_get(rw_args->rpc->cr_opc) == DAOS_OBJ_RPC_FETCH) {
		struct obj_rw_out *orwo;
		daos_iod_t	*iods;
		uint64_t	*sizes;
		int		j;
		int		k;
		int		idx = 0;

		orwo = crt_reply_get(rw_args->rpc);
		iods = orw->orw_iods.da_arrays;
		sizes = orwo->orw_sizes.da_arrays;

		/* update the sizes in iods */
		for (j = 0; j < orw->orw_nr; j++) {
			for (k = 0; k < iods[j].iod_nr; k++) {
				if (idx == orwo->orw_sizes.da_count) {
					D_ERROR("Invalid return size %d\n",
						idx);
					D_GOTO(out, rc = -DER_PROTO);
				}
				iods[j].iod_recxs[k].rx_rsize = sizes[idx];
				idx++;
			}
		}

		if (orwo->orw_sgls.da_count > 0) {
			/* inline transfer */
			rc = dc_obj_shard_sgl_copy(rw_args->rwaa_sgls,
						   rw_args->rwaa_nr,
						   orwo->orw_sgls.da_arrays,
						   orwo->orw_sgls.da_count);
		} else if (rw_args->rwaa_sgls != NULL) {
			/* for bulk transfer it needs to update sg_nr.num_out */
			daos_sg_list_t	*sgls = rw_args->rwaa_sgls;
			uint32_t	*nrs;
			uint32_t	nrs_count;
			int		i;

			nrs = orwo->orw_nrs.da_arrays;
			nrs_count = orwo->orw_nrs.da_count;
			if (nrs_count != rw_args->rwaa_nr) {
				D_ERROR("Invalid nrs %u != %u\n", nrs_count,
					rw_args->rwaa_nr);
				D_GOTO(out, rc = -DER_PROTO);
			}

			/* update sgl_nr */
			for (i = 0; i < nrs_count; i++)
				sgls[i].sg_nr.num_out = nrs[i];
		}
	}
out:
	obj_shard_rw_bulk_fini(rw_args->rpc);
	crt_req_decref(rw_args->rpc);
	dc_pool_put((struct dc_pool *)rw_args->hdlp);

	D_FREE_PTR(rw_args);

	if (ret == 0 || daos_obj_retry_error(rc))
		ret = rc;
	return ret;
}

static inline bool
obj_shard_io_check(unsigned int nr, daos_iod_t *iods)
{
	int i;

	for (i = 0; i < nr; i++) {
		if (iods[i].iod_name.iov_buf == NULL ||
		    iods[i].iod_recxs == NULL)
			/* XXX checksum & eprs should not be mandatory */
			return false;
	}

	return true;
}

/**
 * XXX: Only use dkey to distribute the data among targets for
 * now, and eventually, it should use dkey + akey, but then
 * it means the I/O descriptor might needs to be split into
 * mulitple requests in obj_shard_rw()
 */
static uint32_t
obj_shard_dkey2tag(struct dc_obj_shard *dobj, daos_key_t *dkey)
{
	uint64_t hash;

	/** XXX hash is calculated twice (see cli_obj_dkey2shard) */
	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);
	hash %= dobj->do_part_nr;

	return hash;
}

static uint64_t
iods_data_len(daos_iod_t *iods, int nr)
{
	uint64_t iod_length = 0;
	int	 i;

	for (i = 0; i < nr; i++) {
		uint64_t len = daos_iod_len(&iods[i]);

		if (len == -1) /* unknown */
			return 0;

		iod_length += len;
	}
	return iod_length;
}

static daos_size_t
sgls_buf_len(daos_sg_list_t *sgls, int nr)
{
	daos_size_t sgls_len = 0;
	int	    i;

	if (sgls == NULL)
		return 0;

	/* create bulk transfer for daos_sg_list */
	for (i = 0; i < nr; i++)
		sgls_len += daos_sgl_buf_len(&sgls[i]);

	return sgls_len;
}

static int
obj_shard_rw_bulk_prep(crt_rpc_t *rpc, unsigned int nr, daos_sg_list_t *sgls,
		       struct daos_task *task)
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
	orw->orw_bulks.da_count = nr;
	orw->orw_bulks.da_arrays = bulks;
out:
	if (rc != 0 && bulks != NULL)
		D_FREE(bulks, nr * sizeof(*bulks));

	return rc;
}

static struct dc_pool *
obj_shard_ptr2pool(struct dc_obj_shard *shard)
{
	daos_handle_t poh;

	poh = dc_cont_hdl2pool_hdl(shard->do_co_hdl);
	if (daos_handle_is_inval(poh))
		return NULL;

	return dc_pool_lookup(poh);
}

static int
obj_shard_rw(daos_handle_t oh, enum obj_rpc_opc opc, daos_epoch_t epoch,
	     daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
	     daos_sg_list_t *sgls, unsigned int map_ver, struct daos_task *task)
{
	struct dc_obj_shard	*dobj;
	struct dc_pool		*pool;
	crt_rpc_t		*req;
	struct obj_rw_in	*orw;
	struct rw_args_t	*rw_args = NULL;
	crt_endpoint_t		tgt_ep;
	uuid_t			cont_hdl_uuid;
	uuid_t			cont_uuid;
	daos_size_t		total_len;
	int			rc;

	/** sanity check input parameters */
	if (dkey == NULL || dkey->iov_buf == NULL || nr == 0 ||
	    !obj_shard_io_check(nr, iods))
		D_GOTO(out_task, rc = -DER_INVAL);

	dobj = obj_shard_hdl2ptr(oh);
	if (dobj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = dc_cont_hdl2uuid(dobj->do_co_hdl, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0) {
		obj_shard_decref(dobj);
		D_GOTO(out_task, rc);
	}

	pool = obj_shard_ptr2pool(dobj);
	if (pool == NULL) {
		obj_shard_decref(dobj);
		D_GOTO(out_task, rc);
	}

	tgt_ep.ep_grp = pool->dp_group;
	tgt_ep.ep_rank = dobj->do_rank;
	tgt_ep.ep_tag = obj_shard_dkey2tag(dobj, dkey);
	rc = obj_req_create(daos_task2ctx(task), tgt_ep, opc, &req);
	if (rc != 0) {
		obj_shard_decref(dobj);
		D_GOTO(out_pool, rc);
	}

	orw = crt_req_get(req);
	D_ASSERT(orw != NULL);

	orw->orw_map_ver = map_ver;
	orw->orw_oid = dobj->do_id;
	uuid_copy(orw->orw_co_hdl, cont_hdl_uuid);
	uuid_copy(orw->orw_co_uuid, cont_uuid);

	obj_shard_decref(dobj);
	dobj = NULL;

	orw->orw_epoch = epoch;
	orw->orw_nr = nr;
	/** FIXME: large dkey should be transferred via bulk */
	orw->orw_dkey = *dkey;

	/* FIXME: if iods is too long, then we needs to do bulk transfer
	 * as well, but then we also needs to serialize the iods
	 **/
	orw->orw_iods.da_count = nr;
	orw->orw_iods.da_arrays = iods;

	total_len = iods_data_len(iods, nr);
	/* If it is read, let's try to get the size from sg list */
	if (total_len == 0 && opc == DAOS_OBJ_RPC_FETCH)
		total_len = sgls_buf_len(sgls, nr);

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_FAIL))
		D_GOTO(out_req, rc = -DER_INVAL);

	if (total_len >= OBJ_BULK_LIMIT) {
		/* Transfer data by bulk */
		rc = obj_shard_rw_bulk_prep(req, nr, sgls, task);
		if (rc != 0)
			D_GOTO(out_req, rc);
		orw->orw_sgls.da_count = 0;
		orw->orw_sgls.da_arrays = NULL;
	} else {
		/* Transfer data inline */
		if (sgls != NULL)
			orw->orw_sgls.da_count = nr;
		else
			orw->orw_sgls.da_count = 0;
		orw->orw_sgls.da_arrays = sgls;
		orw->orw_bulks.da_count = 0;
		orw->orw_bulks.da_arrays = NULL;
	}

	D_ALLOC_PTR(rw_args);
	if (rw_args == NULL)
		D_GOTO(out_bulk, rc = -DER_NOMEM);
	crt_req_addref(req);
	rw_args->rpc = req;
	rw_args->hdlp = (daos_handle_t *)pool;

	if (opc == DAOS_OBJ_RPC_FETCH) {
		/* remember the sgl to copyout the data inline for fetch */
		rw_args->rwaa_nr = nr;
		rw_args->rwaa_sgls = sgls;
	}

	if (DAOS_FAIL_CHECK(DAOS_SHARD_OBJ_RW_CRT_ERROR))
		D_GOTO(out_args, rc = -DER_CRT_HG);

	rc = daos_task_register_comp_cb(task, dc_rw_cb, rw_args);
	if (rc != 0)
		D_GOTO(out_args, rc);

	rc = daos_rpc_send(req, task);
	if (rc != 0) {
		D_ERROR("update/fetch rpc failed rc %d\n", rc);
		D_GOTO(out_args, rc);
	}

	return rc;

out_args:
	D_FREE_PTR(rw_args);
	crt_req_decref(req);
out_bulk:
	if (total_len >= OBJ_BULK_LIMIT)
		obj_shard_rw_bulk_fini(req);
out_req:
	crt_req_decref(req);
out_pool:
	dc_pool_put(pool);
out_task:
	daos_task_complete(task, rc);
	return rc;
}

int
dc_obj_shard_update(daos_handle_t oh, daos_epoch_t epoch,
		    daos_key_t *dkey, unsigned int nr,
		    daos_iod_t *iods, daos_sg_list_t *sgls,
		    unsigned int map_ver, struct daos_task *task)
{
	return obj_shard_rw(oh, DAOS_OBJ_RPC_UPDATE, epoch, dkey, nr, iods,
			    sgls, map_ver, task);
}

int
dc_obj_shard_fetch(daos_handle_t oh, daos_epoch_t epoch,
		   daos_key_t *dkey, unsigned int nr,
		   daos_iod_t *iods, daos_sg_list_t *sgls,
		   daos_iom_t *maps, unsigned int map_ver,
		   struct daos_task *task)
{
	return obj_shard_rw(oh, DAOS_OBJ_RPC_FETCH, epoch, dkey, nr, iods,
			    sgls, map_ver, task);
}

struct enum_args_t {
	crt_rpc_t		*rpc;
	daos_handle_t		*hdlp;
	uint32_t		*eaa_nr;
	daos_key_desc_t		*eaa_kds;
	daos_hash_out_t		*eaa_anchor;
	struct dc_obj_shard	*eaa_obj;
	daos_sg_list_t		*eaa_sgl;
};

static int
dc_enumerate_cb(struct daos_task *task, void *arg)
{
	struct enum_args_t	*enum_args = (struct enum_args_t *)arg;
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	int			tgt_tag;
	int			ret = task->dt_result;
	int			rc = 0;

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
	if (oeo->oeo_ret < 0)
		D_GOTO(out, rc = oeo->oeo_ret);

	if (*enum_args->eaa_nr < oeo->oeo_kds.da_count) {
		D_ERROR("DAOS_OBJ_RPC_ENUMERATE return more kds, rc: %d\n",
			-DER_PROTO);
		D_GOTO(out, rc = -DER_PROTO);
	}

	*(enum_args->eaa_nr) = oeo->oeo_kds.da_count;
	memcpy(enum_args->eaa_kds, oeo->oeo_kds.da_arrays,
	       sizeof(*enum_args->eaa_kds) * oeo->oeo_kds.da_count);

	enum_anchor_copy_hkey(enum_args->eaa_anchor, &oeo->oeo_anchor);
	if (daos_hash_is_eof(&oeo->oeo_anchor) &&
	    opc_get(enum_args->rpc->cr_opc) == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		tgt_tag = enum_anchor_get_tag(enum_args->eaa_anchor);
		if (tgt_tag < enum_args->eaa_obj->do_part_nr - 1) {
			enum_anchor_reset_hkey(enum_args->eaa_anchor);
			enum_anchor_set_tag(enum_args->eaa_anchor, ++tgt_tag);
		}
	}

	if (oeo->oeo_sgl.sg_nr.num > 0 && oeo->oeo_sgl.sg_iovs != NULL)
		rc = dc_obj_shard_sgl_copy(enum_args->eaa_sgl, 1, &oeo->oeo_sgl,
					   1);

out:
	if (enum_args->eaa_obj != NULL)
		obj_shard_decref(enum_args->eaa_obj);

	if (oei->oei_bulk != NULL)
		crt_bulk_free(oei->oei_bulk);

	crt_req_decref(enum_args->rpc);
	dc_pool_put((struct dc_pool *)enum_args->hdlp);

	D_FREE_PTR(enum_args);

	if (ret == 0 || daos_obj_retry_error(rc))
		ret = rc;
	return ret;
}

int
dc_obj_shard_list_key(daos_handle_t oh, enum obj_rpc_opc opc,
		      daos_epoch_t epoch, daos_key_t *key, uint32_t *nr,
		      daos_key_desc_t *kds, daos_sg_list_t *sgl,
		      daos_hash_out_t *anchor, unsigned int map_ver,
		      struct daos_task *task)
{
	crt_endpoint_t		tgt_ep;
	struct dc_pool		*pool;
	crt_rpc_t		*req;
	struct dc_obj_shard	*dobj;
	uuid_t			cont_hdl_uuid;
	uuid_t			cont_uuid;
	struct obj_key_enum_in	*oei;
	struct enum_args_t	*enum_args = NULL;
	daos_size_t		sgl_len;
	int			rc;

	dobj = obj_shard_hdl2ptr(oh);
	if (dobj == NULL)
		D_GOTO(out_task, rc = -DER_NO_HDL);

	rc = dc_cont_hdl2uuid(dobj->do_co_hdl, &cont_hdl_uuid, &cont_uuid);
	if (rc != 0)
		D_GOTO(out_put, rc);

	pool = obj_shard_ptr2pool(dobj);
	if (pool == NULL)
		D_GOTO(out_put, rc);

	tgt_ep.ep_grp = pool->dp_group;
	tgt_ep.ep_rank = dobj->do_rank;
	if (opc == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		D_ASSERT(key != NULL);
		tgt_ep.ep_tag = obj_shard_dkey2tag(dobj, key);
	} else {
		tgt_ep.ep_tag = enum_anchor_get_tag(anchor);
	}

	rc = obj_req_create(daos_task2ctx(task), tgt_ep, opc, &req);
	if (rc != 0)
		D_GOTO(out_pool, rc);

	oei = crt_req_get(req);
	if (key != NULL)
		oei->oei_key = *key;
	else
		memset(&oei->oei_key, 0, sizeof(oei->oei_key));

	D_ASSERT(oei != NULL);
	oei->oei_oid = dobj->do_id;
	uuid_copy(oei->oei_co_hdl, cont_hdl_uuid);
	uuid_copy(oei->oei_co_uuid, cont_uuid);

	oei->oei_map_ver = map_ver;
	oei->oei_epoch = epoch;
	oei->oei_nr = *nr;

	enum_anchor_copy_hkey(&oei->oei_anchor, anchor);
	oei->oei_sgl = *sgl;
	sgl_len = sgls_buf_len(sgl, 1);
	if (sgl_len >= OBJ_BULK_LIMIT) {
		/* Create bulk */
		rc = crt_bulk_create(daos_task2ctx(task), daos2crt_sg(sgl),
				     CRT_BULK_RW, &oei->oei_bulk);
		if (rc < 0)
			D_GOTO(out_req, rc);
	}

	D_ALLOC_PTR(enum_args);
	if (enum_args == NULL)
		D_GOTO(out_bulk, rc = -DER_NOMEM);
	crt_req_addref(req);
	enum_args->rpc = req;
	enum_args->hdlp = (daos_handle_t *)pool;
	enum_args->eaa_nr = nr;
	enum_args->eaa_kds = kds;
	enum_args->eaa_anchor = anchor;
	enum_args->eaa_obj = dobj;
	enum_args->eaa_sgl = sgl;

	rc = daos_task_register_comp_cb(task, dc_enumerate_cb, enum_args);
	if (rc != 0)
		D_GOTO(out_eaa, rc);

	rc = daos_rpc_send(req, task);
	if (rc != 0) {
		D_ERROR("enumerate rpc failed rc %d\n", rc);
		D_GOTO(out_eaa, rc);
	}

	return rc;

out_eaa:
	D_FREE_PTR(enum_args);
	crt_req_decref(req);
out_bulk:
	if (sgl_len >= OBJ_BULK_LIMIT)
		crt_bulk_free(oei->oei_bulk);
out_req:
	crt_req_decref(req);
out_pool:
	dc_pool_put(pool);
out_put:
	obj_shard_decref(dobj);
out_task:
	daos_task_complete(task, rc);
	return rc;
}
