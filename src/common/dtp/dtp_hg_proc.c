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
 * This file is part of daos_transport. It implements the main input/output
 * parameter serialization/de-serialization routines (proc functions).
 */

#include <dtp_internal.h>

#define DTP_PROC_NULL (NULL)

static inline hg_proc_op_t
dtp_proc_op2hg(dtp_proc_op_t proc_op)
{
	if (proc_op == DTP_ENCODE)
		return HG_ENCODE;
	else if (proc_op == DTP_DECODE)
		return HG_DECODE;
	else if (proc_op == DTP_FREE)
		return HG_FREE;
	else
		return -DER_INVAL;
}

int
dtp_proc_get_op(dtp_proc_t proc, dtp_proc_op_t *proc_op)
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
		*proc_op = DTP_ENCODE;
		break;
	case HG_DECODE:
		*proc_op = DTP_DECODE;
		break;
	case HG_FREE:
		*proc_op = DTP_FREE;
		break;
	default:
		D_ERROR("bad hg_proc_op: %d.\n", hg_proc_op);
		rc = -DER_INVAL;
	}

out:
	return rc;
}

int
dtp_proc_memcpy(dtp_proc_t proc, void *data, dtp_size_t data_size)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_memcpy(proc, data, data_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int8_t(dtp_proc_t proc, int8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint8_t(dtp_proc_t proc, uint8_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint8_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int16_t(dtp_proc_t proc, int16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint16_t(dtp_proc_t proc, uint16_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint16_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int32_t(dtp_proc_t proc, int32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint32_t(dtp_proc_t proc, uint32_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint32_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_int64_t(dtp_proc_t proc, int64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_int64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uint64_t(dtp_proc_t proc, uint64_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint64_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_bool(dtp_proc_t proc, bool *data)
{
	hg_bool_t	hg_bool;
	hg_return_t	hg_ret;

	hg_bool = (*data == false) ? 0 : 1;
	hg_ret = hg_proc_hg_bool_t(proc, &hg_bool);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_raw(dtp_proc_t proc, void *buf, dtp_size_t buf_size)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_raw(proc, buf, buf_size);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_dtp_bulk_t(dtp_proc_t proc, dtp_bulk_t *bulk_hdl)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_bulk_t(proc, (hg_bulk_t *)bulk_hdl);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_dtp_string_t(dtp_proc_t proc, dtp_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_dtp_const_string_t(dtp_proc_t proc, dtp_const_string_t *data)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_hg_const_string_t(proc, data);

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_uuid_t(dtp_proc_t proc, uuid_t *data)
{
	return dtp_proc_memcpy(proc, data, sizeof(uuid_t));
}

int
dtp_proc_dtp_rank_list_t(dtp_proc_t proc, dtp_rank_list_t **data)
{
	dtp_rank_list_t	*rank_list;
	hg_proc_op_t		proc_op;
	uint32_t		rank_num;
	int			i, rc = 0;

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
			rc = dtp_proc_uint32_t(proc, &rank_num);
			if (rc != 0)
				D_ERROR("dtp_proc_uint32_t failed, rc: %d.\n",
					rc);
			D_GOTO(out, rc);
		}

		rank_num = rank_list->rl_nr.num;
		rc = dtp_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			D_ERROR("dtp_proc_uint32_t failed, rc: %d.\n",
				rc);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
		for (i = 0; i < rank_num; i++) {
			rc = dtp_proc_dtp_rank_t(proc,
						  &rank_list->rl_ranks[i]);
			if (rc != 0) {
				D_ERROR("dtp_proc_dtp_rank_t failed,rc: %d.\n",
					rc);
				D_GOTO(out, rc = -DER_DTP_HG);
			}
		}
		break;
	case HG_DECODE:
		rc = dtp_proc_uint32_t(proc, &rank_num);
		if (rc != 0) {
			D_ERROR("dtp_proc_uint32_t failed, rc: %d.\n",
				rc);
			D_GOTO(out, rc = -DER_DTP_HG);
		}
		if (rank_num == 0) {
			*data = NULL;
			D_GOTO(out, rc);
		}
		D_ALLOC_PTR(rank_list);
		if (rank_list == NULL) {
			D_ERROR("Cannot allocate memory for rank list.\n");
			D_GOTO(out, rc = -DER_NOMEM);
		}
		rank_list->rl_nr.num = rank_num;
		D_ALLOC(rank_list->rl_ranks, rank_num * sizeof(dtp_rank_t));
		if (rank_list->rl_ranks == NULL) {
			D_ERROR("Cannot allocate memory for rl_ranks.\n");
			D_FREE_PTR(rank_list);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		for (i = 0; i < rank_num; i++) {
			rc = dtp_proc_dtp_rank_t(proc,
						  &rank_list->rl_ranks[i]);
			if (rc != 0) {
				D_ERROR("dtp_proc_daso_rank_t failed,rc: %d.\n",
					rc);
				D_GOTO(out, rc = -DER_DTP_HG);
			}
		}
		*data = rank_list;
		break;
	case HG_FREE:
		rank_list = *data;
		daos_rank_list_free(rank_list);
		*data = NULL;
		break;
	default:
		D_ERROR("Bad proc op: %d.\n", proc_op);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

out:
	return rc;
}

/**
 * typedef struct {
 *	dtp_size_t	iov_len;
 *	dtp_size_t	iov_buf_len;
 *	void	       *iov_buf;
 * } dtp_iov_t;
 **/
int
dtp_proc_dtp_iov_t(dtp_proc_t proc, dtp_iov_t *div)
{
	dtp_proc_op_t	proc_op;
	int		rc;

	rc = dtp_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &div->iov_len);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &div->iov_buf_len);
	if (rc != 0)
		return -DER_DTP_HG;

	if (div->iov_buf_len < div->iov_len) {
		D_ERROR("invalid iov buf len "DF_U64" < iov len "DF_U64"\n",
			div->iov_buf_len, div->iov_len);
		return -DER_DTP_HG;
	}
	if (proc_op == DTP_DECODE && div->iov_buf_len > 0) {
		D_ALLOC(div->iov_buf, div->iov_buf_len);
		if (div->iov_buf == NULL)
			return -DER_NOMEM;
	} else if (proc_op == DTP_FREE && div->iov_buf_len > 0) {
		D_FREE(div->iov_buf, div->iov_buf_len);
	}

	if (div->iov_len > 0) {
		rc = dtp_proc_memcpy(proc, div->iov_buf, div->iov_len);
		if (rc != 0) {
			if (proc_op == DTP_DECODE)
				D_FREE(div->iov_buf, div->iov_buf_len);
			return -DER_DTP_HG;
		}
	}

	return 0;
}

struct dtp_msg_field DMF_UUID =
	DEFINE_DTP_MSG("dtp_uuid", 0, sizeof(uuid_t),
		       dtp_proc_uuid_t);

struct dtp_msg_field DMF_GRP_ID =
	DEFINE_DTP_MSG("dtp_group_id", 0, sizeof(dtp_group_id_t),
		       dtp_proc_dtp_group_id_t);

struct dtp_msg_field DMF_INT =
	DEFINE_DTP_MSG("dtp_int", 0, sizeof(int32_t),
		       dtp_proc_int);

struct dtp_msg_field DMF_UINT32 =
	DEFINE_DTP_MSG("dtp_uint32", 0, sizeof(uint32_t),
		       dtp_proc_uint32_t);

struct dtp_msg_field DMF_UINT64 =
	DEFINE_DTP_MSG("dtp_uint64", 0, sizeof(uint64_t),
			dtp_proc_uint64_t);

struct dtp_msg_field DMF_DAOS_SIZE =
	DEFINE_DTP_MSG("dtp_daos_size", 0, sizeof(dtp_size_t),
			dtp_proc_dtp_size_t);

struct dtp_msg_field DMF_BULK =
	DEFINE_DTP_MSG("dtp_bulk", 0, sizeof(dtp_bulk_t),
		       dtp_proc_dtp_bulk_t);

struct dtp_msg_field DMF_BOOL =
	DEFINE_DTP_MSG("dtp_bool", 0, sizeof(bool),
		       dtp_proc_bool);

struct dtp_msg_field DMF_STRING =
	DEFINE_DTP_MSG("dtp_string", 0,
		       sizeof(dtp_string_t), dtp_proc_dtp_string_t);

struct dtp_msg_field DMF_RANK =
	DEFINE_DTP_MSG("daos_rank", 0, sizeof(dtp_rank_t),
		       dtp_proc_uint32_t);

struct dtp_msg_field DMF_RANK_LIST =
	DEFINE_DTP_MSG("daos_rank_list", 0,
		       sizeof(dtp_rank_list_t *), dtp_proc_dtp_rank_list_t);

struct dtp_msg_field DMF_BULK_ARRAY =
	DEFINE_DTP_MSG("daos_bulks", DMF_ARRAY_FLAG,
			sizeof(dtp_bulk_t),
			dtp_proc_dtp_bulk_t);

struct dtp_msg_field DMF_IOVEC =
	DEFINE_DTP_MSG("daos_iov", 0, sizeof(daos_iov_t), dtp_proc_dtp_iov_t);

struct dtp_msg_field *dtp_single_out_fields[] = {
	&DMF_INT,	/* status */
};

int
dtp_proc_common_hdr(dtp_proc_t proc, struct dtp_common_hdr *hdr)
{
	hg_proc_t     hg_proc;
	hg_return_t   hg_ret = HG_SUCCESS;
	int           rc = 0;

	/*
	 * D_DEBUG(DF_TP,"in dtp_proc_common_hdr, opc: 0x%x.\n", hdr->dch_opc);
	 */

	if (proc == DTP_PROC_NULL || hdr == NULL)
		D_GOTO(out, rc = -DER_INVAL);

	hg_proc = proc;
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_magic);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_version);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_opc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_cksum);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_flags);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_rank);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}
	hg_ret = hg_proc_hg_uint32_t(hg_proc, &hdr->dch_grp_id);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* proc the paddings */
	hg_ret = hg_proc_memcpy(hg_proc, hdr->dch_padding, sizeof(uint32_t));
	if (hg_ret != HG_SUCCESS)
		D_ERROR("hg proc error, hg_ret: %d.\n", hg_ret);

out:
	return rc;
}

/* For unpacking only the common header to know about the DAOS opc */
int
dtp_hg_unpack_header(struct dtp_rpc_priv *rpc_priv, dtp_proc_t *proc)
{
	int	rc = 0;

#if DTP_HG_LOWLEVEL_UNPACK
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
	struct dtp_context	*ctx;
	struct dtp_hg_context	*hg_ctx;
	hg_proc_t		hg_proc = HG_PROC_NULL;

	/* Get input buffer */
	handle = rpc_priv->drp_hg_hdl;
	hg_ret = HG_Core_get_input(handle, &in_buf, &in_buf_size);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not get input buffer, hg_ret: %d.", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* Create a new decoding proc */
	ctx = (struct dtp_context *)(rpc_priv->drp_pub.dr_ctx);
	hg_ctx = &ctx->dc_hg_ctx;
	hg_class = hg_ctx->dhc_hgcla;
	hg_ret = hg_proc_create(hg_class, in_buf, in_buf_size, HG_DECODE,
				HG_CRC64, &hg_proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Could not create proc, hg_ret: %d.", hg_ret);
		D_GOTO(out, rc = -DER_DTP_HG);
	}

	/* Decode header */
	rc = dtp_proc_common_hdr(hg_proc, &rpc_priv->drp_req_hdr);
	if (rc != 0)
		D_ERROR("dtp_proc_common_hdr failed rc: %d.\n", rc);

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
	 * Notes: as here we only unpack DAOS common header and not finish
	 * the HG_Get_input() procedure, so for mercury need to turn off the
	 * checksum compiling option (-DMERCURY_USE_CHECKSUMS=OFF), or mercury
	 * will report checksum mismatch in the call of HG_Get_input.
	 */
	void		*hg_in_struct;
	hg_return_t	hg_ret = HG_SUCCESS;

	D_ASSERT(rpc_priv != NULL && proc != NULL);
	D_ASSERT(rpc_priv->drp_pub.dr_input == NULL);

	hg_in_struct = &rpc_priv->drp_pub.dr_input;
	hg_ret = HG_Get_input(rpc_priv->drp_hg_hdl, hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Get_input failed, hg_ret: %d.\n", hg_ret);
		rc = -DER_DTP_HG;
	}

	return rc;
#endif
}

void
dtp_hg_unpack_cleanup(dtp_proc_t proc)
{
#if DTP_HG_LOWLEVEL_UNPACK
	if (proc != HG_PROC_NULL)
		hg_proc_free(proc);
#endif
}

int
dtp_proc_internal(struct drf_field *drf,
			 dtp_proc_t proc, void *data)
{
	int rc = 0;
	void *ptr = data;
	int i;
	int j;

	for (i = 0; i < drf->drf_count; i++) {
		if (drf->drf_msg[i]->dmf_flags & DMF_ARRAY_FLAG) {
			struct dtp_array *array = ptr;
			hg_proc_op_t	 proc_op;
			hg_return_t	 hg_ret;
			void		*array_ptr;

			/* retrieve the count of array first */
			hg_ret = hg_proc_hg_uint64_t(proc, &array->da_count);
			if (hg_ret != HG_SUCCESS) {
				rc = -DER_DTP_HG;
				break;
			}

			/* Let's assume array is not zero size now */
			if (array->da_count == 0)
				break;

			proc_op = hg_proc_get_op(proc);
			if (proc_op == HG_DECODE) {
				D_ALLOC(array->da_arrays, array->da_count *
					drf->drf_msg[i]->dmf_size);
				if (array->da_arrays == NULL) {
					rc = -DER_NOMEM;
					break;
				}
			}
			array_ptr = array->da_arrays;
			for (j = 0; j < array->da_count; j++) {
				rc = drf->drf_msg[i]->dmf_proc(proc, array_ptr);
				if (rc != 0) {
					D_ERROR("dmf_proc failed, i %d, "
						"rc %d.\n", i, rc);
					D_GOTO(out, rc);
				}

				array_ptr = (char *)array_ptr +
					    drf->drf_msg[i]->dmf_size;
			}

			if (proc_op == HG_FREE)
				D_FREE(array->da_arrays, array->da_count *
						drf->drf_msg[i]->dmf_size);

			ptr = (char *)ptr + sizeof(struct dtp_array);
		} else {
			rc = drf->drf_msg[i]->dmf_proc(proc, ptr);

			ptr = (char *)ptr + drf->drf_msg[i]->dmf_size;
		}

		if (rc < 0)
			break;
	}

out:
	return rc;
}

int
dtp_proc_input(struct dtp_rpc_priv *rpc_priv, dtp_proc_t proc)
{
	struct dtp_req_format *drf = rpc_priv->drp_opc_info->doi_drf;

	D_ASSERT(drf != NULL);
	return dtp_proc_internal(&drf->drf_fields[DTP_IN],
				 proc, rpc_priv->drp_pub.dr_input);
}

int
dtp_proc_output(struct dtp_rpc_priv *rpc_priv, dtp_proc_t proc)
{
	struct dtp_req_format *drf = rpc_priv->drp_opc_info->doi_drf;

	D_ASSERT(drf != NULL);
	return dtp_proc_internal(&drf->drf_fields[DTP_OUT],
				 proc, rpc_priv->drp_pub.dr_output);
}

int
dtp_hg_unpack_body(struct dtp_rpc_priv *rpc_priv, dtp_proc_t proc)
{
	int	rc = 0;

#if DTP_HG_LOWLEVEL_UNPACK
	hg_return_t	hg_ret;

	D_ASSERT(rpc_priv != NULL && proc != HG_PROC_NULL);

	/* Decode input parameters */
	rc = dtp_proc_input(rpc_priv, proc);
	if (rc != 0) {
		D_ERROR("dtp_hg_unpack_body failed, rc: %d, opc: 0x%x.\n",
			rc, rpc_priv->drp_pub.dr_opc);
		D_GOTO(out, rc);
	}

	/* Flush proc */
	hg_ret = hg_proc_flush(proc);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("Error in proc flush, hg_ret: %d, opc: 0x%x.",
			hg_ret, rpc_priv->drp_pub.dr_opc);
		D_GOTO(out, rc);
	}
out:
	dtp_hg_unpack_cleanup(proc);

#else
	void		*hg_in_struct;
	hg_return_t	hg_ret = HG_SUCCESS;

	D_ASSERT(rpc_priv != NULL);
	D_ASSERT(rpc_priv->drp_pub.dr_input != NULL);

	hg_in_struct = &rpc_priv->drp_pub.dr_input;
	hg_ret = HG_Get_input(rpc_priv->drp_hg_hdl, hg_in_struct);
	if (hg_ret != HG_SUCCESS) {
		D_ERROR("HG_Get_input failed, hg_ret: %d.\n", hg_ret);
		rc = -DER_DTP_HG;
	}
#endif
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_input as the \param data */
int
dtp_proc_in_common(dtp_proc_t proc, dtp_rpc_input_t *data)
{
	struct dtp_rpc_priv	*rpc_priv;
	int			rc = 0;

	if (proc == DTP_PROC_NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct dtp_rpc_priv, drp_pub.dr_input);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG(DF_TP,"in dtp_proc_in_common, data: %p\n", *data); */

	rc = dtp_proc_common_hdr(proc, &rpc_priv->drp_req_hdr);
	if (rc != 0) {
		D_ERROR("dtp_proc_common_hdr failed rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	if (*data == NULL) {
		/*
		D_DEBUG(DF_TP,"dtp_proc_in_common, opc: 0x%x, NULL input.\n",
			rpc_priv->drp_req_hdr.dch_opc);
		*/
		D_GOTO(out, rc);
	}

	rc = dtp_proc_input(rpc_priv, proc);
	if (rc != 0) {
		D_ERROR("unpack input fails for opc: %s\n",
			rpc_priv->drp_opc_info->doi_drf->drf_name);
		D_GOTO(out, rc);
	}
out:
	return rc;
}

/* NB: caller should pass in &rpc_pub->dr_output as the \param data */
int
dtp_proc_out_common(dtp_proc_t proc, dtp_rpc_output_t *data)
{
	struct dtp_rpc_priv	*rpc_priv;
	int			rc = 0;

	if (proc == DTP_PROC_NULL)
		D_GOTO(out, rc = -DER_INVAL);

	D_ASSERT(data != NULL);
	rpc_priv = container_of(data, struct dtp_rpc_priv, drp_pub.dr_output);
	D_ASSERT(rpc_priv != NULL);

	/* D_DEBUG(DF_TP,"in dtp_proc_out_common, data: %p\n", *data); */

	rc = dtp_proc_common_hdr(proc, &rpc_priv->drp_reply_hdr);
	if (rc != 0) {
		D_ERROR("dtp_proc_common_hdr failed rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	if (*data == NULL) {
		/*
		D_DEBUG(DF_TP,"dtp_proc_out_common, opc: 0x%x, NULL output.\n",
			rpc_priv->drp_req_hdr.dch_opc);
		*/
		D_GOTO(out, rc);
	}

	rc = dtp_proc_output(rpc_priv, proc);
out:
	return rc;
}
