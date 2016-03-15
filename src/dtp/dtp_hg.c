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
 * This file is part of daos_transport. It implements the main interfaces to
 * mercury.
 */

#include <dtp_internal.h>

/* only-for-testing basic RPC in same node, before address model available */
na_addr_t    na_addr_test = NA_ADDR_NULL;

/* be called only in dtp_init */
int
dtp_hg_init(const char *info_string, bool server)
{
	struct dtp_hg_gdata	*hg_gdata;
	na_class_t		*na_class = NULL;
	na_context_t		*na_context = NULL;
	hg_class_t		*hg_class = NULL;
	int			rc = 0;

	if (dtp_initialized()) {
		D_ERROR("dtp already initialized.\n");
		D_GOTO(out, rc = -DER_ALREADY);
	}

	D_ASSERT(info_string != NULL && strlen(info_string) != 0);

	na_class = NA_Initialize(info_string, server);
	if (na_class == NULL) {
		D_ERROR("Could not initialize NA class.\n");
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	na_context = NA_Context_create(na_class);
	if (na_context == NULL) {
		D_ERROR("Could not create NA context.\n");
		NA_Finalize(na_class);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	hg_class = HG_Init(na_class, na_context, NULL);
	if (hg_class == NULL) {
		D_ERROR("Could not initialize HG class.\n");
		NA_Context_destroy(na_class, na_context);
		NA_Finalize(na_class);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	D_ALLOC_PTR(hg_gdata);
	if (hg_gdata == NULL) {
		HG_Finalize(hg_class);
		NA_Context_destroy(na_class, na_context);
		NA_Finalize(na_class);
		D_GOTO(out, rc = -DER_NOMEM);
	}

	hg_gdata->dhg_nacla = na_class;
	hg_gdata->dhg_nactx = na_context;
	hg_gdata->dhg_hgcla = hg_class;

	dtp_gdata.dg_hg = hg_gdata;

	/* only-for-testing, establish a connection */
	if (dtp_gdata.dg_server == 1)
		goto out;
	na_return_t na_ret;
	na_ret = NA_Addr_lookup_wait(na_class, info_string, &na_addr_test);
	if (na_ret != NA_SUCCESS) {
		D_ERROR("Could not connect to %s.\n", info_string);
	} else {
		D_DEBUG(DF_TP, "testing connection to %s succeed.\n",
			info_string);
	}

out:
	return rc;
}

/* be called only in dtp_finalize */
int
dtp_hg_fini()
{
	na_class_t	*na_class;
	na_context_t	*na_context;
	hg_class_t	*hg_class;
	hg_return_t	hg_ret = HG_SUCCESS;
	na_return_t	na_ret = NA_SUCCESS;
	int		rc = 0;

	if (!dtp_initialized()) {
		D_ERROR("dtp not initialized.\n");
		D_GOTO(out, rc = -DER_NO_PERM);
	}

	na_class = dtp_gdata.dg_hg->dhg_nacla;
	na_context = dtp_gdata.dg_hg->dhg_nactx;
	hg_class = dtp_gdata.dg_hg->dhg_hgcla;
	D_ASSERT(na_class != NULL);
	D_ASSERT(na_context != NULL);
	D_ASSERT(hg_class != NULL);

	hg_ret = HG_Finalize(hg_class);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not finalize HG class, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	na_ret = NA_Context_destroy(na_class, na_context);
	if (na_ret != NA_SUCCESS) {
		D_ERROR("Could not destroy NA context, na_ret: %d.\n", na_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	na_ret = NA_Finalize(na_class);
	if (na_ret != NA_SUCCESS) {
		D_ERROR("Could not finalize NA class, na_ret: %d.\n", na_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	D_FREE_PTR(dtp_gdata.dg_hg);

out:
	return rc;
}

int
dtp_hg_ctx_init(struct dtp_hg_context *hg_ctx)
{
	hg_context_t		*hg_context = NULL;
	int			rc = 0;

	D_ASSERT(hg_ctx != NULL);

	hg_context = HG_Context_create(dtp_gdata.dg_hg->dhg_hgcla);
	if (hg_context == NULL) {
		D_ERROR("Could not create HG context.\n");
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	hg_ctx->dhc_nacla = dtp_gdata.dg_hg->dhg_nacla;
	hg_ctx->dhc_nactx = dtp_gdata.dg_hg->dhg_nactx;
	hg_ctx->dhc_hgcla = dtp_gdata.dg_hg->dhg_hgcla;
	hg_ctx->dhc_hgctx = hg_context;
	DAOS_INIT_LIST_HEAD(&hg_ctx->dhc_link);

out:
	return rc;
}

int
dtp_hg_ctx_fini(struct dtp_hg_context *hg_ctx)
{
	hg_context_t    *hg_context;
	hg_return_t     hg_ret = HG_SUCCESS;
	int             rc = 0;

	D_ASSERT(hg_ctx != NULL);
	hg_context = hg_ctx->dhc_hgctx;
	D_ASSERT(hg_context != NULL);

	hg_ret = HG_Context_destroy(hg_context);
	if (hg_ret == HG_SUCCESS) {
		hg_ctx->dhc_hgctx = NULL;
	} else {
		D_ERROR("Could not destroy HG context, hg_ret: %d.\n", hg_ret);
		rc = -DER_DTP_HG;
	}

	return rc;
}

int
dtp_rpc_handler_common(hg_handle_t hg_hdl)
{
	struct dtp_hg_context	*hg_ctx;
	struct hg_info		*hg_info;
	dtp_opcode_t		opc;
	struct dtp_rpc_priv	*rpc_priv;
	dtp_rpc_t		*rpc_pub;
	struct dtp_opc_info	*opc_info = NULL;
	hg_return_t		hg_ret = HG_SUCCESS;
	int			rc = 0, addref = 0;

	hg_info = HG_Get_info(hg_hdl);
	if (hg_info == NULL) {
		D_ERROR("HG_Get_info failed.\n");
		D_GOTO(out, hg_ret = HG_PROTOCOL_ERROR);
	}

	hg_ctx = dtp_hg_context_lookup(hg_info->context);
	if (hg_ctx == NULL) {
		D_ERROR("dtp_hg_context_lookup failed.\n");
		D_GOTO(out, hg_ret = HG_PROTOCOL_ERROR);
	}
	D_ASSERT(hg_ctx->dhc_hgcla == hg_info->hg_class);

	opc = hg_info->id;

	/* D_DEBUG(DF_TP,"in dtp_rpc_handler_common, opc: 0x%x.\n", opc); */

	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map, opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n", opc);
		D_GOTO(out, hg_ret = HG_NO_MATCH);
	}
	D_ASSERT(opc_info->doi_input_size <= DTP_MAX_INPUT_SIZE &&
		 opc_info->doi_output_size <= DTP_MAX_OUTPUT_SIZE);

	D_ALLOC_PTR(rpc_priv);
	if (rpc_priv == NULL)
		D_GOTO(out, hg_ret = HG_NOMEM_ERROR);

	rpc_pub = &rpc_priv->drp_pub;
	rc = dtp_rpc_inout_buff_init(rpc_pub, opc_info->doi_input_size,
				     opc_info->doi_output_size);
	if (rc != 0) {
		D_ERROR("dtp_rpc_inout_buff_init faied, rc: %d, opc: 0x%x.\n",
			rc, opc);
		D_FREE_PTR(rpc_priv);
		D_GOTO(out, hg_ret = HG_NOMEM_ERROR);
	}

	dtp_rpc_priv_init(rpc_priv, (dtp_context_t)hg_ctx, opc, 1);
	rpc_priv->drp_na_addr = hg_info->addr;
	rpc_priv->drp_hg_hdl = hg_hdl;
	rc = dtp_req_addref(rpc_pub);
	D_ASSERT(rc == 0);
	addref = 1;

	D_ASSERT(rpc_priv->drp_srv != 0);
	if (opc_info->doi_input_size > 0) {
		void	*hg_in_struct;

		D_ASSERT(rpc_pub->dr_input != NULL);
		hg_in_struct = &rpc_pub->dr_input;
		/* corresponding to HG_Free_input in dtp_hg_req_destroy */
		hg_ret = HG_Get_input(rpc_priv->drp_hg_hdl, hg_in_struct);
		if (hg_ret == HG_SUCCESS) {
			rpc_priv->drp_input_got = 1;
		} else {
			D_ERROR("HG_Get_input failed, hg_ret: %d, opc: 0x%x.\n",
				hg_ret, rpc_pub->dr_opc);
			D_GOTO(out, hg_ret);
		}
	}

	if (opc_info->doi_rpc_cb != NULL) {
		rc = opc_info->doi_rpc_cb(rpc_pub);
		if (rc != 0) {
			D_ERROR("doi_rpc_cb failed, rc: %d, opc: 0x%x.\n",
				rc, opc);
		}
	} else {
		D_ERROR("NULL drp_hg_hdl, opc: 0x%x.\n", opc);
		hg_ret = HG_NO_MATCH;
	}

out:
	if (addref != 0) {
		rc = dtp_req_decref(rpc_pub);
		if (rc != 0)
			D_ERROR("dtp_req_decref failed, rc: %d.\n", rc);
	}
	return hg_ret;
}

int
dtp_hg_req_create(struct dtp_hg_context *hg_ctx, dtp_endpoint_t tgt_ep,
		  struct dtp_rpc_priv *rpc_priv)
{
	hg_return_t    hg_ret = HG_SUCCESS;
	int            rc = 0;

	D_ASSERT(hg_ctx != NULL && hg_ctx->dhc_hgcla != NULL &&
		 hg_ctx->dhc_hgctx != NULL);
	D_ASSERT(rpc_priv != NULL);

	/* only-for-testing now to use the na_addr_test */
	rpc_priv->drp_na_addr = na_addr_test;

	hg_ret = HG_Create(hg_ctx->dhc_hgcla, hg_ctx->dhc_hgctx,
			   rpc_priv->drp_na_addr, rpc_priv->drp_pub.dr_opc,
			   &rpc_priv->drp_hg_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Create failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		rc = -DER_DTP_HG;
	}

	return rc;
}

int
dtp_hg_req_destroy(struct dtp_rpc_priv *rpc_priv)
{
	hg_return_t	hg_ret = HG_SUCCESS;
	int		rc = 0;

	D_ASSERT(rpc_priv != NULL);
	/*
	D_DEBUG(DF_TP,"enter dtp_hg_req_destroy, opc: 0x%x.\n",
		rpc_priv->drp_pub.dr_opc);
	*/
	hg_ret = HG_Destroy(rpc_priv->drp_hg_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Destroy failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	dtp_rpc_inout_buff_fini(&rpc_priv->drp_pub);
	if (rpc_priv->drp_output_got != 0) {
		hg_ret = HG_Free_output(rpc_priv->drp_hg_hdl,
					&rpc_priv->drp_pub.dr_output);
		if (hg_ret != HG_SUCCESS)
			D_ERROR("HG_Free_output failed, hg_ret: %d, "
				"opc: 0x%x.\n", hg_ret,
				rpc_priv->drp_pub.dr_opc);
	}
	if (rpc_priv->drp_input_got != 0) {
		hg_ret = HG_Free_input(rpc_priv->drp_hg_hdl,
				       &rpc_priv->drp_pub.dr_input);
		if (hg_ret != HG_SUCCESS)
			D_ERROR("HG_Free_input failed, hg_ret: %d, "
				"opc: 0x%x.\n", hg_ret,
				rpc_priv->drp_pub.dr_opc);
	}

	pthread_spin_destroy(&rpc_priv->drp_lock);
	D_FREE_PTR(rpc_priv);

out:
	return rc;
}

struct dtp_hg_send_cbinfo {
	struct dtp_rpc_priv	*rsc_rpc_priv;
	dtp_cb_t		rsc_cb;
	void			*rsc_arg;
};

/* the common completion callback for sending RPC request */
static hg_return_t
dtp_hg_req_send_cb(const struct hg_cb_info *hg_cbinfo)
{
	struct dtp_hg_send_cbinfo	*req_cbinfo;
	struct dtp_cb_info		dtp_cbinfo;
	dtp_rpc_t			*rpc_pub;
	struct dtp_rpc_priv		*rpc_priv;
	dtp_opcode_t			opc;
	hg_return_t			hg_ret = HG_SUCCESS;
	int				rc = 0;

	/* D_DEBUG(DF_TP,"enter dtp_hg_req_send_cb.\n"); */
	req_cbinfo = (struct dtp_hg_send_cbinfo *)hg_cbinfo->arg;
	D_ASSERT(req_cbinfo != NULL);

	rpc_priv = req_cbinfo->rsc_rpc_priv;
	D_ASSERT(rpc_priv != NULL);
	rpc_pub = &rpc_priv->drp_pub;
	opc = rpc_pub->dr_opc;

	if (hg_cbinfo->ret != HG_SUCCESS) {
		D_ERROR("hg_cbinfo->ret: %d.\n", hg_cbinfo->ret);
		rc = -DER_DTP_HG;
		hg_ret = hg_cbinfo->ret;
	}

	if (req_cbinfo->rsc_cb == NULL) {
		/*
		D_DEBUG(DF_TP, "no completion callback registered, "
			"opc: 0x%x.\n", opc);
		*/
		D_GOTO(out, hg_ret);
	}

	if (rc == 0) {
		/* HG_Free_output in dtp_hg_req_destroy */
		hg_ret = HG_Get_output(hg_cbinfo->handle, &rpc_pub->dr_output);
		if (hg_ret != HG_SUCCESS) {
			D_ERROR("HG_Get_output failed, hg_ret: %d, opc: "
				"0x%x.\n", hg_ret, opc);
			rc = -DER_DTP_HG;
		}
		rpc_priv->drp_output_got = 1;
	}

	dtp_cbinfo.dci_rpc = rpc_pub;
	dtp_cbinfo.dci_arg = req_cbinfo->rsc_arg;
	dtp_cbinfo.dci_rc = rc;


	D_ASSERT(req_cbinfo->rsc_cb != NULL);
	rc = req_cbinfo->rsc_cb(&dtp_cbinfo);
	if (rc != 0)
		D_ERROR("req_cbinfo->rsc_cb returned %d.\n", rc);

out:
	D_FREE_PTR(req_cbinfo);

	/* corresponding to the dtp_req_addref in dtp_hg_req_send */
	rc = dtp_req_decref(rpc_pub);
	if (rc != 0)
		D_ERROR("dtp_req_decref failed, rc: %d, opc: 0x%x.\n", rc, opc);

	return hg_ret;
}

int
dtp_hg_req_send(struct dtp_rpc_priv *rpc_priv, dtp_cb_t complete_cb, void *arg)
{
	struct dtp_hg_send_cbinfo	*cb_info;
	hg_return_t			hg_ret = HG_SUCCESS;
	void				*hg_in_struct;
	int				rc = 0;

	D_ASSERT(rpc_priv != NULL);

	D_ALLOC_PTR(cb_info);
	if (cb_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	hg_in_struct = &rpc_priv->drp_pub.dr_input;

	cb_info->rsc_rpc_priv = rpc_priv;
	cb_info->rsc_cb = complete_cb;
	cb_info->rsc_arg = arg;

	hg_ret = HG_Forward(rpc_priv->drp_hg_hdl, dtp_hg_req_send_cb, cb_info,
			    hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Forward failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		D_FREE_PTR(cb_info);
		rc = -DER_DTP_HG;
	}

	rc = dtp_req_addref(&rpc_priv->drp_pub);
	D_ASSERT(rc == 0);

out:
	return rc;
}

/* just to release the reference taken at dtp_hg_reply_send */
static hg_return_t
dtp_hg_reply_send_cb(const struct hg_cb_info *hg_cbinfo)
{
	struct dtp_hg_send_cbinfo	*req_cbinfo;
	struct dtp_rpc_priv		*rpc_priv;
	hg_return_t			hg_ret = HG_SUCCESS;
	dtp_opcode_t			opc;
	int				rc = 0;

	/* D_DEBUG(DF_TP,"enter dtp_hg_reply_send_cb.\n"); */
	req_cbinfo = (struct dtp_hg_send_cbinfo *)hg_cbinfo->arg;
	D_ASSERT(req_cbinfo != NULL && req_cbinfo->rsc_rpc_priv != NULL);

	rpc_priv = req_cbinfo->rsc_rpc_priv;
	opc = rpc_priv->drp_pub.dr_opc;
	hg_ret = hg_cbinfo->ret;
	if (hg_ret != HG_SUCCESS)
		D_ERROR("dtp_hg_reply_send_cb, hg_cbinfo->ret: %d, "
			"opc: 0x%x.\n", hg_ret, opc);

	/* corresponding to the dtp_req_addref in dtp_hg_reply_send */
	rc = dtp_req_decref(&rpc_priv->drp_pub);
	if (rc != 0)
		D_ERROR("dtp_req_decref failed, rc: %d, opc: 0x%x.\n", rc, opc);

	D_FREE_PTR(req_cbinfo);
	return hg_ret;
}

int
dtp_hg_reply_send(struct dtp_rpc_priv *rpc_priv)
{
	struct dtp_hg_send_cbinfo	*cb_info;
	hg_return_t			hg_ret = HG_SUCCESS;
	void				*hg_out_struct;
	int				rc = 0;

	D_ASSERT(rpc_priv != NULL);

	D_ALLOC_PTR(cb_info);
	if (cb_info == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	hg_out_struct = &rpc_priv->drp_pub.dr_output;

	cb_info->rsc_rpc_priv = rpc_priv;

	hg_ret = HG_Respond(rpc_priv->drp_hg_hdl, dtp_hg_reply_send_cb, cb_info,
			    hg_out_struct);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Respond failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		D_FREE_PTR(cb_info);
		rc = -DER_DTP_HG;
	}

	rc = dtp_req_addref(&rpc_priv->drp_pub);
	D_ASSERT(rc == 0);

out:
	return rc;
}

int
dtp_hg_progress(struct dtp_hg_context *hg_ctx, unsigned int timeout)
{
	hg_context_t    *hg_context;
	hg_class_t      *hg_class;
	hg_return_t     hg_ret = HG_SUCCESS;
	unsigned int    num_cb = 0;
	int             rc = 0;

	D_ASSERT(hg_ctx != NULL);
	hg_context = hg_ctx->dhc_hgctx;
	hg_class = hg_ctx->dhc_hgcla;
	D_ASSERT(hg_context != NULL && hg_class != NULL);

	hg_ret = HG_Trigger(hg_class, hg_context, 0, 1, &num_cb);
	if (hg_ret != HG_SUCCESS && hg_ret != HG_TIMEOUT) {
		D_ERROR("HG_Trigger failed, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	hg_ret = HG_Progress(hg_class, hg_context, timeout);
	if (hg_ret != HG_SUCCESS && hg_ret != HG_TIMEOUT) {
		D_ERROR("HG_Progress failed, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* Progress succeed, call HG_Trigger again */
	if (hg_ret == HG_SUCCESS) {
		hg_ret = HG_Trigger(hg_class, hg_context, 0, 1, &num_cb);
		if (hg_ret != HG_SUCCESS && hg_ret != HG_TIMEOUT) {
			D_ERROR("HG_Trigger failed, hg_ret: %d.\n", hg_ret);
			rc = -DER_DTP_HG;
		}
	}

	if (hg_ret == HG_TIMEOUT)
		rc = -ETIMEDOUT;

out:
	return rc;
}
