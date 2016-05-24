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

#include <daos_srv/daos_m_srv.h>
#include <daos_srv/vos.h>
#include <uuid/uuid.h>
#include <daos/transport.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"
#include "dsms_layout.h"

struct dsms_update_async_args {
	daos_sg_list_t	*sgls;
	daos_iov_t	*iovs;
	struct daos_ref	*dref;
};

static int
dsms_co_open(daos_handle_t pool_hdl, uuid_t co_uuid,
	     daos_handle_t *co_hdl)
{
	/* TODO put it to container cache */
	return vos_co_open(pool_hdl, co_uuid, co_hdl, NULL);
}

static int
dsms_co_close(daos_handle_t dh)
{
	return vos_co_close(dh, NULL);
}

static int
dsms_pool_open(const uuid_t pool_uuid, daos_handle_t *vph)
{
	char *path;
	int rc;

	rc = dmgs_tgt_file(pool_uuid, NULL, NULL, &path);
	if (rc != 0)
		return rc;

	rc = vos_pool_open(path, (unsigned char *)pool_uuid, vph, NULL);

	free(path);
	return rc;
}

static int
dsms_pool_close(daos_handle_t vph)
{
	return vos_pool_close(vph, NULL);
}

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
	struct dtp_single_out *dso;
	struct daos_ref		*dr = NULL;
	daos_handle_t		dph = DAOS_HDL_INVAL;
	daos_handle_t		dch = DAOS_HDL_INVAL;
	daos_iov_t		*iovs = NULL;
	daos_sg_list_t		*sgls = NULL;
	dtp_bulk_opid_t		bulk_opid;
	dtp_bulk_t		*remote_bulks;
	int			i = 0;
	int			rc = 0;

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

		rc = dtp_bulk_get_len(&remote_bulks[i], &bulk_len);
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

	rc = dsms_co_open(dph, oui->oui_co_uuid, &dch);
	if (rc != 0)
		D_GOTO(out, rc);

	if (rpc->dr_opc == DSM_TGT_OBJ_UPDATE) {
		/* allocate the buffer in iovs to do rdma */
		rc = vos_obj_update(dch, oui->oui_oid, oui->oui_epoch,
				    &oui->oui_dkey, oui->oui_nr,
				    oui->oui_iods.arrays, sgls, NULL);
	} else {
		rc = vos_obj_fetch(dch, oui->oui_oid, oui->oui_epoch,
				    &oui->oui_dkey, oui->oui_nr,
				    oui->oui_iods.arrays, sgls, NULL);
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

		rc = dtp_bulk_create(rpc->dr_ctx, &sgls[i], DTP_BULK_RW,
				     &local_bulk_hdl);
		if (rc != 0) {
			D_FREE_PTR(arg);
			D_GOTO(out, rc);
		}

		dtp_req_addref(rpc);

		bulk_desc.bd_rpc = rpc;
		bulk_desc.bd_bulk_op = DTP_BULK_GET;
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
	rc = dtp_reply_send(rpc);
	if (rc != 0)
		D_ERROR("send reply failed: %d\n", rc);

	for (i = 0; i < oui->oui_nr; i++) {
		if (iovs[i].iov_buf != NULL)
			D_FREE(iovs[i].iov_buf, iovs[i].iov_buf_len);
	}

	if (!daos_handle_is_inval(dch))
		dsms_co_close(dch);

	if (!daos_handle_is_inval(dch))
		dsms_pool_close(dph);

	if (iovs != NULL)
		D_FREE(iovs, oui->oui_nr * sizeof(*iovs));
	if (sgls != NULL)
		D_FREE(sgls, oui->oui_nr * sizeof(*sgls));

	return rc;
}
