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
/**
 * DAOS enumeration pack/unpack interfaces
 */

#ifndef __DAOS_ENUM_H__
#define __DAOS_ENUM_H__

#include <daos_types.h>
#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/dtx_srv.h>

/** Maximal number of iods (i.e., akeys) in daos_enum_unpack_io.ui_iods */
#define DAOS_ENUM_UNPACK_MAX_IODS 16

typedef int (*daos_enum_copy_cb_t)(daos_handle_t ih,
				   vos_iter_entry_t *it_entry,
				   d_iov_t *iov_out);

struct daos_enum_arg {
	bool			fill_recxs;	/* type == S||R */
	bool			chk_key2big;
	daos_epoch_range_t     *eprs;
	int			eprs_cap;
	int			eprs_len;
	int			last_type;	/* hack for tweaking kds_len */

	/* Buffer fields */
	union {
		struct {	/* !fill_recxs */
			daos_key_desc_t	       *kds;
			int			kds_cap;
			int			kds_len;
			d_sg_list_t	       *sgl;
			int			sgl_idx;
		};
		struct {	/* fill_recxs && type == S||R */
			daos_recx_t	       *recxs;
			int			recxs_cap;
			int			recxs_len;
		};
	};
	daos_size_t		inline_thres;	/* type == S||R || chk_key2big*/
	int			rnum;		/* records num (type == S||R) */
	daos_size_t		rsize;		/* record size (type == S||R) */
	daos_unit_oid_t		oid;		/* for unpack */
	daos_enum_copy_cb_t	copy_cb;	/* data copy callback */
};

/**
 * Used by daos_enum_unpack to accumulate recxs that can be stored with a single
 * VOS update.
 *
 * ui_oid and ui_dkey are only filled by daos_enum_unpack for certain
 * enumeration types, as commented after each field. Callers may fill ui_oid,
 * for instance, when the enumeration type is VOS_ITER_DKEY, to pass the object
 * ID to the callback.
 *
 * ui_iods, ui_recxs_caps, and ui_sgls are arrays of the same capacity
 * (ui_iods_cap) and length (ui_iods_len). That is, the iod in ui_iods[i] can
 * hold at most ui_recxs_caps[i] recxs, which have their inline data described
 * by ui_sgls[i]. ui_sgls is optional. If ui_iods[i].iod_recxs[j] has no inline
 * data, then ui_sgls[i].sg_iovs[j] will be empty.
 */
struct daos_enum_unpack_io {
	daos_unit_oid_t	 ui_oid;	/**< type <= OBJ */
	daos_key_t	 ui_dkey;	/**< type <= DKEY */
	daos_iod_t	*ui_iods;
	int		 ui_iods_cap;
	int		 ui_iods_len;
	int		*ui_recxs_caps;
	daos_epoch_t	 ui_dkey_eph;
	daos_epoch_t	*ui_akey_ephs;
	d_sg_list_t	*ui_sgls;	/**< optional */
	uint32_t	 ui_version;
};

typedef int (*daos_obj_list_obj_cb_t)(daos_handle_t oh, daos_epoch_t *epoch,
				      daos_key_t *dkey, daos_key_t *akey,
				      daos_size_t *size, uint32_t *nr,
				      daos_key_desc_t *kds,
				      daos_epoch_range_t *eprs,
				      d_sg_list_t *sgl, daos_anchor_t *anchor,
				      daos_anchor_t *dkey_anchor,
				      daos_anchor_t *akey_anchor);

typedef int (*daos_enum_unpack_cb_t)(struct daos_enum_unpack_io *io, void *arg);

int daos_enum_pack(vos_iter_param_t *param, vos_iter_type_t type,
		  bool recursive, struct vos_iter_anchors *anchors,
		  struct daos_enum_arg *arg);

int daos_enum_unpack(vos_iter_type_t type, struct daos_enum_arg *arg,
		    daos_enum_unpack_cb_t cb, void *cb_arg);

int daos_enum_dkeys(daos_handle_t oh, daos_unit_oid_t oid, daos_epoch_t epoch,
		    daos_obj_list_obj_cb_t list_cb,
		    daos_enum_unpack_cb_t unpack_cb, void *arg);

#endif /* __DAOS_ENUM_H__ */
