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
	struct daos_csummer	*csummer;
	/** contains the data csums are protecting*/
	struct bio_sglist	*bsgl;
	/** this will index the bsgl as they are processed for the given
	 * iod/recx
	 */
	struct daos_sgl_idx	*bsgl_idx;
	/** always point to first selected record idx of biov (from 0).
	 * because the bsgl doesn't know where the data is in
	 * terms of recx/records ...
	 */
	daos_off_t		 ext_start;
	/** checksums for the bsgl. There should be 1
	 * dcb for each iov in bsgl
	 */
	daos_csum_buf_t		*biov_dcbs;
	uint64_t		 biov_dcb_idx;
	/** while processing, keep an index of csum within dcb  */
	uint64_t		 biov_csum_idx;
	/** cached value of record size */
	size_t			 rec_len;
	/** if new checksums are needed for chunks of recx, store original
	 * extent/csum so it can be verified
	 */
	struct to_verify	*to_verify;
	uint32_t		 to_verify_nr;
	/** Size of to_verify buffer how many csum/data to verify */
	uint64_t		 to_verify_size;

	/** Has a new csum been started for current chunk */
	bool			 csum_started;
	/**
	 * instead of copying a checksum for each chunk, copy
	 * many checksums from a dcb at once if able. The following
	 * fields help to manage this
	 */
	 /** destination dcb */
	daos_csum_buf_t		*dcb_to_copy_to;
	/** start of the dst csum buffer */
	uint64_t		 dcb_to_copy_to_csum_idx;
	/** start of the src csum bytes */
	uint8_t			*csum_buf_to_copy;
	/** How many bytes to copy */
	uint64_t		 csum_buf_to_copy_len;
	/** embedded structures */
	struct to_verify	 to_verify_embedded[TO_VERIFY_EMBEDDED_NR];
};

static int
vcc_verify_orig_extents(struct vos_csum_context *ctx)
{
	struct daos_csummer	*csummer = ctx->csummer;
	struct to_verify	*to_verify = ctx->to_verify;
	uint32_t		 to_verify_nr = ctx->to_verify_nr;
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

	if (ctx->to_verify_nr < ctx->to_verify_size)
		return 0;

	new_size = ctx->to_verify_size * 2;
	D_ALLOC_ARRAY(to_verify, new_size);
	if (to_verify == NULL)
		return -DER_NOMEM;

	memcpy(to_verify, ctx->to_verify,
	       sizeof(*to_verify) * ctx->to_verify_nr);
	if (ctx->to_verify != ctx->to_verify_embedded)
		D_FREE(ctx->to_verify);
	ctx->to_verify = to_verify;
	ctx->to_verify_size = new_size;
	return 0;
}

static void
vcc_remember_to_verify(struct vos_csum_context *ctx, uint8_t *biov_csum,
		       struct bio_iov *biov)
{
	struct to_verify	*ver;
	uint32_t		 chunksize;
	daos_off_t		 sel_lo;
	daos_off_t		 physical_lo;
	daos_off_t		 physical_hi;
	struct daos_csum_range	 chunk;

	vcc_verify_resize_if_needed(ctx);
	ver = &ctx->to_verify[ctx->to_verify_nr];

	/** Need to calculate the appropriate chunk to remember */
	chunksize = ctx->csummer->dcs_chunk_size;
	sel_lo = ctx->ext_start + ctx->bsgl_idx->iov_offset;
	physical_lo = ctx->ext_start - (biov->bi_prefix_len / ctx->rec_len);
	physical_hi = ctx->ext_start + (biov->bi_data_len + biov->bi_suffix_len)
				       / ctx->rec_len - 1;

	chunk = csum_recidx2range(chunksize, sel_lo, physical_lo, physical_hi,
				  ctx->rec_len);

	ver->tv_len = chunk.dcc_nr * ctx->rec_len;
	ver->tv_buf = biov->bi_buf + ctx->bsgl_idx->iov_offset -
		      (sel_lo - chunk.dcc_lo);
	ver->tv_csum = biov_csum;
	C_TRACE("To Verify len: %lu\n", ver->tv_len);
	ctx->to_verify_nr++;
}

static struct bio_iov *
vcc2iov(const struct vos_csum_context *ctx)
{
	return bio_sgl_iov(ctx->bsgl, ctx->bsgl_idx->iov_idx);
}

static struct bio_iov *
vcc2iov2(const struct vos_csum_context *ctx)
{
	return bio_sgl_iov(ctx->bsgl, ctx->bsgl_idx->iov_idx + 1);
}

static bool
vcc_need_new_csum(const struct vos_csum_context *ctx, uint32_t chunk_bytes)
{
	return vic_needs_new_csum(vcc2iov(ctx),
				  ctx->bsgl_idx->iov_offset, chunk_bytes,
				  ctx->ext_start * ctx->rec_len,
				  vcc2iov2(ctx),
				  ctx->csum_started,
				  ctx->csummer->dcs_chunk_size);
}

static uint8_t *
vcc2biovcsum(const struct vos_csum_context *ctx)
{
	return dcb_idx2csum(&ctx->biov_dcbs[ctx->biov_dcb_idx],
			    ctx->biov_csum_idx);
}

static void
vcc_biov_csum_move_next(struct vos_csum_context *ctx)
{
	ctx->biov_csum_idx++;
}

/** Calculate new checksum */
static int
vcc_new_csum_update(struct vos_csum_context *ctx, daos_csum_buf_t *dcb,
		    uint32_t chunk_idx, size_t biov_bytes_for_chunk)
{
	struct bio_iov	*biov = vcc2iov(ctx);
	uint16_t	 csum_len = daos_csummer_get_csum_len(ctx->csummer);

	if (!ctx->csum_started) {
		uint8_t *csum = dcb_idx2csum(dcb, chunk_idx);

		C_TRACE("Starting new checksum for chunk: %d\n", chunk_idx);
		/** Setup csum to start updating */
		memset(csum, 0, csum_len);
		daos_csummer_set_buffer(ctx->csummer, csum, csum_len);
		daos_csummer_reset(ctx->csummer);
		ctx->csum_started = true;
	}
	C_TRACE("Updating for new checksum. "
		"Chunk idx = %d, bytes for chunk = %lu\n",
		chunk_idx, biov_bytes_for_chunk);
	return daos_csummer_update(ctx->csummer,
				 biov->bi_buf +
				 ctx->bsgl_idx->iov_offset,
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
	if (ctx->dcb_to_copy_to == NULL) {
		ctx->dcb_to_copy_to = dcb;
		ctx->dcb_to_copy_to_csum_idx = idx;
		ctx->csum_buf_to_copy = csum;
		ctx->csum_buf_to_copy_len = len;
	} else {
		ctx->csum_buf_to_copy_len += len;
	}
}

/**
 * When can no longer just copy checksums from and extent (or done with the
 * extent) insert the entire csum buf into the destination dcb.
 */
static void
vcc_insert_remembered_csums(struct vos_csum_context *ctx)
{
	if (ctx->dcb_to_copy_to != NULL) {
		dcb_insert(ctx->dcb_to_copy_to, ctx->dcb_to_copy_to_csum_idx,
			   ctx->csum_buf_to_copy, ctx->csum_buf_to_copy_len);
		ctx->dcb_to_copy_to = NULL;
	}
}

/** Copy the extent/chunk checksum or calculate a new checksum if needed. */
static int
vcc_add_csum(struct vos_csum_context *ctx, daos_csum_buf_t *dcb,
	     uint32_t chunk_idx, uint32_t *bytes_needed_for_chunk)
{
	uint8_t		*biov_csum;
	struct bio_iov	*biov;
	uint16_t	 csum_len;
	size_t		 biov_bytes_for_chunk;
	int		 rc;

	csum_len = daos_csummer_get_csum_len(ctx->csummer);

	biov_csum = vcc2biovcsum(ctx);
	biov = vcc2iov(ctx);
	biov_bytes_for_chunk = min(*bytes_needed_for_chunk,
				   biov->bi_data_len -
				   ctx->bsgl_idx->iov_offset);

	if (!bio_addr_is_hole(&biov->bi_addr)) {
		if (vcc_need_new_csum(ctx, *bytes_needed_for_chunk)) {
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
	ctx->bsgl_idx->iov_offset += biov_bytes_for_chunk;

	if (ctx->bsgl_idx->iov_offset == biov->bi_data_len) {
		/** copy checksums saved from this biov */
		vcc_insert_remembered_csums(ctx);

		/** move to the next biov */
		ctx->ext_start += ctx->bsgl_idx->iov_offset / ctx->rec_len;
		ctx->bsgl_idx->iov_idx++;
		ctx->bsgl_idx->iov_offset = 0;
		ctx->biov_csum_idx = 0;
		if (!bio_addr_is_hole(&biov->bi_addr))
			ctx->biov_dcb_idx++;
	}
	*bytes_needed_for_chunk -= biov_bytes_for_chunk;

	return 0;
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
	struct daos_csum_range	chunk;
	uint32_t		chunk_bytes_left;
	int			rc = 0;

	chunksize = daos_csummer_get_chunksize(ctx->csummer);
	rec_size = ctx->rec_len;
	chunk_nr = daos_recx_calc_chunks(*recx, rec_size, chunksize);

	ctx->ext_start = recx->rx_idx;

	for (c = 0; c < chunk_nr; c++) { /** for each chunk/checksum */
		chunk = csum_recx_chunkidx2range(recx, rec_size, chunksize, c);
		chunk_bytes_left = chunk.dcc_nr * rec_size;

		ctx->csum_started = false;
		while (chunk_bytes_left > 0) {
			/** All out of data. Just return because request may
			 * be larger than previously written data
			 */
			if (ctx->bsgl_idx->iov_idx >= ctx->bsgl->bs_nr_out)
				return 0;

			rc = vcc_add_csum(ctx, dcb, c, &chunk_bytes_left);
			if (rc != 0)
				return rc;
		}
		if (ctx->csum_started)
			daos_csummer_finish(ctx->csummer);

		rc = vcc_verify_orig_extents(ctx);
		if (rc != 0)
			return rc;
		ctx->to_verify_nr = 0;
	}
	vcc_insert_remembered_csums(ctx);

	return rc;
}

static uint64_t
vcc2biov_dcbs_nr(struct vos_csum_context *ctx)
{
	return ctx->biov_csum_idx;
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
	ctx.csummer = csummer;
	ctx.bsgl_idx = &bsgl_idx;
	ctx.rec_len = iod->iod_size;
	ctx.bsgl = bsgl;
	ctx.biov_dcbs = biov_dcbs;
	ctx.to_verify = ctx.to_verify_embedded;
	ctx.to_verify_size = TO_VERIFY_EMBEDDED_NR;

	/** for each extent/checksum buf */
	for (i = 0; i < iod->iod_nr && rc == 0; i++) {
		daos_recx_t		*recx = &iod->iod_recxs[i];
		daos_csum_buf_t		*dcb = &iod->iod_csums[i];

		if (ctx.rec_len > 0 && dcb_is_valid(dcb))
			rc = vcc_add_csums_for_recx(&ctx, recx, dcb);
	}

	/** return the cound of biov csums used. */
	if (biov_dcbs_used != NULL)
		*biov_dcbs_used = vcc2biov_dcbs_nr(&ctx);

	return rc;
}

bool
vic_needs_new_csum(struct bio_iov *biov, daos_off_t biov_bytes_used,
		   uint32_t chunk_bytes, size_t biov_byte_start,
		   struct bio_iov *next_biov, bool csum_started,
		   uint32_t chunksize)
{
	size_t	bytes_left_in_biov;
	size_t	chunk_bytes_hi;
	size_t	biov_hi;
	bool	is_only_extent_in_chunk;
	bool	biov_extends_past_chunk;
	bool	using_whole_chunk_of_extent;

	bytes_left_in_biov = biov->bi_data_len - biov_bytes_used;
	chunk_bytes_hi = csum_chunk_align_ceiling(biov_byte_start +
						  biov_bytes_used, chunksize);
	biov_hi = biov_byte_start + biov->bi_data_len - 1;

	/** in order to use stored csum
	 * - a new csum must not have already been started (would mean a
	 *	previous biov contributed to current chunk.
	 * - there must not be a different biov within the same chunk after
	 * - the end of the biov is at or after the end of the requested chunk
	 *	or the biov end is less than requested chunk end and the
	 *	'selected' biov is the whole biov (no extra end/begin)
	 */
	is_only_extent_in_chunk = !csum_started && /** nothing before */
				  (next_biov == NULL || /** nothing after */
				   /** next extent is after current chunk */
				   bytes_left_in_biov >= chunk_bytes);

	biov_extends_past_chunk = biov_hi >= chunk_bytes_hi;

	using_whole_chunk_of_extent = biov_extends_past_chunk ||
				      (biov_hi < chunk_bytes_hi &&
				       biov->bi_suffix_len == 0 &&
				       biov->bi_prefix_len == 0);

	return !(is_only_extent_in_chunk && using_whole_chunk_of_extent);
}

void
vic_update_biov(struct bio_iov *biov, struct evt_entry *ent,
		daos_size_t rsize, daos_csum_buf_t *dcbs,
		uint32_t *dcb_count)
{
	struct evt_extent aligned_extent;

	if (!dcb_is_valid(&ent->en_csum)) {
		biov->bi_prefix_len = 0;
		biov->bi_suffix_len = 0;
		return;
	}

	dcbs[*dcb_count] = ent->en_csum;
	if (DAOS_FAIL_CHECK(DAOS_CHECKSUM_FETCH_FAIL))
		/* poison the checksum */
		dcbs[*dcb_count].cs_csum[0] += 2;
	(*dcb_count)++;

	aligned_extent = evt_entry_align_to_csum_chunk(ent, rsize);

	biov->bi_prefix_len = (ent->en_sel_ext.ex_lo - aligned_extent.ex_lo) *
			      rsize;
	biov->bi_suffix_len = (aligned_extent.ex_hi - ent->en_sel_ext.ex_hi) *
			      rsize;
}
