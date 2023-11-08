/**
 * (C) Copyright 2023-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos
 *
 * vos/vos_obj_flat.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/checksum.h>
#include <daos_types.h>
#include <daos_srv/vos.h>

#include "vos_internal.h"

#define VOF_MAX_DKEY_PER_OBJ	(1024)
#define VOF_MAX_AKEY_PER_DKEY	(1024)
#define VOF_MAX_EXT_PER_AKEY	(1024)
#define VOF_MAX_VAL_LEN		(UINT32_MAX)	/* Maximum length of one value (singv/array) */
#define VOF_MAX_TOTAL_LEN	(1U << 20)	/* Maximum length of total flatten object */

struct vof_iter_arg {
	daos_handle_t		via_coh;
	daos_epoch_t		via_flat_epoch;
	uint64_t		*via_snapshots;
	uint32_t		via_snap_nr;

	daos_unit_oid_t		via_oid;
	daos_epoch_range_t	via_epr;		/* epoch range of the object */
	uint32_t		via_dkey_nr;		/* total number of dkeys */
	uint32_t		via_akey_nr;		/* total number of akeys */
	uint32_t		via_singv_nr;		/* total number of singv */
	uint32_t		via_ext_nr;		/* total number of exts */
	uint32_t		via_dkey_total_len;	/* total length of all dkeys */
	uint32_t		via_dkey_inline_len;	/* total length of inline dkeys */
	uint32_t		via_akey_total_len;	/* total length of all akeys */
	uint32_t		via_akey_inline_len;	/* total length of inline akeys */
	uint32_t		via_val_total_len;	/* total length of all values */
	uint32_t		via_val_inline_len;	/* total length of inline values */
	uint32_t		via_curr_akey_nr;	/* number of akeys under current dkey */
	uint32_t		via_curr_ext_nr;	/* number of exts under current akey */
	uint32_t		via_item_nr;		/* total number of keys/values */
	uint32_t		via_key_val_len;	/* total length of keys/values */

	int32_t			via_dkey_idx;
	int32_t			via_akey_idx;
	int32_t			via_singv_idx;
	int32_t			via_ext_idx;
	uint32_t		via_dkey_off;
	uint32_t		via_akey_off;
	uint32_t		via_val_off;

	struct umem_rsrvd_act	*via_rsrvd_act;
	umem_off_t		via_umoff;

	struct vos_obj_flat_df	*via_df;
	struct vof_item_df	*via_dkeys;
	struct vof_item_df	*via_akeys;
	struct vof_item_df	*via_singvs;
	struct vof_item_df	*via_exts;
	uint8_t			*via_dkey_ptr;
	uint8_t			*via_akey_ptr;
	uint8_t			*via_val_ptr;
	uint32_t		via_df_len;
	uint32_t		via_size_exceed:1,
				via_cross_snap:1,
				via_published:1;
};

static uint8_t *
vof_key_ptr(struct vos_obj_flat_df *df, struct vof_item_df *key_it)
{
	if (key_it->vi_size > VOF_KEY_INLINE_SZ)
		return (df->ofd_payload + key_it->vi_key_off);
	else
		return key_it->vi_key;
}

static int
vof_key_cmp_rc(int rc)
{
	if (rc == 0)
		return 0;
	else if (rc < 0)
		return -1;
	else
		return 1;
}

static int
vof_item_sort_op_cmp(void *array, int a, int b)
{
	struct vos_obj_flat_df	*df = array;
	struct vof_item_df	*items = (struct vof_item_df *)df->ofd_payload;
	struct vof_item_df	*ita, *itb;
	int			 cmp_rc;

	D_ASSERTF(a >= 0 && a < df->ofd_item_nr, "a %d, item_nr %d\n", a, df->ofd_item_nr);
	D_ASSERTF(b >= 0 && b < df->ofd_item_nr, "b %d, item_nr %d\n", b, df->ofd_item_nr);
	ita = items + a;
	itb = items + b;
	D_ASSERTF(ita->vi_type == itb->vi_type, "%d != %d\n", ita->vi_type, itb->vi_type);
	switch (ita->vi_type) {
	case VOF_DKEY:
	case VOF_AKEY:
		if (ita->vi_size < itb->vi_size)
			return -1;
		if (ita->vi_size > itb->vi_size)
			return 1;
		cmp_rc = memcmp(vof_key_ptr(df, ita), vof_key_ptr(df, itb), ita->vi_size);
		if (unlikely(cmp_rc == 0)) {
			daos_key_t tmp_key;

			d_iov_set(&tmp_key, vof_key_ptr(df, ita), ita->vi_size);
			D_ERROR("same key=" DF_KEY ", a %d b %d\n", DP_KEY(&tmp_key), a, b);
		}
		return vof_key_cmp_rc(cmp_rc);
	case VOF_EXT:
		D_ASSERTF(ita->vi_ext_idx != itb->vi_ext_idx, "ext_idx "DF_U64, ita->vi_ext_idx);
		if (ita->vi_ext_idx < itb->vi_ext_idx)
			return -1;
		return 1;
	default:
		D_ASSERTF(0, "bad type %d\n", ita->vi_type);
		break;
	};

	D_ASSERTF(0, "should not get here\n");
	return 0;
}

static void
vof_item_sort_op_swap(void *array, int a, int b)
{
	struct vos_obj_flat_df	*df = array;
	struct vof_item_df	*items = (struct vof_item_df *)df->ofd_payload;
	struct vof_item_df	*ita, *itb;
	struct vof_item_df	 tmp;

	ita = items + a;
	itb = items + b;

	memcpy(&tmp, ita, sizeof(tmp));
	memcpy(ita, itb, sizeof(tmp));
	memcpy(itb, &tmp, sizeof(tmp));
}

static daos_sort_ops_t vof_item_sort_ops = {
	.so_swap	= vof_item_sort_op_swap,
	.so_cmp		= vof_item_sort_op_cmp,
};

static void
vof_fill_dkey(struct vof_iter_arg *arg, vos_iter_entry_t *ent)
{
	struct vof_item_df	*item;

	arg->via_dkey_idx++;
	D_ASSERTF(arg->via_dkey_idx < arg->via_dkey_nr, "%d >= %d\n",
		  arg->via_dkey_idx, arg->via_dkey_nr);
	item = &arg->via_dkeys[arg->via_dkey_idx];
	item->vi_type = VOF_DKEY;
	item->vi_size = ent->ie_key.iov_len;
	item->vi_child_idx = arg->via_dkey_nr + arg->via_akey_idx + 1;
	D_ASSERTF(item->vi_child_idx < arg->via_dkey_nr + arg->via_akey_nr, "%d >= %d\n",
		  item->vi_child_idx, arg->via_dkey_nr + arg->via_akey_nr);
	/* vi_child_nr will be updated when fill_akey */
	item->vi_child_type = VOF_AKEY;
	if (item->vi_size > VOF_KEY_INLINE_SZ) {
		item->vi_key_off = arg->via_dkey_off;
		memcpy(arg->via_dkey_ptr, ent->ie_key.iov_buf, item->vi_size);
		arg->via_dkey_off += item->vi_size;
		arg->via_dkey_ptr += item->vi_size;
	} else {
		memcpy(item->vi_key, ent->ie_key.iov_buf, item->vi_size);
	}
}

static void
vof_fill_akey(struct vof_iter_arg *arg, vos_iter_entry_t *ent)
{
	struct vof_item_df	*item;
	struct vof_item_df	*dkey;

	dkey = &arg->via_dkeys[arg->via_dkey_idx];
	dkey->vi_child_nr++;

	arg->via_akey_idx++;
	D_ASSERTF(arg->via_akey_idx < arg->via_akey_nr, "%d >= %d\n",
		  arg->via_akey_idx, arg->via_akey_nr);
	item = &arg->via_akeys[arg->via_akey_idx];
	item->vi_type = VOF_AKEY;
	item->vi_size = ent->ie_key.iov_len;
	/* vi_child_idx/vi_child_nr/vi_child_type/vi_inob will be updated when fill value */
	if (item->vi_size > VOF_KEY_INLINE_SZ) {
		item->vi_key_off = arg->via_akey_off;
		memcpy(arg->via_akey_ptr, ent->ie_key.iov_buf, item->vi_size);
		arg->via_akey_off += item->vi_size;
		arg->via_akey_ptr += item->vi_size;
	} else {
		memcpy(item->vi_key, ent->ie_key.iov_buf, item->vi_size);
	}
}

static void
vof_val_read(struct vof_iter_arg *arg, vos_iter_entry_t *ent, void *val_ptr, uint32_t len)
{
	struct vos_container		*cont = vos_hdl2cont(arg->via_coh);
	struct bio_iov			*biov = &ent->ie_biov;
	struct bio_io_context		*bio_ctx;
	struct umem_instance		*umem;
	d_iov_t				 data;
	int				 rc;

	D_ASSERTF(!bio_addr_is_hole(&biov->bi_addr), "should not be hole\n");

	d_iov_set(&data, val_ptr, len);
	bio_ctx = vos_data_ioctxt(cont->vc_pool);
	umem = &cont->vc_pool->vp_umm;
	rc = vos_media_read(bio_ctx, umem, biov->bi_addr, &data);
	D_ASSERTF(rc == 0, "read failed "DF_RC"\n", DP_RC(rc));
}

static bool
vof_val_should_pack(vos_iter_entry_t *ent)
{
	bio_addr_t	*addr = &ent->ie_biov.bi_addr;

	return addr->ba_type == DAOS_MEDIA_SCM && !BIO_ADDR_IS_DEDUP(addr) &&
	       !BIO_ADDR_IS_CORRUPTED(addr) && !BIO_ADDR_IS_HOLE(addr);
}

static void
vof_fill_recx(struct vof_iter_arg *arg, vos_iter_entry_t *ent)
{
	struct vof_item_df	*item;
	struct vof_item_df	*akey;
	bio_addr_t		*addr = &ent->ie_biov.bi_addr;
	uint64_t		 len;

	akey = &arg->via_akeys[arg->via_akey_idx];
	if (akey->vi_child_nr == 0) {
		akey->vi_child_idx = arg->via_dkey_nr + arg->via_akey_nr + arg->via_singv_nr +
				     arg->via_ext_idx + 1;
		D_ASSERTF(akey->vi_child_idx < arg->via_item_nr, "%d >= %d\n",
			  akey->vi_child_idx, arg->via_item_nr);
		akey->vi_child_type = VOF_EXT;
		akey->vi_inob = ent->ie_rsize;
	} else {
		D_ASSERTF(akey->vi_child_type == VOF_EXT, "bad type %d\n", akey->vi_child_type);
		D_ASSERTF(akey->vi_inob == ent->ie_rsize, "bad inob %d, rsize "DF_U64"\n",
			  akey->vi_inob, ent->ie_rsize);
	}
	akey->vi_child_nr++;

	arg->via_ext_idx++;
	D_ASSERTF(arg->via_ext_idx < arg->via_ext_nr, "%d >= %d\n",
		  arg->via_ext_idx, arg->via_ext_nr);
	item = &arg->via_exts[arg->via_ext_idx];
	item->vi_type = VOF_EXT;
	item->vi_size = ent->ie_recx.rx_nr;
	item->vi_ext_idx = ent->ie_recx.rx_idx;
	item->vi_ver = ent->ie_ver;

	item->vi_media_type = addr->ba_type;
	item->vi_bio_flags = addr->ba_flags;

	if (vof_val_should_pack(ent)) {
		len = ent->ie_recx.rx_nr * ent->ie_rsize;
		if (len > VOF_VAL_INLINE_SZ) {
			item->vi_val_off = arg->via_val_off;
			vof_val_read(arg, ent, arg->via_val_ptr, len);
			arg->via_val_off += len;
			arg->via_val_ptr += len;
		} else {
			vof_val_read(arg, ent, item->vi_val, len);
		}
	} else {
		item->vi_ex_addr = addr->ba_off;
	}
}

static void
vof_fill_singv(struct vof_iter_arg *arg, vos_iter_entry_t *ent)
{
	struct vof_item_df	*item;
	struct vof_item_df	*akey;
	bio_addr_t		*addr = &ent->ie_biov.bi_addr;

	akey = &arg->via_akeys[arg->via_akey_idx];
	D_ASSERTF(akey->vi_child_nr == 0, "bad singv child nr %d\n", akey->vi_child_nr);
	akey->vi_child_idx = arg->via_dkey_nr + arg->via_akey_nr + arg->via_singv_idx + 1;
	akey->vi_child_type = VOF_SINGV;
	akey->vi_inob = 1;
	akey->vi_child_nr++;

	arg->via_singv_idx++;
	D_ASSERTF(arg->via_singv_idx < arg->via_singv_nr, "%d >= %d\n",
		  arg->via_singv_idx, arg->via_singv_nr);
	item = &arg->via_singvs[arg->via_singv_idx];
	item->vi_type = VOF_SINGV;
	item->vi_size = ent->ie_rsize;
	item->vi_singv_gsize = ent->ie_gsize;
	item->vi_ver = ent->ie_ver;

	item->vi_media_type = addr->ba_type;
	item->vi_bio_flags = addr->ba_flags;

	if (vof_val_should_pack(ent)) {
		item->vi_val_off = arg->via_val_off;
		if (item->vi_size > VOF_VAL_INLINE_SZ) {
			vof_val_read(arg, ent, arg->via_val_ptr, item->vi_size);
			arg->via_val_off += item->vi_size;
			arg->via_val_ptr += item->vi_size;
		} else {
			vof_val_read(arg, ent, item->vi_val, item->vi_size);
		}
	} else {
		item->vi_ex_addr = addr->ba_off;
	}
}

static int
obj_iter_flat_cb(daos_handle_t ih, vos_iter_entry_t *ent, vos_iter_type_t type,
	       vos_iter_param_t *param, void *data, unsigned *acts)
{
	struct vof_iter_arg		*arg = data;
	int				 rc = 0;

	switch (type) {
	case VOS_ITER_DKEY:
		vof_fill_dkey(arg, ent);
		break;
	case VOS_ITER_AKEY:
		vof_fill_akey(arg, ent);
		break;
	case VOS_ITER_RECX:
		vof_fill_recx(arg, ent);
		break;
	case VOS_ITER_SINGLE:
		vof_fill_singv(arg, ent);
		break;
	default:
		D_ERROR("bad type %d\n", type);
		return -DER_INVAL;
	};

	return rc;
}

static void
vof_iter_epoch_check(struct vof_iter_arg *arg, daos_epoch_t epoch)
{
	struct vos_container	*cont = vos_hdl2cont(arg->via_coh);
	int			 i;

	D_ASSERT(epoch != 0);
	arg->via_epr.epr_lo = (arg->via_epr.epr_lo == 0) ? epoch :
			      min(arg->via_epr.epr_lo, epoch);
	arg->via_epr.epr_hi = (arg->via_epr.epr_hi == 0) ? epoch :
			      max(arg->via_epr.epr_hi, epoch);

	D_ASSERTF(arg->via_epr.epr_lo <= arg->via_epr.epr_hi,
		  "bad epr_lo "DF_X64", epr_hi "DF_X64"\n",
		  arg->via_epr.epr_lo, arg->via_epr.epr_hi);
	if (arg->via_epr.epr_lo == arg->via_epr.epr_hi ||
	    arg->via_snap_nr == 0 || arg->via_snapshots == NULL)
		return;

	for (i = 0; i < arg->via_snap_nr; i++) {
		if (arg->via_snapshots[i] < arg->via_epr.epr_hi &&
		    arg->via_snapshots[i] > arg->via_epr.epr_lo) {
			D_DEBUG(DB_IO, DF_CONT": oid "DF_UOID" epoch "DF_X64", epr_lo/_hi: "
				DF_X64"/"DF_X64" cross snapshot "DF_X64"\n",
				DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(arg->via_oid),
				epoch, arg->via_epr.epr_lo, arg->via_epr.epr_hi,
				arg->via_snapshots[i]);
			arg->via_cross_snap = true;
			return;
		}
	}
}

static int
obj_iter_count_cb(daos_handle_t ih, vos_iter_entry_t *ent, vos_iter_type_t type,
	       vos_iter_param_t *param, void *data, unsigned *acts)
{
	struct vof_iter_arg		*arg = data;
	uint64_t			 len, key_val_len, csum_len;

	if (type == VOS_ITER_RECX || type == VOS_ITER_SINGLE) {
		vof_iter_epoch_check(arg, ent->ie_epoch);
		if (arg->via_cross_snap) {
			*acts |= VOS_ITER_CB_EXIT;
			return 0;
		}
	}

	switch (type) {
	case VOS_ITER_DKEY:
		arg->via_dkey_nr++;
		arg->via_curr_akey_nr = 0;
		arg->via_curr_ext_nr = 0;
		arg->via_dkey_total_len += ent->ie_key.iov_len;
		if (ent->ie_key.iov_len <= VOF_KEY_INLINE_SZ)
			arg->via_dkey_inline_len += ent->ie_key.iov_len;
		break;
	case VOS_ITER_AKEY:
		arg->via_akey_nr++;
		arg->via_curr_akey_nr++;
		arg->via_curr_ext_nr = 0;
		arg->via_akey_total_len += ent->ie_key.iov_len;
		if (ent->ie_key.iov_len <= VOF_KEY_INLINE_SZ)
			arg->via_akey_inline_len += ent->ie_key.iov_len;
		break;
	case VOS_ITER_RECX:
		if (ent->ie_recx.rx_nr > VOF_MAX_VAL_LEN) {
			D_DEBUG(DB_IO, DF_UOID" recx "DF_RECX" exceed %d\n",
				DP_UOID(arg->via_oid), DP_RECX(ent->ie_recx), VOF_MAX_VAL_LEN);
			goto size_exceed;
		}
		arg->via_ext_nr++;
		arg->via_curr_ext_nr++;
		if (vof_val_should_pack(ent)) {
			len = ent->ie_rsize * ent->ie_recx.rx_nr;
			arg->via_val_total_len += len;
			if (len <= VOF_VAL_INLINE_SZ)
				arg->via_val_inline_len += len;
		}
		break;
	case VOS_ITER_SINGLE:
		len = ent->ie_rsize;
		if (len > VOF_MAX_VAL_LEN) {
			D_DEBUG(DB_IO, DF_UOID" singv len "DF_U64" exceed %d\n",
				DP_UOID(arg->via_oid), len, VOF_MAX_VAL_LEN);
			goto size_exceed;
		}
		arg->via_singv_nr++;
		if (vof_val_should_pack(ent)) {
			arg->via_val_total_len += len;
			if (len <= VOF_VAL_INLINE_SZ)
				arg->via_val_inline_len += len;
		}
		break;
	default:
		D_ERROR("bad type %d\n", type);
		return -DER_INVAL;
	};

	if (arg->via_dkey_nr > VOF_MAX_DKEY_PER_OBJ ||
	    arg->via_curr_akey_nr > VOF_MAX_AKEY_PER_DKEY ||
	    arg->via_curr_ext_nr > VOF_MAX_EXT_PER_AKEY) {
		D_DEBUG(DB_IO, DF_UOID" dkey_nr %d, curr_akey_nr %d, curr_ext_nr %d exceed\n",
			DP_UOID(arg->via_oid), arg->via_dkey_nr, arg->via_curr_akey_nr,
			arg->via_curr_ext_nr);
		goto size_exceed;
	}
	arg->via_item_nr++;
	key_val_len = arg->via_dkey_total_len - arg->via_dkey_inline_len +
		      arg->via_akey_total_len - arg->via_akey_inline_len +
		      arg->via_val_total_len - arg->via_val_inline_len;
	key_val_len = roundup(key_val_len, VOF_SIZE_ROUND);
	csum_len = 8; /* TODO add csum */
	len = sizeof(struct vos_obj_flat_df) + arg->via_item_nr * sizeof(struct vof_item_df) +
	      key_val_len + csum_len;
	if (len > VOF_MAX_TOTAL_LEN) {
		D_DEBUG(DB_IO, DF_UOID" dkey %d/akey %d/singv %d/ext %d, key_val_len "DF_U64
			", total len "DF_U64", exceed %d\n", DP_UOID(arg->via_oid),
			arg->via_dkey_nr, arg->via_akey_nr, arg->via_singv_nr, arg->via_ext_nr,
			key_val_len, len, VOF_MAX_TOTAL_LEN);
		goto size_exceed;
	}
	arg->via_key_val_len = key_val_len;
	arg->via_df_len = len;

	return 0;

size_exceed:
	*acts |= VOS_ITER_CB_EXIT;
	arg->via_size_exceed = true;
	return 0;
}

static void
vof_flat_fini(struct vof_iter_arg *arg)
{
	struct vos_container	*cont = vos_hdl2cont(arg->via_coh);
	struct umem_instance	*umm;

	if (arg->via_rsrvd_act) {
		umm = &cont->vc_pool->vp_umm;
		if (!arg->via_published)
			umem_cancel(umm, arg->via_rsrvd_act);
		umem_rsrvd_act_free(&arg->via_rsrvd_act);
	}
}

static int
vof_flat_init(struct vof_iter_arg *arg)
{
	struct vos_container	*cont = vos_hdl2cont(arg->via_coh);
	struct vos_obj_flat_df	*flat_df;
	struct umem_rsrvd_act	*act;
	struct umem_instance	*umm;
	umem_off_t		 umoff;

	D_ASSERTF(arg->via_item_nr > 0 && arg->via_df_len > 0 &&
		  arg->via_df_len <= VOF_MAX_TOTAL_LEN, "item_nr %d, df_len %d\n",
		  arg->via_item_nr, arg->via_df_len);
	D_ASSERTF(arg->via_item_nr == arg->via_dkey_nr + arg->via_akey_nr +
				      arg->via_singv_nr + arg->via_ext_nr,
		  "item_nr %d, dkey_nr %d, akey_nr %d, singv_nr %d, ext_nr %d\n",
		  arg->via_item_nr, arg->via_dkey_nr, arg->via_akey_nr,
		  arg->via_singv_nr, arg->via_ext_nr);

	umm = &cont->vc_pool->vp_umm;
	umem_rsrvd_act_alloc(umm, &act, 1);
	umoff = vos_reserve_scm(cont, act, arg->via_df_len);
	if (UMOFF_IS_NULL(umoff)) {
		D_ERROR("Reserve %d from SCM failed\n", arg->via_df_len);
		umem_rsrvd_act_free(&act);
		return -DER_NOSPACE;
	}
	flat_df = umem_off2ptr(umm, umoff);
	memset(flat_df, 0, arg->via_df_len);

	flat_df->ofd_version = VOF_VERSION;
	flat_df->ofd_dkey_nr = arg->via_dkey_nr;
	flat_df->ofd_item_nr = arg->via_item_nr;
	flat_df->ofd_epoch = arg->via_epr.epr_hi;
	flat_df->ofd_len = arg->via_df_len - sizeof(struct vos_obj_flat_df);

	arg->via_rsrvd_act = act;
	arg->via_umoff = umoff;
	arg->via_df = flat_df;
	arg->via_dkeys = (struct vof_item_df *)flat_df->ofd_payload;
	arg->via_akeys = arg->via_dkeys + arg->via_dkey_nr;
	arg->via_singvs = arg->via_akeys + arg->via_akey_nr;
	arg->via_exts = arg->via_singvs + arg->via_singv_nr;
	arg->via_dkey_idx = -1;
	arg->via_akey_idx = -1;
	arg->via_singv_idx = -1;
	arg->via_ext_idx = -1;
	arg->via_dkey_off = arg->via_item_nr * sizeof(struct vof_item_df);
	arg->via_dkey_ptr = flat_df->ofd_payload + arg->via_dkey_off;
	D_ASSERT((uintptr_t)arg->via_dkey_ptr == (uintptr_t)(arg->via_exts + arg->via_ext_nr));
	arg->via_akey_off = arg->via_dkey_off + arg->via_dkey_total_len - arg->via_dkey_inline_len;
	arg->via_akey_ptr = flat_df->ofd_payload + arg->via_akey_off;
	arg->via_val_off = arg->via_akey_off + arg->via_akey_total_len - arg->via_akey_inline_len;
	arg->via_val_ptr = flat_df->ofd_payload + arg->via_val_off;

	return 0;
}

void
vof_flat_dump(struct vos_obj_flat_df *df)
{
	struct vof_item_df		*dkey, *akey, *val;
	struct vof_item_df		*items;
	daos_key_t			 tmp_dkey, tmp_akey;
	int				 i, j, k;

	items = (struct vof_item_df *)df->ofd_payload;
	D_PRINT("dkey_nr %d\n", df->ofd_dkey_nr);
	for (i = 0; i < df->ofd_dkey_nr; i++) {
		dkey = items + i;
		d_iov_set(&tmp_dkey, vof_key_ptr(df, dkey), dkey->vi_size);
		D_PRINT("dkey=" DF_KEY ", child_nr %d\n", DP_KEY(&tmp_dkey), dkey->vi_child_nr);
		for (j = 0; j < dkey->vi_child_nr; j++) {
			akey = &items[dkey->vi_child_idx + j];
			d_iov_set(&tmp_akey, vof_key_ptr(df, akey), akey->vi_size);
			D_PRINT("akey=" DF_KEY ", child_nr %d\n",
				DP_KEY(&tmp_akey), akey->vi_child_nr);
			for (k = 0; k < akey->vi_child_nr; k++) {
				val = &items[akey->vi_child_idx + k];
				if (val->vi_type == VOF_SINGV) {
					D_PRINT("dkey=" DF_KEY ", akey=" DF_KEY ", singv %d, "
						"pm_ver %d.\n", DP_KEY(&tmp_dkey),
						DP_KEY(&tmp_akey), val->vi_size, val->vi_ver);
				} else if (val->vi_type == VOF_EXT) {
					D_PRINT("dkey=" DF_KEY ", akey=" DF_KEY ", "
						"ext ["DF_U64",%d], iod_size %d, pm_ver %d\n",
						DP_KEY(&tmp_dkey), DP_KEY(&tmp_akey),
						val->vi_ext_idx, val->vi_size, akey->vi_inob,
						val->vi_ver);
				} else {
					D_PRINT("dkey=" DF_KEY ", akey=" DF_KEY " bad type %d\n",
						DP_KEY(&tmp_dkey), DP_KEY(&tmp_akey), val->vi_type);
				}
			}
		}
	}
}

static int
vof_flat_sort(struct vof_iter_arg *arg)
{
	struct vos_obj_flat_df		*df = arg->via_df;
	struct vof_item_df		*items;
	struct vof_item_df		*dkey, *akey;
	unsigned int			 i, j;
	int				 rc;

	/* sort dkeys */
	rc = daos_array_sort_adv(df, 0, df->ofd_dkey_nr, true, &vof_item_sort_ops);
	if (rc) {
		D_ERROR("failed to sort dkeys, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	items = (struct vof_item_df *)df->ofd_payload;
	for (i = 0; i < df->ofd_dkey_nr; i++) {
		dkey = items + i;
		if (dkey->vi_child_nr > 1) {
			/* sort akeys under the dkey */
			rc = daos_array_sort_adv(df, dkey->vi_child_idx, dkey->vi_child_nr, true,
						 &vof_item_sort_ops);
			if (rc) {
				D_ERROR("failed to sort akeys, "DF_RC"\n", DP_RC(rc));
				return rc;
			}
		}
		for (j = 0; j < dkey->vi_child_nr; j++) {
			akey = &items[dkey->vi_child_idx + j];
			if (akey->vi_child_nr <= 1)
				continue;
			D_ASSERTF(akey->vi_child_type == VOF_EXT, "bad type %d\n",
				  akey->vi_child_type);
			/* sort exts under the akey */
			rc = daos_array_sort_adv(df, akey->vi_child_idx, akey->vi_child_nr, true,
						 &vof_item_sort_ops);
			if (rc) {
				D_ERROR("failed to sort exts, "DF_RC"\n", DP_RC(rc));
				return rc;
			}
		}
	}

	return rc;
}

int
vof_flat_publish(daos_handle_t ih, unsigned *acts, struct vof_iter_arg *arg)
{
	daos_handle_t			 coh = arg->via_coh;
	daos_unit_oid_t			 oid = arg->via_oid;
	struct vos_container		*cont = vos_hdl2cont(coh);
	struct daos_lru_cache		*occ;
	struct vos_object		*obj;
	struct vos_obj_df		*obj_df;
	struct vos_iterator		*iter = vos_hdl2iter(ih);
	daos_epoch_range_t		 epr = {0, arg->via_flat_epoch};
	struct umem_instance		*umm;
	int				 rc;

	D_ASSERT(iter->it_type == VOS_ITER_OBJ);

	occ = vos_obj_cache_current(cont->vc_pool->vp_sysdb);
	rc = vos_obj_hold(occ, cont, oid, &epr, 0, VOS_OBJ_VISIBLE, DAOS_INTENT_DEFAULT, &obj, 0);
	if (rc != 0) {
		D_ERROR(DF_UOID" vos obj hold failed: rc = "DF_RC"\n", DP_UOID(oid), DP_RC(rc));
		return rc;
	}
	obj_df = obj->obj_df;
	D_ASSERT(memcmp(&oid, &obj_df->vo_id, sizeof(oid)) == 0);

	umm = &cont->vc_pool->vp_umm;
	rc = umem_tx_begin(umm, NULL);
	if (rc)
		goto out;

	rc = umem_tx_publish(umm, arg->via_rsrvd_act);
	if (rc) {
		D_ERROR("tx publish failed "DF_RC"\n", DP_RC(rc));
		goto tx_end;
	}

	/* TODO: destroy the obj_df->vo_tree */

	rc = umem_tx_xadd_ptr(umm, &obj_df->vo_tree, sizeof(obj_df->vo_tree),
			      UMEM_XADD_NO_SNAPSHOT);
	if (rc)
		goto tx_end;
	bio_addr_set(&obj_df->vo_flat.vo_flat_addr, DAOS_MEDIA_SCM, arg->via_umoff);
	obj_df->vo_flat.vo_flat_len = arg->via_df_len;

tx_end:
	rc = umem_tx_end(umm, rc);
out:
	vos_obj_release(occ, obj, false);
	if (rc == 0)
		arg->via_published = 1;
	return rc;
}

static int
cont_iter_cb(daos_handle_t ih, vos_iter_entry_t *ent, vos_iter_type_t type,
	     vos_iter_param_t *cont_param, void *data, unsigned *acts)
{
	struct vof_iter_arg		*arg = data;
	daos_handle_t			 coh = arg->via_coh;
	struct vos_container		*cont = vos_hdl2cont(coh);
	daos_epoch_t			 epoch = arg->via_flat_epoch;
	daos_unit_oid_t			 oid = ent->ie_oid;
	vos_iter_param_t		 param = { 0 };
	struct vos_iter_anchors		 anchor = { 0 };
	int				 rc;

	D_ASSERTF(type == VOS_ITER_OBJ, "bad type %d\n", type);
	memset(&arg->via_oid, 0, sizeof(*arg) - offsetof(struct vof_iter_arg, via_oid));
	arg->via_oid = oid;
	param.ip_hdl = arg->via_coh;
	param.ip_oid = oid;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = epoch;
	/* Only show visible records and skip punches */
	param.ip_flags = VOS_IT_RECX_VISIBLE | VOS_IT_RECX_SKIP_HOLES;
	param.ip_epc_expr = VOS_IT_EPC_RE;

	/* 1st iteration to count the number/length of key/value */
	D_DEBUG(DB_EPC, DF_CONT": iterate oid "DF_UOID"\n",
		DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(oid));
	rc = vos_iterate(&param, VOS_ITER_DKEY, true, &anchor, obj_iter_count_cb, NULL, arg, NULL);
	if (arg->via_size_exceed || arg->via_cross_snap || arg->via_item_nr == 0) {
		D_DEBUG(DB_IO, DF_CONT": oid "DF_UOID" size_exceed %d, cross_snap %d, item_nr %d, "
			"exit iterate\n", DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(oid),
			arg->via_size_exceed, arg->via_cross_snap, arg->via_item_nr);
		if (rc == 4 /* ITER_EXIT */)
			rc = 0;
		goto out;
	}
	if (rc != 0) {
		D_ERROR(DF_CONT": iterate oid "DF_UOID" failed, "DF_RC"\n",
			DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(oid), DP_RC(rc));
		goto out;
	}

	rc = vof_flat_init(arg);
	if (rc)
		goto out;

	/* 2nd iteration for flattening */
	memset(&anchor, 0, sizeof(anchor));
	D_DEBUG(DB_EPC, DF_CONT": iterate oid "DF_UOID" for flattening\n",
		DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(oid));
	rc = vos_iterate(&param, VOS_ITER_DKEY, true, &anchor, obj_iter_flat_cb, NULL, arg, NULL);
	if (rc != 0) {
		D_ERROR(DF_CONT": iterate oid "DF_UOID" failed, "DF_RC"\n",
			DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(oid), DP_RC(rc));
		goto out;
	}

	rc = vof_flat_sort(arg);
	if (rc != 0) {
		D_ERROR(DF_CONT": flat sort "DF_UOID" failed, "DF_RC"\n",
			DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(oid), DP_RC(rc));
		goto out;
	}

#if 0
	rc = vof_flat_publish(ih, acts, arg);
	if (rc != 0) {
		D_ERROR(DF_CONT": flat publish "DF_UOID" failed, "DF_RC"\n",
			DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_UOID(oid), DP_RC(rc));
		goto out;
	}
#endif

out:
	vof_flat_fini(arg);
	return rc;
}

int
vos_flatten(daos_handle_t coh, daos_epoch_t epoch, uint64_t *snapshots, uint32_t snap_nr,
	    int (*yield_func)(void *arg), void *yield_arg)
{
	struct vos_container		*cont = vos_hdl2cont(coh);
	struct vof_iter_arg		 arg = { 0 };
	vos_iter_param_t		 param = { 0 };
	struct vos_iter_anchors		 anchor = { 0 };
	int				 rc = 0;

	arg.via_coh = coh;
	arg.via_flat_epoch = epoch;
	arg.via_snapshots = snapshots;
	arg.via_snap_nr = snap_nr;
	param.ip_hdl = coh;
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = epoch;

	rc = vos_iterate(&param, VOS_ITER_OBJ, false, &anchor, cont_iter_cb, NULL, &arg, NULL);
	if (rc)
		D_ERROR(DF_CONT": iterate failed, "DF_RC"\n",
			DP_CONT(cont->vc_pool->vp_id, cont->vc_id), DP_RC(rc));

	return rc;
}
