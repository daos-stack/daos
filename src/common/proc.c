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

#include <daos_types.h>
#include <daos/common.h>
#include <daos/transport.h>

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
	int rc;

	rc = dtp_proc_uint64_t(proc, &doi->lo);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &doi->mid);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &doi->hi);
	if (rc != 0)
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
	int rc;

	rc = dtp_proc_daos_obj_id_t(proc, &doi->id_pub);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &doi->id_shard);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint32_t(proc, &doi->id_pad_32);
	if (rc != 0)
		return -DER_DTP_HG;

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
	dtp_proc_op_t	proc_op;
	int		rc;

	rc = dtp_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint32_t(proc, &csum->cs_type);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint16_t(proc, &csum->cs_len);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint16_t(proc, &csum->cs_buf_len);
	if (rc != 0)
		return -DER_DTP_HG;

	if (csum->cs_buf_len < csum->cs_len) {
		D_ERROR("invalid csum buf len %hu < csum len %hu\n",
			csum->cs_buf_len, csum->cs_len);
		return -DER_DTP_HG;
	}

	if (proc_op == DTP_DECODE && csum->cs_buf_len > 0) {
		D_ALLOC(csum->cs_csum, csum->cs_buf_len);
		if (csum->cs_csum == NULL)
			return -DER_NOMEM;
	} else if (proc_op == DTP_FREE && csum->cs_buf_len > 0) {
		D_FREE(csum->cs_csum, csum->cs_buf_len);
	}

	if (csum->cs_len > 0) {
		rc = dtp_proc_memcpy(proc, csum->cs_csum, csum->cs_len);
		if (rc != 0) {
			if (proc_op == DTP_DECODE)
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
	int rc;

	rc = dtp_proc_uint64_t(proc, &recx->rx_rsize);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &recx->rx_idx);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &recx->rx_nr);
	if (rc != 0)
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
	int rc;

	rc = dtp_proc_uint64_t(proc, &erange->epr_lo);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &erange->epr_hi);
	if (rc != 0)
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
	dtp_proc_op_t	proc_op;
	int		rc;
	int		i;
	uint32_t	existing_flags = 0;

	if (proc == NULL ||  dvi == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n",
			proc, dvi);
		return -DER_INVAL;
	}

	rc = dtp_proc_dtp_iov_t(proc, &dvi->vd_name);
	if (rc != 0)
		return rc;

	rc = dtp_proc_daos_csum_buf(proc, &dvi->vd_kcsum);
	if (rc != 0)
		return rc;

	rc = dtp_proc_uint32_t(proc, &dvi->vd_nr);
	if (rc != 0)
		return -DER_DTP_HG;

	if (dvi->vd_nr == 0) {
		D_ERROR("invalid i/o vector, vd_nr = 0\n");
		return -DER_DTP_HG;
	}

	rc = dtp_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_DTP_HG;

	if (proc_op == DTP_ENCODE) {
		if (dvi->vd_recxs != NULL)
			existing_flags |= VD_REC_EXIST;
		if (dvi->vd_csums != NULL)
			existing_flags |= VD_CSUM_EXIST;
		if (dvi->vd_eprs != NULL)
			existing_flags |= VD_EPRS_EXIST;
	}

	rc = dtp_proc_uint32_t(proc, &existing_flags);
	if (rc != 0)
		return -DER_DTP_HG;

	if (proc_op == DTP_DECODE) {
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
				if (proc_op == DTP_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & VD_CSUM_EXIST) {
		for (i = 0; i < dvi->vd_nr; i++) {
			rc = dtp_proc_daos_csum_buf(proc, &dvi->vd_csums[i]);
			if (rc != 0) {
				if (proc_op == DTP_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & VD_EPRS_EXIST) {
		for (i = 0; i < dvi->vd_nr; i++) {
			rc = dtp_proc_epoch_range_t(proc, &dvi->vd_eprs[i]);
			if (rc != 0) {
				if (proc_op == DTP_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (proc_op == DTP_FREE) {
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
	int rc;

	rc = dtp_proc_uint64_t(proc, &es->es_hce);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &es->es_lre);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &es->es_lhe);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &es->es_ghce);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &es->es_glre);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint64_t(proc, &es->es_ghpce);
	if (rc != 0)
		return -DER_DTP_HG;

	return 0;
}

int
dtp_proc_daos_hash_out_t(dtp_proc_t proc, daos_hash_out_t *hash)
{
	int rc;

	rc = dtp_proc_raw(proc, hash->body, sizeof(hash->body));

	return (rc == 0) ? 0 : -DER_DTP_HG;
}

int
dtp_proc_daos_key_desc_t(dtp_proc_t proc, daos_key_desc_t *key)
{
	int rc;

	rc = dtp_proc_uint64_t(proc, &key->kd_key_len);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint32_t(proc, &key->kd_csum_type);
	if (rc != 0)
		return -DER_DTP_HG;

	rc = dtp_proc_uint16_t(proc, &key->kd_csum_len);
	if (rc != 0)
		return -DER_DTP_HG;

	return 0;
}

struct dtp_msg_field DMF_OID =
	DEFINE_DTP_MSG("daos_unit_oid_t", 0,
			sizeof(daos_unit_oid_t), dtp_proc_daos_unit_oid_t);

struct dtp_msg_field DMF_VEC_IOD_ARRAY =
	DEFINE_DTP_MSG("daos_vec_iods", DMF_ARRAY_FLAG,
			sizeof(daos_vec_iod_t),
			dtp_proc_daos_vec_iod);

struct dtp_msg_field DMF_REC_SIZE_ARRAY =
	DEFINE_DTP_MSG("daos_rec_size", DMF_ARRAY_FLAG,
			sizeof(uint64_t),
			dtp_proc_uint64_t);

struct dtp_msg_field DMF_KEY_DESC_ARRAY =
	DEFINE_DTP_MSG("dtp_key_desc", DMF_ARRAY_FLAG,
			sizeof(daos_key_desc_t),
			dtp_proc_daos_key_desc_t);

struct dtp_msg_field DMF_EPOCH_STATE =
	DEFINE_DTP_MSG("daos_epoch_state_t", 0, sizeof(daos_epoch_state_t),
		       dtp_proc_daos_epoch_state_t);

struct dtp_msg_field DMF_DAOS_HASH_OUT =
	DEFINE_DTP_MSG("daos_hash_out_t", 0,
			sizeof(daos_hash_out_t),
			dtp_proc_daos_hash_out_t);

