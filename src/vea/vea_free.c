/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

void
free_class_remove(struct vea_space_info *vsi, struct vea_entry *entry)
{
	struct vea_free_class	*vfc = &vsi->vsi_class;
	struct vea_sized_class	*sc = entry->ve_sized_class;
	uint32_t		 blk_cnt = entry->ve_ext.vfe_blk_cnt;

	if (sc == NULL) {
		D_ASSERTF(blk_cnt > vfc->vfc_large_thresh, "%u <= %u",
			  blk_cnt, vfc->vfc_large_thresh);
		D_ASSERT(d_list_empty(&entry->ve_link));

		d_binheap_remove(&vfc->vfc_heap, &entry->ve_node);
		dec_stats(vsi, STAT_FRAGS_LARGE, 1);
	} else {
		d_iov_t		key;
		uint64_t	int_key = blk_cnt;
		int		rc;

		D_ASSERTF(blk_cnt > 0 && blk_cnt <= vfc->vfc_large_thresh,
			  "%u > %u", blk_cnt, vfc->vfc_large_thresh);
		D_ASSERT(daos_handle_is_valid(vfc->vfc_size_btr));

		d_list_del_init(&entry->ve_link);
		entry->ve_sized_class = NULL;
		/* Remove the sized class when it's empty */
		if (d_list_empty(&sc->vsc_lru)) {
			d_iov_set(&key, &int_key, sizeof(int_key));
			rc = dbtree_delete(vfc->vfc_size_btr, BTR_PROBE_EQ, &key, NULL);
			if (rc)
				D_ERROR("Remove size class:%u failed, "DF_RC"\n",
					blk_cnt, DP_RC(rc));
		}
		dec_stats(vsi, STAT_FRAGS_SMALL, 1);
	}
}

int
free_class_add(struct vea_space_info *vsi, struct vea_entry *entry)
{
	struct vea_free_class	*vfc = &vsi->vsi_class;
	daos_handle_t		 btr_hdl = vfc->vfc_size_btr;
	uint32_t		 blk_cnt = entry->ve_ext.vfe_blk_cnt;
	d_iov_t			 key, val, val_out;
	uint64_t		 int_key = blk_cnt;
	struct vea_sized_class	 dummy, *sc;
	int			 rc;

	D_ASSERT(entry->ve_sized_class == NULL);
	D_ASSERT(d_list_empty(&entry->ve_link));

	/* Add to heap if it's a large free extent */
	if (blk_cnt > vfc->vfc_large_thresh) {
		rc = d_binheap_insert(&vfc->vfc_heap, &entry->ve_node);
		if (rc != 0) {
			D_ERROR("Failed to insert heap: %d\n", rc);
			return rc;
		}

		inc_stats(vsi, STAT_FRAGS_LARGE, 1);
		return 0;
	}

	/* Add to a sized class */
	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &int_key, sizeof(int_key));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_fetch(btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, &key, NULL, &val_out);
	if (rc == 0) {
		/* Found an existing sized class */
		sc = (struct vea_sized_class *)val_out.iov_buf;
		D_ASSERT(sc != NULL);
		D_ASSERT(!d_list_empty(&sc->vsc_lru));
	} else if (rc == -DER_NONEXIST) {
		/* Create a new sized class */
		d_iov_set(&val, &dummy, sizeof(dummy));
		d_iov_set(&val_out, NULL, 0);

		rc = dbtree_upsert(btr_hdl, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key, &val,
				   &val_out);
		if (rc != 0) {
			D_ERROR("Insert size class:%u failed. "DF_RC"\n",
				blk_cnt, DP_RC(rc));
			return rc;
		}
		sc = (struct vea_sized_class *)val_out.iov_buf;
		D_ASSERT(sc != NULL);
		D_INIT_LIST_HEAD(&sc->vsc_lru);
	} else {
		D_ERROR("Lookup size class:%u failed. "DF_RC"\n", blk_cnt, DP_RC(rc));
		return rc;
	}

	entry->ve_sized_class = sc;
	d_list_add_tail(&entry->ve_link, &sc->vsc_lru);

	inc_stats(vsi, STAT_FRAGS_SMALL, 1);
	return 0;
}

static void
undock_entry(struct vea_space_info *vsi, struct vea_entry *entry,
	     unsigned int type)
{
	if (type == VEA_TYPE_PERSIST)
		return;

	D_ASSERT(entry != NULL);
	if (type == VEA_TYPE_COMPOUND) {
		free_class_remove(vsi, entry);
	} else {
		d_list_del_init(&entry->ve_link);
		dec_stats(vsi, STAT_FRAGS_AGING, 1);
	}
}

#define LARGE_AGING_FRAG_BLKS	8192

static inline bool
is_aging_frag_large(struct vea_free_extent *vfe)
{
	return vfe->vfe_blk_cnt >= LARGE_AGING_FRAG_BLKS;
}

static inline void
dock_aging_entry(struct vea_space_info *vsi, struct vea_entry *entry)
{
	d_list_add_tail(&entry->ve_link, &vsi->vsi_agg_lru);
	inc_stats(vsi, STAT_FRAGS_AGING, 1);
}

static int
dock_entry(struct vea_space_info *vsi, struct vea_entry *entry, unsigned int type)
{
	int rc = 0;

	D_ASSERT(entry != NULL);
	if (type == VEA_TYPE_COMPOUND) {
		rc = free_class_add(vsi, entry);
	} else {
		D_ASSERT(type == VEA_TYPE_AGGREGATE);
		D_ASSERT(d_list_empty(&entry->ve_link));
		dock_aging_entry(vsi, entry);
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
	bool			 fetch_prev = true, large_prev = false;
	int			 rc, del_opc = BTR_PROBE_BYPASS;

	if (type == VEA_TYPE_COMPOUND)
		btr_hdl = vsi->vsi_free_btr;
	else if (type == VEA_TYPE_PERSIST)
		btr_hdl = vsi->vsi_md_free_btr;
	else if (type  == VEA_TYPE_AGGREGATE)
		btr_hdl = vsi->vsi_agg_btr;
	else
		return -DER_INVAL;

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &ext_in->vfe_blk_off, sizeof(ext_in->vfe_blk_off));
	d_iov_set(&key_out, NULL, 0);
	d_iov_set(&val, NULL, 0);

	/*
	 * Search the one to be merged, the btree trace will be set to proper position for
	 * later potential insertion (when merge failed), so don't change btree trace!
	 */
	rc = dbtree_fetch(btr_hdl, BTR_PROBE_EQ, DAOS_INTENT_DEFAULT, &key, &key_out, &val);
	if (rc == 0) {
		D_ERROR("unexpected extent ["DF_U64", %u]\n",
			ext_in->vfe_blk_off, ext_in->vfe_blk_cnt);
		return -DER_INVAL;
	} else if (rc != -DER_NONEXIST) {
		D_ERROR("search extent with offset "DF_U64" failed. "DF_RC"\n",
			ext_in->vfe_blk_off, DP_RC(rc));
		return rc;
	}
repeat:
	d_iov_set(&key_out, NULL, 0);
	d_iov_set(&val, NULL, 0);

	if (fetch_prev) {
		rc = dbtree_fetch_prev(btr_hdl, &key_out, &val, false);
		if (rc == -DER_NONEXIST) {
			fetch_prev = false;
			goto repeat;
		} else if (rc) {
			D_ERROR("search prev extent failed. "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	} else {
		/*
		 * The btree trace was set to the position for inserting the searched
		 * one. If there is an extent in current position, let's try to merge
		 * it with the searched one; otherwise, we'd lookup next to see if any
		 * extent can be merged.
		 */
		rc = dbtree_fetch_cur(btr_hdl, &key_out, &val);
		if (rc == -DER_NONEXIST) {
			del_opc = BTR_PROBE_EQ;
			rc = dbtree_fetch_next(btr_hdl, &key_out, &val, false);
		}

		if (rc == -DER_NONEXIST) {
			goto done; /* Merge done */
		} else if (rc) {
			D_ERROR("search next extent failed. "DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

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
	rc = fetch_prev ? ext_adjacent(ext, &merged) : ext_adjacent(&merged, ext);
	if (rc < 0)
		return rc;

	/*
	 * When the in-tree aging frag is large enough, we'd stop merging with them,
	 * otherwise, the large aging frag could keep growing and stay in aging buffer
	 * for too long time.
	 *
	 * When the 'prev' and 'next' frags are both large, the freed frag will be
	 * merged with 'next'.
	 */
	if (rc > 0 && type == VEA_TYPE_AGGREGATE && is_aging_frag_large(ext)) {
		if (fetch_prev) {
			rc = 0;
			large_prev = true;
		} else if (!large_prev) {
			rc = 0;
		}
	}

	if (rc > 0) {
		if (flags & VEA_FL_NO_MERGE) {
			D_ERROR("unexpected adjacent extents:"
				" ["DF_U64", %u], ["DF_U64", %u]\n",
				merged.vfe_blk_off, merged.vfe_blk_cnt,
				ext->vfe_blk_off, ext->vfe_blk_cnt);
			return -DER_INVAL;
		}

		if (fetch_prev) {
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
				rc = dbtree_delete(btr_hdl, del_opc, &key_out, NULL);
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

	if (fetch_prev) {
		fetch_prev = false;
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

	if (type == VEA_TYPE_AGGREGATE || type == VEA_TYPE_COMPOUND) {
		neighbor->vfe_age = merged.vfe_age;
		rc = dock_entry(vsi, neighbor_entry, type);
		if (rc < 0)
			return rc;
	}

	return 1;
}

/* Free extent to in-memory compound index */
int
compound_free(struct vea_space_info *vsi, struct vea_free_extent *vfe,
	      unsigned int flags)
{
	struct vea_entry	*entry, dummy;
	d_iov_t			 key, val, val_out;
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
	D_ASSERT(daos_handle_is_valid(vsi->vsi_free_btr));
	d_iov_set(&key, &dummy.ve_ext.vfe_blk_off, sizeof(dummy.ve_ext.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_upsert(vsi->vsi_free_btr, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key,
			   &val, &val_out);
	if (rc != 0) {
		D_ERROR("Insert compound extent failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ASSERT(val_out.iov_buf != NULL);
	entry = (struct vea_entry *)val_out.iov_buf;
	D_INIT_LIST_HEAD(&entry->ve_link);

	rc = free_class_add(vsi, entry);

accounting:
	if (!rc && !(flags & VEA_FL_NO_ACCOUNTING))
		inc_stats(vsi, STAT_FREE_BLKS, vfe->vfe_blk_cnt);
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
	dummy.vfe_age = 0; /* Not used */

	/* Add to persistent free extent tree */
	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &dummy.vfe_blk_off, sizeof(dummy.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));

	rc = dbtree_upsert(btr_hdl, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key, &val, NULL);
	if (rc)
		D_ERROR("Insert persistent extent failed. "DF_RC"\n", DP_RC(rc));
	return rc;
}

/* Free extent to the aggregate free tree */
int
aggregated_free(struct vea_space_info *vsi, struct vea_free_extent *vfe)
{
	struct vea_entry	*entry, dummy;
	d_iov_t			 key, val, val_out;
	daos_handle_t		 btr_hdl = vsi->vsi_agg_btr;
	int			 rc;

	vfe->vfe_age = get_current_age();
	rc = merge_free_ext(vsi, vfe, VEA_TYPE_AGGREGATE, 0);
	if (rc < 0)
		return rc;
	else if (rc > 0)
		return 0;	/* extent merged in tree */

	memset(&dummy, 0, sizeof(dummy));
	D_INIT_LIST_HEAD(&dummy.ve_link);
	dummy.ve_ext = *vfe;

	/* Add to in-memory aggregate free extent tree */
	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &dummy.ve_ext.vfe_blk_off, sizeof(dummy.ve_ext.vfe_blk_off));
	d_iov_set(&val, &dummy, sizeof(dummy));
	d_iov_set(&val_out, NULL, 0);

	rc = dbtree_upsert(btr_hdl, BTR_PROBE_BYPASS, DAOS_INTENT_UPDATE, &key, &val, &val_out);
	if (rc) {
		D_ERROR("Insert aging extent failed. "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	D_ASSERT(val_out.iov_buf != NULL);
	entry = (struct vea_entry *)val_out.iov_buf;
	D_INIT_LIST_HEAD(&entry->ve_link);

	dock_aging_entry(vsi, entry);
	return 0;
}

#define FLUSH_INTVL		5	/* seconds */
#define EXPIRE_INTVL		10	/* seconds */

static int
flush_internal(struct vea_space_info *vsi, bool force, uint32_t cur_time, d_sg_list_t *unmap_sgl)
{
	struct vea_entry	*entry, *tmp;
	struct vea_free_extent	 vfe;
	d_iov_t			*unmap_iov;
	int			 i, rc = 0;

	D_ASSERT(umem_tx_none(vsi->vsi_umem));
	D_ASSERT(unmap_sgl->sg_nr_out == 0);

	d_list_for_each_entry_safe(entry, tmp, &vsi->vsi_agg_lru, ve_link) {
		d_iov_t	key;

		vfe = entry->ve_ext;
		if (!force && cur_time < (vfe.vfe_age + EXPIRE_INTVL))
			break;

		/* Remove entry from aggregate LRU list */
		d_list_del_init(&entry->ve_link);
		dec_stats(vsi, STAT_FRAGS_AGING, 1);

		/* Remove entry from aggregate tree, entry will be freed on deletion */
		d_iov_set(&key, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
		D_ASSERT(daos_handle_is_valid(vsi->vsi_agg_btr));
		rc = dbtree_delete(vsi->vsi_agg_btr, BTR_PROBE_EQ, &key, NULL);
		if (rc) {
			D_ERROR("Remove ["DF_U64", %u] from aggregated tree error: "DF_RC"\n",
				vfe.vfe_blk_off, vfe.vfe_blk_cnt, DP_RC(rc));
			break;
		}

		/* Unmap callback may yield, so we can't call it directly in this tight loop */
		unmap_sgl->sg_nr_out++;
		unmap_iov = &unmap_sgl->sg_iovs[unmap_sgl->sg_nr_out - 1];
		unmap_iov->iov_buf = (void *)vfe.vfe_blk_off;
		unmap_iov->iov_len = vfe.vfe_blk_cnt;

		if (unmap_sgl->sg_nr_out == MAX_FLUSH_FRAGS)
			break;
	}

	vsi->vsi_flush_time = cur_time;

	/*
	 * According to NVMe spec, unmap isn't an expensive non-queue command
	 * anymore, so we should just unmap as soon as the extent is freed.
	 *
	 * Since unmap could yield, it must be called before the compound_free(),
	 * otherwise, the extent could be visible for allocation before unmap done.
	 */
	if (vsi->vsi_unmap_ctxt.vnc_unmap != NULL && unmap_sgl->sg_nr_out > 0) {
		rc = vsi->vsi_unmap_ctxt.vnc_unmap(unmap_sgl, vsi->vsi_md->vsd_blk_sz,
						   vsi->vsi_unmap_ctxt.vnc_data);
		if (rc)
			D_ERROR("Unmap %u frags failed: "DF_RC"\n",
				unmap_sgl->sg_nr_out, DP_RC(rc));
	}

	for (i = 0; i < unmap_sgl->sg_nr_out; i++) {
		unmap_iov = &unmap_sgl->sg_iovs[i];

		vfe.vfe_blk_off = (uint64_t)unmap_iov->iov_buf;
		vfe.vfe_blk_cnt = unmap_iov->iov_len;
		vfe.vfe_age = cur_time;

		rc = compound_free(vsi, &vfe, 0);
		if (rc)
			D_ERROR("Compound free ["DF_U64", %u] error: "DF_RC"\n",
				vfe.vfe_blk_off, vfe.vfe_blk_cnt, DP_RC(rc));
	}

	return rc;
}

static inline bool
need_aging_flush(struct vea_space_info *vsi, uint32_t cur_time, bool force)
{
	if (d_list_empty(&vsi->vsi_agg_lru))
		return false;

	/* External flush controls the flush rate externally */
	if (vsi->vsi_unmap_ctxt.vnc_ext_flush)
		return true;

	if (!force && cur_time < (vsi->vsi_flush_time + FLUSH_INTVL))
		return false;

	return true;
}

int
trigger_aging_flush(struct vea_space_info *vsi, bool force, uint32_t nr_flush,
		    uint32_t *nr_flushed)
{
	d_sg_list_t	 unmap_sgl;
	uint32_t	 cur_time, tot_flushed = 0;
	int		 rc;

	D_ASSERT(nr_flush > 0);
	if (!umem_tx_none(vsi->vsi_umem)) {
		rc = -DER_INVAL;
		goto out;
	}

	cur_time = get_current_age();
	if (!need_aging_flush(vsi, cur_time, force)) {
		rc = 0;
		goto out;
	}

	rc = d_sgl_init(&unmap_sgl, MAX_FLUSH_FRAGS);
	if (rc)
		goto out;

	while (tot_flushed < nr_flush) {
		rc = flush_internal(vsi, force, cur_time, &unmap_sgl);

		tot_flushed += unmap_sgl.sg_nr_out;
		if (rc || unmap_sgl.sg_nr_out < MAX_FLUSH_FRAGS)
			break;

		unmap_sgl.sg_nr_out = 0;
	}

	d_sgl_fini(&unmap_sgl, false);
out:
	if (nr_flushed != NULL)
		*nr_flushed = tot_flushed;

	return rc;
}

static void
flush_end_cb(void *data, bool noop)
{
	struct vea_space_info	*vsi = data;

	if (!noop)
		trigger_aging_flush(vsi, false, MAX_FLUSH_FRAGS * 20, NULL);

	vsi->vsi_flush_scheduled = false;
}

int
schedule_aging_flush(struct vea_space_info *vsi)
{
	int	rc;

	D_ASSERT(vsi != NULL);

	if (vsi->vsi_unmap_ctxt.vnc_ext_flush)
		return 0;

	/* Check flush condition in advance to avoid unnecessary umem_tx_add_callback() */
	if (!need_aging_flush(vsi, get_current_age(), false))
		return 0;

	/* Schedule one transaction end callback flush is enough */
	if (vsi->vsi_flush_scheduled)
		return 0;

	/*
	 * Perform the flush in transaction end callback, since the flush operation
	 * could yield on blob unmap.
	 */
	rc = umem_tx_add_callback(vsi->vsi_umem, vsi->vsi_txd, UMEM_STAGE_NONE,
				  flush_end_cb, vsi);
	if (rc) {
		D_ERROR("Add transaction end callback error "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	vsi->vsi_flush_scheduled = true;

	return 0;
}
