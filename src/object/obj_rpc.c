/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * DSR: RPC Protocol Serialization Functions
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/common.h>
#include <daos/event.h>
#include <daos/rpc.h>
#include <daos/object.h>
#include "obj_rpc.h"

static int
crt_proc_struct_dtx_id(crt_proc_t proc, struct dtx_id *dti)
{
	int rc;

	rc = crt_proc_uuid_t(proc, &dti->dti_uuid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dti->dti_sec);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_key_desc_t(crt_proc_t proc, daos_key_desc_t *key)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &key->kd_key_len);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &key->kd_val_types);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &key->kd_csum_type);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &key->kd_csum_len);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_obj_id_t(crt_proc_t proc, daos_obj_id_t *doi)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &doi->lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &doi->hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_unit_oid_t(crt_proc_t proc, daos_unit_oid_t *doi)
{
	int rc;

	rc = crt_proc_daos_obj_id_t(proc, &doi->id_pub);
	if (rc != 0)
		return rc;

	rc = crt_proc_uint32_t(proc, &doi->id_shard);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &doi->id_pad_32);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_recx_t(crt_proc_t proc, daos_recx_t *recx)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &recx->rx_idx);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &recx->rx_nr);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_epoch_range_t(crt_proc_t proc, daos_epoch_range_t *erange)
{
	int rc;

	rc = crt_proc_uint64_t(proc, &erange->epr_lo);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &erange->epr_hi);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_daos_csum_buf_t(crt_proc_t proc, daos_csum_buf_t *csum)
{
	crt_proc_op_t	proc_op;
	int		rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &csum->cs_type);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &csum->cs_len);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &csum->cs_buf_len);
	if (rc != 0)
		return -DER_HG;

	if (csum->cs_buf_len < csum->cs_len) {
		D_ERROR("invalid csum buf len %iu < csum len %hu\n",
			csum->cs_buf_len, csum->cs_len);
		return -DER_HG;
	}

	if (proc_op == CRT_PROC_DECODE && csum->cs_buf_len > 0) {
		D_ALLOC(csum->cs_csum, csum->cs_buf_len);
		if (csum->cs_csum == NULL)
			return -DER_NOMEM;
	} else if (proc_op == CRT_PROC_FREE && csum->cs_buf_len > 0) {
		D_FREE(csum->cs_csum);
	}

	if (csum->cs_len > 0) {
		rc = crt_proc_memcpy(proc, csum->cs_csum, csum->cs_len);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(csum->cs_csum);
			return -DER_HG;
		}
	}

	return 0;
}

#define IOD_REC_EXIST	(1 << 0)
#define IOD_CSUM_EXIST	(1 << 1)
#define IOD_EPRS_EXIST	(1 << 2)
static int
crt_proc_daos_iod_t(crt_proc_t proc, daos_iod_t *dvi)
{
	crt_proc_op_t	proc_op;
	int		rc;
	int		i;
	uint32_t	existing_flags = 0;

	if (proc == NULL || dvi == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n", proc, dvi);
		return -DER_INVAL;
	}

	rc = crt_proc_d_iov_t(proc, &dvi->iod_name);
	if (rc != 0)
		return rc;

	rc = crt_proc_daos_csum_buf_t(proc, &dvi->iod_kcsum);
	if (rc != 0)
		return rc;

	rc = crt_proc_memcpy(proc, &dvi->iod_type, sizeof(dvi->iod_type));
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dvi->iod_size);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &dvi->iod_nr);
	if (rc != 0)
		return -DER_HG;

	if (dvi->iod_nr == 0 && dvi->iod_type != DAOS_IOD_ARRAY) {
		D_ERROR("invalid I/O descriptor, iod_nr = 0\n");
		return -DER_HG;
	}

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_ENCODE) {
		if (dvi->iod_type == DAOS_IOD_ARRAY && dvi->iod_recxs != NULL)
			existing_flags |= IOD_REC_EXIST;
		if (dvi->iod_csums != NULL)
			existing_flags |= IOD_CSUM_EXIST;
		if (dvi->iod_eprs != NULL)
			existing_flags |= IOD_EPRS_EXIST;
	}

	rc = crt_proc_uint32_t(proc, &existing_flags);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE) {
		if (existing_flags & IOD_REC_EXIST) {
			D_ALLOC_ARRAY(dvi->iod_recxs, dvi->iod_nr);
			if (dvi->iod_recxs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}

		if (existing_flags & IOD_CSUM_EXIST) {
			D_ALLOC_ARRAY(dvi->iod_csums, dvi->iod_nr);
			if (dvi->iod_csums == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}

		if (existing_flags & IOD_EPRS_EXIST) {
			D_ALLOC_ARRAY(dvi->iod_eprs, dvi->iod_nr);
			if (dvi->iod_eprs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}
	}

	if (existing_flags & IOD_REC_EXIST) {
		for (i = 0; i < dvi->iod_nr; i++) {
			rc = crt_proc_daos_recx_t(proc, &dvi->iod_recxs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & IOD_CSUM_EXIST) {
		for (i = 0; i < dvi->iod_nr; i++) {
			rc = crt_proc_daos_csum_buf_t(proc, &dvi->iod_csums[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (existing_flags & IOD_EPRS_EXIST) {
		for (i = 0; i < dvi->iod_nr; i++) {
			rc = crt_proc_daos_epoch_range_t(proc,
							 &dvi->iod_eprs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (proc_op == CRT_PROC_FREE) {
free:
		if (dvi->iod_recxs != NULL)
			D_FREE(dvi->iod_recxs);
		if (dvi->iod_csums != NULL)
			D_FREE(dvi->iod_csums);
		if (dvi->iod_eprs != NULL)
			D_FREE(dvi->iod_eprs);
	}

	return rc;
}

static int
crt_proc_daos_anchor_t(crt_proc_t proc, daos_anchor_t *anchor)
{
	if (crt_proc_uint16_t(proc, &anchor->da_type) != 0)
		return -DER_HG;

	if (crt_proc_uint16_t(proc, &anchor->da_shard) != 0)
		return -DER_HG;

	if (crt_proc_uint32_t(proc, &anchor->da_flags) != 0)
		return -DER_HG;

	if (crt_proc_raw(proc, anchor->da_buf, sizeof(anchor->da_buf)) != 0)
		return -DER_HG;

	return 0;
}

static int
crt_proc_d_sg_list_t(crt_proc_t proc, d_sg_list_t *sgl)
{
	crt_proc_op_t	proc_op;
	int		i;
	int		rc;

	rc = crt_proc_uint32_t(proc, &sgl->sg_nr);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint32_t(proc, &sgl->sg_nr_out);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE && sgl->sg_nr > 0) {
		D_ALLOC_ARRAY(sgl->sg_iovs, sgl->sg_nr);
		if (sgl->sg_iovs == NULL)
			return -DER_NOMEM;
	}

	for (i = 0; i < sgl->sg_nr; i++) {
		rc = crt_proc_d_iov_t(proc, &sgl->sg_iovs[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_FREE(sgl->sg_iovs);
			return -DER_HG;
		}
	}

	if (proc_op == CRT_PROC_FREE && sgl->sg_iovs != NULL)
		D_FREE(sgl->sg_iovs);

	return rc;
}


static int
crt_proc_struct_daos_shard_tgt(crt_proc_t proc, struct daos_shard_tgt *st)
{
	int rc;

	rc = crt_proc_uint32_t(proc, &st->st_rank);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_shard);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_tgt_idx);
	if (rc != 0)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &st->st_tgt_id);
	if (rc != 0)
		return -DER_HG;

	return 0;
}

CRT_RPC_DEFINE(obj_update, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
CRT_RPC_DEFINE(obj_fetch, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
CRT_RPC_DEFINE(obj_key_enum, DAOS_ISEQ_OBJ_KEY_ENUM, DAOS_OSEQ_OBJ_KEY_ENUM)
CRT_RPC_DEFINE(obj_punch, DAOS_ISEQ_OBJ_PUNCH, DAOS_OSEQ_OBJ_PUNCH)
CRT_RPC_DEFINE(obj_query_key, DAOS_ISEQ_OBJ_QUERY_KEY, DAOS_OSEQ_OBJ_QUERY_KEY)

/* Define for cont_rpcs[] array population below.
 * See OBJ_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
}

static struct crt_proto_rpc_format obj_proto_rpc_fmt[] = {
	OBJ_PROTO_CLI_RPC_LIST,
};

#undef X

struct crt_proto_format obj_proto_fmt = {
	.cpf_name  = "daos-obj-proto",
	.cpf_ver   = DAOS_OBJ_VERSION,
	.cpf_count = ARRAY_SIZE(obj_proto_rpc_fmt),
	.cpf_prf   = obj_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_OBJ_MODULE, 0)
};

void
obj_reply_set_status(crt_rpc_t *rpc, int status)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
	case DAOS_OBJ_RPC_TGT_UPDATE:
		((struct obj_rw_out *)reply)->orw_ret = status;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_ret = status;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_ret = status;
		break;
	case DAOS_OBJ_RPC_QUERY_KEY:
		((struct obj_query_key_out *)reply)->okqo_ret = status;
		break;
	default:
		D_ASSERT(0);
	}
}

int
obj_reply_get_status(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_ret;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_ret;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_ret;
	case DAOS_OBJ_RPC_QUERY_KEY:
		return ((struct obj_query_key_out *)reply)->okqo_ret;
	default:
		D_ASSERT(0);
	}
	return 0;
}

void
obj_reply_map_version_set(crt_rpc_t *rpc, uint32_t map_version)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		((struct obj_rw_out *)reply)->orw_map_version = map_version;
		break;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		((struct obj_key_enum_out *)reply)->oeo_map_version =
								map_version;
		break;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		((struct obj_punch_out *)reply)->opo_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_QUERY_KEY:
		((struct obj_query_key_out *)reply)->okqo_map_version =
			map_version;
		break;
	default:
		D_ASSERT(0);
	}
}

uint32_t
obj_reply_map_version_get(crt_rpc_t *rpc)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE:
	case DAOS_OBJ_RPC_FETCH:
		return ((struct obj_rw_out *)reply)->orw_map_version;
	case DAOS_OBJ_DKEY_RPC_ENUMERATE:
	case DAOS_OBJ_AKEY_RPC_ENUMERATE:
	case DAOS_OBJ_RECX_RPC_ENUMERATE:
	case DAOS_OBJ_RPC_ENUMERATE:
		return ((struct obj_key_enum_out *)reply)->oeo_map_version;
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS:
		return ((struct obj_punch_out *)reply)->opo_map_version;
	case DAOS_OBJ_RPC_QUERY_KEY:
		return ((struct obj_query_key_out *)reply)->okqo_map_version;
	default:
		D_ASSERT(0);
	}
	return 0;
}

void
obj_reply_dtx_conflict_set(crt_rpc_t *rpc, struct dtx_conflict_entry *dce)
{
	void *reply = crt_reply_get(rpc);

	switch (opc_get(rpc->cr_opc)) {
	case DAOS_OBJ_RPC_UPDATE:
	case DAOS_OBJ_RPC_TGT_UPDATE: {
		struct obj_rw_out	*orw = reply;

		daos_dti_copy(&orw->orw_dti_conflict, &dce->dce_xid);
		orw->orw_dkey_conflict = dce->dce_dkey;
		break;
	}
	case DAOS_OBJ_RPC_PUNCH:
	case DAOS_OBJ_RPC_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_PUNCH_AKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH:
	case DAOS_OBJ_RPC_TGT_PUNCH_DKEYS:
	case DAOS_OBJ_RPC_TGT_PUNCH_AKEYS: {
		struct obj_punch_out	*opo = reply;

		daos_dti_copy(&opo->opo_dti_conflict, &dce->dce_xid);
		opo->opo_dkey_conflict = dce->dce_dkey;
		break;
	}
	default:
		D_ASSERT(0);
	}
}
