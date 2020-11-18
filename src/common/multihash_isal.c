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

#include <isa-l.h>
#include <isa-l_crypto.h>
#include <gurt/types.h>
#include <daos/common.h>
#include <daos/multihash.h>

/**
 * ---------------------------------------------------------------------------
 * ISA-L Hash Algorithms
 * ---------------------------------------------------------------------------
 */

/** CRC16_T10DIF*/
static int
crc16_init(void **daos_mhash_ctx)
{
	D_ALLOC(*daos_mhash_ctx, sizeof(uint16_t));
	if (*daos_mhash_ctx == NULL)
		return -DER_NOMEM;

	return 0;
}

static int
crc16_reset(void *daos_mhash_ctx)
{
	uint16_t *crc16 = daos_mhash_ctx;

	*crc16 = 0;

	return 0;
}

static void
crc16_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
crc16_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint16_t *crc16 = (uint16_t *)daos_mhash_ctx;

	*crc16 = crc16_t10dif(*crc16, buf, (int)buf_len);
	return 0;
}

static int
crc16_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint16_t *crc16 = (uint16_t *)daos_mhash_ctx;

	*((uint16_t *)buf) = *crc16;

	return 0;
}

struct hash_ft crc16_algo = {
	.cf_update	= crc16_update,
	.cf_init	= crc16_init,
	.cf_reset	= crc16_reset,
	.cf_destroy	= crc16_destroy,
	.cf_finish	= crc16_finish,
	.cf_hash_len	= sizeof(uint16_t),
	.cf_name	= "crc16",
	.cf_type	= HASH_TYPE_CRC16
};

/** CRC32_ISCSI */
static int
crc32_init(void **daos_mhash_ctx)
{
	D_ALLOC(*daos_mhash_ctx, sizeof(uint32_t));
	if (*daos_mhash_ctx == NULL)
		return -DER_NOMEM;

	return 0;
}

static int
crc32_reset(void *daos_mhash_ctx)
{
	uint32_t *crc32 = daos_mhash_ctx;

	*crc32 = 0;

	return 0;
}

static void
crc32_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
crc32_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint32_t *crc32 = (uint32_t *)daos_mhash_ctx;

	*crc32 = crc32_iscsi(buf, (int) buf_len, *crc32);
	return 0;
}

static int
crc32_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint32_t *crc32 = (uint32_t *)daos_mhash_ctx;

	*((uint32_t *)buf) = *crc32;

	return 0;
}

struct hash_ft crc32_algo = {
	.cf_update	= crc32_update,
	.cf_init	= crc32_init,
	.cf_reset	= crc32_reset,
	.cf_destroy	= crc32_destroy,
	.cf_finish	= crc32_finish,
	.cf_hash_len	= sizeof(uint32_t),
	.cf_name	= "crc32",
	.cf_type	= HASH_TYPE_CRC32
};

/** ADLER32 */
static int
adler32_init(void **daos_mhash_ctx)
{
	D_ALLOC(*daos_mhash_ctx, sizeof(uint32_t));
	if (*daos_mhash_ctx == NULL)
		return -DER_NOMEM;

	return 0;
}

static int
adler32_reset(void *daos_mhash_ctx)
{
	memset(daos_mhash_ctx, 0, sizeof(uint32_t));
	return 0;
}

static void
adler32_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
adler32_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint32_t *hash = (uint32_t *)daos_mhash_ctx;

	*hash = isal_adler32(*hash, buf, buf_len);
	return 0;
}

static int
adler32_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint32_t *adler32 = (uint32_t *)daos_mhash_ctx;

	*((uint32_t *)buf) = *adler32;

	return 0;
}

struct hash_ft adler32_algo = {
	.cf_update	= adler32_update,
	.cf_init	= adler32_init,
	.cf_reset	= adler32_reset,
	.cf_destroy	= adler32_destroy,
	.cf_finish	= adler32_finish,
	.cf_hash_len	= sizeof(uint32_t),
	.cf_name	= "adler32",
	.cf_type	= HASH_TYPE_ADLER32
};

/** CRC64_REFL */
static int
crc64_init(void **daos_mhash_ctx)
{
	D_ALLOC(*daos_mhash_ctx, sizeof(uint64_t));
	if (*daos_mhash_ctx == NULL)
		return -DER_NOMEM;

	return 0;
}

static int
crc64_reset(void *daos_mhash_ctx)
{
	uint64_t *crc64 = daos_mhash_ctx;

	*crc64 = 0;

	return 0;
}

static void
crc64_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
crc64_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint64_t *hash = (uint64_t *)daos_mhash_ctx;

	*hash = crc64_ecma_refl(*hash, buf, buf_len);
	return 0;
}

static int
crc64_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	uint64_t *crc64 = (uint64_t *)daos_mhash_ctx;

	*((uint64_t *)buf) = *crc64;

	return 0;
}

struct hash_ft crc64_algo = {
	.cf_update	= crc64_update,
	.cf_init	= crc64_init,
	.cf_reset	= crc64_reset,
	.cf_destroy	= crc64_destroy,
	.cf_finish	= crc64_finish,
	.cf_hash_len	= sizeof(uint64_t),
	.cf_name	= "crc64",
	.cf_type	= HASH_TYPE_CRC64
};

/** SHA1 */
struct sha1_ctx {
	struct mh_sha1_ctx	s1_ctx;
	bool			s1_updated;
};

static int
sha1_init(void **daos_mhash_ctx)
{
	struct sha1_ctx		*ctx;
	int			 rc;

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	rc = mh_sha1_init(&ctx->s1_ctx);
	if (rc == 0)
		*daos_mhash_ctx = ctx;
	return rc;
}

static int
sha1_reset(void *daos_mhash_ctx)
{
	struct sha1_ctx *ctx = daos_mhash_ctx;

	ctx->s1_updated = false;
	return mh_sha1_init(&ctx->s1_ctx);
}

static void
sha1_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
sha1_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	struct sha1_ctx *ctx = daos_mhash_ctx;

	ctx->s1_updated = true;
	return mh_sha1_update(&ctx->s1_ctx, buf, buf_len);
}

static int
sha1_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	struct sha1_ctx *ctx = daos_mhash_ctx;

	if (ctx->s1_updated)
		return mh_sha1_finalize(&ctx->s1_ctx, buf);
	return 0;
}

struct hash_ft sha1_algo = {
	.cf_update	= sha1_update,
	.cf_init	= sha1_init,
	.cf_reset	= sha1_reset,
	.cf_destroy	= sha1_destroy,
	.cf_finish	= sha1_finish,
	.cf_hash_len	= 20,
	.cf_name	= "sha1",
	.cf_type	= HASH_TYPE_SHA1
};

/** SHA256 */
struct sha256_ctx {
	struct mh_sha256_ctx	s2_ctx;
	bool			s2_updated;
};

static int
sha256_init(void **daos_mhash_ctx)
{
	struct sha256_ctx	*ctx;
	int			 rc;

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	rc = mh_sha256_init(&ctx->s2_ctx);
	if (rc == 0)
		*daos_mhash_ctx = ctx;
	return rc;
}

static int
sha256_reset(void *daos_mhash_ctx)
{
	struct sha256_ctx *ctx = daos_mhash_ctx;

	ctx->s2_updated = false;
	return mh_sha256_init(&ctx->s2_ctx);
}

static void
sha256_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
sha256_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	struct sha256_ctx *ctx = daos_mhash_ctx;

	ctx->s2_updated = true;
	return mh_sha256_update(&ctx->s2_ctx, buf, buf_len);
}

static int
sha256_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	struct sha256_ctx *ctx = daos_mhash_ctx;

	if (ctx->s2_updated)
		return mh_sha256_finalize(&ctx->s2_ctx, buf);
	return 0;
}

struct hash_ft sha256_algo = {
	.cf_update	= sha256_update,
	.cf_init	= sha256_init,
	.cf_reset	= sha256_reset,
	.cf_destroy	= sha256_destroy,
	.cf_finish	= sha256_finish,
	.cf_hash_len	= 256 / 8,
	.cf_name	= "sha256",
	.cf_type	= HASH_TYPE_SHA256
};

/** SHA512 */
struct sha512_ctx {
	SHA512_HASH_CTX_MGR	s5_mgr;
	SHA512_HASH_CTX		s5_ctx;
	bool			s5_updated;
};

static int
sha512_init(void **daos_mhash_ctx)
{
	struct sha512_ctx	*ctx;

	D_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -DER_NOMEM;

	sha512_ctx_mgr_init(&ctx->s5_mgr);
	hash_ctx_init(&ctx->s5_ctx);

	*daos_mhash_ctx = ctx;
	return 0;
}

static void
sha512_destroy(void *daos_mhash_ctx)
{
	D_FREE(daos_mhash_ctx);
}

static int
sha512_reset(void *daos_mhash_ctx)
{
	struct sha512_ctx *ctx = daos_mhash_ctx;

	ctx->s5_updated = false;
	return 0;
}

static int
sha512_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	struct sha512_ctx	*ctx = daos_mhash_ctx;
	SHA512_HASH_CTX		*tmp;

	if (!ctx->s5_updated)
		tmp = sha512_ctx_mgr_submit(&ctx->s5_mgr,
					    &ctx->s5_ctx, buf,
					    buf_len,
					    HASH_FIRST);
	else
		tmp = sha512_ctx_mgr_submit(&ctx->s5_mgr,
					    &ctx->s5_ctx, buf,
					    buf_len,
					    HASH_UPDATE);

	if (tmp == NULL)
		sha512_ctx_mgr_flush(&ctx->s5_mgr);

	ctx->s5_updated = true;
	return ctx->s5_ctx.error;
}

static int
sha512_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
	struct sha512_ctx	*ctx = daos_mhash_ctx;

	if (ctx->s5_updated) {
		SHA512_HASH_CTX *tmp;

		tmp = sha512_ctx_mgr_submit(&ctx->s5_mgr,
					    &ctx->s5_ctx, NULL,
					    0,
					    HASH_LAST);

		if (tmp == NULL)
			sha512_ctx_mgr_flush(&ctx->s5_mgr);

		memcpy(buf, ctx->s5_ctx.job.result_digest, buf_len);

		return ctx->s5_ctx.error;
	}

	return 0;
}

struct hash_ft sha512_algo = {
	.cf_update	= sha512_update,
	.cf_init	= sha512_init,
	.cf_reset	= sha512_reset,
	.cf_destroy	= sha512_destroy,
	.cf_finish	= sha512_finish,
	.cf_hash_len	= 512 / 8,
	.cf_name	= "sha512",
	.cf_type	= HASH_TYPE_SHA512
};

/** Index to algo table should align with enum DAOS_HASH_TYPE - 1 */
struct hash_ft *isal_hash_algo_table[] = {
	&crc16_algo,
	&crc32_algo,
	&crc64_algo,
	&sha1_algo,
	&sha256_algo,
	&sha512_algo,
	&adler32_algo,
};
