/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/*
 * dsmc: object operation.
 *
 * dsmc is the DSM client to do object operation.
 */
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

static int
dsmc_obj_pool_container_uuid_get(daos_handle_t oh, uuid_t puuid,
				 uuid_t cuuid, daos_unit_oid_t *do_id)
{
	struct pool_conn *pc;
	struct dsmc_container *dc;
	struct dsmc_object *dobj;

	dobj = dsmc_handle2obj(oh);
	if (dobj == NULL)
		return -DER_ENOENT;

	D_ASSERT(!daos_handle_is_inval(dobj->do_co_hdl));
	dc = dsmc_handle2container(dobj->do_co_hdl);
	if (dc == NULL) {
		dsmc_object_put(dobj);
		return -DER_ENOENT;
	}

	D_ASSERT(!daos_handle_is_inval(dc->dc_pool_hdl));
	pc = dsmc_handle2pool(dc->dc_pool_hdl);
	if (pc == NULL) {
		dsmc_object_put(dobj);
		dsmc_container_put(dc);
		return -DER_ENOENT;
	}

	uuid_copy(puuid, pc->pc_pool);
	uuid_copy(cuuid, dc->dc_uuid);

	*do_id = dobj->do_id;
	dsmc_object_put(dobj);
	dsmc_container_put(dc);

	return 0;
}

static int
update_cp(struct daos_op_sp *sp, daos_event_t *ev, int rc)
{
	struct object_update_in *oui;
	struct dtp_single_out *dso;
	dtp_bulk_t *bulks;
	int i;

	oui = dtp_req_get(sp->sp_rpc);
	D_ASSERT(oui != NULL);
	bulks = oui->oui_bulks.arrays;
	D_ASSERT(bulks != NULL);
	if (rc) {
		D_ERROR("RPC error: %d\n", rc);
		D_GOTO(out, rc);
	}

	dso = dtp_reply_get(sp->sp_rpc);
	if (dso->dso_ret != 0) {
		D_ERROR("DSM_OBJ_UPDATE replied failed, rc: %d\n",
			dso->dso_ret);
		D_GOTO(out, rc = dso->dso_ret);
	}
out:
	for (i = 0; i < oui->oui_nr; i++)
		dtp_bulk_free(bulks[i]);

	D_FREE(oui->oui_bulks.arrays,
	       oui->oui_nr * sizeof(dtp_bulk_t));
	dtp_req_decref(sp->sp_rpc);
	return rc;
}

static int
dsm_obj_rw(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	   unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	   daos_event_t *ev, enum dsm_operation op)
{
	dtp_endpoint_t		tgt_ep;
	dtp_rpc_t		*req;
	struct object_update_in *oui;
	dtp_bulk_t		*bulks;
	struct daos_op_sp	*sp;
	int			i;
	int			rc;

	if (ev == NULL) {
		rc = daos_event_priv_get(&ev);
		if (rc != 0)
			return rc;
	}

	/* TODO we need to pass the target rank to server as well */
	tgt_ep.ep_rank = 0;
	tgt_ep.ep_tag = 0;
	rc = dsm_req_create(daos_ev2ctx(ev), tgt_ep, op, &req);
	if (rc != 0)
		return rc;

	oui = dtp_req_get(req);
	D_ASSERT(oui != NULL);

	rc = dsmc_obj_pool_container_uuid_get(oh,
			oui->oui_pool_uuid, oui->oui_co_uuid,
			&oui->oui_oid);
	if (rc != 0)
		D_GOTO(out, rc);

	oui->oui_epoch = epoch;
	oui->oui_nr = nr;
	oui->oui_dkey = *dkey;
	/* FIXME: if iods is too long, then we needs to do bulk transfer
	 * as well, but then we also needs to serialize the iods
	 **/
	D_ALLOC(bulks, nr * sizeof(*bulks));
	if (bulks == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	oui->oui_iods.count = nr;
	oui->oui_iods.arrays = iods;
	oui->oui_bulks.count = nr;
	oui->oui_bulks.arrays = bulks;
	/* create bulk transfer for daos_sg_list */
	for (i = 0; i < nr; i++) {
		rc = dtp_bulk_create(daos_ev2ctx(ev), &sgls[i], DTP_BULK_RW,
				     &bulks[i]);
		if (rc < 0) {
			int j;

			for (j = 0; j < i; j++)
				rc = dtp_bulk_free(bulks[j]);

			D_GOTO(out_free, rc);
		}
	}

	sp = daos_ev2sp(ev);
	dtp_req_addref(req);
	sp->sp_rpc = req;
	rc = daos_event_launch(ev, NULL, update_cp);
	if (rc != 0) {
		dtp_req_decref(req);
		D_GOTO(out_bulk, rc);
	}

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
dsm_obj_alloc(daos_unit_oid_t id)
{
	struct dsmc_object *dobj;

	D_ALLOC_PTR(dobj);
	if (dobj == NULL)
		return NULL;

	dobj->do_id = id;
	DAOS_INIT_LIST_HEAD(&dobj->do_co_list);
	daos_hhash_hlink_init(&dobj->do_hlink, &dobj_h_ops);
	return dobj;
}

int
dsm_obj_open(daos_handle_t coh, daos_unit_oid_t id, unsigned int mode,
	     daos_handle_t *oh, daos_event_t *ev)
{
	struct dsmc_object	*dobj;
	struct dsmc_container	*dc;

	dc = dsmc_handle2container(coh);
	if (dc == NULL)
		return -DER_INVAL;

	dobj = dsm_obj_alloc(id);
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
		return -DER_EXIST;

	dc = dsmc_handle2container(dobj->do_co_hdl);
	if (dc == NULL) {
		dsmc_object_put(dobj);
		return -DER_INVAL;
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
