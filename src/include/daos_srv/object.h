/*
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_obj: Object Server API
 */

#ifndef __DAOS_SRV_OBJ_H__
#define __DAOS_SRV_OBJ_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos_srv/dtx_srv.h>

typedef int (*iter_copy_data_cb_t)(daos_handle_t ih, vos_iter_entry_t *it_entry, d_iov_t *iov_out);

struct ds_obj_enum_arg {
	daos_epoch_range_t  *eprs;
	struct daos_csummer *csummer;
	int                  eprs_cap;
	int                  eprs_len;
	int                  last_type; /* hack for tweaking kds_len */
	iter_copy_data_cb_t  copy_data_cb;
	/* Buffer fields */
	union {
		struct { /* !fill_recxs */
			daos_key_desc_t *kds;
			int              kds_cap;
			int              kds_len;
			d_sg_list_t     *sgl;
			d_iov_t          csum_iov;
			uint32_t         ec_cell_sz;
			int              sgl_idx;
		};
		struct { /* fill_recxs && type == S||R */
			daos_recx_t *recxs;
			int          recxs_cap;
			int          recxs_len;
		};
	};
	daos_size_t     inline_thres;        /* type == S||R || chk_key2big*/
	int             rnum;                /* records num (type == S||R) */
	daos_size_t     rsize;               /* record size (type == S||R) */
	daos_unit_oid_t oid;                 /* for unpack */
	uint32_t        fill_recxs : 1,      /* type == S||R */
	    chk_key2big : 1, need_punch : 1, /* need to pack punch epoch */
	    obj_punched : 1,                 /* object punch is packed   */
	    size_query  : 1;                 /* Only query size */
};

struct dtx_handle;
typedef int (*enum_iterate_cb_t)(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
				 struct vos_iter_anchors *anchors, vos_iter_cb_t pre_cb,
				 vos_iter_cb_t post_cb, void *arg, struct dtx_handle *dth);

int
ds_obj_enum_pack(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
		 struct vos_iter_anchors *anchors, struct ds_obj_enum_arg *arg,
		 enum_iterate_cb_t iter_cb, struct dtx_handle *dth);

#endif /* __DAOS_SRV_OBJ_H__ */
