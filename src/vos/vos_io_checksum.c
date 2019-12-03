/**
 * (C) Copyright 2019 Intel Corporation.
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

#include <daos/common.h>
#include <daos/checksum.h>
#include <daos_types.h>
#include "vos_io_checksum.h"
#include "evt_priv.h"

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)

/** Holds information about checksum and  data to verify when a
 * new checksum for an extent chunk is needed
 */
struct to_verify {
	void		*tv_buf;
	uint8_t		*tv_csum;
	size_t		 tv_len;
};

#define TO_VERIFY_EMBEDDED_NR	16

/** Checksum Fetch Context */
struct vos_csum_context {
	/** csummer obj that will do checksum calculations if needed */
	struct daos_csummer	*vcc_csummer;
	/** contains the data csums are protecting*/
	struct bio_sglist	*vcc_bsgl;
	/** this will index the bsgl as they are processed for the given
	 * iod/recx
	 */
	struct daos_sgl_idx	*vcc_bsgl_idx;
	/** always point to first selected record idx of biov (from 0).
	 * because the bsgl doesn't know where the data is in
	 * terms of recx/records ...
	 */
	daos_off_t		 vcc_ext_start;
	/** checksums for the bsgl. There should be 1
	 * dcb for each iov in bsgl (that's not a hole)
	 */
	daos_csum_buf_t		*vcc_biov_dcbs;
	uint64_t		 vcc_biov_dcb_idx;
	/** while processing, keep an index of csum within dcb  */
	uint64_t		 vcc_biov_csum_idx;
	/** cached value of record size */
	size_t			 vcc_rec_len;
	/** cached value of chunk size */
	daos_off_t		 vcc_chunksize;

	/** if new checksums are needed for chunks of recx, store original
	 * extent/csum so it can be verified
	 */
	struct to_verify	*vcc_to_verify;
	uint32_t		 vcc_to_verify_nr;
	/** Size of to_verify buffer how many csum/data to verify */
	uint64_t		 vcc_to_verify_size;

	/** Has a new csum been started for current chunk */
	bool			 vcc_csum_started;
	/**
	 * instead of copying a checksum for each chunk, copy
	 * many checksums from a dcb at once if able. The following
	 * fields help to manage this
	 */
	 /** destination dcb */
	daos_csum_buf_t		*vcc_dcb_to_copy_to;
	/** start of the dst csum buffer */
	uint64_t		 vcc_dcb_to_copy_to_csum_idx;
	/** start of the src csum bytes */
	uint8_t			*vcc_csum_buf_to_copy;
	/** How many bytes to copy */
	uint64_t		 vcc_csum_buf_to_copy_len;

	daos_size_t		 vcc_chunk_bytes_left;

	/**
	 * Variety of ranges important during the fetch csum process.
	 * These ranges should be in terms of records (not bytes)
	 */
	/** full extent from bio iov - should map to evt_entry.en_ext */
	struct daos_csum_range	 vcc_raw;
	/** requested extent from bio iov-should map to evt_entry.en_sel_ext */
	struct daos_csum_range	 vcc_req;
	/** actual chunk */
	struct daos_csum_range	 vcc_chunk;
	/** chunk boundaries adjusted to not extent recx */
	struct daos_csum_range	 vcc_recx_chunk;
	/** chunk boundaries adjusted to not raw extent */
	struct daos_csum_range	 vcc_raw_chunk;
	/** chunk boundaries adjusted to not raw extent */
	struct daos_csum_range	 vcc_req_chunk;

	/** embedded structures */
	struct to_verify	 vcc_to_verify_embedded[TO_VERIFY_EMBEDDED_NR];
};

static int
vcc_verify_orig_extents(struct vos_csum_context *ctx)
{
	struct daos_csummer	*csummer = ctx->vcc_csummer;
	struct to_verify	*to_verify = ctx->vcc_to_verify;
	uint32_t		 to_verify_nr = ctx->vcc_to_verify_nr;
	uint16_t		 csum_len = daos_csummer_get_csum_len(csummer);
	uint32_t		 v;

	for (v = 0; v < to_verify_nr; v++) {
		uint8_t			 csum[csum_len];
		bool			 match;
		struct to_verify	*verify;

		memset(csum, 0, csum_len);
		daos_csummer_reset(csummer);
		daos_csummer_set_buffer(csummer, csum, csum_len);
		verify = &to_verify[v];
		daos_csummer_update(csummer, verify->tv_buf, verify->tv_len);
		daos_csummer_finish(csummer);

		match = daos_csummer_csum_compare(csummer, csum,
						  verify->tv_csum, csum_len);
		if (!match)
			return -DER_CSUM;
	}

	return 0;
}

static int
vcc_verify_resize_if_needed(struct vos_csum_context *ctx)
{
	struct to_verify	*to_verify;
	uint64_t		 new_size;

	if (ctx->vcc_to_verify_nr < ctx->vcc_to_verify_size)
		return 0;

	new_size = ctx->vcc_to_verify_size * 2;
	D_ALLOC_ARRAY(to_verify, new_size);
	if (to_verify == NULL)
		return -DER_NOMEM;

	memcpy(to_verify, ctx->vcc_to_verify,
	       sizeof(*to_verify) * ctx->vcc_to_verify_nr);
	if (ctx->vcc_to_verify != ctx->vcc_to_verify_embedded)
		D_FREE(ctx->vcc_to_verify);
	ctx->vcc_to_verify = to_verify;
	ctx->vcc_to_verify_size = new_size;
	return 0;
}

static void
vcc_remember_to_verify(struct vos_csum_context *ctx, uint8_t *biov_csum,
		       struct bio_iov *biov)
{
	struct to_verify	*ver;

	vcc_verify_resize_if_needed(ctx);
	ver = &ctx->vcc_to_verify[ctx->vcc_to_verify_nr];

	ver->tv_len = ctx->vcc_raw_chunk.dcr_nr * ctx->vcc_rec_len;

	ver->tv_buf = bio_iov2raw_buf(biov) +
		((ctx->vcc_raw_chunk.dcr_lo - ctx->vcc_raw.dcr_lo) *
		 ctx->vcc_rec_len);

	ver->tv_csum = biov_csum;
	C_TRACE("To Verify len: %lu\n", ver->tv_len);
	ctx->vcc_to_verify_nr++;
}

static struct bio_iov *
vcc2iov(const struct vos_csum_context *ctx)
{
	return bio_sgl_iov(ctx->vcc_bsgl, ctx->vcc_bsgl_idx->iov_idx);
}

static struct bio_iov *
vcc2iov2(const struct vos_csum_context *ctx)
{
	return bio_sgl_iov(ctx->vcc_bsgl, ctx->vcc_bsgl_idx->iov_idx + 1);
}

static size_t
vcc_biov_bytes_left(const struct vos_csum_context *ctx);

static bool
vcc_need_new_csum(struct vos_csum_context *ctx)
{

	return vic_needs_new_csum(&ctx->vcc_raw, &ctx->vcc_req,
				  &ctx->vcc_chunk,
				  ctx->vcc_csum_started,
				  vcc2iov2(ctx) != NULL);
}

static uint8_t *
vcc2biovcsum(const struct vos_csum_context *ctx)
{
	return dcb_idx2csum(&ctx->vcc_biov_dcbs[ctx->vcc_biov_dcb_idx],
			    ctx->vcc_biov_csum_idx);
}

static void
vcc_biov_csum_move_next(struct vos_csum_context *ctx)
{
	ctx->vcc_biov_csum_idx++;
}

/** Calculate new checksum */
static int
vcc_new_csum_update(struct vos_csum_context *ctx, daos_csum_buf_t *dcb,
		    uint32_t chunk_idx, size_t biov_bytes_for_chunk)
{
	struct bio_iov	*biov = vcc2iov(ctx);
	uint16_t	 csum_len = daos_csummer_get_csum_len(ctx->vcc_csummer);

	if (!ctx->vcc_csum_started) {
		uint8_t *csum = dcb_idx2csum(dcb, chunk_idx);

		C_TRACE("Starting new checksum for chunk: %d\n", chunk_idx);
		/** Setup csum to start updating */
		memset(csum, 0, csum_len);
		daos_csummer_set_buffer(ctx->vcc_csummer, csum, csum_len);
		daos_csummer_reset(ctx->vcc_csummer);
		ctx->vcc_csum_started = true;
	}
	C_TRACE("Updating for new checksum. "
		"Chunk idx = %d, bytes for chunk = %lu\n",
		chunk_idx, biov_bytes_for_chunk);
	return daos_csummer_update(ctx->vcc_csummer,
				 bio_iov2req_buf(biov) +
				 ctx->vcc_bsgl_idx->iov_offset,
				 biov_bytes_for_chunk);
}

/**
 * Save info about the number of checksums that can be copied from extents
 * without the need to calculate new checksums
 */
static void
vcc_remember_to_copy(struct vos_csum_context *ctx, daos_csum_buf_t *dcb,
		     uint32_t idx, uint8_t *csum, uint16_t len)
{
	C_TRACE("Remember to copy csum (idx=%d, len=%d)\n", idx, len);
	if (ctx->vcc_dcb_to_copy_to == NULL) {
		ctx->vcc_dcb_to_copy_to = dcb;
		ctx->vcc_dcb_to_copy_to_csum_idx = idx;
		ctx->vcc_csum_buf_to_copy = csum;
		ctx->vcc_csum_buf_to_copy_len = len;
	} else {
		ctx->vcc_csum_buf_to_copy_len += len;
	}
}

/**
 * When can no longer just copy checksums from and extent (or done with the
 * extent) insert the entire csum buf into the destination dcb.
 */
static void
vcc_insert_remembered_csums(struct vos_csum_context *ctx)
{
	if (ctx->vcc_dcb_to_copy_to != NULL) {
		C_TRACE("Inserting csum (len=%lu)\n",
			ctx->vcc_csum_buf_to_copy_len);
		dcb_insert(ctx->vcc_dcb_to_copy_to,
			   ctx->vcc_dcb_to_copy_to_csum_idx,
			   ctx->vcc_csum_buf_to_copy,
			   ctx->vcc_csum_buf_to_copy_len);
		ctx->vcc_dcb_to_copy_to = NULL;
	}
}


static void
vcc_set_chunk2ranges(struct vos_csum_context *ctx)
{
	daos_off_t cur_rec_idx =
		ctx->vcc_ext_start + ctx->vcc_bsgl_idx->iov_offset /
				     ctx->vcc_rec_len;
	ctx->vcc_raw_chunk = csum_recidx2range(ctx->vcc_chunksize,
					       cur_rec_idx, ctx->vcc_raw.dcr_lo,
					       ctx->vcc_raw.dcr_hi,
					       ctx->vcc_rec_len);
	ctx->vcc_req_chunk = csum_recidx2range(ctx->vcc_chunksize,
					       cur_rec_idx,
					       ctx->vcc_req.dcr_lo,
					       ctx->vcc_req.dcr_hi,
					       ctx->vcc_rec_len);
}

/** set the raw (or actual) and the selected (or requested) ranges
 * for the extent the iov represents
 */
static void
vcc_set_iov2ranges(struct vos_csum_context *ctx, struct bio_iov *iov) {

	dcr_set_idx_nr(&ctx->vcc_req, ctx->vcc_ext_start, bio_iov2req_len(iov) /
							  ctx->vcc_rec_len);

	dcr_set_idx_nr(&ctx->vcc_raw,
		       ctx->vcc_ext_start - (iov->bi_prefix_len /
					    ctx->vcc_rec_len),
		       bio_iov2raw_len(iov) / ctx->vcc_rec_len);
}

static void
vcc_iov_move_next(struct vos_csum_context *ctx) {
	struct bio_iov *iov = vcc2iov(ctx);

	/** update extent start with 'previous' iov requested len */
	ctx->vcc_ext_start += bio_iov2req_len(iov) / ctx->vcc_rec_len;

	/** move to the next biov */
	ctx->vcc_bsgl_idx->iov_idx++;
	ctx->vcc_bsgl_idx->iov_offset = 0;
	ctx->vcc_biov_csum_idx = 0;

	/** get next iov and setup ranges with it */
	iov = vcc2iov(ctx);
	if (iov) {
		vcc_set_iov2ranges(ctx, iov);
		vcc_set_chunk2ranges(ctx);
	}
}

/** Copy the extent/chunk checksum or calculate a new checksum if needed. */
static int
vcc_add_csum(struct vos_csum_context *ctx, daos_csum_buf_t *dcb,
	     uint32_t chunk_idx)
{
	uint8_t		*biov_csum;
	struct bio_iov	*biov;
	uint16_t	 csum_len;
	size_t		 biov_bytes_for_chunk;
	int		 rc;

	csum_len = daos_csummer_get_csum_len(ctx->vcc_csummer);

	biov_csum = vcc2biovcsum(ctx);
	biov = vcc2iov(ctx);
	biov_bytes_for_chunk = min(ctx->vcc_chunk_bytes_left,
				   vcc_biov_bytes_left(ctx));

	if (!bio_addr_is_hole(&biov->bi_addr)) {
		if (vcc_need_new_csum(ctx)) {
			vcc_insert_remembered_csums(ctx);
			/** Calculate a new checksum */
			rc = vcc_new_csum_update(ctx, dcb, chunk_idx,
						 biov_bytes_for_chunk);
			if (rc != 0)
				return rc;

			vcc_remember_to_verify(ctx, biov_csum, biov);
		} else {
			/** just copy the biov_csum */
			vcc_remember_to_copy(ctx, dcb, chunk_idx, biov_csum,
					     csum_len);
		}
		vcc_biov_csum_move_next(ctx);
	}

	/** increment the offset of the index for the biov */
	ctx->vcc_bsgl_idx->iov_offset += biov_bytes_for_chunk;

	if (ctx->vcc_bsgl_idx->iov_offset == bio_iov2req_len(biov)) {
		/** copy checksums saved from this biov */
		vcc_insert_remembered_csums(ctx);

		/** only count dcbs if not a hole */
		if (!bio_addr_is_hole(&biov->bi_addr))
			ctx->vcc_biov_dcb_idx++;

		/** move to the next biov */
		vcc_iov_move_next(ctx);

	}
	ctx->vcc_chunk_bytes_left -= biov_bytes_for_chunk;

	return 0;
}

static size_t
vcc_biov_bytes_left(const struct vos_csum_context *ctx) {
	struct bio_iov *biov = vcc2iov(ctx);

	return bio_iov2req_len(biov) - ctx->vcc_bsgl_idx->iov_offset;
}

/** For a given recx, add checksums to the outupt dcb. Data will come from the
 * bsgls in \ctx
 */
static int
vcc_add_csums_for_recx(struct vos_csum_context *ctx, daos_recx_t *recx,
		       daos_csum_buf_t *dcb)
{
	uint32_t		chunksize;
	size_t			rec_size;
	uint32_t		chunk_nr;
	uint32_t		c;
	int			rc = 0;

	chunksize = daos_csummer_get_chunksize(ctx->vcc_csummer);
	rec_size = ctx->vcc_rec_len;
	chunk_nr = daos_recx_calc_chunks(*recx, rec_size, chunksize);

	/** Because the biovs are acquired by searching for the recx, the first
	 * selected/requested record of a biov will be the recx index
	 */
	ctx->vcc_ext_start = recx->rx_idx;

	vcc_set_iov2ranges(ctx, vcc2iov(ctx));

	for (c = 0; c < chunk_nr; c++) { /** for each chunk/checksum */
		ctx->vcc_recx_chunk = csum_recx_chunkidx2range(recx, rec_size,
							       chunksize, c);
		ctx->vcc_chunk = csum_chunkrange(chunksize / ctx->vcc_rec_len,
						 c);
		ctx->vcc_chunk_bytes_left = ctx->vcc_recx_chunk.dcr_nr *
					    rec_size;

		ctx->vcc_csum_started = false;
		vcc_set_chunk2ranges(ctx);

		/** need to loop until chunk bytes are consumed because more
		 * than 1 extent might contribute to it
		 */
		while (ctx->vcc_chunk_bytes_left > 0) {
			/** All out of data. Just return because request may
			 * be larger than previously written data
			 */
			if (ctx->vcc_bsgl_idx->iov_idx >=
			    ctx->vcc_bsgl->bs_nr_out)
				return 0;

			rc = vcc_add_csum(ctx, dcb, c);
			if (rc != 0)
				return rc;
		}
		if (ctx->vcc_csum_started)
			daos_csummer_finish(ctx->vcc_csummer);

		rc = vcc_verify_orig_extents(ctx);
		if (rc != 0)
			return rc;
		ctx->vcc_to_verify_nr = 0;
	}
	vcc_insert_remembered_csums(ctx);

	return rc;
}

static uint64_t
vcc2biov_dcbs_nr(struct vos_csum_context *ctx)
{
	return ctx->vcc_biov_csum_idx;
}

int
vic_fetch_iod(daos_iod_t *iod, struct daos_csummer *csummer,
	      struct bio_sglist *bsgl, daos_csum_buf_t *biov_dcbs,
	      size_t *biov_dcbs_used)
{
	struct vos_csum_context	ctx = {0};
	struct daos_sgl_idx	bsgl_idx = {0};
	int			rc = 0;
	uint32_t		i;

	if (!(daos_csummer_initialized(csummer) && iod->iod_recxs && bsgl))
		return 0;

	if (!csum_iod_is_supported(csummer->dcs_chunk_size, iod))
		return 0;

	/** setup the context */
	ctx.vcc_csummer = csummer;
	ctx.vcc_bsgl_idx = &bsgl_idx;
	ctx.vcc_rec_len = iod->iod_size;
	ctx.vcc_chunksize = csummer->dcs_chunk_size;
	ctx.vcc_bsgl = bsgl;
	ctx.vcc_biov_dcbs = biov_dcbs;
	ctx.vcc_to_verify = ctx.vcc_to_verify_embedded;
	ctx.vcc_to_verify_size = TO_VERIFY_EMBEDDED_NR;

	/** for each extent/checksum buf */
	for (i = 0; i < iod->iod_nr && rc == 0; i++) {
		daos_recx_t		*recx = &iod->iod_recxs[i];
		daos_csum_buf_t		*dcb = &iod->iod_csums[i];

		if (ctx.vcc_rec_len > 0 && dcb_is_valid(dcb))
			rc = vcc_add_csums_for_recx(&ctx, recx, dcb);
	}

	/** return the cound of biov csums used. */
	if (biov_dcbs_used != NULL)
		*biov_dcbs_used = vcc2biov_dcbs_nr(&ctx);

	return rc;
}

bool
vic_needs_new_csum(struct daos_csum_range *raw_ext,
		   struct daos_csum_range *req_ext,
		   struct daos_csum_range *chunk,
		   bool csum_started,
		   bool has_next_biov)
{
	bool	is_only_extent_in_chunk;
	bool	biov_extends_past_chunk;
	bool	using_whole_chunk_of_extent;

	/** in order to use stored csum
	 * - a new csum must not have already been started (would mean a
	 *	previous biov contributed to current chunk.
	 * - there must not be a different biov within the same chunk after
	 * - the end of the biov is at or after the end of the requested chunk
	 *	or the biov end is less than requested chunk end and the
	 *	'selected' biov is the whole biov (no extra end/begin)
	 */
	/** current extent extends past of chunk */
	biov_extends_past_chunk = req_ext->dcr_hi >= chunk->dcr_hi;
	is_only_extent_in_chunk = !csum_started && /** nothing before */
				  (!has_next_biov || /** nothing after */
				   biov_extends_past_chunk);


	using_whole_chunk_of_extent = biov_extends_past_chunk ||
				      (req_ext->dcr_hi < chunk->dcr_hi &&
				       req_ext->dcr_lo == raw_ext->dcr_lo &&
				       req_ext->dcr_hi == raw_ext->dcr_hi
				       );

	return !(is_only_extent_in_chunk && using_whole_chunk_of_extent);
}

void
vic_update_biov(struct bio_iov *biov, struct evt_entry *ent,
		daos_size_t rsize, daos_csum_buf_t *dcbs,
		uint32_t *dcb_count)
{
	struct evt_extent aligned_extent;

	if (!dcb_is_valid(&ent->en_csum)) {
		bio_iov_set_extra(biov, 0, 0);
		return;
	}

	dcbs[*dcb_count] = ent->en_csum;
	if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_FETCH_FAIL))
		/* poison the checksum */
		dcbs[*dcb_count].cs_csum[0] += 2;
	(*dcb_count)++;

	aligned_extent = evt_entry_align_to_csum_chunk(ent, rsize);
	bio_iov_set_extra(biov,
			  (ent->en_sel_ext.ex_lo - aligned_extent.ex_lo) *
			  rsize,
			  (aligned_extent.ex_hi - ent->en_sel_ext.ex_hi) *
			  rsize);
}
