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

enum vea_free_type {
	VEA_TYPE_COMPOUND,
	VEA_TYPE_AGGREGATE,
	VEA_TYPE_PERSIST,
};

static d_list_t *
blkcnt_to_lru(struct vea_free_class *vfc, uint32_t blkcnt)
{
	int idx;

	D_ASSERTF(blkcnt <= vfc->vfc_sizes[0], "%u, %u\n",
		  blkcnt, vfc->vfc_sizes[0]);
	D_ASSERT(vfc->vfc_lru_cnt > 0);

	for (idx = 0; idx < (vfc->vfc_lru_cnt - 1); idx++) {
		if (blkcnt > vfc->vfc_sizes[idx + 1])
			break;
	}

	return &vfc->vfc_lrus[idx];
}

void
free_class_remove(struct vea_free_class *vfc, struct vea_entry *entry)
{
	if (entry->ve_in_heap) {
		D_ASSERTF(entry->ve_ext.vfe_blk_cnt > vfc->vfc_large_thresh,
			  "%u <= %u", entry->ve_ext.vfe_blk_cnt,
			  vfc->vfc_large_thresh);
		d_binheap_remove(&vfc->vfc_heap, &entry->ve_node);
		entry->ve_in_heap = 0;
	}
	d_list_del_init(&entry->ve_link);
}

int
free_class_add(struct vea_free_class *vfc, struct vea_entry *entry)
{
	int rc;

	D_ASSERT(entry->ve_in_heap == 0);
	D_ASSERT(d_list_empty(&entry->ve_link));

	/* Add to heap if it's a large free extent */
	if (entry->ve_ext.vfe_blk_cnt > vfc->vfc_large_thresh) {
		rc = d_binheap_insert(&vfc->vfc_heap, &entry->ve_node);
		if (rc != 0) {
			D_ERROR("Failed to insert heap: %d\n", rc);
			return rc;
		}

		entry->ve_in_heap = 1;
	} else { /* Otherwise add to one of size categarized LRU */
		struct vea_entry *cur;
		d_list_t *lru_head, *tmp;

		lru_head = blkcnt_to_lru(vfc, entry->ve_ext.vfe_blk_cnt);

		/* List is sorted by free extent age */
		d_list_for_each_prev(tmp, lru_head) {
			cur = d_list_entry(tmp, struct vea_entry, ve_link);

			if (entry->ve_ext.vfe_age >= cur->ve_ext.vfe_age) {
				d_list_add(&entry->ve_link, tmp);
				break;
			}
		}
		if (d_list_empty(&entry->ve_link))
			d_list_add(&entry->ve_link, lru_head);
	}

	return 0;
}

static void
undock_entry(struct vea_space_info *vsi, struct vea_entry *entry,
	     unsigned int type)
{
	if (type == VEA_TYPE_PERSIST)
		return;

	D_ASSERT(entry != NULL);
	if (type == VEA_TYPE_COMPOUND)
		free_class_remove(&vsi->vsi_class, entry);
	else
		d_list_del_init(&entry->ve_link);
}

static int
dock_entry(struct vea_space_info *vsi, struct vea_entry *entry,
	   unsigned int type)
{
	int rc = 0;

	if (type == VEA_TYPE_PERSIST)
		return rc;

	D_ASSERT(entry != NULL);
	if (type == VEA_TYPE_COMPOUND) {
		rc = free_class_add(&vsi->vsi_class, entry);
	} else {
		D_ASSERT(type == VEA_TYPE_AGGREGATE);
		D_ASSERT(d_list_empty(&entry->ve_link));
		d_list_add_tail(&entry->ve_link, &vsi->vsi_agg_lru);
	}

	return rc;
}

/*
 * Make sure there is no overlapping or duplicated extents in the free
 * extent tree. The passed in @ext_in will be merged with adjacent extents
 * and being inserted in the tree.
 *
 * Return value:	0	- Not merged
 *			1	- Merged in tree
 *			-ve	- Error
 */
static int
merge_free_ext(struct vea_space_info *vsi, struct vea_free_extent *ext_in,
	       unsigned int type, unsigned int flags)
{
	struct vea_free_extent	*ext, *neighbor = NULL;
	struct vea_free_extent	 merged = *ext_in;
	struct vea_entry	*entry, *neighbor_entry = NULL;
	daos_handle_t		 btr_hdl;
	d_iov_t			 key, key_out, val;
	uint64_t		*off;
	int			 rc, opc = BTR_PROBE_LE;

	if (type == VEA_TYPE_COMPOUND)
		btr_hdl = vsi->vsi_free_btr;
	else if (type == VEA_TYPE_PERSIST)
		btr_hdl = vsi->vsi_md_free_btr;
	else if (type  == VEA_TYPE_AGGREGATE)
		btr_hdl = vsi->vsi_agg_btr;
	else
		return -DER_INVAL;

	D_ASSERT(!daos_handle_is_inval(btr_hdl));
	d_iov_set(&key, &ext_in->vfe_blk_off, sizeof(ext_in->vfe_blk_off));
repeat:
	d_iov_set(&key_out, NULL, 0);
	d_iov_set(&val, NULL, 0);

	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key, &key_out,
			  &val);
	if (rc == -DER_NONEXIST && opc == BTR_PROBE_LE) {
		opc = BTR_PROBE_GE;
		goto repeat;
	}

	if (rc == -DER_NONEXIST)
		goto done;	/* Merge done */
	else if (rc)
		return rc;	/* Error */

	if (type == VEA_TYPE_PERSIST) {
		entry = NULL;
		ext = (struct vea_free_extent *)val.iov_buf;
	} else {
		entry = (struct vea_entry *)val.iov_buf;
		ext = &entry->ve_ext;
	}

	off = (uint64_t *)key_out.iov_buf;
	rc = verify_free_entry(off, ext);
	if (rc != 0)
		return rc;

	/* This checks overlapping & duplicated extents as well. */
	rc = (opc == BTR_PROBE_LE) ? ext_adjacent(ext, &merged) :
				     ext_adjacent(&merged, ext);
	if (rc < 0)
		return rc;

	if (rc > 0) {
		if (flags & VEA_FL_NO_MERGE) {
			D_ERROR("unexpected adjacent extents:"
				" ["DF_U64", %u], ["DF_U64", %u]\n",
				merged.vfe_blk_off, merged.vfe_blk_cnt,
				ext->vfe_blk_off, ext->vfe_blk_cnt);
			return -DER_INVAL;
		}

		if (opc == BTR_PROBE_LE) {
			merged.vfe_blk_off = ext->vfe_blk_off;
			merged.vfe_blk_cnt += ext->vfe_blk_cnt;

			neighbor = ext;
			neighbor_entry = entry;
		} else {
			merged.vfe_blk_cnt += ext->vfe_blk_cnt;

			/*
			 * Prev adjacent extent will be kept, remove the next
			 * adjacent extent.
			 */
			if (neighbor != NULL) {
				undock_entry(vsi, entry, type);
				rc = dbtree_delete(btr_hdl, BTR_PROBE_BYPASS,
						   &key_out, NULL);
				if (rc) {
					D_ERROR("Failed to delete: %d\n", rc);
					return rc;
				}
			} else {
				neighbor = ext;
				neighbor_entry = entry;
			}
		}
	}

	if (opc == BTR_PROBE_LE) {
		opc = BTR_PROBE_GE;
		goto repeat;
	}
done:
	if (neighbor == NULL)
		return 0;

	if (type == VEA_TYPE_PERSIST) {
		rc = umem_tx_add_ptr(vsi->vsi_umem, neighbor,
				     sizeof(*neighbor));
		if (rc) {
			D_ERROR("Failed add ptr into tx: %d\n", rc);
			return rc;
		}
	} else {
		undock_entry(vsi, neighbor_entry, type);
	}

	/* Adjust in-tree offset & length */
	neighbor->vfe_blk_off = merged.vfe_blk_off;
	neighbor->vfe_blk_cnt = merged.vfe_blk_cnt;
	/* Only bump age for aging tree */
	if (type == VEA_TYPE_AGGREGATE)
		neighbor->vfe_age = merged.vfe_age;

	rc = dock_entry(vsi, neighbor_entry, type);
	if (rc < 0)
		return rc;

	return 1;
}

/* Free extent to in-memory compound index */
int
compound_free(struct vea_space_info *vsi, struct vea_free_extent *vfe,
	      unsigned int flags)
{
	struct vea_entry	*entry, dummy;
	d_iov_t			 key, val;
	int			 rc;

	rc = merge_free_ext(vsi, vfe, VEA_TYPE_COMPOUND, flags);
	if (rc < 0) {
		return rc;
	} else if (rc > 0) {
		rc = 0;	/* extent merged in tree */
		goto accounting;
	}

	memset(&dummy, 0, sizeof(dummy));
	D_INIT_LIST_HEAD(&dummy.ve_link);
	dummy.ve_ext = *vfe;

	/* Add to in-memory free extent tree */
	D_ASSERT(!daos_handle_is_inval(vsi->vsi_free_btr));
	d_iov_set(&key, &dummy.ve_ext.vfe_blk_off,
		  sizeof(dummy.ve_ext.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));

	rc = dbtree_update(vsi->vsi_free_btr, &key, &val);
	if (rc != 0)
		return rc;

	/* Fetch & operate on the in-tree record from now on */
	d_iov_set(&key, &dummy.ve_ext.vfe_blk_off,
		  sizeof(dummy.ve_ext.vfe_blk_off));
	d_iov_set(&val, NULL, 0);

	rc = dbtree_fetch(vsi->vsi_free_btr, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT,
			  &key, NULL, &val);
	D_ASSERT(rc != -DER_NONEXIST);
	if (rc)
		return rc;

	entry = (struct vea_entry *)val.iov_buf;
	D_INIT_LIST_HEAD(&entry->ve_link);

	rc = free_class_add(&vsi->vsi_class, entry);

accounting:
	if (!rc && !(flags & VEA_FL_NO_ACCOUNTING))
		vsi->vsi_stat[STAT_FREE_BLKS] += vfe->vfe_blk_cnt;
	return rc;
}

/* Free extent to persistent free tree */
int
persistent_free(struct vea_space_info *vsi, struct vea_free_extent *vfe)
{
	struct vea_free_extent	dummy;
	d_iov_t			key, val;
	daos_handle_t		btr_hdl = vsi->vsi_md_free_btr;
	int			rc;

	rc = merge_free_ext(vsi, vfe, VEA_TYPE_PERSIST, 0);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		return 0;	/* extent merged in tree */

	memset(&dummy, 0, sizeof(dummy));
	dummy = *vfe;
	dummy.vfe_age = VEA_EXT_AGE_MAX;

	/* Add to persistent free extent tree */
	D_ASSERT(!daos_handle_is_inval(btr_hdl));
	d_iov_set(&key, &dummy.vfe_blk_off, sizeof(dummy.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));

	rc = dbtree_update(btr_hdl, &key, &val);
	return rc;
}

/* Free extent to the aggregate free tree */
int
aggregated_free(struct vea_space_info *vsi, struct vea_free_extent *vfe)
{
	struct vea_entry	*entry, dummy;
	d_iov_t			 key, val;
	daos_handle_t		 btr_hdl = vsi->vsi_agg_btr;
	int			 rc;

	rc = daos_gettime_coarse(&vfe->vfe_age);
	if (rc)
		return rc;

	rc = merge_free_ext(vsi, vfe, VEA_TYPE_AGGREGATE, 0);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		return 0;	/* extent merged in tree */

	memset(&dummy, 0, sizeof(dummy));
	D_INIT_LIST_HEAD(&dummy.ve_link);
	dummy.ve_ext = *vfe;

	/* Add to in-memory aggregate free extent tree */
	D_ASSERT(!daos_handle_is_inval(btr_hdl));
	d_iov_set(&key, &dummy.ve_ext.vfe_blk_off,
		  sizeof(dummy.ve_ext.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));

	rc = dbtree_update(btr_hdl, &key, &val);
	if (rc)
		return rc;

	/* Fetch & operate on the in-tree record from now on */
	d_iov_set(&key, &dummy.ve_ext.vfe_blk_off,
		  sizeof(dummy.ve_ext.vfe_blk_off));
	d_iov_set(&val, NULL, 0);

	rc = dbtree_fetch(btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, &key,
			  NULL, &val);
	D_ASSERT(rc != -DER_NONEXIST);
	if (rc)
		return rc;

	entry = (struct vea_entry *)val.iov_buf;
	D_INIT_LIST_HEAD(&entry->ve_link);

	/* Add to the tail of aggregate LRU list */
	d_list_add_tail(&entry->ve_link, &vsi->vsi_agg_lru);

	return 0;
}

struct vea_unmap_extent {
	struct vea_free_extent	vue_ext;
	d_list_t		vue_link;
};

void
migrate_end_cb(void *data, bool noop)
{
	struct vea_space_info	*vsi = data;
	struct vea_entry	*entry, *tmp;
	struct vea_free_extent	 vfe;
	struct vea_unmap_extent	*vue, *tmp_vue;
	d_list_t		 unmap_list;
	uint64_t		 cur_time = 0;
	int			 rc;

	if (noop)
		return;

	rc = daos_gettime_coarse(&cur_time);
	if (rc)
		return;

	if (vsi->vsi_agg_time == UINT64_MAX ||
	    cur_time < (vsi->vsi_agg_time + VEA_MIGRATE_INTVL))
		return;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_NONE);
	D_ASSERT(vsi != NULL);
	D_INIT_LIST_HEAD(&unmap_list);

	d_list_for_each_entry_safe(entry, tmp, &vsi->vsi_agg_lru, ve_link) {
		d_iov_t	key;

		vfe = entry->ve_ext;
		/* Not force migration, and the oldest extent isn't expired */
		if (vsi->vsi_agg_time != 0 &&
		    cur_time < (vfe.vfe_age + VEA_MIGRATE_INTVL))
			break;

		/* Remove entry from aggregate LRU list */
		d_list_del_init(&entry->ve_link);
		/*
		 * Remove entry from aggregate tree, entry will be freed on
		 * deletion.
		 */
		d_iov_set(&key, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
		D_ASSERT(!daos_handle_is_inval(vsi->vsi_agg_btr));
		rc = dbtree_delete(vsi->vsi_agg_btr, BTR_PROBE_EQ, &key, NULL);
		if (rc) {
			D_ERROR("Remove ["DF_U64", %u] from aggregated "
				"tree error: %d\n", vfe.vfe_blk_off,
				vfe.vfe_blk_cnt, rc);
			break;
		}

		/*
		 * Unmap callback may yield, so we can't call it directly in
		 * this tight loop.
		 */
		if (vsi->vsi_unmap_ctxt.vnc_unmap != NULL) {
			D_ALLOC_PTR(vue);
			if (vue == NULL) {
				rc = -DER_NOMEM;
				break;
			}

			vue->vue_ext = vfe;
			d_list_add_tail(&vue->vue_link, &unmap_list);
		} else {
			vfe.vfe_age = cur_time;
			rc = compound_free(vsi, &vfe, 0);
			if (rc) {
				D_ERROR("Compound free ["DF_U64", %u] error: "
					"%d\n", vfe.vfe_blk_off,
					vfe.vfe_blk_cnt, rc);
				break;
			}
		}
	}

	/* Update aggregation time before yield */
	vsi->vsi_agg_time = cur_time;
	vsi->vsi_agg_scheduled = false;

	/*
	 * According to NVMe spec, unmap isn't an expensive non-queue command
	 * anymore, so we should just unmap as soon as the extent is freed.
	 */
	d_list_for_each_entry_safe(vue, tmp_vue, &unmap_list, vue_link) {
		uint32_t blk_sz = vsi->vsi_md->vsd_blk_sz;
		uint64_t off = vue->vue_ext.vfe_blk_off * blk_sz;
		uint64_t cnt = (uint64_t)vue->vue_ext.vfe_blk_cnt * blk_sz;

		d_list_del(&vue->vue_link);

		/*
		 * Since unmap could yield, it must be called before
		 * compound_free(), otherwise, the extent could be visible
		 * for allocation before unmap done.
		 */
		rc = vsi->vsi_unmap_ctxt.vnc_unmap(off, cnt,
					vsi->vsi_unmap_ctxt.vnc_data);
		if (rc)
			D_ERROR("Unmap ["DF_U64", "DF_U64"] error: %d\n",
				off, cnt, rc);

		vue->vue_ext.vfe_age = cur_time;
		rc = compound_free(vsi, &vue->vue_ext, 0);
		if (rc)
			D_ERROR("Compound free ["DF_U64", %u] error: %d\n",
				vue->vue_ext.vfe_blk_off,
				vue->vue_ext.vfe_blk_cnt, rc);
		D_FREE(vue);
	}
}

void
migrate_free_exts(struct vea_space_info *vsi, bool add_tx_cb)
{
	uint64_t	cur_time;
	int		rc;

	/* Perform the migration instantly if not in a transaction */
	if (pmemobj_tx_stage() == TX_STAGE_NONE) {
		migrate_end_cb((void *)vsi, false);
		return;
	}

	/*
	 * Skip this free extent migration if the transaction is started
	 * without tx callback data provided, see umem_tx_begin().
	 */
	if (!add_tx_cb)
		return;

	/*
	 * Check aggregation time in advance to avoid unnecessary
	 * umem_tx_add_callback() calls.
	 */
	rc = daos_gettime_coarse(&cur_time);
	if (rc)
		return;

	if (vsi->vsi_agg_time == UINT64_MAX ||
	    cur_time < (vsi->vsi_agg_time + VEA_MIGRATE_INTVL))
		return;

	/* Schedule one migrate_end_cb() is enough */
	if (vsi->vsi_agg_scheduled)
		return;

	/*
	 * Perform the migration in transaction end callback, since the
	 * migration could yield on blob unmap.
	 */
	rc = umem_tx_add_callback(vsi->vsi_umem, vsi->vsi_txd, TX_STAGE_NONE,
				  migrate_end_cb, vsi);
	if (rc) {
		D_ERROR("Add transaction end callback error "DF_RC"\n",
			DP_RC(rc));
		return;
	}
	vsi->vsi_agg_scheduled = true;
}
