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
dtp_proc_memcpy(dtp_proc_t proc, void *data, daos_size_t data_size)
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
dtp_proc_raw(dtp_proc_t proc, void *buf, daos_size_t buf_size)
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
dtp_proc_daos_rank_list_t(dtp_proc_t proc, daos_rank_list_t **data)
{
	daos_rank_list_t	*rank_list;
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
			rc = dtp_proc_daos_rank_t(proc,
						  &rank_list->rl_ranks[i]);
			if (rc != 0) {
				D_ERROR("dtp_proc_daos_rank_t failed,rc: %d.\n",
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
		D_ALLOC(rank_list->rl_ranks, rank_num * sizeof(daos_rank_t));
		if (rank_list->rl_ranks == NULL) {
			D_ERROR("Cannot allocate memory for rl_ranks.\n");
			D_FREE_PTR(rank_list);
			D_GOTO(out, rc = -DER_NOMEM);
		}
		for (i = 0; i < rank_num; i++) {
			rc = dtp_proc_daos_rank_t(proc,
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
 *	uint64_t lo;
 *	uint64_t mid;
 *	uint64_t hi;
 * } daos_obj_id_t;
 **/
int
dtp_proc_daos_obj_id_t(dtp_proc_t proc, daos_obj_id_t *doi)
{
	hg_return_t hg_ret;

	hg_ret = hg_proc_uint64_t(proc, &doi->lo);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &doi->mid);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &doi->hi);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	return 0;
}

/**
 * typedef struct {
 *	daos_obj_id_t	id_pub;
 *	uint32_t id_shard;
 *	uint32_t id_pad_32;
 *} daos_unit_oid_t;
 **/
int
dtp_proc_daos_unit_oid_t(dtp_proc_t proc, daos_unit_oid_t *doi)
{
	hg_return_t hg_ret;
	int rc;

	rc = dtp_proc_daos_obj_id_t(proc, &doi->id_pub);
	if (rc != 0)
		return rc;

	hg_ret = hg_proc_uint32_t(proc, &doi->id_shard);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint32_t(proc, &doi->id_pad_32);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	return 0;
}

/**
 * typedef struct {
 *	daos_size_t	iov_len;
 *	daos_size_t	iov_buf_len;
 *	void	       *iov_buf;
 * } daos_iov_t;
 **/
int
dtp_proc_daos_iov(dtp_proc_t proc, daos_iov_t *div)
{
	hg_return_t hg_ret;
	hg_proc_op_t proc_op;

	proc_op = hg_proc_get_op(proc);
	hg_ret = hg_proc_uint64_t(proc, &div->iov_len);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &div->iov_buf_len);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	if (div->iov_buf_len < div->iov_len) {
		D_ERROR("invalid iov buf len "DF_U64" < iov len "DF_U64"\n",
			div->iov_buf_len, div->iov_len);
		return -DER_DTP_HG;
	}
	if (proc_op == HG_DECODE) {
		D_ALLOC(div->iov_buf, div->iov_buf_len);
		if (div->iov_buf == NULL)
			return -DER_NOMEM;
	} else if (proc_op == HG_FREE && div->iov_buf_len > 0) {
		D_FREE(div->iov_buf, div->iov_buf_len);
	}

	hg_ret = hg_proc_memcpy(proc, div->iov_buf, div->iov_len);
	if (hg_ret != HG_SUCCESS) {
		if (proc_op == HG_DECODE)
			D_FREE(div->iov_buf, div->iov_buf_len);
		return -DER_DTP_HG;
	}

	return 0;
}

/**
 * typedef struct {
 *	unsigned int	 cs_type;
 *	unsigned short	 cs_len;
 *	unsigned short	 cs_buf_len;
 *	void		*cs_csum;
 * } daos_csum_buf_t;
**/
int
dtp_proc_daos_csum_buf(dtp_proc_t proc, daos_csum_buf_t *csum)
{
	hg_return_t hg_ret;
	hg_proc_op_t	proc_op;

	proc_op = hg_proc_get_op(proc);
	hg_ret = hg_proc_uint32_t(proc, &csum->cs_type);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint16_t(proc, &csum->cs_len);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint16_t(proc, &csum->cs_buf_len);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	if (csum->cs_buf_len < csum->cs_len) {
		D_ERROR("invalid csum buf len %hu < csum len %hu\n",
			csum->cs_buf_len, csum->cs_len);
		return -DER_DTP_HG;
	}

	if (proc_op == HG_DECODE && csum->cs_buf_len > 0) {
		D_ALLOC(csum->cs_csum, csum->cs_buf_len);
		if (csum->cs_csum == NULL)
			return -DER_NOMEM;
	} else if (proc_op == HG_FREE && csum->cs_buf_len > 0) {
		D_FREE(csum->cs_csum, csum->cs_buf_len);
	}

	if (csum->cs_len > 0) {
		hg_ret = hg_proc_memcpy(proc, csum->cs_csum, csum->cs_len);
		if (hg_ret != HG_SUCCESS) {
			if (proc_op == HG_DECODE)
				D_FREE(csum->cs_csum, csum->cs_buf_len);
			return -DER_DTP_HG;
		}
	}

	return 0;
}

/**
 * daos_recx_t
 * typedef struct {
 *	uint64_t	rx_rsize;
 *	uint64_t	rx_idx;
 *	uint64_t	rx_nr;
 * } daos_recx_t;
 **/
int
dtp_proc_daos_recx_t(dtp_proc_t proc, daos_recx_t *recx)
{
	hg_return_t hg_ret;

	hg_ret = hg_proc_uint64_t(proc, &recx->rx_rsize);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &recx->rx_idx);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &recx->rx_nr);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	return 0;
}

/**
 * typedef struct {
 *	daos_epoch_t	epr_lo;
 *	daos_epoch_t	epr_hi;
 * } daos_epoch_range_t;
**/
int
dtp_proc_epoch_range_t(dtp_proc_t proc,
		       daos_epoch_range_t *erange)
{
	hg_return_t hg_ret;

	hg_ret = hg_proc_uint64_t(proc, &erange->epr_lo);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &erange->epr_hi);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	return 0;
}

/**
 * typedef struct {
 *	daos_akey_t		 vd_name;
 *	daos_csum_buf_t		 vd_kcsum;
 *	unsigned int		 vd_nr;
 *	daos_recx_t		*vd_recxs;
 *	daos_csum_buf_t		*vd_csums;
 *	daos_epoch_range_t	*vd_eprs;
 * } daos_vec_iod_t;
 **/
#define VD_REC_EXIST	(1 << 0)
#define VD_CSUM_EXIST	(1 << 1)
#define VD_EPRS_EXIST	(1 << 2)
int
dtp_proc_daos_vec_iod(dtp_proc_t proc, daos_vec_iod_t *dvi)
{
	hg_proc_op_t proc_op;
	hg_return_t hg_ret;
	int rc;
	int i;
	uint32_t existing_flags = 0;

	if (proc == NULL ||  dvi == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n",
			proc, dvi);
		return -DER_INVAL;
	}

	rc = dtp_proc_daos_iov(proc, &dvi->vd_name);
	if (rc != 0)
		return rc;

	rc = dtp_proc_daos_csum_buf(proc, &dvi->vd_kcsum);
	if (rc != 0)
		return rc;

	hg_ret = hg_proc_uint32_t(proc, &dvi->vd_nr);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	if (dvi->vd_nr == 0) {
		D_ERROR("invalid i/o vector, vd_nr = 0\n");
		return -DER_DTP_HG;
	}

	proc_op = hg_proc_get_op(proc);
	if (proc_op == HG_ENCODE) {
		if (dvi->vd_recxs != NULL)
			existing_flags |= VD_REC_EXIST;
		if (dvi->vd_csums != NULL)
			existing_flags |= VD_CSUM_EXIST;
		if (dvi->vd_eprs != NULL)
			existing_flags |= VD_EPRS_EXIST;
	}

	hg_ret = hg_proc_uint32_t(proc, &existing_flags);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	if (proc_op == HG_DECODE) {
		if (existing_flags & VD_REC_EXIST) {
			D_ALLOC(dvi->vd_recxs,
				dvi->vd_nr * sizeof(*dvi->vd_recxs));
			if (dvi->vd_recxs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}

		if (existing_flags & VD_CSUM_EXIST) {
			D_ALLOC(dvi->vd_csums,
				dvi->vd_nr * sizeof(*dvi->vd_csums));
			if (dvi->vd_csums == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}

		if (existing_flags & VD_EPRS_EXIST) {
			D_ALLOC(dvi->vd_eprs,
				dvi->vd_nr * sizeof(*dvi->vd_eprs));
			if (dvi->vd_eprs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}
	}

	if (existing_flags & VD_REC_EXIST) {
		for (i = 0; i < dvi->vd_nr; i++) {
			rc = dtp_proc_daos_recx_t(proc, &dvi->vd_recxs[i]);
			if (rc != 0) {
				if (proc_op == HG_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & VD_CSUM_EXIST) {
		for (i = 0; i < dvi->vd_nr; i++) {
			rc = dtp_proc_daos_csum_buf(proc, &dvi->vd_csums[i]);
			if (rc != 0) {
				if (proc_op == HG_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & VD_EPRS_EXIST) {
		for (i = 0; i < dvi->vd_nr; i++) {
			rc = dtp_proc_epoch_range_t(proc, &dvi->vd_eprs[i]);
			if (rc != 0) {
				if (proc_op == HG_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (proc_op == HG_FREE) {
free:
		if (dvi->vd_recxs != NULL)
			D_FREE(dvi->vd_recxs,
			       dvi->vd_nr * sizeof(*dvi->vd_recxs));
		if (dvi->vd_csums != NULL)
			D_FREE(dvi->vd_csums,
			       dvi->vd_nr * sizeof(*dvi->vd_csums));
		if (dvi->vd_eprs != NULL)
			D_FREE(dvi->vd_eprs,
			       dvi->vd_nr * sizeof(*dvi->vd_eprs));
	}

	return rc;
}

static int
dtp_proc_daos_epoch_state_t(dtp_proc_t proc, daos_epoch_state_t *es)
{
	hg_return_t hg_ret;

	hg_ret = hg_proc_uint64_t(proc, &es->es_hce);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &es->es_lre);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &es->es_lhe);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &es->es_glb_hce);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &es->es_glb_lre);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint64_t(proc, &es->es_glb_hpce);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	return 0;
}

int
dtp_proc_daos_hash_out_t(dtp_proc_t proc, daos_hash_out_t *hash)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_raw(proc, hash->body, sizeof(hash->body));

	return (hg_ret == HG_SUCCESS) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_daos_key_desc_t(dtp_proc_t proc, daos_key_desc_t *key)
{
	hg_return_t	hg_ret;

	hg_ret = hg_proc_uint64_t(proc, &key->kd_key_len);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = hg_proc_uint32_t(proc, &key->kd_csum_type);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

	hg_ret = dtp_proc_uint16_t(proc, &key->kd_csum_len);
	if (hg_ret != HG_SUCCESS)
		return -DER_DTP_HG;

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
	DEFINE_DTP_MSG("dtp_daos_size", 0, sizeof(daos_size_t),
			dtp_proc_daos_size_t);

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
	DEFINE_DTP_MSG("daos_rank", 0, sizeof(daos_rank_t),
		       dtp_proc_uint32_t);

struct dtp_msg_field DMF_RANK_LIST =
	DEFINE_DTP_MSG("daos_rank_list", 0,
		       sizeof(daos_rank_list_t *), dtp_proc_daos_rank_list_t);

struct dtp_msg_field DMF_OID =
	DEFINE_DTP_MSG("daos_unit_oid_t", 0,
			sizeof(daos_unit_oid_t), dtp_proc_daos_unit_oid_t);

struct dtp_msg_field DMF_IOVEC =
	DEFINE_DTP_MSG("daos_iov", 0, sizeof(daos_iov_t), dtp_proc_daos_iov);

struct dtp_msg_field DMF_VEC_IOD_ARRAY =
	DEFINE_DTP_MSG("daos_vec_iods", DMF_ARRAY_FLAG,
			sizeof(daos_vec_iod_t),
			dtp_proc_daos_vec_iod);

struct dtp_msg_field DMF_REC_SIZE_ARRAY =
	DEFINE_DTP_MSG("daos_rec_size", DMF_ARRAY_FLAG,
			sizeof(uint64_t),
			dtp_proc_uint64_t);

struct dtp_msg_field DMF_BULK_ARRAY =
	DEFINE_DTP_MSG("daos_bulks", DMF_ARRAY_FLAG,
			sizeof(dtp_bulk_t),
			dtp_proc_dtp_bulk_t);

struct dtp_msg_field DMF_KEY_DESC_ARRAY =
	DEFINE_DTP_MSG("dtp_key_desc", DMF_ARRAY_FLAG,
			sizeof(daos_key_desc_t),
			dtp_proc_daos_key_desc_t);

struct dtp_msg_field DMF_EPOCH_STATE =
	DEFINE_DTP_MSG("daos_epoch_state_t", 0, sizeof(daos_epoch_state_t),
		       dtp_proc_daos_epoch_state_t);

struct dtp_msg_field *dtp_single_out_fields[] = {
	&DMF_INT,	/* status */
};

struct dtp_msg_field DMF_DAOS_HASH_OUT =
	DEFINE_DTP_MSG("daos_hash_out_t", 0,
			sizeof(daos_hash_out_t),
			dtp_proc_daos_hash_out_t);

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
			hg_ret = hg_proc_hg_uint64_t(proc, &array->count);
			if (hg_ret != HG_SUCCESS) {
				rc = -DER_DTP_HG;
				break;
			}

			/* Let's assume array is not zero size now */
			if (array->count == 0)
				break;

			proc_op = hg_proc_get_op(proc);
			if (proc_op == HG_DECODE) {
				D_ALLOC(array->arrays,
				     array->count * drf->drf_msg[i]->dmf_size);
				if (array->arrays == NULL) {
					rc = -DER_NOMEM;
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
				D_FREE(array->arrays,
				     array->count * drf->drf_msg[i]->dmf_size);
			}
			ptr = (char *)ptr + sizeof(struct dtp_array);
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
