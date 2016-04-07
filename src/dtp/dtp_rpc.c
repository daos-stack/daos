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
/**
 * This file is part of daos_transport. It implements the main RPC routines.
 */

#include <dtp_internal.h>

int
dtp_req_create(dtp_context_t dtp_ctx, dtp_endpoint_t tgt_ep, dtp_opcode_t opc,
	       dtp_rpc_t **req)
{
	struct dtp_hg_context	*hg_ctx;
	struct dtp_rpc_priv	*rpc_priv = NULL;
	struct dtp_opc_info	*opc_info = NULL;
	dtp_rpc_t		*rpc_pub;
	int			rc = 0;

	if (dtp_ctx == DTP_CONTEXT_NULL || req == NULL) {
		D_ERROR("invalid parameter (NULL dtp_ctx or req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	/* TODO: possibly with multiple service group */
	if (tgt_ep.ep_rank >= dtp_gdata.dg_mcl_srv_set->size) {
		D_ERROR("invalid parameter, rank %d, group_size: %d.\n",
			tgt_ep.ep_rank, dtp_gdata.dg_mcl_srv_set->size);
		D_GOTO(out, rc = -DER_INVAL);
	}

	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map, opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n", opc);
		D_GOTO(out, rc = -DER_DTP_UNREG);
	}
	D_ASSERT(opc_info->doi_input_size <= DTP_MAX_INPUT_SIZE &&
		 opc_info->doi_output_size <= DTP_MAX_OUTPUT_SIZE);

	D_ALLOC_PTR(rpc_priv);
	if (rpc_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpc_pub = &rpc_priv->drp_pub;
	rpc_pub->dr_ep = tgt_ep;

	rc = dtp_rpc_inout_buff_init(rpc_pub, opc_info->doi_input_size,
				     opc_info->doi_output_size);
	if (rc != 0) {
		D_ERROR("dtp_rpc_inout_buff_init faied, rc: %d, opc: 0x%x.\n",
			rc, opc);
		D_FREE_PTR(rpc_priv);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	dtp_rpc_priv_init(rpc_priv, dtp_ctx, opc, 0);

	hg_ctx = (struct dtp_hg_context *)dtp_ctx;
	rc = dtp_hg_req_create(hg_ctx, tgt_ep, rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_hg_req_create failed, rc: %d, opc: 0x%x.\n",
			rc, opc);
		D_FREE_PTR(rpc_priv);
		dtp_rpc_inout_buff_fini(rpc_pub);
		D_GOTO(out, rc);
	}

	*req = rpc_pub;

out:
	return rc;
}

int
dtp_req_addref(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount++;
	pthread_spin_unlock(&rpc_priv->drp_lock);

out:
	return rc;
}

int
dtp_req_decref(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0, destroy = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	pthread_spin_lock(&rpc_priv->drp_lock);
	rpc_priv->drp_refcount--;
	if (rpc_priv->drp_refcount == 0)
		destroy = 1;
	pthread_spin_unlock(&rpc_priv->drp_lock);

	if (destroy == 1) {
		rc = dtp_hg_req_destroy(rpc_priv);
		if (rc != 0)
			D_ERROR("dtp_hg_req_destroy failed, rc: %d, "
				"opc: 0x%x.\n", rc, req->dr_opc);
	}

out:
	return rc;
}

int
dtp_req_send(dtp_rpc_t *req, dtp_cb_t complete_cb, void *arg)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	rc = dtp_hg_req_send(rpc_priv, complete_cb, arg);
	if (rc != 0) {
		D_ERROR("dtp_hg_req_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

int
dtp_reply_send(dtp_rpc_t *req)
{
	struct dtp_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct dtp_rpc_priv, drp_pub);
	rc = dtp_hg_reply_send(rpc_priv);
	if (rc != 0) {
		D_ERROR("dtp_hg_reply_send failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
	}

out:
	return rc;
}

int
dtp_req_abort(dtp_rpc_t *req)
{
	return -DER_NOSYS;
}
