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
 * This file is part of daos_transport. It implements the main interfaces to
 * mercury.
 */

#include <dtp_internal.h>
#include <abt.h>

static na_return_t
na_addr_lookup_cb(const struct na_cb_info *callback_info)
{
	na_addr_t	*addr_ptr = (na_addr_t *) callback_info->arg;
	na_return_t	ret = NA_SUCCESS;

	if (callback_info->ret != NA_SUCCESS) {
		NA_LOG_ERROR("Return from callback with %s error code",
		NA_Error_to_string(callback_info->ret));
		return ret;
	}

	*addr_ptr = callback_info->info.lookup.addr;

	return ret;
}

/* connection timeout 10 second */
#define DTP_CONNECT_TIMEOUT_SEC		(10)

na_return_t
dtp_na_addr_lookup_wait(na_class_t *na_class, const char *name, na_addr_t *addr)
{
	na_addr_t		new_addr = NULL;
	na_context_t		*context = NULL;
	uint64_t		now;
	uint64_t		end;
	unsigned int		prog_msec;
	na_return_t		ret = NA_SUCCESS;

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
		ret = NA_PROTOCOL_ERROR;
		goto done;
	}

	ret = NA_Addr_lookup(na_class, context, &na_addr_lookup_cb, &new_addr,
			     name, NA_OP_ID_IGNORE);
        if (ret != NA_SUCCESS) {
		NA_LOG_ERROR("Could not start NA_Addr_lookup");
		goto done;
	}

	end = dtp_time_usec(DTP_CONNECT_TIMEOUT_SEC);
	prog_msec = 1;

	while (1) {
		na_return_t	trigger_ret;
		unsigned int	actual_count = 0;

		do {
			trigger_ret = NA_Trigger(context, 0, 1, &actual_count);
		} while ((trigger_ret == NA_SUCCESS) && actual_count);

		if (new_addr != NULL) {
			*addr = new_addr;
			break;
		}

		ret = NA_Progress(na_class, context, prog_msec);
		if (ret != NA_SUCCESS && ret != NA_TIMEOUT) {
			NA_LOG_ERROR("Could not make progress");
			break;
		}

		now = dtp_time_usec(0);
		if (now >= end) {
			char		my_host[DTP_ADDR_STR_MAX_LEN] = {'\0'};
			daos_rank_t	my_rank;

			dtp_group_rank(0, &my_rank);
			gethostname(my_host, DTP_ADDR_STR_MAX_LEN);

			D_ERROR("Could not connect to %s within %d second "
				"(rank %d, host %s).\n", name,
				DTP_CONNECT_TIMEOUT_SEC, my_rank, my_host);
			ret = NA_TIMEOUT;
			break;
		}

		if (prog_msec <= 512)
			prog_msec = prog_msec << 1;
	}

	NA_Context_destroy(na_class, context);

done:
	if (new_addr == NULL)
		D_ASSERT(ret != NA_SUCCESS);
	return ret;
}

static int
na_class_get_addr(na_class_t *na_class, char *addr_str, daos_size_t *str_size)
{
	na_addr_t	self_addr;
	na_return_t	na_ret;
	int		rc = 0;

	D_ASSERT(na_class != NULL);
	D_ASSERT(addr_str != NULL && str_size != NULL);

	na_ret = NA_Addr_self(na_class, &self_addr);
	if (na_ret != NA_SUCCESS) {
		D_ERROR("NA_Addr_self failed, na_ret: %d.\n", na_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	na_ret = NA_Addr_to_string(na_class, addr_str, str_size, self_addr);
	if (na_ret != NA_SUCCESS) {
		D_ERROR("NA_Addr_to_string failed, na_ret: %d.\n",
			na_ret);
		NA_Addr_free(na_class, self_addr);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	NA_Addr_free(na_class, self_addr);

out:
	return rc;
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
			info_string = "cci+verbs://";
		else
			info_string = "cci+tcp://";
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
		char		addr_str[DTP_ADDR_STR_MAX_LEN] = {'\0'};
		daos_size_t	str_size = DTP_ADDR_STR_MAX_LEN;

		rc = na_class_get_addr(na_class, addr_str, &str_size);
		if (rc != 0) {
			D_ERROR("na_class_get_addr failed, rc: %d.\n", rc);
			HG_Finalize(hg_class);
			NA_Context_destroy(na_class, na_context);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		*addr = strdup(addr_str);
		if (*addr == NULL) {
			D_ERROR("strdup failed, rc: %d.\n", rc);
			HG_Finalize(hg_class);
			NA_Context_destroy(na_class, na_context);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
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
		char		addr_str[DTP_ADDR_STR_MAX_LEN] = {'\0'};
		daos_size_t	str_size = DTP_ADDR_STR_MAX_LEN;

		if (dtp_gdata.dg_verbs == true)
			info_string = "cci+verbs://";
		else
			info_string = "cci+tcp://";

		na_class = NA_Initialize(info_string, dtp_gdata.dg_server);
		if (na_class == NULL) {
			D_ERROR("Could not initialize NA class.\n");
			D_GOTO(out, rc = -DER_DTP_HG);
		}

		rc = na_class_get_addr(na_class, addr_str, &str_size);
		if (rc != 0) {
			D_ERROR("na_class_get_addr failed, rc: %d.\n", rc);
			NA_Finalize(na_class);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
		D_DEBUG(DF_TP, "New context(idx:%d), listen address: cci+%s.\n",
			idx, addr_str);

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

	hg_ctx->dhc_hgctx = hg_context;
	/* TODO: need to create separate bulk class and bulk context? */
	hg_ctx->dhc_bulkcla = hg_ctx->dhc_hgcla;
	hg_ctx->dhc_bulkctx = hg_ctx->dhc_hgctx;
	D_ASSERT(hg_ctx->dhc_bulkcla != NULL);
	D_ASSERT(hg_ctx->dhc_bulkctx != NULL);

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

struct dtp_context *
dtp_hg_context_lookup(hg_context_t *hg_ctx)
{
	struct dtp_context	*dtp_ctx;
	int			found = 0;

	pthread_rwlock_rdlock(&dtp_gdata.dg_rwlock);

	daos_list_for_each_entry(dtp_ctx, &dtp_gdata.dg_ctx_list, dc_link) {
		if (dtp_ctx->dc_hg_ctx.dhc_hgctx == hg_ctx) {
			found = 1;
			break;
		}
	}

	pthread_rwlock_unlock(&dtp_gdata.dg_rwlock);

	return (found == 1) ? dtp_ctx : NULL;
}

static void
dtp_handle_rpc(void *arg)
{
	struct dtp_rpc_priv	*rpc_priv = arg;
	dtp_rpc_t		*rpc_pub;

	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(rpc_priv->drp_opc_info != NULL);
	D_ASSERT(rpc_priv->drp_opc_info->doi_rpc_cb != NULL);
	rpc_pub = &rpc_priv->drp_pub;
	rpc_priv->drp_opc_info->doi_rpc_cb(rpc_pub);
	dtp_req_decref(rpc_pub);
}

int
dtp_rpc_handler_common(hg_handle_t hg_hdl)
{
	struct dtp_context	*dtp_ctx;
	struct dtp_hg_context	*hg_ctx;
	struct hg_info		*hg_info;
	struct dtp_rpc_priv	*rpc_priv;
	dtp_rpc_t		*rpc_pub;
	dtp_opcode_t		opc;
	dtp_proc_t		proc = NULL;
	struct dtp_opc_info	*opc_info = NULL;
	hg_return_t		hg_ret = HG_SUCCESS;
	int			rc = 0;

	hg_info = HG_Get_info(hg_hdl);
	if (hg_info == NULL) {
		D_ERROR("HG_Get_info failed.\n");
		D_GOTO(out, hg_ret = HG_PROTOCOL_ERROR);
	}

	dtp_ctx = dtp_hg_context_lookup(hg_info->context);
	if (dtp_ctx == NULL) {
		D_ERROR("dtp_hg_context_lookup failed.\n");
		D_GOTO(out, hg_ret = HG_PROTOCOL_ERROR);
	}
	hg_ctx = &dtp_ctx->dc_hg_ctx;
	D_ASSERT(hg_ctx->dhc_hgcla == hg_info->hg_class);

	D_ALLOC_PTR(rpc_priv);
	if (rpc_priv == NULL)
		D_GOTO(out, hg_ret = HG_NOMEM_ERROR);

	rpc_priv->drp_na_addr = hg_info->addr;
	rpc_priv->drp_hg_hdl = hg_hdl;
	rpc_pub = &rpc_priv->drp_pub;
	rpc_pub->dr_ctx = dtp_ctx;
	D_ASSERT(rpc_pub->dr_input == NULL);

	rc = dtp_hg_unpack_header(rpc_priv, &proc);
	if (rc != 0) {
		D_ERROR("dtp_hg_unpack_header failed, rc: %d.\n", rc);
		D_FREE_PTR(rpc_priv);
		D_GOTO(out, hg_ret = HG_OTHER_ERROR);
	}
	D_ASSERT(proc != NULL);
	opc = rpc_priv->drp_req_hdr.dch_opc;

	/* D_DEBUG(DF_TP,"in dtp_rpc_handler_common, opc: 0x%x.\n", opc); */
	opc_info = dtp_opc_lookup(dtp_gdata.dg_opc_map, opc, DTP_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: 0x%x, lookup failed.\n", opc);
		D_FREE_PTR(rpc_priv);
		dtp_hg_unpack_cleanup(proc);
		D_GOTO(out, hg_ret = HG_NO_MATCH);
	}
	D_ASSERT(opc_info->doi_opc == opc);
	rpc_priv->drp_opc_info = opc_info;

	/* D_DEBUG(DF_TP,"in dtp_rpc_handler_common, opc: 0x%x.\n", opc); */
	D_ASSERT(opc_info->doi_input_size <= DTP_MAX_INPUT_SIZE &&
		 opc_info->doi_output_size <= DTP_MAX_OUTPUT_SIZE);

	dtp_rpc_priv_init(rpc_priv, dtp_ctx, opc, 1);

	rc = dtp_rpc_inout_buff_init(rpc_pub);
	if (rc != 0) {
		D_ERROR("dtp_rpc_inout_buff_init faied, rc: %d, opc: 0x%x.\n",
			rc, opc);
		dtp_hg_unpack_cleanup(proc);
		D_GOTO(decref, hg_ret = HG_NOMEM_ERROR);
	}

	D_ASSERT(rpc_priv->drp_srv != 0);
	D_ASSERT(opc_info->doi_input_size == rpc_pub->dr_input_size);
	if (rpc_pub->dr_input_size > 0) {
		D_ASSERT(rpc_pub->dr_input != NULL);
		D_ASSERT(opc_info->doi_drf != NULL);
		/* corresponding to HG_Free_input in dtp_hg_req_destroy */
		rc = dtp_hg_unpack_body(rpc_priv, proc);
		if (rc == 0) {
			rpc_priv->drp_input_got = 1;
			uuid_copy(rpc_pub->dr_ep.ep_grp_id,
				  rpc_priv->drp_req_hdr.dch_grp_id);
			rpc_pub->dr_ep.ep_rank =
					rpc_priv->drp_req_hdr.dch_rank;
		} else {
			D_ERROR("_unpack_body failed, rc: %d, opc: 0x%x.\n",
				hg_ret, rpc_pub->dr_opc);
			D_GOTO(decref, hg_ret = HG_OTHER_ERROR);
		}
	} else {
		dtp_hg_unpack_cleanup(proc);
	}

	if (opc_info->doi_rpc_cb != NULL) {
		if (dtp_ctx->dc_pool != NULL) {
			rc = ABT_thread_create(*(ABT_pool *)dtp_ctx->dc_pool,
					       dtp_handle_rpc, rpc_priv,
					       ABT_THREAD_ATTR_NULL, NULL);
		} else {
			rc = opc_info->doi_rpc_cb(rpc_pub);
			if (rc != 0)
				D_ERROR("doi_rpc_cb failed, rc: %d, "
					"opc: 0x%x.\n", rc, opc);
		}
	} else {
		D_ERROR("NULL drp_hg_hdl, opc: 0x%x.\n", opc);
		hg_ret = HG_NO_MATCH;
		rc = -DER_DTP_UNREG;
	}

decref:
	/* if ABT enabled and the ULT created successfully, the dtp_handle_rpc
	 * will decref it. */
	if (rc != 0 || dtp_ctx->dc_pool == NULL) {
		int rc1;

		rc1 = dtp_req_decref(rpc_pub);
		if (rc1 != 0)
			D_ERROR("dtp_req_decref failed, rc: %d.\n", rc1);
	}
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

	if (tag >= DTP_SRV_CONTEX_NUM) {
		D_ERROR("invalid tag %d (DTP_SRV_CONTEX_NUM %d).\n",
			tag, DTP_SRV_CONTEX_NUM);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_ASSERT(mclset != NULL && na_class != NULL && na_addr != NULL);
	D_ASSERT(rank <= MCL_PS_SIZE_MAX);
	ctx_idx = tag;

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
		if (hg_cbinfo->ret == HG_CANCELED) {
			D_DEBUG(DF_TP, "request being canceled, opx: 0x%x.\n",
				opc);
			rc = -DER_CANCELED;
		} else {
			D_ERROR("hg_cbinfo->ret: %d.\n", hg_cbinfo->ret);
			rc = -DER_DTP_HG;
			hg_ret = hg_cbinfo->ret;
		}
	}

	if (req_cbinfo->rsc_cb == NULL) {
		rpc_priv->drp_state = (hg_cbinfo->ret == HG_CANCELED) ?
				      RPC_CANCELED : RPC_COMPLETED;
		D_GOTO(out, hg_ret);
	}

	if (rc == 0) {
		rpc_priv->drp_state = RPC_REPLY_RECVED;
		/* HG_Free_output in dtp_hg_req_destroy */
		hg_ret = HG_Get_output(hg_cbinfo->info.forward.handle,
				       &rpc_pub->dr_output);
		if (hg_ret == HG_SUCCESS) {
			rpc_priv->drp_output_got = 1;
		} else {
			D_ERROR("HG_Get_output failed, hg_ret: %d, opc: "
				"0x%x.\n", hg_ret, opc);
			rc = -DER_DTP_HG;
		}
	}

	dtp_cbinfo.dci_rpc = rpc_pub;
	dtp_cbinfo.dci_arg = req_cbinfo->rsc_arg;
	dtp_cbinfo.dci_rc = rc;


	D_ASSERT(req_cbinfo->rsc_cb != NULL);
	rc = req_cbinfo->rsc_cb(&dtp_cbinfo);
	if (rc != 0)
		D_ERROR("req_cbinfo->rsc_cb returned %d.\n", rc);

	rpc_priv->drp_state = (hg_cbinfo->ret == HG_CANCELED) ?
			      RPC_CANCELED : RPC_COMPLETED;

out:
	D_FREE_PTR(req_cbinfo);

	dtp_context_req_untrack(rpc_pub);
	/* corresponding to the refcount taken in dtp_rpc_priv_init(). */
	rc = dtp_req_decref(rpc_pub);
	if (rc != 0)
		D_ERROR("dtp_req_decref failed, rc: %d, opc: 0x%x.\n", rc, opc);

	return hg_ret;
}

int
dtp_hg_req_send(struct dtp_rpc_priv *rpc_priv)
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
	cb_info->rsc_cb = rpc_priv->drp_complete_cb;
	cb_info->rsc_arg = rpc_priv->drp_arg;

	hg_ret = HG_Forward(rpc_priv->drp_hg_hdl, dtp_hg_req_send_cb, cb_info,
			    hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Forward failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		D_FREE_PTR(cb_info);
		rc = -DER_DTP_HG;
	}


out:
	return rc;
}

int
dtp_hg_req_cancel(struct dtp_rpc_priv *rpc_priv)
{
	hg_return_t	hg_ret;
	int		rc = 0;

	D_ASSERT(rpc_priv != NULL);
	if (!rpc_priv->drp_hg_hdl)
		D_GOTO(out, rc = -DER_INVAL);

	hg_ret = HG_Cancel(rpc_priv->drp_hg_hdl);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("dtp_hg_req_cancel failed, hg_ret: %d, opc: 0x%x.\n",
			hg_ret, rpc_priv->drp_pub.dr_opc);
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

static int
dtp_hg_trigger(struct dtp_hg_context *hg_ctx)
{
	struct dtp_context	*dtp_ctx;
	hg_context_t		*hg_context;
	hg_return_t		hg_ret = HG_SUCCESS;
	unsigned int		count = 0;

	D_ASSERT(hg_ctx != NULL);
	hg_context = hg_ctx->dhc_hgctx;
	dtp_ctx = container_of(hg_ctx, struct dtp_context, dc_hg_ctx);

	do {
		hg_ret = HG_Trigger(hg_context, 0, UINT32_MAX, &count);
	} while (hg_ret == HG_SUCCESS && count > 0);

	if (hg_ret != HG_TIMEOUT) {
		D_ERROR("HG_Trigger failed, hg_ret: %d.\n", hg_ret);
		return -DER_DTP_HG;
	}

	/**
	 * XXX Let's yield to other process anyway, but there
	 * maybe better strategy when there are more use cases
	 */
	if (dtp_ctx->dc_pool != NULL)
		ABT_thread_yield();

	return 0;
}

int
dtp_hg_progress(struct dtp_hg_context *hg_ctx, int64_t timeout)
{
	hg_context_t		*hg_context;
	hg_class_t		*hg_class;
	hg_return_t		hg_ret = HG_SUCCESS;
	unsigned int		hg_timeout;
	int			rc;

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

	rc = dtp_hg_trigger(hg_ctx);
	if (rc != 0)
		return rc;

	/** progress RPC execution */
	hg_ret = HG_Progress(hg_context, hg_timeout);
	if (hg_ret == HG_TIMEOUT)
		D_GOTO(out, rc = -DER_TIMEDOUT);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Progress failed, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* some RPCs have progressed, call Trigger again */
	rc = dtp_hg_trigger(hg_ctx);

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

int
dtp_hg_bulk_access(dtp_bulk_t bulk_hdl, daos_sg_list_t *sgl)
{
	unsigned int	bulk_sgnum;
	unsigned int	actual_sgnum;
	daos_size_t	bulk_len;
	void		**buf_ptrs = NULL;
	void		*buf_ptrs_stack[DTP_HG_IOVN_STACK];
	hg_size_t	*buf_sizes = NULL;
	hg_size_t	buf_sizes_stack[DTP_HG_IOVN_STACK];
	hg_bulk_t	hg_bulk_hdl;
	hg_return_t	hg_ret = HG_SUCCESS;
	int		rc = 0, i, allocate = 0;

	D_ASSERT(bulk_hdl != DTP_BULK_NULL && sgl != NULL);

	rc = dtp_bulk_get_sgnum(bulk_hdl, &bulk_sgnum);
	if (rc != 0) {
		D_ERROR("dtp_bulk_get_sgnum failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	rc = dtp_bulk_get_len(bulk_hdl, &bulk_len);
	if (rc != 0) {
		D_ERROR("dtp_bulk_get_len failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	if (sgl->sg_nr.num < bulk_sgnum) {
		D_DEBUG(DF_TP, "sgl->sg_nr.num (%d) too small, %d required.\n",
			sgl->sg_nr.num, bulk_sgnum);
		sgl->sg_nr.num_out = bulk_sgnum;
		D_GOTO(out, rc = -DER_TRUNC);
	}

	if (bulk_sgnum <= DTP_HG_IOVN_STACK) {
		allocate = 0;
		buf_sizes = buf_sizes_stack;
		buf_ptrs = buf_ptrs_stack;
	} else {
		allocate = 1;
		D_ALLOC(buf_sizes, bulk_sgnum * sizeof(hg_size_t));
		if (buf_sizes == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_ALLOC(buf_ptrs, bulk_sgnum * sizeof(void *));
		if (buf_sizes == NULL) {
			D_FREE(buf_sizes, bulk_sgnum * sizeof(hg_size_t));
			D_GOTO(out, rc = -DER_NOMEM);
		}
	}

	hg_bulk_hdl = bulk_hdl;
	hg_ret = HG_Bulk_access(hg_bulk_hdl, 0, bulk_len, HG_BULK_READWRITE,
				bulk_sgnum, buf_ptrs, buf_sizes, &actual_sgnum);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Bulk_access failed, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	D_ASSERT(actual_sgnum == bulk_sgnum);

	for (i = 0; i < bulk_sgnum; i++) {
		sgl->sg_iovs[i].iov_buf = buf_ptrs[i];
		sgl->sg_iovs[i].iov_buf_len = buf_sizes[i];
		sgl->sg_iovs[i].iov_len = buf_sizes[i];
	}
	sgl->sg_nr.num_out = bulk_sgnum;

out:
	if (allocate) {
		D_FREE(buf_sizes, bulk_sgnum * sizeof(hg_size_t));
		D_FREE(buf_ptrs, bulk_sgnum * sizeof(void *));
	}
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
	struct dtp_context		*ctx;
	struct dtp_hg_context		*hg_ctx;
	struct dtp_bulk_desc		*bulk_desc;
	hg_return_t			hg_ret = HG_SUCCESS;
	int				rc = 0;

	D_ASSERT(hg_cbinfo != NULL);
	bulk_cbinfo = (struct dtp_hg_bulk_cbinfo *)hg_cbinfo->arg;
	D_ASSERT(bulk_cbinfo != NULL);
	bulk_desc = bulk_cbinfo->bci_desc;
	D_ASSERT(bulk_desc != NULL);
	ctx = (struct dtp_context *)bulk_desc->bd_rpc->dr_ctx;
	hg_ctx = &ctx->dc_hg_ctx;
	D_ASSERT(hg_ctx != NULL);
	D_ASSERT(hg_cbinfo->type == HG_CB_BULK);
	D_ASSERT(hg_cbinfo->info.bulk.origin_handle ==
		 bulk_desc->bd_remote_hdl);
	D_ASSERT(hg_cbinfo->info.bulk.local_handle ==
		 bulk_desc->bd_local_hdl);

	if (hg_cbinfo->ret != HG_SUCCESS) {
		if (hg_cbinfo->ret == HG_CANCELED) {
			D_DEBUG(DF_TP, "bulk transferring canceled.\n");
			rc = -DER_CANCELED;
		} else {
			D_ERROR("dtp_hg_bulk_transfer_cb,hg_cbinfo->ret: %d.\n",
				hg_cbinfo->ret);
			hg_ret = hg_cbinfo->ret;
			rc = -DER_DTP_HG;
		}
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
	struct dtp_context		*ctx;
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
	ctx = (struct dtp_context *)bulk_desc->bd_rpc->dr_ctx;
	hg_ctx = &ctx->dc_hg_ctx;
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
			bulk_desc->bd_remote_hdl, bulk_desc->bd_remote_off,
			bulk_desc->bd_local_hdl, bulk_desc->bd_local_off,
			bulk_desc->bd_len, (hg_op_id_t *)opid);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Bulk_transfer failed, hg_ret: %d.\n", hg_ret);
		D_FREE_PTR(bulk_cbinfo);
		D_FREE_PTR(bulk_desc_dup);
		rc = -DER_DTP_HG;
	}

out:
	return rc;
}
