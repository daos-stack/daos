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

#include <gurt/types.h>
#include <daos/common.h>
#include <daos/compression.h>

#ifdef HAVE_QAT
#include <qat.h>
/**
 * ---------------------------------------------------------------------------
 * Algorithms
 * ---------------------------------------------------------------------------
 */

/** Deflate */
struct deflate_ctx {
	CpaInstanceHandle	dc_inst_hdl;
	CpaDcSessionHandle	session_hdl;

	CpaBufferList		**inter_bufs;
	Cpa16U			num_inter_bufs;
};

static int
deflate_init(void **daos_dc_ctx, uint16_t level,
	     uint32_t max_buf_size)
{
	int rc;
	struct deflate_ctx *ctx;

	CpaDcCompLvl qat_dc_level[] = {
		CPA_DC_L1,
		CPA_DC_L2,
		CPA_DC_L3,
		CPA_DC_L4,
	};

	uint16_t dc_level_index = level - 1;

	if (dc_level_index >= ARRAY_SIZE(qat_dc_level)) {
		D_ERROR("Invalid qat compression level: %d\n", level);
		return DC_STATUS_INVALID_LEVEL;
	}

	D_ALLOC(ctx, sizeof(*ctx));
	if (ctx == NULL)
		return DC_STATUS_NOMEM;

	rc = qat_dc_init(
		&ctx->dc_inst_hdl, &ctx->session_hdl,
		&ctx->num_inter_bufs, &ctx->inter_bufs,
		max_buf_size,
		qat_dc_level[dc_level_index]);

	*daos_dc_ctx = ctx;
	return rc;
}

static int
deflate_compress(void *daos_dc_ctx, uint8_t *src, size_t src_len,
		 uint8_t *dst, size_t dst_len, size_t *produced)
{
	struct deflate_ctx *ctx = daos_dc_ctx;

	return qat_dc_compress(
		&ctx->dc_inst_hdl,
		&ctx->session_hdl,
		src, src_len,
		dst, dst_len, produced, DIR_COMPRESS);
}

static int
deflate_decompress(void *daos_dc_ctx, uint8_t *src, size_t src_len,
		   uint8_t *dst, size_t dst_len, size_t *produced)
{
	struct deflate_ctx *ctx = daos_dc_ctx;

	return qat_dc_compress(
		&ctx->dc_inst_hdl,
		&ctx->session_hdl,
		src, src_len,
		dst, dst_len, produced, DIR_DECOMPRESS);
}

static int
deflate_compress_async(void *daos_dc_ctx, uint8_t *src, size_t src_len,
		       uint8_t *dst, size_t dst_len,
		       dc_callback_fn cb_fn, void *cb_data)
{
	struct deflate_ctx *ctx = daos_dc_ctx;

	return qat_dc_compress_async(
		&ctx->dc_inst_hdl,
		&ctx->session_hdl,
		src, src_len,
		dst, dst_len, DIR_COMPRESS,
		cb_fn, cb_data);
}

static int
deflate_decompress_async(void *daos_dc_ctx, uint8_t *src, size_t src_len,
			 uint8_t *dst, size_t dst_len,
			 dc_callback_fn cb_fn, void *cb_data)
{
	struct deflate_ctx *ctx = daos_dc_ctx;

	return qat_dc_compress_async(
		&ctx->dc_inst_hdl,
		&ctx->session_hdl,
		src, src_len,
		dst, dst_len, DIR_DECOMPRESS,
		cb_fn, cb_data);
}

static int
deflate_poll_response(void *daos_dc_ctx)
{
	struct deflate_ctx *ctx = daos_dc_ctx;

	return qat_dc_poll_response(&ctx->dc_inst_hdl);
}

static void
deflate_destroy(void *daos_dc_ctx)
{
	struct deflate_ctx *ctx = daos_dc_ctx;

	qat_dc_destroy(
		&ctx->dc_inst_hdl,
		&ctx->session_hdl,
		ctx->inter_bufs,
		ctx->num_inter_bufs);

	D_FREE(daos_dc_ctx);
}

static int
is_available()
{
	return qat_dc_is_available();
}

struct compress_ft qat_deflate_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_compress_async = deflate_compress_async,
	.cf_decompress_async = deflate_decompress_async,
	.cf_poll_response = deflate_poll_response,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 1,
	.cf_name = "deflate",
	.cf_type = COMPRESS_TYPE_DEFLATE
};

struct compress_ft qat_deflate1_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_compress_async = deflate_compress_async,
	.cf_decompress_async = deflate_decompress_async,
	.cf_poll_response = deflate_poll_response,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 1,
	.cf_name = "deflate1",
	.cf_type = COMPRESS_TYPE_DEFLATE1
};

struct compress_ft qat_deflate2_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_compress_async = deflate_compress_async,
	.cf_decompress_async = deflate_decompress_async,
	.cf_poll_response = deflate_poll_response,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 2,
	.cf_name = "deflate2",
	.cf_type = COMPRESS_TYPE_DEFLATE2
};

struct compress_ft qat_deflate3_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_compress_async = deflate_compress_async,
	.cf_decompress_async = deflate_decompress_async,
	.cf_poll_response = deflate_poll_response,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 3,
	.cf_name = "deflate3",
	.cf_type = COMPRESS_TYPE_DEFLATE3
};

struct compress_ft qat_deflate4_algo = {
	.cf_init = deflate_init,
	.cf_compress = deflate_compress,
	.cf_decompress = deflate_decompress,
	.cf_compress_async = deflate_compress_async,
	.cf_decompress_async = deflate_decompress_async,
	.cf_poll_response = deflate_poll_response,
	.cf_destroy = deflate_destroy,
	.cf_available = is_available,
	.cf_level = 4,
	.cf_name = "deflate4",
	.cf_type = COMPRESS_TYPE_DEFLATE4
};

/** Index to algo table should align with enum DAOS_COMPRESS_TYPE - 1 */
struct compress_ft *qat_compress_algo_table[] = {
	NULL, /** LZ4 is not supported by QAT for now */
	&qat_deflate_algo,
	&qat_deflate1_algo,
	&qat_deflate2_algo,
	&qat_deflate3_algo,
	&qat_deflate4_algo,
};

#else
struct compress_ft *qat_compress_algo_table[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

#endif
