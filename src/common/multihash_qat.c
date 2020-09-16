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
#define D_LOGFAC    DD_FAC(csum)

#ifdef HAVE_QAT
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <gurt/types.h>
#include <daos/common.h>
#include <daos/multihash.h>
#include <daos/qat.h>

#define SHA1_DIGEST_LENGTH   20
#define SHA256_DIGEST_LENGTH 32
#define SHA512_DIGEST_LENGTH 64
#define QAT_SHA1_BLK_SIZE    64
#define QAT_SHA256_BLK_SIZE  64
#define QAT_SHA512_BLK_SIZE  128

/**
 * ---------------------------------------------------------------------------
 * QAT Hash Algorithms
 * ---------------------------------------------------------------------------
 */

/** SHA1 */
struct sha1_qat_ctx {
    CpaInstanceHandle  cyInstHandle;
    CpaCySymSessionCtx sessionCtx;
    uint8_t            csum_buf[SHA1_DIGEST_LENGTH];
    bool               partial;
    bool               s1_updated;
};

static int
sha1_init(void ** daos_mhash_ctx)
{
    struct sha1_qat_ctx *ctx;
    int rc;
    D_ALLOC(ctx, sizeof(*ctx));
    if (ctx == NULL)
        return -DER_NOMEM;

    rc = qat_hash_init(&ctx->cyInstHandle, &ctx->sessionCtx, 
                CPA_CY_SYM_HASH_SHA1, SHA1_DIGEST_LENGTH);
    if (rc == 0)
        *daos_mhash_ctx = ctx;
    return rc;
}

static int
sha1_reset(void *daos_mhash_ctx)
{
    struct sha1_qat_ctx *ctx = daos_mhash_ctx;
    ctx->partial = false;
    ctx->s1_updated = false;
    return 0;
}

static void
sha1_destroy(void *daos_mhash_ctx)
{
    struct sha1_qat_ctx *ctx = daos_mhash_ctx;
    qat_hash_destroy(&ctx->cyInstHandle, &ctx->sessionCtx);
    
    D_FREE(daos_mhash_ctx);
}

static int
sha1_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
    struct sha1_qat_ctx *ctx = daos_mhash_ctx;

    /** QAT requires 64-byte aligned for SHA1 partial submission,
     *  if it's not the last one */
    ctx->partial = (ctx->s1_updated ||
                    ((buf_len & (QAT_SHA512_BLK_SIZE - 1)) == 0));
    ctx->s1_updated = true;
    
    return qat_hash_update(&ctx->cyInstHandle, &ctx->sessionCtx, buf, buf_len, 
                            ctx->csum_buf, SHA1_DIGEST_LENGTH, ctx->partial);
}

static int
sha1_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
    struct sha1_qat_ctx *ctx = daos_mhash_ctx;

    if (ctx->s1_updated) {
        if (ctx->partial)
            return qat_hash_finish(&ctx->cyInstHandle, &ctx->sessionCtx, 
                                    buf, buf_len);
        else
            memcpy(buf, ctx->csum_buf, buf_len);
    }
    return 0;
}

struct hash_ft qat_sha1_algo = {
    .cf_update = sha1_update,
    .cf_init = sha1_init,
    .cf_reset = sha1_reset,
    .cf_destroy = sha1_destroy,
    .cf_finish = sha1_finish,
    .cf_hash_len = SHA1_DIGEST_LENGTH,
    .cf_name = "sha1"
};

/** SHA256 */
struct sha256_qat_ctx {
    CpaInstanceHandle  cyInstHandle;
    CpaCySymSessionCtx sessionCtx;
    uint8_t            csum_buf[SHA256_DIGEST_LENGTH];
    bool               partial;
    bool               s2_updated;
};

static int
sha256_init(void ** daos_mhash_ctx)
{
    struct sha256_qat_ctx *ctx;
    int rc;

    D_ALLOC(ctx, sizeof(*ctx));
    if (ctx == NULL)
        return -DER_NOMEM;

    rc = qat_hash_init(&ctx->cyInstHandle, &ctx->sessionCtx, 
            CPA_CY_SYM_HASH_SHA256, SHA256_DIGEST_LENGTH);
    if (rc == 0)
        *daos_mhash_ctx = ctx;
    return rc;
}

static int
sha256_reset(void *daos_mhash_ctx)
{
    struct sha256_qat_ctx *ctx = daos_mhash_ctx;

    ctx->partial = false;
    ctx->s2_updated = false;
    return 0;
}

static void
sha256_destroy(void *daos_mhash_ctx)
{
    struct sha256_qat_ctx *ctx = daos_mhash_ctx;
    qat_hash_destroy(&ctx->cyInstHandle, &ctx->sessionCtx);
    
    D_FREE(daos_mhash_ctx);
}

static int
sha256_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
    struct sha256_qat_ctx *ctx = daos_mhash_ctx;

    /** QAT requires 64-byte aligned for SHA256 partial submission,
     *  if it's not the last one */
    ctx->partial = (ctx->s2_updated || 
                    ((buf_len & (QAT_SHA512_BLK_SIZE - 1)) == 0));
    ctx->s2_updated = true;
    
    return qat_hash_update(&ctx->cyInstHandle, &ctx->sessionCtx, buf, buf_len, 
                            ctx->csum_buf, SHA256_DIGEST_LENGTH, ctx->partial);
}

static int
sha256_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
    struct sha256_qat_ctx *ctx = daos_mhash_ctx;

    if (ctx->s2_updated) {
        if (ctx->partial)
            return qat_hash_finish(&ctx->cyInstHandle, &ctx->sessionCtx, 
                                    buf, buf_len);
        else
            memcpy(buf, ctx->csum_buf, buf_len);
    }
    return 0;
}

struct hash_ft qat_sha256_algo = {
    .cf_update = sha256_update,
    .cf_init = sha256_init,
    .cf_reset = sha256_reset,
    .cf_destroy = sha256_destroy,
    .cf_finish = sha256_finish,
    .cf_hash_len = SHA256_DIGEST_LENGTH,
    .cf_name = "sha256"
};

/** SHA512 */
struct sha512_qat_ctx {
    CpaInstanceHandle  cyInstHandle;
    CpaCySymSessionCtx sessionCtx;
    uint8_t            csum_buf[SHA512_DIGEST_LENGTH];
    bool               partial;
    bool               s5_updated;
};

static int
sha512_init(void ** daos_mhash_ctx)
{
    struct sha512_qat_ctx *ctx;
    int rc;

    D_ALLOC(ctx, sizeof(*ctx));
    if (ctx == NULL)
        return -DER_NOMEM;

    rc = qat_hash_init(&ctx->cyInstHandle, &ctx->sessionCtx, 
                    CPA_CY_SYM_HASH_SHA512, SHA512_DIGEST_LENGTH);
    if (rc == 0)
        *daos_mhash_ctx = ctx;
    return rc;
}

static int
sha512_reset(void *daos_mhash_ctx)
{
    struct sha512_qat_ctx *ctx = daos_mhash_ctx;

    ctx->partial = false;
    ctx->s5_updated = false;
    return 0;
}

static void
sha512_destroy(void *daos_mhash_ctx)
{
    struct sha512_qat_ctx *ctx = daos_mhash_ctx;
    qat_hash_destroy(&ctx->cyInstHandle, &ctx->sessionCtx);
    
    D_FREE(daos_mhash_ctx);
}

static int
sha512_update(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
    struct sha512_qat_ctx *ctx = daos_mhash_ctx;

    /** QAT requires 128-byte aligned for SHA512 partial submission,
     *  if it's not the last one */
    ctx->partial = (ctx->s5_updated ||
                    ((buf_len & (QAT_SHA512_BLK_SIZE - 1)) == 0));
    ctx->s5_updated = true;
    
    return qat_hash_update(&ctx->cyInstHandle, &ctx->sessionCtx, buf, buf_len, 
                            ctx->csum_buf, SHA512_DIGEST_LENGTH, ctx->partial);
}

static int
sha512_finish(void *daos_mhash_ctx, uint8_t *buf, size_t buf_len)
{
    struct sha512_qat_ctx *ctx = daos_mhash_ctx;

    if (ctx->s5_updated) {
        if (ctx->partial)
            return qat_hash_finish(&ctx->cyInstHandle, &ctx->sessionCtx, 
                                    buf, buf_len);
        else
            memcpy(buf, ctx->csum_buf, buf_len);
    }
    return 0;
}

struct hash_ft qat_sha512_algo = {
    .cf_update = sha512_update,
    .cf_init = sha512_init,
    .cf_reset = sha512_reset,
    .cf_destroy = sha512_destroy,
    .cf_finish = sha512_finish,
    .cf_hash_len = SHA512_DIGEST_LENGTH,
    .cf_name = "sha512"
};

/** Index to algo table should align with enum DAOS_HASH_TYPE - 1 */
struct hash_ft *qat_hash_algo_table[] = {
    NULL, /* CRC16 is not supported by QAT */
    NULL, /* CRC32 is not supported by QAT */
    NULL, /* CRC64 is not supported by QAT */
    &qat_sha1_algo,
    &qat_sha256_algo,
    &qat_sha512_algo,
};
#else
#include <gurt/types.h>
struct hash_ft *qat_hash_algo_table[] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};
#endif
