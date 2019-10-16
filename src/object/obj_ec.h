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

/**
 * Flag to differentiate EC cell size defined as number of
 * records (== 1) or bytes (== 0).
 */
#define OBJ_EC_CELL_ON_RECORD	(0)
/** MAX number of data cells */
#define OBJ_EC_MAX_K		(48)
/** MAX number of parity cells */
/** if (k + p) exceed 64, need to change tgt_set related code */
#define OBJ_EC_MAX_P		(16)

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

/**
 * To record the recxs in original iod which include full stripes,
 * size 8B aligned as will be used in RPC request.
 */
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
	/** checksum associate with the checksum (TODO) */
	daos_csum_buf_t		oer_csum;
};

/** To record all full stripe recxs in one iod, size 8B aligned. */
struct obj_ec_recx_array {
	/* number of valid items in oer_recxs array */
	uint32_t		 oer_nr;
	/* total number of full stripes in oer_recxs array */
	uint32_t		 oer_stripe_total;
	struct obj_ec_recx	*oer_recxs;
};

/**
 * EC parity code structure for all iods, each field is an array pointer that
 * with daos_obj_rw_t::nr elements.
 */
struct obj_ec_pcode {
	struct obj_ec_recx_array	*oep_ec_recxs;
	crt_bulk_t			*oep_bulks;
	d_sg_list_t			*oep_sgls;
};

static inline void
obj_ec_recx_array_dump(struct obj_ec_recx_array *ac_rexc_array)
{
	struct obj_ec_recx	*ec_recx;
	int			 i;

	D_PRINT("obj_ec_recx_array %p:\n", ac_rexc_array);
	D_PRINT("oer_nr %d\n", ac_rexc_array->oer_nr);
	D_PRINT("oer_stripe_total %d\n", ac_rexc_array->oer_stripe_total);
	for (i = 0; i < ac_rexc_array->oer_nr; i++) {
		ec_recx = &ac_rexc_array->oer_recxs[i];
		D_PRINT("oer_recxs: [%3d] - idx %d, stripe_nr %d, byte_off %d, "
			"recx_idx %d, recx_nr %d.\n", i, ec_recx->oer_idx,
			ec_recx->oer_stripe_nr, (int)ec_recx->oer_byte_off,
			(int)ec_recx->oer_recx.rx_idx,
			(int)ec_recx->oer_recx.rx_nr);
	}
}

#if OBJ_EC_CELL_ON_RECORD
/** Query the number of records in EC full stripe */
#define obj_ec_stripe_rec_nr(iod, oca)					\
	((oca)->u.ec.e_k * (oca)->u.ec.e_len)
/** Query the number of bytes in EC cell */
#define obj_ec_cell_bytes(iod, oca)					\
	(((oca)->u.ec.e_len) * ((iod)->iod_size))
#else
#define obj_ec_stripe_rec_nr(iod, oca)					\
	(((oca)->u.ec.e_k * (oca)->u.ec.e_len) / ((iod)->iod_size))
#define obj_ec_cell_bytes(iod, oca)					\
	((oca)->u.ec.e_len)
#endif

/* obj_class.c */
int obj_ec_codec_init(void);
void obj_ec_codec_fini(void);
struct obj_ec_codec *obj_ec_codec_get(daos_oclass_id_t oc_id);

/* cli_ec.c */
int obj_ec_recxs_init(struct obj_ec_recx_array *recxs, uint32_t recx_nr);
void obj_ec_recxs_fini(struct obj_ec_recx_array *recxs);
void obj_ec_pcode_fini(struct obj_ec_pcode *pcode, uint32_t nr);
int obj_ec_update_encode(daos_obj_update_t *args, daos_obj_id_t oid,
			 struct daos_oclass_attr *oca,
			 struct obj_ec_pcode *pcode, uint64_t *tgt_set);

#endif /* __OBJ_EC_H__ */
