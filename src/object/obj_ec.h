/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#ifndef __OBJ_EC_H__
#define __OBJ_EC_H__

#include <daos_types.h>
#include <daos_obj.h>

#include <isa-l.h>

/** MAX number of data cells */
#define OBJ_EC_MAX_K		(64)
/** MAX number of parity cells */
#define OBJ_EC_MAX_P		(16)
#define OBJ_EC_MAX_M		(OBJ_EC_MAX_K + OBJ_EC_MAX_P)
/** Length of target bitmap */
#define OBJ_TGT_BITMAP_LEN						\
	(roundup(((OBJ_EC_MAX_M) / NBBY), 8))

/* EC parity is stored in a private address range that is selected by setting
 * the most-significant bit of the offset (an unsigned long). This effectively
 * limits the addressing of user extents to the lower 63 bits of the offset
 * range. The client stack should enforce this limitation.
 */
#define PARITY_INDICATOR (1ULL << 63)

/** EC codec for object EC encoding/decoding */
struct obj_ec_codec {
	/** encode matrix, can be used to generate decode matrix */
	unsigned char		*ec_en_matrix;
	/**
	 * GF (galois field) tables, pointer to array of input tables generated
	 * from coding coefficients. Needed for both encoding and decoding.
	 */
	unsigned char		*ec_gftbls;
};

/** Shard IO descriptor */
struct obj_shard_iod {
	/** tgt index [0, k+p) */
	uint32_t		 siod_tgt_idx;
	/** start index in extend array in daos_iod_t */
	uint32_t		 siod_idx;
	/** number of extends in extend array in daos_iod_t */
	uint32_t		 siod_nr;
	/** the byte offset of this shard's data to the sgl/bulk */
	uint64_t		 siod_off;
};

struct obj_iod_array {
	/* number of iods (oia_iods) */
	uint32_t		 oia_iod_nr;
	/* number obj iods (oia_oiods) */
	uint32_t		 oia_oiod_nr;
	daos_iod_t		*oia_iods;
	struct dcs_iod_csums	*oia_iod_csums;
	struct obj_io_desc	*oia_oiods;
	/* byte offset array for target, need this info after RPC dispatched
	 * to specific target server as there is no oiod info already.
	 * one for each iod, NULL for replica.
	 */
	uint64_t		*oia_offs;
};

/** Evenly distributed for EC full-stripe-only mode */
#define OBJ_SIOD_EVEN_DIST	((uint32_t)1 << 0)
/** Flag used only for proc func, to only proc to one specific target */
#define OBJ_SIOD_PROC_ONE	((uint32_t)1 << 1)
/** Flag of single value EC */
#define OBJ_SIOD_SINGV		((uint32_t)1 << 2)

/**
 * Object IO descriptor.
 * NULL for replica obj, as each shard/tgt with same extends in iod.
 * Non-NULL for EC obj to specify IO descriptor for different targets.
 */
struct obj_io_desc {
	/**
	 * number of shard IODs involved for this object IO.
	 * for EC obj, if there is only one target for example partial update or
	 * fetch targeted with only one shard, oiod_siods should be NULL as need
	 * not carry extra info.
	 */
	uint16_t		 oiod_nr;
	/**
	 * the target index [0, tgt_nr), only used for EC evenly distributed
	 * single value.
	 */
	uint16_t		 oiod_tgt_idx;
	/**
	 * Flags, OBJ_SIOD_EVEN_DIST is for a special case that the extends
	 * only cover full stripe(s), then each target has same number of
	 * extends in the extend array (evenly distributed).
	 */
	uint32_t		 oiod_flags;
	/** shard IOD array */
	struct obj_shard_iod	*oiod_siods;
};

/** To record the recxs in original iod which include full stripes */
struct obj_ec_recx {
	/** index of the recx in original iod::iod_recxs array */
	uint32_t		oer_idx;
	/** number of full stripes in oer_recx */
	uint32_t		oer_stripe_nr;
	/**
	 * the byte offset of the start of oer_recx, in the extends covered by
	 * iod::iod_recxs array. Can be used to find corresponding sgl offset.
	 */
	uint64_t		oer_byte_off;
	/** the extend that includes the full stripes */
	daos_recx_t		oer_recx;
};

/** To record all full stripe recxs in one iod */
struct obj_ec_recx_array {
	/** number of recxs for each tgt */
	uint32_t		*oer_tgt_recx_nrs;
	/** start recxs idx for each tgt */
	uint32_t		*oer_tgt_recx_idxs;
	/** number of data tgts and parity tgts */
	uint32_t		 oer_k;
	uint32_t		 oer_p;
	/** matched index last time, only used for ec_recx_with_full_stripe */
	uint32_t		 oer_last;
	/** parity buffer pointer array, one for each parity tgt */
	uint8_t			*oer_pbufs[OBJ_EC_MAX_P];
	/** total number of full stripes in oer_recxs array */
	uint32_t		 oer_stripe_total;
	/** number of valid items in oer_recxs array */
	uint32_t		 oer_nr;
	/** full stripe recx array */
	struct obj_ec_recx	*oer_recxs;
};

/**
 * Object target oiod/offset.
 * Only used as temporary buffer to facilitate the RPC proc.
 */
struct obj_tgt_oiod {
	/* target idx [0, k + p) */
	uint32_t		 oto_tgt_idx;
	/* number of iods */
	uint32_t		 oto_iod_nr;
	/* offset array, oto_iod_nr offsets for each target */
	uint64_t		*oto_offs;
	/* oiod array, oto_iod_nr oiods for each target,
	 * each oiod with just one siod.
	 */
	struct obj_io_desc	*oto_oiods;
};

/**
 * Split obj request (only used on leader shard for obj update).
 * For object update, client sends update request to leader, the leader need to
 * split it for different targets before dispatch.
 */
struct obj_ec_split_req {
	uint32_t		 osr_start_shard;
	/* forward targets' tgt_oiods */
	struct obj_tgt_oiod	*osr_tgt_oiods;
	/* leader shard's iods */
	daos_iod_t		*osr_iods;
	/* leader shard's offsets (one for each iod) */
	uint64_t		*osr_offs;
	/* leader shard's iod_csums */
	struct dcs_iod_csums	*osr_iod_csums;
	/* csum_info for singvs */
	struct dcs_csum_info	*osr_singv_cis;
};

/**
 * Segment sorter to sort segments per target.
 * In EC IO request reassemble, it needs to regenerate a new sgl with iovs
 * grouped by target and each target's segments need to be sorted to be same
 * order as recxs. Before the sorting it does not know each target's segment
 * number. This sorter is to facilitate the handing.
 */
struct obj_ec_seg_head {
	uint32_t		 esh_tgt_idx;
	uint32_t		 esh_seg_nr;
	uint32_t		 esh_first;
	uint32_t		 esh_last;
};

struct obj_ec_seg {
	d_iov_t			 oes_iov;
	int32_t			 oes_next;
};

struct obj_ec_seg_sorter {
	uint32_t		 ess_seg_nr;
	uint32_t		 ess_seg_nr_total;
	uint32_t		 ess_tgt_nr;
	uint32_t		 ess_tgt_nr_total;
	struct obj_ec_seg_head	*ess_tgts;
	struct obj_ec_seg	*ess_segs;
};
#define OBJ_EC_SEG_NIL		 (-1)

/** ISAL codec for EC data recovery */
struct obj_ec_recov_codec {
	unsigned char		*er_gftbls;	/* GF tables */
	unsigned char		*er_de_matrix;	/* decode matrix */
	unsigned char		*er_inv_matrix;	/* invert matrix */
	unsigned char		*er_b_matrix;	/* temporary b matrix */
	uint32_t		*er_dec_idx;	/* decode index */
	uint32_t		*er_err_list;	/* target idx list in error */
	bool			*er_in_err;	/* boolean array for targets */
	uint32_t		 er_nerrs;	/* #targets in error */
	uint32_t		 er_data_nerrs; /* #data-targets in error */
};

/* EC recovery task */
struct obj_ec_recov_task {
	daos_iod_t		ert_iod;
	d_sg_list_t		ert_sgl;
	daos_epoch_t		ert_epoch;
	daos_handle_t		ert_th; /* read-only tx handle */
};

/** EC obj IO failure information */
struct obj_ec_fail_info {
	/* the original user iods pointer, used for the case that in singv
	 * degraded fetch, set the iod_size.
	 */
	daos_iod_t			*efi_uiods;
	/* missed (to be recovered) recx list */
	struct daos_recx_ep_list	*efi_recx_lists;
	/* list of error targets */
	uint32_t			*efi_tgt_list;
	/* number of lists in efi_recx_lists/efi_stripe_lists, equal to #iods */
	uint32_t			 efi_nrecx_lists;
	/* number of error targets */
	uint32_t			 efi_ntgts;
	struct obj_ec_recov_codec	*efi_recov_codec;
	/* to be recovered full-stripe list */
	struct daos_recx_ep_list	*efi_stripe_lists;
	/* The buffer for all the full-stripes in efi_stripe_lists.
	 * One iov for each recx_ep (with 1 or more stripes), for each stripe
	 * it contains ((k + p) * cell_byte_size) memory.
	 */
	d_sg_list_t			*efi_stripe_sgls;
	/* For each daos_recx_ep in efi_stripe_lists will create one recovery
	 * task to fetch the data from servers.
	 */
	struct obj_ec_recov_task	*efi_recov_tasks;
	uint32_t			 efi_recov_ntasks;
};

struct obj_reasb_req;

/** Query the number of records in EC full stripe */
#define obj_ec_stripe_rec_nr(oca)					\
	((oca)->u.ec.e_k * (oca)->u.ec.e_len)
/** Query the number of records in one EC cell/target */
#define obj_ec_cell_rec_nr(oca)						\
	((oca)->u.ec.e_len)
/** Query the number of targets of EC obj class */
#define obj_ec_tgt_nr(oca)						\
	((oca)->u.ec.e_k + (oca)->u.ec.e_p)
/** Query the number of data targets of EC obj class */
#define obj_ec_data_tgt_nr(oca)						\
	((oca)->u.ec.e_k)
/** Query the number of parity targets of EC obj class */
#define obj_ec_parity_tgt_nr(oca)					\
	((oca)->u.ec.e_p)
/** Query the number of bytes in EC cell */
#define obj_ec_cell_bytes(iod, oca)					\
	(((oca)->u.ec.e_len) * ((iod)->iod_size))
/** Query the tgt idx of data cell for daos recx idx */
#define obj_ec_tgt_of_recx_idx(idx, stripe_rec_nr, e_len)		\
	(((idx) % (stripe_rec_nr)) / (e_len))
/**
 * Query the mapped VOS recx idx on data cells of daos recx idx, it is also the
 * parity's VOS recx idx on parity cells (the difference is parity's VOS recx
 * idx with highest bit set, see PARITY_INDICATOR.
 * Note that for replicated data on parity cells the VOS idx is unmapped
 * original daos recx idx to facilitate aggregation.
 */
#define obj_ec_idx_daos2vos(idx, stripe_rec_nr, e_len)			\
	((((idx) / (stripe_rec_nr)) * (e_len)) + ((idx) % (e_len)))
/** Query the original daos idx of mapped VOS index */
#define obj_ec_idx_vos2daos(vos_idx, stripe_rec_nr, e_len, tgt_idx)	       \
	((((vos_idx) / (e_len)) * stripe_rec_nr) + (tgt_idx) * (e_len) +       \
	 (vos_idx) % (e_len))

#define obj_ec_idx_parity2daos(vos_off, e_len, stripe_rec_nr)		\
	(((vos_off) / e_len) * stripe_rec_nr)

/**
 * Threshold size of EC single-value layout (even distribution).
 * When record_size <= OBJ_EC_SINGV_EVENDIST_SZ then stored in one data
 * target, or will evenly distributed to all data targets.
 */
#define OBJ_EC_SINGV_EVENDIST_SZ(data_tgt_nr)	(((data_tgt_nr) / 8 + 1) * 4096)
/** Alignment size of sing value local size */
#define OBJ_EC_SINGV_CELL_ALIGN			(8)

/** Local rec size, padding bytes and offset in the global record */
struct obj_ec_singv_local {
	uint64_t	esl_off;
	uint64_t	esl_size;
	uint32_t	esl_bytes_pad;
};

/** Query the target index for small sing-value record */
#define obj_ec_singv_small_idx(oca, iod)	(0)

/** Query if the single value record is stored in one data target */
static inline bool
obj_ec_singv_one_tgt(daos_iod_t *iod, d_sg_list_t *sgl,
		     struct daos_oclass_attr *oca)
{
	uint64_t size = OBJ_EC_SINGV_EVENDIST_SZ(obj_ec_data_tgt_nr(oca));

	if ((iod->iod_size != DAOS_REC_ANY && iod->iod_size <= size) ||
	    (sgl != NULL && daos_sgl_buf_size(sgl) <= size))
		return true;

	return false;
}

/* Query the cell size (#bytes) of evenly distributed singv */
static inline uint64_t
obj_ec_singv_cell_bytes(uint64_t rec_gsize, struct daos_oclass_attr *oca)
{
	uint32_t	data_tgt_nr = obj_ec_data_tgt_nr(oca);
	uint64_t	cell_size;

	cell_size = rec_gsize / data_tgt_nr;
	if ((rec_gsize % data_tgt_nr) != 0)
		cell_size++;
	cell_size = roundup(cell_size, OBJ_EC_SINGV_CELL_ALIGN);

	return cell_size;
}

/** Query local record size and needed padding for evenly distributed singv */
static inline void
obj_ec_singv_local_sz(uint64_t rec_gsize, struct daos_oclass_attr *oca,
		      uint32_t tgt_idx, struct obj_ec_singv_local *loc)
{
	uint32_t	data_tgt_nr = obj_ec_data_tgt_nr(oca);
	uint64_t	cell_size;

	D_ASSERT(tgt_idx < obj_ec_tgt_nr(oca));

	cell_size = obj_ec_singv_cell_bytes(rec_gsize, oca);
	if (tgt_idx >= data_tgt_nr)
		loc->esl_off = rec_gsize + (tgt_idx - data_tgt_nr) * cell_size;
	else
		loc->esl_off = tgt_idx * cell_size;
	if (tgt_idx == data_tgt_nr - 1) {
		/* the last data target possibly with less size and padding */
		loc->esl_size = rec_gsize - (data_tgt_nr - 1) * cell_size;
		loc->esl_bytes_pad = cell_size - loc->esl_size;
	} else {
		loc->esl_size = cell_size;
		loc->esl_bytes_pad = 0;
	}
}

/** Query the number of data cells the recx covers */
static inline uint32_t
obj_ec_recx_cell_nr(daos_recx_t *recx, struct daos_oclass_attr *oca)
{
	uint64_t	recx_end, start, end;

	recx_end = recx->rx_idx + recx->rx_nr;
	start = roundup(recx->rx_idx, obj_ec_cell_rec_nr(oca));
	end = rounddown(recx_end, obj_ec_cell_rec_nr(oca));
	if (start > end)
		return 1;
	return (end - start) / obj_ec_cell_rec_nr(oca) +
	       ((recx->rx_idx % obj_ec_cell_rec_nr(oca)) != 0) +
	       ((recx_end % obj_ec_cell_rec_nr(oca)) != 0);
}

static inline int
obj_io_desc_init(struct obj_io_desc *oiod, uint32_t tgt_nr, uint32_t flags)
{
#if 0
	/* XXX refine it later */
	if (tgt_nr < 2 || flags == OBJ_SIOD_EVEN_DIST) {
		oiod->oiod_siods = NULL;
		oiod->oiod_nr = tgt_nr;
		oiod->oiod_flags = flags;
		return 0;
	}
#endif
	if ((flags & OBJ_SIOD_SINGV) == 0) {
		D_ALLOC_ARRAY(oiod->oiod_siods, tgt_nr);
		if (oiod->oiod_siods == NULL)
			return -DER_NOMEM;
	}
	oiod->oiod_flags = flags;
	oiod->oiod_nr = tgt_nr;
	return 0;
}

static inline void
obj_io_desc_fini(struct obj_io_desc *oiod)
{
	if (oiod != NULL) {
		if (oiod->oiod_siods != NULL)
			D_FREE(oiod->oiod_siods);
		memset(oiod, 0, sizeof(*oiod));
	}
}

/* translate the queried VOS shadow list to daos extents */
static inline void
obj_shadow_list_vos2daos(uint32_t nr, struct daos_recx_ep_list *lists,
			 struct daos_oclass_attr *oca)
{
	struct daos_recx_ep_list	*list;
	daos_recx_t			*recx;
	uint64_t			 stripe_rec_nr =
						obj_ec_stripe_rec_nr(oca);
	uint64_t			 cell_rec_nr =
						obj_ec_cell_rec_nr(oca);
	uint32_t			 i, j, stripe_nr;

	if (lists == NULL)
		return;
	for (i = 0; i < nr; i++) {
		list = &lists[i];
		for (j = 0; j < list->re_nr; j++) {
			recx = &list->re_items[j].re_recx;
			recx->rx_idx = rounddown(recx->rx_idx, cell_rec_nr);
			stripe_nr = roundup(recx->rx_nr, cell_rec_nr) /
				    cell_rec_nr;
			D_ASSERT((recx->rx_idx & PARITY_INDICATOR) != 0);
			recx->rx_idx &= ~PARITY_INDICATOR;
			recx->rx_idx = obj_ec_idx_vos2daos(recx->rx_idx,
						stripe_rec_nr, cell_rec_nr,
						0);
			recx->rx_nr = stripe_rec_nr * stripe_nr;
		}
	}
}

/* Break iod's recxs on cell_size boundary, for the use case that translate
 * mapped VOS extend to original daos extent - one mapped VOS extend possibly
 * corresponds to multiple original dis-continuous daos extents.
 */
static inline int
obj_iod_break(daos_iod_t *iod, struct daos_oclass_attr *oca)
{
	daos_recx_t	*recx, *new_recx;
	uint64_t	 cell_size = obj_ec_cell_rec_nr(oca);
	uint64_t	 rec_nr;
	uint32_t	 i, j, stripe_nr;

	for (i = 0; i < iod->iod_nr; i++) {
		recx = &iod->iod_recxs[i];
		stripe_nr = obj_ec_recx_cell_nr(recx, oca);
		D_ASSERT(stripe_nr >= 1);
		if (stripe_nr == 1)
			continue;
		D_ALLOC_ARRAY(new_recx, stripe_nr + iod->iod_nr - 1);
		if (new_recx == NULL)
			return -DER_NOMEM;
		for (j = 0; j < i; j++)
			new_recx[j] = iod->iod_recxs[j];
		rec_nr = recx->rx_nr;
		for (j = 0; j < stripe_nr; j++) {
			if (j == 0) {
				new_recx[i].rx_idx = recx->rx_idx;
				new_recx[i].rx_nr = cell_size -
						    (recx->rx_idx % cell_size);
				rec_nr -= new_recx[i].rx_nr;
			} else {
				new_recx[i + j].rx_idx =
					new_recx[i + j - 1].rx_idx +
					new_recx[i + j - 1].rx_nr;
				D_ASSERT(new_recx[i + j].rx_idx % cell_size ==
					 0);
				if (j == stripe_nr - 1) {
					new_recx[i + j].rx_nr = rec_nr;
				} else {
					new_recx[i + j].rx_nr = cell_size;
					rec_nr -= cell_size;
				}
			}
		}
		for (j = i + 1; j < iod->iod_nr; j++)
			new_recx[j + stripe_nr - 1] = iod->iod_recxs[j];
		i += (stripe_nr - 1);
		iod->iod_nr += (stripe_nr - 1);
		D_FREE(iod->iod_recxs);
		iod->iod_recxs = new_recx;
	}

	return 0;
}

/* translate iod's recxs from mapped VOS extend to unmapped daos extents */
static inline int
obj_iod_recx_vos2daos(uint32_t iod_nr, daos_iod_t *iods, uint32_t tgt_idx,
		     struct daos_oclass_attr *oca)
{
	daos_iod_t	*iod;
	daos_recx_t	*recx;
	uint64_t	 stripe_rec_nr = obj_ec_stripe_rec_nr(oca);
	uint64_t	 cell_rec_nr = obj_ec_cell_rec_nr(oca);
	uint32_t	 i, j;
	int		 rc;

	for (i = 0; i < iod_nr; i++) {
		iod = &iods[i];
		if (iod->iod_type == DAOS_IOD_SINGLE)
			continue;

		rc = obj_iod_break(iod, oca);
		if (rc != 0) {
			D_ERROR("obj_iod_break failed, "DF_RC"\n", DP_RC(rc));
			return rc;
		}
		for (j = 0; j < iod->iod_nr; j++) {
			recx = &iod->iod_recxs[j];
			D_ASSERT((recx->rx_idx & PARITY_INDICATOR) == 0);
			recx->rx_idx = obj_ec_idx_vos2daos(recx->rx_idx,
						stripe_rec_nr, cell_rec_nr,
						tgt_idx);
		}
	}

	return 0;
}

static inline void
obj_iod_idx_vos2parity(uint32_t iod_nr, daos_iod_t *iods)
{
	daos_iod_t	*iod;
	daos_recx_t	*recx;
	uint32_t	 i, j;

	for (i = 0; i < iod_nr; i++) {
		iod = &iods[i];
		if (iod->iod_type == DAOS_IOD_SINGLE)
			continue;
		for (j = 0; j < iod->iod_nr; j++) {
			recx = &iod->iod_recxs[j];
			D_ASSERT((recx->rx_idx & PARITY_INDICATOR) == 0);
			recx->rx_idx |= PARITY_INDICATOR;
		}
	}
}

static inline void
obj_iod_idx_parity2vos(uint32_t iod_nr, daos_iod_t *iods)
{
	daos_iod_t	*iod;
	daos_recx_t	*recx;
	uint32_t	 i, j;

	for (i = 0; i < iod_nr; i++) {
		iod = &iods[i];
		if (iod->iod_type == DAOS_IOD_SINGLE)
			continue;
		for (j = 0; j < iod->iod_nr; j++) {
			recx = &iod->iod_recxs[j];
			D_ASSERT((recx->rx_idx & PARITY_INDICATOR) != 0);
			recx->rx_idx &= ~PARITY_INDICATOR;
		}
	}
}

static inline bool
obj_ec_tgt_in_err(uint32_t *err_list, uint32_t nerrs, uint16_t tgt_idx)
{
	uint32_t	i;

	for (i = 0; i < nerrs; i++) {
		if (err_list[i] == tgt_idx)
			return true;
	}
	return false;
}

static inline bool
obj_shard_is_ec_parity(daos_unit_oid_t oid, struct daos_oclass_attr **p_attr)
{
	struct daos_oclass_attr *attr;
	bool is_ec;

	is_ec = daos_oclass_is_ec(oid.id_pub, &attr);
	if (p_attr != NULL)
		*p_attr = attr;
	if (!is_ec)
		return false;

	if ((oid.id_shard % obj_ec_tgt_nr(attr)) < obj_ec_data_tgt_nr(attr))
		return false;

	return true;
}

/* obj_class.c */
int obj_ec_codec_init(void);
void obj_ec_codec_fini(void);
struct obj_ec_codec *obj_ec_codec_get(daos_oclass_id_t oc_id);

/* cli_ec.c */
int obj_ec_req_reasb(daos_iod_t *iods, d_sg_list_t *sgls, daos_obj_id_t oid,
		     struct daos_oclass_attr *oca,
		     struct obj_reasb_req *reasb_req,
		     uint32_t iod_nr, bool update);
void obj_ec_recxs_fini(struct obj_ec_recx_array *recxs);
void obj_ec_seg_sorter_fini(struct obj_ec_seg_sorter *sorter);
void obj_ec_tgt_oiod_fini(struct obj_tgt_oiod *tgt_oiods);
struct obj_tgt_oiod *obj_ec_tgt_oiod_init(struct obj_io_desc *r_oiods,
			uint32_t iod_nr, uint8_t *tgt_bitmap,
			uint32_t tgt_max_idx, uint32_t tgt_nr);
struct obj_tgt_oiod *obj_ec_tgt_oiod_get(struct obj_tgt_oiod *tgt_oiods,
			uint32_t tgt_nr, uint32_t tgt_idx);
void obj_ec_fetch_set_sgl(struct obj_reasb_req *reasb_req, uint32_t iod_nr);
int obj_ec_recov_add(struct obj_reasb_req *reasb_req,
		     struct daos_recx_ep_list *recx_lists, unsigned int nr);
struct obj_ec_fail_info *obj_ec_fail_info_get(struct obj_reasb_req *reasb_req,
					      bool create, uint16_t p);
void obj_ec_fail_info_reset(struct obj_reasb_req *reasb_req);
void obj_ec_fail_info_free(struct obj_reasb_req *reasb_req);
int obj_ec_recov_prep(struct obj_reasb_req *reasb_req, daos_obj_id_t oid,
		      daos_iod_t *iods, uint32_t iod_nr);
void obj_ec_recov_data(struct obj_reasb_req *reasb_req, daos_obj_id_t oid,
		       uint32_t iod_nr);
int obj_ec_get_degrade(struct obj_reasb_req *reasb_req, uint16_t fail_tgt_idx,
		       uint32_t *parity_tgt_idx, bool ignore_fail_tgt_idx);

/* srv_ec.c */
struct obj_rw_in;
int obj_ec_rw_req_split(daos_unit_oid_t oid, struct obj_iod_array *iod_array,
			uint32_t iod_nr, uint32_t start_shard,
			void *tgt_map, uint32_t map_size,
			uint32_t tgt_nr, struct daos_shard_tgt *tgts,
			struct obj_ec_split_req **split_req);
void obj_ec_split_req_fini(struct obj_ec_split_req *req);

#endif /* __OBJ_EC_H__ */
