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

static na_return_t
na_addr_lookup_cb(const struct na_cb_info *callback_info)
{
    na_addr_t *addr_ptr = (na_addr_t *) callback_info->arg;
    na_return_t ret = NA_SUCCESS;

    if (callback_info->ret != NA_SUCCESS) {
        NA_LOG_ERROR("Return from callback with %s error code",
                NA_Error_to_string(callback_info->ret));
        return ret;
    }

    *addr_ptr = callback_info->info.lookup.addr;

    return ret;
}

na_return_t
dtp_na_addr_lookup_wait(na_class_t *na_class, const char *name, na_addr_t *addr)
{
    na_addr_t new_addr = NULL;
    na_bool_t lookup_completed = NA_FALSE;
    na_context_t *context = NULL;
    na_return_t ret = NA_SUCCESS;

    if (!na_class) {
        NA_LOG_ERROR("NULL NA class");
        ret = NA_INVALID_PARAM;
        goto done;
    }
    if (!name) {
        NA_LOG_ERROR("Lookup name is NULL");
        ret = NA_INVALID_PARAM;
        goto done;
    }
    if (!addr) {
        NA_LOG_ERROR("NULL pointer to na_addr_t");
        ret = NA_INVALID_PARAM;
        goto done;
    }

    context = NA_Context_create(na_class);
    if (!context) {
        NA_LOG_ERROR("Could not create context");
        goto done;
    }

    ret = NA_Addr_lookup(na_class, context, &na_addr_lookup_cb, &new_addr, name,
            NA_OP_ID_IGNORE);
    if (ret != NA_SUCCESS) {
        NA_LOG_ERROR("Could not start NA_Addr_lookup");
        goto done;
    }

    while (!lookup_completed) {
        na_return_t trigger_ret;
        unsigned int actual_count = 0;

        do {
            trigger_ret = NA_Trigger(context, 0, 1, &actual_count);
        } while ((trigger_ret == NA_SUCCESS) && actual_count);

        if (new_addr) {
            lookup_completed = NA_TRUE;
            *addr = new_addr;
        }

        if (lookup_completed) break;

        ret = NA_Progress(na_class, context, NA_MAX_IDLE_TIME);
        if (ret != NA_SUCCESS) {
            NA_LOG_ERROR("Could not make progress");
            goto done;
        }
    }

    ret = NA_Context_destroy(na_class, context);
    if (ret != NA_SUCCESS) {
        NA_LOG_ERROR("Could not destroy context");
        goto done;
    }

done:
    return ret;
}

/* be called only in dtp_init */
int
dtp_hg_init(dtp_phy_addr_t *addr, bool server)
{
	const char		*info_string;
	struct dtp_hg_gdata	*hg_gdata;
	na_class_t		*na_class = NULL;
	na_context_t		*na_context = NULL;
	hg_class_t		*hg_class = NULL;
	int			rc = 0;

	if (dtp_initialized()) {
		D_ERROR("dtp already initialized.\n");
		D_GOTO(out, rc = -DER_ALREADY);
	}

	if (*addr != NULL) {
		info_string = *addr;
		D_ASSERT(strncmp(info_string, "bmi+tcp", 7) == 0);
	} else {
		if (dtp_gdata.dg_verbs == true)
			info_string = "cci+verbs://host";
		else
			info_string = "cci+tcp://host";
	}

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

	hg_class = HG_Init_na(na_class, na_context);
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

	/* register the DTP_HG_RPCID */
	rc = dtp_hg_reg(dtp_gdata.dg_hg->dhg_hgcla, DTP_HG_RPCID,
			(dtp_proc_cb_t)dtp_proc_in_common,
			(dtp_proc_cb_t)dtp_proc_out_common,
			(dtp_hg_rpc_cb_t)dtp_rpc_handler_common);
	if (rc != 0) {
		D_ERROR("dtp_hg_reg(rpcid: 0x%x), failed rc: %d.\n",
			DTP_HG_RPCID, rc);
		HG_Finalize(hg_class);
		NA_Context_destroy(na_class, na_context);
		NA_Finalize(na_class);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	if (*addr == NULL) {
		const char *port_name;
		char addr_str[DTP_ADDR_STR_MAX_LEN] = {'\0'};

		port_name = NA_CCI_Get_port_name(na_class);
		if (port_name == NULL) {
			D_ERROR("NA_CCI_Get_port_name failed.\n");
			HG_Finalize(hg_class);
			NA_Context_destroy(na_class, na_context);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		rc = snprintf(addr_str, DTP_ADDR_STR_MAX_LEN, "cci+%s",
			      port_name);
		*addr = strdup(addr_str);
		if (rc < 0 || *addr == NULL) {
			D_ERROR("snprintf or strdup failed, rc: %d.\n", rc);
			HG_Finalize(hg_class);
			NA_Context_destroy(na_class, na_context);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
		rc = 0;
		info_string = *addr;
	}

	D_DEBUG(DF_TP, "in dtp_hg_init, listen address: %s.\n", *addr);

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
	/*
	 * Ignore the error due to a HG bug:
	 * https://github.com/mercury-hpc/mercury/issues/88
	 */
	/*
	if (na_ret != NA_SUCCESS) {
		D_ERROR("Could not destroy NA context, na_ret: %d.\n", na_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	*/

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
dtp_hg_ctx_init(struct dtp_hg_context *hg_ctx, int idx)
{
	na_class_t	*na_class = NULL;
	na_context_t	*na_context = NULL;
	hg_class_t	*hg_class = NULL;
	hg_context_t	*hg_context = NULL;
	const char	*info_string;
	const char	*port_name;
	int		rc = 0;

	D_ASSERT(hg_ctx != NULL);

	if (idx == 0 || dtp_gdata.dg_multi_na == false) {
		hg_context = HG_Context_create(dtp_gdata.dg_hg->dhg_hgcla);
		if (hg_context == NULL) {
			D_ERROR("Could not create HG context.\n");
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		hg_ctx->dhc_nacla = dtp_gdata.dg_hg->dhg_nacla;
		hg_ctx->dhc_nactx = dtp_gdata.dg_hg->dhg_nactx;
		hg_ctx->dhc_hgcla = dtp_gdata.dg_hg->dhg_hgcla;
		hg_ctx->dhc_shared_na = true;
	} else {
		if (dtp_gdata.dg_verbs == true)
			info_string = "cci+verbs://host";
		else
			info_string = "cci+tcp://host";

		na_class = NA_Initialize(info_string, dtp_gdata.dg_server);
		if (na_class == NULL) {
			D_ERROR("Could not initialize NA class.\n");
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		port_name = NA_CCI_Get_port_name(na_class);
		if (port_name == NULL) {
			D_ERROR("NA_CCI_Get_port_name failed.\n");
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
		D_DEBUG(DF_TP, "New context(idx:%d), listen address: cci+%s.\n",
			idx, port_name);

		na_context = NA_Context_create(na_class);
		if (na_context == NULL) {
			D_ERROR("Could not create NA context.\n");
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		hg_class = HG_Init_na(na_class, na_context);
		if (hg_class == NULL) {
			D_ERROR("Could not initialize HG class.\n");
			NA_Context_destroy(na_class, na_context);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		hg_context = HG_Context_create(hg_class);
		if (hg_context == NULL) {
			D_ERROR("Could not create HG context.\n");
			HG_Finalize(hg_class);
			NA_Context_destroy(na_class, na_context);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		/* register the shared RPCID to every hg_class */
		rc = dtp_hg_reg(hg_class, DTP_HG_RPCID,
				(dtp_proc_cb_t)dtp_proc_in_common,
				(dtp_proc_cb_t)dtp_proc_out_common,
				(dtp_hg_rpc_cb_t)dtp_rpc_handler_common);
		if (rc != 0) {
			D_ERROR("dtp_hg_reg(rpcid: 0x%x), failed rc: %d.\n",
				DTP_HG_RPCID, rc);
			HG_Context_destroy(hg_context);
			HG_Finalize(hg_class);
			NA_Context_destroy(na_class, na_context);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		hg_ctx->dhc_nacla = na_class;
		hg_ctx->dhc_nactx = na_context;
		hg_ctx->dhc_hgcla = hg_class;
		hg_ctx->dhc_shared_na = false;
	}

	hg_ctx->dhc_idx = idx;
	hg_ctx->dhc_hgctx = hg_context;
	/* TODO: need to create separate bulk class and bulk context? */
	hg_ctx->dhc_bulkcla = hg_ctx->dhc_hgcla;
	hg_ctx->dhc_bulkctx = hg_ctx->dhc_hgctx;
	D_ASSERT(hg_ctx->dhc_bulkcla != NULL);
	D_ASSERT(hg_ctx->dhc_bulkctx != NULL);
	DAOS_INIT_LIST_HEAD(&hg_ctx->dhc_link);

out:
	return rc;
}

int
dtp_hg_ctx_fini(struct dtp_hg_context *hg_ctx)
{
	hg_context_t	*hg_context;
	hg_return_t	hg_ret = HG_SUCCESS;
	na_return_t	na_ret;
	int		rc = 0;

	D_ASSERT(hg_ctx != NULL);
	hg_context = hg_ctx->dhc_hgctx;
	D_ASSERT(hg_context != NULL);

	hg_ret = HG_Context_destroy(hg_context);
	if (hg_ret == HG_SUCCESS) {
		hg_ctx->dhc_hgctx = NULL;
	} else {
		D_ERROR("Could not destroy HG context, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	if (hg_ctx->dhc_shared_na == true)
		goto out;

	/* the hg_context destroyed, ignore below errors with error logging */
	hg_ret = HG_Finalize(hg_ctx->dhc_hgcla);
	if (hg_ret != HG_SUCCESS)
		D_ERROR("Could not finalize HG class, hg_ret: %d.\n", hg_ret);

	na_ret = NA_Context_destroy(hg_ctx->dhc_nacla, hg_ctx->dhc_nactx);
	if (na_ret != NA_SUCCESS)
		D_ERROR("Could not destroy NA context, na_ret: %d.\n", na_ret);

	na_ret = NA_Finalize(hg_ctx->dhc_nacla);
	if (na_ret != NA_SUCCESS)
		D_ERROR("Could not finalize NA class, na_ret: %d.\n", na_ret);

out:
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
	dtp_proc_t		proc = NULL;
	struct dtp_opc_info	*opc_info = NULL;
	hg_return_t		hg_ret = HG_SUCCESS;
	int			rc = 0;

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

	D_ALLOC_PTR(rpc_priv);
	if (rpc_priv == NULL)
		D_GOTO(out, hg_ret = HG_NOMEM_ERROR);

	rpc_priv->drp_na_addr = hg_info->addr;
	rpc_priv->drp_hg_hdl = hg_hdl;
	rpc_pub = &rpc_priv->drp_pub;
	rpc_pub->dr_ctx = (dtp_context_t)hg_ctx;
	D_ASSERT(rpc_pub->dr_input == NULL);

	rc = dtp_hg_unpack_header(rpc_priv, &proc);
	if (rc != 0) {
		D_ERROR("dtp_hg_unpack_header failed, rc: %d.\n", rc);
		D_FREE_PTR(rpc_priv);
		D_GOTO(out, hg_ret = HG_OTHER_ERROR);
	}

	opc = rpc_priv->drp_req_hdr.dch_opc;
	/* D_DEBUG(DF_TP,"in dtp_rpc_handler_common, opc: 0x%x.\n", opc); */
	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map, opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n", opc);
		D_FREE_PTR(rpc_priv);
		dtp_hg_unpack_cleanup(proc);
		D_GOTO(out, hg_ret = HG_NO_MATCH);
	}
	D_ASSERT(opc_info->doi_input_size <= DTP_MAX_INPUT_SIZE &&
		 opc_info->doi_output_size <= DTP_MAX_OUTPUT_SIZE);

	dtp_rpc_priv_init(rpc_priv, (dtp_context_t)hg_ctx, opc, 1);

	rc = dtp_rpc_inout_buff_init(rpc_pub, opc_info->doi_input_size,
				     opc_info->doi_output_size);
	if (rc != 0) {
		D_ERROR("dtp_rpc_inout_buff_init faied, rc: %d, opc: 0x%x.\n",
			rc, opc);
		dtp_hg_unpack_cleanup(proc);
		D_GOTO(decref, hg_ret = HG_NOMEM_ERROR);
	}

	D_ASSERT(rpc_priv->drp_srv != 0);
	if (opc_info->doi_input_size > 0) {
		D_ASSERT(rpc_pub->dr_input != NULL);
		D_ASSERT(opc_info->doi_inproc_cb != NULL);
		/* corresponding to HG_Free_input in dtp_hg_req_destroy */
		rc = dtp_hg_unpack_body(rpc_priv, proc,
					opc_info->doi_inproc_cb);
		if (rc == 0) {
			rpc_priv->drp_input_got = 1;
			uuid_copy(rpc_pub->dr_ep.ep_grp_id,
				  rpc_priv->drp_req_hdr.dch_grp_id);
			rpc_pub->dr_ep.ep_rank = rpc_priv->drp_req_hdr.dch_rank;
		} else {
			D_ERROR("_unpack_body failed, rc: %d, opc: 0x%x.\n",
				hg_ret, rpc_pub->dr_opc);
			D_GOTO(decref, hg_ret = HG_OTHER_ERROR);
		}
	} else {
		dtp_hg_unpack_cleanup(proc);
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

decref:
	rc = dtp_req_decref(rpc_pub);
	if (rc != 0)
		D_ERROR("dtp_req_decref failed, rc: %d.\n", rc);
out:
	return hg_ret;
}

/*
 * MCL address lookup table, use a big array for simplicity. Also simplify the
 * lock needed for possible race of lookup (possible cause multiple address
 * resolution and one time memory leak).
 * The multiple listening addresses for one server rank is a temporary solution,
 * it will be replaced by OFI tag matching mechanism and then this should be
 * removed.
 */
struct addr_entry {
	/* rank's base uri is known by mcl */
	dtp_phy_addr_t	ae_base_uri;
	na_addr_t	ae_tag_addrs[DTP_SRV_CONTEX_NUM];
} addr_lookup_table[MCL_PS_SIZE_MAX];

static int
dtp_mcl_lookup(struct mcl_set *mclset, daos_rank_t rank, uint32_t tag,
	       na_class_t *na_class, na_addr_t *na_addr)
{
	na_addr_t	tmp_addr;
	uint32_t	ctx_idx;
	char		tmp_addrstr[DTP_ADDR_STR_MAX_LEN] = {'\0'};
	char		*pchar;
	int		port;
	na_return_t	na_ret;
	int		rc = 0;

	D_ASSERT(mclset != NULL && na_class != NULL && na_addr != NULL);
	D_ASSERT(rank <= MCL_PS_SIZE_MAX);
	ctx_idx = tag % DTP_SRV_CONTEX_NUM;

	if (addr_lookup_table[rank].ae_tag_addrs[ctx_idx] != NULL) {
		*na_addr = addr_lookup_table[rank].ae_tag_addrs[ctx_idx];
		goto out;
	}

	if (addr_lookup_table[rank].ae_base_uri == NULL) {
		rc = mcl_lookup(mclset, rank, na_class, &tmp_addr);
		if (rc != MCL_SUCCESS) {
			D_ERROR("mcl_lookup failed, rc: %d.\n", rc);
			D_GOTO(out, rc = -DER_DTP_MCL);
		}
		D_ASSERT(mclset->cached[rank].visited != 0);
		D_ASSERT(tmp_addr != NULL);

		addr_lookup_table[rank].ae_base_uri = mclset->cached[rank].uri;
		addr_lookup_table[rank].ae_tag_addrs[0] = tmp_addr;
		if (ctx_idx == 0) {
			*na_addr = tmp_addr;
			D_GOTO(out, rc);
		}
	}

	/* calculate the ctx_idx's listening address and connect to it */
	strncpy(tmp_addrstr, addr_lookup_table[rank].ae_base_uri,
		DTP_ADDR_STR_MAX_LEN);
	pchar = strrchr(tmp_addrstr, ':');
	pchar++;
	port = atoi(pchar);
	port += ctx_idx;
	*pchar = '\0';
	snprintf(pchar, 16, "%d", port);
	D_DEBUG(DF_TP, "rank(%d), base uri(%s), tag(%d) uri(%s).\n",
		rank, addr_lookup_table[rank].ae_base_uri, tag, tmp_addrstr);

	na_ret = dtp_na_addr_lookup_wait(na_class, tmp_addrstr, &tmp_addr);
	if (na_ret == NA_SUCCESS) {
		D_DEBUG(DF_TP, "Connect to %s succeed.\n", tmp_addrstr);
		D_ASSERT(tmp_addr != NULL);
		addr_lookup_table[rank].ae_tag_addrs[ctx_idx] = tmp_addr;
		*na_addr = tmp_addr;
	} else {
		D_ERROR("Could not connect to %s, na_ret: %d.\n",
			tmp_addrstr, na_ret);
		D_GOTO(out, rc = -DER_DTP_MCL);
	}
out:
	return rc;
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

	rc = dtp_mcl_lookup(dtp_gdata.dg_mcl_srv_set, tgt_ep.ep_rank,
			    tgt_ep.ep_tag, hg_ctx->dhc_nacla,
			    &rpc_priv->drp_na_addr);
	if (rc != 0) {
		D_ERROR("dtp_mcl_lookup failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
		D_GOTO(out, rc);
	}

	hg_ret = HG_Create(hg_ctx->dhc_hgctx, rpc_priv->drp_na_addr,
			   DTP_HG_RPCID, &rpc_priv->drp_hg_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Create failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		rc = -DER_DTP_HG;
	}

out:
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

	hg_ret = HG_Destroy(rpc_priv->drp_hg_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Destroy failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
	}

	pthread_spin_destroy(&rpc_priv->drp_lock);
	D_FREE_PTR(rpc_priv);

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
	D_ASSERT(hg_cbinfo->type == HG_CB_FORWARD);

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
		hg_ret = HG_Get_output(hg_cbinfo->info.forward.handle,
				       &rpc_pub->dr_output);
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

	/* corresponding to the refcount taken in dtp_rpc_priv_init(). */
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
		dtp_req_decref(&rpc_priv->drp_pub);
		rc = -DER_DTP_HG;
	}


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
dtp_hg_progress(struct dtp_hg_context *hg_ctx, int64_t timeout)
{
	hg_context_t    *hg_context;
	hg_class_t      *hg_class;
	hg_return_t     hg_ret = HG_SUCCESS;
	unsigned int    count = 0;
	unsigned int	hg_timeout;
	int             rc;

	D_ASSERT(hg_ctx != NULL);
	hg_context = hg_ctx->dhc_hgctx;
	hg_class = hg_ctx->dhc_hgcla;
	D_ASSERT(hg_context != NULL && hg_class != NULL);

	/**
	 * Mercury only supports milli-second timeout and uses an unsigned int
	 */
	if (timeout < 0) {
		hg_timeout = UINT32_MAX;
	} else {
		hg_timeout = timeout / 1000;
		if (hg_timeout == 0)
			hg_timeout = 1;

	}

	/** execute pending callbacks, if any */
	hg_ret = HG_Trigger(hg_context, 0, UINT32_MAX, &count);
	if (hg_ret == HG_SUCCESS) {
		if (count > 0)
			/** some callbacks got executed, inform the caller */
			D_GOTO(out, rc = 0);
	} else if (hg_ret != HG_TIMEOUT) {
		D_ERROR("HG_Trigger failed, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/** progress RPC execution */
	hg_ret = HG_Progress(hg_context, timeout);
	if (hg_ret == HG_TIMEOUT)
		D_GOTO(out, rc = -DER_TIMEDOUT);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Progress failed, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* some RPCs have progressed, call HG_Trigger again */
	hg_ret = HG_Trigger(hg_context, 0, UINT32_MAX, &count);
	if (hg_ret != HG_TIMEOUT && hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Trigger failed, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	D_GOTO(out, rc = 0);
out:
	return rc;
}

#define DTP_HG_IOVN_STACK	(8)
int
dtp_hg_bulk_create(struct dtp_hg_context *hg_ctx, daos_sg_list_t *sgl,
		   dtp_bulk_perm_t bulk_perm, dtp_bulk_t *bulk_hdl)
{
	void		**buf_ptrs = NULL;
	void		*buf_ptrs_stack[DTP_HG_IOVN_STACK];
	hg_size_t	*buf_sizes = NULL;
	hg_size_t	buf_sizes_stack[DTP_HG_IOVN_STACK];
	hg_uint8_t	flags;
	hg_bulk_t	hg_bulk_hdl;
	hg_return_t	hg_ret = HG_SUCCESS;
	int		rc = 0, i, allocate;

	D_ASSERT(hg_ctx != NULL && hg_ctx->dhc_bulkcla != NULL);
	D_ASSERT(sgl != NULL && bulk_hdl != NULL);
	D_ASSERT(bulk_perm == DTP_BULK_RW || bulk_perm == DTP_BULK_RO);

	flags = (bulk_perm == DTP_BULK_RW) ? HG_BULK_READWRITE :
					     HG_BULK_READ_ONLY;

	if (sgl->sg_nr.num <= DTP_HG_IOVN_STACK) {
		allocate = 0;
		buf_sizes = buf_sizes_stack;
	} else {
		allocate = 1;
		D_ALLOC(buf_sizes, sgl->sg_nr.num * sizeof(hg_size_t));
		if (buf_sizes == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}
	for (i = 0; i < sgl->sg_nr.num; i++)
		buf_sizes[i] = sgl->sg_iovs[i].iov_buf_len;

	if (sgl->sg_iovs == NULL) {
		buf_ptrs = NULL;
	} else {
		if (allocate == 0) {
			buf_ptrs = buf_ptrs_stack;
		} else {
			D_ALLOC(buf_ptrs, sgl->sg_nr.num * sizeof(void *));
			if (buf_ptrs == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
		for (i = 0; i < sgl->sg_nr.num; i++)
			buf_ptrs[i] = sgl->sg_iovs[i].iov_buf;
	}

	hg_ret = HG_Bulk_create(hg_ctx->dhc_bulkcla, sgl->sg_nr.num, buf_ptrs,
				buf_sizes, flags, &hg_bulk_hdl);
	if (hg_ret == HG_SUCCESS) {
		*bulk_hdl = hg_bulk_hdl;
	} else {
		D_ERROR("HG_Bulk_create failed, hg_ret: %d.\n", hg_ret);
		rc = -DER_DTP_HG;
	}

out:
	/* HG_Bulk_create copied the parameters, can free here */
	if (allocate == 1 && buf_ptrs != NULL)
		D_FREE(buf_ptrs, sgl->sg_nr.num * sizeof(void *));
	if (allocate == 1 && buf_sizes != NULL)
		D_FREE(buf_sizes, sgl->sg_nr.num * sizeof(hg_size_t));

	return rc;
}

struct dtp_hg_bulk_cbinfo {
	struct dtp_bulk_desc	*bci_desc;
	dtp_bulk_cb_t		bci_cb;
	void			*bci_arg;
};

static hg_return_t
dtp_hg_bulk_transfer_cb(const struct hg_cb_info *hg_cbinfo)
{
	struct dtp_hg_bulk_cbinfo	*bulk_cbinfo;
	struct dtp_bulk_cb_info		dtp_bulk_cbinfo;
	struct dtp_hg_context		*hg_ctx;
	struct dtp_bulk_desc		*bulk_desc;
	hg_return_t			hg_ret = HG_SUCCESS;
	int				rc = 0;

	D_ASSERT(hg_cbinfo != NULL);
	bulk_cbinfo = (struct dtp_hg_bulk_cbinfo *)hg_cbinfo->arg;
	D_ASSERT(bulk_cbinfo != NULL);
	bulk_desc = bulk_cbinfo->bci_desc;
	D_ASSERT(bulk_desc != NULL);
	hg_ctx = (struct dtp_hg_context *)bulk_desc->bd_rpc->dr_ctx;
	D_ASSERT(hg_ctx != NULL);
	D_ASSERT(hg_cbinfo->type == HG_CB_BULK);
	D_ASSERT(hg_cbinfo->info.bulk.origin_handle ==
		 bulk_desc->bd_remote_hdl);
	D_ASSERT(hg_cbinfo->info.bulk.local_handle ==
		 bulk_desc->bd_local_hdl);

	if (hg_cbinfo->ret != HG_SUCCESS) {
		D_ERROR("dtp_hg_bulk_transfer_cb, hg_cbinfo->ret: %d.\n",
			hg_cbinfo->ret);
		hg_ret = hg_cbinfo->ret;
		rc = -DER_DTP_HG;
	}

	if (bulk_cbinfo->bci_cb == NULL) {
		D_DEBUG(DF_TP, "No bulk completion callback registered.\n");
		D_GOTO(out, hg_ret);
	}
	dtp_bulk_cbinfo.bci_arg = bulk_cbinfo->bci_arg;
	dtp_bulk_cbinfo.bci_rc = rc;
	dtp_bulk_cbinfo.bci_bulk_desc = bulk_desc;

	rc = bulk_cbinfo->bci_cb(&dtp_bulk_cbinfo);
	if (rc != 0)
		D_ERROR("bulk_cbinfo->bci_cb failed, rc: %d.\n", rc);

out:
	D_FREE_PTR(bulk_cbinfo);
	D_FREE_PTR(bulk_desc);
	return hg_ret;
}

int
dtp_hg_bulk_transfer(struct dtp_bulk_desc *bulk_desc, dtp_bulk_cb_t complete_cb,
		     void *arg, dtp_bulk_opid_t *opid)
{
	struct dtp_hg_context		*hg_ctx;
	struct dtp_hg_bulk_cbinfo	*bulk_cbinfo;
	hg_bulk_op_t			hg_bulk_op;
	struct dtp_bulk_desc		*bulk_desc_dup;
	struct dtp_rpc_priv		*rpc_priv;
	hg_return_t			hg_ret = HG_SUCCESS;
	int				rc = 0;

	D_ASSERT(bulk_desc != NULL && opid != NULL);
	D_ASSERT(bulk_desc->bd_bulk_op == DTP_BULK_PUT ||
		 bulk_desc->bd_bulk_op == DTP_BULK_GET);
	D_ASSERT(bulk_desc->bd_rpc != NULL);
	hg_ctx = (struct dtp_hg_context *)bulk_desc->bd_rpc->dr_ctx;
	D_ASSERT(hg_ctx != NULL && hg_ctx->dhc_bulkctx != NULL);

	D_ALLOC_PTR(bulk_cbinfo);
	if (bulk_cbinfo == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_PTR(bulk_desc_dup);
	if (bulk_desc_dup == NULL) {
		D_FREE_PTR(bulk_cbinfo);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	dtp_bulk_desc_dup(bulk_desc_dup, bulk_desc);

	bulk_cbinfo->bci_desc = bulk_desc_dup;
	bulk_cbinfo->bci_cb = complete_cb;
	bulk_cbinfo->bci_arg = arg;

	hg_bulk_op = (bulk_desc->bd_bulk_op == DTP_BULK_PUT) ?
		     HG_BULK_PUSH : HG_BULK_PULL;
	rpc_priv = container_of(bulk_desc->bd_rpc, struct dtp_rpc_priv,
				drp_pub);
	hg_ret = HG_Bulk_transfer(hg_ctx->dhc_bulkctx, dtp_hg_bulk_transfer_cb,
			bulk_cbinfo, hg_bulk_op, rpc_priv->drp_na_addr,
			bulk_desc->bd_remote_hdl, 0,
			bulk_desc->bd_local_hdl, 0,
			bulk_desc->bd_len, opid);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Bulk_transfer failed, hg_ret: %d.\n", hg_ret);
		D_FREE_PTR(bulk_cbinfo);
		D_FREE_PTR(bulk_desc_dup);
		rc = -DER_DTP_HG;
	}

out:
	return rc;
}
