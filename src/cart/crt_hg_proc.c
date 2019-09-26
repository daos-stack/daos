/* Copyright (C) 2016-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of CaRT. It implements the main input/output
 * parameter serialization/de-serialization routines (proc functions).
 */
#define D_LOGFAC	DD_FAC(hg)

#include "crt_internal.h"

#define CRT_PROC_NULL (NULL)

int
crt_proc_get_op(crt_proc_t proc, crt_proc_op_t *proc_op)
{
	hg_proc_op_t	hg_proc_op;
	int		rc = 0;

	if (proc == NULL) {
		D_ERROR("Proc is not initilalized.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (proc_op == NULL) {
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
	hg_return_t	hg_ret;

	hg_ret = hg_proc_memcpy(proc, data, data_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_int8_t(crt_proc_t proc, int8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_uint8_t(crt_proc_t proc, uint8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_int16_t(crt_proc_t proc, int16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_uint16_t(crt_proc_t proc, uint16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_int32_t(crt_proc_t proc, int32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_uint32_t(crt_proc_t proc, uint32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_int64_t(crt_proc_t proc, int64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_uint64_t(crt_proc_t proc, uint64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_bool(crt_proc_t proc, bool *data)
{
	hg_bool_t	hg_bool;
	hg_return_t	hg_ret;

	hg_bool = (*data == false) ? 0 : 1;
	hg_ret = hg_proc_hg_bool_t(proc, &hg_bool);
	*data = (hg_bool == 0) ? false : true;

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

int
crt_proc_raw(crt_proc_t proc, void *buf, size_t buf_size)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_raw(proc, buf, buf_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_HG;
}

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
	d_rank_list_t		*rank_list;
	hg_proc_op_t		 proc_op;
	uint32_t		 rank_num;
	int			 i, rc = 0;

	if (proc == NULL || data == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n", proc, data);
		D_GOTO(out, rc = -DER_INVAL);
	}

	proc_op = hg_proc_get_op(proc);
	switch (proc_op) {
	case HG_ENCODE:
		rank_list = *data;
		if (rank_list == NULL) {
			rank_num = 0;
			rc = crt_proc_uint32_t(proc, &rank_num);
			if (rc != 0)
				D_ERROR("crt_proc_uint32_t failed, rc: %d.\n",
					rc);
			D_GOTO(out, rc);
		}

		rank_num = rank_list->rl_nr;
		rc = crt_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			D_ERROR("crt_proc_uint32_t failed, rc: %d.\n",
				rc);
			D_GOTO(out, rc = -DER_HG);
		}
		for (i = 0; i < rank_num; i++) {
			rc = crt_proc_d_rank_t(proc, &rank_list->rl_ranks[i]);
			if (rc != 0) {
				D_ERROR("crt_proc_d_rank_t failed,rc: %d.\n",
					rc);
				D_GOTO(out, rc = -DER_HG);
			}
		}
		break;
	case HG_DECODE:
		rc = crt_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			D_ERROR("crt_proc_uint32_t failed, rc: %d.\n",
				rc);
			D_GOTO(out, rc = -DER_HG);
		}
		if (rank_num == 0) {
			*data = NULL;
			D_GOTO(out, rc);
		}
		D_ALLOC_PTR(rank_list);
		if (rank_list == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
		rank_list->rl_nr = rank_num;
		D_ALLOC_ARRAY(rank_list->rl_ranks, rank_num);
		if (rank_list->rl_ranks == NULL) {
			D_FREE(rank_list);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		for (i = 0; i < rank_num; i++) {
			rc = crt_proc_d_rank_t(proc, &rank_list->rl_ranks[i]);
			if (rc != 0) {
				D_ERROR("crt_proc_d_rank_t failed,rc: %d.\n",
					rc);
				D_GOTO(out, rc = -DER_HG);
			}
		}
		*data = rank_list;
		break;
	case HG_FREE:
		rank_list = *data;
		d_rank_list_free(rank_list);
		*data = NULL;
		break;
	default:
		D_ERROR("Bad proc op: %d.\n", proc_op);
		D_GOTO(out, rc = -DER_HG);
	}

out:
	return rc;
}

int
crt_proc_d_iov_t(crt_proc_t proc, d_iov_t *div)
{
	crt_proc_op_t	proc_op;
	int		rc;

	if (div == NULL) {
		D_ERROR("invalid parameter, NULL div.\n");
		return -DER_INVAL;
	}

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_FREE) {
		if (div->iov_buf_len > 0)
			D_FREE(div->iov_buf);
		div->iov_buf_len = 0;
		div->iov_len = 0;
		return 0;
	}

	rc = crt_proc_uint64_t(proc, &div->iov_len);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &div->iov_buf_len);
	if (rc != 0)
		return -DER_HG;

	if (div->iov_buf_len < div->iov_len) {
		D_ERROR("invalid iov buf len "DF_U64" < iov len "DF_U64"\n",
			div->iov_buf_len, div->iov_len);
		return -DER_HG;
	}
	if (proc_op == CRT_PROC_DECODE) {
		if (div->iov_buf_len > 0) {
			D_ALLOC(div->iov_buf, div->iov_buf_len);
			if (div->iov_buf == NULL)
				return -DER_NOMEM;
		} else {
			div->iov_buf = NULL;
		}
	}

	rc = crt_proc_memcpy(proc, div->iov_buf, div->iov_len);
	if (rc != 0) {
		if (proc_op == CRT_PROC_DECODE) {
			D_FREE(div->iov_buf);
			div->iov_buf_len = 0;
			div->iov_len = 0;
		}
		return -DER_HG;
	}

	return 0;
}

static inline int
crt_proc_corpc_hdr(crt_proc_t proc, struct crt_corpc_hdr *hdr)
{
	hg_proc_t     hg_proc;
	hg_return_t   hg_ret = HG_SUCCESS;
	int           rc = 0;

	if (proc == CRT_PROC_NULL || hdr == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	hg_proc = proc;
	rc = crt_proc_crt_group_id_t(hg_proc, &hdr->coh_grpid);
	if (rc != 0) {
		D_ERROR("crt proc error, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	rc = crt_proc_crt_bulk_t(hg_proc, &hdr->coh_bulk_hdl);
	if (rc != 0) {
		D_ERROR("crt proc error, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	rc = crt_proc_d_rank_list_t(hg_proc, &hdr->coh_filter_ranks);
	if (rc != 0) {
		D_ERROR("crt proc error, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	rc = crt_proc_d_rank_list_t(hg_proc, &hdr->coh_inline_ranks);
	if (rc != 0) {
		D_ERROR("crt proc error, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->coh_grp_ver);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->coh_tree_topo);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->coh_root);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->coh_padding);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		rc = -DER_HG;
	}

out:
	return rc;
}

static inline int
crt_proc_common_hdr(crt_proc_t proc, struct crt_common_hdr *hdr)
{
	hg_proc_t     hg_proc;
	hg_return_t   hg_ret = HG_SUCCESS;
	int           rc = 0;

	/*
	 * D_DEBUG("in crt_proc_common_hdr, opc: %#x.\n", hdr->cch_opc);
	 */

	if (proc == CRT_PROC_NULL || hdr == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	hg_proc = proc;
	hg_ret = hg_proc_memcpy(hg_proc, hdr, sizeof(*hdr));
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_HG);
	}

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
	void			*in_buf;
	hg_size_t		in_buf_size;
	hg_return_t		hg_ret = HG_SUCCESS;
	hg_class_t		*hg_class;
	struct crt_context	*ctx;
	struct crt_hg_context	*hg_ctx;
	hg_proc_t		hg_proc = HG_PROC_NULL;

	/* Get input buffer */
	hg_ret = HG_Get_input_buf(handle, &in_buf, &in_buf_size);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not get input buffer, hg_ret: %d.", hg_ret);
		D_GOTO(out, rc = -DER_HG);
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
		return -DER_HG;

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, crp_pub.cr_input);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG("in crt_proc_in_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		if (proc_op == CRT_PROC_ENCODE) {
			hdr = &rpc_priv->crp_req_hdr;

			hdr->cch_flags = rpc_priv->crp_flags;
			hdr->cch_tag = rpc_priv->crp_pub.cr_ep.ep_tag;
			hdr->cch_hlc = crt_hlc_get();
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
		 *    this funcion with DECODE, but the handling was changed to
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
	return rc;
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
		return -DER_HG;

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, crp_pub.cr_output);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG("in crt_proc_out_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		if (proc_op == CRT_PROC_ENCODE)
			rpc_priv->crp_reply_hdr.cch_hlc = crt_hlc_get();
		rc = crt_proc_common_hdr(proc, &rpc_priv->crp_reply_hdr);
		if (rc != 0) {
			RPC_ERROR(rpc_priv,
				  "crt_proc_common_hdr failed rc: %d\n", rc);
			D_GOTO(out, rc);
		}
		if (proc_op == CRT_PROC_DECODE)
			(void)crt_hlc_get_msg(rpc_priv->crp_reply_hdr.cch_hlc);
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
	return rc;
}
