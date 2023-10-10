/**
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
#include <gurt/telemetry_producer.h>
#include "vea_internal.h"

int
verify_free_entry(uint64_t *off, struct vea_free_extent *vfe)
{
	D_ASSERT(vfe != NULL);
	if (off != NULL && *off != vfe->vfe_blk_off) {
		D_CRIT("corrupted free entry, off: "DF_U64" != "DF_U64"\n",
		       *off, vfe->vfe_blk_off);
		return -DER_INVAL;
	}

	if (vfe->vfe_blk_off == VEA_HINT_OFF_INVAL) {
		D_CRIT("corrupted free entry, off == VEA_HINT_OFF_INVAL(%d)\n",
			VEA_HINT_OFF_INVAL);
		return -DER_INVAL;
	}

	if (vfe->vfe_blk_cnt == 0) {
		D_CRIT("corrupted free entry, cnt:, %u\n",
		       vfe->vfe_blk_cnt);
		return -DER_INVAL;
	}
	return 0;
}

int
verify_bitmap_entry(struct vea_free_bitmap *vfb)
{
	D_ASSERT(vfb != NULL);
	if (vfb->vfb_blk_off == VEA_HINT_OFF_INVAL) {
		D_CRIT("corrupted bitmap entry, off == VEA_HINT_OFF_INVAL(%d)\n",
			VEA_HINT_OFF_INVAL);
		return -DER_INVAL;
	}

	if (vfb->vfb_class < VEA_MIN_BITMAP_CLASS || vfb->vfb_class > VEA_MAX_BITMAP_CLASS) {
		D_CRIT("corrupted bitmap entry, class: %u is out of [%u, %u]\n",
			vfb->vfb_class, VEA_MIN_BITMAP_CLASS, VEA_MAX_BITMAP_CLASS);
		return -DER_INVAL;
	}

	if (vfb->vfb_blk_cnt < VEA_BITMAP_MIN_CHUNK_BLKS ||
	    vfb->vfb_blk_cnt > VEA_BITMAP_MAX_CHUNK_BLKS) {
		D_CRIT("corrupted bitmap entry, chunk size: %u is out of [%u, %u]\n",
			vfb->vfb_blk_cnt, VEA_BITMAP_MIN_CHUNK_BLKS, VEA_BITMAP_MAX_CHUNK_BLKS);
		return -DER_INVAL;
	}

	if (vfb->vfb_blk_cnt % VEA_BITMAP_MIN_CHUNK_BLKS) {
		D_CRIT("coruppted bitmap entry, chunk size: %u should be times of %u\n",
			vfb->vfb_blk_cnt, VEA_BITMAP_MIN_CHUNK_BLKS);
		return -DER_INVAL;
	}

	if (vfb->vfb_bitmap_sz * 64 * vfb->vfb_class < vfb->vfb_blk_cnt) {
		D_CRIT("corrupted bitmap entry, bitmap size: %u could not cover chunk size: %u\n",
			vfb->vfb_bitmap_sz, vfb->vfb_blk_cnt);
		return -DER_INVAL;
	}

	return 0;
}

/**
 * Check if current extent is adjacent with next one.
 * returns	1 - Adjacent
 *		0 - Not adjacent
 *		-DER_INVAL - Overlapping or @cur is behind of @next
 */
int
ext_adjacent(struct vea_free_extent *cur, struct vea_free_extent *next)
{
	uint64_t cur_end = cur->vfe_blk_off + cur->vfe_blk_cnt;

	if (cur_end == next->vfe_blk_off)
		return 1;
	else if (cur_end < next->vfe_blk_off)
		return 0;

	/* Overlapped extents! */
	D_CRIT("corrupted free extents ["DF_U64", %u], ["DF_U64", %u]\n",
	       cur->vfe_blk_off, cur->vfe_blk_cnt,
	       next->vfe_blk_off, next->vfe_blk_cnt);

	return -DER_INVAL;
}

int
verify_resrvd_ext(struct vea_resrvd_ext *resrvd)
{
	if (resrvd->vre_blk_off == VEA_HINT_OFF_INVAL) {
		D_CRIT("invalid blk_off "DF_U64"\n", resrvd->vre_blk_off);
		return -DER_INVAL;
	} else if (resrvd->vre_blk_cnt == 0) {
		D_CRIT("invalid blk_cnt %u\n", resrvd->vre_blk_cnt);
		return -DER_INVAL;
	}

	return 0;
}

static int
vea_dump_bitmap(struct vea_space_info *vsi, bool transient)
{
	struct vea_free_bitmap	*bitmap;
	struct vea_bitmap_entry	*entry;
	daos_handle_t		 ih, btr_hdl;
	d_iov_t			 key, val;
	uint64_t		*off;
	int			 rc, print_cnt = 0, opc = BTR_PROBE_FIRST;

	if (transient)
		btr_hdl = vsi->vsi_bitmap_btr;
	else
		btr_hdl = vsi->vsi_md_bitmap_btr;

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	rc = dbtree_iter_prepare(btr_hdl, BTR_ITER_EMBEDDED, &ih);
	if (rc)
		return rc;

	rc = dbtree_iter_probe(ih, opc, DAOS_INTENT_DEFAULT, NULL, NULL);

	D_PRINT("Bitmaps:");
	while (rc == 0) {
		d_iov_set(&key, NULL, 0);
		d_iov_set(&val, NULL, 0);
		rc = dbtree_iter_fetch(ih, &key, &val, NULL);
		if (rc != 0)
			break;

		off = (uint64_t *)key.iov_buf;
		if (*off == VEA_BITMAP_CHUNK_HINT_KEY)
			goto next;

		if (transient) {
			entry = (struct vea_bitmap_entry *)val.iov_buf;
			bitmap = &entry->vbe_bitmap;
		} else {
			bitmap = (struct vea_free_bitmap *)val.iov_buf;

		}
		rc = verify_bitmap_entry(bitmap);
		if (rc != 0) {
			D_ERROR("dump failed???\n");
			break;
		}

		D_PRINT("["DF_U64", %u]", bitmap->vfb_blk_off, bitmap->vfb_blk_cnt);
		print_cnt++;
		if (print_cnt % 10 == 0)
			D_PRINT("\n");
		else
			D_PRINT(" ");
next:
		rc = dbtree_iter_next(ih);
	}

	D_PRINT("\n");
	dbtree_iter_finish(ih);

	return rc = -DER_NONEXIST ? 0 : rc;


}

static int
vea_dump_extent(struct vea_space_info *vsi, bool transient)
{
	struct vea_free_extent	*ext;
	struct vea_extent_entry	*entry;
	daos_handle_t		 ih, btr_hdl;
	d_iov_t			 key, val;
	uint64_t		*off;
	int			 rc, print_cnt = 0, opc = BTR_PROBE_FIRST;

	if (transient)
		btr_hdl = vsi->vsi_free_btr;
	else
		btr_hdl = vsi->vsi_md_free_btr;
	D_ASSERT(daos_handle_is_valid(btr_hdl));
	rc = dbtree_iter_prepare(btr_hdl, BTR_ITER_EMBEDDED, &ih);
	if (rc)
		return rc;

	rc = dbtree_iter_probe(ih, opc, DAOS_INTENT_DEFAULT, NULL, NULL);

	D_PRINT("Free extents:");
	while (rc == 0) {
		d_iov_set(&key, NULL, 0);
		d_iov_set(&val, NULL, 0);
		rc = dbtree_iter_fetch(ih, &key, &val, NULL);
		if (rc != 0)
			break;

		off = (uint64_t *)key.iov_buf;
		if (transient) {
			entry = (struct vea_extent_entry *)val.iov_buf;
			ext = &entry->vee_ext;
		} else {
			ext = (struct vea_free_extent *)val.iov_buf;
		}
		rc = verify_free_entry(off, ext);
		if (rc != 0)
			break;

		D_PRINT("["DF_U64", %u]", ext->vfe_blk_off, ext->vfe_blk_cnt);
		print_cnt++;
		if (print_cnt % 10 == 0)
			D_PRINT("\n");
		else
			D_PRINT(" ");

		rc = dbtree_iter_next(ih);
	}

	D_PRINT("\n");
	dbtree_iter_finish(ih);

	return rc = -DER_NONEXIST ? 0 : rc;
}

int
vea_dump(struct vea_space_info *vsi, bool transient)
{
	int rc;

	rc = vea_dump_bitmap(vsi, transient);
	if (rc)
		return rc;

	return vea_dump_extent(vsi, transient);
}

/**
 * Check if two extents are overlapping.
 * returns	0 - Non-overlapping
 *		1 - @ext1 contains @ext2
 *		-DER_INVAL - Overlapping
 */
static int
ext_overlapping(struct vea_free_extent *ext1, struct vea_free_extent *ext2)
{
	if ((ext1->vfe_blk_off + ext1->vfe_blk_cnt) <= ext2->vfe_blk_off ||
	    (ext2->vfe_blk_off + ext2->vfe_blk_cnt) <= ext1->vfe_blk_off)
		return 0;

	if (ext1->vfe_blk_off <= ext2->vfe_blk_off &&
	    ext1->vfe_blk_cnt >= ext2->vfe_blk_cnt)
		return 1;

	return -DER_INVAL;
}

static int
verify_alloc_bitmap(struct vea_space_info *vsi, bool transient, uint64_t off,
		    uint32_t cnt)
{
	daos_handle_t btr_hdl;
	d_iov_t key, key_out, val;
	int rc, opc = BTR_PROBE_LE;
	struct vea_free_bitmap *vfb;

	if (transient)
		btr_hdl = vsi->vsi_bitmap_btr;
	else
		btr_hdl = vsi->vsi_md_bitmap_btr;

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &off, sizeof(off));

	d_iov_set(&key_out, NULL, 0);
	d_iov_set(&val, NULL, 0);
	rc = dbtree_fetch(btr_hdl, opc, DAOS_INTENT_DEFAULT, &key, &key_out,
			  &val);
	/* bitmap not allocated */
	if (rc == -DER_NONEXIST)
		return 1;

	if (rc)
		return rc;

	if (transient) {
		struct vea_bitmap_entry *entry;

		entry = (struct vea_bitmap_entry *)val.iov_buf;
		vfb = &entry->vbe_bitmap;
	} else {
		vfb = (struct vea_free_bitmap *)val.iov_buf;
	}

	rc = verify_bitmap_entry(vfb);
	if (rc != 0) {
		D_ERROR("verify bitmap alloc failed\n");
		return rc;
	}

	/* not in the bitmap range */
	if (off + cnt <= vfb->vfb_blk_off || off >= vfb->vfb_blk_off + vfb->vfb_blk_cnt)
		return 1;

	if (isset_range((uint8_t *)vfb->vfb_bitmaps,
			(off - vfb->vfb_blk_off) / vfb->vfb_class,
			(off - vfb->vfb_blk_off + cnt - 1) / vfb->vfb_class))
		return 0;

	return 1;
}


static int
verify_alloc_extent(struct vea_space_info *vsi, bool transient, uint64_t off, uint32_t cnt)
{
	struct vea_free_extent	 vfe, *ext;
	daos_handle_t		 btr_hdl;
	d_iov_t			 key, key_out, val;
	uint64_t		*key_off;
	int			 rc, opc = BTR_PROBE_LE;

	/* Sanity check on input parameters */
	vfe.vfe_blk_off = off;
	vfe.vfe_blk_cnt = cnt;
	rc = verify_free_entry(NULL, &vfe);
	if (rc)
		return rc;

	if (transient)
		btr_hdl = vsi->vsi_free_btr;
	else
		btr_hdl = vsi->vsi_md_free_btr;

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	d_iov_set(&key, &vfe.vfe_blk_off, sizeof(vfe.vfe_blk_off));
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
		return 0;	/* Allocated */
	else if (rc)
		return rc;	/* Error */

	key_off = (uint64_t *)key_out.iov_buf;
	if (transient) {
		struct vea_extent_entry *entry;

		entry = (struct vea_extent_entry *)val.iov_buf;
		ext = &entry->vee_ext;
	} else {
		ext = (struct vea_free_extent *)val.iov_buf;
	}

	rc = verify_free_entry(key_off, ext);
	if (rc != 0)
		return rc;

	rc = ext_overlapping(ext, &vfe);
	if (rc)
		return rc;

	if (opc == BTR_PROBE_LE) {
		opc = BTR_PROBE_GE;
		goto repeat;
	}

	return rc;
}

/**
 * Verify if an extent is allocated in persistent or transient metadata.
 *
 * \param vsi       [IN]	In-memory compound index
 * \param transient [IN]	Persistent or transient
 * \param off       [IN]	Block offset of extent
 * \param cnt       [IN]	Block count of extent
 * \param is_bitmap [IN]	Bitmap or extent
 *
 * \return			0 - Allocated
 *				1 - Not allocated
 *				Negative value on error
 */
int
vea_verify_alloc(struct vea_space_info *vsi, bool transient,
		 uint64_t off, uint32_t cnt, bool is_bitmap)
{
	if (!is_bitmap)
		return verify_alloc_extent(vsi, transient, off, cnt);

	return verify_alloc_bitmap(vsi, transient, off, cnt);
}

void
vea_metrics_free(void *data)
{
	D_FREE(data);
}

static inline char *
rsrv_type2str(int rsrv_type)
{
	switch (rsrv_type) {
	case STAT_RESRV_HINT:
		return "hint";
	case STAT_RESRV_LARGE:
		return "large";
	case STAT_RESRV_SMALL:
		return "small";
	case STAT_RESRV_BITMAP:
		return "bitmap";
	default:
		return "unknown";
	}
}

static inline char *
frags_type2str(int frags_type)
{
	switch (frags_type) {
	case STAT_FRAGS_LARGE:
		return "large";
	case STAT_FRAGS_SMALL:
		return "small";
	case STAT_FRAGS_AGING:
		return "aging";
	case STAT_FRAGS_BITMAP:
		return "bitmap";
	default:
		return "unknown";
	}
}

#define VEA_TELEMETRY_DIR	"block_allocator"

void *
vea_metrics_alloc(const char *path, int tgt_id)
{
	struct vea_metrics	*metrics;
	char			 desc[40];
	int			 i, rc;

	D_ASSERT(tgt_id >= 0);

	D_ALLOC_PTR(metrics);
	if (metrics == NULL)
		return NULL;

	for (i = 0; i < STAT_RESRV_TYPE_MAX; i++) {
		snprintf(desc, sizeof(desc), "number of %s block allocs", rsrv_type2str(i));

		rc = d_tm_add_metric(&metrics->vm_rsrv[i], D_TM_COUNTER, desc, "allocs",
				     "%s/%s/alloc/%s/tgt_%u", path, VEA_TELEMETRY_DIR,
				     rsrv_type2str(i), tgt_id);
		if (rc)
			D_WARN("Failed to create 'alloc/%s' telemetry: "DF_RC"\n",
			       rsrv_type2str(i), DP_RC(rc));
	}

	for (i = 0; i < STAT_FRAGS_TYPE_MAX; i++) {
		int type = i + STAT_FRAGS_LARGE;

		snprintf(desc, sizeof(desc), "number of %s frags", frags_type2str(type));

		rc = d_tm_add_metric(&metrics->vm_frags[i], D_TM_GAUGE, desc, "frags",
				     "%s/%s/frags/%s/tgt_%u", path, VEA_TELEMETRY_DIR,
				     frags_type2str(type), tgt_id);
		if (rc)
			D_WARN("Failed to create 'frags/%s' telemetry: "DF_RC"\n",
			       frags_type2str(type), DP_RC(rc));
	}

	rc = d_tm_add_metric(&metrics->vm_free_blks, D_TM_GAUGE, "number of free blocks",
			     "blks", "%s/%s/free_blks/tgt_%u", path, VEA_TELEMETRY_DIR, tgt_id);
	if (rc)
		D_WARN("Failed to create free blks telemetry: "DF_RC"\n", DP_RC(rc));

	return metrics;
}

int
vea_metrics_count(void)
{
	return (sizeof(struct vea_metrics) / sizeof(struct d_tm_node_t *));
}

static void
update_stats(struct vea_space_info *vsi, unsigned int type, uint64_t nr, bool dec)
{
	struct vea_metrics	*metrics = vsi->vsi_metrics;
	int			 frag_idx;

	switch (type) {
	case STAT_RESRV_HINT:
	case STAT_RESRV_LARGE:
	case STAT_RESRV_SMALL:
	case STAT_RESRV_BITMAP:
		D_ASSERT(!dec && nr == 1);
		vsi->vsi_stat[type] += nr;
		if (metrics && metrics->vm_rsrv[type])
			d_tm_set_counter(metrics->vm_rsrv[type], vsi->vsi_stat[type]);
		break;
	case STAT_FRAGS_LARGE:
	case STAT_FRAGS_SMALL:
	case STAT_FRAGS_BITMAP:
	case STAT_FRAGS_AGING:
		D_ASSERT(nr == 1 && type >= STAT_FRAGS_LARGE);
		if (dec) {
			D_ASSERT(vsi->vsi_stat[type] > 0);
			vsi->vsi_stat[type] -= nr;
		} else {
			vsi->vsi_stat[type] += nr;
		}
		frag_idx = type - STAT_FRAGS_LARGE;
		if (metrics && metrics->vm_frags[frag_idx])
			d_tm_set_gauge(metrics->vm_frags[frag_idx], vsi->vsi_stat[type]);
		break;
	case STAT_FREE_EXTENT_BLKS:
	case STAT_FREE_BITMAP_BLKS:
		if (dec) {
			D_ASSERTF(vsi->vsi_stat[type] >= nr, "free:"DF_U64" < rsrvd:"DF_U64"\n",
				  vsi->vsi_stat[type], nr);
			vsi->vsi_stat[type] -= nr;
		} else {
			vsi->vsi_stat[type] += nr;
		}
		if (metrics && metrics->vm_free_blks)
			d_tm_set_gauge(metrics->vm_free_blks, vsi->vsi_stat[type]);
		break;
	default:
		D_ASSERTF(0, "Invalid stat type %u\n", type);
		break;
	}
}

void
dec_stats(struct vea_space_info *vsi, unsigned int type, uint64_t nr)
{
	return update_stats(vsi, type, nr, true);
}

void
inc_stats(struct vea_space_info *vsi, unsigned int type, uint64_t nr)
{
	return update_stats(vsi, type, nr, false);
}
