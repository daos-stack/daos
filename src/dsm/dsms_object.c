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
 * dsms: object Operations
 *
 * This file contains the server API methods and the RPC handlers that are both
 * related to object.
 */

#include <uuid/uuid.h>

#include <daos/transport.h>
#include <daos_srv/daos_m_srv.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_server.h>

#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

struct dsms_vpool {
	daos_handle_t dvp_hdl; /* vos pool handle */
	uuid_t	      dvp_uuid;
	daos_list_t   dvp_list;
};

static struct daos_list_head dvp_list_head;
static pthread_mutex_t	     dvp_list_mutex;

int
dsms_object_init()
{
	DAOS_INIT_LIST_HEAD(&dvp_list_head);
	pthread_mutex_init(&dvp_list_mutex, NULL);
	return 0;
}

void
dsms_object_fini()
{
	struct dsms_vpool *dvp;
	struct dsms_vpool *tmp;

	pthread_mutex_lock(&dvp_list_mutex);
	daos_list_for_each_entry_safe(dvp, tmp, &dvp_list_head, dvp_list) {
		daos_list_del(&dvp->dvp_list);
		vos_pool_close(dvp->dvp_hdl, NULL);
		D_FREE_PTR(dvp);
	}
	pthread_mutex_unlock(&dvp_list_mutex);
	pthread_mutex_destroy(&dvp_list_mutex);
}

static struct dsms_vpool *
dsms_vpool_lookup(const uuid_t vp_uuid)
{
	struct dsms_vpool *dvp;

	daos_list_for_each_entry(dvp, &dvp_list_head, dvp_list) {
		if (uuid_compare(vp_uuid, dvp->dvp_uuid) == 0) {
			pthread_mutex_unlock(&dvp_list_mutex);
			return dvp;
		}
	}
	return NULL;
}

static int
dsms_co_open_create(daos_handle_t pool_hdl, uuid_t co_uuid,
		    daos_handle_t *co_hdl)
{
	int rc;

	/* TODO put it to container cache */
	rc = vos_co_open(pool_hdl, co_uuid, co_hdl, NULL);
	if (rc == -DER_NONEXIST) {
		/** create container on-the-fly */
		rc = vos_co_create(pool_hdl, co_uuid, NULL);
		if (rc)
			return rc;
		/** attempt to open again now that it is created ... */
		rc = vos_co_open(pool_hdl, co_uuid, co_hdl, NULL);
	}

	return rc;
}

static int
dsms_co_close(daos_handle_t dh)
{
	return vos_co_close(dh, NULL);
}

static int
dsms_pool_open(const uuid_t pool_uuid, daos_handle_t *vph)
{
	struct dss_module_info	*dmi;
	struct dsms_vpool	*vpool;
	struct dsms_vpool	*new;
	char			*path;
	int			 rc;

	D_DEBUG(DF_MISC, "lookup pool "DF_UUID"\n",
		DP_UUID(pool_uuid));
	pthread_mutex_lock(&dvp_list_mutex);
	vpool = dsms_vpool_lookup(pool_uuid);
	pthread_mutex_unlock(&dvp_list_mutex);
	if (vpool != NULL) {
		*vph = vpool->dvp_hdl;
		D_DEBUG(DF_MISC, "get pool "DF_UUID" from cache.\n",
			DP_UUID(pool_uuid));
		return 0;
	}

	dmi = dss_get_module_info();
	rc = dmgs_tgt_file(pool_uuid, VOS_FILE, &dmi->dmi_tid, &path);
	if (rc != 0)
		return rc;

	rc = vos_pool_open(path, (unsigned char *)pool_uuid, vph, NULL);
	if (rc != 0)
		D_GOTO(out_free, rc);

	D_ALLOC_PTR(new);
	if (new == NULL)
		D_GOTO(out_close, rc = -DER_NOMEM);

	uuid_copy(new->dvp_uuid, pool_uuid);
	new->dvp_hdl = *vph;
	DAOS_INIT_LIST_HEAD(&new->dvp_list);
	pthread_mutex_lock(&dvp_list_mutex);
	vpool = dsms_vpool_lookup(pool_uuid);
	if (vpool == NULL) {
		D_DEBUG(DF_MISC, "add pool "DF_UUID"\n",
			DP_UUID(pool_uuid));
		daos_list_add(&new->dvp_list, &dvp_list_head);
	} else {
		D_FREE_PTR(new);
		*vph = vpool->dvp_hdl;
	}

	pthread_mutex_unlock(&dvp_list_mutex);

	D_DEBUG(DF_MISC, "open pool "DF_X64"\n", vph->cookie);

out_close:
	if (rc != 0)
		vos_pool_close(*vph, NULL);
out_free:
	free(path);
	return rc;
}

struct dsms_update_async_args {
	daos_sg_list_t	*sgls;
	daos_iov_t	*iovs;
	struct daos_ref	*dref;
	daos_handle_t	pool_hdl;
	daos_handle_t   cont_hdl;
};

static int
update_write_bulk_complete_cb(const struct dtp_bulk_cb_info *cb_info)
{
	struct dsms_update_async_args	*args;
	struct dtp_bulk_desc		*bulk_desc;
	dtp_rpc_t			*rpc_req;
	dtp_bulk_t			local_bulk_hdl;
	int				rc = 0;

	rc = cb_info->bci_rc;
	if (rc != 0)
		D_ERROR("bulk transfer failed: rc = %d\n", rc);

	bulk_desc = cb_info->bci_bulk_desc;
	local_bulk_hdl = bulk_desc->bd_local_hdl;
	rpc_req = bulk_desc->bd_rpc;
	args = (struct dsms_update_async_args *)cb_info->bci_arg;

	if (daos_ref_dec_and_test(args->dref)) {
		struct object_update_in	*oui;
		struct dtp_single_out *dso;
		daos_sg_list_t  *sgls;

		if (!daos_handle_is_inval(args->cont_hdl))
			dsms_co_close(args->cont_hdl);

		oui = dtp_req_get(rpc_req);
		/* After the bulk is done, send reply */
		sgls = args->sgls;
		D_ASSERT(sgls != NULL);
		D_FREE(args->iovs,
		       oui->oui_nr * sizeof(*args->iovs));
		D_FREE(sgls, oui->oui_nr * sizeof(*sgls));

		dso = dtp_reply_get(rpc_req);
		dso->dso_ret = rc;

		rc = dtp_reply_send(rpc_req);
		if (rc != 0)
			D_ERROR("send reply failed: %d\n", rc);
	}
	dtp_bulk_free(local_bulk_hdl);
	dtp_req_decref(rpc_req);
	D_FREE_PTR(args);
	return rc;
}

static int
obj_rw_rpc_final_cb(dtp_rpc_t *rpc)
{
	struct daos_ref *dr = rpc->dr_data;

	if (dr != NULL) {
		pthread_mutex_destroy(&dr->dr_lock);
		D_FREE_PTR(dr);
	}

	return 0;
}

int
dsms_hdlr_object_rw(dtp_rpc_t *rpc)
{
	struct object_update_in	*oui;
	struct dtp_single_out	*dso;
	struct daos_ref		*dr = NULL;
	daos_handle_t		dph = DAOS_HDL_INVAL;
	daos_handle_t		dch = DAOS_HDL_INVAL;
	daos_iov_t		*iovs = NULL;
	daos_sg_list_t		*sgls = NULL;
	dtp_bulk_opid_t		bulk_opid;
	dtp_bulk_t		*remote_bulks;
	dtp_bulk_op_t		bulk_op;
	int			i = 0;
	int			rc = 0;
	int			rc1;

	oui = dtp_req_get(rpc);
	if (oui == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Sigh, we have to allocate the sgls and iovs here,
	 * because after bulk transfer the update will be
	 * done in different callback.
	 **/
	D_ALLOC(iovs, oui->oui_nr * sizeof(*iovs));
	if (iovs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	D_ALLOC(sgls, oui->oui_nr * sizeof(*sgls));
	if (sgls == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	remote_bulks = oui->oui_bulks.arrays;
	D_ALLOC_PTR(dr);
	if (dr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpc->dr_final_cb = obj_rw_rpc_final_cb;
	rpc->dr_data = dr;
	daos_ref_init(dr, oui->oui_nr);

	for (i = 0; i < oui->oui_nr; i++) {
		daos_size_t bulk_len;

		rc = dtp_bulk_get_len(remote_bulks[i], &bulk_len);
		if (rc != 0) {
			D_ERROR("i %d get bulk len error.: rc = %d\n", i, rc);
			D_GOTO(out, rc);
		}

		iovs[i].iov_buf = NULL;
		iovs[i].iov_len = bulk_len;
		iovs[i].iov_buf_len = bulk_len;

		sgls[i].sg_nr.num = 1;
		sgls[i].sg_iovs = &iovs[i];
	}

	/* Open the pool and container */
	rc = dsms_pool_open(oui->oui_pool_uuid, &dph);
	if (rc != 0)
		D_GOTO(out, rc);

	rc = dsms_co_open_create(dph, oui->oui_co_uuid, &dch);
	if (rc != 0)
		D_GOTO(out, rc);

	if (opc_get(rpc->dr_opc) == DSM_TGT_OBJ_UPDATE) {
		/* allocate the buffer in iovs to do rdma */
		rc = vos_obj_update(dch, oui->oui_oid, oui->oui_epoch,
				    &oui->oui_dkey, oui->oui_nr,
				    oui->oui_iods.arrays, sgls, NULL);
		bulk_op = DTP_BULK_GET;
	} else {
		rc = vos_obj_fetch(dch, oui->oui_oid, oui->oui_epoch,
				    &oui->oui_dkey, oui->oui_nr,
				    oui->oui_iods.arrays, sgls, NULL);
		bulk_op = DTP_BULK_PUT;
	}
	if (rc != 0)
		D_GOTO(out, rc);

	for (i = 0; i < oui->oui_nr; i++) {
		struct dtp_bulk_desc		bulk_desc;
		struct dsms_update_async_args	*arg;
		dtp_bulk_t			local_bulk_hdl;

		D_ALLOC_PTR(arg);
		if (arg == NULL)
			D_GOTO(out, rc);

		arg->dref = dr;
		arg->sgls = sgls;
		arg->iovs = iovs;
		arg->pool_hdl = dph;
		arg->cont_hdl = dch;

		rc = dtp_bulk_create(rpc->dr_ctx, &sgls[i], DTP_BULK_RW,
				     &local_bulk_hdl);
		if (rc != 0) {
			D_FREE_PTR(arg);
			D_GOTO(out, rc);
		}

		dtp_req_addref(rpc);

		bulk_desc.bd_rpc = rpc;
		bulk_desc.bd_bulk_op = bulk_op;
		bulk_desc.bd_remote_hdl = remote_bulks[i];
		bulk_desc.bd_local_hdl = local_bulk_hdl;
		bulk_desc.bd_len = sgls[i].sg_iovs->iov_len;
		bulk_desc.bd_remote_off = 0;
		bulk_desc.bd_local_off = 0;

		rc = dtp_bulk_transfer(&bulk_desc,
				       update_write_bulk_complete_cb,
				       arg, &bulk_opid);
		if (rc < 0) {
			dtp_bulk_free(local_bulk_hdl);
			dtp_req_decref(rpc);
			D_FREE_PTR(arg);
			D_GOTO(out, rc);
		}
	}

	return rc;
out:
	dso = dtp_reply_get(rpc);
	dso->dso_ret = rc;
	rc1 = dtp_reply_send(rpc);
	if (rc1 != 0)
		D_ERROR("send reply failed: %d\n", rc1);

	for (i = 0; i < oui->oui_nr; i++) {
		if (iovs[i].iov_buf != NULL)
			D_FREE(iovs[i].iov_buf, iovs[i].iov_buf_len);
	}

	if (!daos_handle_is_inval(dch))
		dsms_co_close(dch);

	if (iovs != NULL)
		D_FREE(iovs, oui->oui_nr * sizeof(*iovs));
	if (sgls != NULL)
		D_FREE(sgls, oui->oui_nr * sizeof(*sgls));

	D_DEBUG(DF_MISC, "rw result %d\n", rc);
	return rc;
}
