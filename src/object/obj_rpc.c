/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#include "rpc_csum.h"

static int
crt_proc_struct_dtx_id(crt_proc_t proc, struct dtx_id *dti)
{
	int rc;

	rc = crt_proc_uuid_t(proc, &dti->dti_uuid);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &dti->dti_hlc);
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

	rc = crt_proc_uint32_t(proc, &key->kd_val_type);
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint16_t(proc, &key->kd_csum_type);
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
crt_proc_struct_obj_shard_iod(crt_proc_t proc, struct obj_shard_iod *siod)
{
	if (crt_proc_uint32_t(proc, &siod->siod_tgt_idx) != 0)
		return -DER_HG;
	if (crt_proc_uint32_t(proc, &siod->siod_idx) != 0)
		return -DER_HG;
	if (crt_proc_uint32_t(proc, &siod->siod_nr) != 0)
		return -DER_HG;
	if (crt_proc_uint64_t(proc, &siod->siod_off) != 0)
		return -DER_HG;
	return 0;
}

static int
crt_proc_struct_obj_io_desc(crt_proc_t proc, struct obj_io_desc *oiod)
{
	crt_proc_op_t	proc_op;
	uint32_t	i;
	int		rc;

	rc = crt_proc_uint16_t(proc, &oiod->oiod_nr);
	if (rc)
		return -DER_HG;
	rc = crt_proc_uint16_t(proc, &oiod->oiod_tgt_idx);
	if (rc)
		return -DER_HG;
	rc = crt_proc_uint32_t(proc, &oiod->oiod_flags);
	if (rc)
		return -DER_HG;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc != 0)
		return -DER_HG;
	if (proc_op == CRT_PROC_DECODE && oiod->oiod_nr > 0) {
		rc = obj_io_desc_init(oiod, oiod->oiod_nr, oiod->oiod_flags);
		if (rc)
			return rc;
	}

	for (i = 0; oiod->oiod_siods != NULL && i < oiod->oiod_nr; i++) {
		rc = crt_proc_struct_obj_shard_iod(proc, &oiod->oiod_siods[i]);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				obj_io_desc_fini(oiod);
			return -DER_HG;
		}
	}

	if (proc_op == CRT_PROC_FREE && oiod->oiod_siods != NULL)
		obj_io_desc_fini(oiod);

	return 0;
}

#define IOD_REC_EXIST	(1 << 0)
static int
crt_proc_daos_iod_and_csum(crt_proc_t proc, crt_proc_op_t proc_op,
			   daos_iod_t *iod, struct dcs_iod_csums *iod_csum,
			   struct obj_io_desc *oiod)
{
	uint32_t	i, start, nr;
	bool		proc_one = false;
	bool		singv = false;
	uint32_t	existing_flags = 0;
	int		rc;

	if (proc == NULL || iod == NULL) {
		D_ERROR("Invalid parameter, proc: %p, data: %p.\n", proc, iod);
		return -DER_INVAL;
	}

	rc = crt_proc_d_iov_t(proc, &iod->iod_name);
	if (rc != 0)
		return rc;

	if (rc != 0)
		return rc;

	rc = crt_proc_memcpy(proc, &iod->iod_type, sizeof(iod->iod_type));
	if (rc != 0)
		return -DER_HG;

	rc = crt_proc_uint64_t(proc, &iod->iod_size);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_ENCODE && oiod != NULL &&
	    (oiod->oiod_flags & OBJ_SIOD_PROC_ONE) != 0) {
		proc_one = true;
		if (oiod->oiod_siods != NULL) {
			start = oiod->oiod_siods[0].siod_idx;
			nr = oiod->oiod_siods[0].siod_nr;
			D_ASSERT(start < iod->iod_nr &&
				 start + nr <= iod->iod_nr);
		} else {
			D_ASSERT(oiod->oiod_flags & OBJ_SIOD_SINGV);
			if (iod_csum != NULL && iod_csum->ic_data != NULL &&
			    iod_csum->ic_data[0].cs_nr > 1) {
				start = oiod->oiod_tgt_idx;
				singv = true;
			} else {
				start = 0;
			}
			nr = 1;
		}
		rc = crt_proc_uint32_t(proc, &nr);
	} else {
		start = 0;
		rc = crt_proc_uint32_t(proc, &iod->iod_nr);
		nr = iod->iod_nr;
	}
	if (rc != 0)
		return -DER_HG;

#if 0
	if (iod->iod_nr == 0 && iod->iod_type != DAOS_IOD_ARRAY) {
		D_ERROR("invalid I/O descriptor, iod_nr = 0\n");
		return -DER_HG;
	}
#else
	/* Zero nr is possible for EC (even for singv), as different IODs
	 * possibly with different targets, so for one target server received
	 * IO request, when it with multiple IODs then it is possible that one
	 * IOD is valid (iod_nr > 0) but another IOD is invalid (iod_nr == 0).
	 * The RW handler can just ignore those invalid IODs.
	 */
	if (nr == 0)
		return 0;
#endif

	if (proc_op == CRT_PROC_ENCODE || proc_op == CRT_PROC_FREE) {
		if (iod->iod_type == DAOS_IOD_ARRAY && iod->iod_recxs != NULL)
			existing_flags |= IOD_REC_EXIST;
	}

	rc = crt_proc_uint32_t(proc, &existing_flags);
	if (rc != 0)
		return -DER_HG;

	if (proc_op == CRT_PROC_DECODE) {
		if (existing_flags & IOD_REC_EXIST) {
			D_ALLOC_ARRAY(iod->iod_recxs, nr);
			if (iod->iod_recxs == NULL)
				D_GOTO(free, rc = -DER_NOMEM);
		}
	}

	if (existing_flags & IOD_REC_EXIST) {
		D_ASSERT(iod->iod_recxs != NULL || nr == 0);
		for (i = start; i < start + nr; i++) {
			rc = crt_proc_daos_recx_t(proc, &iod->iod_recxs[i]);
			if (rc != 0) {
				if (proc_op == CRT_PROC_DECODE)
					D_GOTO(free, rc);
				return rc;
			}
		}
	}

	if (iod_csum) {
		rc = crt_proc_struct_dcs_iod_csums_adv(proc, proc_op, iod_csum,
						       singv, start, nr);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_GOTO(free, rc);
			return rc;
		}
	}

	if (oiod != NULL && !proc_one) {
		rc = crt_proc_struct_obj_io_desc(proc, oiod);
		if (rc != 0) {
			if (proc_op == CRT_PROC_DECODE)
				D_GOTO(free, rc);
			return rc;
		}
	}

	if (proc_op == CRT_PROC_FREE) {
free:
		if ((existing_flags & IOD_REC_EXIST) && iod->iod_recxs != NULL)
			D_FREE(iod->iod_recxs);
	}

	return rc;
}

static int
crt_proc_struct_obj_iod_array(crt_proc_t proc, struct obj_iod_array *iod_array)
{
	struct obj_io_desc	*oiod;
	void			*buf;
	crt_proc_op_t		 proc_op;
	daos_size_t		 iod_size, off_size, buf_size, csum_size;
	uint8_t			 with_iod_csums = 0;
	uint32_t		 off_nr;
	bool			 proc_one = false;
	int			 i, rc;

	rc = crt_proc_get_op(proc, &proc_op);
	if (rc)
		return rc;

	if (proc_op == CRT_PROC_ENCODE && iod_array->oia_oiods != NULL &&
	    (iod_array->oia_oiods[0].oiod_flags & OBJ_SIOD_PROC_ONE) != 0) {
		proc_one = true;
		iod_array->oia_oiod_nr = 0;
	}

	if (crt_proc_uint32_t(proc, &iod_array->oia_iod_nr) != 0)
		return -DER_HG;
	if (crt_proc_uint32_t(proc, &iod_array->oia_oiod_nr) != 0)
		return -DER_HG;
	if (iod_array->oia_iod_nr == 0)
		return 0;

	D_ASSERT(iod_array->oia_oiod_nr == iod_array->oia_iod_nr ||
		 iod_array->oia_oiod_nr == 0);

	if (proc_op == CRT_PROC_ENCODE) {
		off_nr = iod_array->oia_offs != NULL ?
			 iod_array->oia_iod_nr : 0;
		if (crt_proc_uint32_t(proc, &off_nr) != 0)
			return -DER_HG;
		with_iod_csums = iod_array->oia_iod_csums != NULL ? 1 : 0;
		if (crt_proc_uint8_t(proc, &with_iod_csums) != 0)
			return -DER_HG;
		D_ASSERT(iod_array->oia_offs != NULL || off_nr == 0);
		for (i = 0; i < off_nr; i++) {
			if (crt_proc_uint64_t(proc, &iod_array->oia_offs[i]))
				return -DER_HG;
		}
	} else if (proc_op == CRT_PROC_DECODE) {
		if (crt_proc_uint32_t(proc, &off_nr) != 0)
			return -DER_HG;
		if (crt_proc_uint8_t(proc, &with_iod_csums) != 0)
			return -DER_HG;
		iod_size = roundup(sizeof(daos_iod_t) * iod_array->oia_iod_nr,
				   8);
		off_size = sizeof(uint64_t) * off_nr;
		if (with_iod_csums)
			csum_size = roundup(sizeof(struct dcs_iod_csums) *
					    iod_array->oia_iod_nr, 8);
		else
			csum_size = 0;
		buf_size = iod_size + off_size + csum_size;
		if (iod_array->oia_oiod_nr != 0)
			buf_size += sizeof(struct obj_io_desc) *
				    iod_array->oia_oiod_nr;
		D_ALLOC(buf, buf_size);
		if (buf == NULL)
			return -DER_NOMEM;
		iod_array->oia_iods = buf;
		if (off_nr != 0) {
			iod_array->oia_offs = buf + iod_size;
			for (i = 0; i < off_nr; i++) {
				if (crt_proc_uint64_t(proc,
					&iod_array->oia_offs[i]))
					return -DER_HG;
			}
		} else {
			iod_array->oia_offs = NULL;
		}

		if (with_iod_csums)
			iod_array->oia_iod_csums = buf + iod_size + off_size;
		else
			iod_array->oia_iod_csums = NULL;

		if (iod_array->oia_oiod_nr != 0)
			iod_array->oia_oiods = buf + iod_size + off_size +
					       csum_size;
		else
			iod_array->oia_oiods = NULL;
	}

	for (i = 0; i < iod_array->oia_iod_nr; i++) {
		struct dcs_iod_csums	*iod_csum;

		if (iod_array->oia_oiod_nr != 0 || proc_one) {
			D_ASSERT(iod_array->oia_oiods != NULL);
			oiod = &iod_array->oia_oiods[i];
		} else {
			oiod = NULL;
		}

		iod_csum = (iod_array->oia_iod_csums != NULL) ?
			   (&iod_array->oia_iod_csums[i]) :
			   NULL;
		rc = crt_proc_daos_iod_and_csum(proc, proc_op,
						&iod_array->oia_iods[i],
						iod_csum,
						oiod);
		if (rc)
			break;
	}

	if (proc_op == CRT_PROC_FREE) {
		D_FREE(iod_array->oia_iods);
		return 0;
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
CRT_RPC_DEFINE(obj_sync, DAOS_ISEQ_OBJ_SYNC, DAOS_OSEQ_OBJ_SYNC)
CRT_RPC_DEFINE(obj_migrate, DAOS_ISEQ_OBJ_MIGRATE, DAOS_OSEQ_OBJ_MIGRATE)


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
	case DAOS_OBJ_RPC_SYNC:
		((struct obj_sync_out *)reply)->oso_ret = status;
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
	case DAOS_OBJ_RPC_SYNC:
		return ((struct obj_sync_out *)reply)->oso_ret;
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
	case DAOS_OBJ_RPC_SYNC:
		((struct obj_sync_out *)reply)->oso_map_version = map_version;
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
	case DAOS_OBJ_RPC_SYNC:
		return ((struct obj_sync_out *)reply)->oso_map_version;
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
	case DAOS_OBJ_RPC_TGT_UPDATE: {
		struct obj_rw_out	*orw = reply;

		daos_dti_copy(&orw->orw_dti_conflict, &dce->dce_xid);
		orw->orw_dkey_conflict = dce->dce_dkey;
		break;
	}
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
