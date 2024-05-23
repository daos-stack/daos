/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(csum)

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <lz4.h>
#include <isa-l.h>
#include <gurt/types.h>
#include <daos/common.h>
#include <daos/compression.h>

/**
 * ---------------------------------------------------------------------------
 * Algorithms
 * ---------------------------------------------------------------------------
 */

/** LZ4 */
static int
lz4_init(void **daos_dc_ctx,
	 uint16_t level,
	 uint32_t max_buf_size)
{
	return DC_STATUS_OK;
}

static int
lz4_compress(void *daos_dc_ctx,
	     uint8_t *src, size_t src_len,
	     uint8_t *dst, size_t dst_len,
	     size_t *produced)
{
	int rc = DC_STATUS_ERR;
	int len = 0;

	len = LZ4_compress_default((const char *)src, (char *)dst,
				   (int)src_len, (int)dst_len);
	if (len > 0) {
		*produced = len;
		rc = DC_STATUS_OK;
	}

	return rc;
}

static int
lz4_decompress(void *daos_dc_ctx,
	       uint8_t *src, size_t src_len,
	       uint8_t *dst, size_t dst_len,
	       size_t *produced)
{
	int ret = LZ4_decompress_safe((const char *)src, (char *)dst,
				      (int)src_len, (int)dst_len);

	if (ret <= 0)
		return DC_STATUS_ERR;

	*produced = ret;

	return DC_STATUS_OK;
}

void
lz4_destroy(void *daos_dc_ctx)
{
	return;
}

struct compress_ft lz4_algo = {
	.cf_init = lz4_init,
	.cf_compress = lz4_compress,
	.cf_decompress = lz4_decompress,
	.cf_destroy = lz4_destroy,
	.cf_level = 1,
	.cf_name = "lz4",
	.cf_type = COMPRESS_TYPE_LZ4
};

/** Deflate */
struct deflate_ctx {
	struct isal_zstream stream;
	struct inflate_state state;
};

static int
deflate_init(void **daos_dc_ctx,
	     uint16_t level,
	     uint32_t max_buf_size)
{
	int level_size_buf[] = {
		ISAL_DEF_LVL0_DEFAULT,
		ISAL_DEF_LVL1_DEFAULT,
		ISAL_DEF_LVL2_DEFAULT,
		ISAL_DEF_LVL3_DEFAULT,
	};
	uint16_t isal_level = level - 1;

	if (isal_level >= sizeof(level_size_buf) / sizeof(int)) {
		D_ERROR("Invalid isa-l compression level: %d\n", level);
		return DC_STATUS_INVALID_LEVEL;
	}

	struct deflate_ctx *ctx;

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return DC_STATUS_NOMEM;

	isal_deflate_stateless_init(&ctx->stream);
	ctx->stream.level = isal_level;
	ctx->stream.level_buf = malloc(level_size_buf[isal_level]);
	ctx->stream.level_buf_size = level_size_buf[isal_level];
	ctx->stream.flush = FULL_FLUSH;
	ctx->stream.end_of_stream = 1;

	isal_deflate_set_hufftables(&ctx->stream, NULL, IGZIP_HUFFTABLE_STATIC);

	isal_inflate_init(&ctx->state);

	*daos_dc_ctx = ctx;

	return DC_STATUS_OK;
}

static int
deflate_compress(void *daos_dc_ctx, uint8_t *src, size_t src_len,
		 uint8_t *dst, size_t dst_len, size_t *produced)
{
	int ret = 0;
	struct deflate_ctx *ctx = daos_dc_ctx;

	isal_deflate_reset(&ctx->stream);

	ctx->stream.next_in = src;
	ctx->stream.avail_in = src_len;
	ctx->stream.next_out = dst;
	ctx->stream.avail_out = dst_len;

	ret = isal_deflate_stateless(&ctx->stream);

	if (ret == COMP_OK) {
		*produced = ctx->stream.total_out;
		return DC_STATUS_OK;
	}

	if (ret == STATELESS_OVERFLOW)
		return DC_STATUS_OVERFLOW;

	return DC_STATUS_ERR;
}

static int
deflate_decompress(void *daos_dc_ctx, uint8_t *src, size_t src_len,
		   uint8_t *dst, size_t dst_len, size_t *produced)
{
	int ret = 0;
	struct deflate_ctx *ctx = daos_dc_ctx;

	isal_inflate_reset(&ctx->state);

	ctx->state.next_in = src;
	ctx->state.avail_in = src_len;
	ctx->state.next_out = dst;
	ctx->state.avail_out = dst_len;

	ret = isal_inflate(&ctx->state);

	if (ret == ISAL_DECOMP_OK) {
		*produced = ctx->state.total_out;
		return DC_STATUS_OK;
	}

	if (ret == ISAL_OUT_OVERFLOW)
		return DC_STATUS_OVERFLOW;

	return DC_STATUS_ERR;
}

static void
deflate_destroy(void *daos_dc_ctx)
{
	struct deflate_ctx *ctx = daos_dc_ctx;

	if (ctx == NULL)
		return;

	D_FREE(ctx->stream.level_buf);
	D_FREE(daos_dc_ctx);
}

static int
is_available()
{
	/** ISA-L is always available */
	return 1;
}

struct compress_ft deflate_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 1,
	.cf_name = "deflate",
	.cf_type = COMPRESS_TYPE_DEFLATE
};

struct compress_ft deflate1_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 1,
	.cf_name = "deflate1",
	.cf_type = COMPRESS_TYPE_DEFLATE1
};

struct compress_ft deflate2_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 2,
	.cf_name = "deflate2",
	.cf_type = COMPRESS_TYPE_DEFLATE2
};

struct compress_ft deflate3_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 3,
	.cf_name = "deflate3",
	.cf_type = COMPRESS_TYPE_DEFLATE3
};

struct compress_ft deflate4_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 4,
	.cf_name = "deflate4",
	.cf_type = COMPRESS_TYPE_DEFLATE4
};

/** Index to algo table should align with enum DAOS_COMPRESS_TYPE - 1 */
struct compress_ft *isal_compress_algo_table[] = {
	&lz4_algo,
	&deflate_algo,
	&deflate1_algo,
	&deflate2_algo,
	&deflate3_algo,
	&deflate4_algo,
};
