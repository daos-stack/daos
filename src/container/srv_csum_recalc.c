/**
 * (C) Copyright 2020 Intel Corporation.
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

#define D_LOGFAC	DD_FAC(csum)
#define C_TRACE(...)	D_DEBUG(DB_CSUM, __VA_ARGS__)

#include <daos/common.h>
#include <daos/checksum.h>
#include <daos_srv/daos_server.h>
#include <daos_srv/container.h>
#include <daos_srv/evtree.h>

/*
 * Checksum recalculation code. Called from vos_aggregate.c.
 *
 * Recalculation is driven by an array of csum_recalc structs,
 * one per input segment. These segments are coalecsed into a single
 * output segment by the overall aggregation process.
 *
 * The input data is held in a buffer that is specified by a bio sg_list.
 * The data for the output segment is place within the initial range of
 * the buffer. Following this range, the additional data required for
 * checksum data is stored. These additional segments, either prefix
 * or suffix ranges of each input physical extent, are appended in order
 * at the end of the buffer, with corresponding * entries in the bgsl's
 * biov array.
 *
 * A temporary sg_list is constructed for each input segment, with an
 * optional prefix entry, the outputable range, and an optional suffix
 * making up the data used to calculate the checksum used to verify
 * the data for each input segment.
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

/* Construct sgl to send to csummer for verify of an input segment. */
static unsigned int
csum_agg_set_sgl(d_sg_list_t *sgl, struct bio_sglist *bsgl,
		 struct csum_recalc *recalcs, uint8_t *buf,
		 unsigned int buf_len, int add_start, daos_size_t seg_size,
		 unsigned int idx, unsigned int add_offset,
		 unsigned int *buf_idx, unsigned int *add_idx)
{
	unsigned int sgl_idx = 0;

	sgl->sg_nr = 1;
	if (recalcs[idx].cr_prefix_len) {
		sgl->sg_iovs[sgl_idx].iov_buf = &buf[*add_idx+seg_size];
		D_ASSERT(recalcs[idx].cr_prefix_len ==
			 bsgl->bs_iovs[add_start + add_offset].bi_data_len);
		sgl->sg_iovs[sgl_idx].iov_buf_len =
			bsgl->bs_iovs[add_start + add_offset].bi_data_len;
		sgl->sg_iovs[sgl_idx++].iov_len =
			bsgl->bs_iovs[add_start + add_offset].bi_data_len;
		*add_idx += bsgl->bs_iovs[add_start + add_offset].bi_data_len;
		add_offset++;
		sgl->sg_nr++;
	}

	sgl->sg_iovs[sgl_idx].iov_buf = &buf[*buf_idx];
	sgl->sg_iovs[sgl_idx].iov_buf_len = bsgl->bs_iovs[idx].bi_data_len;
	sgl->sg_iovs[sgl_idx++].iov_len = bsgl->bs_iovs[idx].bi_data_len;
	*buf_idx += bsgl->bs_iovs[idx].bi_data_len;

	if (recalcs[idx].cr_suffix_len) {
		sgl->sg_iovs[sgl_idx].iov_buf = &buf[*add_idx + seg_size];
		D_ASSERT(recalcs[idx].cr_suffix_len ==
			 bsgl->bs_iovs[add_start + add_offset].bi_data_len);
		sgl->sg_iovs[sgl_idx].iov_buf_len =
			bsgl->bs_iovs[add_start + add_offset].bi_data_len;
		sgl->sg_iovs[sgl_idx].iov_len =
			bsgl->bs_iovs[add_start + add_offset].bi_data_len;
		*add_idx += bsgl->bs_iovs[add_start + add_offset].bi_data_len;
		add_offset++;
		sgl->sg_nr++;
	}
	return add_offset;
}

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
	return !memcmp(new_csum->cs_csum,
		       &recalc->cr_phy_csum->cs_csum[j * new_csum->cs_len],
		       new_csum->cs_nr * new_csum->cs_len);
}

/* Driver for the checksum verification of input segments, and calculation
 * of checksum array for the output segment. This function is called directly
 * from the VOS unit test, but is invoked in a ULT (running in a helper xstream
 * when available) for standard aggregation running within the DAOS server.
 */
void
ds_csum_agg_recalc(void *recalc_args)
{
	d_sg_list_t		 sgl;
	struct csum_recalc_args *args = recalc_args;
	struct bio_sglist	*bsgl = args->cra_bsgl;
	struct evt_entry_in	*ent_in = args->cra_ent_in;
	struct csum_recalc	*recalcs = args->cra_recalcs;
	struct daos_csummer	*csummer;
	struct dcs_csum_info	 csum_info = args->cra_ent_in->ei_csum;
	unsigned int		 buf_idx = 0;
	unsigned int		 add_idx = 0;
	unsigned int		 i,  add_offset = 0;
	int			 rc = 0;

	/* need at most prefix + buf + suffix in sgl */
	rc = daos_sgl_init(&sgl, 3);
	if (rc) {
		args->cra_rc = rc;
		return;
	}
	daos_csummer_init_with_type(&csummer, csum_info.cs_type,
				    csum_info.cs_chunksize, 0);
	for (i = 0; i < args->cra_seg_cnt; i++) {
		bool		is_valid = false;
		unsigned int	this_buf_nr, this_buf_idx;


		/* Number of records in this input segment, include added
		 * segments.
		 */
		this_buf_nr = (bsgl->bs_iovs[i].bi_data_len +
			       recalcs[i].cr_prefix_len +
			       recalcs[i].cr_suffix_len) / ent_in->ei_inob;
		/* Sets up the SGL for the (verification) checksum calculation.
		 * Returns the offset of the next add-on (prefix/suffix)
		 * segment.
		 */
		add_offset = csum_agg_set_sgl(&sgl, bsgl, recalcs,
					      args->cra_buf, args->cra_buf_len,
					      args->cra_seg_cnt,
					      args->cra_seg_size, i, add_offset,
					      &buf_idx, &add_idx);
		D_ASSERT(recalcs[i].cr_log_ext.ex_hi -
			 recalcs[i].cr_log_ext.ex_lo + 1 ==
			 bsgl->bs_iovs[i].bi_data_len / ent_in->ei_inob);

		/* Determines number of checksum entries, and start index, for
		 * calculating verification checksum,
		 */
		this_buf_idx = calc_csum_params(&csum_info, &recalcs[i],
						recalcs[i].cr_prefix_len,
						recalcs[i].cr_suffix_len,
						ent_in->ei_inob);

		/* Ensure buffer is zero-ed. */
		memset(csum_info.cs_csum, 0, csum_info.cs_buf_len);

		/* Calculates the checksums for the input segment. */
		rc = daos_csummer_calc_one(csummer, &sgl, &csum_info,
					   ent_in->ei_inob, this_buf_nr,
					   this_buf_idx);
		if (rc)
			goto out;

		/* Verifies that calculated checksums match prior (input)
		 * checksums, for the appropriate range.
		 */
		is_valid = csum_agg_verify(&recalcs[i], &csum_info,
					   ent_in->ei_inob,
					   recalcs[i].cr_prefix_len);
		if (!is_valid) {
			rc = -DER_CSUM;
			goto out;
		}
	}

	/* Re-set checksum buffer to zero values. (Input and output
	 * checksum infos share a buffer range.)
	 */
	memset(ent_in->ei_csum.cs_csum, 0, ent_in->ei_csum.cs_buf_len);
	args->cra_sgl->sg_iovs[0].iov_len = args->cra_seg_size;

	/* Calculate checksum(s) for output segment. */
	rc = daos_csummer_calc_one(csummer, args->cra_sgl, &ent_in->ei_csum,
				   ent_in->ei_inob,
				   evt_extent_width(&ent_in->ei_rect.rc_ex),
				   ent_in->ei_rect.rc_ex.ex_lo);
out:
	/* Eventual set okay, even with no offload (unit test). */
	ABT_eventual_set(args->csum_eventual, NULL, 0);
	daos_csummer_destroy(&csummer);
	D_FREE(sgl.sg_iovs);
	args->cra_rc = rc;
}

#ifndef VOS_UNIT_TEST
/* Entry point for offload invocation. */
void
ds_csum_recalc(void *args)
{
	struct csum_recalc_args	*cs_args = (struct csum_recalc_args *) args;
	struct dss_module_info  *info;

	C_TRACE("Checksum Aggregation\n");
	ABT_eventual_create(0, &cs_args->csum_eventual);
	dss_ult_create(ds_csum_agg_recalc, args,
		       DSS_ULT_CHECKSUM, DSS_TGT_SELF, 0, NULL);
	ABT_eventual_wait(cs_args->csum_eventual, NULL);
	if (cs_args->cra_rc == -DER_CSUM) {
		info = dss_get_module_info();

		cs_args->cra_bio_ctxt = info->dmi_nvme_ctxt;
		cs_args->cra_tgt_id = info->dmi_tgt_id;
	}
	ABT_eventual_free(&cs_args->csum_eventual);
}
#endif
