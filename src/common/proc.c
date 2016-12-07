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
#define DD_SUBSYS	DD_FAC(common)

#include <daos_types.h>
#include <daos/common.h>
#include <daos/rpc.h>

/**
 * typedef struct {
 *	uint64_t lo;
 *	uint64_t mid;
 *	uint64_t hi;
 * } daos_obj_id_t;
 **/
int
daos_proc_objid(crt_proc_t proc, daos_obj_id_t *doi)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &doi->lo);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &doi->mid);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &doi->hi);
	if (rc != 0)
		return -DER_CRT_HG;

	return 0;
}

/**
 * typedef struct {
 *      daos_size_t	 iov_len;
 *      daos_size_t	 iov_buf_len;
 *      void		*iov_buf;
 * } daos_iov_t;
 **/
int
daos_proc_iovec(crt_proc_t proc, daos_iov_t *div)
{
	crt_proc_op_t   proc_op;
	int             rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &div->iov_len);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &div->iov_buf_len);
	if (rc != 0)
		return -DER_CRT_HG;

	if (div->iov_buf_len < div->iov_len) {
		D_ERROR("invalid iov buf len "DF_U64" < iov len "DF_U64"\n",
				div->iov_buf_len, div->iov_len);
		return -DER_CRT_HG;
	}
	if (proc_op == CRT_PROC_DECODE && div->iov_buf_len > 0) {
		D_ALLOC(div->iov_buf, div->iov_buf_len);
		if (div->iov_buf == NULL)
			return -DER_NOMEM;
	} else if (proc_op == CRT_PROC_FREE && div->iov_buf_len > 0) {
		D_FREE(div->iov_buf, div->iov_buf_len);
	}

	if (div->iov_len > 0) {
		rc = crt_proc_memcpy(proc, div->iov_buf, div->iov_len);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(div->iov_buf, div->iov_buf_len);
			return -DER_CRT_HG;
		}
	}

	return 0;
}

/**
 * typedef struct {
 *	daos_obj_id_t	id_pub;
 *	uint32_t	id_shard;
 *	uint32_t	id_pad_32;
 *} daos_unit_oid_t;
 **/
int
daos_proc_unit_oid(crt_proc_t proc, daos_unit_oid_t *doi)
{
	int rc;

	rc = daos_proc_objid(proc, &doi->id_pub);
	if (rc != 0)
		return rc;

	rc = crt_proc_uint32_t(proc, &doi->id_shard);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint32_t(proc, &doi->id_pad_32);
	if (rc != 0)
		return -DER_CRT_HG;

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
daos_proc_csum_buf(crt_proc_t proc, daos_csum_buf_t *csum)
{
	crt_proc_op_t	proc_op;
	int		rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint32_t(proc, &csum->cs_type);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint16_t(proc, &csum->cs_len);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint16_t(proc, &csum->cs_buf_len);
	if (rc != 0)
		return -DER_CRT_HG;

	if (csum->cs_buf_len < csum->cs_len) {
		D_ERROR("invalid csum buf len %hu < csum len %hu\n",
			csum->cs_buf_len, csum->cs_len);
		return -DER_CRT_HG;
	}

	if (proc_op == CRT_PROC_DECODE && csum->cs_buf_len > 0) {
		D_ALLOC(csum->cs_csum, csum->cs_buf_len);
		if (csum->cs_csum == NULL)
			return -DER_NOMEM;
	} else if (proc_op == CRT_PROC_FREE && csum->cs_buf_len > 0) {
		D_FREE(csum->cs_csum, csum->cs_buf_len);
	}

	if (csum->cs_len > 0) {
		rc = crt_proc_memcpy(proc, csum->cs_csum, csum->cs_len);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(csum->cs_csum, csum->cs_buf_len);
			return -DER_CRT_HG;
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
daos_proc_recx(crt_proc_t proc, daos_recx_t *recx)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &recx->rx_rsize);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &recx->rx_idx);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &recx->rx_nr);
	if (rc != 0)
		return -DER_CRT_HG;

	return 0;
}

/**
 * typedef struct {
 *	daos_epoch_t	epr_lo;
 *	daos_epoch_t	epr_hi;
 * } daos_epoch_range_t;
**/
int
daos_proc_epoch_range(crt_proc_t proc, daos_epoch_range_t *erange)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &erange->epr_lo);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &erange->epr_hi);
	if (rc != 0)
		return -DER_CRT_HG;

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
daos_proc_vec_iod(crt_proc_t proc, daos_vec_iod_t *dvi)
{
	crt_proc_op_t	proc_op;
	int		rc;
	int		i;
	uint32_t	existing_flags = 0;

	if (proc == NULL ||  dvi == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n",
			proc, dvi);
		return -DER_INVAL;
	}

	rc = daos_proc_iovec(proc, &dvi->vd_name);
	if (rc != 0)
		return rc;

	rc = daos_proc_csum_buf(proc, &dvi->vd_kcsum);
	if (rc != 0)
		return rc;

	rc = crt_proc_uint32_t(proc, &dvi->vd_nr);
	if (rc != 0)
		return -DER_CRT_HG;

	if (dvi->vd_nr == 0) {
		D_ERROR("invalid i/o vector, vd_nr = 0\n");
		return -DER_CRT_HG;
	}

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_CRT_HG;

	if (proc_op == CRT_PROC_ENCODE) {
		if (dvi->vd_recxs != NULL)
			existing_flags |= VD_REC_EXIST;
		if (dvi->vd_csums != NULL)
			existing_flags |= VD_CSUM_EXIST;
		if (dvi->vd_eprs != NULL)
			existing_flags |= VD_EPRS_EXIST;
	}

	rc = crt_proc_uint32_t(proc, &existing_flags);
	if (rc != 0)
		return -DER_CRT_HG;

	if (proc_op == CRT_PROC_DECODE) {
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
			rc = daos_proc_recx(proc, &dvi->vd_recxs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & VD_CSUM_EXIST) {
		for (i = 0; i < dvi->vd_nr; i++) {
			rc = daos_proc_csum_buf(proc, &dvi->vd_csums[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & VD_EPRS_EXIST) {
		for (i = 0; i < dvi->vd_nr; i++) {
			rc = daos_proc_epoch_range(proc, &dvi->vd_eprs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (proc_op == CRT_PROC_FREE) {
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
daos_proc_epoch_state(crt_proc_t proc, daos_epoch_state_t *es)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &es->es_hce);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &es->es_lre);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &es->es_lhe);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &es->es_ghce);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &es->es_glre);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint64_t(proc, &es->es_ghpce);
	if (rc != 0)
		return -DER_CRT_HG;

	return 0;
}

int
daos_proc_hash_out(crt_proc_t proc, daos_hash_out_t *hash)
{
	int rc;

	rc = crt_proc_raw(proc, hash->body, sizeof(hash->body));

	return (rc == 0) ? 0 : -DER_CRT_HG;
}

int
daos_proc_key_desc(crt_proc_t proc, daos_key_desc_t *key)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &key->kd_key_len);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint32_t(proc, &key->kd_csum_type);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_uint16_t(proc, &key->kd_csum_len);
	if (rc != 0)
		return -DER_CRT_HG;

	return 0;
}

static int
daos_proc_nr(crt_proc_t proc, daos_nr_t *dnr)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &dnr->num);
	if (rc != 0)
		return rc;

	rc = crt_proc_uint32_t(proc, &dnr->num_out);
	if (rc != 0)
		return rc;

	return rc;
}

int
daos_proc_sg_list(crt_proc_t proc, daos_sg_list_t *sgl)
{
	crt_proc_op_t	proc_op;
	int		i;
	int		rc;

	rc = daos_proc_nr(proc, &sgl->sg_nr);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_CRT_HG;

	if (proc_op == CRT_PROC_DECODE && sgl->sg_nr.num > 0) {
		D_ALLOC(sgl->sg_iovs, sizeof(sgl->sg_iovs[0]) * sgl->sg_nr.num);
		if (sgl->sg_iovs == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < sgl->sg_nr.num; i++) {
		rc = daos_proc_iovec(proc, &sgl->sg_iovs[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(sgl->sg_iovs,
				       sizeof(sgl->sg_iovs[0]) *
				       sgl->sg_nr.num);
			return -DER_CRT_HG;
		}
	}

	if (proc_op == CRT_PROC_FREE && sgl->sg_iovs != NULL)
		D_FREE(sgl->sg_iovs, sizeof(sgl->sg_iovs[0]) * sgl->sg_nr.num);

	return rc;
}

int
daos_proc_sg_desc_list(crt_proc_t proc, daos_sg_list_t *sgl)
{
	crt_proc_op_t	proc_op;
	int		i;
	int		rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_CRT_HG;

	rc = daos_proc_nr(proc, &sgl->sg_nr);
	if (rc != 0)
		return -DER_CRT_HG;

	if (proc_op == CRT_PROC_DECODE && sgl->sg_nr.num > 0) {
		D_ALLOC(sgl->sg_iovs, sizeof(sgl->sg_iovs[0]) * sgl->sg_nr.num);
		if (sgl->sg_iovs == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < sgl->sg_nr.num; i++) {
		daos_iov_t	*div;

		div = &sgl->sg_iovs[i];
		rc = crt_proc_uint64_t(proc, &div->iov_len);
		if (rc != 0)
			return -DER_CRT_HG;

		rc = crt_proc_uint64_t(proc, &div->iov_buf_len);
		if (rc != 0)
			return -DER_CRT_HG;
	}

	if (proc_op == CRT_PROC_FREE && sgl->sg_iovs != NULL)
		D_FREE(sgl->sg_iovs, sizeof(sgl->sg_iovs[0]) * sgl->sg_nr.num);

	return rc;
}

struct crt_msg_field DMF_OID =
	DEFINE_CRT_MSG("daos_unit_oid_t", 0,
			sizeof(daos_unit_oid_t), daos_proc_unit_oid);

struct crt_msg_field DMF_IOVEC =
	DEFINE_CRT_MSG("daos_iov", 0, sizeof(daos_iov_t), daos_proc_iovec);

struct crt_msg_field DMF_VEC_IOD_ARRAY =
	DEFINE_CRT_MSG("daos_vec_iods", CMF_ARRAY_FLAG, sizeof(daos_vec_iod_t),
			daos_proc_vec_iod);

struct crt_msg_field DMF_REC_SIZE_ARRAY =
	DEFINE_CRT_MSG("daos_rec_size", CMF_ARRAY_FLAG, sizeof(uint64_t),
			crt_proc_uint64_t);

struct crt_msg_field DMF_KEY_DESC_ARRAY =
	DEFINE_CRT_MSG("daos_key_desc", CMF_ARRAY_FLAG, sizeof(daos_key_desc_t),
			daos_proc_key_desc);

struct crt_msg_field DMF_EPOCH_STATE =
	DEFINE_CRT_MSG("daos_epoch_state_t", 0, sizeof(daos_epoch_state_t),
		       daos_proc_epoch_state);

struct crt_msg_field DMF_HASH_OUT =
	DEFINE_CRT_MSG("daos_hash_out_t", 0, sizeof(daos_hash_out_t),
			daos_proc_hash_out);

struct crt_msg_field DMF_SGL_ARRAY =
	DEFINE_CRT_MSG("daos_sg_list_t", CMF_ARRAY_FLAG, sizeof(daos_sg_list_t),
			daos_proc_sg_list);

struct crt_msg_field DMF_SGL =
	DEFINE_CRT_MSG("daos_sg_list_t", 0, sizeof(daos_sg_list_t),
			daos_proc_sg_list);

struct crt_msg_field DMF_SGL_DESC_ARRAY =
	DEFINE_CRT_MSG("daos_sg_desc_list_t", CMF_ARRAY_FLAG,
			sizeof(daos_sg_list_t), daos_proc_sg_desc_list);

struct crt_msg_field DMF_SGL_DESC =
	DEFINE_CRT_MSG("daos_sg_desc_list_t", 0, sizeof(daos_sg_list_t),
			daos_proc_sg_desc_list);
