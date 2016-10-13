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

#include <daos/container.h>
#include <daos/pool_map.h>
#include <daos/transport.h>
#include <daos/daos_m.h>
#include "obj_rpc.h"
#include "obj_internal.h"

static void
obj_shard_free(struct dc_obj_shard *shard)
{
	D_FREE_PTR(shard);
}

static struct dc_obj_shard *
obj_shard_alloc(daos_rank_t rank, daos_unit_oid_t id, uint32_t nr_srv)
{
	struct dc_obj_shard *shard;

	D_ALLOC_PTR(shard);
	if (shard == NULL)
		return NULL;

	shard->do_rank	= rank;
	shard->do_nr_srv = nr_srv;
	shard->do_id	= id;
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

static struct dc_obj_shard*
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
		  unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	struct dc_obj_shard	*dobj;
	struct pool_target	*map_tgt;
	int			rc;

	rc = dc_cont_tgt_idx2pool_tgt(coh, &map_tgt, tgt);
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
dc_obj_shard_close(daos_handle_t oh, daos_event_t *ev)
{
	struct dc_obj_shard *dobj;

	dobj = obj_shard_hdl2ptr(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	obj_shard_hdl_unlink(dobj);
	obj_shard_decref(dobj);
	return 0;
}

static int
obj_rw_cp(struct daos_task *task, int rc)
{
	struct daos_op_sp *sp = daos_task2sp(task);
	struct obj_update_in	*oui;
	dtp_bulk_t		*bulks;
	int			i;
	int			ret;

	oui = dtp_req_get(sp->sp_rpc);
	D_ASSERT(oui != NULL);
	bulks = oui->oui_bulks.da_arrays;
	if (rc) {
		D_ERROR("RPC error: %d\n", rc);
		D_GOTO(out, rc);
	}

	ret = obj_reply_get_status(sp->sp_rpc);
	if (ret != 0) {
		D_ERROR("DAOS_OBJ_RPC_UPDATE/FETCH replied failed, rc: %d\n",
			ret);
		D_GOTO(out, rc = ret);
	}

	if (opc_get(sp->sp_rpc->dr_opc) == DAOS_OBJ_RPC_FETCH) {
		struct obj_fetch_out *ofo;
		daos_vec_iod_t	*iods;
		uint64_t	*sizes;
		int		j;
		int		k;
		int		idx = 0;

		ofo = dtp_reply_get(sp->sp_rpc);
		iods = oui->oui_iods.da_arrays;
		sizes = ofo->ofo_sizes.da_arrays;

		/* update the sizes in iods */
		for (j = 0; j < oui->oui_nr; j++) {
			for (k = 0; k < iods[j].vd_nr; k++) {
				if (idx == ofo->ofo_sizes.da_count) {
					D_ERROR("Invalid return size %d\n",
						idx);
					D_GOTO(out, rc = -DER_PROTO);
				}
				iods[j].vd_recxs[k].rx_rsize = sizes[idx];
				idx++;
			}
		}
	}
out:
	if (bulks != NULL) {
		for (i = 0; i < oui->oui_nr; i++)
			dtp_bulk_free(bulks[i]);

		D_FREE(oui->oui_bulks.da_arrays,
		       oui->oui_nr * sizeof(dtp_bulk_t));
	}
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

static inline bool
obj_shard_io_check(unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls)
{
	int i;

	for (i = 0; i < nr; i++) {
		if (iods[i].vd_name.iov_buf == NULL ||
		    iods[i].vd_recxs == NULL)
			/* XXX checksum & eprs should not be mandatory */
			return false;
	}

	return true;
}

/**
 * XXX: Only use dkey to distribute the data among targets for
 * now, and eventually, it should use dkey + akey, but then
 * it means the I/O vector might needs to be split into
 * mulitple requests in obj_shard_rw()
 */
static uint32_t
obj_shard_dkey2tag(struct dc_obj_shard *dobj, daos_dkey_t *dkey)
{
	uint64_t hash;

	/** XXX hash is calculated twice (see cli_obj_dkey2shard) */
	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);
	hash %= dobj->do_nr_srv;

	return hash;
}

int
dc_obj_shard_rpc_cb(const struct dtp_cb_info *cb_info)
{
	struct daos_task	*task = cb_info->dci_arg;

	if (cb_info->dci_rc == -DER_TIMEDOUT)
		/** TODO */
		;

	if (task->dt_result == 0)
		task->dt_result = cb_info->dci_rc;

	task->dt_comp_cb(task);
	return 0;
}

static int
obj_shard_rw(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	     unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	     struct daos_task *task, enum obj_rpc_opc op)
{
	struct dc_obj_shard	*dobj;
	dtp_rpc_t		*req;
	dtp_bulk_t		*bulks;
	struct obj_update_in	*oui;
	struct daos_op_sp	*sp;
	dtp_bulk_perm_t		 bulk_perm;
	dtp_endpoint_t		 tgt_ep;
	uuid_t			 cont_hdl_uuid;
	int			 i;
	int			 rc;

	D_ASSERT(op == DAOS_OBJ_RPC_UPDATE || op == DAOS_OBJ_RPC_FETCH);
	bulk_perm = (op == DAOS_OBJ_RPC_UPDATE) ? DTP_BULK_RO : DTP_BULK_RW;

	/** sanity check input parameters */
	if (dkey == NULL || dkey->iov_buf == NULL || nr == 0 ||
	    !obj_shard_io_check(nr, iods, sgls))
		return -DER_INVAL;

	dobj = obj_shard_hdl2ptr(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	rc = dc_cont_hdl2uuid(dobj->do_co_hdl, &cont_hdl_uuid);
	if (rc != 0) {
		obj_shard_decref(dobj);
		return rc;
	}

	tgt_ep.ep_rank = dobj->do_rank;
	tgt_ep.ep_tag = obj_shard_dkey2tag(dobj, dkey);
	rc = obj_req_create(daos_task2ctx(task), tgt_ep, op, &req);
	if (rc != 0) {
		obj_shard_decref(dobj);
		return rc;
	}

	oui = dtp_req_get(req);
	D_ASSERT(oui != NULL);

	oui->oui_oid = dobj->do_id;
	uuid_copy(oui->oui_co_hdl, cont_hdl_uuid);

	obj_shard_decref(dobj);
	dobj = NULL;

	oui->oui_epoch = epoch;
	oui->oui_nr = nr;
	/** FIXME: large dkey should be transferred via bulk */
	oui->oui_dkey = *dkey;

	/* FIXME: if iods is too long, then we needs to do bulk transfer
	 * as well, but then we also needs to serialize the iods
	 **/
	oui->oui_iods.da_count = nr;
	oui->oui_iods.da_arrays = iods;

	D_ALLOC(bulks, nr * sizeof(*bulks));
	if (bulks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* create bulk transfer for daos_sg_list */
	for (i = 0; i < nr; i++) {
		if (sgls != NULL && sgls[i].sg_iovs != NULL &&
		    sgls[i].sg_iovs[0].iov_buf != NULL) {
			rc = dtp_bulk_create(daos_task2ctx(task), &sgls[i],
					     bulk_perm, &bulks[i]);
			if (rc < 0) {
				int j;

				for (j = 0; j < i; j++)
					dtp_bulk_free(bulks[j]);

				D_GOTO(out_free, rc);
			}
		}
	}
	oui->oui_bulks.da_count = nr;
	oui->oui_bulks.da_arrays = bulks;

	sp = daos_task2sp(task);
	dtp_req_addref(req);
	sp->sp_rpc = req;
	sp->sp_callback = obj_rw_cp;

	rc = dtp_req_send(req, dc_obj_shard_rpc_cb, task);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	return rc;
out_bulk:
	for (i = 0; i < nr; i++)
		rc = dtp_bulk_free(bulks[i]);
out_free:
	if (bulks != NULL)
		D_FREE(bulks, nr * sizeof(*bulks));
out:
	dtp_req_decref(req);
	return rc;
}

int
dc_obj_shard_update(daos_handle_t oh, daos_epoch_t epoch,
		    daos_dkey_t *dkey, unsigned int nr,
		    daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		    struct daos_task *task)
{
	return obj_shard_rw(oh, epoch, dkey, nr, iods, sgls, task,
			    DAOS_OBJ_RPC_UPDATE);
}

int
dc_obj_shard_fetch(daos_handle_t oh, daos_epoch_t epoch,
		   daos_dkey_t *dkey, unsigned int nr,
		   daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		   daos_vec_map_t *maps, struct daos_task *task)
{
	return obj_shard_rw(oh, epoch, dkey, nr, iods, sgls, task,
			    DAOS_OBJ_RPC_FETCH);
}

/**
 * Temporary solution for packing the tag into the hash out,
 * which will stay at 25-28 bytes of daos_hash_out_t->body
 */
#define ENUM_ANCHOR_TAG_OFF		24

static void
enum_anchor_copy(daos_hash_out_t *dst, daos_hash_out_t *src)
{
	memcpy(&dst->body[DAOS_HASH_HKEY_START],
	       &src->body[DAOS_HASH_HKEY_START], DAOS_HASH_HKEY_LENGTH);
}

static uint32_t
enum_anchor_get_tag(daos_hash_out_t *anchor)
{
	uint32_t tag;

	D_CASSERT(DAOS_HASH_HKEY_START + DAOS_HASH_HKEY_LENGTH <
		  ENUM_ANCHOR_TAG_OFF);
	memcpy(&tag, &anchor->body[ENUM_ANCHOR_TAG_OFF], sizeof(tag));
	return tag;
}

static void
enum_anchor_set_tag(daos_hash_out_t *anchor, uint32_t tag)
{
	memcpy(&anchor->body[ENUM_ANCHOR_TAG_OFF], &tag, sizeof(tag));
}

struct enum_async_arg {
	uint32_t	*eaa_nr;
	daos_key_desc_t *eaa_kds;
	daos_hash_out_t *eaa_anchor;
	struct dc_obj_shard *eaa_obj;
};

static int
enumerate_cp(struct daos_task *task, int rc)
{
	struct daos_op_sp	*sp = daos_task2sp(task);
	struct obj_key_enum_in	*oei;
	struct obj_key_enum_out	*oeo;
	struct enum_async_arg	*eaa;
	int			tgt_tag;

	oei = dtp_req_get(sp->sp_rpc);
	D_ASSERT(oei != NULL);
	eaa = sp->sp_arg;
	D_ASSERT(eaa != NULL);
	if (rc) {
		D_ERROR("RPC error: %d\n", rc);
		D_GOTO(out, rc);
	}

	oeo = dtp_reply_get(sp->sp_rpc);
	if (oeo->oeo_ret < 0) {
		D_ERROR("DAOS_OBJ_RPC_ENUMERATE replied failed, rc: %d\n",
			oeo->oeo_ret);
		D_GOTO(out, rc = oeo->oeo_ret);
	}

	if (*eaa->eaa_nr < oeo->oeo_kds.da_count) {
		D_ERROR("DAOS_OBJ_RPC_ENUMERATE return more kds, rc: %d\n",
			-DER_PROTO);
		D_GOTO(out, rc = -DER_PROTO);
	}

	*(eaa->eaa_nr) = oeo->oeo_kds.da_count;
	memcpy(eaa->eaa_kds, oeo->oeo_kds.da_arrays,
	       sizeof(*eaa->eaa_kds) * oeo->oeo_kds.da_count);

	enum_anchor_copy(eaa->eaa_anchor, &oeo->oeo_anchor);
	if (daos_hash_is_eof(&oeo->oeo_anchor) &&
	    opc_get(sp->sp_rpc->dr_opc) == DAOS_OBJ_DKEY_RPC_ENUMERATE) {
		tgt_tag = enum_anchor_get_tag(eaa->eaa_anchor);
		if (tgt_tag < eaa->eaa_obj->do_nr_srv - 1) {
			memset(eaa->eaa_anchor, 0, sizeof(*eaa->eaa_anchor));
			enum_anchor_set_tag(eaa->eaa_anchor, ++tgt_tag);
		}
	}

out:
	if (eaa->eaa_obj != NULL)
		obj_shard_decref(eaa->eaa_obj);

	D_FREE_PTR(eaa);
	dtp_bulk_free(oei->oei_bulk);

	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dc_obj_shard_list_key(daos_handle_t oh, uint32_t op, daos_epoch_t epoch,
		      daos_key_t *key, uint32_t *nr, daos_key_desc_t *kds,
		      daos_sg_list_t *sgl, daos_hash_out_t *anchor,
		      struct daos_task *task)
{
	dtp_endpoint_t		tgt_ep;
	dtp_rpc_t		*req;
	struct dc_obj_shard	*dobj;
	uuid_t			cont_hdl_uuid;
	struct obj_key_enum_in	*oei;
	struct enum_async_arg	*eaa;
	dtp_bulk_t		bulk;
	struct daos_op_sp	*sp;
	int			rc;

	dobj = obj_shard_hdl2ptr(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	rc = dc_cont_hdl2uuid(dobj->do_co_hdl, &cont_hdl_uuid);
	if (rc != 0) {
		obj_shard_decref(dobj);
		return rc;
	}

	tgt_ep.ep_grp = NULL;
	tgt_ep.ep_rank = dobj->do_rank;
	if (op == DAOS_OBJ_AKEY_RPC_ENUMERATE) {
		D_ASSERT(key != NULL);
		tgt_ep.ep_tag = obj_shard_dkey2tag(dobj, key);
	} else {
		tgt_ep.ep_tag = enum_anchor_get_tag(anchor);
	}
	rc = obj_req_create(daos_task2ctx(task), tgt_ep, op, &req);
	if (rc != 0)
		D_GOTO(out_put, rc);

	oei = dtp_req_get(req);
	if (key != NULL)
		oei->oei_key = *key;
	else
		memset(&oei->oei_key, 0, sizeof(oei->oei_key));

	D_ASSERT(oei != NULL);
	oei->oei_oid = dobj->do_id;
	uuid_copy(oei->oei_co_hdl, cont_hdl_uuid);

	oei->oei_epoch = epoch;
	oei->oei_nr = *nr;

	enum_anchor_copy(&oei->oei_anchor, anchor);
	/* Create bulk */
	rc = dtp_bulk_create(daos_task2ctx(task), sgl, DTP_BULK_RW, &bulk);
	if (rc < 0)
		D_GOTO(out_req, rc);

	oei->oei_bulk = bulk;

	sp = daos_task2sp(task);
	dtp_req_addref(req);
	sp->sp_rpc = req;
	D_ALLOC_PTR(eaa);
	if (eaa == NULL)
		D_GOTO(out_bulk, rc = -DER_NOMEM);

	eaa->eaa_nr = nr;
	eaa->eaa_kds = kds;
	eaa->eaa_anchor = anchor;
	eaa->eaa_obj = dobj;
	sp->sp_arg = eaa;
	sp->sp_callback = enumerate_cp;

	rc = dtp_req_send(req, dc_obj_shard_rpc_cb, task);
	if (rc != 0)
		D_GOTO(out_eaa, rc);

	return rc;
out_eaa:
	D_FREE_PTR(eaa);
out_bulk:
	dtp_bulk_free(&bulk);
out_req:
	dtp_req_decref(req);
out_put:
	obj_shard_decref(dobj);
	return rc;
}
