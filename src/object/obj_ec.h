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
	(roundup(((OBJ_EC_MAX_M) / NBBY), 4))

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
#define obj_ec_cell_rec_nr(oca)					\
	((oca)->u.ec.e_len)
/** Query the number of targets of EC obj class */
#define obj_ec_tgt_nr(oca)					\
	((oca)->u.ec.e_k + (oca)->u.ec.e_p)
/** Query the number of data targets of EC obj class */
#define obj_ec_data_tgt_nr(oca)					\
	((oca)->u.ec.e_k)
/** Query the number of parity targets of EC obj class */
#define obj_ec_parity_tgt_nr(oca)					\
	((oca)->u.ec.e_p)
/** Query the number of bytes in EC cell */
#define obj_ec_cell_bytes(iod, oca)					\
	(((oca)->u.ec.e_len) * ((iod)->iod_size))
/** Query the number of data cells the recx covers */
#define obj_ec_recx_cell_nr(recx, oca)					\
	((((recx)->rx_nr) / ((oca)->u.ec.e_len)) +			\
	 (((recx)->rx_idx) % ((oca)->u.ec.e_len)))
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

/* obj_class.c */
int obj_ec_codec_init(void);
void obj_ec_codec_fini(void);
struct obj_ec_codec *obj_ec_codec_get(daos_oclass_id_t oc_id);

/* cli_ec.c */
int obj_ec_req_reassemb(daos_obj_rw_t *args, daos_obj_id_t oid,
			struct daos_oclass_attr *oca,
			struct obj_reasb_req *reasb_req);
void obj_ec_recxs_fini(struct obj_ec_recx_array *recxs);
void obj_ec_seg_sorter_fini(struct obj_ec_seg_sorter *sorter);

#endif /* __OBJ_EC_H__ */
