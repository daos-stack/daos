/**
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include <daos/dtx.h>
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
verify_vec_entry(uint64_t *off, struct vea_ext_vector *vec)
{
	int i;
	uint64_t prev_off = 0;

	D_ASSERT(vec != NULL);
	if (vec->vev_size == 0 || vec->vev_size > VEA_EXT_VECTOR_MAX) {
		D_CRIT("corrupted vector entry, sz: %u\n", vec->vev_size);
		return -DER_INVAL;
	}

	if (off != NULL && *off != vec->vev_blk_off[0]) {
		D_CRIT("corrupted vector entry, off: "DF_U64" != "DF_U64"\n",
		       *off, vec->vev_blk_off[0]);
		return -DER_INVAL;
	}

	for (i = 0; i < vec->vev_size; i++) {
		if (vec->vev_blk_off[i] <= prev_off) {
			D_CRIT("corrupted vector entry[%d],"
			       " "DF_U64" <= "DF_U64"\n",
			       i, vec->vev_blk_off[i], prev_off);
			return -DER_INVAL;
		}
		if (vec->vev_blk_cnt[i] == 0) {
			D_CRIT("corrupted vector entry[%d], %u\n",
			       i, vec->vev_blk_cnt[i]);
			return -DER_INVAL;
		}
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
	} else if (resrvd->vre_vector != NULL) {
		/* Vector allocation isn't supported yet. */
		D_CRIT("vector isn't NULL?\n");
		return -DER_NOSYS;
	}

	return 0;
}

int
vea_dump(struct vea_space_info *vsi, bool transient)
{
	struct vea_free_extent *ext;
	daos_handle_t ih, btr_hdl;
	d_iov_t key, val;
	uint64_t *off;
	int rc, print_cnt = 0, opc = BTR_PROBE_FIRST;

	if (transient)
		btr_hdl = vsi->vsi_free_btr;
	else
		btr_hdl = vsi->vsi_md_free_btr;

	D_ASSERT(daos_handle_is_valid(btr_hdl));
	rc = dbtree_iter_prepare(btr_hdl, BTR_ITER_EMBEDDED, &ih);
	if (rc)
		return rc;

	rc = dbtree_iter_probe(ih, opc, DAOS_INTENT_DEFAULT, NULL, NULL);

	while (rc == 0) {
		d_iov_set(&key, NULL, 0);
		d_iov_set(&val, NULL, 0);
		rc = dbtree_iter_fetch(ih, &key, &val, NULL);
		if (rc != 0)
			break;

		off = (uint64_t *)key.iov_buf;
		if (transient) {
			struct vea_entry *entry;

			entry = (struct vea_entry *)val.iov_buf;
			ext = &entry->ve_ext;
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

/**
 * Verify if an extent is allocated in persistent or transient metadata.
 *
 * \param vsi       [IN]	In-memory compound index
 * \param transient [IN]	Persistent or transient
 * \param off       [IN]	Block offset of extent
 * \param cnt       [IN]	Block count of extent
 *
 * \return			0 - Allocated
 *				1 - Not allocated
 *				Negative value on error
 */
int
vea_verify_alloc(struct vea_space_info *vsi, bool transient, uint64_t off,
		 uint32_t cnt)
{
	struct vea_free_extent vfe, *ext;
	daos_handle_t btr_hdl;
	d_iov_t key, key_out, val;
	uint64_t *key_off;
	int rc, opc = BTR_PROBE_LE;

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
		struct vea_entry *entry;

		entry = (struct vea_entry *)val.iov_buf;
		ext = &entry->ve_ext;
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
