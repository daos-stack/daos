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
 * dsmc: object operation.
 *
 * dsmc is the DSM client to do object operation.
 */

#include <daos/pool_map.h>
#include <daos/transport.h>
#include "dsm_rpc.h"
#include "dsmc_internal.h"

struct daos_hhash *dsmc_hhash;

static inline struct dsmc_object*
dsmc_handle2obj(daos_handle_t hdl)
{
	struct daos_hlink *dlink;

	dlink = daos_hhash_link_lookup(dsmc_hhash, hdl.cookie);
	if (dlink == NULL)
		return NULL;

	return container_of(dlink, struct dsmc_object, do_hlink);
}

/**
 * XXX Getting dsmc object, pool and container by object handle, which
 * only retrieve from client cache for now.
 */
static int
dsm_open_pool_container(daos_handle_t hdl, struct dsmc_object **dobjp,
			struct dsmc_pool **dpoolp,
			struct dsmc_container **dcontp)
{
	struct dsmc_pool	*dp = NULL;
	struct dsmc_container	*dc = NULL;
	struct dsmc_object	*dobj = NULL;
	int			 rc = 0;

	dobj = dsmc_handle2obj(hdl);
	if (dobj == NULL)
		return -DER_NO_HDL;
	*dobjp = dobj;

	D_ASSERT(!daos_handle_is_inval(dobj->do_co_hdl));
	dc = dsmc_handle2container(dobj->do_co_hdl);
	if (dc == NULL)
		D_GOTO(out_put, rc = -DER_NO_HDL);
	*dcontp = dc;

	D_ASSERT(!daos_handle_is_inval(dc->dc_pool_hdl));
	dp = dsmc_handle2pool(dc->dc_pool_hdl);
	if (dp == NULL)
		D_GOTO(out_put, rc = -DER_NO_HDL);
	*dpoolp = dp;

out_put:
	if (rc != 0) {
		if (dobj != NULL)
			dsmc_object_put(dobj);
		if (dc != NULL)
			dsmc_container_put(dc);
		if (dp != NULL)
			dsmc_pool_put(dp);
	}

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

	ret = dsm_get_reply_status(sp->sp_rpc);
	if (ret != 0) {
		D_ERROR("DSM_OBJ_UPDATE/FETCH replied failed, rc: %d\n",
			ret);
		D_GOTO(out, rc = ret);
	}

	if (opc_get(sp->sp_rpc->dr_opc) == DSM_TGT_OBJ_FETCH) {
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
dsm_io_check(unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls)
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
 * mulitple requests in dsm_obj_rw()
 */
static uint32_t
dsm_get_tag(struct dsmc_object *dobj, daos_dkey_t *dkey)
{
	uint64_t hash;

	/** XXX hash is calculated twice (see cli_obj_dkey2shard) */
	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);
	hash %= dobj->do_nr_srv;

	return hash;
}

static int
dsm_obj_rw(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	   unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	   daos_event_t *ev, enum dsm_operation op)
{
	struct dsmc_object	*dobj;
	dtp_endpoint_t		 tgt_ep;
	dtp_rpc_t		*req;
	struct object_update_in *oui;
	dtp_bulk_t		*bulks;
	dtp_bulk_perm_t		 bulk_perm;
	struct daos_op_sp	*sp;
	struct dsmc_pool	*dpool = NULL;
	struct dsmc_container	*dcont = NULL;
	int			 i;
	int			 rc;

	D_ASSERT(op == DSM_TGT_OBJ_UPDATE || op == DSM_TGT_OBJ_FETCH);
	bulk_perm = (op == DSM_TGT_OBJ_UPDATE) ? DTP_BULK_RO : DTP_BULK_RW;

	/** sanity check input parameters */
	if (dkey == NULL || dkey->iov_buf == NULL || nr == 0 ||
	    !dsm_io_check(nr, iods, sgls))
		return -DER_INVAL;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	rc = dsm_open_pool_container(oh, &dobj, &dpool, &dcont);
	if (rc != 0)
		return rc;

	tgt_ep.ep_rank = dobj->do_rank;
	tgt_ep.ep_tag = dsm_get_tag(dobj, dkey);
	rc = dsm_req_create(daos_ev2ctx(ev), tgt_ep, op, &req);
	if (rc != 0) {
		dsmc_object_put(dobj);
		dsmc_container_put(dcont);
		dsmc_pool_put(dpool);
		return rc;
	}

	oui = dtp_req_get(req);
	D_ASSERT(oui != NULL);

	oui->oui_oid = dobj->do_id;
	uuid_copy(oui->oui_co_hdl, dcont->dc_cont_hdl);

	dsmc_object_put(dobj);
	dsmc_container_put(dcont);
	dsmc_pool_put(dpool);
	dobj = NULL;
	dcont = NULL;
	dpool = NULL;

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
dsm_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	       unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	       daos_event_t *ev)
{
	return dsm_obj_rw(oh, epoch, dkey, nr, iods, sgls, ev,
			  DSM_TGT_OBJ_UPDATE);
}

int
dsm_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	      unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	      daos_vec_map_t *maps, daos_event_t *ev)
{
	return dsm_obj_rw(oh, epoch, dkey, nr, iods, sgls, ev,
			  DSM_TGT_OBJ_FETCH);
}

static void
dsmc_object_free(struct daos_hlink *hlink)
{
	struct dsmc_object *dobj;

	dobj = container_of(hlink, struct dsmc_object, do_hlink);
	D_FREE_PTR(dobj);
}

struct daos_hlink_ops dobj_h_ops = {
	.hop_free = dsmc_object_free,
};

static struct dsmc_object *
dsm_obj_alloc(daos_rank_t rank, daos_unit_oid_t id, uint32_t nr_srv)
{
	struct dsmc_object *dobj;

	D_ALLOC_PTR(dobj);
	if (dobj == NULL)
		return NULL;

	dobj->do_rank	= rank;
	dobj->do_nr_srv	= nr_srv;
	dobj->do_id	= id;
	DAOS_INIT_LIST_HEAD(&dobj->do_co_list);
	daos_hhash_hlink_init(&dobj->do_hlink, &dobj_h_ops);

	return dobj;
}

int
dsm_obj_open(daos_handle_t coh, uint32_t tgt, daos_unit_oid_t id,
	     unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	struct dsmc_object	*dobj;
	struct dsmc_container	*dc;
	struct dsmc_pool	*pool;
	struct pool_target	*map_tgt;
	int			n;

	dc = dsmc_handle2container(coh);
	if (dc == NULL)
		return -DER_NO_HDL;

	/* Get map_tgt so that we can have the rank of the target. */
	pool = dsmc_handle2pool(dc->dc_pool_hdl);
	D_ASSERT(pool != NULL);
	n = pool_map_find_target(pool->dp_map, tgt, &map_tgt);
	dsmc_pool_put(pool);
	if (n != 1) {
		D_ERROR("failed to find target %u\n", tgt);
		dsmc_container_put(dc);
		return -DER_INVAL;
	}

	dobj = dsm_obj_alloc(map_tgt->ta_comp.co_rank, id,
			     map_tgt->ta_comp.co_nr);
	if (dobj == NULL) {
		dsmc_container_put(dc);
		return -DER_NOMEM;
	}

	/* XXX Might have performance issue here */
	pthread_rwlock_wrlock(&dc->dc_obj_list_lock);
	if (dc->dc_closing) {
		pthread_rwlock_unlock(&dc->dc_obj_list_lock);
		dsmc_object_put(dobj);
		dsmc_container_put(dc);
		return -DER_INVAL;
	}
	daos_list_add(&dobj->do_co_list, &dc->dc_obj_list);
	dobj->do_co_hdl = coh;
	pthread_rwlock_unlock(&dc->dc_obj_list_lock);

	dsmc_object_add_cache(dobj, oh);
	dsmc_container_put(dc);
	return 0;
}

int
dsm_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	struct dsmc_object *dobj;
	struct dsmc_container *dc;

	dobj = dsmc_handle2obj(oh);
	if (dobj == NULL)
		return -DER_NO_HDL;

	dc = dsmc_handle2container(dobj->do_co_hdl);
	if (dc == NULL) {
		dsmc_object_put(dobj);
		return -DER_NO_HDL;
	}

	/* remove from container list */
	pthread_rwlock_wrlock(&dc->dc_obj_list_lock);
	daos_list_del_init(&dobj->do_co_list);
	pthread_rwlock_unlock(&dc->dc_obj_list_lock);

	/* remove from hash */
	dsmc_object_put(dobj);
	dsmc_object_del_cache(dobj);
	dsmc_container_put(dc);
	return 0;
}

struct enumerate_async_arg {
	uint32_t	*eaa_nr;
	daos_key_desc_t *eaa_kds;
	daos_hash_out_t *eaa_anchor;
	struct dsmc_object *eaa_obj;
	struct dsmc_container *eaa_cont;
	struct dsmc_pool      *eaa_pool;
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
		D_ERROR("DSM_OBJ_ENUMERATE replied failed, rc: %d\n",
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

	dsmc_hash_hkey_copy(eaa->eaa_anchor,
			    &oeo->oeo_anchor);
	if (daos_hash_is_eof(&oeo->oeo_anchor)) {
		tgt_tag = dsmc_hash_get_tag(eaa->eaa_anchor);
		if (tgt_tag < eaa->eaa_obj->do_nr_srv - 1) {
			dsmc_hash_set_tag(eaa->eaa_anchor, ++tgt_tag);
			dsmc_hash_set_start(eaa->eaa_anchor);
		}
	}

out:
	if (eaa->eaa_obj != NULL)
		dsmc_object_put(eaa->eaa_obj);
	if (eaa->eaa_cont != NULL)
		dsmc_container_put(eaa->eaa_cont);
	if (eaa->eaa_pool != NULL)
		dsmc_pool_put(eaa->eaa_pool);

	D_FREE_PTR(eaa);
	dtp_bulk_free(oei->oei_bulk);

	dtp_req_decref(sp->sp_rpc);
	return rc;
}

int
dsm_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		  daos_key_desc_t *kds, daos_sg_list_t *sgl,
		  daos_hash_out_t *anchor, daos_event_t *ev)
{
	dtp_endpoint_t		tgt_ep;
	dtp_rpc_t		*req;
	struct dsmc_object	*dobj;
	struct dsmc_container	*dcont;
	struct dsmc_pool	*dpool;
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

	rc = dsm_open_pool_container(oh, &dobj, &dpool, &dcont);
	if (rc != 0)
		return rc;

	tgt_ep.ep_rank = dobj->do_rank;
	tgt_ep.ep_tag = dsmc_hash_get_tag(anchor);
	rc = dsm_req_create(daos_ev2ctx(ev), tgt_ep, DSM_TGT_OBJ_ENUMERATE,
			    &req);
	if (rc != 0)
		D_GOTO(out_put, rc);

	oei = dtp_req_get(req);
	D_ASSERT(oei != NULL);

	oei->oei_oid = dobj->do_id;
	uuid_copy(oei->oei_co_hdl, dcont->dc_cont_hdl);

	oei->oei_epoch = epoch;
	oei->oei_nr = *nr;

	dsmc_hash_hkey_copy(&oei->oei_anchor, anchor);
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
	eaa->eaa_pool = dpool;
	eaa->eaa_obj = dobj;
	eaa->eaa_cont = dcont;
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
	dsmc_object_put(dobj);
	dsmc_container_put(dcont);
	dsmc_pool_put(dpool);
	return rc;
}
