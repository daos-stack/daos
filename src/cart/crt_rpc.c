/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the main RPC routines.
 */
#define D_LOGFAC	DD_FAC(rpc)

#include <semaphore.h>

#include "crt_internal.h"

#define CRT_CTL_MAX_LOG_MSG_SIZE 256

void
crt_hdlr_ctl_fi_toggle(crt_rpc_t *rpc_req)
{
	struct crt_ctl_fi_toggle_in	*in_args;
	struct crt_ctl_fi_toggle_out	*out_args;
	int				 rc = 0;

	in_args = crt_req_get(rpc_req);
	out_args = crt_reply_get(rpc_req);

	if (in_args->op)
		rc = d_fault_inject_enable();
	else
		rc = d_fault_inject_disable();

	out_args->rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed. rc: %d\n", rc);
}

void
crt_hdlr_ctl_log_add_msg(crt_rpc_t *rpc_req)
{
	struct crt_ctl_log_add_msg_in	*in_args;
	struct crt_ctl_log_add_msg_out	*out_args;
	int				rc = 0;

	in_args = crt_req_get(rpc_req);
	out_args = crt_reply_get(rpc_req);

	if (in_args->log_msg == NULL) {
		D_ERROR("Empty log message\n");
		rc = -DER_INVAL;
	} else {
		D_EMIT("%.*s\n", CRT_CTL_MAX_LOG_MSG_SIZE,
		       in_args->log_msg);
	}

	out_args->rc = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed. rc: %d\n", rc);
}

void
crt_hdlr_ctl_log_set(crt_rpc_t *rpc_req)
{
	struct crt_ctl_log_set_in	*in_args;
	struct crt_ctl_log_set_out	*out_args;
	int				rc = 0;

	in_args = crt_req_get(rpc_req);
	out_args = crt_reply_get(rpc_req);

	out_args->rc = rc;

	d_log_setmasks(in_args->log_mask, -1);
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed. rc: %d\n", rc);
}

void
crt_hdlr_ctl_fi_attr_set(crt_rpc_t *rpc_req)
{
	struct crt_ctl_fi_attr_set_in	*in_args_fi_attr;
	struct crt_ctl_fi_attr_set_out	*out_args_fi_attr;
	struct d_fault_attr_t		 fa_in = {0};
	int				 rc;

	in_args_fi_attr = crt_req_get(rpc_req);
	out_args_fi_attr = crt_reply_get(rpc_req);

	fa_in.fa_max_faults = in_args_fi_attr->fa_max_faults;
	fa_in.fa_probability_x = in_args_fi_attr->fa_probability_x;
	fa_in.fa_probability_y = in_args_fi_attr->fa_probability_y;
	fa_in.fa_err_code = in_args_fi_attr->fa_err_code;
	fa_in.fa_interval = in_args_fi_attr->fa_interval;

	rc = d_fault_attr_set(in_args_fi_attr->fa_fault_id, fa_in);
	if (rc != 0)
		D_ERROR("d_fault_attr_set() failed. rc: %d\n", rc);

	out_args_fi_attr->fa_ret = rc;
	rc = crt_reply_send(rpc_req);
	if (rc != 0)
		D_ERROR("crt_reply_send() failed. rc: %d\n", rc);
}

/* CRT internal RPC format definitions uri lookup */
CRT_RPC_DEFINE(crt_uri_lookup, CRT_ISEQ_URI_LOOKUP, CRT_OSEQ_URI_LOOKUP)

/* for self-test service */
CRT_RPC_DEFINE(crt_st_send_id_reply_iov,
	       CRT_ISEQ_ST_SEND_ID, CRT_OSEQ_ST_REPLY_IOV)

CRT_RPC_DEFINE(crt_st_send_iov_reply_empty,
	       CRT_ISEQ_ST_SEND_ID_IOV, CRT_OSEQ_ST_REPLY_EMPTY)

CRT_RPC_DEFINE(crt_st_both_iov,
	       CRT_ISEQ_ST_SEND_ID_IOV, CRT_OSEQ_ST_REPLY_IOV)

CRT_RPC_DEFINE(crt_st_send_iov_reply_bulk,
	       CRT_ISEQ_ST_SEND_ID_IOV_BULK, CRT_OSEQ_ST_REPLY_EMPTY)

CRT_RPC_DEFINE(crt_st_send_bulk_reply_iov,
	       CRT_ISEQ_ST_SEND_ID_BULK, CRT_OSEQ_ST_REPLY_IOV)

CRT_RPC_DEFINE(crt_st_both_bulk,
	       CRT_ISEQ_ST_SEND_ID_BULK, CRT_OSEQ_ST_REPLY_EMPTY)

CRT_RPC_DEFINE(crt_st_open_session,
	       CRT_ISEQ_ST_SEND_SESSION, CRT_OSEQ_ST_REPLY_ID)

CRT_RPC_DEFINE(crt_st_close_session,
	       CRT_ISEQ_ST_SEND_ID, CRT_OSEQ_ST_REPLY_EMPTY)

CRT_RPC_DEFINE(crt_st_start, CRT_ISEQ_ST_START, CRT_OSEQ_ST_START)

CRT_RPC_DEFINE(crt_st_status_req,
	       CRT_ISEQ_ST_STATUS_REQ, CRT_OSEQ_ST_STATUS_REQ)

CRT_RPC_DEFINE(crt_iv_fetch, CRT_ISEQ_IV_FETCH, CRT_OSEQ_IV_FETCH)

CRT_RPC_DEFINE(crt_iv_update, CRT_ISEQ_IV_UPDATE, CRT_OSEQ_IV_UPDATE)

CRT_RPC_DEFINE(crt_iv_sync, CRT_ISEQ_IV_SYNC, CRT_OSEQ_IV_SYNC)

static struct crt_corpc_ops crt_iv_sync_co_ops = {
	.co_aggregate = crt_iv_sync_corpc_aggregate,
	.co_pre_forward = crt_iv_sync_corpc_pre_forward,
};

CRT_GEN_PROC_FUNC(crt_grp_cache, CRT_SEQ_GRP_CACHE);

static int
crt_proc_struct_crt_grp_cache(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct crt_grp_cache *data)
{
	return crt_proc_crt_grp_cache(proc, data);
}

/* !! All of the following 4 RPC definition should have the same input fields !!
 * All of them are verified in one function:
 * int verify_ctl_in_args(struct crt_ctl_ep_ls_in *in_args)
 */
CRT_RPC_DEFINE(crt_ctl_get_uri_cache, CRT_ISEQ_CTL, CRT_OSEQ_CTL_GET_URI_CACHE)
CRT_RPC_DEFINE(crt_ctl_ep_ls,         CRT_ISEQ_CTL, CRT_OSEQ_CTL_EP_LS)
CRT_RPC_DEFINE(crt_ctl_get_host,      CRT_ISEQ_CTL, CRT_OSEQ_CTL_GET_HOST)
CRT_RPC_DEFINE(crt_ctl_get_pid,       CRT_ISEQ_CTL, CRT_OSEQ_CTL_GET_PID)

CRT_RPC_DEFINE(crt_proto_query, CRT_ISEQ_PROTO_QUERY, CRT_OSEQ_PROTO_QUERY)

CRT_RPC_DEFINE(crt_ctl_fi_attr_set, CRT_ISEQ_CTL_FI_ATTR_SET,
	       CRT_OSEQ_CTL_FI_ATTR_SET)
CRT_RPC_DEFINE(crt_ctl_fi_toggle, CRT_ISEQ_CTL_FI_TOGGLE,
	       CRT_OSEQ_CTL_FI_TOGGLE)

CRT_RPC_DEFINE(crt_ctl_log_set, CRT_ISEQ_CTL_LOG_SET, CRT_OSEQ_CTL_LOG_SET)
CRT_RPC_DEFINE(crt_ctl_log_add_msg, CRT_ISEQ_CTL_LOG_ADD_MSG,
	       CRT_OSEQ_CTL_LOG_ADD_MSG)

/* Define for crt_internal_rpcs[] array population below.
 * See CRT_INTERNAL_RPCS_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags = b,		\
	.prf_req_fmt = c,	\
	.prf_hdlr = d,		\
	.prf_co_ops = e,	\
},

static struct crt_proto_rpc_format crt_internal_rpcs[] = {
	CRT_INTERNAL_RPCS_LIST
};

static struct crt_proto_rpc_format crt_fi_rpcs[] = {
	CRT_FI_RPCS_LIST
};

static struct crt_proto_rpc_format crt_st_rpcs[] = {
	CRT_ST_RPCS_LIST
};

static struct crt_proto_rpc_format crt_ctl_rpcs[] = {
	CRT_CTL_RPCS_LIST
};

static struct crt_proto_rpc_format crt_iv_rpcs[] = {
	CRT_IV_RPCS_LIST
};

#undef X

#define X(a, b, c, d, e) case a: return #a;

/* Helper function to convert internally registered RPC opc to str */
char
*crt_opc_to_str(crt_opcode_t opc)
{
	if (crt_opc_is_swim(opc))
		return "SWIM";

	switch (opc) {
		CRT_INTERNAL_RPCS_LIST
		CRT_FI_RPCS_LIST
		CRT_IV_RPCS_LIST
		CRT_ST_RPCS_LIST
		CRT_CTL_RPCS_LIST
	}
	return "DAOS";
}

#undef X

/* CRT RPC related APIs or internal functions */
int
crt_internal_rpc_register(bool server)
{
	struct crt_proto_format	cpf;
	int			rc;

	cpf.cpf_name  = "internal";
	cpf.cpf_ver   = CRT_PROTO_INTERNAL_VERSION;
	cpf.cpf_count = ARRAY_SIZE(crt_internal_rpcs);
	cpf.cpf_prf   = crt_internal_rpcs;
	cpf.cpf_base  = CRT_OPC_INTERNAL_BASE;

	rc = crt_proto_register_internal(&cpf);
	if (rc != 0) {
		D_ERROR("crt_proto_register_internal() failed, "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	/* TODO: The self-test protocols should not be registered on the client
	 * by default.
	 */

	cpf.cpf_name  = "self-test";
	cpf.cpf_ver   = CRT_PROTO_ST_VERSION;
	cpf.cpf_count = ARRAY_SIZE(crt_st_rpcs);
	cpf.cpf_prf   = crt_st_rpcs;
	cpf.cpf_base  = CRT_OPC_ST_BASE;

	rc = crt_proto_register(&cpf);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (!server)
		return -DER_SUCCESS;

	cpf.cpf_name  = "fault-injection";
	cpf.cpf_ver   = CRT_PROTO_FI_VERSION;
	cpf.cpf_count = ARRAY_SIZE(crt_fi_rpcs);
	cpf.cpf_prf   = crt_fi_rpcs;
	cpf.cpf_base  = CRT_OPC_FI_BASE;

	rc = crt_proto_register(&cpf);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	cpf.cpf_name  = "ctl";
	cpf.cpf_ver   = CRT_PROTO_CTL_VERSION;
	cpf.cpf_count = ARRAY_SIZE(crt_ctl_rpcs);
	cpf.cpf_prf   = crt_ctl_rpcs;
	cpf.cpf_base  = CRT_OPC_CTL_BASE;

	rc = crt_proto_register(&cpf);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	cpf.cpf_name  = "incast";
	cpf.cpf_ver   = CRT_PROTO_IV_VERSION;
	cpf.cpf_count = ARRAY_SIZE(crt_iv_rpcs);
	cpf.cpf_prf   = crt_iv_rpcs;
	cpf.cpf_base  = CRT_OPC_IV_BASE;

	rc = crt_proto_register(&cpf);
	if (rc != 0)
		D_ERROR("crt_proto_register() failed, "DF_RC"\n", DP_RC(rc));

	return rc;
}

struct crt_pfi {
	int	pfi_ver;
	int	pfi_rc;
	sem_t	pfi_sem;
};

static void
crt_pfi_cb(struct crt_proto_query_cb_info *cb_info)
{
	struct crt_pfi *pfi = cb_info->pq_arg;

	pfi->pfi_rc = cb_info->pq_rc;
	pfi->pfi_ver = cb_info->pq_ver;
	sem_post(&pfi->pfi_sem);
}

/* Register the FI protocol against an endpoint.
 * This is from client code, so pass in an endpoint, and query the target for
 * what version it supports.  The client only supports one version so abort
 * if there is any error.
 */
int
crt_register_proto_fi(crt_endpoint_t *ep)
{
	struct crt_proto_format cpf;
	struct crt_pfi	pfi = {};
	int		rc;

	cpf.cpf_name  = "fault-injection";
	cpf.cpf_ver   = CRT_PROTO_FI_VERSION;
	cpf.cpf_count = ARRAY_SIZE(crt_fi_rpcs);
	cpf.cpf_prf   = crt_fi_rpcs;
	cpf.cpf_base  = CRT_OPC_FI_BASE;

	rc = sem_init(&pfi.pfi_sem, 0, 0);
	if (rc != 0)
		return -DER_MISC;

	rc = crt_proto_query(ep, cpf.cpf_base, &cpf.cpf_ver,
			     1, crt_pfi_cb, &pfi);
	if (rc != -DER_SUCCESS)
		D_GOTO(out, rc);

	sem_wait(&pfi.pfi_sem);

	if (pfi.pfi_rc != -DER_SUCCESS)
		D_GOTO(out, rc = pfi.pfi_rc);

	if (pfi.pfi_ver != cpf.cpf_ver)
		D_GOTO(out, rc = -DER_MISMATCH);

	rc = crt_proto_register(&cpf);
	if (rc != 0)
		D_ERROR("crt_proto_register() failed, "DF_RC"\n", DP_RC(rc));

out:
	sem_destroy(&pfi.pfi_sem);

	return rc;
}

int
crt_register_proto_ctl(crt_endpoint_t *ep)
{
	struct crt_proto_format cpf;
	struct crt_pfi	pfi = {};
	int		rc;

	cpf.cpf_name  = "ctl";
	cpf.cpf_ver   = CRT_PROTO_CTL_VERSION;
	cpf.cpf_count = ARRAY_SIZE(crt_ctl_rpcs);
	cpf.cpf_prf   = crt_ctl_rpcs;
	cpf.cpf_base  = CRT_OPC_CTL_BASE;

	rc = sem_init(&pfi.pfi_sem, 0, 0);
	if (rc != 0)
		return -DER_MISC;

	rc = crt_proto_query(ep, cpf.cpf_base, &cpf.cpf_ver,
			     1, crt_pfi_cb, &pfi);
	if (rc != -DER_SUCCESS)
		D_GOTO(out, rc);

	sem_wait(&pfi.pfi_sem);

	if (pfi.pfi_rc != -DER_SUCCESS)
		D_GOTO(out, rc = pfi.pfi_rc);

	if (pfi.pfi_ver != cpf.cpf_ver)
		D_GOTO(out, rc = -DER_MISMATCH);

	rc = crt_proto_register(&cpf);
	if (rc != 0)
		D_ERROR("crt_proto_register() failed, "DF_RC"\n", DP_RC(rc));

out:
	sem_destroy(&pfi.pfi_sem);

	return rc;
}

int
crt_rpc_priv_alloc(crt_opcode_t opc, struct crt_rpc_priv **priv_allocated,
		   bool forward)
{
	struct crt_rpc_priv	*rpc_priv;
	struct crt_opc_info	*opc_info;
	int			rc = 0;

	D_ASSERT(priv_allocated != NULL);

	D_DEBUG(DB_TRACE, "entering (opc: %#x)\n", opc);
	opc_info = crt_opc_lookup(crt_gdata.cg_opc_map, opc, CRT_UNLOCK);
	if (opc_info == NULL) {
		D_ERROR("opc: %#x, lookup failed.\n", opc);
		D_GOTO(out, rc = -DER_UNREG);
	}
	if (opc_info->coi_crf != NULL &&
	    (opc_info->coi_crf->crf_size_in > CRT_MAX_INPUT_SIZE ||
	     opc_info->coi_crf->crf_size_out > CRT_MAX_OUTPUT_SIZE)) {
		D_ERROR("opc: %#x, input_size " DF_U64 " or output_size "
			DF_U64 " too large.\n",
			opc, opc_info->coi_crf->crf_size_in,
			opc_info->coi_crf->crf_size_out);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (forward)
		D_ALLOC(rpc_priv, opc_info->coi_input_offset);
	else
		D_ALLOC(rpc_priv, opc_info->coi_rpc_size);
	if (rpc_priv == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rpc_priv->crp_opc_info = opc_info;
	rpc_priv->crp_forward = forward;
	*priv_allocated = rpc_priv;
	rpc_priv->crp_pub.cr_opc = opc;

	RPC_TRACE(DB_TRACE, rpc_priv, "(opc: %#x rpc_pub: %p) allocated.\n",
		  rpc_priv->crp_opc_info->coi_opc,
		  &rpc_priv->crp_pub);

out:
	return rc;
}

void
crt_rpc_priv_free(struct crt_rpc_priv *rpc_priv)
{
	if (rpc_priv == NULL)
		return;

	if (rpc_priv->crp_coll && rpc_priv->crp_corpc_info)
		crt_corpc_info_fini(rpc_priv);

	if (rpc_priv->crp_uri_free != 0)
		D_FREE(rpc_priv->crp_tgt_uri);

	D_SPIN_DESTROY(&rpc_priv->crp_lock);

	D_FREE(rpc_priv);
}

static inline void
crt_rpc_priv_set_ep(struct crt_rpc_priv *rpc_priv, crt_endpoint_t *tgt_ep)
{
	if (tgt_ep->ep_grp == NULL) {
		rpc_priv->crp_pub.cr_ep.ep_grp  =
			&crt_gdata.cg_grp->gg_primary_grp->gp_pub;
	} else {
		rpc_priv->crp_pub.cr_ep.ep_grp = tgt_ep->ep_grp;
	}
	rpc_priv->crp_pub.cr_ep.ep_rank = tgt_ep->ep_rank;
	rpc_priv->crp_pub.cr_ep.ep_tag = tgt_ep->ep_tag;
	rpc_priv->crp_have_ep = 1;
}

static int check_ep(crt_endpoint_t *tgt_ep, struct crt_grp_priv **ret_grp_priv)
{
	struct crt_grp_priv	*grp_priv;
	int rc = 0;

	grp_priv = crt_grp_pub2priv(tgt_ep->ep_grp);
	if (grp_priv == NULL) {
		D_ERROR("crt_grp_pub2priv(%p) got NULL.\n", tgt_ep->ep_grp);
		D_GOTO(out, rc = -DER_BAD_TARGET);
	}

out:
	if (rc == 0)
		*ret_grp_priv = grp_priv;

	return rc;
}

int
crt_req_create_internal(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep,
			crt_opcode_t opc, bool forward, crt_rpc_t **req)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	struct crt_grp_priv	*grp_priv = NULL;
	int			 rc;

	D_ASSERT(crt_ctx != CRT_CONTEXT_NULL && req != NULL);

	rc = crt_rpc_priv_alloc(opc, &rpc_priv, forward);
	if (rc != 0) {
		D_ERROR("crt_rpc_priv_alloc(%#x) failed, " DF_RC "\n",
			opc, DP_RC(rc));
		D_GOTO(out, rc);
	}

	D_ASSERT(rpc_priv != NULL);

	if (tgt_ep != NULL) {
		rc = check_ep(tgt_ep, &grp_priv);
		if (rc != 0)
			D_GOTO(out, rc);

		crt_rpc_priv_set_ep(rpc_priv, tgt_ep);

		rpc_priv->crp_grp_priv = grp_priv;
	}

	rc = crt_rpc_priv_init(rpc_priv, crt_ctx, false /* srv_flag */);
	if (rc != 0) {
		RPC_ERROR(rpc_priv,
			  "crt_rpc_priv_init(%#x) failed, " DF_RC "\n",
			  opc, DP_RC(rc));
		crt_rpc_priv_free(rpc_priv);
		D_GOTO(out, rc);
	}

	*req = &rpc_priv->crp_pub;
out:
	return rc;
}

int
crt_req_create(crt_context_t crt_ctx, crt_endpoint_t *tgt_ep, crt_opcode_t opc,
	       crt_rpc_t **req)
{
	int rc = 0;
	struct crt_grp_priv *grp_priv = NULL;
	struct crt_rpc_priv	*rpc_priv;

	if (crt_ctx == CRT_CONTEXT_NULL || req == NULL) {
		D_ERROR("invalid parameter (NULL crt_ctx or req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (!crt_initialized()) {
		D_ERROR("CRT not initialized.\n");
		D_GOTO(out, rc = -DER_UNINIT);
	}
	if (tgt_ep != NULL) {
		rc = check_ep(tgt_ep, &grp_priv);
		if (rc != 0)
			D_GOTO(out, rc);
	}

	rc = crt_req_create_internal(crt_ctx, tgt_ep, opc, false /* forward */,
				     req);
	if (rc != 0) {
		D_ERROR("crt_req_create_internal(%#x) failed, " DF_RC "\n",
			opc, DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ASSERT(*req != NULL);

	if (grp_priv) {
		rpc_priv = container_of(*req, struct crt_rpc_priv, crp_pub);
		rpc_priv->crp_grp_priv = grp_priv;
	}

out:
	return rc;
}

int
crt_req_set_endpoint(crt_rpc_t *req, crt_endpoint_t *tgt_ep)
{
	struct crt_rpc_priv	*rpc_priv;
	struct crt_grp_priv	*grp_priv = NULL;
	int			 rc = 0;

	if (req == NULL || tgt_ep == NULL) {
		D_ERROR("invalid parameter (NULL req or tgt_ep).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);
	if (rpc_priv->crp_have_ep == 1) {
		RPC_ERROR(rpc_priv, "target endpoint already set\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = check_ep(tgt_ep, &grp_priv);
	if (rc != 0)
		D_GOTO(out, rc);

	crt_rpc_priv_set_ep(rpc_priv, tgt_ep);

	rpc_priv->crp_grp_priv = grp_priv;

	RPC_TRACE(DB_NET, rpc_priv, "ep set %u.%u.\n",
		  req->cr_ep.ep_rank, req->cr_ep.ep_tag);

out:
	return rc;
}

int
crt_req_set_timeout(crt_rpc_t *req, uint32_t timeout_sec)
{
	struct crt_rpc_priv	*rpc_priv;
	int			 rc = 0;

	if (req == NULL || timeout_sec == 0) {
		D_ERROR("invalid parameter (NULL req or zero timeout_sec).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);
	rpc_priv->crp_timeout_sec = timeout_sec;

out:
	return rc;
}

/* Called from a decref() call when the count drops to zero */
void
crt_req_destroy(struct crt_rpc_priv *rpc_priv)
{
	if (rpc_priv->crp_reply_pending == 1) {
		D_WARN("no reply sent for rpc_priv %p (opc: %#x).\n",
		       rpc_priv, rpc_priv->crp_pub.cr_opc);
		/* We have executed the user RPC handler, but the user
		 * handler forgot to call crt_reply_send(). We send a
		 * CART level error message to notify the client
		 */
		crt_hg_reply_error_send(rpc_priv, -DER_NOREPLY);
	}

	crt_hg_req_destroy(rpc_priv);
}

int
crt_req_addref(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);
	RPC_ADDREF(rpc_priv);

out:
	return rc;
}

int
crt_req_decref(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);
	RPC_DECREF(rpc_priv);

out:
	return rc;
}

static inline int
crt_req_fill_tgt_uri(struct crt_rpc_priv *rpc_priv, crt_phy_addr_t base_uri)
{
	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(base_uri != NULL);

	D_STRNDUP(rpc_priv->crp_tgt_uri, base_uri, CRT_ADDR_STR_MAX_LEN);

	if (rpc_priv->crp_tgt_uri == NULL) {
		/* avoid checksum warning */
		return -DER_NOMEM;
	}

	rpc_priv->crp_uri_free = 1;

	return DER_SUCCESS;
}

static int
crt_issue_uri_lookup(crt_context_t ctx, crt_group_t *group,
		     d_rank_t contact_rank, uint32_t contact_tag,
		     d_rank_t query_rank, uint32_t query_tag,
		     struct crt_rpc_priv *chained_rpc);

#define MAX_URI_LOOKUP_RETRIES 10

static int
crt_issue_uri_lookup_retry(crt_context_t ctx,
			   struct crt_grp_priv *grp_priv,
			   d_rank_t query_rank, uint32_t query_tag,
			   struct crt_rpc_priv *rpc_priv)
{
	d_rank_list_t	*membs;
	d_rank_t	contact_rank;
	int		rc;

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);

	/* IF PSRs are specified cycle through them, else use members */
	if (grp_priv->gp_psr_ranks)
		membs = grp_priv->gp_psr_ranks;
	else
		membs = grp_priv_get_membs(grp_priv);

	/* Note: membership can change between uri lookups, but we don't need
	 * to handle this case, as it should be rare and will result in rank
	 * being either repeated or skipped
	 */
	if (!membs || membs->rl_nr <= 1 || rpc_priv->crp_ul_idx == -1) {
		contact_rank = grp_priv->gp_psr_rank;
	} else {
		rpc_priv->crp_ul_idx = (rpc_priv->crp_ul_idx + 1) %
				       membs->rl_nr;
		contact_rank = membs->rl_ranks[rpc_priv->crp_ul_idx];
	}

	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	rc = crt_issue_uri_lookup(ctx, &grp_priv->gp_pub,
				  contact_rank, 0,
				  query_rank, query_tag,
				  rpc_priv);
	return rc;
}

static void
uri_lookup_cb(const struct crt_cb_info *cb_info)
{
	struct crt_rpc_priv		*chained_rpc_priv;
	struct crt_context		*ctx;
	struct crt_uri_lookup_out	*ul_out;
	struct crt_uri_lookup_in	*ul_in;
	struct crt_grp_priv		*grp_priv;
	crt_rpc_t			*lookup_rpc;
	d_rank_list_t			*membs;
	bool				found;
	int				rc = 0;

	chained_rpc_priv = cb_info->cci_arg;
	lookup_rpc  = cb_info->cci_rpc;
	grp_priv = chained_rpc_priv->crp_grp_priv;

	ul_in = crt_req_get(lookup_rpc);
	if (cb_info->cci_rc != 0) {
		RPC_ERROR(chained_rpc_priv,
			  "URI_LOOKUP rpc completed with rc="DF_RC"\n",
			  DP_RC(cb_info->cci_rc));
		D_GOTO(retry, rc = cb_info->cci_rc);
	}

	ul_out = crt_reply_get(lookup_rpc);

	if (ul_out->ul_rc != 0) {
		RPC_ERROR(chained_rpc_priv, "URI_LOOKUP returned rc="DF_RC"\n",
			  DP_RC(ul_out->ul_rc));
		D_GOTO(retry, rc = ul_out->ul_rc);
	}

	ctx = lookup_rpc->cr_ctx;

	rc = crt_grp_lc_uri_insert(grp_priv, ul_in->ul_rank,
				   ul_out->ul_tag, ul_out->ul_uri);
	if (rc != 0) {
		RPC_ERROR(chained_rpc_priv,
			  "URI insertion '%s' failed for %d:%d; rc="DF_RC"\n",
			  ul_out->ul_uri, ul_in->ul_rank, ul_out->ul_tag,
			  DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Lookup request will either return tag=ul_in->ul_tag URI or
	 * tag=0 URI if ul_in->ul_tag is not found in server-side cache.
	 *
	 * ul_out->ul_tag will point to which tag URI was returned for.
	 * If requested tag does not match returned tag, issue URI
	 * request directly to the rank:tag=0 server
	 */
	char *fill_uri = NULL;

	if (ul_in->ul_tag != ul_out->ul_tag) {
		if (!crt_provider_is_contig_ep(ctx->cc_hg_ctx.chc_provider)) {
			rc = crt_issue_uri_lookup(lookup_rpc->cr_ctx,
						  lookup_rpc->cr_ep.ep_grp,
						  ul_in->ul_rank, 0,
						  ul_in->ul_rank, ul_in->ul_tag,
						  chained_rpc_priv);
			D_GOTO(out, rc);
		} else {
			/* Sanity check; if provider supports contiguous eps,
			 * any successful lookup should return proper tag
			 */
			D_ERROR("Should never get here\n");
			D_GOTO(out, rc = -DER_INVAL);
		}
	} else {
		fill_uri = ul_out->ul_uri;
	}

	rc = crt_req_fill_tgt_uri(chained_rpc_priv, fill_uri);
	if (rc != 0) {
		RPC_ERROR(chained_rpc_priv,
			  "crt_req_fill_tgt_uri() failed; rc="DF_RC"\n",
			  DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* After a URI lookup, check if membership list has this rank.
	 * If not - we discovered a new rank and need to populate it in membs
	 * list of the group.
	 */
	D_RWLOCK_WRLOCK(&grp_priv->gp_rwlock);
	membs = grp_priv_get_membs(grp_priv);
	found = d_rank_list_find(membs, ul_in->ul_rank, NULL);

	if (!found) {
		rc = grp_add_to_membs_list(grp_priv, ul_in->ul_rank);
		if (rc != 0) {
			D_ERROR("Failed to add %d to group rc "DF_RC"\n",
				ul_in->ul_rank, DP_RC(rc));
			D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
			D_GOTO(out, rc);
		}
	}
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

	/* issue the original RPC */
	rc = crt_req_send_internal(chained_rpc_priv);

retry:

	if (rc != 0) {
		if (chained_rpc_priv->crp_ul_retry++ < MAX_URI_LOOKUP_RETRIES) {
			rc = crt_issue_uri_lookup_retry(lookup_rpc->cr_ctx,
							grp_priv,
							ul_in->ul_rank,
							ul_in->ul_tag,
							chained_rpc_priv);
			D_GOTO(out, rc);
		} else {
			D_ERROR("URI lookups exceeded %d retries\n",
				chained_rpc_priv->crp_ul_retry);
		}
	}

out:
	RPC_PUB_DECREF(lookup_rpc);

	/* Force complete and destroy chained rpc */
	if (rc != 0) {
		crt_context_req_untrack(chained_rpc_priv);
		crt_rpc_complete(chained_rpc_priv, rc);
	}

	/* Addref done in crt_issue_uri_lookup() */
	RPC_DECREF(chained_rpc_priv);
}

/**
 * Helper function that returns rank for clients to contact
 * for performing uri lookups
 */
static d_rank_t
crt_client_get_contact_rank(crt_context_t crt_ctx, crt_group_t *grp,
			    d_rank_t query_rank, uint32_t query_tag,
			    int *ret_idx)
{
	struct crt_grp_priv	*grp_priv;
	d_rank_t		 contact_rank = CRT_NO_RANK;
	char			*cached_uri = NULL;
	struct crt_context	*ctx;
	d_rank_list_t		*membs;

	grp_priv = crt_grp_pub2priv(grp);
	ctx = crt_ctx;

	/* If query_rank:tag=0 is in cache, use it as contact destination */
	if (query_tag != 0) {
		crt_grp_lc_lookup(grp_priv, ctx->cc_idx,
				  query_rank, 0, &cached_uri, NULL);
		if (cached_uri != NULL) {
			*ret_idx = -1;
			D_GOTO(out, contact_rank = query_rank);
		}
	}

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);

	if (grp_priv->gp_psr_ranks)
		membs = grp_priv->gp_psr_ranks;
	else
		membs = grp_priv_get_membs(grp_priv);

	if (!membs || membs->rl_nr == 0) {
		/* If list is not set, default to legacy psr */
		contact_rank = grp_priv->gp_psr_rank;
		*ret_idx = -1;
	} else {
		/* Pick random rank from the list */
		*ret_idx = rand() % membs->rl_nr;
		contact_rank = membs->rl_ranks[*ret_idx];

		D_DEBUG(DB_ALL, "URI lookup rank chosen: %d\n", contact_rank);
	}

	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);

out:
	return contact_rank;
}

static int
crt_req_uri_lookup(struct crt_rpc_priv *rpc_priv)
{
	crt_endpoint_t		*tgt_ep;
	crt_context_t		 ctx;
	crt_group_t		*grp;
	int			 ret_idx;
	int			 rc;

	tgt_ep = &rpc_priv->crp_pub.cr_ep;
	ctx = rpc_priv->crp_pub.cr_ctx;
	grp = tgt_ep->ep_grp;

	/* Client handling */
	if (!crt_is_service()) {
		d_rank_t lookup_rank;

		lookup_rank = crt_client_get_contact_rank(ctx, grp,
							  tgt_ep->ep_rank,
							  tgt_ep->ep_tag,
							  &ret_idx);
		if (lookup_rank == CRT_NO_RANK) {
			D_ERROR("Failed to rank for uri lookups\n");
			D_GOTO(out, rc = -DER_NONEXIST);
		}

		/* Save index from membership list */
		rpc_priv->crp_ul_retry = 0;
		rpc_priv->crp_ul_idx = ret_idx;

		rc = crt_issue_uri_lookup(ctx, grp, lookup_rank, 0,
					  tgt_ep->ep_rank, tgt_ep->ep_tag,
					  rpc_priv);
		D_GOTO(out, rc);
	}

	/* Server handling */

	/* Servers must know tag=0 uris of other servers */
	if (tgt_ep->ep_tag == 0) {
		RPC_ERROR(rpc_priv, "Target %d:%d not known\n",
			  tgt_ep->ep_rank, tgt_ep->ep_tag);
		D_GOTO(out, rc = -DER_OOG);
	}

	/* Send request to tag=0 to get uri for ep_tag */
	rc = crt_issue_uri_lookup(ctx, grp, tgt_ep->ep_rank, 0,
				  tgt_ep->ep_rank, tgt_ep->ep_tag, rpc_priv);
out:
	return rc;
}

static int
crt_issue_uri_lookup(crt_context_t ctx, crt_group_t *group,
		     d_rank_t contact_rank, uint32_t contact_tag,
		     d_rank_t query_rank, uint32_t query_tag,
		     struct crt_rpc_priv *chained_rpc_priv)
{
	crt_rpc_t			*rpc;
	crt_endpoint_t			target_ep;
	struct crt_uri_lookup_in	*ul_in;
	int				rc;

	target_ep.ep_rank = contact_rank;
	target_ep.ep_tag = contact_tag;
	target_ep.ep_grp = group;

	rc = crt_req_create(ctx, &target_ep, CRT_OPC_URI_LOOKUP, &rpc);
	if (rc != 0) {
		D_ERROR("URI_LOOKUP rpc create failed; rc="DF_RC"\n",
			DP_RC(rc));
		D_GOTO(exit, rc);
	}

	ul_in = crt_req_get(rpc);
	ul_in->ul_grp_id = group->cg_grpid;
	ul_in->ul_rank = query_rank;
	ul_in->ul_tag = query_tag;

	RPC_PUB_ADDREF(rpc);
	chained_rpc_priv->crp_ul_req = rpc;

	RPC_ADDREF(chained_rpc_priv);
	rc = crt_req_send(rpc, uri_lookup_cb, chained_rpc_priv);

	if (rc != 0) {
		RPC_DECREF(chained_rpc_priv);

		/* Addref done above */
		RPC_PUB_DECREF(rpc);
		chained_rpc_priv->crp_ul_req = NULL;
	}

exit:
	return rc;
}

/* Fill rpc_priv->crp_hg_addr field based on local cache contents */
static void
crt_lc_hg_addr_fill(struct crt_rpc_priv *rpc_priv)
{
	struct crt_grp_priv	*grp_priv;
	crt_rpc_t		*req;
	crt_endpoint_t		*tgt_ep;
	struct crt_context	*ctx;

	req = &rpc_priv->crp_pub;
	ctx = req->cr_ctx;
	tgt_ep = &req->cr_ep;

	grp_priv = crt_grp_pub2priv(tgt_ep->ep_grp);

	crt_grp_lc_lookup(grp_priv, ctx->cc_idx, tgt_ep->ep_rank,
			  tgt_ep->ep_tag, NULL, &rpc_priv->crp_hg_addr);
}

bool
crt_req_is_self(struct crt_rpc_priv *rpc_priv)
{
	struct crt_grp_priv	*grp_priv_self;
	crt_endpoint_t		*tgt_ep;
	bool			 same_group;
	bool			 same_rank;

	D_ASSERT(rpc_priv != NULL);
	grp_priv_self = crt_grp_pub2priv(NULL);
	tgt_ep = &rpc_priv->crp_pub.cr_ep;
	same_group = (tgt_ep->ep_grp == NULL) ||
		     crt_grp_id_identical(tgt_ep->ep_grp->cg_grpid,
					  grp_priv_self->gp_pub.cg_grpid);
	same_rank = tgt_ep->ep_rank == grp_priv_self->gp_self;

	return (same_group && same_rank);
}

/* look in the local cache to find the NA address of the target */
static int
crt_req_ep_lc_lookup(struct crt_rpc_priv *rpc_priv, bool *uri_exists)
{
	struct crt_grp_priv	*grp_priv;
	crt_rpc_t		*req;
	crt_endpoint_t		*tgt_ep;
	struct crt_context	*ctx;
	crt_phy_addr_t		 uri = NULL;
	int			 rc = 0;
	crt_phy_addr_t		 base_addr = NULL;

	req = &rpc_priv->crp_pub;
	ctx = req->cr_ctx;
	tgt_ep = &req->cr_ep;

	*uri_exists = false;
	grp_priv = crt_grp_pub2priv(tgt_ep->ep_grp);

	crt_grp_lc_lookup(grp_priv, ctx->cc_idx,
			  tgt_ep->ep_rank, tgt_ep->ep_tag, &base_addr,
			  &rpc_priv->crp_hg_addr);

	if (base_addr == NULL && rpc_priv->crp_hg_addr == NULL) {
		if (crt_req_is_self(rpc_priv)) {
			rc = crt_self_uri_get(tgt_ep->ep_tag, &uri);
			if (rc != DER_SUCCESS) {
				D_ERROR("crt_self_uri_get(tag: %d) failed, "
					"rc %d\n", tgt_ep->ep_tag, rc);
				D_GOTO(out, rc);
			}

			rc = crt_grp_lc_uri_insert(grp_priv,
						   tgt_ep->ep_rank,
						   tgt_ep->ep_tag, base_addr);
			if (rc != 0)
				D_GOTO(out, rc);

			rc = crt_req_fill_tgt_uri(rpc_priv, uri);
			base_addr = uri;
			D_GOTO(out, rc);
		}
	}

	if (base_addr != NULL && rpc_priv->crp_hg_addr == NULL) {
		rc = crt_req_fill_tgt_uri(rpc_priv, base_addr);
		if (rc != 0)
			RPC_ERROR(rpc_priv,
				  "crt_req_fill_tgt_uri() failed, " DF_RC "\n",
				  DP_RC(rc));
		D_GOTO(out, rc);
	}

	/*
	 * If the target endpoint is the PSR and it's not already in the address
	 * cache, insert the URI of the PSR to the address cache.
	 * Did it in crt_grp_attach(), in the case that this context created
	 * later can insert it here.
	 */
	if (base_addr == NULL && !crt_is_service()) {
		D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
		if (tgt_ep->ep_rank == grp_priv->gp_psr_rank &&
		    tgt_ep->ep_tag == 0) {
			D_STRNDUP(uri, grp_priv->gp_psr_phy_addr,
				  CRT_ADDR_STR_MAX_LEN);
			D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
			if (uri == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			base_addr = uri;
			rc = crt_grp_lc_uri_insert(grp_priv,
						   tgt_ep->ep_rank, 0, uri);
			if (rc != 0) {
				D_ERROR("crt_grp_lc_uri_insert() failed, "
					"rc: %d\n", rc);
				D_GOTO(out, rc);
			}

			rc = crt_req_fill_tgt_uri(rpc_priv, uri);
			if (rc != 0) {
				D_ERROR("crt_req_fill_tgt_uri failed, "
					"opc: %#x.\n", req->cr_opc);
				D_GOTO(out, rc);
			}
		} else {
			D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
		}
	}

out:
	if (base_addr)
		*uri_exists = true;
	D_FREE(uri);
	return rc;
}

/*
 * the case where we have the base URI but don't have the NA address of the tag
 * TODO: This function will be gone after hg handle cache revamp
 */
static int
crt_req_hg_addr_lookup(struct crt_rpc_priv *rpc_priv)
{
	hg_addr_t		 hg_addr;
	hg_return_t		 hg_ret;
	struct crt_context	*crt_ctx;
	int			 rc = 0;

	crt_ctx = rpc_priv->crp_pub.cr_ctx;

	hg_ret = HG_Addr_lookup2(crt_ctx->cc_hg_ctx.chc_hgcla,
				 rpc_priv->crp_tgt_uri, &hg_addr);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Addr_lookup2() failed. uri=%s, hg_ret=%d\n",
			rpc_priv->crp_tgt_uri, hg_ret);
		D_GOTO(out, rc = -DER_HG);
	}

	rc = crt_grp_lc_addr_insert(rpc_priv->crp_grp_priv, crt_ctx,
				    rpc_priv->crp_pub.cr_ep.ep_rank,
				    rpc_priv->crp_pub.cr_ep.ep_tag,
				    &hg_addr);
	if (rc != 0) {
		D_ERROR("Failed to insert\n");
		rpc_priv->crp_state = RPC_STATE_FWD_UNREACH;
		D_GOTO(finish_rpc, rc);
	}

	rpc_priv->crp_hg_addr = hg_addr;
	rc = crt_req_send_internal(rpc_priv);
	if (rc != 0) {
		RPC_ERROR(rpc_priv,
			  "crt_req_send_internal() failed, rc %d\n",
			  rc);
		D_GOTO(finish_rpc, rc);
	}

finish_rpc:
	if (rc != 0) {
		crt_context_req_untrack(rpc_priv);
		crt_rpc_complete(rpc_priv, rc);

		/* Do not propagate error further as we've completed the rpc */
		rc = DER_SUCCESS;
	}

out:
	return rc;
}

static inline int
crt_req_send_immediately(struct crt_rpc_priv *rpc_priv)
{
	crt_rpc_t			*req;
	struct crt_context		*ctx;
	int				 rc = 0;

	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(rpc_priv->crp_hg_addr != NULL);

	req = &rpc_priv->crp_pub;
	ctx = req->cr_ctx;
	rc = crt_hg_req_create(&ctx->cc_hg_ctx, rpc_priv);
	if (rc != 0) {
		D_ERROR("crt_hg_req_create failed, rc: %d, opc: %#x.\n",
			rc, req->cr_opc);
		D_GOTO(out, rc);
	}
	D_ASSERT(rpc_priv->crp_hg_hdl != NULL);

	/* set state ahead to avoid race with completion cb */
	rpc_priv->crp_state = RPC_STATE_REQ_SENT;
	rc = crt_hg_req_send(rpc_priv);
	if (rc != DER_SUCCESS) {
		RPC_ERROR(rpc_priv,
			  "crt_hg_req_send failed, rc: %d\n", rc);
	}
out:
	return rc;
}

int
crt_req_send_internal(struct crt_rpc_priv *rpc_priv)
{
	crt_rpc_t	*req;
	bool		uri_exists = false;
	int		rc = 0;

	req = &rpc_priv->crp_pub;
	switch (rpc_priv->crp_state) {
	case RPC_STATE_QUEUED:
		rpc_priv->crp_state = RPC_STATE_INITED;
	case RPC_STATE_INITED:
		/* lookup local cache  */
		rpc_priv->crp_hg_addr = NULL;
		rc = crt_req_ep_lc_lookup(rpc_priv, &uri_exists);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_grp_ep_lc_lookup() failed, " DF_RC "\n",
				  DP_RC(rc));
			D_GOTO(out, rc);
		}

		if (rpc_priv->crp_hg_addr != NULL) {
			/* send the RPC if the local cache has the HG_Addr */
			rc = crt_req_send_immediately(rpc_priv);
		} else if (uri_exists == true) {
			/* send addr lookup req */
			rpc_priv->crp_state = RPC_STATE_ADDR_LOOKUP;
			rc = crt_req_hg_addr_lookup(rpc_priv);
			if (rc != 0)
				D_ERROR("crt_req_hg_addr_lookup() failed, "
					"rc %d, opc: %#x.\n", rc, req->cr_opc);
		} else {
			/* base_addr == NULL, send uri lookup req */
			rpc_priv->crp_state = RPC_STATE_URI_LOOKUP;
			rc = crt_req_uri_lookup(rpc_priv);
			if (rc != 0)
				RPC_ERROR(rpc_priv,
					  "crt_req_uri_lookup() failed. rc "
					  DF_RC"\n", DP_RC(rc));
		}
		break;
	case RPC_STATE_URI_LOOKUP:
		crt_lc_hg_addr_fill(rpc_priv);

		if (rpc_priv->crp_hg_addr != NULL) {
			rc = crt_req_send_immediately(rpc_priv);
		} else {
			/* send addr lookup req */
			rpc_priv->crp_state = RPC_STATE_ADDR_LOOKUP;
			rc = crt_req_hg_addr_lookup(rpc_priv);
			if (rc != 0)
				D_ERROR("crt_req_hg_addr_lookup() failed, "
					"rc %d, opc: %#x.\n", rc, req->cr_opc);
		}
		break;
	case RPC_STATE_ADDR_LOOKUP:
		rc = crt_req_send_immediately(rpc_priv);
		break;
	default:
		RPC_ERROR(rpc_priv,
			  "bad rpc state: %#x\n",
			  rpc_priv->crp_state);
		rc = -DER_PROTO;
		break;
	}

out:
	if (rc != 0)
		rpc_priv->crp_state = RPC_STATE_INITED;
	return rc;
}

int
crt_req_send(crt_rpc_t *req, crt_cb_t complete_cb, void *arg)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			 rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		if (complete_cb != NULL) {
			struct crt_cb_info	cbinfo;

			cbinfo.cci_rpc = NULL;
			cbinfo.cci_arg = arg;
			cbinfo.cci_rc  = -DER_INVAL;
			complete_cb(&cbinfo);

			return 0;
		} else {
			return -DER_INVAL;
		}
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);
	/* Take a reference to ensure rpc_priv is valid for duration of this
	 * function.  Referenced dropped at end of this function.
	 */
	RPC_ADDREF(rpc_priv);

	if (req->cr_ctx == NULL) {
		D_ERROR("invalid parameter (NULL req->cr_ctx).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv->crp_complete_cb = complete_cb;
	rpc_priv->crp_arg = arg;

	if (rpc_priv->crp_coll) {
		rc = crt_corpc_req_hdlr(rpc_priv);
		if (rc != 0)
			RPC_ERROR(rpc_priv,
				  "crt_corpc_req_hdlr() failed, " DF_RC "\n",
				  DP_RC(rc));
		D_GOTO(out, rc);
	} else {
		if (!rpc_priv->crp_have_ep) {
			D_WARN("target endpoint not set "
				"rpc: %p, opc: %#x.\n", rpc_priv, req->cr_opc);
			D_GOTO(out, rc = -DER_INVAL);
		}
	}

	RPC_TRACE(DB_TRACE, rpc_priv, "submitted.\n");

	rc = crt_context_req_track(rpc_priv);
	if (rc == CRT_REQ_TRACK_IN_INFLIGHQ) {
		/* tracked in crt_ep_inflight::epi_req_q */
		rc = crt_req_send_internal(rpc_priv);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_req_send_internal() failed, " DF_RC "\n",
				  DP_RC(rc));
			crt_context_req_untrack(rpc_priv);
		}
	} else if (rc == CRT_REQ_TRACK_IN_WAITQ) {
		/* queued in crt_hg_context::dhc_req_q */
		rc = 0;
	} else {
		RPC_ERROR(rpc_priv,
			  "crt_context_req_track() failed, " DF_RC "\n",
			  DP_RC(rc));
	}

out:
	/* internally destroy the req when failed */
	if (rc != 0) {
		if (!rpc_priv->crp_coll) {
			crt_rpc_complete(rpc_priv, rc);
			/* failure already reported through complete cb */
			if (complete_cb != NULL)
				rc = 0;
		} else {
			RPC_DECREF(rpc_priv);
		}
	}

	/* corresponds to RPC_ADDREF in this function */
	RPC_DECREF(rpc_priv);
	return rc;
}

int
crt_reply_send(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);

	D_DEBUG(DB_ALL, "rpc_priv: %p\n", rpc_priv);
	if (rpc_priv->crp_coll == 1) {
		struct crt_cb_info	cb_info;

		D_DEBUG(DB_ALL, "call crp_corpc_reply_hdlf: rpc_priv: %p\n",
			rpc_priv);
		cb_info.cci_rpc = &rpc_priv->crp_pub;
		cb_info.cci_rc = 0;
		cb_info.cci_arg = rpc_priv;

		crt_corpc_reply_hdlr(&cb_info);
	} else {
		D_DEBUG(DB_ALL, "call crt_hg_reply_send: rpc_priv: %p\n",
			rpc_priv);
		rc = crt_hg_reply_send(rpc_priv);
		if (rc != 0)
			D_ERROR("crt_hg_reply_send failed, rc: %d,opc: %#x.\n",
				rc, rpc_priv->crp_pub.cr_opc);
	}

	rpc_priv->crp_reply_pending = 0;
out:
	return rc;
}

int
crt_req_abort(crt_rpc_t *req)
{
	struct crt_rpc_priv	*rpc_priv;
	int			 rc = 0;

	if (req == NULL) {
		D_ERROR("invalid parameter (NULL req).\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(req, struct crt_rpc_priv, crp_pub);

	if (rpc_priv->crp_state == RPC_STATE_CANCELED ||
	    rpc_priv->crp_state == RPC_STATE_COMPLETED) {
		RPC_TRACE(DB_NET, rpc_priv,
			  "aborted or completed, need not abort again.\n");
		D_GOTO(out, rc = -DER_ALREADY);
	}

	if (rpc_priv->crp_state != RPC_STATE_REQ_SENT ||
	    rpc_priv->crp_on_wire != 1) {
		RPC_TRACE(DB_NET, rpc_priv,
			  "rpc_priv->crp_state %#x, not inflight, complete it "
			  "as canceled.\n",
			  rpc_priv->crp_state);
		crt_rpc_complete(rpc_priv, -DER_CANCELED);
		D_GOTO(out, rc = 0);
	}

	rc = crt_hg_req_cancel(rpc_priv);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_hg_req_cancel failed, rc: %d, "
			  "opc: %#x.\n", rc, rpc_priv->crp_pub.cr_opc);
		crt_rpc_complete(rpc_priv, rc);
		D_GOTO(out, rc);
	}

out:
	return rc;
}

static void
crt_rpc_inout_buff_fini(struct crt_rpc_priv *rpc_priv)
{
	crt_rpc_t	*rpc_pub;

	D_ASSERT(rpc_priv != NULL);
	rpc_pub = &rpc_priv->crp_pub;

	if (rpc_pub->cr_input != NULL) {
		D_ASSERT(rpc_pub->cr_input_size != 0);
		rpc_pub->cr_input_size = 0;
		rpc_pub->cr_input = NULL;
	}

	if (rpc_pub->cr_output != NULL) {
		rpc_pub->cr_output_size = 0;
		rpc_pub->cr_output = NULL;
	}
}

static void
crt_rpc_inout_buff_init(struct crt_rpc_priv *rpc_priv)
{
	crt_rpc_t		*rpc_pub;
	struct crt_opc_info	*opc_info;

	D_ASSERT(rpc_priv != NULL);
	rpc_pub = &rpc_priv->crp_pub;
	D_ASSERT(rpc_pub->cr_input == NULL);
	D_ASSERT(rpc_pub->cr_output == NULL);
	opc_info = rpc_priv->crp_opc_info;
	D_ASSERT(opc_info != NULL);

	if (opc_info->coi_crf == NULL)
		return;

	/*
	 * for forward request, need not allocate memory here, instead it will
	 * reuse the original input buffer of parent RPC.
	 * See crt_corpc_req_hdlr().
	 */
	if (opc_info->coi_crf->crf_size_in > 0 && !rpc_priv->crp_forward) {
		rpc_pub->cr_input = ((void *)rpc_priv) +
			opc_info->coi_input_offset;
		rpc_pub->cr_input_size = opc_info->coi_crf->crf_size_in;
	}
	if (opc_info->coi_crf->crf_size_out > 0) {
		rpc_pub->cr_output = ((void *)rpc_priv) +
			opc_info->coi_output_offset;
		rpc_pub->cr_output_size = opc_info->coi_crf->crf_size_out;
	}
}

static inline void
crt_common_hdr_init(struct crt_rpc_priv *rpc_priv, crt_opcode_t opc)
{
	uint64_t	rpcid;

	rpcid = atomic_fetch_add(&crt_gdata.cg_rpcid, 1);

	rpc_priv->crp_req_hdr.cch_opc = opc;
	rpc_priv->crp_req_hdr.cch_rpcid = rpcid;

	rpc_priv->crp_reply_hdr.cch_opc = opc;
	rpc_priv->crp_reply_hdr.cch_rpcid = rpcid;
}

int
crt_rpc_priv_init(struct crt_rpc_priv *rpc_priv, crt_context_t crt_ctx,
		  bool srv_flag)
{
	crt_opcode_t opc = rpc_priv->crp_opc_info->coi_opc;
	struct crt_context *ctx = crt_ctx;
	int rc;

	rc = D_SPIN_INIT(&rpc_priv->crp_lock, PTHREAD_PROCESS_PRIVATE);
	if (rc != 0)
		D_GOTO(exit, rc);

	D_INIT_LIST_HEAD(&rpc_priv->crp_epi_link);
	D_INIT_LIST_HEAD(&rpc_priv->crp_tmp_link);
	D_INIT_LIST_HEAD(&rpc_priv->crp_parent_link);
	rpc_priv->crp_complete_cb = NULL;
	rpc_priv->crp_arg = NULL;
	if (!srv_flag) {
		/* avoid checksum warning */
		crt_common_hdr_init(rpc_priv, opc);
	}
	rpc_priv->crp_state = RPC_STATE_INITED;
	rpc_priv->crp_hdl_reuse = NULL;
	rpc_priv->crp_srv = srv_flag;
	rpc_priv->crp_ul_retry = 0;
	/**
	 * initialized to 1, so user can call crt_req_decref to destroy new req
	 */
	rpc_priv->crp_refcount = 1;

	rpc_priv->crp_pub.cr_opc = opc;
	rpc_priv->crp_pub.cr_ctx = crt_ctx;

	crt_rpc_inout_buff_init(rpc_priv);

	rpc_priv->crp_timeout_sec = ctx->cc_timeout_sec;

exit:
	return rc;
}

void
crt_rpc_priv_fini(struct crt_rpc_priv *rpc_priv)
{
	D_ASSERT(rpc_priv != NULL);
	crt_rpc_inout_buff_fini(rpc_priv);
}

static void
crt_handle_rpc(void *arg)
{
	crt_rpc_t		*rpc_pub = arg;
	struct crt_rpc_priv	*rpc_priv;

	D_ASSERT(rpc_pub != NULL);

	rpc_priv = container_of(rpc_pub, struct crt_rpc_priv, crp_pub);
	D_ASSERT(rpc_priv->crp_opc_info != NULL);
	D_ASSERT(rpc_priv->crp_opc_info->coi_rpc_cb != NULL);

	/*
	 * for user initiated corpc if it delivered to itself, in user's RPC
	 * handler after sending reply the refcount possibly be dropped at
	 * crt_corpc_reply_hdlr's corpc completion, take a ref here to ensure
	 * its safe to access the rpc before RPC handler returns.
	 */
	if (rpc_priv->crp_coll && !rpc_priv->crp_srv)
		RPC_ADDREF(rpc_priv);
	rpc_priv->crp_opc_info->coi_rpc_cb(rpc_pub);
	/*
	 * Correspond to crt_rpc_handler_common -> crt_rpc_priv_init's set
	 * refcount as 1. "rpc_priv->crp_srv" is to differentiate from calling
	 * path of crt_req_send -> crt_corpc_req_hdlr -> crt_rpc_common_hdlr.
	 * Or to dec the ref taken above.
	 */
	if (rpc_priv->crp_srv || (rpc_priv->crp_coll && !rpc_priv->crp_srv))
		RPC_DECREF(rpc_priv);
}

int
crt_rpc_common_hdlr(struct crt_rpc_priv *rpc_priv)
{
	struct crt_context	*crt_ctx;
	int			 rc = 0;
	bool			skip_check = false;
	d_rank_t		self_rank;

	D_ASSERT(rpc_priv != NULL);
	crt_ctx = rpc_priv->crp_pub.cr_ctx;

	self_rank = crt_gdata.cg_grp->gg_primary_grp->gp_self;

	/* If RPC failed HLC epsilon delta check return an error */
	if (rpc_priv->crp_fail_hlc)
		D_GOTO(out, rc = -DER_HLC_SYNC);

	if (self_rank == CRT_NO_RANK)
		skip_check = true;

	/* Skip check when CORPC is sent to self */
	if (rpc_priv->crp_coll) {
		d_rank_t pri_root;

		pri_root = crt_grp_priv_get_primary_rank(
				rpc_priv->crp_corpc_info->co_grp_priv,
				rpc_priv->crp_corpc_info->co_root);

		if (pri_root == self_rank)
			skip_check = true;
	}

	if ((self_rank != rpc_priv->crp_req_hdr.cch_dst_rank) ||
	    (crt_ctx->cc_idx != rpc_priv->crp_req_hdr.cch_dst_tag)) {
		if (!skip_check) {
			D_ERROR("Mismatch rpc: %p opc: %x rank:%d tag:%d "
				"self:%d cc_idx:%d ep_rank:%d ep_tag:%d\n",
				rpc_priv,
				rpc_priv->crp_pub.cr_opc,
				rpc_priv->crp_req_hdr.cch_dst_rank,
				rpc_priv->crp_req_hdr.cch_dst_tag,
				self_rank,
				crt_ctx->cc_idx,
				rpc_priv->crp_pub.cr_ep.ep_rank,
				rpc_priv->crp_pub.cr_ep.ep_tag);

			D_GOTO(out, rc = -DER_BAD_TARGET);
		}
	}

	/* Set the reply pending bit unless this is a one-way OPCODE */
	if (!rpc_priv->crp_opc_info->coi_no_reply)
		rpc_priv->crp_reply_pending = 1;

	if (crt_rpc_cb_customized(crt_ctx, &rpc_priv->crp_pub)) {
		rc = crt_ctx->cc_rpc_cb((crt_context_t)crt_ctx,
					&rpc_priv->crp_pub,
					crt_handle_rpc,
					crt_ctx->cc_rpc_cb_arg);
	} else {
		rpc_priv->crp_opc_info->coi_rpc_cb(&rpc_priv->crp_pub);
		/*
		 * Correspond to crt_rpc_handler_common -> crt_rpc_priv_init's
		 * set refcount as 1.
		 */
		if (rpc_priv->crp_srv)
			RPC_DECREF(rpc_priv);
	}

out:
	return rc;
}

static int
timeout_bp_node_enter(struct d_binheap *h, struct d_binheap_node *e)
{
	struct crt_rpc_priv	*rpc_priv;

	D_ASSERT(h != NULL);
	D_ASSERT(e != NULL);

	rpc_priv = container_of(e, struct crt_rpc_priv, crp_timeout_bp_node);

	RPC_TRACE(DB_NET, rpc_priv, "entering the timeout binheap.\n");

	return 0;
}

static int
timeout_bp_node_exit(struct d_binheap *h, struct d_binheap_node *e)
{
	struct crt_rpc_priv	*rpc_priv;

	D_ASSERT(h != NULL);
	D_ASSERT(e != NULL);

	rpc_priv = container_of(e, struct crt_rpc_priv, crp_timeout_bp_node);

	RPC_TRACE(DB_NET, rpc_priv, "exiting the timeout binheap.\n");

	return 0;
}

static bool
timeout_bp_node_cmp(struct d_binheap_node *a, struct d_binheap_node *b)
{
	struct crt_rpc_priv	*rpc_priv_a;
	struct crt_rpc_priv	*rpc_priv_b;

	D_ASSERT(a != NULL);
	D_ASSERT(b != NULL);

	rpc_priv_a = container_of(a, struct crt_rpc_priv, crp_timeout_bp_node);
	rpc_priv_b = container_of(b, struct crt_rpc_priv, crp_timeout_bp_node);

	return rpc_priv_a->crp_timeout_ts < rpc_priv_b->crp_timeout_ts;
}

struct d_binheap_ops crt_timeout_bh_ops = {
	.hop_enter	= timeout_bp_node_enter,
	.hop_exit	= timeout_bp_node_exit,
	.hop_compare	= timeout_bp_node_cmp
};

int
crt_req_src_rank_get(crt_rpc_t *rpc, d_rank_t *rank)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (rpc == NULL) {
		D_ERROR("NULL rpc passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank == NULL) {
		D_ERROR("NULL rank passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);

	*rank = rpc_priv->crp_req_hdr.cch_src_rank;

out:
	return rc;
}

int
crt_req_dst_rank_get(crt_rpc_t *rpc, d_rank_t *rank)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (rpc == NULL) {
		D_ERROR("NULL rpc passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (rank == NULL) {
		D_ERROR("NULL rank passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);

	*rank = rpc_priv->crp_req_hdr.cch_dst_rank;

out:
	return rc;
}

int
crt_req_dst_tag_get(crt_rpc_t *rpc, uint32_t *tag)
{
	struct crt_rpc_priv	*rpc_priv = NULL;
	int			rc = 0;

	if (rpc == NULL) {
		D_ERROR("NULL rpc passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (tag == NULL) {
		D_ERROR("NULL tag passed\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rpc_priv = container_of(rpc, struct crt_rpc_priv, crp_pub);
	*tag = rpc_priv->crp_req_hdr.cch_dst_tag;
out:
	return rc;
}

int
crt_register_hlc_error_cb(crt_hlc_error_cb event_handler, void *arg)
{
	int rc = 0;

	D_MUTEX_LOCK(&crt_plugin_gdata.cpg_mutex);
	crt_plugin_gdata.hlc_error_cb = event_handler;
	crt_plugin_gdata.hlc_error_cb_arg = arg;
	D_MUTEX_UNLOCK(&crt_plugin_gdata.cpg_mutex);

	return rc;
}

void
crt_trigger_hlc_error_cb(void)
{
	crt_hlc_error_cb	handler;
	void			*arg;

	D_MUTEX_LOCK(&crt_plugin_gdata.cpg_mutex);
	handler = crt_plugin_gdata.hlc_error_cb;
	arg = crt_plugin_gdata.hlc_error_cb_arg;
	D_MUTEX_UNLOCK(&crt_plugin_gdata.cpg_mutex);

	if (handler)
		handler(arg);
}
