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
#include <daos_srv/obj_ec.h>
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
	d_list_t	as_dextents;
};

struct ec_agg_entry {
	d_list_t		 ae_link;
	daos_unit_oid_t		 ae_oid;
	struct daos_oclass_attr	*ae_oca;
	daos_handle_t		 ae_hdl;
	daos_epoch_t		 ae_epoch;
	daos_epoch_t		 ae_epoch_lo;
	daos_key_t		 ae_dkey;
	daos_key_t		 ae_akey;
	struct ec_agg_stripe	 ae_current_stripe;
	struct ec_agg_par_extent ae_par_extent;
};

struct ec_agg_extent {
	d_list_t	ae_link;
	daos_recx_t	ae_recx;
	daos_epoch_t	ae_epoch;
};

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
	D_ASSERT(false);
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
	/*
	if (lhs->ae_oid.id_pub.lo == rhs->ae_oid.id_pub.lo &&
		lhs->ae_oid.id_pub.hi == rhs->ae_oid.id_pub.hi)
		return true;
	return false;
*/
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
	return rc;
}

static int
agg_akey(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry, unsigned int *acts)
{
	int rc = 0;

	agg_entry->ae_akey	= entry->ie_key;
	agg_entry->ae_hdl	= ih;
	return rc;
}

/*
static void
agg_clear_stripes(struct ec_agg_entry *agg_entry)
{
	struct ec_agg_stripe *agg_stripe, *tmp;
	struct ec_agg_extent *agg_extent, *ext_tmp;

	d_list_for_each_entry_safe(agg_stripe, tmp, &agg_entry->ae_stripes,
				   as_link) {

		d_list_for_each_entry_safe(agg_extent, ext_tmp,
					   &agg_stripe->as_dextents, ae_link) {
			d_list_del(&agg_extent->ae_link);
			D_FREE_PTR(agg_extent);
		}
		d_list_del(&agg_stripe->as_link);
		D_FREE_PTR(agg_stripe);
	}
}
*/

static void
agg_clear_extents(struct ec_agg_entry *agg_entry)
{
	struct ec_agg_extent *agg_extent, *ext_tmp;

	d_list_for_each_entry_safe(agg_extent, ext_tmp,
				   &agg_entry->ae_current_stripe.as_dextents,
				   ae_link) {
		d_list_del(&agg_extent->ae_link);
		D_FREE_PTR(agg_extent);
	}
}


static int
agg_akey_post(daos_handle_t ih, vos_iter_entry_t *entry,
	 struct ec_agg_entry *agg_entry,
	 unsigned int *acts)
{
	int rc = 0;

	//D_ASSERT(!agg_key_compare(agg_entry->ae_akey, entry->ie_key));
	//	D_PRINT("clearing stripes\n");
	//	agg_clear_stripes(agg_entry);
	return rc;
}

static inline void
recx2ext(daos_recx_t *recx, struct evt_extent *ext)
{
	D_ASSERT(recx->rx_nr > 0);
	ext->ex_lo = recx->rx_idx;
	ext->ex_hi = recx->rx_idx + recx->rx_nr - 1;
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

	D_PRINT("PRE\n");
	D_ASSERT(type == VOS_ITER_RECX);
	agg_entry->ae_par_extent.ape_recx = entry->ie_recx;
	agg_entry->ae_par_extent.ape_epoch = entry->ie_epoch;
	D_PRINT("epoch: %lu\n", entry->ie_epoch);

	return rc;
}
/*
static int
agg_recx_iter_post_cb(daos_handle_t ih, vos_iter_entry_t *entry,
		     vos_iter_type_t type, vos_iter_param_t *param,
		     void *cb_arg, unsigned int *acts)
{
	struct ec_agg_entry	*agg_entry = (struct ec_agg_entry *) cb_arg;
	int			 rc = 0;

	D_PRINT("POST\n");
	D_ASSERT(type == VOS_ITER_RECX);
	agg_entry->ae_par_extent.ape_recx = entry->ie_recx;
	agg_entry->ae_par_extent.ape_epoch = entry->ie_epoch;
	D_PRINT("epoch: %lu\n", entry->ie_epoch);

	return rc;
}
*/
static int
agg_process_stripe(struct ec_agg_entry *entry)
{
	vos_iter_param_t	 iter_param = { 0 };
	struct vos_iter_anchors  anchors = { 0 };
	int			 rc = 0;

	entry->ae_par_extent.ape_recx.rx_idx = ~(0ULL);
	entry->ae_par_extent.ape_recx.rx_nr = ~(0ULL);
	entry->ae_par_extent.ape_epoch = ~(0ULL);

	iter_param.ip_hdl		= DAOS_HDL_INVAL;
	iter_param.ip_ih		= entry->ae_hdl;
	/*
	iter_param.ip_oid		= entry->ae_oid;
	iter_param.ip_dkey		= entry->ae_dkey;
	iter_param.ip_akey		= entry->ae_akey;
	iter_param.ip_epr.epr_lo	= 0ULL;
	iter_param.ip_epr.epr_hi	= DAOS_EPOCH_MAX;
	//iter_param.ip_epc_expr		= VOS_IT_EPC_RR;
	*/
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;
	iter_param.ip_recx.rx_idx = PARITY_INDICATOR |
		(entry->ae_current_stripe.as_stripenum *
			entry->ae_oca->u.ec.e_len);
	iter_param.ip_recx.rx_nr = entry->ae_oca->u.ec.e_len;

	D_PRINT("Querying parity for stripe: %lu, offset: %lu\n",
		entry->ae_current_stripe.as_stripenum,
		iter_param.ip_recx.rx_idx & (unsigned long)(~PARITY_INDICATOR));
		//iter_param.ip_recx.rx_nr);

	rc = vos_iterate(&iter_param, VOS_ITER_RECX, false, &anchors,
			 agg_recx_iter_pre_cb, NULL, entry);
	D_PRINT("Parity query rc: %d, epoch: %lu, offset: %lu, length: %lu\n",
		rc, entry->ae_par_extent.ape_epoch,
		entry->ae_par_extent.ape_recx.rx_idx,
		entry->ae_par_extent.ape_recx.rx_nr);
	agg_clear_extents(entry);
	return rc;
}


static int
agg_data_extent(vos_iter_entry_t *entry, struct ec_agg_entry *agg_entry,
		daos_handle_t ih)
{
	//struct ec_agg_stripe	*stripe = NULL;
	struct ec_agg_extent	*extent = NULL;
	//bool			 add_stripe = false;
	int			 rc = 0;

	/*
	if (d_list_empty(&agg_entry->ae_stripes))
		add_stripe = true;
	else {
		stripe = d_list_entry(agg_entry->ae_stripes.prev,
				      struct ec_agg_stripe, as_link);
		if (stripe->as_stripenum !=
				agg_stripenum(agg_entry, entry->ie_recx.rx_idx))
			add_stripe = true;
	}
	if (add_stripe) {
		D_ALLOC_PTR(stripe);
		if (stripe == NULL) {
			rc = -DER_NOMEM;
			goto out;
		} else {
			stripe->as_stripenum =
				agg_stripenum(agg_entry,
					      entry->ie_recx.rx_idx);

			D_INIT_LIST_HEAD(&stripe->as_dextents);
			d_list_add_tail(&stripe->as_link,
					&agg_entry->ae_stripes);
		}
		D_PRINT("adding stripe  data: %lu, stripe: %lu\n",
			entry->ie_recx.rx_idx, stripe->as_stripenum);
	}
	*/
	if (agg_stripenum(agg_entry, entry->ie_recx.rx_idx) !=
			agg_entry->ae_current_stripe.as_stripenum) {
		agg_process_stripe(agg_entry);
		agg_entry->ae_current_stripe.as_stripenum =
			agg_stripenum(agg_entry,entry->ie_recx.rx_idx);
		D_PRINT("stripenum: %lu\n",
			agg_entry->ae_current_stripe.as_stripenum);
	}
	D_ALLOC_PTR(extent);
	if (extent == NULL) {
		rc = -DER_NOMEM;
		goto out;
	} else {
		extent->ae_recx = entry->ie_recx;
		extent->ae_epoch = entry->ie_epoch;
		d_list_add_tail(&extent->ae_link,
				&agg_entry->ae_current_stripe.as_dextents);
	}
	D_PRINT("adding extent %lu,%lu, to stripe  %lu\n",
		extent->ae_recx.rx_idx, extent->ae_recx.rx_nr,
		agg_stripenum(agg_entry, extent->ae_recx.rx_idx));
out:
	return rc;
}

/*
static bool
agg_parity_covers(vos_iter_entry_t *entry, struct ec_agg_entry *agg_entry)
{
	return true;
}

static int
agg_parity_extent(vos_iter_entry_t *entry, struct ec_agg_entry *agg_entry)
{
	daos_off_t		 ext_stripe;
	int			 rc = 0;

	if (!agg_entry->ae_iteration) {
		if (d_list_empty(&agg_entry->ae_stripes))
			return 1;
		agg_entry->ae_iteration = true;
		agg_entry->ae_current_stripe =
			d_list_entry(agg_entry->ae_stripes.next,
				   struct ec_agg_stripe, as_link);
		D_PRINT("Init current stripe: %lu\n",
			agg_entry->ae_current_stripe->as_stripenum);
	}
	ext_stripe = agg_stripenum(agg_entry,
				   entry->ie_recx.rx_idx & (~PARITY_INDICATOR));
        //agg_stripe = d_list_entry(agg_entry->ae_stripe_iter.next,
				   //struct ec_agg_stripe, as_link);
	//D_PRINT("ext_stripe: %lu, current stripe: %lu\n", ext_stripe,
			//agg_entry->ae_current_stripe->as_stripenum);
				 //entry->ie_recx.rx_idx & (~PARITY_INDICATOR));
	if (agg_entry->ae_current_stripe->as_stripenum > ext_stripe) {
		//D_PRINT("current stripe > ext_stripe\n");
		goto out;
	} else if (agg_entry->ae_current_stripe->as_stripenum == ext_stripe) {
		agg_entry->ae_par_extent.ape_recx = entry->ie_recx;
		agg_entry->ae_par_extent.ape_epoch = entry->ie_epoch;
		goto out;
	}

	d_list_for_each_entry_continue(agg_entry->ae_current_stripe,
					&agg_entry->ae_stripes, as_link) {
	//	D_PRINT("inloop: ext_stripe: %lu, agg_stripe: %lu\n",
		//ext_stripe, agg_entry->ae_current_stripe->as_stripenum);
		if (agg_entry->ae_current_stripe->as_stripenum < ext_stripe) {
			continue;
		} else if (ext_stripe ==
				agg_entry->ae_current_stripe->as_stripenum) {
			agg_entry->ae_par_extent.ape_recx = entry->ie_recx;
			agg_entry->ae_par_extent.ape_epoch = entry->ie_epoch;
			//if (agg_parity_covers(entry, agg_entry))
	//		D_PRINT("Parity covers data: %lu\n", ext_stripe);
			break;
		} else {
	//		D_PRINT("breaking out of loop\n");
			break;
		}
	//	D_PRINT("Loop current stripe: %lu\n",
	//		agg_entry->ae_current_stripe->as_stripenum);
	}
out:
	return rc;
}
*/
static int
agg_ev(daos_handle_t ih, vos_iter_entry_t *entry,
       struct ec_agg_entry *agg_entry, unsigned int *acts)
{
        struct evt_extent	ext;
	int			rc = 0;

        recx2ext(&entry->ie_recx, &ext);
	if (!(ext.ex_lo & PARITY_INDICATOR)) {
		rc = agg_data_extent(entry, agg_entry, ih);
		//D_PRINT(" adding stripe  data: ex_lo: %lu, ex_hi: %lu\n",
		//	ext.ex_lo, ext.ex_hi);
	} else
		D_PRINT("Outer iter parity, stripe: %lu, address: %lu\n",
			agg_stripenum(agg_entry, entry->ie_recx.rx_idx &
				      (~PARITY_INDICATOR)),
			entry->ie_recx.rx_idx &
				      (unsigned long)(~PARITY_INDICATOR));

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
		//D_PRINT("pre type: %u\n", type);
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
		rc = agg_dkey(ih, entry, agg_entry, acts);
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
	iter_param.ip_epr.epr_lo	= agg_entry->ae_epoch_lo;
	iter_param.ip_epr.epr_hi	= agg_entry->ae_epoch;
	//iter_param.ip_recx.rx_idx	= 1 << 16;
	//iter_param.ip_recx.rx_nr	= 1 << 18;
	iter_param.ip_epc_expr		= VOS_IT_EPC_RR;
	iter_param.ip_flags		= VOS_IT_RECX_VISIBLE;

	D_PRINT("VOS Iterate!\n");
	rc = vos_iterate(&iter_param, VOS_ITER_DKEY, true, &anchors,
			 agg_iterate_pre_cb, agg_iterate_post_cb, agg_entry);
	D_PRINT("VOS Iterate complete\n");
	return rc;

}

int
ds_obj_ec_aggregate(struct ds_cont_child *cont, daos_epoch_t start_epoch)
{
	struct ec_agg_set	  agg_set = { 0 };
	struct ec_agg_entry	 *agg_entry;
	struct ec_agg_entry	**agg_entry_array = NULL;
	int			  rc = 0;

	uuid_copy(agg_set.as_pool_uuid, cont->sc_pool->spc_uuid);
	agg_set.as_pool_version = cont->sc_pool->spc_pool->sp_map_version;
	agg_set.as_start_epoch = start_epoch;
	D_INIT_LIST_HEAD(&agg_set.as_entries);
	rc = vos_agg_iterate(cont->sc_hdl, committed_dtx_cb, &agg_set);
	D_PRINT("rc: %d, Count: %u\n", rc, agg_set.as_count);
	if (agg_set.as_count) {
		struct ec_agg_entry	*prev_agg_entry = NULL;
		int			 i = 0;

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
//		D_PRINT("lo: %lu\n", lo);
		for (i = 0; i < agg_set.as_count; i++) {
			agg_entry = agg_entry_array[i];
			if (agg_entry->ae_epoch > start_epoch)
				start_epoch = agg_entry->ae_epoch;
//			D_PRINT("agg_entry->ae_epoch: %lu\n", agg_entry->ae_epoch);
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
//			D_PRINT("prev_agg_entry - lo: %lu\n",
//				prev_agg_entry->ae_epoch_lo);
		}
		prev_agg_entry->ae_epoch_lo = lo;
		D_FREE(agg_entry_array);
		d_list_for_each_entry(agg_entry, &agg_set.as_entries, ae_link) {
			D_INIT_LIST_HEAD(&agg_entry->
					 ae_current_stripe.as_dextents);
/*
			D_PRINT("agg_entry: lo: %lu, high: %lu\n",
				agg_entry->ae_epoch_lo, agg_entry->ae_epoch);
*/
			rc = agg_iterate(agg_entry, cont->sc_hdl);
			if (rc != 0)
				goto out;
		}
	} else
		sleep(10);
out:
	if (rc)
		return rc;
	else
		return start_epoch;
}
