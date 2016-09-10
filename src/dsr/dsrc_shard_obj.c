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
 * dsr sub object operation.
 *
 * dsr sub object is mainly for IO on each shard.
 */

#include <daos/pool_map.h>
#include <daos/transport.h>
#include <daos/daos_m.h>
#include "dsr_rpc.h"
#include "dsr_internal.h"

struct daos_hhash *dsr_shard_hhash;

static inline void
dsr_shard_object_add_cache(struct dsr_shard_object *dobj, daos_handle_t *hdl)
{
	/* add obj to hash and assign the cookie to hdl */
	daos_hhash_link_insert(dsr_shard_hhash, &dobj->do_hlink,
			       DAOS_HTYPE_OBJ);
	daos_hhash_link_key(&dobj->do_hlink, &hdl->cookie);
}

static inline void
dsr_shard_object_del_cache(struct dsr_shard_object *dobj)
{
	daos_hhash_link_delete(dsr_shard_hhash, &dobj->do_hlink);
}

static inline void
dsr_shard_object_put(struct dsr_shard_object *dobj)
{
	daos_hhash_link_putref(dsr_shard_hhash, &dobj->do_hlink);
}

static inline struct dsr_shard_object*
dsr_handle2shard_obj(daos_handle_t hdl)
{
	struct daos_hlink *dlink;

	dlink = daos_hhash_link_lookup(dsr_shard_hhash, hdl.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dsr_shard_object, do_hlink);
}

static void
dsr_shard_object_free(struct daos_hlink *hlink)
{
	struct dsr_shard_object *dobj;

	dobj = container_of(hlink, struct dsr_shard_object, do_hlink);
	D_FREE_PTR(dobj);
}

struct daos_hlink_ops dobj_h_ops = {
	.hop_free = dsr_shard_object_free,
};

static struct dsr_shard_object *
dsr_shard_obj_alloc(daos_rank_t rank, daos_unit_oid_t id, uint32_t nr_srv)
{
	struct dsr_shard_object *dobj;

	D_ALLOC_PTR(dobj);
	if (dobj == NULL)
		return NULL;

	dobj->do_rank	= rank;
	dobj->do_nr_srv = nr_srv;
	dobj->do_id	= id;
	DAOS_INIT_LIST_HEAD(&dobj->do_co_list);
	daos_hhash_hlink_init(&dobj->do_hlink, &dobj_h_ops);

	return dobj;
}

int
dsr_shard_obj_open(daos_handle_t coh, uint32_t tgt, daos_unit_oid_t id,
		   unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	struct dsr_shard_object	*dobj;
	struct pool_target	*map_tgt;
	int			rc;

	rc = dsm_tgt_idx2pool_tgt(coh, &map_tgt, tgt);
	if (rc != 0)
		return rc;

	dobj = dsr_shard_obj_alloc(map_tgt->ta_comp.co_rank, id,
				   map_tgt->ta_comp.co_nr);
	if (dobj == NULL)
		return -DER_NOMEM;

	dobj->do_co_hdl = coh;

	dsr_shard_object_add_cache(dobj, oh);
	return 0;
}

int
dsr_shard_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	struct dsr_shard_object *dobj;

	dobj = dsr_handle2shard_obj(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	/* remove from hash */
	dsr_shard_object_put(dobj);
	dsr_shard_object_del_cache(dobj);
	return 0;
}

static int
obj_rw_cp(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp *sp = arg;
	struct object_update_in *oui;
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

	ret = dsr_get_reply_status(sp->sp_rpc);
	if (ret != 0) {
		D_ERROR("DSR_OBJ_UPDATE/FETCH replied failed, rc: %d\n",
			ret);
		D_GOTO(out, rc = ret);
	}

	if (opc_get(sp->sp_rpc->dr_opc) == DSR_TGT_OBJ_FETCH) {
		struct object_fetch_out *ofo;
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
dsr_shard_io_check(unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls)
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
 * mulitple requests in dsr_shard_obj_rw()
 */
static uint32_t
dsr_shard_get_tag(struct dsr_shard_object *dobj, daos_dkey_t *dkey)
{
	uint64_t hash;

	/** XXX hash is calculated twice (see cli_obj_dkey2shard) */
	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);
	hash %= dobj->do_nr_srv;

	return hash;
}

static int
dsr_shard_obj_rw(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
		 unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		 daos_event_t *ev, enum dsr_operation op)
{
	struct dsr_shard_object	*dobj;
	dtp_endpoint_t		 tgt_ep;
	dtp_rpc_t		*req;
	struct object_update_in *oui;
	dtp_bulk_t		*bulks;
	dtp_bulk_perm_t		 bulk_perm;
	struct daos_op_sp	*sp;
	uuid_t			cont_hdl_uuid;
	int			 i;
	int			 rc;

	D_ASSERT(op == DSR_TGT_OBJ_UPDATE || op == DSR_TGT_OBJ_FETCH);
	bulk_perm = (op == DSR_TGT_OBJ_UPDATE) ? DTP_BULK_RO : DTP_BULK_RW;

	/** sanity check input parameters */
	if (dkey == NULL || dkey->iov_buf == NULL || nr == 0 ||
	    !dsr_shard_io_check(nr, iods, sgls))
		return -DER_INVAL;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	dobj = dsr_handle2shard_obj(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	rc = dsm_cont_hdl2uuid(dobj->do_co_hdl, &cont_hdl_uuid);
	if (rc != 0) {
		dsr_shard_object_put(dobj);
		return rc;
	}

	tgt_ep.ep_rank = dobj->do_rank;
	tgt_ep.ep_tag = dsr_shard_get_tag(dobj, dkey);
	rc = dsr_req_create(daos_ev2ctx(ev), tgt_ep, op, &req);
	if (rc != 0) {
		dsr_shard_object_put(dobj);
		return rc;
	}

	oui = dtp_req_get(req);
	D_ASSERT(oui != NULL);

	oui->oui_oid = dobj->do_id;
	uuid_copy(oui->oui_co_hdl, cont_hdl_uuid);

	dsr_shard_object_put(dobj);
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
			rc = dtp_bulk_create(daos_ev2ctx(ev), &sgls[i],
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

	sp = daos_ev2sp(ev);
	dtp_req_addref(req);
	sp->sp_rpc = req;

	rc = daos_event_register_comp_cb(ev, obj_rw_cp, sp);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(out_bulk, rc);

	/** send the request */
	return daos_rpc_send(req, ev);

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
dsr_shard_obj_update(daos_handle_t oh, daos_epoch_t epoch,
		     daos_dkey_t *dkey, unsigned int nr,
		     daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		     daos_event_t *ev)
{
	return dsr_shard_obj_rw(oh, epoch, dkey, nr, iods, sgls, ev,
				DSR_TGT_OBJ_UPDATE);
}

int
dsr_shard_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
		    daos_dkey_t *dkey, unsigned int nr,
		    daos_vec_iod_t *iods, daos_sg_list_t *sgls,
		    daos_vec_map_t *maps, daos_event_t *ev)
{
	return dsr_shard_obj_rw(oh, epoch, dkey, nr, iods, sgls, ev,
				DSR_TGT_OBJ_FETCH);
}

struct enumerate_async_arg {
	uint32_t	*eaa_nr;
	daos_key_desc_t *eaa_kds;
	daos_hash_out_t *eaa_anchor;
	struct dsr_shard_object *eaa_obj;
};

static int
enumerate_cp(void *arg, daos_event_t *ev, int rc)
{
	struct daos_op_sp		*sp = arg;
	struct object_enumerate_in	*oei;
	struct object_enumerate_out	*oeo;
	struct enumerate_async_arg	*eaa;
	int				 tgt_tag;

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
		D_ERROR("DSR_OBJ_ENUMERATE replied failed, rc: %d\n",
			oeo->oeo_ret);
		D_GOTO(out, rc = oeo->oeo_ret);
	}

	if (*eaa->eaa_nr < oeo->oeo_kds.da_count) {
		D_ERROR("DSM_OBJ_ENUMERATE return more kds, rc: %d\n",
			-DER_PROTO);
		D_GOTO(out, rc = -DER_PROTO);
	}

	*(eaa->eaa_nr) = oeo->oeo_kds.da_count;
	memcpy(eaa->eaa_kds, oeo->oeo_kds.da_arrays,
	       sizeof(*eaa->eaa_kds) * oeo->oeo_kds.da_count);

	dsr_hash_hkey_copy(eaa->eaa_anchor, &oeo->oeo_anchor);
	if (daos_hash_is_eof(&oeo->oeo_anchor)) {
		tgt_tag = dsr_hash_get_tag(eaa->eaa_anchor);
		if (tgt_tag < eaa->eaa_obj->do_nr_srv - 1) {
			dsr_hash_set_tag(eaa->eaa_anchor, ++tgt_tag);
			dsr_hash_set_start(eaa->eaa_anchor);
		}
	}

out:
	if (eaa->eaa_obj != NULL)
		dsr_shard_object_put(eaa->eaa_obj);

	D_FREE_PTR(eaa);
	dtp_bulk_free(oei->oei_bulk);

	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dsr_shard_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch,
			uint32_t *nr, daos_key_desc_t *kds,
			daos_sg_list_t *sgl, daos_hash_out_t *anchor,
			daos_event_t *ev)
{
	dtp_endpoint_t		tgt_ep;
	dtp_rpc_t		*req;
	struct dsr_shard_object	*dobj;
	uuid_t			cont_hdl_uuid;
	struct object_enumerate_in *oei;
	struct enumerate_async_arg *eaa;
	dtp_bulk_t		bulk;
	struct daos_op_sp	*sp;
	int			rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	dobj = dsr_handle2shard_obj(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	rc = dsm_cont_hdl2uuid(dobj->do_co_hdl, &cont_hdl_uuid);
	if (rc != 0) {
		dsr_shard_object_put(dobj);
		return rc;
	}

	tgt_ep.ep_rank = dobj->do_rank;
	tgt_ep.ep_tag = dsr_hash_get_tag(anchor);
	rc = dsr_req_create(daos_ev2ctx(ev), tgt_ep, DSR_TGT_OBJ_ENUMERATE,
			    &req);
	if (rc != 0)
		D_GOTO(out_put, rc);

	oei = dtp_req_get(req);
	D_ASSERT(oei != NULL);

	oei->oei_oid = dobj->do_id;
	uuid_copy(oei->oei_co_hdl, cont_hdl_uuid);

	oei->oei_epoch = epoch;
	oei->oei_nr = *nr;

	dsr_hash_hkey_copy(&oei->oei_anchor, anchor);
	/* Create bulk */
	rc = dtp_bulk_create(daos_ev2ctx(ev), sgl, DTP_BULK_RW, &bulk);
	if (rc < 0)
		D_GOTO(out_req, rc);

	oei->oei_bulk = bulk;

	sp = daos_ev2sp(ev);
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

	rc = daos_event_register_comp_cb(ev, enumerate_cp, sp);
	if (rc != 0)
		D_GOTO(out_eaa, rc);

	rc = daos_event_launch(ev);
	if (rc != 0)
		D_GOTO(out_eaa, rc);

	/** send the request */
	return daos_rpc_send(req, ev);
out_eaa:
	D_FREE_PTR(eaa);
out_bulk:
	dtp_bulk_free(&bulk);
out_req:
	dtp_req_decref(req);
out_put:
	dsr_shard_object_put(dobj);
	return rc;
}
