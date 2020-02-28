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
/**
 *  Implementation of csum support for aggregation.
 */
#define D_LOGFAC        DD_FAC(vos)

#include <daos_srv/vos.h>
#include <daos/checksum.h>
#include "vos_internal.h"
#include "evt_priv.h"

struct csum_recalc_args {
	struct bio_sglist	*cra_bsgl;	/* read sgl */
	d_sg_list_t		*cra_sgl;	/* write sgl */
	struct evt_entry_in	*cra_ent_in;    /* coalesced entry */
	struct csum_recalc	*cra_recalcs;   /* recalc info */
	void			*cra_buf;	/* read buffer */
	daos_size_t		 cra_seg_size;  /* size of coalesced entry */
	unsigned int		 cra_seg_cnt;   /* # of read segments */
	unsigned int		 cra_buf_len;	/* length of read buffer */
	int			 cra_rc;	/* return code */
	ABT_eventual		 csum_eventual;
};

/* construct sgl to send to csummer for verify of read data */
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
	cs_cnt = csum_chunk_count(recalc->cr_phy_ent->pe_csum_info.cs_chunksize,
				    low_idx, high_idx, rec_size);
	csum_info->cs_nr = cs_cnt;
	D_ASSERT(cs_cnt * csum_info->cs_len  <= csum_info->cs_buf_len);
	return low_idx;
}

static bool
csum_agg_verify(struct csum_recalc *recalc, struct dcs_csum_info *new_csum,
		unsigned int rec_size, unsigned int prefix_len)
{
	unsigned int j = 0;
	bool match;

	if (new_csum->cs_nr != recalc->cr_phy_ent->pe_csum_info.cs_nr) {
		unsigned int chunksize = new_csum->cs_chunksize;
		unsigned int orig_offset =
			(recalc->cr_phy_ent->pe_rect.rc_ex.ex_lo +
			 recalc->cr_phy_ent->pe_off)  * rec_size;
		unsigned int out_offset = recalc->cr_log_ext.ex_lo * rec_size -
								prefix_len;

		D_ASSERT(new_csum->cs_nr <
				 recalc->cr_phy_ent->pe_csum_info.cs_nr);
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

	match = memcmp(new_csum->cs_csum,
		&recalc->cr_phy_ent->pe_csum_info.cs_csum[j * new_csum->cs_len],
			new_csum->cs_nr * new_csum->cs_len) == 0;
	if (!match)
		return false;
	return true;
}

static void
csum_agg_recalc(void *recalc_args)
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
	D_ALLOC_ARRAY(sgl.sg_iovs, 3);
	if (sgl.sg_iovs == NULL) {
		args->cra_rc = -DER_NOMEM;
		return;
	}
	daos_csummer_type_init(&csummer, csum_info.cs_type,
			       csum_info.cs_chunksize);
	for (i = 0; i < args->cra_seg_cnt; i++) {
		bool		is_valid = false;
		unsigned int	this_buf_nr, this_buf_idx;


		this_buf_nr = (bsgl->bs_iovs[i].bi_data_len +
			       recalcs[i].cr_prefix_len +
			       recalcs[i].cr_suffix_len) / ent_in->ei_inob;
		add_offset = csum_agg_set_sgl(&sgl, bsgl, recalcs,
					      args->cra_buf, args->cra_buf_len,
					      args->cra_seg_cnt,
					      args->cra_seg_size, i, add_offset,
					      &buf_idx, &add_idx);

		D_ASSERT(recalcs[i].cr_log_ext.ex_hi -
			 recalcs[i].cr_log_ext.ex_lo + 1 ==
			 bsgl->bs_iovs[i].bi_data_len / ent_in->ei_inob);

		this_buf_idx = calc_csum_params(&csum_info, &recalcs[i],
						recalcs[i].cr_prefix_len,
						recalcs[i].cr_suffix_len,
						ent_in->ei_inob);

		memset(csum_info.cs_csum, 0, csum_info.cs_buf_len);
		rc = daos_csummer_calc_one(csummer, &sgl, &csum_info,
					   ent_in->ei_inob, this_buf_nr,
					   this_buf_idx);
		if (rc)
			goto out;

		is_valid = csum_agg_verify(&recalcs[i], &csum_info,
					   ent_in->ei_inob,
					   recalcs[i].cr_prefix_len);
		if (!is_valid) {
			rc = -DER_CSUM;
			goto out;
		}
	}

	memset(ent_in->ei_csum.cs_csum, 0, ent_in->ei_csum.cs_buf_len);
	args->cra_sgl->sg_iovs[0].iov_len = args->cra_seg_size;
	rc = daos_csummer_calc_one(csummer, args->cra_sgl, &ent_in->ei_csum,
				   ent_in->ei_inob,
				   evt_extent_width(&ent_in->ei_rect.rc_ex),
				   ent_in->ei_rect.rc_ex.ex_lo);
out:
	/* set eventual, if xstteam offload */
	daos_csummer_destroy(&csummer);
	D_FREE(sgl.sg_iovs);
	args->cra_rc = rc;
	if (rc == -DER_CSUM) {
		for (i = 0; i < args->cra_seg_cnt; i++)
			recalcs[i].cr_phy_ent->pe_csum_info.cs_not_valid = true;
	}
}

/* Widen biov entry for read extents to range required to verify checksums. */
unsigned int
vos_csum_widen_biov(struct bio_iov *biov, struct vos_agg_phy_ent *phy_ent,
		    struct evt_extent *ext, uint32_t rsize, daos_off_t phy_lo,
		    uint32_t *wider)
{
	struct evt_entry	ent;
	struct evt_extent	aligned_extent = { 0 };
	unsigned int		added_segs = 0;

	ent.en_ext = phy_ent->pe_rect.rc_ex;
	if (phy_lo)
		ent.en_ext.ex_lo = phy_lo;
	ent.en_sel_ext = *ext;
	ent.en_csum = phy_ent->pe_csum_info;
	aligned_extent = evt_entry_align_to_csum_chunk(&ent, rsize);
	bio_iov_set_extra(biov,
			  (ent.en_sel_ext.ex_lo - aligned_extent.ex_lo) *
			  rsize,
			  (aligned_extent.ex_hi - ent.en_sel_ext.ex_hi) *
			  rsize);
	*wider = biov->bi_prefix_len + biov->bi_suffix_len;
	added_segs += biov->bi_prefix_len != 0;
	added_segs += biov->bi_suffix_len != 0;
	return added_segs;
}


/* Extends bio_sglist to include extension to csum boumdaries (added to the end
 * of the list).
 */
int
vos_csum_append_added_segs(struct bio_sglist *bsgl, unsigned int added_segs)
{
	void		*buffer;
	unsigned int	 i, add_idx = bsgl->bs_nr;

	D_REALLOC(buffer, bsgl->bs_iovs,
		  (bsgl->bs_nr + added_segs) * sizeof(struct bio_iov));
	if (buffer == NULL)
		return -DER_NOMEM;
	bsgl->bs_iovs = buffer;

	for (i = 0; i < bsgl->bs_nr; i++) {
		if (bsgl->bs_iovs[i].bi_prefix_len) {
			D_ASSERT(add_idx < bsgl->bs_nr + added_segs);
			bsgl->bs_iovs[add_idx].bi_addr.ba_off =
					bsgl->bs_iovs[i].bi_addr.ba_off;
			bsgl->bs_iovs[add_idx].bi_data_len =
					bsgl->bs_iovs[i].bi_prefix_len;

			bsgl->bs_iovs[add_idx].bi_addr.ba_type =
				bsgl->bs_iovs[i].bi_addr.ba_type;

			bsgl->bs_iovs[add_idx].bi_prefix_len = 0;
			bsgl->bs_iovs[add_idx].bi_suffix_len = 0;
			bsgl->bs_iovs[add_idx].bi_buf = NULL;
			bsgl->bs_iovs[add_idx++].bi_addr.ba_hole = 0;
		}
		if (bsgl->bs_iovs[i].bi_suffix_len) {
			D_ASSERT(add_idx < bsgl->bs_nr + added_segs);
			bsgl->bs_iovs[add_idx].bi_addr.ba_off =
					bsgl->bs_iovs[i].bi_addr.ba_off +
					bsgl->bs_iovs[i].bi_data_len -
					bsgl->bs_iovs[i].bi_suffix_len;
			bsgl->bs_iovs[add_idx].bi_data_len =
					bsgl->bs_iovs[i].bi_suffix_len;
			bsgl->bs_iovs[add_idx].bi_addr.ba_type =
				bsgl->bs_iovs[i].bi_addr.ba_type;
			bsgl->bs_iovs[add_idx].bi_prefix_len = 0;
			bsgl->bs_iovs[add_idx].bi_suffix_len = 0;
			bsgl->bs_iovs[add_idx].bi_buf = NULL;
			bsgl->bs_iovs[add_idx++].bi_addr.ba_hole = 0;
		}

		if (bsgl->bs_iovs[i].bi_prefix_len) {
			bsgl->bs_iovs[i].bi_addr.ba_off +=
						bsgl->bs_iovs[i].bi_prefix_len;
			bsgl->bs_iovs[i].bi_data_len -=
						bsgl->bs_iovs[i].bi_prefix_len;
			bsgl->bs_iovs[i].bi_prefix_len = 0;
		}
		if (bsgl->bs_iovs[i].bi_suffix_len) {
			bsgl->bs_iovs[i].bi_data_len -=
						bsgl->bs_iovs[i].bi_suffix_len;
			bsgl->bs_iovs[i].bi_suffix_len = 0;
		}
	}
	bsgl->bs_nr += added_segs;
	return 0;
}

int
vos_csum_recalc(struct vos_agg_io_context *io, struct bio_sglist *bsgl,
	    d_sg_list_t *sgl, struct evt_entry_in *ent_in,
	    struct csum_recalc *recalcs,
	    unsigned int recalc_seg_cnt, daos_size_t seg_size, bool unit_test)
{
	struct csum_recalc_args	args = { 0 };

	D_ASSERT(recalc_seg_cnt && recalcs[0].cr_phy_ent->pe_csum_info.cs_csum
		 && recalcs[0].cr_phy_ent->pe_csum_info.cs_nr &&
		 recalcs[0].cr_phy_ent->pe_csum_info.cs_type);

	args.cra_bsgl		= bsgl;
	args.cra_sgl		= sgl;
	args.cra_ent_in		= ent_in;
	args.cra_recalcs	= recalcs;
	args.cra_seg_size	= seg_size;
	args.cra_seg_cnt	= recalc_seg_cnt;
	args.cra_buf		= io->ic_buf;
	args.cra_buf_len	= io->ic_buf_len;

#ifdef OFF_LOAD
	if (!unit_test) {
		ABT_eventual_create(0, &args.csum_eventual);
		dss_ult_create(csum_agg_recalc, &args,
			       DSS_ULT_CHECKSUM, DSS_TGT_SELF, 0, NULL);
		ABT_eventual_wait(args.csum_eventual, NULL);
		ABT_eventual_free(*&args.csum_eventual);
	} else
#endif
		csum_agg_recalc(&args);

	return args.cra_rc;
}

unsigned int
vos_csum_prepare_ent(struct evt_entry_in *ent_in,
		     struct vos_agg_phy_ent *phy_ent)
{
	unsigned int chunksize = phy_ent->pe_csum_info.cs_chunksize;
	unsigned int cur_cnt = csum_chunk_count(chunksize,
						ent_in->ei_rect.rc_ex.ex_lo,
						ent_in->ei_rect.rc_ex.ex_hi,
						ent_in->ei_inob);

	ent_in->ei_csum.cs_nr = cur_cnt;
	ent_in->ei_csum.cs_type = phy_ent->pe_csum_info.cs_type;
	ent_in->ei_csum.cs_len = phy_ent->pe_csum_info.cs_len;
	ent_in->ei_csum.cs_buf_len = cur_cnt * ent_in->ei_csum.cs_len;
	ent_in->ei_csum.cs_chunksize = chunksize;
	ent_in->ei_csum.cs_not_valid = false;

	return cur_cnt * ent_in->ei_csum.cs_len;
}

int
vos_csum_prepare_buf(struct vos_agg_lgc_seg *segs, unsigned int seg_cnt,
		 void **csum_bufp, unsigned int cur_buf, unsigned int add_len)
{
	void		*buffer;
	unsigned char	*csum_buf = *csum_bufp;
	unsigned int	 new_len = cur_buf + add_len;
	int		 i;

	D_ASSERT(add_len);
	D_REALLOC(buffer, csum_buf, new_len);
	if (buffer == NULL)
		return -DER_NOMEM;
	csum_buf = buffer;
	memset(&csum_buf[cur_buf], 0, add_len);
	for (i = 0; i < seg_cnt; i++) {
		struct dcs_csum_info *csum_info = &segs[i].ls_ent_in.ei_csum;

		csum_info->cs_csum = &csum_buf[cur_buf];
		cur_buf += csum_info->cs_len * csum_info->cs_nr;
		D_ASSERT(cur_buf <= new_len);
	}

	return 0;
}
