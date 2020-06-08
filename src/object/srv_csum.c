/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
#include "daos_srv/srv_csum.h"

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

/**
 * Checksum context needed during the process of determining if new checksums
 * are needed for fetched extents.
 */
struct csum_context {
	/** csummer obj that will do checksum calculations if needed */
	struct daos_csummer	*cc_csummer;
	/** contains the data csums are protecting*/
	struct bio_sglist	*cc_bsgl;
	/** this will index the bsgl as they are processed for the given
	 * iod/recx
	 */
	struct daos_sgl_idx	*cc_bsgl_idx;
	/** always point to first selected record idx of biov (from 0).
	 * because the bsgl doesn't know where the data is in
	 * terms of recx/records ...
	 */
	daos_off_t		 cc_ext_start;
	/** checksums for the bsgl. There should be 1
	 * csum info for each iov in bsgl (that's not a hole)
	 */
	struct dcs_csum_info	*cc_biov_csums;
	uint64_t		 cc_biov_csums_idx;
	/** while processing, keep an index of csum within csum info  */
	uint64_t		 cc_biov_csum_idx;
	/** cached value of record size */
	size_t			 cc_rec_len;
	/** cached value of chunk size */
	daos_off_t		 cc_rec_chunksize;

	/** if new checksums are needed for chunks of recx, store original
	 * extent/csum so it can be verified
	 */
	struct to_verify	*cc_to_verify;
	uint32_t		 cc_to_verify_nr;
	/** Size of to_verify buffer how many csum/data to verify */
	uint64_t		 cc_to_verify_size;

	/** Has a new csum been started for current chunk */
	bool			 cc_csum_started;
	/**
	 * instead of copying a checksum for each chunk, copy
	 * many checksums from a csum info at once if able. The following
	 * fields help to manage this
	 */
	 /** destination csum info */
	struct dcs_csum_info	*cc_csums_to_copy_to;
	/** start of the dst csum buffer */
	uint64_t		 cc_csums_to_copy_to_csum_idx;
	/** start of the src csum bytes */
	uint8_t			*cc_csum_buf_to_copy;
	/** How many bytes to copy */
	uint64_t		 cc_csum_buf_to_copy_len;

	daos_size_t		 cc_chunk_bytes_left;

	/**
	 * Variety of ranges important during the fetch csum process.
	 * These ranges should be in terms of records (not bytes)
	 */
	/** full extent from bio iov - should map to evt_entry.en_ext */
	struct daos_csum_range	 cc_raw;
	/** requested extent from bio iov-should map to evt_entry.en_sel_ext */
	struct daos_csum_range	 cc_req;
	/** actual chunk */
	struct daos_csum_range	 cc_chunk;
	/** chunk boundaries adjusted to not extent recx */
	struct daos_csum_range	 cc_recx_chunk;
	/** chunk boundaries adjusted to not raw extent */
	struct daos_csum_range	 cc_raw_chunk;
	/** chunk boundaries adjusted to not raw extent */
	struct daos_csum_range	 cc_req_chunk;

	/** embedded structures */
	struct to_verify	 cc_to_verify_embedded[TO_VERIFY_EMBEDDED_NR];
};

static int
cc_verify_orig_extents(struct csum_context *ctx)
{
	struct daos_csummer	*csummer = ctx->cc_csummer;
	struct to_verify	*to_verify = ctx->cc_to_verify;
	uint32_t		 to_verify_nr = ctx->cc_to_verify_nr;
	uint16_t		 csum_len = daos_csummer_get_csum_len(csummer);
	uint32_t		 v;

	for (v = 0; v < to_verify_nr; v++) {
		C_TRACE("(CALC) Verifying original extent\n");
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
		if (!match) {
			D_ERROR("Original extent corrupted\n");
			return -DER_CSUM;
		}
	}

	return 0;
}

static int
cc_verify_resize_if_needed(struct csum_context *ctx)
{
	struct to_verify	*to_verify;
	uint64_t		 new_size;

	if (ctx->cc_to_verify_nr < ctx->cc_to_verify_size)
		return 0;

	new_size = ctx->cc_to_verify_size * 2;
	D_ALLOC_ARRAY(to_verify, new_size);
	if (to_verify == NULL)
		return -DER_NOMEM;

	memcpy(to_verify, ctx->cc_to_verify,
	       sizeof(*to_verify) * ctx->cc_to_verify_nr);
	if (ctx->cc_to_verify != ctx->cc_to_verify_embedded)
		D_FREE(ctx->cc_to_verify);
	ctx->cc_to_verify = to_verify;
	ctx->cc_to_verify_size = new_size;
	return 0;
}

static void
cc_remember_to_verify(struct csum_context *ctx, uint8_t *biov_csum,
		      struct bio_iov *biov)
{
	struct to_verify	*ver;

	cc_verify_resize_if_needed(ctx);
	ver = &ctx->cc_to_verify[ctx->cc_to_verify_nr];

	ver->tv_len = ctx->cc_raw_chunk.dcr_nr * ctx->cc_rec_len;

	ver->tv_buf = bio_iov2raw_buf(biov) +
		((ctx->cc_raw_chunk.dcr_lo - ctx->cc_raw.dcr_lo) *
		 ctx->cc_rec_len);

	D_ASSERT(biov_csum != NULL);
	ver->tv_csum = biov_csum;
	C_TRACE("To Verify len: %lu\n", ver->tv_len);
	ctx->cc_to_verify_nr++;
}

static struct bio_iov *
cc2iov(const struct csum_context *ctx)
{
	return bio_sgl_iov(ctx->cc_bsgl, ctx->cc_bsgl_idx->iov_idx);
}

static struct bio_iov *
cc2iov2(const struct csum_context *ctx)
{
	return bio_sgl_iov(ctx->cc_bsgl, ctx->cc_bsgl_idx->iov_idx + 1);
}

static size_t
cc_biov_bytes_left(const struct csum_context *ctx);

static bool
cc_need_new_csum(struct csum_context *ctx)
{

	return ds_csum_calc_needed(&ctx->cc_raw, &ctx->cc_req,
				   &ctx->cc_chunk,
				   ctx->cc_csum_started,
				   cc2iov2(ctx) != NULL);
}

static uint8_t *
cc2biovcsum(const struct csum_context *ctx)
{
	return ci_idx2csum(&ctx->cc_biov_csums[ctx->cc_biov_csums_idx],
			   ctx->cc_biov_csum_idx);
}

static void
cc_biov_csum_move_next(struct csum_context *ctx)
{
	ctx->cc_biov_csum_idx++;
}

/** Calculate new checksum */
static int
cc_new_csum_update(struct csum_context *ctx, struct dcs_csum_info *info,
		   uint32_t chunk_idx, size_t biov_bytes_for_chunk)
{
	struct bio_iov	*biov = cc2iov(ctx);
	uint16_t	 csum_len = daos_csummer_get_csum_len(ctx->cc_csummer);

	if (!ctx->cc_csum_started) {
		uint8_t *csum = ci_idx2csum(info, chunk_idx);

		C_TRACE("Starting new checksum for chunk: %d\n", chunk_idx);
		/** Setup csum to start updating */
		memset(csum, 0, csum_len);
		daos_csummer_set_buffer(ctx->cc_csummer, csum, csum_len);
		daos_csummer_reset(ctx->cc_csummer);
		ctx->cc_csum_started = true;
	}
	C_TRACE("(CALC) Updating new checksum. "
		"Chunk idx = %d, bytes for chunk = %lu\n",
		chunk_idx, biov_bytes_for_chunk);
	return daos_csummer_update(ctx->cc_csummer,
				 bio_iov2req_buf(biov) +
				 ctx->cc_bsgl_idx->iov_offset,
				 biov_bytes_for_chunk);
}

/**
 * Save info about the number of checksums that can be copied from extents
 * without the need to calculate new checksums
 */
static void
cc_remember_to_copy(struct csum_context *ctx, struct dcs_csum_info *info,
		    uint32_t idx, uint8_t *csum, uint16_t len)
{
	C_TRACE("Remember to copy csum (idx=%d, len=%d)\n", idx, len);
	if (csum == NULL) {
		D_ERROR("Expected to have checksums to copy for fetch.\n");
		return;
	}
	if (ctx->cc_csums_to_copy_to == NULL) {
		ctx->cc_csums_to_copy_to = info;
		ctx->cc_csums_to_copy_to_csum_idx = idx;
		ctx->cc_csum_buf_to_copy = csum;
		ctx->cc_csum_buf_to_copy_len = len;
	} else {
		ctx->cc_csum_buf_to_copy_len += len;
	}
}

/**
 * When can no longer just copy checksums from and extent (or done with the
 * extent) insert the entire csum buf into the destination csum info.
 */
static void
cc_insert_remembered_csums(struct csum_context *ctx)
{
	if (ctx->cc_csums_to_copy_to != NULL) {
		C_TRACE("Inserting csum (len=%"PRIu64"): "DF_CI_BUF"\n",
			ctx->cc_csum_buf_to_copy_len,
			DP_CI_BUF(ctx->cc_csum_buf_to_copy,
				  ctx->cc_csum_buf_to_copy_len));
		ci_insert(ctx->cc_csums_to_copy_to,
			  ctx->cc_csums_to_copy_to_csum_idx,
			  ctx->cc_csum_buf_to_copy,
			  ctx->cc_csum_buf_to_copy_len);
		ctx->cc_csums_to_copy_to = NULL;
	}
}

static void
cc_set_chunk2ranges(struct csum_context *ctx)
{
	daos_off_t cur_rec_idx =
		ctx->cc_ext_start + ctx->cc_bsgl_idx->iov_offset /
				    ctx->cc_rec_len;
	ctx->cc_raw_chunk = csum_recidx2range(ctx->cc_rec_chunksize,
					      cur_rec_idx, ctx->cc_raw.dcr_lo,
					      ctx->cc_raw.dcr_hi,
					      ctx->cc_rec_len);
	ctx->cc_req_chunk = csum_recidx2range(ctx->cc_rec_chunksize,
					      cur_rec_idx,
					      ctx->cc_req.dcr_lo,
					      ctx->cc_req.dcr_hi,
					      ctx->cc_rec_len);
}

/** set the raw (or actual) and the selected (or requested) ranges
 * for the extent the iov represents
 */
static void
cc_set_iov2ranges(struct csum_context *ctx, struct bio_iov *iov) {

	dcr_set_idx_nr(&ctx->cc_req, ctx->cc_ext_start, bio_iov2req_len(iov) /
							ctx->cc_rec_len);

	dcr_set_idx_nr(&ctx->cc_raw,
		       ctx->cc_ext_start - (iov->bi_prefix_len /
					    ctx->cc_rec_len),
		       bio_iov2raw_len(iov) / ctx->cc_rec_len);
}

static void
cc_iov_move_next(struct csum_context *ctx) {
	struct bio_iov *iov = cc2iov(ctx);

	/** update extent start with 'previous' iov requested len */
	ctx->cc_ext_start += bio_iov2req_len(iov) / ctx->cc_rec_len;

	/** move to the next biov */
	ctx->cc_bsgl_idx->iov_idx++;
	ctx->cc_bsgl_idx->iov_offset = 0;
	ctx->cc_biov_csum_idx = 0;

	/** get next iov and setup ranges with it */
	iov = cc2iov(ctx);
	if (iov) {
		cc_set_iov2ranges(ctx, iov);
		cc_set_chunk2ranges(ctx);
	}
}

/** Copy the extent/chunk checksum or calculate a new checksum if needed. */
static int
cc_add_csum(struct csum_context *ctx, struct dcs_csum_info *info,
	    uint32_t chunk_idx)
{
	uint8_t		*biov_csum;
	struct bio_iov	*biov;
	uint16_t	 csum_len;
	size_t		 biov_bytes_for_chunk;
	int		 rc;

	csum_len = daos_csummer_get_csum_len(ctx->cc_csummer);

	biov_csum = cc2biovcsum(ctx);
	biov = cc2iov(ctx);
	biov_bytes_for_chunk = min(ctx->cc_chunk_bytes_left,
				   cc_biov_bytes_left(ctx));

	if (!bio_addr_is_hole(&biov->bi_addr)) {
		if (cc_need_new_csum(ctx)) {
			cc_insert_remembered_csums(ctx);
			/** Calculate a new checksum */
			rc = cc_new_csum_update(ctx, info, chunk_idx,
						biov_bytes_for_chunk);
			if (rc != 0)
				return rc;

			cc_remember_to_verify(ctx, biov_csum, biov);
		} else {
			/** just copy the biov_csum */
			cc_remember_to_copy(ctx, info, chunk_idx, biov_csum,
					    csum_len);
		}
		cc_biov_csum_move_next(ctx);
	}

	/** increment the offset of the index for the biov */
	ctx->cc_bsgl_idx->iov_offset += biov_bytes_for_chunk;

	if (ctx->cc_bsgl_idx->iov_offset == bio_iov2req_len(biov)) {
		/** copy checksums saved from this biov */
		cc_insert_remembered_csums(ctx);

		/** only count csum_infos if not a hole */
		if (!bio_addr_is_hole(&biov->bi_addr))
			ctx->cc_biov_csums_idx++;

		/** move to the next biov */
		cc_iov_move_next(ctx);

	}
	ctx->cc_chunk_bytes_left -= biov_bytes_for_chunk;

	return 0;
}

static size_t
cc_biov_bytes_left(const struct csum_context *ctx) {
	struct bio_iov *biov = cc2iov(ctx);

	return bio_iov2req_len(biov) - ctx->cc_bsgl_idx->iov_offset;
}

/** For a given recx, add checksums to the outupt csum info. Data will come
 * from the bsgls in \ctx
 */
static int
cc_add_csums_for_recx(struct csum_context *ctx, daos_recx_t *recx,
		      struct dcs_csum_info *info)
{
	uint32_t		rec_chunksize;
	size_t			rec_size;
	uint32_t		chunk_nr;
	uint32_t		c; /** recx chunk index */
	uint32_t		sc; /** system chunk index */
	int			rc = 0;

	rec_size = ctx->cc_rec_len;
	rec_chunksize = ctx->cc_rec_chunksize;
	chunk_nr = daos_recx_calc_chunks(*recx, rec_size, rec_chunksize);

	/** Because the biovs are acquired by searching for the recx, the first
	 * selected/requested record of a biov will be the recx index
	 */
	ctx->cc_ext_start = recx->rx_idx;

	cc_set_iov2ranges(ctx, cc2iov(ctx));

	sc = (recx->rx_idx * rec_size) / rec_chunksize;
	for (c = 0; c < chunk_nr; c++, sc++) { /** for each chunk/checksum */
		ctx->cc_recx_chunk = csum_recx_chunkidx2range(recx, rec_size,
							      rec_chunksize, c);
		ctx->cc_chunk = csum_chunkrange(rec_chunksize / ctx->cc_rec_len,
						sc);
		ctx->cc_chunk_bytes_left = ctx->cc_recx_chunk.dcr_nr *
					   rec_size;

		ctx->cc_csum_started = false;
		cc_set_chunk2ranges(ctx);

		/** need to loop until chunk bytes are consumed because more
		 * than 1 extent might contribute to it
		 */
		while (ctx->cc_chunk_bytes_left > 0) {
			/** All out of data. Just return because request may
			 * be larger than previously written data
			 */
			if (ctx->cc_bsgl_idx->iov_idx >=
			    ctx->cc_bsgl->bs_nr_out)
				return 0;

			rc = cc_add_csum(ctx, info, c);
			if (rc != 0)
				return rc;
		}
		if (ctx->cc_csum_started)
			daos_csummer_finish(ctx->cc_csummer);

		rc = cc_verify_orig_extents(ctx);
		if (rc != 0)
			return rc;
		ctx->cc_to_verify_nr = 0;
	}
	cc_insert_remembered_csums(ctx);

	return rc;
}

static uint64_t
cc2biov_csums_nr(struct csum_context *ctx)
{
	return ctx->cc_biov_csum_idx + 1;
}

int
ds_csum_add2iod(daos_iod_t *iod, struct daos_csummer *csummer,
		struct bio_sglist *bsgl, struct dcs_csum_info *biov_csums,
		size_t *biov_csums_used, struct dcs_iod_csums *iod_csums)
{
	struct csum_context	ctx = {0};
	struct daos_sgl_idx	bsgl_idx = {0};
	int			rc = 0;
	uint32_t		i, j;

	if (biov_csums_used != NULL)
		*biov_csums_used = 0;

	if (!(daos_csummer_initialized(csummer) && bsgl))
		return 0;

	if (!csum_iod_is_supported(iod))
		return 0;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		ci_insert(&iod_csums->ic_data[0], 0,
			   biov_csums[0].cs_csum, biov_csums[0].cs_len);
		if (biov_csums_used != NULL)
			(*biov_csums_used) = 1;
		return 0;
	}

	/**
	 * Array value IOD ...
	 */

	/** Verify have correct csums for extents returned.
	 * Should be 1 biov_csums for each non-hole biov in bsgl
	 */
	for (i = 0, j = 0; i < bsgl->bs_nr_out; i++) {
		if (bio_addr_is_hole(&(bio_sgl_iov(bsgl, i)->bi_addr)))
			continue;
		if (!ci_is_valid(&biov_csums[j++])) {
			D_ERROR("Invalid csum for biov %d.\n", i);
			return -DER_CSUM;
		}
	}

	/** setup the context */
	ctx.cc_csummer = csummer;
	ctx.cc_bsgl_idx = &bsgl_idx;
	ctx.cc_rec_len = iod->iod_size;
	ctx.cc_rec_chunksize = daos_csummer_get_rec_chunksize(csummer,
							      iod->iod_size);
	ctx.cc_bsgl = bsgl;
	ctx.cc_biov_csums = biov_csums;
	ctx.cc_to_verify = ctx.cc_to_verify_embedded;
	ctx.cc_to_verify_size = TO_VERIFY_EMBEDDED_NR;

	iod_csums->ic_nr = iod->iod_nr;

	/** for each extent/checksum buf */
	for (i = 0; i < iod->iod_nr && rc == 0; i++) {
		daos_recx_t		*recx = &iod->iod_recxs[i];
		struct dcs_csum_info	*info = &iod_csums->ic_data[i];

		if (ctx.cc_rec_len > 0 && ci_is_valid(info)) {
			rc = cc_add_csums_for_recx(&ctx, recx, info);
			if (rc != 0) {
				D_ERROR("Failed to add csum for "
						"recx"DF_RECX": %d\n",
					DP_RECX(*recx), rc);
			}
		}
	}

	/** return the count of biov csums used. */
	if (biov_csums_used != NULL)
		*biov_csums_used = cc2biov_csums_nr(&ctx);

	return rc;
}

bool
ds_csum_calc_needed(struct daos_csum_range *raw_ext,
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
