/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * This file is part of daos
 *
 * vos/vos_obj.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

#define ITER_KEY_SIZE		2048

/** iterator for dkey/akey/recx */
struct vos_obj_iter {
	/* public part of the iterator */
	struct vos_iterator	 it_iter;
	/** handle of iterator */
	daos_handle_t		 it_hdl;
	/** condition of the iterator: epoch logic expression */
	vos_it_epc_expr_t	 it_epc_expr;
	/** condition of the iterator: epoch range */
	daos_epoch_range_t	 it_epr;
	/** condition of the iterator: attribute key */
	daos_key_t		 it_akey;
	/** XXX workaround, buffer to store the previous key */
	char			 it_key_prev[ITER_KEY_SIZE];
	/** length of previous key */
	int			 it_key_len;
	/* reference on the object */
	struct vos_object	*it_obj;
};

static struct vos_obj_iter *
vos_iter2oiter(struct vos_iterator *iter)
{
	return container_of(iter, struct vos_obj_iter, it_iter);
}

struct vos_obj_iter*
vos_hdl2oiter(daos_handle_t hdl)
{
	return vos_iter2oiter(vos_hdl2iter(hdl));
}

/**
 * @} vos_tree_helper
 */

static int
key_punch(struct vos_object *obj, daos_epoch_t epoch, uuid_t cookie,
	  uint32_t pm_ver, daos_key_t *dkey, unsigned int akey_nr,
	  daos_key_t *akeys, uint32_t flags)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_csum_buf_t		csum;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	int			rc;

	rc = obj_tree_init(obj);
	if (rc)
		D_GOTO(out, rc);

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch = epoch;

	tree_rec_bundle2iov(&rbund, &riov);
	uuid_copy(rbund.rb_cookie, cookie);
	rbund.rb_mmid	= UMMID_NULL;
	rbund.rb_ver	= pm_ver;
	rbund.rb_csum	= &csum;
	memset(&csum, 0, sizeof(csum));

	if (!akeys) {
		kbund.kb_key = dkey;
		rbund.rb_iov = dkey;
		rbund.rb_tclass	= VOS_BTR_DKEY;

		rc = key_tree_punch(obj, obj->obj_toh, &kiov, &riov, flags);
		if (rc != 0)
			D_GOTO(out, rc);

	} else {
		daos_handle_t	toh;
		int		i;

		rc = key_tree_prepare(obj, epoch, obj->obj_toh, VOS_BTR_DKEY,
				      dkey, SUBTR_CREATE, &toh);
		if (rc)
			D_GOTO(out, rc); /* real failure */

		rbund.rb_tclass	= VOS_BTR_AKEY;
		for (i = 0; i < akey_nr; i++) {
			kbund.kb_key = &akeys[i];
			rbund.rb_iov = &akeys[i];
			rc = key_tree_punch(obj, toh, &kiov, &riov, flags);
			if (rc != 0)
				break;
		}
		key_tree_release(toh, 0);
	}
 out:
	return rc;
}

static int
obj_punch(daos_handle_t coh, struct vos_object *obj, daos_epoch_t epoch,
	  uuid_t cookie, uint32_t flags)
{
	struct vos_container	*cont;
	int			 rc;

	cont = vos_hdl2cont(coh);
	rc = vos_oi_punch(cont, obj->obj_id, epoch, flags, obj->obj_df);
	if (rc)
		D_GOTO(failed, rc);

	/* evict it from catch, because future fetch should only see empty
	 * object (without obj_df)
	 */
	vos_obj_evict(obj);
failed:
	return rc;
}

/**
 * Punch an object, or punch a dkey, or punch an array of akeys.
 */
int
vos_obj_punch(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
	      uuid_t cookie, uint32_t pm_ver, uint32_t flags, daos_key_t *dkey,
	      unsigned int akey_nr, daos_key_t *akeys)
{
	PMEMobjpool	  *pop;
	struct vos_object *obj;
	int		   rc;

	D_DEBUG(DB_IO, "Punch "DF_UOID", cookie "DF_UUID" epoch "
		DF_U64"\n", DP_UOID(oid), DP_UUID(cookie), epoch);

	/* NB: punch always generate a new incarnation of the object */
	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch,
			  false, &obj);
	if (rc != 0)
		return rc;

	pop = vos_obj2pop(obj);
	TX_BEGIN(pop) {
		if (dkey) { /* key punch */
			rc = key_punch(obj, epoch, cookie, pm_ver, dkey,
				       akey_nr, akeys, flags);
		} else { /* object punch */
			rc = obj_punch(coh, obj, epoch, cookie, flags);
		}

	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DB_IO, "Failed to punch object: %d\n", rc);
	} TX_END

	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}

/**
 * @defgroup vos_obj_iters VOS object iterators
 * @{
 *
 * - iterate d-key
 * - iterate a-key (array)
 * - iterate recx
 */
static int
key_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *ent,
	       daos_anchor_t *anchor)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	daos_csum_buf_t		csum;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	tree_rec_bundle2iov(&rbund, &riov);

	rbund.rb_iov	= &ent->ie_key;
	rbund.rb_csum	= &csum;

	daos_iov_set(rbund.rb_iov, NULL, 0); /* no copy */
	daos_csum_set(rbund.rb_csum, NULL, 0);

	rc = dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
	if (rc == 0) {
		D_ASSERT(rbund.rb_krec);
		ent->ie_epoch = rbund.rb_krec->kr_punched;
	}
	return rc;
}

/**
 * Check if the current entry can match the iterator condition, this function
 * retuns IT_OPC_NOOP for true, returns IT_OPC_NEXT or IT_OPC_PROBE if further
 * operation is required. If IT_OPC_PROBE is returned, then the key to be
 * probed and its epoch range are also returned to @ent.
 */
static int
key_iter_match(struct vos_obj_iter *oiter, vos_iter_entry_t *ent, int *probe_p)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_epoch_range_t	*epr = &oiter->it_epr;
	struct vos_key_bundle	 kbund;
	struct vos_rec_bundle	 rbund;
	daos_handle_t		 toh;
	daos_iov_t		 kiov;
	daos_iov_t		 riov;
	int			 probe;
	int			 rc;

	rc = key_iter_fetch(oiter, ent, NULL);
	if (rc) {
		D_ERROR("Failed to fetch the entry: %d\n", rc);
		return rc;
	}

	probe = 0;
	if (ent->ie_epoch <= epr->epr_lo) {
		probe = BTR_PROBE_GT;
		ent->ie_epoch = epr->epr_lo;

	} else if (ent->ie_epoch > epr->epr_hi) {
		daos_key_t *key = &ent->ie_key;

		if (key->iov_len != oiter->it_key_len ||
		    memcmp(key->iov_buf, oiter->it_key_prev, key->iov_len)) {
			/* previous key is not the same key, it's a match.
			 * XXX this is a workaround, we just copy the whole
			 * key and always assume it can fit into the buffer.
			 */
			D_ASSERT(key->iov_len < ITER_KEY_SIZE);
			memcpy(oiter->it_key_prev, key->iov_buf, key->iov_len);
			oiter->it_key_len = key->iov_len;
		} else {
			/* GT + EPOCH_MAX will effectively probe the next key */
			ent->ie_epoch = DAOS_EPOCH_MAX;
			probe = BTR_PROBE_GT;
		}
	}

	if (probe != 0) {
		/* not in the range, need further operation */
		*probe_p = probe;
		return IT_OPC_PROBE;
	}

	if ((oiter->it_iter.it_type == VOS_ITER_AKEY) ||
	    (oiter->it_akey.iov_buf == NULL)) /* dkey w/o akey as condition */
		return IT_OPC_NOOP;

	/* else: has akey as condition */
	if (epr->epr_lo != epr->epr_hi) {
		D_ERROR("Cannot support epoch range for conditional iteration "
			"because it is not clearly defined.\n");
		return -DER_INVAL; /* XXX simplify it for now */
	}

	rc = key_tree_prepare(obj, ent->ie_epoch, obj->obj_toh, VOS_BTR_DKEY,
			      &ent->ie_key, 0, &toh);
	if (rc != 0) {
		D_DEBUG(DB_IO, "can't load the akey tree: %d\n", rc);
		return rc;
	}

	/* check if the akey exists */
	tree_rec_bundle2iov(&rbund, &riov);
	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_key = &oiter->it_akey;
	kbund.kb_epoch = epr->epr_lo;

	rc = dbtree_fetch(toh, BTR_PROBE_GT | BTR_PROBE_MATCHED, &kiov, NULL,
			  &riov);
	key_tree_release(toh, false);
	if (rc == 0)
		return IT_OPC_NOOP; /* match the condition (akey), done */

	if (rc == -DER_NONEXIST)
		return IT_OPC_NEXT; /* no matched akey */

	return rc;
}

/**
 * Check if the current item can match the provided condition (with the
 * giving a-key). If the item can't match the condition, this function
 * traverses the tree until a matched item is found.
 */
static int
key_iter_match_probe(struct vos_obj_iter *oiter)
{
	int	rc;

	while (1) {
		vos_iter_entry_t	entry;
		struct vos_key_bundle	kbund;
		daos_iov_t		kiov;
		int			opc = 0;

		rc = key_iter_match(oiter, &entry, &opc);
		switch (rc) {
		default:
			D_ASSERT(rc < 0);
			D_ERROR("match failed, rc=%d\n", rc);
			goto out;

		case IT_OPC_NOOP:
			/* already match the condition, no further operation */
			rc = 0;
			goto out;

		case IT_OPC_PROBE:
			/* probe the returned key and epoch range */
			tree_key_bundle2iov(&kbund, &kiov);
			kbund.kb_key	= &entry.ie_key;
			kbund.kb_epoch	= entry.ie_epoch;
			rc = dbtree_iter_probe(oiter->it_hdl, opc, &kiov, NULL);
			if (rc)
				goto out;
			break;

		case IT_OPC_NEXT:
			/* move to the next tree record */
			rc = dbtree_iter_next(oiter->it_hdl);
			if (rc)
				goto out;
			break;
		}
	}
 out:
	return rc;
}

static int
key_iter_probe(struct vos_obj_iter *oiter, daos_anchor_t *anchor)
{
	int	rc;

	rc = dbtree_iter_probe(oiter->it_hdl,
			       anchor ? BTR_PROBE_GE : BTR_PROBE_FIRST,
			       NULL, anchor);
	if (rc)
		D_GOTO(out, rc);

	rc = key_iter_match_probe(oiter);
	if (rc)
		D_GOTO(out, rc);
 out:
	return rc;
}

static int
key_iter_next(struct vos_obj_iter *oiter)
{
	int	rc;

	rc = dbtree_iter_next(oiter->it_hdl);
	if (rc)
		D_GOTO(out, rc);

	rc = key_iter_match_probe(oiter);
	if (rc)
		D_GOTO(out, rc);
out:
	return rc;
}

/**
 * Iterator for the d-key tree.
 */
static int
dkey_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *akey)
{
	/* optional condition, d-keys with the provided attribute (a-key) */
	oiter->it_akey = *akey;

	return dbtree_iter_prepare(oiter->it_obj->obj_toh, 0, &oiter->it_hdl);
}

/**
 * Iterator for the akey tree.
 */
static int
akey_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_handle_t		 toh;
	int			 rc;

	rc = key_tree_prepare(obj, oiter->it_epr.epr_lo, obj->obj_toh,
			      VOS_BTR_DKEY, dkey, 0, &toh);
	if (rc != 0) {
		D_ERROR("Cannot load the akey tree: %d\n", rc);
		return rc;
	}

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc)
		D_GOTO(out, rc);

	key_tree_release(toh, false);
out:
	return rc;
}

/**
 * Record extent (recx) iterator
 */

/**
 * Record extent (recx) iterator
 */
static int singv_iter_fetch(struct vos_obj_iter *oiter,
			   vos_iter_entry_t *it_entry,
			   daos_anchor_t *anchor);
/**
 * Prepare the iterator for the recx tree.
 */
static int
singv_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		   daos_key_t *akey)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_handle_t		 dk_toh;
	daos_handle_t		 ak_toh;
	int			 rc;

	rc = key_tree_prepare(obj, oiter->it_epr.epr_hi, obj->obj_toh,
			      VOS_BTR_DKEY, dkey, 0, &dk_toh);
	if (rc != 0)
		D_GOTO(failed_0, rc);

	rc = key_tree_prepare(obj, oiter->it_epr.epr_hi, dk_toh,
			      VOS_BTR_AKEY, akey, 0, &ak_toh);
	if (rc != 0)
		D_GOTO(failed_1, rc);

	/* see BTR_ITER_EMBEDDED for the details */
	rc = dbtree_iter_prepare(ak_toh, BTR_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc != 0) {
		D_DEBUG(DB_IO, "Cannot prepare singv iterator: %d\n", rc);
		D_GOTO(failed_2, rc);
	}
 failed_2:
	key_tree_release(ak_toh, false);
 failed_1:
	key_tree_release(dk_toh, false);
 failed_0:
	return rc;
}

/**
 * Probe the single value based on @opc and conditions in @entry (epoch),
 * return the matched one to @entry.
 */
static int
singv_iter_probe_fetch(struct vos_obj_iter *oiter, dbtree_probe_opc_t opc,
		       vos_iter_entry_t *entry)
{
	struct vos_key_bundle	kbund;
	daos_iov_t		kiov;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch = entry->ie_epoch;

	rc = dbtree_iter_probe(oiter->it_hdl, opc, &kiov, NULL);
	if (rc != 0)
		return rc;

	memset(entry, 0, sizeof(*entry));
	rc = singv_iter_fetch(oiter, entry, NULL);
	return rc;
}

/**
 * Find the data that was written before/in the specified epoch of @oiter
 * for the recx in @entry. If this recx has no data for this epoch, then
 * this function will move on to the next recx and repeat this process.
 */
static int
singv_iter_probe_epr(struct vos_obj_iter *oiter, vos_iter_entry_t *entry)
{
	daos_epoch_range_t *epr = &oiter->it_epr;

	while (1) {
		int	opc;
		int	rc;

		switch (oiter->it_epc_expr) {
		default:
			return -DER_INVAL;

		case VOS_IT_EPC_EQ:
			if (entry->ie_epoch > epr->epr_hi)
				return -DER_NONEXIST;

			if (entry->ie_epoch < epr->epr_lo) {
				entry->ie_epoch = epr->epr_lo;
				opc = BTR_PROBE_EQ;
				break;
			}
			return 0;

		case VOS_IT_EPC_RE:
			if (entry->ie_epoch > epr->epr_hi)
				return -DER_NONEXIST;

			if (entry->ie_epoch < epr->epr_lo) {
				entry->ie_epoch = epr->epr_lo;
				opc = BTR_PROBE_GE;
				break;
			}
			return 0;

		case VOS_IT_EPC_RR:
			if (entry->ie_epoch < epr->epr_lo)
				return -DER_NONEXIST; /* end of story */

			if (entry->ie_epoch > epr->epr_hi) {
				entry->ie_epoch = epr->epr_hi;
				opc = BTR_PROBE_LE;
				break;
			}
			return 0;

		case VOS_IT_EPC_GE:
			if (entry->ie_epoch < epr->epr_lo) {
				entry->ie_epoch = epr->epr_lo;
				opc = BTR_PROBE_GE;
				break;
			}
			return 0;

		case VOS_IT_EPC_LE:
			if (entry->ie_epoch > epr->epr_lo) {
				epr->epr_lo = epr->epr_lo;
				opc = BTR_PROBE_LE;
				break;
			}
			return 0;
		}
		rc = singv_iter_probe_fetch(oiter, opc, entry);
		if (rc != 0)
			return rc;
	}
}

static int
singv_iter_probe(struct vos_obj_iter *oiter, daos_anchor_t *anchor)
{
	vos_iter_entry_t	entry;
	daos_anchor_t		tmp;
	int			opc;
	int			rc;

	if (oiter->it_epc_expr == VOS_IT_EPC_RR)
		opc = anchor == NULL ? BTR_PROBE_LAST : BTR_PROBE_LE;
	else
		opc = anchor == NULL ? BTR_PROBE_FIRST : BTR_PROBE_GE;

	rc = dbtree_iter_probe(oiter->it_hdl, opc, NULL, anchor);
	if (rc != 0)
		return rc;

	memset(&entry, 0, sizeof(entry));
	rc = singv_iter_fetch(oiter, &entry, &tmp);
	if (rc != 0)
		return rc;

	if (anchor != NULL) {
		if (memcmp(anchor, &tmp, sizeof(tmp)) == 0)
			return 0;

		D_DEBUG(DB_IO, "Can't find the provided anchor\n");
		/**
		 * the original recx has been merged/discarded, so we need to
		 * call singv_iter_probe_epr() and check if the current record
		 * can match the condition.
		 */
	}
	rc = singv_iter_probe_epr(oiter, &entry);
	return rc;
}

static int
singv_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		 daos_anchor_t *anchor)
{
	struct vos_key_bundle	kbund;
	struct vos_rec_bundle	rbund;
	daos_iov_t		kiov;
	daos_iov_t		riov;
	int			rc;

	tree_key_bundle2iov(&kbund, &kiov);
	kbund.kb_epoch	= it_entry->ie_epoch;

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_eiov	= &it_entry->ie_eiov;
	rbund.rb_csum	= &it_entry->ie_csum;

	memset(&it_entry->ie_eiov, 0, sizeof(it_entry->ie_eiov));
	daos_csum_set(rbund.rb_csum, NULL, 0);

	rc = dbtree_iter_fetch(oiter->it_hdl, &kiov, &riov, anchor);
	if (rc)
		D_GOTO(out, rc);

	uuid_copy(it_entry->ie_cookie, rbund.rb_cookie);
	it_entry->ie_epoch	 = kbund.kb_epoch;
	it_entry->ie_rsize	 = rbund.rb_rsize;
	it_entry->ie_ver	 = rbund.rb_ver;
	it_entry->ie_recx.rx_idx = 0;
	it_entry->ie_recx.rx_nr  = 1;
 out:
	return rc;
}

static int
singv_iter_next(struct vos_obj_iter *oiter)
{
	vos_iter_entry_t entry;
	int		 rc;
	int		 opc;

	memset(&entry, 0, sizeof(entry));
	rc = singv_iter_fetch(oiter, &entry, NULL);
	if (rc != 0)
		return rc;

	if (oiter->it_epc_expr == VOS_IT_EPC_RE)
		entry.ie_epoch += 1;
	else if (oiter->it_epc_expr == VOS_IT_EPC_RR)
		entry.ie_epoch -= 1;
	else
		entry.ie_epoch = DAOS_EPOCH_MAX;

	opc = (oiter->it_epc_expr == VOS_IT_EPC_RR) ?
		BTR_PROBE_LE : BTR_PROBE_GE;

	rc = singv_iter_probe_fetch(oiter, opc, &entry);
	if (rc != 0)
		return rc;

	rc = singv_iter_probe_epr(oiter, &entry);
	return rc;
}

/**
 * Prepare the iterator for the recx tree.
 */
static int
recx_iter_prepare(struct vos_obj_iter *oiter, daos_key_t *dkey,
		  daos_key_t *akey)
{
	struct vos_object	*obj = oiter->it_obj;
	daos_handle_t		 dk_toh;
	daos_handle_t		 ak_toh;
	int			 rc;

	rc = key_tree_prepare(obj, oiter->it_epr.epr_hi, obj->obj_toh,
			      VOS_BTR_DKEY, dkey, 0, &dk_toh);
	if (rc != 0)
		D_GOTO(failed_0, rc);

	rc = key_tree_prepare(obj, oiter->it_epr.epr_hi, dk_toh,
			      VOS_BTR_AKEY, akey, SUBTR_EVT, &ak_toh);
	if (rc != 0)
		D_GOTO(failed_1, rc);

	rc = evt_iter_prepare(ak_toh, EVT_ITER_EMBEDDED, &oiter->it_hdl);
	if (rc != 0) {
		D_DEBUG(DB_IO, "Cannot prepare recx iterator : %d\n", rc);
		D_GOTO(failed_2, rc);
	}
 failed_2:
	key_tree_release(ak_toh, true);
 failed_1:
	key_tree_release(dk_toh, false);
 failed_0:
	return rc;
}
static int
recx_iter_probe(struct vos_obj_iter *oiter, daos_anchor_t *anchor)
{
	int	opc;
	int	rc;

	opc = anchor ? EVT_ITER_FIND : EVT_ITER_FIRST;
	rc = evt_iter_probe(oiter->it_hdl, opc, NULL, anchor);
	return rc;
}

static int
recx_iter_fetch(struct vos_obj_iter *oiter, vos_iter_entry_t *it_entry,
		daos_anchor_t *anchor)
{
	struct evt_rect	 *rect;
	struct evt_entry  entry;
	int		  rc;

	rc = evt_iter_fetch(oiter->it_hdl, &entry, anchor);
	if (rc != 0)
		D_GOTO(out, rc);

	memset(it_entry, 0, sizeof(*it_entry));

	rect = &entry.en_rect;
	it_entry->ie_epoch	 = rect->rc_epc_lo;
	it_entry->ie_recx.rx_idx = rect->rc_off_lo;
	it_entry->ie_recx.rx_nr	 = rect->rc_off_hi - rect->rc_off_lo + 1;
	it_entry->ie_rsize	 = entry.en_ptr.pt_inob;
	uuid_copy(it_entry->ie_cookie, entry.en_ptr.pt_cookie);
	it_entry->ie_ver	= entry.en_ptr.pt_ver;
 out:
	return rc;
}

static int
recx_iter_next(struct vos_obj_iter *oiter)
{
	return evt_iter_next(oiter->it_hdl);
}

static int
recx_iter_fini(struct vos_obj_iter *oiter)
{
	return evt_iter_finish(oiter->it_hdl);
}

/**
 * common functions for iterator.
 */
static int vos_obj_iter_fini(struct vos_iterator *vitr);

/** prepare an object content iterator */
int
vos_obj_iter_prep(vos_iter_type_t type, vos_iter_param_t *param,
		  struct vos_iterator **iter_pp)
{
	struct vos_obj_iter *oiter;
	int		     rc;

	D_ALLOC_PTR(oiter);
	if (oiter == NULL)
		return -DER_NOMEM;

	oiter->it_epr = param->ip_epr;
	/* XXX the condition epoch ranges could cover multiple versions of
	 * the object/key if it's punched more than once. However, rebuild
	 * system should guarantee this will never happen.
	 */
	rc = vos_obj_hold(vos_obj_cache_current(), param->ip_hdl,
			  param->ip_oid, param->ip_epr.epr_hi, true,
			  &oiter->it_obj);
	if (rc != 0)
		D_GOTO(failed, rc);

	if (vos_obj_is_empty(oiter->it_obj)) {
		D_DEBUG(DB_IO, "Empty object, nothing to iterate\n");
		D_GOTO(failed, rc = -DER_NONEXIST);
	}

	rc = obj_tree_init(oiter->it_obj);
	if (rc != 0)
		goto failed;

	switch (type) {
	default:
		D_ERROR("unknown iterator type %d.\n", type);
		rc = -DER_INVAL;
		break;

	case VOS_ITER_DKEY:
		rc = dkey_iter_prepare(oiter, &param->ip_akey);
		break;

	case VOS_ITER_AKEY:
		rc = akey_iter_prepare(oiter, &param->ip_dkey);
		break;

	case VOS_ITER_SINGLE:
		oiter->it_epc_expr = param->ip_epc_expr;
		rc = singv_iter_prepare(oiter, &param->ip_dkey,
					&param->ip_akey);
		break;

	case VOS_ITER_RECX:
		rc = recx_iter_prepare(oiter, &param->ip_dkey, &param->ip_akey);
		break;
	}

	if (rc != 0)
		D_GOTO(failed, rc);

	*iter_pp = &oiter->it_iter;
	return 0;
 failed:
	vos_obj_iter_fini(&oiter->it_iter);
	return rc;
}

/** release the object iterator */
static int
vos_obj_iter_fini(struct vos_iterator *iter)
{
	struct vos_obj_iter	*oiter = vos_iter2oiter(iter);
	int			 rc;

	if (daos_handle_is_inval(oiter->it_hdl))
		D_GOTO(out, rc = -DER_NO_HDL);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		break;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		rc = dbtree_iter_finish(oiter->it_hdl);
		break;

	case VOS_ITER_RECX:
		rc = recx_iter_fini(oiter);
		break;
	}
 out:
	if (oiter->it_obj != NULL)
		vos_obj_release(vos_obj_cache_current(), oiter->it_obj);

	D_FREE_PTR(oiter);
	return 0;
}

int
vos_obj_iter_probe(struct vos_iterator *iter, daos_anchor_t *anchor)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_probe(oiter, anchor);

	case VOS_ITER_SINGLE:
		return singv_iter_probe(oiter, anchor);

	case VOS_ITER_RECX:
		return recx_iter_probe(oiter, anchor);
	}
}

static int
vos_obj_iter_next(struct vos_iterator *iter)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_next(oiter);

	case VOS_ITER_SINGLE:
		return singv_iter_next(oiter);

	case VOS_ITER_RECX:
		return recx_iter_next(oiter);
	}
}

static int
vos_obj_iter_fetch(struct vos_iterator *iter, vos_iter_entry_t *it_entry,
		   daos_anchor_t *anchor)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
		return key_iter_fetch(oiter, it_entry, anchor);

	case VOS_ITER_SINGLE:
		return singv_iter_fetch(oiter, it_entry, anchor);

	case VOS_ITER_RECX:
		return recx_iter_fetch(oiter, it_entry, anchor);
	}
}

static int
obj_iter_delete(struct vos_obj_iter *oiter, void *args)
{
	int		rc = 0;
	PMEMobjpool	*pop;

	D_DEBUG(DB_TRACE, "BTR delete called of obj\n");
	pop = vos_obj2pop(oiter->it_obj);

	TX_BEGIN(pop) {
		rc = dbtree_iter_delete(oiter->it_hdl, args);
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_ERROR("Failed to delete iter entry: %d\n", rc);
	} TX_END

	return rc;
}

static int
vos_obj_iter_delete(struct vos_iterator *iter, void *args)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;

	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		return obj_iter_delete(oiter, args);

	case VOS_ITER_RECX:
		return -DER_NOSYS;
	}
}

static int
vos_obj_iter_empty(struct vos_iterator *iter)
{
	struct vos_obj_iter *oiter = vos_iter2oiter(iter);

	if (daos_handle_is_inval(oiter->it_hdl))
		return -DER_NO_HDL;

	switch (iter->it_type) {
	default:
		D_ASSERT(0);
		return -DER_INVAL;
	case VOS_ITER_DKEY:
	case VOS_ITER_AKEY:
	case VOS_ITER_SINGLE:
		return dbtree_iter_empty(oiter->it_hdl);
	case VOS_ITER_RECX:
		return -DER_NOSYS;
	}
}

struct vos_iter_ops	vos_obj_iter_ops = {
	.iop_prepare	= vos_obj_iter_prep,
	.iop_finish	= vos_obj_iter_fini,
	.iop_probe	= vos_obj_iter_probe,
	.iop_next	= vos_obj_iter_next,
	.iop_fetch	= vos_obj_iter_fetch,
	.iop_delete	= vos_obj_iter_delete,
	.iop_empty	= vos_obj_iter_empty,
};
/**
 * @} vos_obj_iters
 */

static int
vos_oi_set_attr_helper(daos_handle_t coh, daos_unit_oid_t oid,
		       daos_epoch_t epoch, uint64_t attr, bool set)
{
	PMEMobjpool	  *pop;
	struct vos_object *obj;
	int		   rc;

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, false,
			  &obj);
	if (rc != 0)
		return rc;

	pop = vos_obj2pop(obj);
	TX_BEGIN(pop) {
		rc = umem_tx_add_ptr(vos_obj2umm(obj), &obj->obj_df->vo_oi_attr,
				     sizeof(obj->obj_df->vo_oi_attr));
		if (set) {
			obj->obj_df->vo_oi_attr |= attr;
		} else {
			/* Only clear bits that are set */
			uint64_t to_clear = attr & obj->obj_df->vo_oi_attr;

			obj->obj_df->vo_oi_attr ^= to_clear;
		}
	} TX_ONABORT {
		rc = umem_tx_errno(rc);
		D_DEBUG(DB_IO, "Failed to set attributes on object: %d\n", rc);
	} TX_END
	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}

int
vos_oi_set_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t attr)
{
	D_DEBUG(DB_IO, "Set attributes "DF_UOID", epoch "DF_U64", attributes "
		 DF_X64"\n", DP_UOID(oid), epoch, attr);

	return vos_oi_set_attr_helper(coh, oid, epoch, attr, true);
}

int
vos_oi_clear_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t attr)
{
	D_DEBUG(DB_IO, "Clear attributes "DF_UOID", epoch "DF_U64
		 ", attributes "DF_X64"\n", DP_UOID(oid), epoch, attr);

	return vos_oi_set_attr_helper(coh, oid, epoch, attr, false);
}

int
vos_oi_get_attr(daos_handle_t coh, daos_unit_oid_t oid, daos_epoch_t epoch,
		uint64_t *attr)
{
	struct vos_object *obj;
	int		   rc = 0;

	D_DEBUG(DB_IO, "Get attributes "DF_UOID", epoch "DF_U64"\n",
		 DP_UOID(oid), epoch);

	if (attr == NULL) {
		D_ERROR("Invalid attribute argument\n");
		return -DER_INVAL;
	}

	rc = vos_obj_hold(vos_obj_cache_current(), coh, oid, epoch, true,
			  &obj);
	if (rc != 0)
		return rc;

	*attr = 0;

	if (obj->obj_df == NULL) /* nothing to do */
		D_GOTO(out, rc = 0);

	*attr = obj->obj_df->vo_oi_attr;

out:
	vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}
