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
 * This file is part of daos_transport. It implements the main input/output
 * parameter serialization/de-serialization routines (proc functions).
 */

#include <dtp_internal.h>

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

	hg_ret = hg_proc_hg_bulk_t(proc, bulk_hdl);

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
int
dtp_proc_daos_vec_iod(dtp_proc_t proc, daos_vec_iod_t *dvi)
{
	hg_proc_op_t proc_op;
	hg_return_t hg_ret;
	int rc;
	int i;

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
	if (proc_op == HG_DECODE) {
		D_ALLOC(dvi->vd_recxs,
			dvi->vd_nr * sizeof(*dvi->vd_recxs));

		D_ALLOC(dvi->vd_csums,
			dvi->vd_nr * sizeof(*dvi->vd_csums));

		D_ALLOC(dvi->vd_eprs,
			dvi->vd_nr * sizeof(*dvi->vd_eprs));

		if (dvi->vd_recxs == NULL || dvi->vd_csums == NULL ||
		    dvi->vd_eprs == NULL)
			return -DER_NOMEM;
	}
	for (i = 0; i < dvi->vd_nr; i++) {
		rc = dtp_proc_daos_recx_t(proc, &dvi->vd_recxs[i]);
		if (rc != 0)
			return rc;
	}

	for (i = 0; i < dvi->vd_nr; i++) {
		rc = dtp_proc_daos_csum_buf(proc, &dvi->vd_csums[i]);
		if (rc != 0)
			return rc;
	}

	for (i = 0; i < dvi->vd_nr; i++) {
		rc = dtp_proc_epoch_range_t(proc, &dvi->vd_eprs[i]);
		if (rc != 0)
			return rc;
	}

	if (proc_op == HG_FREE) {
		D_FREE(dvi->vd_eprs,
		       dvi->vd_nr * sizeof(*dvi->vd_eprs));
		D_FREE(dvi->vd_recxs,
		       dvi->vd_nr * sizeof(*dvi->vd_recxs));

		D_FREE(dvi->vd_csums,
		       dvi->vd_nr * sizeof(*dvi->vd_csums));
	}

	return rc;
}

struct dtp_msg_field DMF_UUID =
	DEFINE_DTP_MSG("dtp_uuid", 0, sizeof(uuid_t),
		       dtp_proc_uuid_t);

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

struct dtp_msg_field DMF_BULK_ARRAY =
	DEFINE_DTP_MSG("daos_bulks", DMF_ARRAY_FLAG,
			sizeof(dtp_bulk_t),
			dtp_proc_dtp_bulk_t);

struct dtp_msg_field *dtp_single_out_fields[] = {
	&DMF_INT,	/* status */
};

