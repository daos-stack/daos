/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of CaRT. It implements the main input/output
 * parameter serialization/de-serialization routines (proc functions).
 */
#define D_LOGFAC	DD_FAC(hg)

#include <gurt/mem.h>

#include "crt_internal.h"

#define CRT_PROC_NULL (NULL)
#define CRT_PROC_TYPE_FUNC(type)				\
	int crt_proc_##type(crt_proc_t proc, type *data)	\
	{							\
		crt_proc_op_t	 proc_op;			\
		type		*buf;				\
		int		 rc = 0;			\
		rc = crt_proc_get_op(proc, &proc_op);		\
		if (unlikely(rc))				\
			return -DER_HG;				\
		if (proc_op == CRT_PROC_FREE)			\
			return rc;				\
		buf = hg_proc_save_ptr(proc, sizeof(*buf));	\
		switch (proc_op) {				\
		case CRT_PROC_ENCODE:				\
			*buf = *data;				\
			break;					\
		case CRT_PROC_DECODE:				\
			*data = *buf;				\
			break;					\
		default:					\
			break;					\
		}						\
		return rc;					\
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
		D_ERROR("invalid parameter - NULL proc_op.\n");
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
		D_ERROR("bad hg_proc_op: %d.\n", hg_proc_op);
		rc = -DER_INVAL;
	}

out:
	return rc;
}

int
crt_proc_memcpy(crt_proc_t proc, void *data, size_t data_size)
{
	crt_proc_op_t	 proc_op;
	void		*buf;
	int		 rc = 0;

	rc = crt_proc_get_op(proc, &proc_op);
	if (unlikely(rc))
		return -DER_HG;

	if (proc_op == CRT_PROC_FREE)
		return rc;

	buf = hg_proc_save_ptr(proc, data_size);
	switch (proc_op) {
	case CRT_PROC_ENCODE:
		d_memcpy(buf, data, data_size);
		break;
	case CRT_PROC_DECODE:
		d_memcpy(data, buf, data_size);
		break;
	default:
		break;
	}

	return rc;
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
crt_proc_crt_bulk_t(crt_proc_t proc, crt_bulk_t *bulk_hdl)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_bulk_t(proc, (hg_bulk_t *)bulk_hdl);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_d_string_t(crt_proc_t proc, d_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_d_const_string_t(crt_proc_t proc, d_const_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_const_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_uuid_t(crt_proc_t proc, uuid_t *data)
{
	return crt_proc_memcpy(proc, data, sizeof(uuid_t));
}

int
crt_proc_d_rank_list_t(crt_proc_t proc, d_rank_list_t **data)
{
	d_rank_list_t	*rank_list;
	crt_proc_op_t	 proc_op;
	uint32_t	*buf;
	uint32_t	 nr;
	int		 rc = 0;

	if (unlikely(data == NULL)) {
		D_ERROR("Invalid parameter data: %p.\n", data);
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_proc_get_op(proc, &proc_op);
	if (unlikely(rc))
		D_GOTO(out, rc = -DER_HG);

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

		D_ALLOC_PTR(rank_list);
		if (rank_list == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		D_ALLOC_ARRAY(rank_list->rl_ranks, nr);
		if (rank_list->rl_ranks == NULL) {
			D_FREE(rank_list);
			D_GOTO(out, rc = -DER_NOMEM);
		}
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
crt_proc_d_iov_t(crt_proc_t proc, d_iov_t *div)
{
	crt_proc_op_t	proc_op;
	int		rc = 0;

	if (unlikely(div == NULL)) {
		D_ERROR("invalid parameter, NULL div.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = crt_proc_get_op(proc, &proc_op);
	if (unlikely(rc))
		D_GOTO(out, rc = -DER_HG);

	if (proc_op == CRT_PROC_FREE) {
		div->iov_buf = NULL;
		div->iov_buf_len = 0;
		div->iov_len = 0;
		D_GOTO(out, rc = 0);
	}

	rc = crt_proc_uint64_t(proc, &div->iov_buf_len);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_uint64_t(proc, &div->iov_len);
	if (unlikely(rc))
		D_GOTO(out, rc);

	if (div->iov_buf_len < div->iov_len) {
		D_ERROR("invalid iov buf len "DF_U64" < iov len "DF_U64"\n",
			div->iov_buf_len, div->iov_len);
		D_GOTO(out, rc = -DER_HG);
	}

	if (proc_op == CRT_PROC_DECODE) {
		if (div->iov_buf_len == 0) {
			div->iov_buf = NULL;
		} else {
			/**
			 * Don't allocate/memcpy like we do for others.
			 * Just point at memory in request buffer instead.
			 */
			div->iov_buf = hg_proc_save_ptr(proc, div->iov_len);
		}
	} else { /* proc_op == CRT_PROC_ENCODE */
		rc = crt_proc_memcpy(proc, div->iov_buf, div->iov_len);
	}

out:
	return rc;
}

static inline int
crt_proc_corpc_hdr(crt_proc_t proc, struct crt_corpc_hdr *hdr)
{
	crt_proc_op_t	 proc_op;
	uint32_t	*buf;
	int		 rc = 0;

	if (unlikely(proc == CRT_PROC_NULL || hdr == NULL))
		D_GOTO(out, rc = -DER_INVAL);

	rc = crt_proc_get_op(proc, &proc_op);
	if (unlikely(rc))
		D_GOTO(out, rc = -DER_HG);

	rc = crt_proc_crt_group_id_t(proc, &hdr->coh_grpid);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_crt_bulk_t(proc, &hdr->coh_bulk_hdl);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_d_rank_list_t(proc, &hdr->coh_filter_ranks);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_d_rank_list_t(proc, &hdr->coh_inline_ranks);
	if (unlikely(rc))
		D_GOTO(out, rc);

	switch (proc_op) {
	case CRT_PROC_ENCODE:
		buf = hg_proc_save_ptr(proc, 4 * sizeof(*buf));
		buf[0] = hdr->coh_grp_ver;
		buf[1] = hdr->coh_tree_topo;
		buf[2] = hdr->coh_root;
		buf[3] = hdr->coh_padding;
		break;
	case CRT_PROC_DECODE:
		buf = hg_proc_save_ptr(proc, 4 * sizeof(*buf));
		hdr->coh_grp_ver   = buf[0];
		hdr->coh_tree_topo = buf[1];
		hdr->coh_root      = buf[2];
		hdr->coh_padding   = buf[3];
		break;
	case CRT_PROC_FREE:
		break;
	}

out:
	return rc;
}

static inline int
crt_proc_common_hdr(crt_proc_t proc, struct crt_common_hdr *hdr)
{
	int rc;

	/*
	 * D_DEBUG("in crt_proc_common_hdr, opc: %#x.\n", hdr->cch_opc);
	 */

	if (unlikely(proc == CRT_PROC_NULL || hdr == NULL))
		D_GOTO(out, rc = -DER_INVAL);

	rc = crt_proc_memcpy(proc, hdr, sizeof(*hdr));

out:
	return rc;
}

/* For unpacking only the common header to know about the CRT opc */
int
crt_hg_unpack_header(hg_handle_t handle, struct crt_rpc_priv *rpc_priv,
		     crt_proc_t *proc)
{
	int	rc = 0;

	/*
	 * Use some low level HG APIs to unpack header first and then unpack the
	 * body, avoid unpacking two times (which needs to lookup, create the
	 * proc multiple times).
	 * The potential risk is mercury possibly will not export those APIs
	 * later, and the hard-coded method HG_CRC32 used below which maybe
	 * different with future's mercury code change.
	 */
	void			*in_buf = NULL;
	hg_size_t		in_buf_size;
	hg_return_t		hg_ret = HG_SUCCESS;
	hg_class_t		*hg_class;
	struct crt_context	*ctx;
	struct crt_hg_context	*hg_ctx;
	hg_proc_t		hg_proc = HG_PROC_NULL;

	/* Get extra input buffer; if it's null, get regular input buffer */
	hg_ret = HG_Get_input_extra_buf(handle, &in_buf, &in_buf_size);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not get extra input buff, hg_ret: %d.", hg_ret);
		D_GOTO(out, rc = -DER_HG);
	}

	/* If extra buffer is null, rpc can fit into a regular buffer */
	if (in_buf == NULL) {
		hg_ret = HG_Get_input_buf(handle, &in_buf, &in_buf_size);
		if (hg_ret != HG_SUCCESS) {
			D_ERROR("Could not get input buf, hg_ret: %d.", hg_ret);
			D_GOTO(out, rc = -DER_HG);
		}
	}

	/* Create a new decoding proc */
	ctx = rpc_priv->crp_pub.cr_ctx;
	hg_ctx = &ctx->cc_hg_ctx;
	hg_class = hg_ctx->chc_hgcla;
	hg_ret = hg_proc_create_set(hg_class, in_buf, in_buf_size, HG_DECODE,
				    HG_CRC32, &hg_proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not create proc, hg_ret: %d.", hg_ret);
		D_GOTO(out, rc = -DER_HG);
	}

	/* Decode header */
	rc = crt_proc_common_hdr(hg_proc, &rpc_priv->crp_req_hdr);
	if (rc != 0) {
		D_ERROR("crt_proc_common_hdr failed rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	/* Clients never decode requests. */
	D_ASSERT(crt_is_service());
	(void)crt_hlc_get_msg(rpc_priv->crp_req_hdr.cch_hlc);
	rpc_priv->crp_flags = rpc_priv->crp_req_hdr.cch_flags;
	if (rpc_priv->crp_flags & CRT_RPC_FLAG_COLL) {
		rc = crt_proc_corpc_hdr(hg_proc, &rpc_priv->crp_coreq_hdr);
		if (rc != 0) {
			D_ERROR("crt_proc_corpc_hdr failed rc: %d.\n", rc);
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
	int	rc = 0;

	hg_return_t	hg_ret;

	D_ASSERT(rpc_priv != NULL && proc != HG_PROC_NULL);

	/* Decode input parameters */
	rc = crt_proc_input(rpc_priv, proc);
	if (rc != 0) {
		D_ERROR("crt_hg_unpack_body failed, rc: %d, opc: %#x.\n",
			rc, rpc_priv->crp_pub.cr_opc);
		D_GOTO(out, rc);
	}

	/* Flush proc */
	hg_ret = hg_proc_flush(proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Error in proc flush, hg_ret: %d, opc: %#x.",
			hg_ret, rpc_priv->crp_pub.cr_opc);
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
	crt_proc_op_t		 proc_op;
	struct crt_common_hdr	*hdr;
	int			 rc = 0;

	if (proc == CRT_PROC_NULL)
		D_GOTO(out, rc = -DER_INVAL);

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, crp_pub.cr_input);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG("in crt_proc_in_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		if (proc_op == CRT_PROC_ENCODE) {
			hdr = &rpc_priv->crp_req_hdr;

			hdr->cch_flags = rpc_priv->crp_flags;
			hdr->cch_dst_rank = crt_grp_priv_get_primary_rank(
						rpc_priv->crp_grp_priv,
						rpc_priv->crp_pub.cr_ep.ep_rank
						);
			hdr->cch_dst_tag = rpc_priv->crp_pub.cr_ep.ep_tag;

			if (crt_is_service()) {
				hdr->cch_src_rank =
					crt_grp_priv_get_primary_rank(
						rpc_priv->crp_grp_priv,
						rpc_priv->crp_grp_priv->gp_self
						);
				hdr->cch_hlc = crt_hlc_get();
			} else {
				hdr->cch_src_rank = CRT_NO_RANK;
				/*
				 * Because client HLC timestamps shall never be
				 * used to sync server HLCs, we forward the
				 * HLCT reading, which must be either zero or a
				 * server HLC timestamp.
				 */
				hdr->cch_hlc = crt_hlct_get();
			}
		}
		rc = crt_proc_common_hdr(proc, &rpc_priv->crp_req_hdr);
		if (rc != 0) {
			D_ERROR("crt_proc_common_hdr failed rc: %d.\n", rc);
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
			D_ERROR("crt_proc_corpc_hdr failed rc: %d.\n", rc);
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
		D_ERROR("unpack input fails for opc: %#x\n",
			rpc_priv->crp_pub.cr_opc);
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

	if (proc == CRT_PROC_NULL)
		D_GOTO(out, rc = -DER_INVAL);

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, crp_pub.cr_output);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG("in crt_proc_out_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		if (proc_op == CRT_PROC_ENCODE) {
			/* Clients never encode replies. */
			D_ASSERT(crt_is_service());
			rpc_priv->crp_reply_hdr.cch_hlc = crt_hlc_get();
		}
		rc = crt_proc_common_hdr(proc, &rpc_priv->crp_reply_hdr);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_proc_common_hdr failed rc: %d\n", rc);
			D_GOTO(out, rc);
		}
		if (proc_op == CRT_PROC_DECODE) {
			uint64_t t = rpc_priv->crp_reply_hdr.cch_hlc;

			if (crt_is_service())
				crt_hlc_get_msg(t);
			else
				crt_hlct_sync(t);
		}
		if (rpc_priv->crp_reply_hdr.cch_rc != 0) {
			RPC_ERROR(rpc_priv,
				  "RPC failed to execute on target. error code: %d\n",
				  rpc_priv->crp_reply_hdr.cch_rc);
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
