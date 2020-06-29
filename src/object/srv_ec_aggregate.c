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
 "
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * DAOS server erasure-coded object IO handling.
 *
 * src/object/srv_ec.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <stddef.h>
#include <stdio.h>
#include <daos/common.h>
#include <daos_srv/vos.h>
#include <daos_srv/daos_server.h>
#include "obj_ec.h"
#include "obj_internal.h"

struct ec_agg_set {
	d_list_t	as_entries;
	uuid_t		as_pool_uuid;
	daos_epoch_t	as_start_epoch;
	uint32_t	as_pool_version;
	unsigned int	as_count;
};

struct ec_agg_par_extent {
	daos_recx_t	ape_recx;
	daos_epoch_t	ape_epoch;
};

struct ec_agg_stripe {
	daos_off_t	as_stripenum;
	daos_epoch_t	as_hi_epoch;
	d_list_t	as_dextents;
	daos_off_t	as_stripe_fill;
	unsigned int	as_extent_cnt;
};

struct ec_agg_entry {
	d_list_t		 ae_link;
	daos_unit_oid_t		 ae_oid;
	struct daos_oclass_attr	*ae_oca;
	d_sg_list_t		*ae_sgl;
	daos_handle_t		 ae_chdl;
	daos_handle_t		 ae_thdl;
	/* upper extent threshold */
	daos_epoch_t		 ae_epoch;
	/* lower extent threshold - thresholds not currently used */
	daos_epoch_t		 ae_epoch_lo;
	daos_key_t		 ae_dkey;
	daos_key_t		 ae_akey;
	daos_size_t		 ae_rsize;
	struct ec_agg_stripe	 ae_cur_stripe;
	struct ec_agg_par_extent ae_par_extent;
//	ABT_eventual		 ae_eventual;
//	int			 ae_offload_rc;
};

struct ec_agg_extent {
	d_list_t	ae_link;
	daos_recx_t	ae_recx;
	daos_epoch_t	ae_epoch;
};

#define MARK_YIELD 1

static void
op_swap(void *array, int a, int b)
{
	struct ec_agg_entry **agg_entry_array = (struct ec_agg_entry **)array;
	struct ec_agg_entry  *agg_entry_tmp;

	agg_entry_tmp = agg_entry_array[a];
	agg_entry_array[a] = agg_entry_array[b];
	agg_entry_array[b] = agg_entry_tmp;

}

static int
op_cmp(void *array, int a, int b)
{
	struct ec_agg_entry **agg_entry_array = (struct ec_agg_entry **)array;

	if (agg_entry_array[a]->ae_oid.id_pub.lo >
				 agg_entry_array[b]->ae_oid.id_pub.lo)
		return 1;
	if (agg_entry_array[a]->ae_oid.id_pub.lo <
				 agg_entry_array[b]->ae_oid.id_pub.lo)
		return -1;
	if (agg_entry_array[a]->ae_oid.id_pub.hi >
				 agg_entry_array[b]->ae_oid.id_pub.hi)
		return 1;
	if (agg_entry_array[a]->ae_oid.id_pub.hi <
				 agg_entry_array[b]->ae_oid.id_pub.hi)
		return -1;
	if (agg_entry_array[a]->ae_epoch > agg_entry_array[b]->ae_epoch)
		return 1;
	if (agg_entry_array[a]->ae_epoch < agg_entry_array[b]->ae_epoch)
		return -1;
	return 0;
}

static int
op_cmp_key(void *array, int i, uint64_t key)
{
	struct ec_agg_entry **agg_entry_array = (struct ec_agg_entry **)array;

	if (agg_entry_array[i]->ae_oid.id_pub.lo > key)
		return 1;
	if (agg_entry_array[i]->ae_oid.id_pub.lo < key)
		return - 1;
	return 0;
}

static daos_sort_ops_t sort_ops = {
	.so_swap	= op_swap,
	.so_cmp		= op_cmp,
	.so_cmp_key	= op_cmp_key,
};

static int
committed_dtx_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *value, void *arg)
{
        daos_unit_oid_t		 oid = vos_cmt_get_oid(value);
	daos_epoch_t		 epoch = vos_cmt_get_epoch(value);
	struct ec_agg_set	*agg_set = (struct ec_agg_set *)arg;
	struct daos_oclass_attr *oca;
	int			 rc = 0;

	if ( epoch <= agg_set->as_start_epoch ||
				 !daos_oclass_is_ec(oid.id_pub, &oca))
		return rc;
	rc = ds_pool_check_leader(agg_set->as_pool_uuid, &oid,
				  agg_set->as_pool_version);
	if (rc == 1) {
		struct ec_agg_entry *entry;

		D_ALLOC_PTR(entry);
		if (entry == NULL) {
			rc = -DER_NOMEM;
		} else {
			entry->ae_oid = oid;
			entry->ae_oca = oca;
			entry->ae_epoch = epoch;
			entry->ae_cur_stripe.as_stripenum = ~0UL;
			d_list_add_tail(&entry->ae_link, &agg_set->as_entries);
			agg_set->as_count++;
			rc = 0;
		}
	}
	return rc;
}

static bool
agg_ent_oid_equal(struct ec_agg_entry *lhs, struct ec_agg_entry *rhs)
{
	return !daos_unit_oid_compare(lhs->ae_oid, rhs->ae_oid);
}

static inline void
reset_agg_pos(vos_iter_type_t type, struct ec_agg_entry *agg_entry)
{
	switch (type) {
	case VOS_ITER_DKEY:
		memset(&agg_entry->ae_dkey, 0, sizeof(agg_entry->ae_dkey));
		break;
	case VOS_ITER_AKEY:
		memset(&agg_entry->ae_akey, 0, sizeof(agg_entry->ae_akey));
		break;
	default:
		break;
        }
}

static inline int
agg_key_compare(daos_key_t key1, daos_key_t key2)
{
	if (key1.iov_len != key2.iov_len)
		return 1;

	return memcmp(key1.iov_buf, key2.iov_buf, key1.iov_len);
}

static int
agg_dkey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	int rc = 0;

	if (agg_key_compare(agg_entry->ae_dkey, entry->ie_key)) {
		agg_entry->ae_dkey = entry->ie_key;
		reset_agg_pos(VOS_ITER_AKEY, agg_entry);
	}
	agg_entry->ae_dkey	= entry->ie_key;
	return rc;
}

static int
agg_akey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	int rc = 0;

	agg_entry->ae_akey	= entry->ie_key;
	agg_entry->ae_thdl	= ih;
	return rc;
}

static bool
agg_carry_over(struct ec_agg_entry *agg_entry, struct ec_agg_extent *agg_extent)
{
	/* TBD */
	return false;
}


static void
agg_clear_extents(struct ec_agg_entry *agg_entry, bool is_end)
{
	struct ec_agg_extent *agg_extent, *ext_tmp;

	d_list_for_each_entry_safe(agg_extent, ext_tmp,
				   &agg_entry->ae_cur_stripe.as_dextents,
				   ae_link) {
		// check for carry-over extent
		if (!agg_carry_over(agg_entry, agg_extent)) {
			d_list_del(&agg_extent->ae_link);
			D_FREE_PTR(agg_extent);
		}
	}
	agg_entry->ae_cur_stripe.as_hi_epoch = 0UL;
	agg_entry->ae_cur_stripe.as_stripe_fill = 0U;
}

static int
agg_akey_post(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry,
	 unsigned int *acts)
{
	int rc = 0;

	agg_clear_extents(agg_entry, true);
	return rc;
}

static inline daos_off_t
agg_stripenum(struct ec_agg_entry *entry, daos_off_t ex_lo)
{
	return ex_lo / (entry->ae_oca->u.ec.e_k * entry->ae_oca->u.ec.e_len);
}

static int
agg_recx_iter_pre_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		     vos_iter_type_t type, vos_iter_param_t *param,
		     void *cb_arg, unsigned int *acts)
{
	struct ec_agg_entry	*agg_entry = (struct ec_agg_entry *) cb_arg;
	int			 rc = 0;

	D_ASSERT(type == VOS_ITER_RECX);
	D_ASSERT(entry->ie_recx.rx_idx == (PARITY_INDICATOR |
		(agg_entry->ae_cur_stripe.as_stripenum *
			agg_entry->ae_oca->u.ec.e_len)));
	agg_entry->ae_par_extent.ape_recx = entry->ie_recx;
	agg_entry->ae_par_extent.ape_epoch = entry->ie_epoch;
	D_PRINT("epoch: %lu\n", entry->ie_epoch);
	return rc;
}

enum agg_iov_entry {
	AGG_IOV_DATA	= 0,
	AGG_IOV_ODATA,
	AGG_IOV_PARITY,
	AGG_IOV_OPARITY,
	AGG_IOV_CNT,
};

static int
agg_alloc_buf(d_sg_list_t *sgl, size_t ent_buf_len, unsigned int iov_entry)
{
	unsigned char	*buf = NULL;
	int		 rc = 0;

	D_REALLOC(buf, sgl->sg_iovs[iov_entry].iov_buf,
		  ent_buf_len);
	if (buf == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	sgl->sg_iovs[iov_entry].iov_buf = buf;
	sgl->sg_iovs[iov_entry].iov_buf_len = ent_buf_len;
out:
	return rc;
}

static int
agg_prep_sgl(struct ec_agg_entry *entry)
{
	size_t		 data_buf_len, par_buf_len;
	unsigned int	 len = entry->ae_oca->u.ec.e_len;
	unsigned int	 k = entry->ae_oca->u.ec.e_k;
	unsigned int	 p = entry->ae_oca->u.ec.e_p;
	int		 rc = 0;

	if (entry->ae_sgl->sg_nr == 0) {
		D_ALLOC_ARRAY(entry->ae_sgl->sg_iovs, AGG_IOV_CNT);
		if (entry->ae_sgl->sg_iovs == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		entry->ae_sgl->sg_nr = AGG_IOV_CNT;
	}
	D_ASSERT(entry->ae_sgl->sg_nr == AGG_IOV_CNT);
	data_buf_len = len * k * entry->ae_rsize;
	if (entry->ae_sgl->sg_iovs[AGG_IOV_DATA].iov_buf_len < data_buf_len) {
		rc = agg_alloc_buf(entry->ae_sgl, data_buf_len, AGG_IOV_DATA);
		if (rc)
			goto out;
	}
	if (entry->ae_sgl->sg_iovs[AGG_IOV_ODATA].iov_buf_len < data_buf_len) {
		rc = agg_alloc_buf(entry->ae_sgl, data_buf_len, AGG_IOV_ODATA);
		if (rc)
			goto out;
	}
	par_buf_len = len * p * entry->ae_rsize;
	if (entry->ae_sgl->sg_iovs[AGG_IOV_PARITY].iov_buf_len < par_buf_len) {
		rc = agg_alloc_buf(entry->ae_sgl, par_buf_len, AGG_IOV_PARITY);
		if (rc)
			goto out;
	}
	if (entry->ae_sgl->sg_iovs[AGG_IOV_OPARITY].iov_buf_len < par_buf_len) {
		rc = agg_alloc_buf(entry->ae_sgl, par_buf_len, AGG_IOV_OPARITY);
		if (rc)
			goto out;
	}
out:
	return rc;

}

static int
agg_fetch_data_stripe(struct ec_agg_entry *entry)
{
	daos_iod_t	iod = { 0 };
	daos_recx_t	recx = { 0 };
	unsigned int	len = entry->ae_oca->u.ec.e_len;
	unsigned int	k = entry->ae_oca->u.ec.e_k;
	int		rc = 0;

	rc = agg_prep_sgl(entry);
	if (rc)
		goto out;
	recx.rx_idx = entry->ae_cur_stripe.as_stripenum * k * len;
	recx.rx_nr = k * len;
	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = entry->ae_rsize;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	entry->ae_sgl->sg_nr = 1;
	entry->ae_sgl->sg_iovs[AGG_IOV_DATA].iov_len = len * k * entry->ae_rsize;

	rc = vos_obj_fetch(entry->ae_chdl, entry->ae_oid,
			   entry->ae_cur_stripe.as_hi_epoch,
			   VOS_FETCH_RECX_LIST, &entry->ae_dkey, 1, &iod,
			   entry->ae_sgl);
	entry->ae_sgl->sg_nr = AGG_IOV_CNT;

out:
	return rc;

}

static void
agg_encode_full_stripe(void *arg)
{
	struct ec_agg_entry	*entry = (struct ec_agg_entry *)arg;
	struct obj_ec_codec	*codec;
	unsigned int		 len = entry->ae_oca->u.ec.e_len;
	unsigned int		 k = entry->ae_oca->u.ec.e_k;
	unsigned int		 p = entry->ae_oca->u.ec.e_p;
	unsigned int		 cell_bytes = len * entry->ae_rsize;
	unsigned char		*data[k];
	unsigned char		*parity_bufs[p];
	unsigned char		*buf;
	int			 i;

	buf = entry->ae_sgl->sg_iovs[AGG_IOV_DATA].iov_buf;
	for (i = 0; i < k; i++)
		data[i] = &buf[i*len];

	buf = entry->ae_sgl->sg_iovs[AGG_IOV_PARITY].iov_buf;
	for (i = 0; i < p; i++)
		parity_bufs[i] = &buf[i*len];

	codec = obj_ec_codec_get(daos_obj_id2class(entry->ae_oid.id_pub));
	ec_encode_data(cell_bytes, k, p, codec->ec_gftbls, data, parity_bufs);
}

static int
agg_encode_local_parity(struct ec_agg_entry *entry)
{
	int rc = 0;

	rc = agg_fetch_data_stripe(entry);
	if (rc)
		goto out;
	agg_encode_full_stripe(entry);

out:
	return rc;
}

static bool
agg_data_is_newer(struct ec_agg_entry *entry)
{
	struct ec_agg_extent	*agg_extent;

	d_list_for_each_entry(agg_extent, &entry->ae_cur_stripe.as_dextents,
			      ae_link) {
		if (agg_extent->ae_epoch < entry->ae_par_extent.ape_epoch)
			return false;
	}
	return true;
}

static bool
agg_stripe_is_filled(struct ec_agg_entry *entry, bool has_parity)
{
	bool	is_filled, rc = false;


	D_PRINT("naive has parity: %d\n", has_parity == false ? 0 : 1);
	is_filled = entry->ae_cur_stripe.as_stripe_fill ==
		entry->ae_oca->u.ec.e_k * entry->ae_oca->u.ec.e_len;
	D_PRINT("naive is filled: %d\n", is_filled == false ? 0 : 1);

	if (is_filled)
		if (!has_parity || agg_data_is_newer(entry))
			rc = true;
	return rc;
}

static int
agg_update_vos(struct ec_agg_entry *entry)
{
	d_sg_list_t	sgl = { 0 };
	daos_iod_t	iod = { 0 };
	d_iov_t		iov;
	daos_recx_t	recx;
	unsigned int	len = entry->ae_oca->u.ec.e_len;
	unsigned int	k = entry->ae_oca->u.ec.e_k;
	int		rc = 0;

	iov.iov_buf = entry->ae_sgl->sg_iovs[AGG_IOV_PARITY].iov_buf;
	iov.iov_buf_len =
			entry->ae_sgl->sg_iovs[AGG_IOV_PARITY].iov_buf_len;
	iov.iov_len =
			entry->ae_sgl->sg_iovs[AGG_IOV_PARITY].iov_buf_len;
	sgl.sg_iovs = &iov;
	sgl.sg_nr = 1;

	recx.rx_idx = entry->ae_cur_stripe.as_stripenum * k * len;
	recx.rx_nr = k * len;
	iod.iod_name = entry->ae_akey;
	iod.iod_type = DAOS_IOD_ARRAY;
	iod.iod_size = 0;
	iod.iod_nr = 1;
	iod.iod_recxs = &recx;
	rc = vos_obj_update(entry->ae_chdl, entry->ae_oid,
			    entry->ae_cur_stripe.as_hi_epoch,
			    0, 0, &entry->ae_dkey, 1, &iod, NULL, NULL);

	D_PRINT("Update for delete returned: %d\n", rc);
	iod.iod_size = entry->ae_rsize;
	recx.rx_idx = entry->ae_cur_stripe.as_stripenum * len;
	recx.rx_nr = len;
	rc = vos_obj_update(entry->ae_chdl, entry->ae_oid,
			    entry->ae_cur_stripe.as_hi_epoch, 0, 0,
			    //VOS_OF_OVERWRITE,
			    &entry->ae_dkey, 1, &iod, NULL,
			    &sgl);
	D_PRINT("Update parity returned: %d\n", rc);
	return rc;
}

static int
agg_process_stripe(struct ec_agg_entry *entry, bool *mark_yield)
{
	vos_iter_param_t	iter_param = { 0 };
	struct vos_iter_anchors	anchors = { 0 };
	bool			update_vos = true;
	int			rc = 0;

	entry->ae_par_extent.ape_epoch	= ~(0ULL);

	iter_param.ip_hdl		= DAOS_HDL_INVAL;
	iter_param.ip_ih		= entry->ae_thdl;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_idx	= PARITY_INDICATOR |
					  (entry->ae_cur_stripe.as_stripenum *
						entry->ae_oca->u.ec.e_len);
	iter_param.ip_recx.rx_nr	= entry->ae_oca->u.ec.e_len;

	D_PRINT("Querying parity for stripe: %lu, offset: %lu\n",
		entry->ae_cur_stripe.as_stripenum,
		iter_param.ip_recx.rx_idx);

	rc = vos_iterate(&iter_param, VOS_ITER_RECX, false, &anchors,
			 agg_recx_iter_pre_cb, NULL, entry);
	if (rc != 0)
		goto out;

	D_PRINT("Par query: epoch: %lu, offset: %lu, length: %lu\n",
		entry->ae_par_extent.ape_epoch,
		entry->ae_par_extent.ape_recx.rx_idx,
		entry->ae_par_extent.ape_recx.rx_nr);

	if (entry->ae_par_extent.ape_epoch > entry->ae_cur_stripe.as_hi_epoch &&
			entry->ae_par_extent.ape_epoch != ~(0ULL)) {
		D_PRINT("parity newer than data; nothing to do\n");
		goto out;
	}

	if ((entry->ae_par_extent.ape_epoch == ~(0ULL)
				 && agg_stripe_is_filled(entry, false)) ||
					agg_stripe_is_filled(entry, true)) {
		rc = agg_encode_local_parity(entry);
		//*mark_yield = true;
		goto out;
	if (entry->ae_par_extent.ape_epoch == ~(0ULL))
		update_vos = false;
		goto out;
	}
out:
	if (update_vos)
		rc = agg_update_vos(entry);
		/* offload of dsc_obj_update (TBD) to push remote parity */

	agg_clear_extents(entry, false);
	return rc;
}

static daos_off_t
agg_in_stripe(struct ec_agg_entry *entry, daos_recx_t *recx)
{
	unsigned int		len = entry->ae_oca->u.ec.e_len;
	unsigned int		k = entry->ae_oca->u.ec.e_k;
	daos_off_t		stripe = recx->rx_idx / (len * k);
	daos_off_t		stripe_end = (stripe + 1) * len * k;

	if (recx->rx_idx + recx->rx_nr > stripe_end)
		return stripe_end - recx->rx_idx;
	else
		return recx->rx_nr;
}

static int
agg_data_extent(vos_iter_entry_t *entry, struct ec_agg_entry *agg_entry,
		daos_handle_t ih, unsigned int *acts)
{
	struct ec_agg_extent	*extent = NULL;
	bool			 mark_yield = false;
	int			 rc = 0;

	if (agg_stripenum(agg_entry, entry->ie_recx.rx_idx) !=
			agg_entry->ae_cur_stripe.as_stripenum) {
		if (agg_entry->ae_cur_stripe.as_stripenum != ~0UL)
			agg_process_stripe(agg_entry, &mark_yield);
		agg_entry->ae_cur_stripe.as_stripenum =
			agg_stripenum(agg_entry,entry->ie_recx.rx_idx);
		D_PRINT("stripenum: %lu\n",
			agg_entry->ae_cur_stripe.as_stripenum);
	}
	D_ALLOC_PTR(extent);
	if (extent == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}
	extent->ae_recx = entry->ie_recx;
	extent->ae_epoch = entry->ie_epoch;
	agg_entry->ae_rsize = entry->ie_rsize;

	d_list_add_tail(&extent->ae_link,
			&agg_entry->ae_cur_stripe.as_dextents);
	agg_entry->ae_cur_stripe.as_extent_cnt++;
	agg_entry->ae_cur_stripe.as_stripe_fill +=
		agg_in_stripe(agg_entry, &entry->ie_recx);

	if (extent->ae_epoch > agg_entry->ae_cur_stripe.as_hi_epoch)
		agg_entry->ae_cur_stripe.as_hi_epoch = extent->ae_epoch;

	D_PRINT("adding extent %lu,%lu, to stripe  %lu, shard: %u\n",
		extent->ae_recx.rx_idx, extent->ae_recx.rx_nr,
		agg_stripenum(agg_entry, extent->ae_recx.rx_idx),
		agg_entry->ae_oid.id_shard);
out:
	if (mark_yield)
		*acts |= VOS_ITER_CB_YIELD;
	return rc;
}

static int
agg_ev(daos_handle_t ih, vos_iter_entry_t *entry,
       struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	int			rc = 0;

	if (!(entry->ie_recx.rx_idx & PARITY_INDICATOR))
		rc = agg_data_extent(entry, agg_entry, ih, acts);
	else
		D_ASSERT(false);

	return rc;
}

static int
agg_iterate_pre_cb(daos_handle_t ih, vos_iter_entry_t *entry,
	vos_iter_type_t type, vos_iter_param_t *param,
	void *cb_arg, unsigned int *acts)
{
	struct ec_agg_entry	*agg_entry = (struct ec_agg_entry *) cb_arg;
	int			 rc = 0;

	switch(type) {
	case VOS_ITER_DKEY:
		rc = agg_dkey(ih, entry, agg_entry, acts);
		break;
        case VOS_ITER_AKEY:
		rc = agg_akey(ih, entry, agg_entry, acts);
		break;
	case VOS_ITER_RECX:
		rc = agg_ev(ih, entry, agg_entry, acts);
		break;
	default:
		break;
	}

	return rc;
}

static int
agg_iterate_post_cb(daos_handle_t ih, vos_iter_entry_t *entry,
	vos_iter_type_t type, vos_iter_param_t *param,
	void *cb_arg, unsigned int *acts)
{
	struct ec_agg_entry	*agg_entry = (struct ec_agg_entry *) cb_arg;
	int			 rc = 0;

	switch(type) {
	case VOS_ITER_DKEY:
		break;
        case VOS_ITER_AKEY:
		rc = agg_akey_post(ih, entry, agg_entry, acts);
		break;
	case VOS_ITER_RECX:
		break;
	default:
		D_PRINT("post type: %u\n", type);
		break;
	}

	return rc;
}

static int
agg_iterate(struct ec_agg_entry *agg_entry, daos_handle_t coh)
{
	vos_iter_param_t	 iter_param = { 0 };
	struct vos_iter_anchors  anchors = { 0 };
	int			 rc = 0;

	iter_param.ip_hdl		= coh;
	iter_param.ip_oid		= agg_entry->ae_oid;
	iter_param.ip_epr.epr_lo	= 0ULL;
	iter_param.ip_epr.epr_hi	= DAOS_EPOCH_MAX;
	iter_param.ip_epc_expr		= VOS_IT_EPC_RR;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_idx	= 0ULL;
	iter_param.ip_recx.rx_nr	= ~PARITY_INDICATOR;

	D_PRINT("VOS Iterate!\n");
	rc = vos_iterate(&iter_param, VOS_ITER_DKEY, true, &anchors,
			 agg_iterate_pre_cb, agg_iterate_post_cb, agg_entry);
	D_PRINT("VOS Iterate complete\n");
	return rc;

}

static int
agg_iterate_committed(struct ds_cont_child *cont, daos_epoch_t start_epoch)
{
	struct ec_agg_set	  agg_set = { 0 };
	d_sg_list_t		  sgl = { 0 };
	struct ec_agg_entry	 *agg_entry;
	struct ec_agg_entry	**agg_entry_array = NULL;
	int			  rc = 0;

	uuid_copy(agg_set.as_pool_uuid, cont->sc_pool->spc_uuid);
	agg_set.as_pool_version = cont->sc_pool->spc_pool->sp_map_version;
	agg_set.as_start_epoch = start_epoch;
	D_INIT_LIST_HEAD(&agg_set.as_entries);
	rc = vos_agg_iterate(cont->sc_hdl, committed_dtx_cb, &agg_set);
	if (agg_set.as_count) {
		struct ec_agg_entry	*prev_agg_entry = NULL;
		int			 i = 0;

		rc = vos_agg_iterate(cont->sc_hdl, committed_dtx_cb, &agg_set);
		D_ALLOC_ARRAY(agg_entry_array, agg_set.as_count);
		if (agg_entry_array == NULL) {
			D_PRINT("agg array allocation failed");
			rc = -DER_NOMEM;
			goto out;
		}
		d_list_for_each_entry(agg_entry, &agg_set.as_entries, ae_link) {
			agg_entry_array[i++] = agg_entry;
		}

		daos_array_sort(agg_entry_array, agg_set.as_count, false,
				&sort_ops);
		daos_epoch_t lo = agg_entry_array[0]->ae_epoch;
		for (i = 0; i < agg_set.as_count; i++) {
			agg_entry = agg_entry_array[i];
			if (agg_entry->ae_epoch > start_epoch)
				start_epoch = agg_entry->ae_epoch;
			if (prev_agg_entry && !agg_ent_oid_equal(prev_agg_entry,
								 agg_entry)) {
				prev_agg_entry->ae_epoch_lo = lo;
			} else if (prev_agg_entry) {
				lo = prev_agg_entry->ae_epoch_lo;
				d_list_del(&prev_agg_entry->ae_link);
				D_FREE_PTR(prev_agg_entry);
			}
			prev_agg_entry = agg_entry;
			prev_agg_entry->ae_epoch_lo = lo;
		}
		prev_agg_entry->ae_epoch_lo = lo;
		D_FREE(agg_entry_array);
		d_list_for_each_entry(agg_entry, &agg_set.as_entries, ae_link) {
			D_INIT_LIST_HEAD(&agg_entry->
					 ae_cur_stripe.as_dextents);
			agg_entry->ae_sgl = &sgl;
			agg_entry->ae_chdl = cont->sc_hdl;
			rc = agg_iterate(agg_entry, cont->sc_hdl);
			if (rc != 0)
				goto out;
		}
	} else
		sleep(10);
out:
	// free sgl
	if (rc)
		return rc;
	else
		return start_epoch;
}

static int
agg_iterate_all(daos_handle_t coh)
{
	return 0;
}

int
ds_obj_ec_aggregate(struct ds_cont_child *cont, daos_epoch_t start_epoch,
		    bool process_committed)
{
	int			  rc = 0;

	if (!process_committed)
		rc = agg_iterate_all(cont->sc_hdl);
	else
		rc = agg_iterate_committed(cont, start_epoch);

	return rc;
}
