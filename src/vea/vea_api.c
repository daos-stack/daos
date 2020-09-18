/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/btree_class.h>
#include <daos/dtx.h>
#include "vea_internal.h"

#define VEA_BLK_SZ	(4 * 1024)	/* 4K */
#define VEA_TREE_ODR	20

static void
erase_md(struct umem_instance *umem, struct vea_space_df *md)
{
	struct umem_attr uma;
	daos_handle_t free_btr, vec_btr;
	int rc;

	uma.uma_id = umem->umm_id;
	uma.uma_pool = umem->umm_pool;
	rc = dbtree_open_inplace(&md->vsd_free_tree, &uma, &free_btr);
	if (rc == 0) {
		rc = dbtree_destroy(free_btr, NULL);
		if (rc)
			D_ERROR("destroy free extent tree error: "DF_RC"\n",
				DP_RC(rc));
	}

	rc = dbtree_open_inplace(&md->vsd_vec_tree, &uma, &vec_btr);
	if (rc == 0) {
		rc = dbtree_destroy(vec_btr, NULL);
		if (rc)
			D_ERROR("destroy vector tree error: "DF_RC"\n",
				DP_RC(rc));
	}
}

/*
 * Initialize the space tracking information on SCM and the header of the
 * block device.
 */
int
vea_format(struct umem_instance *umem, struct umem_tx_stage_data *txd,
	   struct vea_space_df *md, uint32_t blk_sz, uint32_t hdr_blks,
	   uint64_t capacity, vea_format_callback_t cb, void *cb_data,
	   bool force)
{
	struct vea_free_extent free_ext;
	struct umem_attr uma;
	uint64_t tot_blks;
	daos_handle_t free_btr, vec_btr;
	d_iov_t key, val;
	int rc;

	D_ASSERT(umem != NULL);
	D_ASSERT(md != NULL);
	/* Can't reformat without 'force' specified */
	if (md->vsd_magic == VEA_MAGIC) {
		D_CDEBUG(force, DLOG_WARN, DLOG_ERR, "reformat %p force=%d\n",
			 md, force);
		if (!force)
			return -DER_EXIST;

		erase_md(umem, md);
	}

	/* Block size should be aligned with 4K and <= 1M */
	if (blk_sz && ((blk_sz % VEA_BLK_SZ) != 0 || blk_sz > (1U << 20)))
		return -DER_INVAL;

	if (hdr_blks < 1)
		return -DER_INVAL;

	blk_sz = blk_sz ? : VEA_BLK_SZ;
	if (capacity < (blk_sz * 100))
		return -DER_NOSPACE;

	tot_blks = capacity / blk_sz;
	if (tot_blks <= hdr_blks)
		return -DER_NOSPACE;
	tot_blks -= hdr_blks;

	/*
	 * Extent block count is represented by uint32_t, make sure the
	 * largest extent won't overflow.
	 */
	if (tot_blks >= UINT32_MAX) {
		D_ERROR("Capacity "DF_U64" is too large.\n", capacity);
		return -DER_INVAL;
	}

	/* Initialize block device header in callback */
	if (cb) {
		/*
		 * This function can't be called in pmemobj transaction since
		 * the callback for block header initialization could yield.
		 */
		D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);

		rc = cb(cb_data, umem);
		if (rc != 0)
			return rc;
	}

	/* Start transaction to initialize allocation metadata */
	rc = umem_tx_begin(umem, txd);
	if (rc != 0)
		return rc;

	free_btr = vec_btr = DAOS_HDL_INVAL;

	rc = umem_tx_add_ptr(umem, md, sizeof(*md));
	if (rc != 0)
		goto out;

	md->vsd_magic = VEA_MAGIC;
	md->vsd_compat = 0;
	md->vsd_blk_sz = blk_sz;
	md->vsd_tot_blks = tot_blks;
	md->vsd_hdr_blks = hdr_blks;

	/* Create free extent tree */
	uma.uma_id = umem->umm_id;
	uma.uma_pool = umem->umm_pool;
	rc = dbtree_create_inplace(DBTREE_CLASS_IV, BTR_FEAT_DIRECT_KEY,
				   VEA_TREE_ODR, &uma, &md->vsd_free_tree,
				   &free_btr);
	if (rc != 0)
		goto out;

	/* Insert the initial free extent */
	free_ext.vfe_blk_off = hdr_blks;
	free_ext.vfe_blk_cnt = tot_blks;
	free_ext.vfe_flags = 0;
	free_ext.vfe_age = VEA_EXT_AGE_MAX;

	d_iov_set(&key, &free_ext.vfe_blk_off,
		     sizeof(free_ext.vfe_blk_off));
	d_iov_set(&val, &free_ext, sizeof(free_ext));

	rc = dbtree_update(free_btr, &key, &val);
	if (rc != 0)
		goto out;

	/* Create extent vector tree */
	rc = dbtree_create_inplace(DBTREE_CLASS_IV, BTR_FEAT_DIRECT_KEY,
				   VEA_TREE_ODR, &uma, &md->vsd_vec_tree,
				   &vec_btr);
	if (rc != 0)
		goto out;

out:
	if (!daos_handle_is_inval(free_btr))
		dbtree_close(free_btr);
	if (!daos_handle_is_inval(vec_btr))
		dbtree_close(vec_btr);

	/* Commit/Abort transaction on success/error */
	return rc ? umem_tx_abort(umem, rc) : umem_tx_commit(umem);
}

/* Free the memory footprint created by vea_load(). */
void
vea_unload(struct vea_space_info *vsi)
{
	D_ASSERT(vsi != NULL);
	unload_space_info(vsi);

	/* Destroy the in-memory free extent tree */
	if (!daos_handle_is_inval(vsi->vsi_free_btr)) {
		dbtree_destroy(vsi->vsi_free_btr, NULL);
		vsi->vsi_free_btr = DAOS_HDL_INVAL;
	}

	/* Destroy the in-memory extent vector tree */
	if (!daos_handle_is_inval(vsi->vsi_vec_btr)) {
		dbtree_destroy(vsi->vsi_vec_btr, NULL);
		vsi->vsi_vec_btr = DAOS_HDL_INVAL;
	}

	/* Destroy the in-memory aggregation tree */
	if (!daos_handle_is_inval(vsi->vsi_agg_btr)) {
		dbtree_destroy(vsi->vsi_agg_btr, NULL);
		vsi->vsi_agg_btr = DAOS_HDL_INVAL;
	}

	destroy_free_class(&vsi->vsi_class);
	D_FREE(vsi);
}

/*
 * Load space tracking information from SCM to initialize the in-memory
 * compound index.
 */
int
vea_load(struct umem_instance *umem, struct umem_tx_stage_data *txd,
	 struct vea_space_df *md, struct vea_unmap_context *unmap_ctxt,
	 struct vea_space_info **vsip)
{
	struct umem_attr uma;
	struct vea_space_info *vsi;
	int rc;

	D_ASSERT(umem != NULL);
	D_ASSERT(txd != NULL);
	D_ASSERT(md != NULL);
	D_ASSERT(unmap_ctxt != NULL);
	D_ASSERT(vsip != NULL);

	if (md->vsd_magic != VEA_MAGIC) {
		D_DEBUG(DB_IO, "load unformatted blob\n");
		return -DER_UNINIT;
	}

	D_ALLOC_PTR(vsi);
	if (vsi == NULL)
		return -DER_NOMEM;

	vsi->vsi_umem = umem;
	vsi->vsi_txd = txd;
	vsi->vsi_md = md;
	vsi->vsi_md_free_btr = DAOS_HDL_INVAL;
	vsi->vsi_md_vec_btr = DAOS_HDL_INVAL;
	vsi->vsi_free_btr = DAOS_HDL_INVAL;
	D_INIT_LIST_HEAD(&vsi->vsi_agg_lru);
	vsi->vsi_agg_btr = DAOS_HDL_INVAL;
	vsi->vsi_vec_btr = DAOS_HDL_INVAL;
	vsi->vsi_agg_time = 0;
	vsi->vsi_agg_scheduled = false;
	vsi->vsi_unmap_ctxt = *unmap_ctxt;

	rc = create_free_class(&vsi->vsi_class, md);
	if (rc)
		goto error;

	memset(&uma, 0, sizeof(uma));
	uma.uma_id = UMEM_CLASS_VMEM;
	/* Create in-memory free extent tree */
	rc = dbtree_create(DBTREE_CLASS_IV, BTR_FEAT_DIRECT_KEY, VEA_TREE_ODR,
			   &uma, NULL, &vsi->vsi_free_btr);
	if (rc != 0)
		goto error;

	/* Create in-memory extent vector tree */
	rc = dbtree_create(DBTREE_CLASS_IV, BTR_FEAT_DIRECT_KEY, VEA_TREE_ODR,
			   &uma, NULL, &vsi->vsi_vec_btr);
	if (rc != 0)
		goto error;

	/* Create in-memory aggregation tree */
	rc = dbtree_create(DBTREE_CLASS_IV, BTR_FEAT_DIRECT_KEY, VEA_TREE_ODR,
			   &uma, NULL, &vsi->vsi_agg_btr);
	if (rc != 0)
		goto error;

	/* Load free space tracking info from SCM */
	rc = load_space_info(vsi);
	if (rc)
		goto error;

	*vsip = vsi;
	return 0;
error:
	vea_unload(vsi);
	return rc;
}

/*
 * Reserve an extent on block device.
 *
 * Always try to preserve sequential locality by 'hint', 'free extent size'
 * and 'free extent age', if the block device is too fragmented to satisfy
 * a contiguous allocation, reserve an extent vector as the last resort.
 *
 * Reserve attempting order:
 *
 * 1. Reserve from the free extent with 'hinted' start offset. (vsi_free_tree)
 * 2. Reserve from the largest free extent if it isn't non-active (extent age
 *    isn't VEA_EXT_AGE_MAX), otherwise, divide it in half-and-half and resreve
 *    from the latter half. (vfc_heap)
 * 3. Search & reserve from a bunch of extent size classed LRUs in first fit
 *    policy, larger & older free extent has priority. (vfc_lrus)
 * 4. Repeat the search in 3rd step to reserve an extent vector. (vsi_vec_tree)
 * 5. Fail reserve with ENOMEM if all above attempts fail.
 */
int
vea_reserve(struct vea_space_info *vsi, uint32_t blk_cnt,
	    struct vea_hint_context *hint, d_list_t *resrvd_list)
{
	struct vea_resrvd_ext *resrvd;
	bool retry = true;
	int rc = 0;

	D_ASSERT(vsi != NULL);
	D_ASSERT(resrvd_list != NULL);

	D_ALLOC_PTR(resrvd);
	if (resrvd == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&resrvd->vre_link);
	resrvd->vre_hint_off = VEA_HINT_OFF_INVAL;

	/* Get hint offset */
	hint_get(hint, &resrvd->vre_hint_off);

migrate:
	/* Trigger free extents migration */
	migrate_free_exts(vsi, false);

	/* Reserve from hint offset */
	rc = reserve_hint(vsi, blk_cnt, resrvd);
	if (rc != 0)
		goto error;
	else if (resrvd->vre_blk_cnt != 0)
		goto done;

	/* Reserve from the large extents */
	rc = reserve_large(vsi, blk_cnt, resrvd);
	if (rc != 0)
		goto error;
	else if (resrvd->vre_blk_cnt != 0)
		goto done;

	/* Reserve from the small extents */
	rc = reserve_small(vsi, blk_cnt, resrvd);
	if (rc != 0)
		goto error;
	else if (resrvd->vre_blk_cnt != 0)
		goto done;

	/* Reserve extent vector as the last resort */
	rc = reserve_vector(vsi, blk_cnt, resrvd);

	if (rc == -DER_NOSPACE && retry) {
		vsi->vsi_agg_time = 0; /* force free extents migration */
		retry = false;
		goto migrate;
	} else if (rc != 0) {
		goto error;
	}
done:
	D_ASSERT(resrvd->vre_blk_off != VEA_HINT_OFF_INVAL);
	D_ASSERT(resrvd->vre_blk_cnt == blk_cnt);

	D_ASSERTF(vsi->vsi_stat[STAT_FREE_BLKS] >= blk_cnt,
		  "free:"DF_U64" < rsrvd:%u\n",
		  vsi->vsi_stat[STAT_FREE_BLKS], blk_cnt);
	vsi->vsi_stat[STAT_FREE_BLKS] -= blk_cnt;

	/* Update hint offset */
	hint_update(hint, resrvd->vre_blk_off + blk_cnt,
		    &resrvd->vre_hint_seq);

	d_list_add_tail(&resrvd->vre_link, resrvd_list);

	return 0;
error:
	D_FREE(resrvd);
	return rc;
}

static int
process_resrvd_list(struct vea_space_info *vsi, struct vea_hint_context *hint,
		    d_list_t *resrvd_list, bool publish)
{
	struct vea_resrvd_ext	*resrvd, *tmp;
	struct vea_free_extent vfe = {0};
	uint64_t		 seq_max = 0, seq_min = 0;
	uint64_t		 off_c = 0, off_p = 0;
	uint64_t		 cur_time;
	int			 rc = 0;

	if (d_list_empty(resrvd_list))
		return 0;

	rc = daos_gettime_coarse(&cur_time);
	if (rc)
		return rc;

	vfe.vfe_blk_off = 0;
	vfe.vfe_blk_cnt = 0;

	d_list_for_each_entry(resrvd, resrvd_list, vre_link) {
		rc = verify_resrvd_ext(resrvd);
		if (rc)
			goto error;

		/* Reserved list is sorted by hint sequence */
		if (seq_min == 0) {
			seq_min = resrvd->vre_hint_seq;
			off_c = resrvd->vre_hint_off;
		} else if (hint != NULL) {
			D_ASSERT(seq_min < resrvd->vre_hint_seq);
		}

		seq_max = resrvd->vre_hint_seq;
		off_p = resrvd->vre_blk_off + resrvd->vre_blk_cnt;

		if (vfe.vfe_blk_off + vfe.vfe_blk_cnt == resrvd->vre_blk_off) {
			vfe.vfe_blk_cnt += resrvd->vre_blk_cnt;
			continue;
		}

		if (vfe.vfe_blk_cnt != 0) {
			vfe.vfe_age = cur_time;
			rc = publish ? persistent_alloc(vsi, &vfe) :
				       compound_free(vsi, &vfe, 0);
			if (rc)
				goto error;
		}

		vfe.vfe_blk_off = resrvd->vre_blk_off;
		vfe.vfe_blk_cnt = resrvd->vre_blk_cnt;
	}

	if (vfe.vfe_blk_cnt != 0) {
		vfe.vfe_age = cur_time;
		rc = publish ? persistent_alloc(vsi, &vfe) :
			       compound_free(vsi, &vfe, 0);
		if (rc)
			goto error;
	}

	rc = publish ? hint_tx_publish(vsi->vsi_umem, hint, off_p, seq_min,
				       seq_max) :
		       hint_cancel(hint, off_c, seq_min, seq_max);
error:
	d_list_for_each_entry_safe(resrvd, tmp, resrvd_list, vre_link) {
		d_list_del_init(&resrvd->vre_link);
		D_FREE(resrvd);
	}

	return rc;
}

/* Cancel the reserved extent(s) */
int
vea_cancel(struct vea_space_info *vsi, struct vea_hint_context *hint,
	   d_list_t *resrvd_list)
{
	D_ASSERT(vsi != NULL);
	D_ASSERT(resrvd_list != NULL);
	return process_resrvd_list(vsi, hint, resrvd_list, false);
}

/*
 * Make the reservation persistent. It should be part of transaction
 * manipulated by caller.
 */
int
vea_tx_publish(struct vea_space_info *vsi, struct vea_hint_context *hint,
	       d_list_t *resrvd_list)
{
	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK ||
		 vsi->vsi_umem->umm_id == UMEM_CLASS_VMEM);
	D_ASSERT(vsi != NULL);
	D_ASSERT(resrvd_list != NULL);
	/*
	 * We choose to don't rollback the in-memory hint updates even if the
	 * transaction manipulcated by caller is aborted, that'll result in
	 * some 'holes' in the allocation stream, but it can keep the API as
	 * simplified as possible, otherwise, caller has to explicitly call
	 * a hint cancel API on transaction abort.
	 */
	return process_resrvd_list(vsi, hint, resrvd_list, true);
}

struct free_commit_cb_arg {
	struct vea_space_info	*fca_vsi;
	struct vea_free_extent	 fca_vfe;
};

static void
free_commit_cb(void *data, bool noop)
{
	struct free_commit_cb_arg *fca = data;
	int rc;

	/* Transaction aborted, only need to free callback arg */
	if (noop)
		goto free;

	/*
	 * Aggregated free will be executed on outermost transaction
	 * commit.
	 *
	 * If it fails, the freed space on persistent free tree won't
	 * be added in in-memory free tree, hence the space won't be
	 * visible for allocation until the tree sync up on next server
	 * restart. Such temporary space leak is tolerable, what we must
	 * avoid is the contrary case: in-memory tree update succeeds
	 * but persistent tree update fails, which risks data corruption.
	 */
	rc = aggregated_free(fca->fca_vsi, &fca->fca_vfe);

	D_CDEBUG(rc, DLOG_ERR, DB_IO, "Aggregated free on vsi:%p rc %d\n",
		 fca->fca_vsi, rc);
free:
	D_FREE(fca);
}

/*
 * Free allocated extent.
 *
 * The just recent freed extents won't be visible for allocation instantly,
 * they will stay in vsi_agg_lru for a short period time, and being coalesced
 * with each other there.
 *
 * Expired free extents in the vsi_agg_lru will be migrated to the allocation
 * visible index (vsi_free_tree, vfc_heap or vfc_lrus) from time to time, this
 * kind of migration will be triggered by vea_reserve() & vea_free() calls.
 */
int
vea_free(struct vea_space_info *vsi, uint64_t blk_off, uint32_t blk_cnt)
{
	D_ASSERT(vsi != NULL);
	struct umem_instance *umem = vsi->vsi_umem;
	struct free_commit_cb_arg *fca;
	int rc;

	D_ALLOC_PTR(fca);
	if (fca == NULL)
		return -DER_NOMEM;

	fca->fca_vsi = vsi;
	fca->fca_vfe.vfe_blk_off = blk_off;
	fca->fca_vfe.vfe_blk_cnt = blk_cnt;

	rc = verify_free_entry(NULL, &fca->fca_vfe);
	if (rc)
		goto error;

	/*
	 * The transaction may have been started by caller already, here
	 * we start the nested transaction to ensure the stage callback
	 * and stage callback data being set to transaction properly.
	 */
	rc = umem_tx_begin(umem, vsi->vsi_txd);
	if (rc != 0)
		goto error;

	/* Add the free extent in persistent free extent tree */
	rc = persistent_free(vsi, &fca->fca_vfe);
	if (rc)
		goto done;

	rc = umem_tx_add_callback(umem, vsi->vsi_txd, TX_STAGE_ONCOMMIT,
				  free_commit_cb, fca);
	if (rc == 0)
		fca = NULL;	/* Will be freed by commit callback */
done:
	/* Commit/Abort transaction on success/error */
	rc = rc ? umem_tx_abort(umem, rc) : umem_tx_commit(umem);
	/* Migrate the expired aggregated free extents to compound index */
	if (rc == 0)
		migrate_free_exts(vsi, true);
error:
	/*
	 * -DER_NONEXIST or -DER_ENOENT could be ignored by some caller,
	 * let's convert them to more serious error here.
	 */
	if (rc == -DER_NONEXIST || rc == -DER_ENOENT)
		rc = -DER_INVAL;

	if (fca != NULL)
		D_FREE(fca);
	return rc;
}

/* Set an arbitrary age to a free extent with specified start offset. */
int
vea_set_ext_age(struct vea_space_info *vsi, uint64_t blk_off, uint64_t age)
{
	D_ASSERT(vsi != NULL);
	return 0;
}

/* Convert an extent into an allocated extent vector. */
int
vea_get_ext_vector(struct vea_space_info *vsi, uint64_t blk_off,
		   uint32_t blk_cnt, struct vea_ext_vector *ext_vector)
{
	D_ASSERT(vsi != NULL);
	D_ASSERT(ext_vector != NULL);
	return 0;
}

/* Load persistent hint data and initialize in-memory hint context */
int
vea_hint_load(struct vea_hint_df *phd, struct vea_hint_context **thc)
{
	D_ASSERT(phd != NULL);
	D_ASSERT(thc != NULL);
	struct vea_hint_context *hint_ctxt;

	D_ALLOC_PTR(hint_ctxt);
	if (hint_ctxt == NULL)
		return -DER_NOMEM;

	hint_ctxt->vhc_pd = phd;
	hint_ctxt->vhc_off = phd->vhd_off;
	hint_ctxt->vhc_seq = phd->vhd_seq;
	*thc = hint_ctxt;

	return 0;
}

/* Free memory foot-print created by vea_hint_load() */
void
vea_hint_unload(struct vea_hint_context *thc)
{
	D_FREE(thc);
}

static int
count_free_persistent(daos_handle_t ih, d_iov_t *key, d_iov_t *val,
		      void *arg)
{
	struct vea_free_extent	*vfe;
	uint64_t		*off, *free_blks = arg;
	int			 rc;

	off = (uint64_t *)key->iov_buf;
	vfe = (struct vea_free_extent *)val->iov_buf;

	rc = verify_free_entry(off, vfe);
	if (rc != 0)
		return rc;

	D_ASSERT(free_blks != NULL);
	*free_blks += vfe->vfe_blk_cnt;

	return 0;
}

static int
count_free_transient(daos_handle_t ih, d_iov_t *key, d_iov_t *val,
		     void *arg)
{
	struct vea_entry	*ve;
	uint64_t		*free_blks = arg;

	ve = (struct vea_entry *)val->iov_buf;
	D_ASSERT(free_blks != NULL);
	*free_blks += ve->ve_ext.vfe_blk_cnt;

	return 0;
}

/* Query attributes and statistics */
int
vea_query(struct vea_space_info *vsi, struct vea_attr *attr,
	  struct vea_stat *stat)
{
	D_ASSERT(vsi != NULL);
	if (attr == NULL && stat == NULL)
		return -DER_INVAL;

	/* Trigger free extents migration only for slow query */
	if (stat != NULL)
		migrate_free_exts(vsi, false);

	if (attr != NULL) {
		struct vea_space_df *vsd = vsi->vsi_md;

		attr->va_compat = vsd->vsd_compat;
		attr->va_blk_sz = vsd->vsd_blk_sz;
		attr->va_hdr_blks = vsd->vsd_hdr_blks;
		attr->va_large_thresh = vsi->vsi_class.vfc_large_thresh;
		attr->va_tot_blks = vsd->vsd_tot_blks;
		attr->va_free_blks = vsi->vsi_stat[STAT_FREE_BLKS];
	}

	if (stat != NULL) {
		struct vea_free_class	*vfc = &vsi->vsi_class;
		int			 i, rc;

		stat->vs_free_persistent = 0;
		rc = dbtree_iterate(vsi->vsi_md_free_btr, DAOS_INTENT_DEFAULT,
				    false, count_free_persistent,
				    (void *)&stat->vs_free_persistent);
		if (rc != 0)
			return rc;

		stat->vs_free_transient = 0;
		rc = dbtree_iterate(vsi->vsi_free_btr, DAOS_INTENT_DEFAULT,
				    false, count_free_transient,
				    (void *)&stat->vs_free_transient);
		if (rc != 0)
			return rc;

		stat->vs_large_frags = d_binheap_size(&vfc->vfc_heap);

		stat->vs_small_frags = 0;
		stat->vs_largest_blks = 0;
		for (i = 0; i < vfc->vfc_lru_cnt; i++) {
			struct vea_entry	*ve;
			d_list_t		*lru = &vfc->vfc_lrus[i];

			d_list_for_each_entry(ve, lru, ve_link) {
				stat->vs_small_frags++;
				if (ve->ve_ext.vfe_blk_cnt >
						stat->vs_largest_blks)
					stat->vs_largest_blks =
						ve->ve_ext.vfe_blk_cnt;
			}
		}

		if (!d_binheap_is_empty(&vfc->vfc_heap)) {
			struct d_binheap_node	*root;
			struct vea_entry	*entry;

			root = d_binheap_root(&vfc->vfc_heap);
			entry = container_of(root, struct vea_entry, ve_node);
			stat->vs_largest_blks = entry->ve_ext.vfe_blk_cnt;
		}

		stat->vs_resrv_hint = vsi->vsi_stat[STAT_RESRV_HINT];
		stat->vs_resrv_large = vsi->vsi_stat[STAT_RESRV_LARGE];
		stat->vs_resrv_small = vsi->vsi_stat[STAT_RESRV_SMALL];
		stat->vs_resrv_vec = vsi->vsi_stat[STAT_RESRV_VEC];
	}

	return 0;
}

void
vea_flush(struct vea_space_info *vsi, bool plug)
{
	D_ASSERT(vsi != NULL);

	if (plug) {
		vsi->vsi_agg_time = UINT64_MAX;
		return;
	}

	vsi->vsi_agg_time = 0;
	migrate_free_exts(vsi, false);
}
