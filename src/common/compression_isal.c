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
lz4_init(void **daos_dc_ctx, uint16_t level)
{
	return 0;
}

static int
lz4_compress(void *daos_dc_ctx,
	     uint8_t *src, size_t src_len,
	     uint8_t *dst, size_t dst_len)
{
	return LZ4_compress_default((const char *)src, (char *)dst,
		(int)src_len, (int)dst_len);
}

static int
lz4_decompress(void *daos_dc_ctx,
	       uint8_t *src, size_t src_len,
	       uint8_t *dst, size_t dst_len)
{
	int ret = LZ4_decompress_safe((const char *)src, (char *)dst,
		(int)src_len, (int)dst_len);

	if (ret <= 0)
		return 0;

	return ret;
}

void
lz4_destroy(void *daos_dc_ctx)
{
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
deflate_init(void **daos_dc_ctx, uint16_t level)
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
		return -1;
	}

	struct deflate_ctx *ctx;

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	isal_deflate_stateless_init(&ctx->stream);
	ctx->stream.level = isal_level;
	ctx->stream.level_buf = malloc(level_size_buf[isal_level]);
	ctx->stream.level_buf_size = level_size_buf[isal_level];
	ctx->stream.flush = FULL_FLUSH;
	ctx->stream.end_of_stream = 1;

	isal_deflate_set_hufftables(&ctx->stream, NULL, IGZIP_HUFFTABLE_STATIC);

	isal_inflate_init(&ctx->state);

	*daos_dc_ctx = ctx;

	return 0;
}

static int
deflate_compress(void *daos_dc_ctx, uint8_t *src, size_t src_len,
		 uint8_t *dst, size_t dst_len)
{
	int ret = 0;
	struct deflate_ctx *ctx = daos_dc_ctx;

	ctx->stream.total_in = 0;
	ctx->stream.total_out = 0;
	ctx->stream.avail_in = src_len;
	ctx->stream.next_in = src;
	ctx->stream.avail_out = dst_len;
	ctx->stream.next_out = dst;

	ret = isal_deflate_stateless(&ctx->stream);

	/* Check if input buffer are all consumed */
	if (ctx->stream.avail_in)
		return 0;

	if (ret != COMP_OK)
		return 0;

	return ctx->stream.total_out;
}

static int
deflate_decompress(void *daos_dc_ctx, uint8_t *src, size_t src_len,
		   uint8_t *dst, size_t dst_len)
{
	int ret = 0;
	struct deflate_ctx *ctx = daos_dc_ctx;

	ctx->stream.total_in = 0;
	ctx->stream.total_out = 0;
	ctx->state.next_in = src;
	ctx->state.avail_in = src_len;
	ctx->state.next_out = dst;
	ctx->state.avail_out = dst_len;

	ret = isal_inflate(&ctx->state);

	/* Check if input buffer are all consumed */
	if (ctx->state.avail_in)
		return -1;

	if (ret != ISAL_DECOMP_OK)
		return ret;

	return ctx->state.total_out;
}

void
deflate_destroy(void *daos_dc_ctx)
{
	D_FREE(daos_dc_ctx);
}

struct compress_ft deflate_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_level = 1,
	.cf_name = "deflate",
	.cf_type = COMPRESS_TYPE_DEFLATE
};

struct compress_ft deflate1_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_level = 1,
	.cf_name = "deflate1",
	.cf_type = COMPRESS_TYPE_DEFLATE1
};

struct compress_ft deflate2_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_level = 2,
	.cf_name = "deflate2",
	.cf_type = COMPRESS_TYPE_DEFLATE2
};

struct compress_ft deflate3_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
	.cf_level = 3,
	.cf_name = "deflate3",
	.cf_type = COMPRESS_TYPE_DEFLATE3
};

struct compress_ft deflate4_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_destroy = deflate_destroy,
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
