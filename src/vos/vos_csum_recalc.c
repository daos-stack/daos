/**
 * (C) Copyright 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)
#define C_TRACE(...)	D_DEBUG(DB_CSUM, __VA_ARGS__)

#include <daos/common.h>
#include <daos/checksum.h>
#include <daos_srv/evtree.h>
#include "vos_internal.h"

/*
 * Checksum recalculation code. Called from vos_aggregate.c.
 *
 * Recalculation is driven by an array of csum_recalc structs,
 * one per input segment. These segments are coalecsed into a single
 * output segment by the overall aggregation process.
 *
 * The calculated checksums, are then compared to the checksums
 * associated with the input segments. These input checksums were
 * returned by the evtree iterator that generates the input extents.
 * Input segments that overlap a merge window are an exception to this.
 * Here, the checksums used for verification are from the overlapping
 * output extent (who's checksums were verified in the previous window).
 *
 * If an input segment fails verification, an checksum error code is returned
 * to the caller (in vos_aggregate.c), and the output checksum data is left
 * at zero values.
 *
 * Following input verification, generation of the checksum(s) for the
 * output segment is performed.
 *
 * All checksum calculation is performed using the DAOS checksum library.
 * The calculations are offloaded to a helper Xstream, when one is available.
 *
 */

/* Determine checksum parameters for verification of an input segemnt. */
static unsigned int
calc_csum_params(struct dcs_csum_info *csum_info, struct csum_recalc *recalc,
		 unsigned int prefix_len, unsigned int suffix_len,
		 unsigned int rec_size)
{

	unsigned int cs_cnt, low_idx = recalc->cr_log_ext.ex_lo -
							prefix_len / rec_size;
	unsigned int high_idx = recalc->cr_log_ext.ex_hi +
							suffix_len / rec_size;

	assert(prefix_len % rec_size == 0);
	cs_cnt = csum_chunk_count(recalc->cr_phy_csum->cs_chunksize,
				    low_idx, high_idx, rec_size);
	csum_info->cs_nr = cs_cnt;
	D_ASSERT(cs_cnt * csum_info->cs_len  <= csum_info->cs_buf_len);
	return low_idx;
}

/* Verifies checksums for an input segment. */
static bool
csum_agg_verify(struct csum_recalc *recalc, struct dcs_csum_info *new_csum,
		unsigned int rec_size, unsigned int prefix_len)
{
	unsigned int	j = 0;
	bool		match;

	if (recalc->cr_phy_off && DAOS_FAIL_CHECK(DAOS_VOS_AGG_MW_THRESH)) {
		D_INFO("CHECKSUM merge window failure injection.\n");
		return false;
	}

	/* The index j is used to determine the start offset within
	 * the prior checksum array (associated with the input physical
	 * extent). If the array sizes for input and output segments are
	 * the same, then the comparison begins at the beginning of the input
	 * checksum array. Otherwise, the start index is set by incrementing
	 * j on each checksum boundary until the offset associated with j
	 * matches the offset of the (csum-extended) output segment.
	 */
	if (new_csum->cs_nr != recalc->cr_phy_csum->cs_nr) {
		unsigned int chunksize = new_csum->cs_chunksize;
		unsigned int orig_offset =
			(recalc->cr_phy_ext->ex_lo +
			 recalc->cr_phy_off)  * rec_size;
		unsigned int out_offset = recalc->cr_log_ext.ex_lo * rec_size -
								prefix_len;

		D_ASSERT(new_csum->cs_nr <
				 recalc->cr_phy_csum->cs_nr);
		D_ASSERT(orig_offset <= out_offset);
		if (orig_offset != out_offset) {
			unsigned int add_start = chunksize -
							orig_offset % chunksize;
			unsigned int offset = orig_offset + add_start;

			if (add_start)
				j++;
			while (offset < out_offset) {
				offset += chunksize;
				j++;
			}
			D_ASSERT(offset == out_offset);
		}
	}

	/* Comparison is for the full length of the output csum array,
	 * starting at the correct offset of the checksum array for the input
	 * segment.
	 */
	match = memcmp(new_csum->cs_csum,
		       &recalc->cr_phy_csum->cs_csum[j * new_csum->cs_len],
		       new_csum->cs_nr * new_csum->cs_len) == 0;
	if (!match) {
		D_ERROR("calc ("DF_CI") != phy ("DF_CI")\n",
			DP_CI(*new_csum), DP_CI(*recalc->cr_phy_csum));
	}
	return match;
}

/*
 * Driver for the checksum verification of input segments, and calculation
 * of checksum array for the output segment.
 */
int
vos_csum_recalc_fn(void *recalc_args)
{
	d_sg_list_t		 sgl, sgl_dst;
	struct csum_recalc_args *args = recalc_args;
	struct bio_sglist	*bsgl = args->cra_bsgl;
	struct evt_entry_in	*ent_in = args->cra_ent_in;
	struct csum_recalc	*recalcs = args->cra_recalcs;
	struct daos_csummer	*csummer;
	struct dcs_csum_info	 csum_info = args->cra_ent_in->ei_csum;
	struct bio_iov		*biov;
	int			 i, rc = 0;

	D_ASSERT(args->cra_seg_cnt > 0);
	rc = d_sgl_init(&sgl, 1);
	if (rc) {
		args->cra_rc = rc;
		return rc;
	}

	rc = d_sgl_init(&sgl_dst, args->cra_seg_cnt);
	if (rc) {
		d_sgl_fini(&sgl, false);
		args->cra_rc = rc;
		return rc;
	}

	daos_csummer_init_with_type(&csummer, csum_info.cs_type,
				    csum_info.cs_chunksize, 0);
	for (i = 0; i < args->cra_seg_cnt; i++) {
		bool		is_valid = false;
		unsigned int	this_rec_nr, this_rec_idx;

		biov = &bsgl->bs_iovs[i];
		/* Number of records in this input segment */
		this_rec_nr = bio_iov2raw_len(biov) / ent_in->ei_inob;

		D_ASSERT(recalcs[i].cr_log_ext.ex_hi -
			 recalcs[i].cr_log_ext.ex_lo + 1 ==
			 bio_iov2req_len(biov) / ent_in->ei_inob);

		D_ASSERT(bio_iov2raw_buf(biov) != NULL);
		D_ASSERT(bio_iov2raw_len(biov) > 0);

		d_iov_set(&sgl.sg_iovs[0], bio_iov2raw_buf(biov), bio_iov2raw_len(biov));
		d_iov_set(&sgl_dst.sg_iovs[i], bio_iov2req_buf(biov), bio_iov2req_len(biov));

		/* Determines number of checksum entries, and start index, for
		 * calculating verification checksum,
		 */
		this_rec_idx = calc_csum_params(&csum_info, &recalcs[i], biov->bi_prefix_len,
						biov->bi_suffix_len, ent_in->ei_inob);

		/* Ensure buffer is zero-ed. */
		memset(csum_info.cs_csum, 0, csum_info.cs_buf_len);

		/* Calculates the checksums for the input segment. */
		rc = daos_csummer_calc_one(csummer, &sgl, &csum_info,
					   ent_in->ei_inob, this_rec_nr,
					   this_rec_idx);
		if (rc)
			goto out;

		/* Verifies that calculated checksums match prior (input)
		 * checksums, for the appropriate range.
		 */
		is_valid = csum_agg_verify(&recalcs[i], &csum_info,
					   ent_in->ei_inob, biov->bi_prefix_len);
		if (!is_valid) {
			rc = -DER_CSUM;
			goto out;
		}
	}

	/* Re-set checksum buffer to zero values. (Input and output
	 * checksum infos share a buffer range.)
	 */
	memset(ent_in->ei_csum.cs_csum, 0, ent_in->ei_csum.cs_buf_len);

	/* Calculate checksum(s) for output segment. */
	rc = daos_csummer_calc_one(csummer, &sgl_dst, &ent_in->ei_csum,
				   ent_in->ei_inob,
				   evt_extent_width(&ent_in->ei_rect.rc_ex),
				   ent_in->ei_rect.rc_ex.ex_lo);
out:
	daos_csummer_destroy(&csummer);
	d_sgl_fini(&sgl, false);
	d_sgl_fini(&sgl_dst, false);
	args->cra_rc = rc;
	return rc;
}
