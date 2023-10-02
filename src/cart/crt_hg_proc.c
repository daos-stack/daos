/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It implements the main input/output
 * parameter serialization/de-serialization routines (proc functions).
 */
#define D_LOGFAC	DD_FAC(hg)

#include "crt_internal.h"

#define CRT_PROC_NULL (NULL)
#define CRT_PROC_TYPE_FUNC(type)                                                                   \
	int crt_proc_##type(crt_proc_t proc, crt_proc_op_t proc_op, type *data)                    \
	{                                                                                          \
		type *buf;                                                                         \
		if (FREEING(proc_op))                                                              \
			return 0;                                                                  \
		buf = hg_proc_save_ptr(proc, sizeof(*buf));                                        \
		if (ENCODING(proc_op))                                                             \
			*buf = *data;                                                              \
		else /* DECODING(proc_op) */                                                       \
			*data = *buf;                                                              \
		return 0;                                                                          \
	}

static inline int
crt_proc_op2hg(crt_proc_op_t crt_op, hg_proc_op_t *hg_op)
{
	int	rc = 0;

	switch (crt_op) {
	case CRT_PROC_ENCODE:
		*hg_op = HG_ENCODE;
		break;
	case CRT_PROC_DECODE:
		*hg_op = HG_DECODE;
		break;
	case CRT_PROC_FREE:
		*hg_op = HG_FREE;
		break;
	default:
		rc = -DER_INVAL;
		break;
	}

	return rc;
}

int
crt_proc_get_op(crt_proc_t proc, crt_proc_op_t *proc_op)
{
	hg_proc_op_t	hg_proc_op;
	int		rc = 0;

	if (unlikely(proc == NULL)) {
		D_ERROR("Proc is not initilalized.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (unlikely(proc_op == NULL)) {
		D_ERROR("Invalid parameter - NULL proc_op.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	hg_proc_op = hg_proc_get_op(proc);
	switch (hg_proc_op) {
	case HG_ENCODE:
		*proc_op = CRT_PROC_ENCODE;
		break;
	case HG_DECODE:
		*proc_op = CRT_PROC_DECODE;
		break;
	case HG_FREE:
		*proc_op = CRT_PROC_FREE;
		break;
	default:
		D_ERROR("Bad hg_proc_op: %d.\n", hg_proc_op);
		rc = -DER_INVAL;
	}

out:
	return rc;
}

int
crt_proc_memcpy(crt_proc_t proc, crt_proc_op_t proc_op,
		void *data, size_t data_size)
{
	void *buf;

	if (FREEING(proc_op))
		return 0;

	buf = hg_proc_save_ptr(proc, data_size);
	if (ENCODING(proc_op))
		memcpy(buf, data, data_size);
	else /* DECODING(proc_op) */
		memcpy(data, buf, data_size);

	return 0;
}

CRT_PROC_TYPE_FUNC(int8_t)
CRT_PROC_TYPE_FUNC(uint8_t)
CRT_PROC_TYPE_FUNC(int16_t)
CRT_PROC_TYPE_FUNC(uint16_t)
CRT_PROC_TYPE_FUNC(int32_t)
CRT_PROC_TYPE_FUNC(uint32_t)
CRT_PROC_TYPE_FUNC(int64_t)
CRT_PROC_TYPE_FUNC(uint64_t)
CRT_PROC_TYPE_FUNC(bool)

int
crt_proc_crt_bulk_t(crt_proc_t proc, crt_proc_op_t proc_op,
		    crt_bulk_t *bulk_hdl)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_bulk_t(proc, (hg_bulk_t *)bulk_hdl);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_d_string_t(crt_proc_t proc, crt_proc_op_t proc_op, d_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_d_const_string_t(crt_proc_t proc, crt_proc_op_t proc_op,
			  d_const_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_const_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_uuid_t(crt_proc_t proc, crt_proc_op_t proc_op, uuid_t *data)
{
	return crt_proc_memcpy(proc, proc_op, data, sizeof(uuid_t));
}

int
crt_proc_d_rank_list_t(crt_proc_t proc, crt_proc_op_t proc_op,
		       d_rank_list_t **data)
{
	d_rank_list_t	*rank_list;
	uint32_t	*buf;
	uint32_t	 nr;
	int		 rc = 0;

	if (unlikely(data == NULL)) {
		D_ERROR("Invalid parameter data: %p.\n", data);
		D_GOTO(out, rc = -DER_INVAL);
	}

	switch (proc_op) {
	case CRT_PROC_ENCODE:
		buf = hg_proc_save_ptr(proc, sizeof(*buf));

		rank_list = *data;
		if (rank_list == NULL) {
			*buf = 0;
			D_GOTO(out, rc = 0);
		}

		nr = rank_list->rl_nr;
		*buf = nr;
		buf = hg_proc_save_ptr(proc, nr * sizeof(*buf));
		memcpy(buf, rank_list->rl_ranks, nr * sizeof(*buf));
		break;
	case CRT_PROC_DECODE:
		buf = hg_proc_save_ptr(proc, sizeof(*buf));

		nr = *buf;
		if (nr == 0) {
			*data = NULL;
			D_GOTO(out, rc = 0);
		}

		rank_list = d_rank_list_alloc(nr);
		if (unlikely(rank_list == NULL))
			D_GOTO(out, rc = -DER_NOMEM);
		buf = hg_proc_save_ptr(proc, nr * sizeof(*buf));
		memcpy(rank_list->rl_ranks, buf, nr * sizeof(*buf));
		rank_list->rl_nr = nr;
		*data = rank_list;
		break;
	case CRT_PROC_FREE:
		d_rank_list_free(*data);
		*data = NULL;
		break;
	}

out:
	return rc;
}

int
crt_proc_d_iov_t(crt_proc_t proc, crt_proc_op_t proc_op, d_iov_t *div)
{
	int rc;

	if (unlikely(div == NULL))
		D_GOTO(out, rc = -DER_INVAL);

	if (FREEING(proc_op)) {
		div->iov_buf = NULL;
		div->iov_buf_len = 0;
		div->iov_len = 0;
		D_GOTO(out, rc = 0);
	}

	rc = crt_proc_uint64_t(proc, proc_op, &div->iov_buf_len);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_uint64_t(proc, proc_op, &div->iov_len);
	if (unlikely(rc))
		D_GOTO(out, rc);

	if (div->iov_buf_len < div->iov_len) {
		D_ERROR("invalid iov buf len "DF_U64" < iov len "DF_U64"\n",
			div->iov_buf_len, div->iov_len);
		D_GOTO(out, rc = -DER_HG);
	}

	if (DECODING(proc_op)) {
		if (div->iov_buf_len == 0) {
			div->iov_buf = NULL;
		} else {
			/**
			 * Don't allocate/memcpy like we do for others.
			 * Just point at memory in request buffer instead.
			 */
			div->iov_buf = hg_proc_save_ptr(proc, div->iov_len);
		}
	} else { /* ENCODING(proc_op) */
		rc = crt_proc_memcpy(proc, proc_op, div->iov_buf, div->iov_len);
	}

out:
	return rc;
}

static inline int
crt_proc_corpc_hdr(crt_proc_t proc, struct crt_corpc_hdr *hdr)
{
	crt_proc_op_t	 proc_op;
	uint32_t	*buf;
	int		 rc;

	if (unlikely(hdr == NULL))
		D_GOTO(out, rc = -DER_INVAL);

	rc = crt_proc_get_op(proc, &proc_op);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_crt_group_id_t(proc, proc_op, &hdr->coh_grpid);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_crt_bulk_t(proc, proc_op, &hdr->coh_bulk_hdl);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_d_rank_list_t(proc, proc_op, &hdr->coh_filter_ranks);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_d_rank_list_t(proc, proc_op, &hdr->coh_inline_ranks);
	if (unlikely(rc))
		D_GOTO(out, rc);

	if (FREEING(proc_op))
		D_GOTO(out, rc = 0);

	buf = hg_proc_save_ptr(proc, 4 * sizeof(*buf));
	if (ENCODING(proc_op)) {
		buf[0] = hdr->coh_grp_ver;
		buf[1] = hdr->coh_tree_topo;
		buf[2] = hdr->coh_root;
		buf[3] = hdr->coh_padding;
	} else { /* DECODING(proc_op) */
		hdr->coh_grp_ver   = buf[0];
		hdr->coh_tree_topo = buf[1];
		hdr->coh_root      = buf[2];
		hdr->coh_padding   = buf[3];
	}

out:
	return rc;
}

static inline int
crt_proc_common_hdr(crt_proc_t proc, struct crt_common_hdr *hdr)
{
	crt_proc_op_t	proc_op;
	int		rc;

	if (unlikely(hdr == NULL))
		D_GOTO(out, rc = -DER_INVAL);

	/*
	 * D_DEBUG("in crt_proc_common_hdr, opc: %#x.\n", hdr->cch_opc);
	 */

	rc = crt_proc_get_op(proc, &proc_op);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_memcpy(proc, proc_op, hdr, sizeof(*hdr));

out:
	return rc;
}

static double next_hlc_sync_err_report;

/*
 * Report an HLC synchronization error with a simple 1h-per-message rate
 * limitation. Not thread-safe, but the consequence is not too harmful.
 */
#define REPORT_HLC_SYNC_ERR(fmt, ...)					\
do {									\
	struct timespec	rhse_ts;					\
	double		rhse_now;					\
	int		rhse_rc;					\
									\
	rhse_rc = d_gettime(&rhse_ts);					\
	if (rhse_rc != 0)						\
		break;							\
	rhse_now = d_time2s(rhse_ts);					\
									\
	if (rhse_now >= next_hlc_sync_err_report) {			\
		D_CRIT(fmt, ## __VA_ARGS__);				\
		crt_trigger_hlc_error_cb();				\
		next_hlc_sync_err_report = rhse_now + 3600 /* 1h */;	\
	}								\
} while (0)

/* For unpacking only the common header to know about the CRT opc */
int
crt_hg_unpack_header(hg_handle_t handle, struct crt_rpc_priv *rpc_priv,
		     crt_proc_t *proc)
{
	/*
	 * Use some low level HG APIs to unpack header first and then unpack the
	 * body, avoid unpacking two times (which needs to lookup, create the
	 * proc multiple times).
	 * The potential risk is mercury possibly will not export those APIs
	 * later, and the hard-coded method HG_CRC32 used below which maybe
	 * different with future's mercury code change.
	 */
	void			*in_buf = NULL;
	hg_size_t		 in_buf_size;
	hg_class_t		*hg_class;
	struct crt_context	*ctx;
	struct crt_hg_context	*hg_ctx;
	uint64_t		 clock_offset;
	hg_proc_t		 hg_proc = HG_PROC_NULL;
	hg_return_t		 hg_ret = HG_SUCCESS;
	int			 rc;

	/* Get extra input buffer; if it's null, get regular input buffer */
	hg_ret = HG_Get_input_extra_buf(handle, &in_buf, &in_buf_size);
	if (hg_ret != HG_SUCCESS) {
		RPC_ERROR(rpc_priv, "HG_Get_input_extra_buf failed: %d\n", hg_ret);
		D_GOTO(out, rc = crt_hgret_2_der(hg_ret));
	}

	/* If extra buffer is null, rpc can fit into a regular buffer */
	if (in_buf == NULL) {
		hg_ret = HG_Get_input_buf(handle, &in_buf, &in_buf_size);
		if (hg_ret != HG_SUCCESS) {
			RPC_ERROR(rpc_priv, "HG_Get_input_buf failed: %d\n", hg_ret);
			D_GOTO(out, rc = crt_hgret_2_der(hg_ret));
		}
	}

	/* Create a new decoding proc */
	ctx = rpc_priv->crp_pub.cr_ctx;
	hg_ctx = &ctx->cc_hg_ctx;
	hg_class = hg_ctx->chc_hgcla;
	hg_ret   = hg_proc_create_set(hg_class, in_buf, in_buf_size, HG_DECODE, HG_CRC32, &hg_proc);
	if (hg_ret != HG_SUCCESS) {
		RPC_ERROR(rpc_priv, "hg_proc_create_set failed: %d\n", hg_ret);
		D_GOTO(out, rc = crt_hgret_2_der(hg_ret));
	}

	/* Decode header */
	rc = crt_proc_common_hdr(hg_proc, &rpc_priv->crp_req_hdr);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_proc_common_hdr failed: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Sync the HLC. Clients never decode requests. */
	D_ASSERT(crt_is_service());
	rc = d_hlc_get_msg(rpc_priv->crp_req_hdr.cch_hlc,
			     &ctx->cc_last_unpack_hlc, &clock_offset);
	if (rc != 0) {
		REPORT_HLC_SYNC_ERR("failed to sync HLC for request: opc=%x ts="
				    DF_U64" offset="DF_U64" from=%u\n",
				    rpc_priv->crp_req_hdr.cch_opc,
				    rpc_priv->crp_req_hdr.cch_hlc,
				    clock_offset,
				    rpc_priv->crp_req_hdr.cch_src_rank);

		/* Fail all but SWIM requests. */
		if (!crt_opc_is_swim(rpc_priv->crp_req_hdr.cch_opc))
			rpc_priv->crp_fail_hlc = 1;

		rc = 0;
	}

	rpc_priv->crp_flags = rpc_priv->crp_req_hdr.cch_flags;
	if (rpc_priv->crp_flags & CRT_RPC_FLAG_COLL) {
		rc = crt_proc_corpc_hdr(hg_proc, &rpc_priv->crp_coreq_hdr);
		if (rc != 0) {
			RPC_ERROR(rpc_priv, "crt_proc_corpc_hdr failed: "
				  DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

	*proc = hg_proc;

out:
	return rc;
}

/* Copy the RPC header from one descriptor to another */
void
crt_hg_header_copy(struct crt_rpc_priv *in, struct crt_rpc_priv *out)
{
	out->crp_hg_addr = in->crp_hg_addr;
	out->crp_hg_hdl = in->crp_hg_hdl;
	out->crp_pub.cr_ctx = in->crp_pub.cr_ctx;
	out->crp_flags = in->crp_flags;

	out->crp_req_hdr = in->crp_req_hdr;
	out->crp_reply_hdr.cch_hlc = in->crp_reply_hdr.cch_hlc;

	if (!(out->crp_flags & CRT_RPC_FLAG_COLL))
		return;

	out->crp_coreq_hdr = in->crp_coreq_hdr;
}

void
crt_hg_unpack_cleanup(crt_proc_t proc)
{
	if (proc != HG_PROC_NULL)
		hg_proc_free(proc);
}

static inline int
crt_proc_input(struct crt_rpc_priv *rpc_priv, crt_proc_t proc)
{
	struct crt_req_format *crf = rpc_priv->crp_opc_info->coi_crf;

	D_ASSERT(crf != NULL);
	return crf->crf_proc_in(proc, rpc_priv->crp_pub.cr_input);
}

static inline int
crt_proc_output(struct crt_rpc_priv *rpc_priv, crt_proc_t proc)
{
	struct crt_req_format *crf = rpc_priv->crp_opc_info->coi_crf;

	D_ASSERT(crf != NULL);
	return crf->crf_proc_out(proc, rpc_priv->crp_pub.cr_output);
}

int
crt_hg_unpack_body(struct crt_rpc_priv *rpc_priv, crt_proc_t proc)
{
	hg_return_t	hg_ret;
	int		rc;

	D_ASSERT(rpc_priv != NULL && proc != HG_PROC_NULL);

	/* Decode input parameters */
	rc = crt_proc_input(rpc_priv, proc);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_proc_input failed: "DF_RC"\n",
			  DP_RC(rc));
		D_GOTO(out, rc);
	}

	/* Flush proc */
	hg_ret = hg_proc_flush(proc);
	if (hg_ret != HG_SUCCESS) {
		RPC_ERROR(rpc_priv, "hg_proc_flush failed: %d\n", hg_ret);
		D_GOTO(out, rc);
	}
out:
	crt_hg_unpack_cleanup(proc);
	return rc;
}

/* NB: caller should pass in &rpc_pub->cr_input as the \param data */
int
crt_proc_in_common(crt_proc_t proc, crt_rpc_input_t *data)
{
	struct crt_rpc_priv	*rpc_priv;
	struct crt_common_hdr	*hdr;
	crt_proc_op_t		 proc_op;
	int			 rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, crp_pub.cr_input);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG("in crt_proc_in_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		if (ENCODING(proc_op)) {
			hdr = &rpc_priv->crp_req_hdr;

			hdr->cch_flags = rpc_priv->crp_flags;
			hdr->cch_dst_rank = crt_grp_priv_get_primary_rank(
						rpc_priv->crp_grp_priv,
						rpc_priv->crp_pub.cr_ep.ep_rank
						);
			hdr->cch_dst_tag = rpc_priv->crp_pub.cr_ep.ep_tag;

			hdr->cch_src_timeout = rpc_priv->crp_timeout_sec;
			if (crt_is_service()) {
				hdr->cch_src_rank =
					crt_grp_priv_get_primary_rank(
						rpc_priv->crp_grp_priv,
						rpc_priv->crp_grp_priv->gp_self
						);
				hdr->cch_hlc = d_hlc_get();
			} else {
				hdr->cch_src_rank = CRT_NO_RANK;
				/*
				 * Because client HLC timestamps shall never be
				 * used to sync server HLCs, we forward the
				 * HLCT reading, which must be either zero or a
				 * server HLC timestamp.
				 */
				hdr->cch_hlc = d_hlct_get();
			}
		}
		rc = crt_proc_common_hdr(proc, &rpc_priv->crp_req_hdr);
		if (rc != 0) {
			RPC_ERROR(rpc_priv, "crt_proc_common_hdr failed: "
				  DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
		/**
		 * crt_proc_in_common will be called in two paths:
		 * 1. Within HG_Forward -> hg_set_input ...,
		 *    to pack (ENCODE) in the request.
		 * 2. When received the RPC, if user calls HG_Get_input it call
		 *    this function with DECODE, but the handling was changed to
		 *    crt_hg_unpack_header + _unpack_body and the direct call
		 *    of HG_Get_inputwas removed.
		 *
		 * XXXX: Keep assertion here to avoid silent logic change.
		 */
		D_ASSERT(proc_op != CRT_PROC_DECODE);
	}

	if (rpc_priv->crp_flags & CRT_RPC_FLAG_COLL) {
		rc = crt_proc_corpc_hdr(proc, &rpc_priv->crp_coreq_hdr);
		if (rc != 0) {
			RPC_ERROR(rpc_priv, "crt_proc_corpc_hdr failed: "
				  DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
	}

	if (*data == NULL) {
		/*
		D_DEBUG("crt_proc_in_common, opc: %#x, NULL input.\n",
			rpc_priv->crp_req_hdr.cch_opc);
		*/
		D_GOTO(out, rc);
	}

	rc = crt_proc_input(rpc_priv, proc);
	if (rc != 0) {
		RPC_ERROR(rpc_priv, "crt_proc_input failed: "DF_RC"\n",
			  DP_RC(rc));
		D_GOTO(out, rc);
	}
out:
	return crt_der_2_hgret(rc);
}

/* NB: caller should pass in &rpc_pub->cr_output as the \param data */
int
crt_proc_out_common(crt_proc_t proc, crt_rpc_output_t *data)
{
	struct crt_rpc_priv	*rpc_priv;
	crt_proc_op_t		 proc_op;
	int			 rc = 0;
	int			 rc2;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, crp_pub.cr_output);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG("in crt_proc_out_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		if (ENCODING(proc_op)) {
			/* Clients never encode replies. */
			D_ASSERT(crt_is_service());
			rpc_priv->crp_reply_hdr.cch_hlc = d_hlc_get();
		}
		rc = crt_proc_common_hdr(proc, &rpc_priv->crp_reply_hdr);
		if (rc != 0) {
			RPC_ERROR(rpc_priv, "crt_proc_common_hdr failed: "
				  DF_RC"\n", DP_RC(rc));
			D_GOTO(out, rc);
		}
		if (DECODING(proc_op)) {
			struct crt_common_hdr *hdr = &rpc_priv->crp_reply_hdr;

			if (crt_is_service()) {
				uint64_t clock_offset;

				rc = d_hlc_get_msg(hdr->cch_hlc,
						     NULL /* hlc_out */,
						     &clock_offset);
				if (rc != 0) {
					REPORT_HLC_SYNC_ERR("failed to sync "
							    "HLC for reply: "
							    "opc=%x ts="DF_U64
							    " offset="DF_U64
							    " from=%u\n",
							    hdr->cch_opc,
							    hdr->cch_hlc,
							    clock_offset,
							    hdr->cch_dst_rank);
					/* Fail all but SWIM replies. */
					if (!crt_opc_is_swim(hdr->cch_opc))
						rpc_priv->crp_fail_hlc = 1;

					rc = 0;
				}
			} else {
				d_hlct_sync(hdr->cch_hlc);
			}
		}

		rc2 = rpc_priv->crp_reply_hdr.cch_rc;
		if (rc2 != 0) {
			if (rpc_priv->crp_reply_hdr.cch_rc != -DER_GRPVER)
				RPC_ERROR(rpc_priv,
					  "RPC failed to execute on target. "
					  "error code: "DF_RC"\n", DP_RC(rc2));
			else
				RPC_TRACE(DB_NET, rpc_priv,
					  "RPC failed to execute on target. "
					  "error code: "DF_RC"\n", DP_RC(rc2));

			D_GOTO(out, rc);
		}
	}

	if (*data == NULL) {
		/*
		D_DEBUG("crt_proc_out_common, opc: %#x, NULL output.\n",
			rpc_priv->crp_req_hdr.cch_opc);
		*/
		D_GOTO(out, rc);
	}

	rc = crt_proc_output(rpc_priv, proc);
out:
	return crt_der_2_hgret(rc);
}

int
crt_proc_create(crt_context_t crt_ctx, void *buf, size_t buf_size,
		crt_proc_op_t proc_op, crt_proc_t *proc)
{
	struct crt_context	*ctx = crt_ctx;
	hg_proc_t		 hg_proc;
	hg_return_t		 hg_ret;
	hg_proc_op_t		 hg_op = 0;
	int			 rc = 0;

	rc = crt_proc_op2hg(proc_op, &hg_op);
	D_ASSERT(rc == 0);

	hg_ret = hg_proc_create_set(ctx->cc_hg_ctx.chc_hgcla, buf, buf_size,
				    hg_op, HG_NOHASH, &hg_proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Failed to create CaRT proc: %d\n", hg_ret);
		rc = crt_hgret_2_der(hg_ret);
	} else {
		*proc = (crt_proc_t)hg_proc;
	}

	return rc;
}

int
crt_proc_destroy(crt_proc_t proc)
{
	hg_proc_t	hg_proc = (hg_proc_t)proc;
	hg_return_t	hg_ret;
	int		rc = 0;

	hg_ret = hg_proc_free(hg_proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Failed to destroy CaRT proc: %d\n", hg_ret);
		rc = crt_hgret_2_der(hg_ret);
	}

	return rc;
}

int
crt_proc_reset(crt_proc_t proc, void *buf, size_t buf_size, crt_proc_op_t proc_op)
{
	hg_proc_t	hg_proc = (hg_proc_t)proc;
	hg_return_t	hg_ret;
	hg_proc_op_t	hg_op = 0;
	int		rc = 0;

	rc = crt_proc_op2hg(proc_op, &hg_op);
	D_ASSERT(rc == 0);

	hg_ret = hg_proc_reset(hg_proc, buf, buf_size, hg_op);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Failed to reset CaRT proc to op %d: %d\n", proc_op, hg_ret);
		rc = crt_hgret_2_der(hg_ret);
	}

	return rc;
}

size_t
crp_proc_get_size_used(crt_proc_t proc)
{
	hg_proc_t	hg_proc = (hg_proc_t)proc;
	hg_size_t	hg_size;

	hg_size = hg_proc_get_size_used(hg_proc);

	return (size_t)hg_size;
}
