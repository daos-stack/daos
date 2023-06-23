/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <daos/common.h>
#include <gurt/common.h>
#include <daos/checksum.h>
#include <daos/object.h>
#include <daos_types.h>
#include <daos_srv/bio.h>

#define C_TRACE(...) D_DEBUG(DB_CSUM, __VA_ARGS__)
#define YES_NO(b) b ? "YES" : "NO"

/** Holds information about checksum and data to verify when a
 * new checksum for an extent chunk is needed
 */
struct to_verify {
	void		*tv_buf;
	uint8_t		*tv_csum;
	size_t		 tv_len;
};

#define TO_VERIFY_EMBEDDED_NR	16

struct biov_ranges {
	struct daos_csum_range	br_req;
	struct daos_csum_range	br_raw;
	bool			br_has_prefix;
	bool			br_has_suffix;
};

struct csum_context {
	/** csummer obj that will do checksum calculations if needed */
	struct daos_csummer	*cc_csummer;
	/** cached value of record size */
	size_t			 cc_rec_len;
	/** cached value of chunk size (in number of records, not bytes) */
	daos_off_t		 cc_rec_chunksize;
	uint16_t		 cc_csum_len;
	/** contains the data csums are protecting */
	struct bio_sglist	*cc_bsgl;

	daos_recx_t		*cc_cur_recx;
	uint64_t		 cc_cur_recx_idx;
	struct dcs_csum_info	*cc_cur_csum_info;
	/** index into the csum_info.csum */
	uint64_t		 cc_csum_idx;

	/** this will index the bsgl as they are processed for the given
	 * iod/recx
	 */
	struct daos_sgl_idx	cc_bsgl_idx;
	/** represents the current biov raw/req ranges in records */
	struct biov_ranges	cc_biov_ranges;
	/** checksums for the bsgl. There should be 1
	 * csum info for each iov in bsgl (that's not a hole)
	 */
	struct dcs_ci_list	*cc_biov_csums;
	uint64_t		 cc_biov_csums_idx;
	/** while processing, keep an index of csum within csum info  */
	uint64_t		 cc_biov_csum_idx;

	/** if new checksums are needed for chunks of recx, store original
	 * extent/csum so it can be verified
	 */
	struct to_verify	*cc_to_verify;
	uint32_t		 cc_to_verify_nr;
	/** Size of to_verify buffer how many csum/data to verify */
	uint64_t		 cc_to_verify_size;

	/** embedded structures */
	struct to_verify	 cc_to_verify_embedded[TO_VERIFY_EMBEDDED_NR];
};

static void
cc_init(struct csum_context *ctx, struct daos_csummer *csummer, struct bio_sglist *bsgl,
	struct dcs_ci_list *biov_csums, daos_size_t size)
{
	ctx->cc_csummer = csummer;
	ctx->cc_rec_len = size;
	ctx->cc_rec_chunksize = daos_csummer_get_rec_chunksize(csummer, size) /
									size;
	ctx->cc_csum_len = daos_csummer_get_csum_len(csummer);
	ctx->cc_bsgl = bsgl;
	ctx->cc_biov_csums = biov_csums;
	ctx->cc_to_verify = ctx->cc_to_verify_embedded;
	ctx->cc_to_verify_size = TO_VERIFY_EMBEDDED_NR;
}

static void cc_fini(struct csum_context *ctx)
{
	if (ctx->cc_to_verify != ctx->cc_to_verify_embedded)
		D_FREE(ctx->cc_to_verify);
}

static uint64_t
cc_2nr(const struct csum_context *ctx, uint64_t bytes)
{
	return bytes / ctx->cc_rec_len;
}

static uint64_t
cc_2nb(const struct csum_context *ctx, uint64_t records)
{
	return records * ctx->cc_rec_len;
}

static struct bio_iov *
cc2biov(const struct csum_context *ctx)
{
	return bio_sgl_iov(ctx->cc_bsgl, ctx->cc_bsgl_idx.iov_idx);
}

static struct to_verify *
cc2verify(const struct csum_context *ctx)
{
	return &ctx->cc_to_verify[ctx->cc_to_verify_nr];
}

static struct daos_csum_range
cc_get_cur_chunk_range_raw(const struct csum_context *ctx)
{
	return csum_recidx2range(
		cc_2nb(ctx, ctx->cc_rec_chunksize),
		ctx->cc_cur_recx_idx,
		ctx->cc_biov_ranges.br_raw.dcr_lo,
		ctx->cc_biov_ranges.br_raw.dcr_hi,
		ctx->cc_rec_len);
}

static struct daos_csum_range
cc_get_cur_chunk_range_req(const struct csum_context *ctx)
{
	return csum_recidx2range(
		cc_2nb(ctx, ctx->cc_rec_chunksize),
		ctx->cc_cur_recx_idx,
		ctx->cc_biov_ranges.br_req.dcr_lo,
		ctx->cc_biov_ranges.br_req.dcr_hi,
		ctx->cc_rec_len);
}

static void
cc_verify_incr(struct csum_context *ctx)
{
	ctx->cc_to_verify_nr++;
}

static uint64_t
recx2end(daos_recx_t *r)
{
	return r->rx_idx + r->rx_nr - 1;
}

static bool
cc_in_same_chunk(struct csum_context *ctx, daos_off_t a, daos_off_t b)
{
	return a / ctx->cc_rec_chunksize == b / ctx->cc_rec_chunksize;
}

static bool
cc_has_biov(struct csum_context *ctx)
{
	return cc2biov(ctx) != NULL;
}

static bool
cc_end_of_recx(struct csum_context *ctx)
{
	return ctx->cc_cur_recx_idx > recx2end(ctx->cc_cur_recx);
}

static bool
cc_end_of_chunk(const struct csum_context *ctx)
{
	return ctx->cc_cur_recx_idx % ctx->cc_rec_chunksize == 0;
}

static bool
cc_end_of_biov(const struct csum_context *ctx)
{
	return ctx->cc_bsgl_idx.iov_offset >= bio_iov2req_len(cc2biov(ctx));
}

static bool
cc_next_non_hole_extent_in_chunk(struct csum_context *ctx, daos_off_t idx)
{
	struct daos_csum_range	 next_range;
	struct bio_iov		*next_biov;
	uint32_t		 i;
	bool			 next_range_in_recx;

	i = 1;
	next_range = ctx->cc_biov_ranges.br_req;
	do {
		next_biov = bio_sgl_iov(ctx->cc_bsgl,
					ctx->cc_bsgl_idx.iov_idx + i);
		if (next_biov == NULL)
			break;
		i++;
		dcr_set_idx_nr(&next_range, next_range.dcr_hi + 1,
			       cc_2nr(ctx, bio_iov2req_len(next_biov)));
	} while (bio_addr_is_hole(&next_biov->bi_addr));

	next_range_in_recx =
		next_range.dcr_lo <= recx2end(ctx->cc_cur_recx);
	if (next_biov != NULL &&
	    cc_in_same_chunk(ctx, idx, next_range.dcr_lo) &&
	    next_range_in_recx)
		return true;

	return false;
}

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
		daos_csummer_set_buffer(csummer, csum, csum_len);
		daos_csummer_reset(csummer);
		verify = &to_verify[v];
		daos_csummer_update(csummer, verify->tv_buf, verify->tv_len);
		daos_csummer_finish(csummer);

		match = daos_csummer_csum_compare(csummer, csum,
						  verify->tv_csum, csum_len);
		if (!match) {
			D_ERROR("[%d] Original extent corrupted. "
				"Calculated ("DF_CI_BUF") != "
				"Stored ("DF_CI_BUF")\n",
				v, DP_CI_BUF(csum, csum_len),
				DP_CI_BUF(verify->tv_csum, csum_len));
			return -DER_CSUM;
		}
	}

	ctx->cc_to_verify_nr = 0;

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

static uint8_t *
cc2iodcsum(const struct csum_context *ctx)
{
	return ci_idx2csum(ctx->cc_cur_csum_info, ctx->cc_csum_idx);
}

static void
cc_iodcsum_incr(struct csum_context *ctx, uint32_t nr)
{
	ctx->cc_csum_idx += nr;
}

static uint8_t *
cc2biovcsum(const struct csum_context *ctx)
{
	struct dcs_csum_info *ci = dcs_csum_info_get(ctx->cc_biov_csums, ctx->cc_biov_csums_idx);

	D_ASSERT(ci);
	return ci_idx2csum(ci, ctx->cc_biov_csum_idx);
}

static void
cc_biovcsum_incr(struct csum_context *ctx, uint32_t nr)
{
	ctx->cc_biov_csum_idx += nr;
}

/**
 * determine if a new checksum is needed for the chunk given a record index.
 * A biov will have a prefix or suffix if the raw extent is partly covered by
 * another extent or if only part of an extent was requested. A prefix/suffix
 * means that a new checksum needs to be calculated for that chunk.
 */
static bool
cc_need_new_csum(struct csum_context *ctx, daos_off_t recx_idx)
{
	struct biov_ranges	*br = &ctx->cc_biov_ranges;
	uint64_t		 chunk_idx = recx_idx  / ctx->cc_rec_chunksize;

	bool result = false;

	/**
	 * - biov has a prefix
	 * - current recx idx is in same chunk as beginning of request
	 * - at least part of the prefix is in the current
	 */
	if (br->br_has_prefix &&
	    chunk_idx == br->br_req.dcr_lo / ctx->cc_rec_chunksize &&
	    chunk_idx == ((br->br_req.dcr_lo - 1) / ctx->cc_rec_chunksize))
		result = true;
	/**
	 * - biov has a suffix
	 * - current recx idx in the same chunk as the end of the request
	 * - at least part of the suffix is in the current chunk
	 */
	else if (br->br_has_suffix &&
	    chunk_idx == (br->br_req.dcr_hi / ctx->cc_rec_chunksize) &&
	    chunk_idx == ((br->br_req.dcr_hi + 1) / ctx->cc_rec_chunksize))
		result = true;

	/**
	 * has no prefix or suffix portion in the current extent, but
	 * there's another extent in the same chunk
	 */
	else if (cc_next_non_hole_extent_in_chunk(ctx, recx_idx))
		result = true;

	C_TRACE("br_has_prefix: %s, br_has_suffix: %s, recx_idx: %lu, chunk_idx: %lu, br_req: "
			DF_RANGE", br_raw: "DF_RANGE", result: %s\n",
		YES_NO(br->br_has_prefix), YES_NO(br->br_has_suffix), recx_idx, chunk_idx,
		DP_RANGE(br->br_req),
		DP_RANGE(br->br_raw),
		YES_NO(result)
		);

	return result;
}

/** copy the number of csums and then increment csum indexes */
static void
cc_copy_csum(struct csum_context *ctx, uint32_t csum_nr)
{
	ci_insert(ctx->cc_cur_csum_info, ctx->cc_csum_idx, cc2biovcsum(ctx),
		  csum_nr * ctx->cc_csum_len);
	cc_iodcsum_incr(ctx, csum_nr);
	cc_biovcsum_incr(ctx, csum_nr);
}

static int
cc_remember_to_verify(struct csum_context *ctx)
{
	struct to_verify	*ver;
	daos_off_t		 abs_idx;
	struct daos_csum_range	 cur_chunk_range;
	int			 rc;

	rc = cc_verify_resize_if_needed(ctx);
	if (rc != 0)
		return rc;

	ver = cc2verify(ctx);
	cc_verify_incr(ctx);

	cur_chunk_range = cc_get_cur_chunk_range_raw(ctx);

	ver->tv_len = cc_2nb(ctx, cur_chunk_range.dcr_nr);

	/** absolute biov index of the biov (not the extent based index */
	abs_idx = cur_chunk_range.dcr_lo - ctx->cc_biov_ranges.br_raw.dcr_lo;
	/** convert index to biov offset */
	ver->tv_buf = bio_iov2raw_buf(cc2biov(ctx)) + cc_2nb(ctx, abs_idx);

	D_ASSERT(cc2biovcsum(ctx) != NULL);
	ver->tv_csum = cc2biovcsum(ctx);
	cc_biovcsum_incr(ctx, 1);

	C_TRACE("Remember to Verify len: %lu\n", ver->tv_len);

	return 0;
}

/**
 * set the raw (or actual) and the selected (or requested) ranges
 * for the extent the current biov represents. These ranges are record based.
 */
static void
set_biov_ranges(struct csum_context *ctx, daos_off_t start_idx)
{
	if (!cc_has_biov(ctx))
		return;

	memset(&ctx->cc_biov_ranges, 0, sizeof(ctx->cc_biov_ranges));
	if (!cc_has_biov(ctx))
		return;

	dcr_set_idx_nr(&ctx->cc_biov_ranges.br_req, start_idx,
		       cc_2nr(ctx, bio_iov2req_len(cc2biov(ctx))));
	dcr_set_idx_nr(&ctx->cc_biov_ranges.br_raw,
		       start_idx - cc_2nr(ctx, cc2biov(ctx)->bi_prefix_len),
		       cc_2nr(ctx, bio_iov2raw_len(cc2biov(ctx))));
	ctx->cc_biov_ranges.br_has_prefix = cc2biov(ctx)->bi_prefix_len > 0;
	ctx->cc_biov_ranges.br_has_suffix = cc2biov(ctx)->bi_suffix_len > 0;
}

static void
cc_biov_move_next(struct csum_context *ctx, bool biov_csum_used)
{
	/** move to the next biov */
	ctx->cc_bsgl_idx.iov_idx++;
	ctx->cc_bsgl_idx.iov_offset = 0;

	/** Need to know if biov csum was used. For holes there is no csum, but
	 * still need to move to next biov
	 */
	if (biov_csum_used) {
		ctx->cc_biov_csum_idx = 0;
		ctx->cc_biov_csums_idx++;
	}

	C_TRACE("Moving to biov %d, biov_csum_used: %s, csums_idx: %lu\n", ctx->cc_bsgl_idx.iov_idx,
		biov_csum_used ? "YES" : "NO", ctx->cc_biov_csums_idx);

	set_biov_ranges(ctx, ctx->cc_cur_recx_idx);
}

static void
cc_move_forward(struct csum_context *ctx, uint64_t nr, bool biov_csum_used)
{
	/** Move recx index forward */
	ctx->cc_cur_recx_idx += nr;
	/** move bsgl forward */
	ctx->cc_bsgl_idx.iov_offset += cc_2nb(ctx, nr);

	if (cc_end_of_biov(ctx))
		cc_biov_move_next(ctx, biov_csum_used);
}

/* Count the number of checksums to copy or skip for a hole */
#define num_csums_to_biov_end(ctx) \
	((ctx->cc_biov_ranges.br_req.dcr_hi / ctx->cc_rec_chunksize) - \
	(ctx->cc_cur_recx_idx / ctx->cc_rec_chunksize) + 1)

#define nr_to_biov_end(ctx) (ctx->cc_biov_ranges.br_req.dcr_hi - ctx->cc_cur_recx_idx + 1)
#define cur_chunk_idx(ctx) (ctx->cc_cur_recx_idx / ctx->cc_rec_chunksize)
#define first_chunk_idx(ctx) (ctx->cc_cur_recx->rx_idx / ctx->cc_rec_chunksize)

static void
cc_skip_hole(struct csum_context *ctx)
{
	daos_size_t csum_nr = num_csums_to_biov_end(ctx);
	daos_size_t nr = nr_to_biov_end(ctx);

	if (ctx->cc_csum_idx >  cur_chunk_idx(ctx) - first_chunk_idx(ctx))
		csum_nr--; /* Already passed the current chunk */

	if (cc_need_new_csum(ctx, ctx->cc_biov_ranges.br_req.dcr_hi))
		csum_nr--;

	C_TRACE("Skipping hole ["DF_X64"-"DF_X64"]. %lu csums and %lu records, csum_idx %lu->%lu\n",
		ctx->cc_biov_ranges.br_req.dcr_lo,
		ctx->cc_biov_ranges.br_req.dcr_hi, csum_nr, nr,
		ctx->cc_csum_idx, ctx->cc_csum_idx + csum_nr);
	cc_iodcsum_incr(ctx, csum_nr);
	cc_move_forward(ctx, nr, false);
}

/**
 * Create a new checksum for the current chunk (defined by current index)
 * Can consume multiple biovs until the end of the chunk is reached, or end of
 * bsgl is reached.
 */
static int
cc_create(struct csum_context *ctx)
{
	struct daos_csum_range	 range;
	uint8_t			*csum;
	void			*buf;
	daos_size_t		 bytes;
	int			 rc;

	csum = cc2iodcsum(ctx);
	D_ASSERT(csum != NULL);
	C_TRACE("Creating new checksum for csum_idx: %lu\n", ctx->cc_csum_idx);
	cc_iodcsum_incr(ctx, 1);

	C_TRACE("(CALC) Starting new checksum for recx idx: "DF_X64", recx: "DF_RECX"\n",
		ctx->cc_cur_recx_idx, DP_RECX(*ctx->cc_cur_recx));
	/** Setup csum to start updating */
	memset(csum, 0, ctx->cc_csum_len);
	daos_csummer_set_buffer(ctx->cc_csummer, csum, ctx->cc_csum_len);
	daos_csummer_reset(ctx->cc_csummer);

	do {
		range = cc_get_cur_chunk_range_req(ctx);
		bytes = cc_2nb(ctx, range.dcr_nr);
		buf = bio_iov2req_buf(cc2biov(ctx)) +
		      ctx->cc_bsgl_idx.iov_offset;
		if (bio_addr_is_hole(&cc2biov(ctx)->bi_addr)) {
			/** will skip until end of biov or end of chunk because
			 * that's what the range is
			 */
			cc_move_forward(ctx, range.dcr_nr, false);
			continue;
		}
		rc = daos_csummer_update(ctx->cc_csummer, buf, bytes);
		if (rc != 0)
			return rc;
		cc_remember_to_verify(ctx);
		cc_move_forward(ctx, range.dcr_nr, true);
	} while (cc_has_biov(ctx) &&
		!cc_end_of_recx(ctx) &&
		!cc_end_of_chunk(ctx));
	daos_csummer_finish(ctx->cc_csummer);

	rc = cc_verify_orig_extents(ctx);

	return rc;
}

/**
 * Copies the checksums from the biov checksums (gathered during
 * vos_fetch_begin). Will attempt to copy all checksums from biov unless the
 * last checksum is in a chunk that will need a new checksum
 */
static int
cc_copy(struct csum_context *ctx)
{
	daos_size_t csum_nr = num_csums_to_biov_end(ctx);
	daos_size_t nr = nr_to_biov_end(ctx);

	/**
	 * If the last chunk in the biov is in need of a new csum then remove it. It
	 * will be created on the next pass of the \cc_add_csums_for_recx
	 */
	if (cc_need_new_csum(ctx, ctx->cc_biov_ranges.br_req.dcr_hi)) {
		csum_nr--;
		nr -= (ctx->cc_biov_ranges.br_req.dcr_hi + 1) % ctx->cc_rec_chunksize;
	}

	if (csum_nr == 0)
		return 0;

	C_TRACE("Copying %lu csums for %lu records ["DF_X64"-"DF_X64"]\n", csum_nr, nr,
		ctx->cc_cur_recx_idx, ctx->cc_cur_recx_idx + nr - 1);
	cc_copy_csum(ctx, csum_nr);
	cc_move_forward(ctx, nr, true);

	return 0;
}

/** For a given recx, add checksums to the output csum info. Data will come
 * from the bsgls in \ctx
 */
static int
cc_add_csums_for_recx(struct csum_context *ctx, daos_recx_t *recx,
		      struct dcs_csum_info *info)
{
	int rc = 0;

	/** setup for this recx/csum_info */
	ctx->cc_cur_recx_idx = recx->rx_idx;
	ctx->cc_cur_csum_info = info;
	ctx->cc_cur_recx = recx;
	ctx->cc_csum_idx = 0;
	set_biov_ranges(ctx, recx->rx_idx);
	C_TRACE("recx: "DF_RECX"\n", DP_RECX(*recx));

	while (cc_has_biov(ctx) && !cc_end_of_recx(ctx)) {
		if (bio_addr_is_hole(&cc2biov(ctx)->bi_addr))
			cc_skip_hole(ctx);
		else if (cc_need_new_csum(ctx, ctx->cc_cur_recx_idx))
			rc = cc_create(ctx);
		else
			rc = cc_copy(ctx);

		if (rc != 0)
			return rc;
	}

	C_TRACE("rc: "DF_RC"\n", DP_RC(rc));
	return rc;
}

static int
ds_csum_add2iod_array(daos_iod_t *iod, struct daos_csummer *csummer, struct bio_sglist *bsgl,
		      struct dcs_ci_list *biov_csums, size_t *biov_csums_used,
		      struct dcs_iod_csums *iod_csums)
{
	struct csum_context	ctx = {0};
	uint32_t		i, j;
	int			rc = 0;

	if (biov_csums_used != NULL)
		*biov_csums_used = 0;

	/** Verify have correct csums for extents returned.
	 * Should be 1 biov_csums for each non-hole biov in bsgl
	 */
	for (i = 0, j = 0; i < bsgl->bs_nr_out; i++) {
		if (bio_addr_is_hole(&(bio_sgl_iov(bsgl, i)->bi_addr))) {
			C_TRACE("biov is a hole. skipping "DF_U64" bytes\n",
				bio_sgl_iov(bsgl, i)->bi_data_len);
			continue;
		}
		if (!ci_is_valid(dcs_csum_info_get(biov_csums, j++))) {
			D_ERROR("Invalid csum for biov %d.\n", i);
			return -DER_CSUM;
		}
	}

	/** setup the context */
	cc_init(&ctx, csummer, bsgl, biov_csums, iod->iod_size);

	iod_csums->ic_nr = iod->iod_nr;

	/** for each extent/checksum buf */
	for (i = 0; i < iod->iod_nr && rc == 0; i++) {
		daos_recx_t		*recx = &iod->iod_recxs[i];
		struct dcs_csum_info	*info = &iod_csums->ic_data[i];

		if (ctx.cc_rec_len > 0 && ci_is_valid(info)) {
			C_TRACE("Adding csums for recx %d: "DF_RECX"\n", i, DP_RECX(*recx));
			rc = cc_add_csums_for_recx(&ctx, recx, info);
			if (rc != 0)
				D_ERROR("Failed to add csum for recx"DF_RECX": %d\n",
					DP_RECX(*recx), rc);
		}
	}

	cc_fini(&ctx);

	/** return the count of biov csums used. */
	if (biov_csums_used != NULL)
		/**
		 * ctx.cc_biov_csums_idx will have the number of
		 * csums processed
		 */
		*biov_csums_used = ctx.cc_biov_csums_idx;
	return rc;
}

int
ds_csum_add2iod(daos_iod_t *iod, struct daos_csummer *csummer, struct bio_sglist *bsgl,
		struct dcs_ci_list *biov_csums, size_t *biov_csums_used,
		struct dcs_iod_csums *iod_csums)
{
	if (biov_csums_used != NULL)
		*biov_csums_used = 0;

	if (!(daos_csummer_initialized(csummer) && bsgl))
		return 0;

	if (!csum_iod_is_supported(iod))
		return 0;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		struct dcs_csum_info *ci = dcs_csum_info_get(biov_csums, 0);

		C_TRACE("Adding fetched to IOD: "DF_C_IOD", csum: "DF_CI"\n",
			DP_C_IOD(iod), DP_CI(*ci));
		ci_insert(&iod_csums->ic_data[0], 0, ci->cs_csum, ci->cs_len);
		if (biov_csums_used != NULL)
			(*biov_csums_used) = 1;
		return 0;
	}

	return ds_csum_add2iod_array(iod, csummer, bsgl, biov_csums,
				     biov_csums_used, iod_csums);
}

int
ds_csum_verify_keys(struct daos_csummer *csummer, daos_key_t *dkey,
		    struct dcs_csum_info *dkey_csum,
		    daos_iod_t *iods, struct dcs_iod_csums *iod_csums, uint32_t iod_nr,
		    daos_unit_oid_t *uoid)
{
	uint32_t	i;
	int		rc;

	if (!daos_csummer_initialized(csummer) || csummer->dcs_skip_key_verify)
		return 0;

	if (!DAOS_FAIL_CHECK(DAOS_VC_DIFF_DKEY)) {
		/**
		 * with DAOS_VC_DIFF_DKEY, the dkey will be corrupt on purpose
		 * for object verification tests. Don't reject the
		 * update in this case
		 */
		rc = daos_csummer_verify_key(csummer, dkey, dkey_csum);
		if (rc != 0) {
			D_ERROR("daos_csummer_verify_key error for dkey: "
				DF_RC"\n", DP_RC(rc));
			return rc;
		}
	}

	for (i = 0; i < iod_nr; i++) {
		daos_iod_t		*iod = &iods[i];
		struct dcs_iod_csums	*csum = &iod_csums[i];

		if (!csum_iod_is_supported(iod))
			continue;

		D_DEBUG(DB_CSUM, DF_C_UOID_DKEY"iod[%d]: "DF_C_IOD", csum_nr: %d\n",
			DP_C_UOID_DKEY(*uoid, dkey), i, DP_C_IOD(iod), csum->ic_nr);

		if (csum->ic_nr > 0)
			D_DEBUG(DB_CSUM, "first data csum: "DF_CI"\n", DP_CI(*csum->ic_data));

		rc = daos_csummer_verify_key(csummer,
					     &iod->iod_name,
					     &csum->ic_akey);
		if (rc != 0) {
			D_ERROR(DF_C_UOID_DKEY"iod[%d]: "DF_C_IOD" verify_key "
					       "failed for akey: "DF_KEY", csum: "DF_CI", "
					       "error: "DF_RC"\n",
				DP_C_UOID_DKEY(*uoid, dkey), i,
				DP_C_IOD(iod), DP_KEY(&iod->iod_name),
				DP_CI(csum->ic_akey), DP_RC(rc));
			return rc;
		}
	}

	return 0;
}
