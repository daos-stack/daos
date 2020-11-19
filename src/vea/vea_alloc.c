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
#include <daos/dtx.h>
#include "vea_internal.h"

int
compound_vec_alloc(struct vea_space_info *vsi, struct vea_ext_vector *vec)
{
	/* TODO Add in in-memory extent vector tree */
	return 0;
}

static int
compound_alloc(struct vea_space_info *vsi, struct vea_free_extent *vfe,
	       struct vea_entry *entry)
{
	struct vea_free_extent	*remain;
	d_iov_t			 key;
	int			 rc;

	remain = &entry->ve_ext;
	D_ASSERT(remain->vfe_blk_cnt >= vfe->vfe_blk_cnt);
	D_ASSERT(remain->vfe_blk_off == vfe->vfe_blk_off);

	/* Remove the found free extent from compound index */
	free_class_remove(&vsi->vsi_class, entry);

	if (remain->vfe_blk_cnt == vfe->vfe_blk_cnt) {
		d_iov_set(&key, &vfe->vfe_blk_off, sizeof(vfe->vfe_blk_off));
		rc = dbtree_delete(vsi->vsi_free_btr, BTR_PROBE_EQ, &key, NULL);
	} else {
		/* Adjust in-tree offset & length */
		remain->vfe_blk_off += vfe->vfe_blk_cnt;
		remain->vfe_blk_cnt -= vfe->vfe_blk_cnt;
		rc = daos_gettime_coarse(&remain->vfe_age);
		if (rc)
			return rc;

		rc = free_class_add(&vsi->vsi_class, entry);
	}

	return rc;
}

int
reserve_hint(struct vea_space_info *vsi, uint32_t blk_cnt,
	     struct vea_resrvd_ext *resrvd)
{
	struct vea_free_extent vfe;
	struct vea_entry *entry;
	d_iov_t key, val;
	int rc;

	/* No hint offset provided */
	if (resrvd->vre_hint_off == VEA_HINT_OFF_INVAL)
		return 0;

	vfe.vfe_blk_off = resrvd->vre_hint_off;
	vfe.vfe_blk_cnt = blk_cnt;

	/* Fetch & operate on the in-tree record */
	d_iov_set(&key, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
	d_iov_set(&val, NULL, 0);

	D_ASSERT(!daos_handle_is_inval(vsi->vsi_free_btr));
	rc = dbtree_fetch(vsi->vsi_free_btr, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT,
			  &key, NULL, &val);
	if (rc)
		return (rc == -DER_NONEXIST) ? 0 : rc;

	entry = (struct vea_entry *)val.iov_buf;
	/* The matching free extent isn't big enough */
	if (entry->ve_ext.vfe_blk_cnt < vfe.vfe_blk_cnt)
		return 0;

	rc = compound_alloc(vsi, &vfe, entry);
	if (rc)
		return rc;

	resrvd->vre_blk_off = vfe.vfe_blk_off;
	resrvd->vre_blk_cnt = vfe.vfe_blk_cnt;

	vsi->vsi_stat[STAT_RESRV_HINT] += 1;

	D_DEBUG(DB_IO, "["DF_U64", %u]\n", resrvd->vre_blk_off,
		resrvd->vre_blk_cnt);

	return 0;
}

int
reserve_large(struct vea_space_info *vsi, uint32_t blk_cnt,
	      struct vea_resrvd_ext *resrvd)
{
	struct vea_free_class *vfc = &vsi->vsi_class;
	struct vea_free_extent vfe;
	struct vea_entry *entry;
	struct d_binheap_node *root;
	int rc;

	/* No large free extent available */
	if (d_binheap_is_empty(&vfc->vfc_heap))
		return 0;

	root = d_binheap_root(&vfc->vfc_heap);
	entry = container_of(root, struct vea_entry, ve_node);

	D_ASSERT(entry->ve_ext.vfe_blk_cnt > vfc->vfc_large_thresh);
	D_DEBUG(DB_IO, "largest free extent ["DF_U64", %u]\n",
	       entry->ve_ext.vfe_blk_off, entry->ve_ext.vfe_blk_cnt);

	/* The largest free extent can't satisfy huge allocate request */
	if (entry->ve_ext.vfe_blk_cnt < blk_cnt)
		return 0;

	/*
	 * Reserve from the largest free extent when it's idle or too
	 * small for splitting, otherwise, divide it in half-and-half
	 * and reserve from the second half.
	 */
	if (ext_is_idle(&entry->ve_ext) ||
	    (entry->ve_ext.vfe_blk_cnt <= (blk_cnt * 2))) {
		vfe.vfe_blk_off = entry->ve_ext.vfe_blk_off;
		vfe.vfe_blk_cnt = blk_cnt;

		rc = compound_alloc(vsi, &vfe, entry);
		if (rc)
			return rc;

	} else {
		uint32_t half_blks, tot_blks;
		uint64_t blk_off;

		blk_off = entry->ve_ext.vfe_blk_off;
		tot_blks = entry->ve_ext.vfe_blk_cnt;
		half_blks = tot_blks >> 1;
		D_ASSERT(tot_blks >= (half_blks + blk_cnt));

		/* Shrink the original extent to half size */
		free_class_remove(&vsi->vsi_class, entry);
		entry->ve_ext.vfe_blk_cnt = half_blks;
		rc = free_class_add(&vsi->vsi_class, entry);
		if (rc)
			return rc;

		/* Add the remaining part of second half */
		if (tot_blks > (half_blks + blk_cnt)) {
			vfe.vfe_blk_off = blk_off + half_blks + blk_cnt;
			vfe.vfe_blk_cnt = tot_blks - half_blks - blk_cnt;
			rc = daos_gettime_coarse(&vfe.vfe_age);
			if (rc)
				return rc;

			rc = compound_free(vsi, &vfe, VEA_FL_NO_MERGE |
						VEA_FL_NO_ACCOUNTING);
			if (rc)
				return rc;
		}
		vfe.vfe_blk_off = blk_off + half_blks;
	}

	resrvd->vre_blk_off = vfe.vfe_blk_off;
	resrvd->vre_blk_cnt = blk_cnt;

	vsi->vsi_stat[STAT_RESRV_LARGE] += 1;

	D_DEBUG(DB_IO, "["DF_U64", %u]\n", resrvd->vre_blk_off,
		resrvd->vre_blk_cnt);

	return 0;
}

#define EXT_AGE_WEIGHT	300	/* seconds */

static void
cursor_update(struct free_ext_cursor *cursor, struct vea_entry *ent,
	      int ent_idx)
{
	uint64_t age_cur, age_next;

	D_ASSERT(ent != NULL);

	if (cursor->fec_cur == NULL)
		goto update;
	else if (ext_is_idle(&cursor->fec_cur->ve_ext))
		return;
	else if (ext_is_idle(&ent->ve_ext))
		goto update;

	D_ASSERT(!ext_is_idle(&cursor->fec_cur->ve_ext));
	D_ASSERT(!ext_is_idle(&ent->ve_ext));

	age_cur = cursor->fec_cur->ve_ext.vfe_age;
	age_cur += (uint64_t)cursor->fec_idx * EXT_AGE_WEIGHT;
	age_next = ent->ve_ext.vfe_age;
	age_next += (uint64_t)ent_idx * EXT_AGE_WEIGHT;

	if (age_cur <= age_next)
		return;
update:
	cursor->fec_cur = ent;
	cursor->fec_idx = ent_idx;
}

static struct free_ext_cursor *
cursor_prepare(struct vea_free_class *vfc, uint32_t blk_cnt)
{
	struct free_ext_cursor *cursor = vfc->vfc_cursor;
	struct vea_entry *entry;
	int i, e_cnt;

	for (e_cnt = 0; e_cnt < vfc->vfc_lru_cnt; e_cnt++) {
		if (blk_cnt > vfc->vfc_sizes[e_cnt])
			break;
	}

	cursor->fec_cur = NULL;
	cursor->fec_idx = 0;
	cursor->fec_entry_cnt = e_cnt;
	memset(cursor->fec_entries, 0, vfc->vfc_lru_cnt * sizeof(entry));

	/* Initialize vea_entry pointer array, setup starting entry */
	for (i = 0; i < e_cnt; i++) {
		d_list_t *lru = &vfc->vfc_lrus[i];

		if (d_list_empty(lru)) {
			cursor->fec_entries[i] = NULL;
			continue;
		}

		entry = d_list_entry(lru->next, struct vea_entry, ve_link);
		cursor->fec_entries[i] = entry;
		cursor_update(cursor, entry, i);
	}

	return cursor;
}

static struct vea_entry *
cursor_next(struct vea_free_class *vfc, struct free_ext_cursor *cursor)
{
	struct vea_entry *cur, *next;
	d_list_t *lru_head;
	int i, cur_idx = cursor->fec_idx;

	D_ASSERT(cursor->fec_cur != NULL);
	cur = cursor->fec_cur;
	lru_head = &vfc->vfc_lrus[cur_idx];

	/* Make sure the fec_cur is cleared */
	cursor->fec_cur = NULL;

	if (cur->ve_link.next == lru_head) {
		cursor->fec_entries[cur_idx] = NULL;
	} else {
		next = d_list_entry(cur->ve_link.next, struct vea_entry,
				    ve_link);
		cursor->fec_entries[cur_idx] = next;

		/* Keeping on current size class if the extent is idle */
		if (ext_is_idle(&next->ve_ext)) {
			cursor->fec_cur = next;
			return cursor->fec_cur;
		}
	}

	for (i = 0; i < cursor->fec_entry_cnt; i++) {
		cur = cursor->fec_entries[i];
		if (cur != NULL)
			cursor_update(cursor, cur, i);
	}

	return cursor->fec_cur;
}

int
reserve_small(struct vea_space_info *vsi, uint32_t blk_cnt,
	      struct vea_resrvd_ext *resrvd)
{
	struct vea_free_extent vfe;
	struct vea_entry *entry;
	struct free_ext_cursor *cursor;
	int rc = 0;

	/* Skip huge allocate request */
	if (blk_cnt > vsi->vsi_class.vfc_large_thresh)
		return 0;

	cursor = cursor_prepare(&vsi->vsi_class, blk_cnt);
	D_ASSERT(cursor != NULL);

	entry = cursor->fec_cur;
	while (entry != NULL) {
		if (entry->ve_ext.vfe_blk_cnt >= blk_cnt) {
			vfe.vfe_blk_off = entry->ve_ext.vfe_blk_off;
			vfe.vfe_blk_cnt = blk_cnt;

			rc = compound_alloc(vsi, &vfe, entry);
			if (rc)
				break;

			resrvd->vre_blk_off = vfe.vfe_blk_off;
			resrvd->vre_blk_cnt = blk_cnt;

			vsi->vsi_stat[STAT_RESRV_SMALL] += 1;

			D_DEBUG(DB_IO, "["DF_U64", %u]\n",
				resrvd->vre_blk_off, resrvd->vre_blk_cnt);
			break;
		}
		entry = cursor_next(&vsi->vsi_class, cursor);
	}

	return rc;
}

int
reserve_vector(struct vea_space_info *vsi, uint32_t blk_cnt,
	       struct vea_resrvd_ext *resrvd)
{
	/* TODO reserve extent vector for non-contiguous allocation */
	return -DER_NOSPACE;
}

int
persistent_alloc(struct vea_space_info *vsi, struct vea_free_extent *vfe)
{
	struct vea_free_extent *found, frag;
	daos_handle_t btr_hdl;
	d_iov_t key_in, key_out, val;
	uint64_t *blk_off, found_end, vfe_end;
	int rc, opc = BTR_PROBE_LE;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK ||
		 vsi->vsi_umem->umm_id == UMEM_CLASS_VMEM);
	D_ASSERT(vfe->vfe_blk_off != VEA_HINT_OFF_INVAL);
	D_ASSERT(vfe->vfe_blk_cnt > 0);

	btr_hdl = vsi->vsi_md_free_btr;
	D_ASSERT(!daos_handle_is_inval(btr_hdl));

	D_DEBUG(DB_IO, "Persistent alloc ["DF_U64", %u]\n",
		vfe->vfe_blk_off, vfe->vfe_blk_cnt);

	/* Fetch & operate on the in-tree record */
	d_iov_set(&key_in, &vfe->vfe_blk_off, sizeof(vfe->vfe_blk_off));
	d_iov_set(&key_out, NULL, sizeof(*blk_off));
	d_iov_set(&val, NULL, sizeof(*found));

	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key_in, &key_out,
			  &val);
	if (rc) {
		D_ERROR("failed to find extent ["DF_U64", %u]\n",
			vfe->vfe_blk_off, vfe->vfe_blk_cnt);
		return rc;
	}

	found = (struct vea_free_extent *)val.iov_buf;
	blk_off = (uint64_t *)key_out.iov_buf;

	rc = verify_free_entry(blk_off, found);
	if (rc)
		return rc;

	found_end = found->vfe_blk_off + found->vfe_blk_cnt;
	vfe_end = vfe->vfe_blk_off + vfe->vfe_blk_cnt;

	if (found->vfe_blk_off > vfe->vfe_blk_off || found_end < vfe_end) {
		D_ERROR("mismatched extent ["DF_U64", %u] ["DF_U64", %u]\n",
			found->vfe_blk_off, found->vfe_blk_cnt,
			vfe->vfe_blk_off, vfe->vfe_blk_cnt);
		return -DER_INVAL;
	}

	if (found->vfe_blk_off < vfe->vfe_blk_off) {
		/* Adjust the in-tree free extent length */
		rc = umem_tx_add_ptr(vsi->vsi_umem, &found->vfe_blk_cnt,
				     sizeof(found->vfe_blk_cnt));
		if (rc)
			return rc;

		found->vfe_blk_cnt = vfe->vfe_blk_off - found->vfe_blk_off;

		/* Add back the rear part of free extent */
		if (found_end > vfe_end) {
			frag.vfe_blk_off = vfe->vfe_blk_off + vfe->vfe_blk_cnt;
			frag.vfe_blk_cnt = found_end - vfe_end;
			rc = daos_gettime_coarse(&frag.vfe_age);
			if (rc)
				return rc;

			d_iov_set(&key_in, &frag.vfe_blk_off,
				  sizeof(frag.vfe_blk_off));
			d_iov_set(&val, &frag, sizeof(frag));
			rc = dbtree_update(btr_hdl, &key_in, &val);
			if (rc)
				return rc;
		}
	} else if (found_end > vfe_end) {
		/* Adjust the in-tree extent offset & length */
		rc = umem_tx_add_ptr(vsi->vsi_umem, found, sizeof(*found));
		if (rc)
			return rc;

		found->vfe_blk_off = vfe->vfe_blk_off + vfe->vfe_blk_cnt;
		found->vfe_blk_cnt = found_end - vfe_end;
		rc = daos_gettime_coarse(&found->vfe_age);
		if (rc)
			return rc;
	} else {
		/* Remove the original free extent from persistent tree */
		rc = dbtree_delete(btr_hdl, BTR_PROBE_BYPASS, &key_out, NULL);
		if (rc)
			return rc;
	}

	return 0;
}
