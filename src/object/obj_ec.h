/**
 * (C) Copyright 2019 Intel Corporation.
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
	uint32_t		 oiod_nr;
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
 * Splitted obj request (only used on leader shard for obj update).
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
#define obj_ec_vos_recx_idx(idx, stripe_rec_nr, e_len)			\
	((((idx) / (stripe_rec_nr)) * (e_len)) + ((idx) % (e_len)))
/** Query the original daos idx of mapped VOS index */
#define obj_ec_idx_of_vos_idx(vos_idx, stripe_rec_nr, e_len, tgt_idx)	       \
	((((vos_idx) / (e_len)) * stripe_rec_nr) + (tgt_idx) * (e_len) +       \
	 (vos_idx) % (e_len))

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
	       (recx->rx_idx % obj_ec_cell_rec_nr(oca)) +
	       (recx_end % obj_ec_cell_rec_nr(oca));
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
	if (oiod->oiod_siods != NULL)
		D_FREE(oiod->oiod_siods);
	memset(oiod, 0, sizeof(*oiod));
}

/* obj_class.c */
int obj_ec_codec_init(void);
void obj_ec_codec_fini(void);
struct obj_ec_codec *obj_ec_codec_get(daos_oclass_id_t oc_id);

/* cli_ec.c */
int obj_ec_req_reasb(daos_obj_rw_t *args, daos_obj_id_t oid,
		     struct daos_oclass_attr *oca,
		     struct obj_reasb_req *reasb_req, bool update);
void obj_ec_recxs_fini(struct obj_ec_recx_array *recxs);
void obj_ec_seg_sorter_fini(struct obj_ec_seg_sorter *sorter);
void obj_ec_tgt_oiod_fini(struct obj_tgt_oiod *tgt_oiods);
struct obj_tgt_oiod *obj_ec_tgt_oiod_init(struct obj_io_desc *r_oiods,
			uint32_t iod_nr, uint8_t *tgt_bitmap,
			uint32_t tgt_max_idx, uint32_t tgt_nr);
struct obj_tgt_oiod *obj_ec_tgt_oiod_get(struct obj_tgt_oiod *tgt_oiods,
			uint32_t tgt_nr, uint32_t tgt_idx);

/* srv_ec.c */
struct obj_rw_in;
int obj_ec_rw_req_split(struct obj_rw_in *orw, struct obj_ec_split_req **req);
void obj_ec_split_req_fini(struct obj_ec_split_req *req);

#endif /* __OBJ_EC_H__ */
