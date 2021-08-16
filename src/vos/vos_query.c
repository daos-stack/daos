/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include "vos_ts.h"

struct open_query {
	struct vos_object	*qt_obj;
	struct vos_ts_set	*qt_ts_set;
	daos_epoch_t		 qt_bound;
	daos_epoch_range_t	 qt_epr;
	struct vos_punch_record	 qt_punch;
	struct vos_ilog_info	 qt_info;
	struct btr_root		*qt_dkey_root;
	daos_handle_t		 qt_dkey_toh;
	struct btr_root		*qt_akey_root;
	daos_handle_t		 qt_akey_toh;
	struct evt_root		*qt_recx_root;
	struct vos_pool		*qt_pool;
	daos_handle_t		 qt_coh;
	daos_anchor_t		 qt_dkey_anchor;
	daos_anchor_t		 qt_akey_anchor;
	uint64_t		 qt_stripe_size;
	uint32_t		 qt_flags;
	unsigned int		 qt_cell_size;
};

static int
check_key(struct open_query *query, struct vos_krec_df *krec)
{
	daos_epoch_range_t	 epr = query->qt_epr;
	int			 rc;

	rc = vos_ilog_fetch(vos_obj2umm(query->qt_obj),
			    vos_cont2hdl(query->qt_obj->obj_cont),
			    DAOS_INTENT_DEFAULT, &krec->kr_ilog,
			    epr.epr_hi, query->qt_bound, &query->qt_punch, NULL,
			    &query->qt_info);
	if (rc != 0)
		return rc;

	if (vos_has_uncertainty(query->qt_ts_set, &query->qt_info, epr.epr_hi,
				query->qt_bound))
		return -DER_TX_RESTART;

	rc = vos_ilog_check(&query->qt_info, &query->qt_epr, &epr, true);
	if (rc != 0)
		return rc;

	query->qt_epr = epr;
	query->qt_punch = query->qt_info.ii_prior_punch;

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
	struct dcs_csum_info	 csum;
	daos_epoch_range_t	 epr = query->qt_epr;
	struct vos_punch_record	 punch = query->qt_punch;
	int			 rc = 0;
	int			 fini_rc;
	int			 opc;

	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0)
		return rc;

	if (query->qt_flags & VOS_GET_MAX) {
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
		ci_set_null(rbund.rb_csum);

		rc = dbtree_iter_fetch(ih, &kiov, &riov, anchor);
		if (vos_dtx_continue_detect(rc))
			goto next;

		if (rc != 0)
			break;

		rc = check_key(query, rbund.rb_krec);
		if (rc == 0)
			break;

		if (vos_dtx_continue_detect(rc))
			continue;

		if (rc != -DER_NONEXIST)
			break;

		/* Reset the epr */
		query->qt_epr = epr;
		query->qt_punch = punch;

next:
		if (query->qt_flags & VOS_GET_MAX)
			rc = dbtree_iter_prev(ih);
		else
			rc = dbtree_iter_next(ih);
	} while (rc == 0);
out:
	fini_rc = dbtree_iter_finish(ih);

	if (rc == 0)
		rc = fini_rc;

	return vos_dtx_hit_inprogress() ? -DER_INPROGRESS : rc;
}

static int
_query_recx(struct open_query *query, daos_recx_t *recx, struct evt_extent *filter_extent,
	    int opc)
{
	struct evt_desc_cbs	cbs;
	struct evt_entry	entry;
	daos_handle_t		toh;
	daos_handle_t		ih;
	struct evt_filter	filter = {0};
	int			rc;
	int			close_rc;
	uint32_t		inob;
	uint64_t		start = 0;
	uint64_t		end = 0;
	bool			exist = false;

	recx->rx_idx = 0;
	recx->rx_nr = 0;

	vos_evt_desc_cbs_init(&cbs, query->qt_pool, query->qt_coh);
	rc = evt_open(query->qt_recx_root, &query->qt_pool->vp_uma, &cbs, &toh);
	if (rc != 0)
		return rc;

	if (query->qt_flags & VOS_GET_MAX)
		opc |= EVT_ITER_REVERSE;

	filter.fr_ex = *filter_extent;
	filter.fr_punch_epc = query->qt_punch.pr_epc;
	filter.fr_punch_minor_epc = query->qt_punch.pr_minor_epc;
	filter.fr_epr.epr_hi = query->qt_bound;
	filter.fr_epr.epr_lo = query->qt_epr.epr_lo;
	filter.fr_epoch = query->qt_epr.epr_hi;

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

	if (opc & EVT_ITER_SKIP_HOLES) {
		if (entry.en_visibility & EVT_VISIBLE) {
			recx->rx_idx = entry.en_sel_ext.ex_lo;
			recx->rx_nr = entry.en_sel_ext.ex_hi - entry.en_sel_ext.ex_lo + 1;
		} else {
			rc = -DER_NONEXIST;
		}
		D_GOTO(fini, rc);
	}

	/* The following only supports MAX recx query at the moment */
	if (!(query->qt_flags & VOS_GET_MAX))
		D_GOTO(fini, rc = -DER_INVAL);

	/* Check if these extents can be merged */
	D_ASSERT(bio_addr_is_hole(&entry.en_addr));
	end = entry.en_sel_ext.ex_hi;
	start = entry.en_sel_ext.ex_lo;
	while (1) {
		rc = evt_iter_next(ih);
		if (rc)
			break;

		rc = evt_iter_fetch(ih, &inob, &entry, NULL);
		if (rc != 0)
			break;

		D_ASSERT(bio_addr_is_hole(&entry.en_addr));
		if (entry.en_sel_ext.ex_hi == start - 1)
			start = entry.en_sel_ext.ex_lo;
		else
			break;
	}

	recx->rx_idx = start;
	recx->rx_nr = end - start + 1;

fini:
	D_DEBUG(DB_TRACE, "query recx "DF_U64"/"DF_U64" : "DF_RC"\n", recx->rx_idx,
		recx->rx_nr, DP_RC(rc));
	if (rc == 0)
		exist = true;
	if (rc == -DER_NONEXIST)
		rc = 0;
	close_rc = evt_iter_finish(ih);
	if (rc == 0)
		rc = close_rc;
out:
	close_rc = evt_close(toh);
	if (rc == 0 && !exist)
		rc = -DER_NONEXIST;
	if (rc == 0)
		rc = close_rc;

	return rc;
}

static int
query_normal_recx(struct open_query *query, daos_recx_t *recx)
{
	struct evt_extent filter_ex;
	int		  opc;

	memset(recx, 0, sizeof(*recx));
	/* query visible last recx */
	opc = EVT_ITER_EMBEDDED | EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES;
	if (query->qt_flags & VOS_GET_MAX)
		opc |= EVT_ITER_REVERSE;

	filter_ex.ex_lo = 0;
	filter_ex.ex_hi = ~(uint64_t)0;

	return _query_recx(query, recx, &filter_ex, opc);
}

static int
query_ec_normal_recx(struct open_query *query, daos_recx_t *recx,
		     struct evt_extent *filter_ex, bool skip_hole)
{
	struct evt_extent tmp_ex;
	int		  opc;

	memset(recx, 0, sizeof(*recx));
	/* query visible last recx */
	opc = EVT_ITER_EMBEDDED | EVT_ITER_VISIBLE;
	if (query->qt_flags & VOS_GET_MAX)
		opc |= EVT_ITER_REVERSE;

	if (skip_hole)
		opc |= EVT_ITER_SKIP_HOLES;
	else /* otherwise skip DATA */
		opc |= EVT_ITER_SKIP_DATA;

	if (filter_ex) {
		tmp_ex = *filter_ex;
	} else {
		tmp_ex.ex_lo = 0;
		tmp_ex.ex_hi = DAOS_EC_PARITY_BIT - 1;
	}
	D_DEBUG(DB_TRACE, "search opc %x ["DF_U64"-"DF_U64"]\n", opc,
		tmp_ex.ex_lo, tmp_ex.ex_hi);
	return _query_recx(query, recx, &tmp_ex, opc);
}

static int
query_ec_parity_recx(struct open_query *query, daos_recx_t *recx)
{
	struct evt_extent filter_ex;
	int		  opc;

	memset(recx, 0, sizeof(*recx));
	/* query visible last recx */
	opc = EVT_ITER_EMBEDDED | EVT_ITER_VISIBLE | EVT_ITER_SKIP_HOLES;
	if (query->qt_flags & VOS_GET_MAX)
		opc |= EVT_ITER_REVERSE;

	filter_ex.ex_lo = DAOS_EC_PARITY_BIT;
	filter_ex.ex_hi = ~(uint64_t)0;

	return _query_recx(query, recx, &filter_ex, opc);
}

static int
query_recx(struct open_query *query, daos_recx_t *recxs)
{
	struct evt_extent filter_ex;
	int		  rc;

	if (!(query->qt_flags & VOS_GET_RECX_EC))
		return query_normal_recx(query, &recxs[0]);

	/* visible recxs */
	rc = query_ec_normal_recx(query, &recxs[0], NULL, true);
	if (rc && rc != -DER_NONEXIST)
		return rc;

	/* visible parity recxs */
	rc = query_ec_parity_recx(query, &recxs[1]);
	if (rc) {
		if (rc == -DER_NONEXIST)
			rc = 0;
		return rc;
	}

	/* possible punched recxs within this parity recx */
	filter_ex.ex_lo = recxs[1].rx_idx;
	D_ASSERT((filter_ex.ex_lo & DAOS_EC_PARITY_BIT) != 0);
	filter_ex.ex_lo &= ~DAOS_EC_PARITY_BIT;
	D_ASSERT(filter_ex.ex_lo % query->qt_cell_size == 0);
	filter_ex.ex_hi = (((filter_ex.ex_lo + recxs[1].rx_nr) / query->qt_cell_size) *
			   query->qt_stripe_size);
	filter_ex.ex_lo = ((filter_ex.ex_lo / query->qt_cell_size) *
			   query->qt_stripe_size);

	rc = query_ec_normal_recx(query, &recxs[2], &filter_ex, false);
	if (rc == -DER_NONEXIST)
		rc = 0;

	D_DEBUG(DB_TRACE, "get punched recxs "DF_U64"/"DF_U64"\n", recxs[2].rx_idx,
		recxs[2].rx_nr);

	return rc;
}

static int
open_and_query_key(struct open_query *query, daos_key_t *key,
		   uint32_t tree_type, daos_anchor_t *anchor)
{
	daos_handle_t		*toh;
	struct ilog_df		*ilog = NULL;
	struct btr_root		*to_open;
	struct dcs_csum_info	 csum = {0};
	struct vos_rec_bundle	 rbund;
	d_iov_t			 riov;
	enum vos_tree_class	 tclass;
	int			 rc = 0;
	bool			 check = true;

	if (tree_type == VOS_GET_DKEY) {
		toh = &query->qt_dkey_toh;
		to_open = query->qt_dkey_root;
		tclass = VOS_BTR_DKEY;
	} else {
		toh = &query->qt_akey_toh;
		to_open = query->qt_akey_root;
		tclass = VOS_BTR_AKEY;
	}

	if (daos_handle_is_valid(*toh)) {
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
	if (rc == 0)
		ilog = &rbund.rb_krec->kr_ilog;

	vos_ilog_ts_add(query->qt_ts_set, ilog, NULL, 0);

	if (rc != 0)
		return rc;

	if (check) {
		rc = check_key(query, rbund.rb_krec);
		if (rc != 0)
			return rc;
	}

	if (tree_type == VOS_GET_DKEY)
		query->qt_akey_root = &rbund.rb_krec->kr_btr;
	else if ((rbund.rb_krec->kr_bmap & KREC_BF_EVT) == 0)
		return -DER_NONEXIST;
	else
		query->qt_recx_root = &rbund.rb_krec->kr_evt;

	return 0;
}

#define LOG_RC(rc, ...)					\
	VOS_TX_LOG_FAIL(rc, __VA_ARGS__)

int
vos_obj_query_key(daos_handle_t coh, daos_unit_oid_t oid, uint32_t flags,
		  daos_epoch_t epoch, daos_key_t *dkey, daos_key_t *akey,
		  daos_recx_t *recx, unsigned int cell_size, uint64_t stripe_size,
		  struct dtx_handle *dth)
{
	struct vos_container	*cont;
	struct vos_object	*obj = NULL;
	struct open_query	*query;
	daos_epoch_t		 bound;
	daos_epoch_range_t	 dkey_epr;
	struct vos_punch_record	 dkey_punch;
	daos_ofeat_t		 obj_feats;
	daos_epoch_range_t	 obj_epr = {0};
	struct vos_ts_set	 akey_save = {0};
	struct vos_ts_set	 dkey_save = {0};
	uint32_t		 cflags = 0;
	int			 rc = 0;
	int			 nr_akeys = 0;

	obj_epr.epr_hi = dtx_is_valid_handle(dth) ? dth->dth_epoch : epoch;
	bound = dtx_is_valid_handle(dth) ? dth->dth_epoch_bound : epoch;

	if ((flags & VOS_GET_MAX) && (flags & VOS_GET_MIN)) {
		D_ERROR("Ambiguous query.  Please select either VOS_GET_MAX"
			" or VOS_GET_MIN\n");
		return -DER_INVAL;
	}

	if (!(flags & VOS_GET_MAX) && !(flags & VOS_GET_MIN)) {
		D_ERROR("No query type.  Please select either VOS_GET_MAX"
			" or VOS_GET_MIN\n");
		return -DER_INVAL;
	}

	if ((flags & (VOS_GET_DKEY | VOS_GET_AKEY | VOS_GET_RECX)) == 0) {
		D_ERROR("No tree queried.  Please select one or more of"
			" VOS_GET_DKEY, VOS_GET_AKEY, or VOS_GET_RECX\n");
		return -DER_INVAL;
	}

	D_ALLOC_PTR(query);
	if (query == NULL)
		return -DER_NOMEM;

	query->qt_ts_set = NULL;

	if (flags & VOS_GET_DKEY) {
		if (dkey == NULL) {
			D_ERROR("dkey can't be NULL with VOS_GET_DKEY\n");
			D_GOTO(free_query, rc = -DER_INVAL);
		}
		daos_anchor_set_zero(&query->qt_dkey_anchor);

		cflags = VOS_TS_READ_OBJ;
	}

	if (flags & VOS_GET_AKEY) {
		if (akey == NULL) {
			D_ERROR("akey can't be NULL with VOS_GET_AKEY\n");
			D_GOTO(free_query, rc = -DER_INVAL);
		}

		if (cflags == 0)
			cflags = VOS_TS_READ_DKEY;
	}

	if (flags & VOS_GET_RECX) {
		if (recx == NULL) {
			D_ERROR("recx can't be NULL with VOS_GET_RECX\n");
			D_GOTO(free_query, rc = -DER_INVAL);
		}

		nr_akeys = 1;
		if (cflags == 0)
			cflags = VOS_TS_READ_AKEY;
	}

	vos_dth_set(dth);
	rc = vos_ts_set_allocate(&query->qt_ts_set, 0, cflags, nr_akeys, dth);
	if (rc != 0) {
		D_ERROR("Failed to allocate timestamp set: "DF_RC"\n",
			DP_RC(rc));
		goto free_query;
	}

	cont = vos_hdl2cont(coh);

	vos_ts_set_add(query->qt_ts_set, cont->vc_ts_idx, NULL, 0);

	query->qt_bound = MAX(obj_epr.epr_hi, bound);
	rc = vos_obj_hold(vos_obj_cache_current(), vos_hdl2cont(coh), oid,
			  &obj_epr, query->qt_bound, VOS_OBJ_VISIBLE,
			  DAOS_INTENT_DEFAULT, &obj, query->qt_ts_set);
	if (rc != 0) {
		LOG_RC(rc, "Could not hold object: %s\n", d_errstr(rc));
		goto out;
	}

	if (obj->obj_ilog_info.ii_uncertain_create) {
		rc = -DER_TX_RESTART;
		goto out;
	}

	D_ASSERT(obj != NULL);
	/* only integer keys supported */
	obj_feats = daos_obj_id2feat(obj->obj_df->vo_id.id_pub);
	if ((flags & VOS_GET_DKEY) &&
	    (obj_feats & DAOS_OF_DKEY_UINT64) == 0) {
		rc = -DER_INVAL;
		D_ERROR("Only integer dkey supported for query\n");
		goto out;
	}
	if ((flags & VOS_GET_AKEY) &&
	    (obj_feats & DAOS_OF_AKEY_UINT64) == 0) {
		rc = -DER_INVAL;
		D_ERROR("Only integer akey supported for query\n");
		goto out;
	}

	vos_ilog_fetch_init(&query->qt_info);
	query->qt_dkey_toh   = DAOS_HDL_INVAL;
	query->qt_akey_toh   = DAOS_HDL_INVAL;
	query->qt_obj	    = obj;
	query->qt_flags	    = flags;
	query->qt_dkey_root  = &obj->obj_df->vo_tree;
	query->qt_coh	    = coh;
	query->qt_pool	    = vos_obj2pool(obj);
	query->qt_cell_size = cell_size;
	query->qt_stripe_size = stripe_size;

	/** We may read a dkey/akey that has no valid akey/recx and will need to
	 *  reset the timestamp cache state to cache the new dkey/akey
	 *  timestamps.
	 */
	vos_ts_set_save(query->qt_ts_set, &dkey_save);
	for (;;) {
		/* Reset the epoch range */
		query->qt_epr = obj_epr;
		query->qt_punch = obj->obj_ilog_info.ii_prior_punch;
		rc = open_and_query_key(query, dkey, VOS_GET_DKEY,
					&query->qt_dkey_anchor);
		if (rc != 0) {
			LOG_RC(rc, "Could not query dkey: %s\n", d_errstr(rc));
			break;
		}

		if ((flags & (VOS_GET_AKEY | VOS_GET_RECX)) == 0)
			break;

		if (query->qt_flags & VOS_GET_AKEY)
			daos_anchor_set_zero(&query->qt_akey_anchor);

		dkey_punch = query->qt_punch;
		dkey_epr = query->qt_epr;
		vos_ts_set_save(query->qt_ts_set, &akey_save);
		for (;;) {
			rc = open_and_query_key(query, akey, VOS_GET_AKEY,
						&query->qt_akey_anchor);
			if (rc != 0) {
				LOG_RC(rc, "Could not query akey: %s\n",
				       d_errstr(rc));
				break;
			}

			if ((flags & VOS_GET_RECX) == 0)
				break;

			rc = query_recx(query, recx);

			if (rc != 0) {
				LOG_RC(rc, "Could not query recx: %s\n",
				       d_errstr(rc));
				if (rc == -DER_NONEXIST &&
				    query->qt_flags & VOS_GET_AKEY) {
					/* Reset the epoch range to last dkey */
					query->qt_epr = dkey_epr;
					query->qt_punch = dkey_punch;
					/** Go ahead and save timestamps for
					 * things we read
					 */
					vos_ts_set_update(query->qt_ts_set,
							  obj_epr.epr_hi);
					vos_ts_set_restore(query->qt_ts_set,
							   &akey_save);
					continue;
				}
			}
			break;
		}
		if (rc == -DER_NONEXIST &&
		    query->qt_flags & VOS_GET_DKEY) {
			/** Go ahead and save timestamps for things we read */
			vos_ts_set_update(query->qt_ts_set, obj_epr.epr_hi);
			vos_ts_set_restore(query->qt_ts_set, &dkey_save);
			continue;
		}
		break;
	}

	vos_ilog_fetch_finish(&query->qt_info);
	if (daos_handle_is_valid(query->qt_akey_toh))
		dbtree_close(query->qt_akey_toh);
	if (daos_handle_is_valid(query->qt_dkey_toh))
		dbtree_close(query->qt_dkey_toh);
out:
	if (obj != NULL)
		vos_obj_release(vos_obj_cache_current(), obj, false);

	vos_dth_set(NULL);
	if (rc == 0 || rc == -DER_NONEXIST) {
		if (vos_ts_wcheck(query->qt_ts_set, obj_epr.epr_hi,
				  query->qt_bound))
			rc = -DER_TX_RESTART;
	}

	if (rc == 0 || rc == -DER_NONEXIST)
		vos_ts_set_update(query->qt_ts_set, obj_epr.epr_hi);

	vos_ts_set_free(query->qt_ts_set);
free_query:
	D_FREE(query);

	return rc;
}
