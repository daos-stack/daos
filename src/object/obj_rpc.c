/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
crt_proc_daos_key_desc_t(crt_proc_t proc, crt_proc_op_t proc_op,
			 daos_key_desc_t *key)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &key->kd_key_len);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &key->kd_val_type);
	if (unlikely(rc))
		return rc;

	return 0;
}

static inline int
crt_proc_daos_unit_oid_t(crt_proc_t proc, crt_proc_op_t proc_op,
			 daos_unit_oid_t *p)
{
	return crt_proc_memcpy(proc, proc_op, p, sizeof(*p));
}

static int
crt_proc_daos_recx_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_recx_t *recx)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &recx->rx_idx);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &recx->rx_nr);
	if (unlikely(rc))
		return rc;

	return 0;
}

static int
crt_proc_daos_epoch_range_t(crt_proc_t proc, crt_proc_op_t proc_op,
			    daos_epoch_range_t *erange)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &erange->epr_lo);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &erange->epr_hi);
	if (unlikely(rc))
		return rc;

	return 0;
}

static int
crt_proc_struct_obj_io_desc(crt_proc_t proc, crt_proc_op_t proc_op,
			    struct obj_io_desc *oiod)
{
	int		rc;

	if (FREEING(proc_op)) {
		obj_io_desc_fini(oiod);
		return 0;
	}

	rc = crt_proc_uint16_t(proc, proc_op, &oiod->oiod_nr);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint16_t(proc, proc_op, &oiod->oiod_tgt_idx);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &oiod->oiod_flags);
	if (unlikely(rc))
		return rc;

	if (oiod->oiod_nr == 0)
		return rc;

	switch (proc_op) {
	case CRT_PROC_DECODE:
		rc = obj_io_desc_init(oiod, oiod->oiod_nr, oiod->oiod_flags);
		if (unlikely(rc))
			return rc;
		/* fall through to fill oiod_siods */
	case CRT_PROC_ENCODE:
		if (oiod->oiod_siods != NULL) { /* !(flags & OBJ_SIOD_SINGV) */
			rc = crt_proc_memcpy(proc, proc_op, oiod->oiod_siods,
					     oiod->oiod_nr *
					     sizeof(*oiod->oiod_siods));
			if (unlikely(rc)) {
				if (DECODING(proc_op))
					obj_io_desc_fini(oiod);
				return rc;
			}
		}
		break;
	default:
		return -DER_INVAL;
	}

	return 0;
}

#define IOD_REC_EXIST	(1 << 0)
static int
crt_proc_daos_iod_and_csum(crt_proc_t proc, crt_proc_op_t proc_op,
			   daos_iod_t *iod, struct dcs_iod_csums *iod_csum,
			   struct obj_io_desc *oiod)
{
	uint32_t	start, nr;
	bool		proc_one = false;
	bool		singv = false;
	uint32_t	existing_flags = 0;
	int		rc;

	rc = crt_proc_d_iov_t(proc, proc_op, &iod->iod_name);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_memcpy(proc, proc_op,
			     &iod->iod_type, sizeof(iod->iod_type));
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_uint64_t(proc, proc_op, &iod->iod_size);
	if (unlikely(rc))
		D_GOTO(out, rc);

	rc = crt_proc_uint64_t(proc, proc_op, &iod->iod_flags);
	if (unlikely(rc))
		D_GOTO(out, rc);

	if (ENCODING(proc_op) && oiod != NULL &&
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
		rc = crt_proc_uint32_t(proc, proc_op, &nr);
	} else {
		start = 0;
		rc = crt_proc_uint32_t(proc, proc_op, &iod->iod_nr);
		nr = iod->iod_nr;
	}
	if (unlikely(rc))
		D_GOTO(out, rc);

	if (iod_csum) {
		rc = crt_proc_struct_dcs_iod_csums_adv(proc, proc_op, iod_csum,
						       singv, start, nr);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(out_free, rc);
			D_GOTO(out, rc);
		}
	}

#if 0
	if (iod->iod_nr == 0 && iod->iod_type != DAOS_IOD_ARRAY) {
		D_ERROR("invalid I/O descriptor, iod_nr = 0\n");
		D_GOTO(out, rc = -DER_HG);
	}
#else
	/* Zero nr is possible for EC (even for singv), as different IODs
	 * possibly with different targets, so for one target server received
	 * IO request, when it with multiple IODs then it is possible that one
	 * IOD is valid (iod_nr > 0) but another IOD is invalid (iod_nr == 0).
	 * The RW handler can just ignore those invalid IODs.
	 */
	if (nr == 0)
		D_GOTO(out, rc = 0);
#endif

	if (ENCODING(proc_op) || FREEING(proc_op)) {
		if (iod->iod_type == DAOS_IOD_ARRAY && iod->iod_recxs != NULL)
			existing_flags |= IOD_REC_EXIST;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &existing_flags);
	if (unlikely(rc))
		D_GOTO(out, rc);

	if (DECODING(proc_op)) {
		if (existing_flags & IOD_REC_EXIST) {
			D_ALLOC_ARRAY(iod->iod_recxs, nr);
			if (iod->iod_recxs == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}

	if (existing_flags & IOD_REC_EXIST) {
		D_ASSERT(iod->iod_recxs != NULL || nr == 0);
		if (nr > 0) {
			rc = crt_proc_memcpy(proc, proc_op,
					     &iod->iod_recxs[start],
					     nr * sizeof(*iod->iod_recxs));
			if (unlikely(rc)) {
				if (DECODING(proc_op))
					D_GOTO(out_free, rc);
				D_GOTO(out, rc);
			}
		}
	}

	if (oiod != NULL && !proc_one) {
		rc = crt_proc_struct_obj_io_desc(proc, proc_op, oiod);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(out_free, rc);
			D_GOTO(out, rc);
		}
	}

	if (FREEING(proc_op)) {
out_free:
		if (existing_flags & IOD_REC_EXIST)
			D_FREE(iod->iod_recxs);
	}
out:
	return rc;
}

static int crt_proc_daos_iom_t(crt_proc_t proc, crt_proc_op_t proc_op,
			       daos_iom_t *map)
{
	uint32_t	iom_nr = 0;
	int		rc;

	if (FREEING(proc_op)) {
		D_FREE(map->iom_recxs);
		return 0;
	}

	rc = crt_proc_memcpy(proc, proc_op,
			     &map->iom_type, sizeof(map->iom_type));
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &map->iom_size);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_daos_recx_t(proc, proc_op, &map->iom_recx_lo);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_daos_recx_t(proc, proc_op, &map->iom_recx_hi);
	if (unlikely(rc))
		return rc;

	if (ENCODING(proc_op) && (map->iom_flags & DAOS_IOMF_DETAIL))
		iom_nr = map->iom_nr;

	rc = crt_proc_uint32_t(proc, proc_op, &iom_nr);
	if (unlikely(rc))
		return rc;

	if (iom_nr == 0)
		return 0;

	if (DECODING(proc_op)) {
		map->iom_nr = iom_nr;
		map->iom_nr_out = iom_nr;
		D_ALLOC_ARRAY(map->iom_recxs, iom_nr);
		if (map->iom_recxs == NULL)
			return -DER_NOMEM;
	}
	rc = crt_proc_memcpy(proc, proc_op, map->iom_recxs,
			     iom_nr * sizeof(*map->iom_recxs));
	if (unlikely(rc)) {
		if (DECODING(proc_op))
			D_FREE(map->iom_recxs);
		return rc;
	}

	return 0;
}

static int
crt_proc_struct_daos_recx_ep_list(crt_proc_t proc, crt_proc_op_t proc_op,
				  struct daos_recx_ep_list *list)
{
	unsigned int	i;
	int		rc;

	if (FREEING(proc_op)) {
		daos_recx_ep_free(list);
		return 0;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &list->re_nr);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_bool(proc, proc_op, &list->re_snapshot);
	if (unlikely(rc))
		return rc;
	rc = crt_proc_bool(proc, proc_op, &list->re_ep_valid);
	if (unlikely(rc))
		return rc;

	if (list->re_nr == 0)
		return 0;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(list->re_items, list->re_nr);
		if (list->re_items == NULL)
			return -DER_NOMEM;
		list->re_total = list->re_nr;
	}
	rc = crt_proc_memcpy(proc, proc_op, list->re_items,
			     list->re_nr * sizeof(*list->re_items));
	if (unlikely(rc)) {
		if (DECODING(proc_op))
			daos_recx_ep_free(list);
		return rc;
	}

	if (!list->re_ep_valid && DECODING(proc_op)) {
		for (i = 0; i < list->re_nr; i++)
			list->re_items[i].re_ep = 0;
	}

	return rc;
}

static int
crt_proc_struct_obj_iod_array(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct obj_iod_array *iod_array)
{
	struct obj_io_desc	*oiod;
	void			*buf;
	daos_size_t		 iod_size, off_size, buf_size, csum_size;
	uint8_t			 with_iod_csums = 0;
	uint32_t		 off_nr;
	bool			 proc_one = false;
	int			 i, rc;

	if (ENCODING(proc_op) && iod_array->oia_oiods != NULL &&
	    (iod_array->oia_oiods[0].oiod_flags & OBJ_SIOD_PROC_ONE) != 0) {
		proc_one = true;
		iod_array->oia_oiod_nr = 0;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &iod_array->oia_iod_nr);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &iod_array->oia_oiod_nr);
	if (unlikely(rc))
		return rc;

	if (iod_array->oia_iod_nr == 0)
		return 0;

	D_ASSERT(iod_array->oia_oiod_nr == iod_array->oia_iod_nr ||
		 iod_array->oia_oiod_nr == 0);

	if (ENCODING(proc_op)) {
		off_nr = iod_array->oia_offs != NULL ?
			 iod_array->oia_iod_nr : 0;
		rc = crt_proc_uint32_t(proc, proc_op, &off_nr);
		if (unlikely(rc))
			return rc;

		with_iod_csums = iod_array->oia_iod_csums != NULL ? 1 : 0;
		rc = crt_proc_uint8_t(proc, proc_op, &with_iod_csums);
		if (unlikely(rc))
			return rc;

		D_ASSERT(iod_array->oia_offs != NULL || off_nr == 0);
		if (off_nr != 0) {
			rc = crt_proc_memcpy(proc, proc_op,
					     iod_array->oia_offs, off_nr *
					     sizeof(*iod_array->oia_offs));
			if (unlikely(rc))
				return rc;
		}
	} else if (DECODING(proc_op)) {
		rc = crt_proc_uint32_t(proc, proc_op, &off_nr);
		if (unlikely(rc))
			return rc;

		rc = crt_proc_uint8_t(proc, proc_op, &with_iod_csums);
		if (unlikely(rc))
			return rc;

		iod_size = roundup(sizeof(daos_iod_t) * iod_array->oia_iod_nr,
				   8);
		off_size = sizeof(*iod_array->oia_offs) * off_nr;
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
			rc = crt_proc_memcpy(proc, proc_op,
					     iod_array->oia_offs, off_nr *
					     sizeof(*iod_array->oia_offs));
			if (unlikely(rc)) {
				D_FREE(iod_array->oia_iods);
				return rc;
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
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_FREE(iod_array->oia_iods);
			return rc;
		}
	}

	if (FREEING(proc_op))
		D_FREE(iod_array->oia_iods);

	return rc;
}

static int
crt_proc_d_sg_list_t(crt_proc_t proc, crt_proc_op_t proc_op, d_sg_list_t *p)
{
	int		i;
	int		rc;

	if (FREEING(proc_op)) {
		/* NB: don't need free in crt_proc_d_iov_t() */
		D_FREE(p->sg_iovs);
		return 0;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &p->sg_nr);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &p->sg_nr_out);
	if (unlikely(rc))
		return rc;

	if (p->sg_nr == 0)
		return 0;

	switch (proc_op) {
	case CRT_PROC_DECODE:
		D_ALLOC_ARRAY(p->sg_iovs, p->sg_nr);
		if (p->sg_iovs == NULL)
			return -DER_NOMEM;
		/* fall through to fill sg_iovs */
	case CRT_PROC_ENCODE:
		for (i = 0; i < p->sg_nr; i++) {
			rc = crt_proc_d_iov_t(proc, proc_op, &p->sg_iovs[i]);
			if (unlikely(rc)) {
				if (DECODING(proc_op))
					D_FREE(p->sg_iovs);
				return rc;
			}
		}
		break;
	default:
		return -DER_INVAL;
	}

	return rc;
}

static int
crt_proc_struct_daos_shard_tgt(crt_proc_t proc, crt_proc_op_t proc_op,
			       struct daos_shard_tgt *p)
{
	return crt_proc_memcpy(proc, proc_op, p, sizeof(*p));
}

/* For compounded RPC. */

static int
crt_proc_struct_daos_cpd_sub_head(crt_proc_t proc, crt_proc_op_t proc_op,
				  struct daos_cpd_sub_head *dcsh, bool mbs)
{
	uint32_t	size = 0;
	int		rc;

	if (FREEING(proc_op)) {
		if (mbs)
			D_FREE(dcsh->dcsh_mbs);
		return 0;
	}

	rc = crt_proc_struct_dtx_id(proc, proc_op, &dcsh->dcsh_xid);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_daos_unit_oid_t(proc, proc_op, &dcsh->dcsh_leader_oid);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_memcpy(proc, proc_op,
			     &dcsh->dcsh_epoch, sizeof(dcsh->dcsh_epoch));
	if (unlikely(rc))
		return rc;

	if (!mbs)
		return 0;

	if (ENCODING(proc_op))
		/* Pack the size of dcsh->dcsh_mbs to help decode case. */
		size = sizeof(*dcsh->dcsh_mbs) + dcsh->dcsh_mbs->dm_data_size;

	rc = crt_proc_uint32_t(proc, proc_op, &size);
	if (unlikely(rc))
		return rc;

	D_ASSERT(size != 0);

	if (DECODING(proc_op)) {
		D_ALLOC(dcsh->dcsh_mbs, size);
		if (dcsh->dcsh_mbs == NULL)
			return -DER_NOMEM;
	}

	rc = crt_proc_memcpy(proc, proc_op, dcsh->dcsh_mbs, size);
	if (unlikely(rc)) {
		if (DECODING(proc_op))
			D_FREE(dcsh->dcsh_mbs);
		return rc;
	}

	return 0;
}

static int
crt_proc_daos_iod_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_iod_t *iod)
{
	int rc;

	rc = crt_proc_daos_key_t(proc, proc_op, &iod->iod_name);
	if (unlikely(rc))
		return rc;

	if (FREEING(proc_op)) {
		/* NB: don't need free in crt_proc_d_iov_t() */
		D_FREE(iod->iod_recxs);
		return 0;
	}

	rc = crt_proc_memcpy(proc, proc_op,
			     &iod->iod_type, sizeof(iod->iod_type));
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &iod->iod_size);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &iod->iod_flags);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &iod->iod_nr);
	if (unlikely(rc))
		return rc;

	if (iod->iod_nr == 0)
		return 0;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(iod->iod_recxs, iod->iod_nr);
		if (iod->iod_recxs == NULL)
			return -DER_NOMEM;
	}
	rc = crt_proc_memcpy(proc, proc_op, iod->iod_recxs,
			     iod->iod_nr * sizeof(*iod->iod_recxs));
	if (unlikely(rc)) {
		if (DECODING(proc_op)) {
			D_FREE(iod->iod_name.iov_buf);
			D_FREE(iod->iod_recxs);
		}
		return rc;
	}

	return 0;
}

int
crt_proc_struct_daos_cpd_sub_req(crt_proc_t proc, crt_proc_op_t proc_op,
				 struct daos_cpd_sub_req *dcsr, bool with_oid)
{
	int		rc;
	int		i;

	rc = crt_proc_uint16_t(proc, proc_op, &dcsr->dcsr_opc);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint16_t(proc, proc_op, &dcsr->dcsr_ec_tgt_nr);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &dcsr->dcsr_nr);
	if (unlikely(rc))
		return rc;

	if (with_oid) {
		rc = crt_proc_daos_unit_oid_t(proc, proc_op, &dcsr->dcsr_oid);
	} else if (ENCODING(proc_op)) {
		daos_unit_oid_t		 oid = { 0 };

		daos_dc_obj2id(dcsr->dcsr_obj, &oid);
		/*
		 * It is not important what the id_shard is, that
		 * is packed via daos_cpd_req_idx::dcri_shard_id.
		 */
		rc = crt_proc_daos_unit_oid_t(proc, proc_op, &oid);
	}
	if (unlikely(rc))
		return rc;

	rc = crt_proc_daos_key_t(proc, proc_op, &dcsr->dcsr_dkey);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &dcsr->dcsr_dkey_hash);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &dcsr->dcsr_api_flags);
	if (unlikely(rc))
		return rc;

	switch (dcsr->dcsr_opc) {
	case DCSO_UPDATE: {
		struct daos_cpd_update	*dcu = &dcsr->dcsr_update;

		if (DECODING(proc_op)) {
			if (dcsr->dcsr_ec_tgt_nr != 0) {
				D_ALLOC_ARRAY(dcu->dcu_ec_tgts,
					      dcsr->dcsr_ec_tgt_nr);
				if (dcu->dcu_ec_tgts == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}
		}

		rc = crt_proc_struct_dcs_csum_info(proc, proc_op,
						   &dcu->dcu_dkey_csum);
		if (unlikely(rc))
			D_GOTO(out, rc);

		if (dcsr->dcsr_ec_tgt_nr > 0) {
			rc = crt_proc_memcpy(proc, proc_op, dcu->dcu_ec_tgts,
					     dcsr->dcsr_ec_tgt_nr *
					     sizeof(*dcu->dcu_ec_tgts));
			if (unlikely(rc))
				D_GOTO(out, rc);
		}

		rc = crt_proc_struct_obj_iod_array(proc, proc_op,
						   &dcu->dcu_iod_array);
		if (unlikely(rc))
			D_GOTO(out, rc);

		rc = crt_proc_uint32_t(proc, proc_op, &dcu->dcu_start_shard);
		if (unlikely(rc))
			D_GOTO(out, rc);

		rc = crt_proc_uint32_t(proc, proc_op, &dcu->dcu_flags);
		if (unlikely(rc))
			D_GOTO(out, rc);

		if (dcsr->dcsr_nr == 0)
			D_GOTO(out, rc = 0);

		if (dcu->dcu_flags & ORF_CPD_BULK) {
			if (DECODING(proc_op)) {
				D_ALLOC_ARRAY(dcu->dcu_bulks, dcsr->dcsr_nr);
				if (dcu->dcu_bulks == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}

			for (i = 0; i < dcsr->dcsr_nr; i++) {
				rc = crt_proc_crt_bulk_t(proc, proc_op,
							 &dcu->dcu_bulks[i]);
				if (unlikely(rc))
					D_GOTO(out, rc);
			}
		} else if (!(dcu->dcu_flags & ORF_EMPTY_SGL)) {
			if (DECODING(proc_op)) {
				D_ALLOC_ARRAY(dcu->dcu_sgls, dcsr->dcsr_nr);
				if (dcu->dcu_sgls == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}

			for (i = 0; i < dcsr->dcsr_nr; i++) {
				rc = crt_proc_d_sg_list_t(proc, proc_op,
							  &dcu->dcu_sgls[i]);
				if (unlikely(rc))
					D_GOTO(out, rc);
			}
		}

		break;
	}
	case DCSO_PUNCH_OBJ:
	case DCSO_PUNCH_DKEY:
	case DCSO_PUNCH_AKEY: {
		struct daos_cpd_punch	*dcp = &dcsr->dcsr_punch;

		if (dcsr->dcsr_nr == 0)
			D_GOTO(out, rc = 0);

		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(dcp->dcp_akeys, dcsr->dcsr_nr);
			if (dcp->dcp_akeys == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		for (i = 0; i < dcsr->dcsr_nr; i++) {
			rc = crt_proc_daos_key_t(proc, proc_op,
						 &dcp->dcp_akeys[i]);
			if (unlikely(rc))
				D_GOTO(out, rc);
		}

		break;
	}
	case DCSO_READ: {
		struct daos_cpd_read	*dcr = &dcsr->dcsr_read;

		if (dcsr->dcsr_nr == 0)
			D_GOTO(out, rc = 0);

		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(dcr->dcr_iods, dcsr->dcsr_nr);
			if (dcr->dcr_iods == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		for (i = 0; i < dcsr->dcsr_nr; i++) {
			rc = crt_proc_daos_iod_t(proc, proc_op,
						 &dcr->dcr_iods[i]);
			if (unlikely(rc))
				D_GOTO(out, rc);
		}

		break;
	}
	default:
		D_GOTO(out, rc = -DER_INVAL);
	}

out:
	if (ENCODING(proc_op) || (DECODING(proc_op) && rc == 0))
		return rc;

	switch (dcsr->dcsr_opc) {
	case DCSO_UPDATE:
		D_FREE(dcsr->dcsr_update.dcu_ec_tgts);
		D_FREE(dcsr->dcsr_update.dcu_sgls);
		break;
	case DCSO_PUNCH_OBJ:
	case DCSO_PUNCH_DKEY:
	case DCSO_PUNCH_AKEY:
		D_FREE(dcsr->dcsr_punch.dcp_akeys);
		break;
	case DCSO_READ:
		D_FREE(dcsr->dcsr_read.dcr_iods);
		break;
	}

	return rc;
}

static int
crt_proc_struct_daos_cpd_disp_ent(crt_proc_t proc, crt_proc_op_t proc_op,
				  struct daos_cpd_disp_ent *dcde)
{
	uint32_t	count;
	int		rc;

	if (FREEING(proc_op)) {
		D_FREE(dcde->dcde_reqs);
		return 0;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &dcde->dcde_read_cnt);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &dcde->dcde_write_cnt);
	if (unlikely(rc))
		return rc;

	count = dcde->dcde_read_cnt + dcde->dcde_write_cnt;
	if (count == 0)
		return 0;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(dcde->dcde_reqs, count);
		if (dcde->dcde_reqs == NULL)
			return -DER_NOMEM;
	}
	rc = crt_proc_memcpy(proc, proc_op, dcde->dcde_reqs,
			     count * sizeof(*dcde->dcde_reqs));
	if (unlikely(rc)) {
		if (DECODING(proc_op))
			D_FREE(dcde->dcde_reqs);
		return rc;
	}

	return 0;
}

static int
crt_proc_struct_daos_cpd_bulk(crt_proc_t proc, crt_proc_op_t proc_op,
			      struct daos_cpd_bulk *dcb, bool head)
{
	int	rc;

	if (head) {
		rc = crt_proc_struct_daos_cpd_sub_head(proc, proc_op, &dcb->dcb_head, false);
		if (unlikely(rc))
			return rc;
	}

	if (FREEING(proc_op)) {
		D_FREE(dcb->dcb_bulk);
		return 0;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &dcb->dcb_size);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &dcb->dcb_padding);
	if (unlikely(rc))
		return rc;

	if (DECODING(proc_op)) {
		D_ALLOC_PTR(dcb->dcb_bulk);
		if (dcb->dcb_bulk == NULL)
			return -DER_NOMEM;
	}

	rc = crt_proc_crt_bulk_t(proc, proc_op, dcb->dcb_bulk);
	if (unlikely(rc))
		return rc;

	/* The other fields will not be packed on-wire. */

	return 0;
}

static int
crt_proc_struct_daos_cpd_sg(crt_proc_t proc, crt_proc_op_t proc_op,
			    struct daos_cpd_sg *dcs)
{
	int		rc;
	int		i;

	rc = crt_proc_uint32_t(proc, proc_op, &dcs->dcs_type);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &dcs->dcs_nr);
	if (unlikely(rc))
		return rc;

	switch (dcs->dcs_type_base) {
	case DCST_HEAD: {
		struct daos_cpd_sub_head *dcsh;

		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(dcsh, dcs->dcs_nr);
			if (dcsh == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dcsh;
		} else {
			dcsh = dcs->dcs_buf;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_cpd_sub_head(proc, proc_op, &dcsh[i], true);
			if (unlikely(rc))
				D_GOTO(out, rc);
		}

		break;
	}
	case DCST_REQ_CLI:
	case DCST_REQ_SRV: {
		struct daos_cpd_sub_req	*dcsr;
		bool			 with_oid;

		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(dcsr, dcs->dcs_nr);
			if (dcsr == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dcsr;
			with_oid = true;
		} else {
			dcsr = dcs->dcs_buf;
			if (dcs->dcs_type_base == DCST_REQ_SRV)
				with_oid = true;
			else
				with_oid = false;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_cpd_sub_req(proc, proc_op,
							      &dcsr[i],
							      with_oid);
			if (unlikely(rc))
				D_GOTO(out, rc);
		}

		break;
	}
	case DCST_ENT: {
		struct daos_cpd_disp_ent	*dcde;

		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(dcde, dcs->dcs_nr);
			if (dcde == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dcde;
		} else {
			dcde = dcs->dcs_buf;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_cpd_disp_ent(proc, proc_op,
							       &dcde[i]);
			if (unlikely(rc))
				D_GOTO(out, rc);
		}

		break;
	}
	case DCST_TGT: {
		struct daos_shard_tgt		*dst;

		if (DECODING(proc_op)) {
			D_ALLOC_ARRAY(dst, dcs->dcs_nr);
			if (dst == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dst;
		} else {
			dst = dcs->dcs_buf;
		}

		for (i = 0; i < dcs->dcs_nr; i++) {
			rc = crt_proc_struct_daos_shard_tgt(proc, proc_op,
							    &dst[i]);
			if (unlikely(rc))
				D_GOTO(out, rc);
		}

		break;
	}
	case DCST_BULK_HEAD:
	case DCST_BULK_REQ:
	case DCST_BULK_ENT:
	case DCST_BULK_TGT: {
		struct daos_cpd_bulk	*dcb;

		if (DECODING(proc_op)) {
			D_ALLOC_PTR(dcb);
			if (dcb == NULL)
				D_GOTO(out, rc = -DER_NOMEM);

			dcs->dcs_buf = dcb;
		} else {
			dcb = dcs->dcs_buf;
		}

		rc = crt_proc_struct_daos_cpd_bulk(proc, proc_op, dcb,
						   dcs->dcs_type_base == DCST_BULK_HEAD);
		if (unlikely(rc))
			D_GOTO(out, rc);

		break;
	}
	default:
		return -DER_INVAL;
	}

out:
	/* XXX: There is potential memory leak for the case of CRT_PROC_DECODE
	 *	with failure. We may allocate some DRAM in some low layer proc
	 *	functions when decoding former elements.
	 *
	 *	Currently, We seems not have efficient way to release them. It
	 *	is not special for CPD related proc interfaces, instead, it is
	 *	general issue for the whole CRT proc mechanism.
	 */
	if (FREEING(proc_op) || (DECODING(proc_op) && rc != 0))
		D_FREE(dcs->dcs_buf);

	return rc;
}

CRT_RPC_DEFINE(obj_rw, DAOS_ISEQ_OBJ_RW, DAOS_OSEQ_OBJ_RW)
CRT_RPC_DEFINE(obj_key_enum, DAOS_ISEQ_OBJ_KEY_ENUM, DAOS_OSEQ_OBJ_KEY_ENUM)
CRT_RPC_DEFINE(obj_punch, DAOS_ISEQ_OBJ_PUNCH, DAOS_OSEQ_OBJ_PUNCH)
CRT_RPC_DEFINE(obj_query_key, DAOS_ISEQ_OBJ_QUERY_KEY, DAOS_OSEQ_OBJ_QUERY_KEY)
CRT_RPC_DEFINE(obj_sync, DAOS_ISEQ_OBJ_SYNC, DAOS_OSEQ_OBJ_SYNC)
CRT_RPC_DEFINE(obj_migrate, DAOS_ISEQ_OBJ_MIGRATE, DAOS_OSEQ_OBJ_MIGRATE)
CRT_RPC_DEFINE(obj_ec_agg, DAOS_ISEQ_OBJ_EC_AGG, DAOS_OSEQ_OBJ_EC_AGG)
CRT_RPC_DEFINE(obj_cpd, DAOS_ISEQ_OBJ_CPD, DAOS_OSEQ_OBJ_CPD)
CRT_RPC_DEFINE(obj_ec_rep, DAOS_ISEQ_OBJ_EC_REP, DAOS_OSEQ_OBJ_EC_REP)
CRT_RPC_DEFINE(obj_key2anchor, DAOS_ISEQ_OBJ_KEY2ANCHOR, DAOS_OSEQ_OBJ_KEY2ANCHOR)

/* Define for obj_proto_rpc_fmt[] array population below.
 * See OBJ_PROTO_*_RPC_LIST macro definition
 */
#define X(a, b, c, d, e, f)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
},

static struct crt_proto_rpc_format obj_proto_rpc_fmt[] = {
	OBJ_PROTO_CLI_RPC_LIST
};

#undef X

struct crt_proto_format obj_proto_fmt = {
	.cpf_name  = "daos-object",
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
	case DAOS_OBJ_RPC_KEY2ANCHOR:
		((struct obj_key2anchor_out *)reply)->oko_ret = status;
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
	case DAOS_OBJ_RPC_EC_AGGREGATE:
		((struct obj_ec_agg_out *)reply)->ea_status = status;
		break;
	case DAOS_OBJ_RPC_CPD:
		((struct obj_cpd_out *)reply)->oco_ret = status;
		break;
	case DAOS_OBJ_RPC_EC_REPLICATE:
		((struct obj_ec_rep_out *)reply)->er_status = status;
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
	case DAOS_OBJ_RPC_KEY2ANCHOR:
		return ((struct obj_key2anchor_out *)reply)->oko_ret;
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
	case DAOS_OBJ_RPC_EC_AGGREGATE:
		return ((struct obj_ec_agg_out *)reply)->ea_status;
	case DAOS_OBJ_RPC_CPD:
		return ((struct obj_cpd_out *)reply)->oco_ret;
	case DAOS_OBJ_RPC_EC_REPLICATE:
		return ((struct obj_ec_rep_out *)reply)->er_status;
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
		((struct obj_key_enum_out *)reply)->oeo_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_KEY2ANCHOR:
		((struct obj_key2anchor_out *)reply)->oko_map_version = map_version;
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
		((struct obj_query_key_out *)reply)->okqo_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_SYNC:
		((struct obj_sync_out *)reply)->oso_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_EC_AGGREGATE:
		((struct obj_ec_agg_out *)reply)->ea_map_ver = map_version;
		break;
	case DAOS_OBJ_RPC_CPD:
		((struct obj_cpd_out *)reply)->oco_map_version = map_version;
		break;
	case DAOS_OBJ_RPC_EC_REPLICATE:
		((struct obj_ec_rep_out *)reply)->er_map_ver = map_version;
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
	case DAOS_OBJ_RPC_KEY2ANCHOR:
		return ((struct obj_key2anchor_out *)reply)->oko_map_version;
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
	case DAOS_OBJ_RPC_CPD:
		return ((struct obj_cpd_out *)reply)->oco_map_version;
	default:
		D_ASSERT(0);
	}
	return 0;
}
