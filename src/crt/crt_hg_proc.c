/* Copyright (C) 2016 Intel Corporation
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

#include <crt_internal.h>

#define CRT_PROC_NULL (NULL)

static inline hg_proc_op_t
crt_proc_op2hg(crt_proc_op_t proc_op)
{
	if (proc_op == CRT_PROC_ENCODE)
		return HG_ENCODE;
	else if (proc_op == CRT_PROC_DECODE)
		return HG_DECODE;
	else if (proc_op == CRT_PROC_FREE)
		return HG_FREE;
	else
		return -CER_INVAL;
}

int
crt_proc_get_op(crt_proc_t proc, crt_proc_op_t *proc_op)
{
	hg_proc_op_t	hg_proc_op;
	int		rc = 0;

	if (proc == NULL) {
		C_ERROR("Proc is not initilalized.\n");
		C_GOTO(out, rc = -CER_INVAL);
	}
	if (proc_op == NULL) {
		C_ERROR("invalid parameter - NULL proc_op.\n");
		C_GOTO(out, rc = -CER_INVAL);
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
		C_ERROR("bad hg_proc_op: %d.\n", hg_proc_op);
		rc = -CER_INVAL;
	}

out:
	return rc;
}

int
crt_proc_memcpy(crt_proc_t proc, void *data, crt_size_t data_size)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_memcpy(proc, data, data_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_int8_t(crt_proc_t proc, int8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_uint8_t(crt_proc_t proc, uint8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_int16_t(crt_proc_t proc, int16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_uint16_t(crt_proc_t proc, uint16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_int32_t(crt_proc_t proc, int32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_uint32_t(crt_proc_t proc, uint32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_int64_t(crt_proc_t proc, int64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_uint64_t(crt_proc_t proc, uint64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_bool(crt_proc_t proc, bool *data)
{
	hg_bool_t	hg_bool;
	hg_return_t	hg_ret;

	hg_bool = (*data == false) ? 0 : 1;
	hg_ret = hg_proc_hg_bool_t(proc, &hg_bool);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_raw(crt_proc_t proc, void *buf, crt_size_t buf_size)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_raw(proc, buf, buf_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_crt_bulk_t(crt_proc_t proc, crt_bulk_t *bulk_hdl)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_bulk_t(proc, (hg_bulk_t *)bulk_hdl);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_crt_string_t(crt_proc_t proc, crt_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_crt_const_string_t(crt_proc_t proc, crt_const_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_const_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -CER_HG;
}

int
crt_proc_uuid_t(crt_proc_t proc, uuid_t *data)
{
	return crt_proc_memcpy(proc, data, sizeof(uuid_t));
}

int
crt_proc_crt_rank_list_t(crt_proc_t proc, crt_rank_list_t **data)
{
	crt_rank_list_t	*rank_list;
	hg_proc_op_t		proc_op;
	uint32_t		rank_num;
	int			i, rc = 0;

	if (proc == NULL || data == NULL) {
		C_ERROR("Invalid parameter, proc: %p, data: %p.\n", proc, data);
		C_GOTO(out, rc = -CER_INVAL);
	}

	proc_op = hg_proc_get_op(proc);
	switch (proc_op) {
	case HG_ENCODE:
		rank_list = *data;
		if (rank_list == NULL) {
			rank_num = 0;
			rc = crt_proc_uint32_t(proc, &rank_num);
			if (rc != 0)
				C_ERROR("crt_proc_uint32_t failed, rc: %d.\n",
					rc);
			C_GOTO(out, rc);
		}

		rank_num = rank_list->rl_nr.num;
		rc = crt_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			C_ERROR("crt_proc_uint32_t failed, rc: %d.\n",
				rc);
			C_GOTO(out, rc = -CER_HG);
		}
		for (i = 0; i < rank_num; i++) {
			rc = crt_proc_crt_rank_t(proc,
						  &rank_list->rl_ranks[i]);
			if (rc != 0) {
				C_ERROR("crt_proc_crt_rank_t failed,rc: %d.\n",
					rc);
				C_GOTO(out, rc = -CER_HG);
			}
		}
		break;
	case HG_DECODE:
		rc = crt_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			C_ERROR("crt_proc_uint32_t failed, rc: %d.\n",
				rc);
			C_GOTO(out, rc = -CER_HG);
		}
		if (rank_num == 0) {
			*data = NULL;
			C_GOTO(out, rc);
		}
		C_ALLOC_PTR(rank_list);
		if (rank_list == NULL) {
			C_ERROR("Cannot allocate memory for rank list.\n");
			C_GOTO(out, rc = -CER_NOMEM);
		}
		rank_list->rl_nr.num = rank_num;
		C_ALLOC(rank_list->rl_ranks, rank_num * sizeof(crt_rank_t));
		if (rank_list->rl_ranks == NULL) {
			C_ERROR("Cannot allocate memory for rl_ranks.\n");
			C_FREE_PTR(rank_list);
			C_GOTO(out, rc = -CER_NOMEM);
		}
		for (i = 0; i < rank_num; i++) {
			rc = crt_proc_crt_rank_t(proc,
						  &rank_list->rl_ranks[i]);
			if (rc != 0) {
				C_ERROR("crt_proc_daso_rank_t failed,rc: %d.\n",
					rc);
				C_GOTO(out, rc = -CER_HG);
			}
		}
		*data = rank_list;
		break;
	case HG_FREE:
		rank_list = *data;
		crt_rank_list_free(rank_list);
		*data = NULL;
		break;
	default:
		C_ERROR("Bad proc op: %d.\n", proc_op);
		C_GOTO(out, rc = -CER_HG);
	}

out:
	return rc;
}

int
crt_proc_crt_iov_t(crt_proc_t proc, crt_iov_t *div)
{
	crt_proc_op_t	proc_op;
	int		rc;

	if (div == NULL) {
		C_ERROR("invalid parameter, NULL div.\n");
		return -CER_INVAL;
	}

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -CER_HG;

	if (proc_op == CRT_PROC_FREE) {
		if (div->iov_buf_len > 0)
			C_FREE(div->iov_buf, div->iov_buf_len);
		return 0;
	}

	rc = crt_proc_uint64_t(proc, &div->iov_len);
	if (rc != 0)
		return -CER_HG;

	rc = crt_proc_uint64_t(proc, &div->iov_buf_len);
	if (rc != 0)
		return -CER_HG;

	if (div->iov_buf_len < div->iov_len) {
		C_ERROR("invalid iov buf len "CF_U64" < iov len "CF_U64"\n",
			div->iov_buf_len, div->iov_len);
		return -CER_HG;
	}
	if (proc_op == CRT_PROC_DECODE) {
		if (div->iov_buf_len > 0) {
			C_ALLOC(div->iov_buf, div->iov_buf_len);
			if (div->iov_buf == NULL)
				return -CER_NOMEM;
		} else {
			div->iov_buf = NULL;
		}
	}

	rc = crt_proc_memcpy(proc, div->iov_buf, div->iov_len);
	if (rc != 0) {
		if (proc_op == CRT_PROC_DECODE)
			C_FREE(div->iov_buf, div->iov_buf_len);
		return -CER_HG;
	}

	return 0;
}

struct crt_msg_field DMF_UUID =
	DEFINE_CRT_MSG("crt_uuid", 0, sizeof(uuid_t),
		       crt_proc_uuid_t);

struct crt_msg_field DMF_GRP_ID =
	DEFINE_CRT_MSG("crt_group_id", 0, sizeof(crt_group_id_t),
		       crt_proc_crt_group_id_t);

struct crt_msg_field DMF_INT =
	DEFINE_CRT_MSG("crt_int", 0, sizeof(int32_t),
		       crt_proc_int);

struct crt_msg_field DMF_UINT32 =
	DEFINE_CRT_MSG("crt_uint32", 0, sizeof(uint32_t),
		       crt_proc_uint32_t);

struct crt_msg_field DMF_UINT64 =
	DEFINE_CRT_MSG("crt_uint64", 0, sizeof(uint64_t),
			crt_proc_uint64_t);

struct crt_msg_field DMF_CRT_SIZE =
	DEFINE_CRT_MSG("crt_crt_size", 0, sizeof(crt_size_t),
			crt_proc_crt_size_t);

struct crt_msg_field DMF_BULK =
	DEFINE_CRT_MSG("crt_bulk", 0, sizeof(crt_bulk_t),
		       crt_proc_crt_bulk_t);

struct crt_msg_field DMF_BOOL =
	DEFINE_CRT_MSG("crt_bool", 0, sizeof(bool),
		       crt_proc_bool);

struct crt_msg_field DMF_STRING =
	DEFINE_CRT_MSG("crt_string", 0,
		       sizeof(crt_string_t), crt_proc_crt_string_t);

struct crt_msg_field DMF_PHY_ADDR =
	DEFINE_CRT_MSG("crt_phy_addr", 0,
		       sizeof(crt_phy_addr_t), crt_proc_crt_phy_addr_t);

struct crt_msg_field DMF_RANK =
	DEFINE_CRT_MSG("crt_rank", 0, sizeof(crt_rank_t),
		       crt_proc_uint32_t);

struct crt_msg_field DMF_RANK_LIST =
	DEFINE_CRT_MSG("crt_rank_list", 0,
		       sizeof(crt_rank_list_t *), crt_proc_crt_rank_list_t);

struct crt_msg_field DMF_BULK_ARRAY =
	DEFINE_CRT_MSG("crt_bulks", DMF_ARRAY_FLAG,
			sizeof(crt_bulk_t),
			crt_proc_crt_bulk_t);

struct crt_msg_field DMF_IOVEC =
	DEFINE_CRT_MSG("crt_iov", 0, sizeof(crt_iov_t), crt_proc_crt_iov_t);

struct crt_msg_field *crt_single_out_fields[] = {
	&DMF_INT,	/* status */
};

int
crt_proc_common_hdr(crt_proc_t proc, struct crt_common_hdr *hdr)
{
	hg_proc_t     hg_proc;
	hg_return_t   hg_ret = HG_SUCCESS;
	int           rc = 0;

	/*
	 * C_DEBUG("in crt_proc_common_hdr, opc: 0x%x.\n", hdr->dch_opc);
	 */

	if (proc == CRT_PROC_NULL || hdr == NULL)
		C_GOTO(out, rc = -CER_INVAL);

	hg_proc = proc;
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_magic);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_version);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_opc);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_cksum);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_flags);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_rank);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_grp_id);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}

	/* proc the paddings */
	hg_ret = hg_proc_memcpy(hg_proc, hdr->dch_padding, sizeof(uint32_t));
	if (hg_ret != HG_SUCCESS)
		C_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);

out:
	return rc;
}

/* For unpacking only the common header to know about the CRT opc */
int
crt_hg_unpack_header(struct crt_rpc_priv *rpc_priv, crt_proc_t *proc)
{
	int	rc = 0;

#if CRT_HG_LOWLEVEL_UNPACK
	/*
	 * Use some low level HG APIs to unpack header first and then unpack the
	 * body, avoid unpacking two times (which needs to lookup, create the
	 * proc multiple times).
	 * The potential risk is mercury possibly will not export those APIs
	 * later, and the hard-coded method HG_CRC64 used below which maybe
	 * different with future's mercury code change.
	 */
	void			*in_buf;
	hg_size_t		in_buf_size;
	hg_return_t		hg_ret = HG_SUCCESS;
	hg_handle_t		handle;
	hg_class_t		*hg_class;
	struct crt_context	*ctx;
	struct crt_hg_context	*hg_ctx;
	hg_proc_t		hg_proc = HG_PROC_NULL;

	/* Get input buffer */
	handle = rpc_priv->drp_hg_hdl;
	hg_ret = HG_Core_get_input(handle, &in_buf, &in_buf_size);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("Could not get input buffer, hg_ret: %d.", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}

	/* Create a new decoding proc */
	ctx = (struct crt_context *)(rpc_priv->drp_pub.dr_ctx);
	hg_ctx = &ctx->dc_hg_ctx;
	hg_class = hg_ctx->dhc_hgcla;
	hg_ret = hg_proc_create(hg_class, in_buf, in_buf_size, HG_DECODE,
				HG_CRC64, &hg_proc);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("Could not create proc, hg_ret: %d.", hg_ret);
		C_GOTO(out, rc = -CER_HG);
	}

	/* Decode header */
	rc = crt_proc_common_hdr(hg_proc, &rpc_priv->drp_req_hdr);
	if (rc != 0)
		C_ERROR("crt_proc_common_hdr failed rc: %d.\n", rc);

	*proc = hg_proc;
out:
	return rc;
#else
	/*
	 * In the case that if mercury does not export the HG_Core_xxx APIs,
	 * we can only use the HG_Get_input to unpack the header which indeed
	 * will cause the unpacking twice as later we still need to unpack the
	 * body.
	 *
	 * Notes: as here we only unpack CRT common header and not finish
	 * the HG_Get_input() procedure, so for mercury need to turn off the
	 * checksum compiling option (-DMERCURY_USE_CHECKSUMS=OFF), or mercury
	 * will report checksum mismatch in the call of HG_Get_input.
	 */
	void		*hg_in_struct;
	hg_return_t	hg_ret = HG_SUCCESS;

	C_ASSERT(rpc_priv != NULL && proc != NULL);
	C_ASSERT(rpc_priv->drp_pub.dr_input == NULL);

	hg_in_struct = &rpc_priv->drp_pub.dr_input;
	hg_ret = HG_Get_input(rpc_priv->drp_hg_hdl, hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("HG_Get_input failed, hg_ret: %d.\n", hg_ret);
		rc = -CER_HG;
	}

	return rc;
#endif
}

void
crt_hg_unpack_cleanup(crt_proc_t proc)
{
#if CRT_HG_LOWLEVEL_UNPACK
	if (proc != HG_PROC_NULL)
		hg_proc_free(proc);
#endif
}

int
crt_proc_internal(struct drf_field *drf,
			 crt_proc_t proc, void *data)
{
	int rc = 0;
	void *ptr = data;
	int i;
	int j;

	for (i = 0; i < drf->drf_count; i++) {
		if (drf->drf_msg[i]->dmf_flags & DMF_ARRAY_FLAG) {
			struct crt_array *array = ptr;
			hg_proc_op_t	 proc_op;
			hg_return_t	 hg_ret;
			void		*array_ptr;

			/* retrieve the count of array first */
			hg_ret = hg_proc_hg_uint64_t(proc, &array->count);
			if (hg_ret != HG_SUCCESS) {
				rc = -CER_HG;
				break;
			}

			/* Let's assume array is not zero size now */
			if (array->count == 0)
				break;

			proc_op = hg_proc_get_op(proc);
			if (proc_op == HG_DECODE) {
				C_ALLOC(array->arrays,
				     array->count * drf->drf_msg[i]->dmf_size);
				if (array->arrays == NULL) {
					rc = -CER_NOMEM;
					break;
				}
			}
			array_ptr = array->arrays;
			for (j = 0; j < array->count; j++) {
				rc = drf->drf_msg[i]->dmf_proc(proc, array_ptr);
				if (rc != 0)
					break;

				array_ptr = (char *)array_ptr +
					    drf->drf_msg[i]->dmf_size;
			}

			if (proc_op == HG_FREE) {
				C_FREE(array->arrays,
				     array->count * drf->drf_msg[i]->dmf_size);
			}
			ptr = (char *)ptr + sizeof(struct crt_array);
		} else {
			rc = drf->drf_msg[i]->dmf_proc(proc, ptr);

			ptr = (char *)ptr + drf->drf_msg[i]->dmf_size;
		}

		if (rc < 0)
			break;
	}

	return rc;
}

int
crt_proc_input(struct crt_rpc_priv *rpc_priv, crt_proc_t proc)
{
	struct crt_req_format *drf = rpc_priv->drp_opc_info->doi_drf;

	C_ASSERT(drf != NULL);
	return crt_proc_internal(&drf->drf_fields[CRT_IN],
				 proc, rpc_priv->drp_pub.dr_input);
}

int
crt_proc_output(struct crt_rpc_priv *rpc_priv, crt_proc_t proc)
{
	struct crt_req_format *drf = rpc_priv->drp_opc_info->doi_drf;

	C_ASSERT(drf != NULL);
	return crt_proc_internal(&drf->drf_fields[CRT_OUT],
				 proc, rpc_priv->drp_pub.dr_output);
}

int
crt_hg_unpack_body(struct crt_rpc_priv *rpc_priv, crt_proc_t proc)
{
	int	rc = 0;

#if CRT_HG_LOWLEVEL_UNPACK
	hg_return_t	hg_ret;

	C_ASSERT(rpc_priv != NULL && proc != HG_PROC_NULL);

	/* Decode input parameters */
	rc = crt_proc_input(rpc_priv, proc);
	if (rc != 0) {
		C_ERROR("crt_hg_unpack_body failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
		C_GOTO(out, rc);
	}

	/* Flush proc */
	hg_ret = hg_proc_flush(proc);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("Error in proc flush, hg_ret: %d, opc: 0x%x.",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		C_GOTO(out, rc);
	}
out:
	crt_hg_unpack_cleanup(proc);

#else
	void		*hg_in_struct;
	hg_return_t	hg_ret = HG_SUCCESS;

	C_ASSERT(rpc_priv != NULL);
	C_ASSERT(rpc_priv->drp_pub.dr_input != NULL);

	hg_in_struct = &rpc_priv->drp_pub.dr_input;
	hg_ret = HG_Get_input(rpc_priv->drp_hg_hdl, hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		C_ERROR("HG_Get_input failed, hg_ret: %d.\n", hg_ret);
		rc = -CER_HG;
	}
#endif
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_input as the \param data */
int
crt_proc_in_common(crt_proc_t proc, crt_rpc_input_t *data)
{
	struct crt_rpc_priv	*rpc_priv;
	crt_proc_op_t		 proc_op;
	int			 rc = 0;

	if (proc == CRT_PROC_NULL)
		C_GOTO(out, rc = -CER_INVAL);

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -CER_HG;

	C_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, drp_pub.dr_input);
	C_ASSERT(rpc_priv != NULL);

	/* C_DEBUG("in crt_proc_in_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		rc = crt_proc_common_hdr(proc, &rpc_priv->drp_req_hdr);
		if (rc != 0) {
			C_ERROR("crt_proc_common_hdr failed rc: %d.\n", rc);
			C_GOTO(out, rc);
		}
	}

	if (*data == NULL) {
		/*
		C_DEBUG("crt_proc_in_common, opc: 0x%x, NULL input.\n",
			rpc_priv->drp_req_hdr.dch_opc);
		*/
		C_GOTO(out, rc);
	}

	rc = crt_proc_input(rpc_priv, proc);
	if (rc != 0) {
		C_ERROR("unpack input fails for opc: %s\n",
			rpc_priv->drp_opc_info->doi_drf->drf_name);
		C_GOTO(out, rc);
	}
out:
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_output as the \param data */
int
crt_proc_out_common(crt_proc_t proc, crt_rpc_output_t *data)
{
	struct crt_rpc_priv	*rpc_priv;
	crt_proc_op_t		 proc_op;
	int			 rc = 0;

	if (proc == CRT_PROC_NULL)
		C_GOTO(out, rc = -CER_INVAL);

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -CER_HG;

	C_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct crt_rpc_priv, drp_pub.dr_output);
	C_ASSERT(rpc_priv != NULL);

	/* C_DEBUG("in crt_proc_out_common, data: %p\n", *data); */

	if (proc_op != CRT_PROC_FREE) {
		rc = crt_proc_common_hdr(proc, &rpc_priv->drp_reply_hdr);
		if (rc != 0) {
			C_ERROR("crt_proc_common_hdr failed rc: %d.\n", rc);
			C_GOTO(out, rc);
		}
	}

	if (*data == NULL) {
		/*
		C_DEBUG("crt_proc_out_common, opc: 0x%x, NULL output.\n",
			rpc_priv->drp_req_hdr.dch_opc);
		*/
		C_GOTO(out, rc);
	}

	rc = crt_proc_output(rpc_priv, proc);
out:
	return rc;
}
