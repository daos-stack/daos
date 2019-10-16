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
 * This file is part of daos
 *
 * vos/vos_query.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree.h>
#include <daos_types.h>
#include <daos_srv/vos.h>
#include <daos_api.h> /* For ofeat bits */
#include <daos/checksum.h>
#include "vos_internal.h"

struct open_query {
	struct vos_object	*qt_obj;
	daos_epoch_range_t	 qt_epr;
	struct ilog_entries	 qt_entries;
	struct btr_root		*qt_dkey_root;
	daos_handle_t		 qt_dkey_toh;
	struct btr_root		*qt_akey_root;
	daos_handle_t		 qt_akey_toh;
	struct evt_root		*qt_recx_root;
	uint32_t		 qt_flags;
	struct vos_pool		*qt_pool;
	daos_handle_t		 qt_coh;
};

static int
check_key(struct open_query *query, struct vos_krec_df *krec, bool *visible)
{
	struct ilog_entry	*entry;
	daos_epoch_range_t	 epr = query->qt_epr;
	int			 rc;

	*visible = false;

	rc = key_ilog_fetch(query->qt_obj, DAOS_INTENT_DEFAULT,
			    &epr, krec, &query->qt_entries);
	if (rc != 0)
		return rc;

	ilog_foreach_entry_reverse(&query->qt_entries, entry) {
		if (entry->ie_status == ILOG_REMOVED)
			continue;
		if (entry->ie_status == ILOG_UNCOMMITTED)
			return -DER_INPROGRESS;

		if (entry->ie_punch) {
			if ((entry->ie_id.id_epoch + 1) > epr.epr_lo)
				epr.epr_lo = entry->ie_id.id_epoch + 1;
			break;
		}

		*visible = true;
	}

	if (*visible)
		query->qt_epr = epr;

	return 0;
}

static int
find_key(struct open_query *query, daos_handle_t toh, daos_key_t *key,
	 daos_anchor_t *anchor)
{
	daos_handle_t		 ih;
	struct vos_rec_bundle	 rbund;
	d_iov_t			 kiov;
	d_iov_t			 riov;
	daos_csum_buf_t		 csum;
	daos_epoch_range_t	 epr = query->qt_epr;
	int			 rc = 0;
	int			 fini_rc;
	bool			 visible;
	int			 opc;

	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0)
		return rc;

	if (query->qt_flags & DAOS_GET_MAX) {
		if (daos_anchor_is_zero(anchor))
			opc = BTR_PROBE_LAST;
		else
			opc = BTR_PROBE_LT;
	} else {
		if (daos_anchor_is_zero(anchor))
			opc = BTR_PROBE_FIRST;
		else
			opc = BTR_PROBE_GT;
	}
	rc = dbtree_iter_probe(ih, opc, DAOS_INTENT_DEFAULT, NULL, anchor);
	if (rc != 0)
		goto out;

	tree_rec_bundle2iov(&rbund, &riov);
	d_iov_set(&kiov, NULL, 0);

	rbund.rb_iov = key;
	rbund.rb_csum = &csum;

	do {
		d_iov_set(rbund.rb_iov, NULL, 0);
		dcb_set_null(rbund.rb_csum);

		rc = dbtree_iter_fetch(ih, &kiov, &riov, anchor);
		if (rc != 0)
			break;

		rc = check_key(query, rbund.rb_krec, &visible);
		if (rc != 0)
			break;

		if (visible)
			break;

		/* Reset the epr */
		query->qt_epr = epr;

		if (query->qt_flags & DAOS_GET_MAX)
			rc = dbtree_iter_prev(ih);
		else
			rc = dbtree_iter_next(ih);
	} while (rc == 0);
out:
	fini_rc = dbtree_iter_finish(ih);

	if (rc == 0)
		rc = fini_rc;

	return rc;
}

static int
query_recx(struct open_query *query, daos_recx_t *recx)
{
	struct evt_desc_cbs	cbs;
	struct evt_entry	entry;
	daos_handle_t		toh;
	daos_handle_t		ih;
	struct evt_filter	filter = {0};
	int			rc;
	int			close_rc;
	int			opc;
	uint32_t		inob;

	recx->rx_idx = 0;
	recx->rx_nr = 0;

	vos_evt_desc_cbs_init(&cbs, query->qt_pool, query->qt_coh);
	rc = evt_open(query->qt_recx_root, &query->qt_pool->vp_uma, &cbs, &toh);
	if (rc != 0)
		return rc;

	opc = EVT_ITER_EMBEDDED | EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES;
	if (query->qt_flags & DAOS_GET_MAX)
		opc |= EVT_ITER_REVERSE;

	filter.fr_ex.ex_lo = 0;
	filter.fr_ex.ex_hi = ~(uint64_t)0;
	filter.fr_epr = query->qt_epr;


	rc = evt_iter_prepare(toh, opc, &filter, &ih);
	if (rc != 0)
		goto out;

	/* For MAX, we do reverse iterator
	 * For MIN, we use forward iterator
	 * In both cases, EVT_ITER_FIRST gives us what we want
	 */
	rc = evt_iter_probe(ih, EVT_ITER_FIRST, NULL, NULL);
	if (rc != 0)
		goto fini;

	rc = evt_iter_fetch(ih, &inob, &entry, NULL);
	if (rc != 0)
		goto fini;

	recx->rx_idx = entry.en_sel_ext.ex_lo;
	recx->rx_nr = entry.en_sel_ext.ex_hi - entry.en_sel_ext.ex_lo + 1;
fini:
	close_rc = evt_iter_finish(ih);
	if (rc == 0)
		rc = close_rc;
out:
	close_rc = evt_close(toh);
	if (rc == 0)
		rc = close_rc;

	return rc;
}

static int
open_and_query_key(struct open_query *query, daos_key_t *key,
		   uint32_t tree_type, daos_anchor_t *anchor)
{
	daos_handle_t		*toh;
	struct btr_root		*to_open;
	daos_csum_buf_t		 csum = {0};
	struct vos_rec_bundle	 rbund;
	d_iov_t			 riov;
	enum vos_tree_class	 tclass;
	int			 rc = 0;
	bool			 check = true;
	bool			 visible = true;

	if (tree_type == DAOS_GET_DKEY) {
		toh = &query->qt_dkey_toh;
		to_open = query->qt_dkey_root;
		tclass = VOS_BTR_DKEY;
	} else {
		toh = &query->qt_akey_toh;
		to_open = query->qt_akey_root;
		tclass = VOS_BTR_AKEY;
	}

	if (!daos_handle_is_inval(*toh)) {
		dbtree_close(*toh);
		*toh = DAOS_HDL_INVAL;
	}

	if (to_open->tr_class == 0)
		return -DER_NONEXIST;

	rc = dbtree_open_inplace_ex(to_open, &query->qt_pool->vp_uma,
				    query->qt_coh, query->qt_pool, toh);
	if (rc != 0)
		return rc;

	if (tree_type & query->qt_flags) {
		rc = find_key(query, *toh, key, anchor);

		if (rc != 0)
			return rc;

		check = false; /* Already checked the key */
	}

	tree_rec_bundle2iov(&rbund, &riov);
	rbund.rb_off	= UMOFF_NULL;
	rbund.rb_csum = &csum;
	rbund.rb_tclass = tclass;

	rc = dbtree_fetch(*toh, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, key, NULL,
			  &riov);
	if (rc != 0)
		return rc;

	if (check) {
		rc = check_key(query, rbund.rb_krec, &visible);
		if (rc != 0)
			return rc;
		if (visible == false)
			return -DER_NONEXIST;
	}

	if (tree_type == DAOS_GET_DKEY)
		query->qt_akey_root = &rbund.rb_krec->kr_btr;
	else if ((rbund.rb_krec->kr_bmap & KREC_BF_EVT) == 0)
		return -DER_NONEXIST;
	else
		query->qt_recx_root = &rbund.rb_krec->kr_evt;

	return 0;
}

#define LOG_RC(rc, ...)				\
	do {					\
		D_ASSERT(rc != 0);		\
		if (rc == -DER_NONEXIST)	\
			D_INFO(__VA_ARGS__);	\
		else				\
			D_ERROR(__VA_ARGS__);	\
	} while (0)

int
vos_obj_query_key(daos_handle_t coh, daos_unit_oid_t oid, uint32_t flags,
		  daos_epoch_t epoch, daos_key_t *dkey, daos_key_t *akey,
		  daos_recx_t *recx)
{
	struct vos_object	*obj;
	struct open_query	 query;
	daos_epoch_range_t	 dkey_epr;
	daos_anchor_t		 dkey_anchor;
	daos_anchor_t		 akey_anchor;
	daos_ofeat_t		 obj_feats;
	int			 rc = 0;

	if ((flags & DAOS_GET_MAX) && (flags & DAOS_GET_MIN)) {
		D_ERROR("Ambiguous query.  Please select either DAOS_GET_MAX"
			" or DAOS_GET_MIN\n");
		return -DER_INVAL;
	}

	if (!(flags & DAOS_GET_MAX) && !(flags & DAOS_GET_MIN)) {
		D_ERROR("No query type.  Please select either DAOS_GET_MAX"
			" or DAOS_GET_MIN\n");
		return -DER_INVAL;
	}

	if ((flags & (DAOS_GET_DKEY | DAOS_GET_AKEY | DAOS_GET_RECX)) == 0) {
		D_ERROR("No tree queried.  Please select one or more of"
			" DAOS_GET_DKEY, DAOS_GET_AKEY, or DAOS_GET_RECX\n");
		return -DER_INVAL;
	}

	if (flags & DAOS_GET_DKEY) {
		if (dkey == NULL) {
			D_ERROR("dkey can't be NULL with DAOS_GET_DKEY\n");
			return -DER_INVAL;
		}
		daos_anchor_set_zero(&dkey_anchor);
	}

	if (flags & DAOS_GET_AKEY && akey == NULL) {
		D_ERROR("akey can't be NULL with DAOS_GET_AKEY\n");
		return -DER_INVAL;
	}

	if (flags & DAOS_GET_RECX && recx == NULL) {
		D_ERROR("recx can't be NULL with DAOS_GET_RECX\n");
		return -DER_INVAL;
	}

	rc = vos_obj_hold(vos_obj_cache_current(), vos_hdl2cont(coh), oid,
			  epoch, true, DAOS_INTENT_DEFAULT, &obj);
	if (rc != 0) {
		LOG_RC(rc, "Could not hold object: %s\n", d_errstr(rc));
		return rc;
	}

	if (obj->obj_df == NULL) {
		rc = -DER_NONEXIST;
		D_INFO("Object not created yet\n");
		goto out;
	}

	/* only integer keys supported */
	obj_feats = daos_obj_id2feat(obj->obj_df->vo_id.id_pub);
	if ((flags & DAOS_GET_DKEY) &&
	    (obj_feats & DAOS_OF_DKEY_UINT64) == 0) {
		rc = -DER_INVAL;
		D_ERROR("Only integer dkey supported for query\n");
		goto out;
	}
	if ((flags & DAOS_GET_AKEY) &&
	    (obj_feats & DAOS_OF_AKEY_UINT64) == 0) {
		rc = -DER_INVAL;
		D_ERROR("Only integer akey supported for query\n");
		goto out;
	}

	ilog_fetch_init(&query.qt_entries);
	query.qt_dkey_toh   = DAOS_HDL_INVAL;
	query.qt_akey_toh   = DAOS_HDL_INVAL;
	query.qt_obj = obj;
	query.qt_flags	    = flags;
	query.qt_dkey_root  = &obj->obj_df->vo_tree;
	query.qt_coh	    = coh;
	query.qt_pool	    = vos_obj2pool(obj);

	for (;;) {
		/* Reset the epoch range */
		query.qt_epr.epr_lo = 0;
		query.qt_epr.epr_hi = epoch;
		rc = open_and_query_key(&query, dkey, DAOS_GET_DKEY,
					&dkey_anchor);
		if (rc != 0) {
			LOG_RC(rc, "Could not query dkey: %s\n", d_errstr(rc));
			break;
		}

		if ((flags & (DAOS_GET_AKEY | DAOS_GET_RECX)) == 0)
			break;

		if (query.qt_flags & DAOS_GET_AKEY)
			daos_anchor_set_zero(&akey_anchor);

		dkey_epr = query.qt_epr;
		for (;;) {
			rc = open_and_query_key(&query, akey, DAOS_GET_AKEY,
						&akey_anchor);
			if (rc != 0) {
				LOG_RC(rc, "Could not query akey: %s\n",
				       d_errstr(rc));
				break;
			}

			if ((flags & DAOS_GET_RECX) == 0)
				break;

			rc = query_recx(&query, recx);

			if (rc != 0) {
				LOG_RC(rc, "Could not query recx: %s\n",
				       d_errstr(rc));
				if (rc == -DER_NONEXIST &&
				    query.qt_flags & DAOS_GET_AKEY) {
					/* Reset the epoch range to last dkey */
					query.qt_epr = dkey_epr;
					continue;
				}
			}
			break;
		}
		if (rc == -DER_NONEXIST &&
		    query.qt_flags & DAOS_GET_DKEY) {
			continue;
		}
		break;
	}

	ilog_fetch_finish(&query.qt_entries);
	if (!daos_handle_is_inval(query.qt_akey_toh))
		dbtree_close(query.qt_akey_toh);
	if (!daos_handle_is_inval(query.qt_dkey_toh))
		dbtree_close(query.qt_dkey_toh);
out:
	if (obj)
		vos_obj_release(vos_obj_cache_current(), obj);
	return rc;
}
