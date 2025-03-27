/*****************************************************************************
 Copyright (c) 2021, Intel Corporation

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Intel Corporation nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>

#include <intel-ipsec-mb.h>
#include "gcm_ctr_vectors_test.h"
#include "utils.h"

#define BUF_SIZE ((uint32_t)sizeof(struct gcm_key_data))
#define NUM_BUFS 8
#define SNOW3G_TOTAL_BUF_SIZE NUM_BUFS*16
/* If SNOW3G_N_TEST_COUNT changes tests for snow3g_f8_8_buffer_multikey and
 * snow3g_f8_n_buffer_multikey need to be separated.
 */
#define SNOW3G_N_TEST_COUNT 8


#define ZUC_MAX_BITLEN     65504
#define ZUC_MAX_BYTELEN    (ZUC_MAX_BITLEN / 8)
#define KASUMI_MAX_BITLEN  20000
#define KASUMI_MAX_BYTELEN  (KASUMI_MAX_BITLEN / 8)

#ifdef _WIN32
#define __func__ __FUNCTION__
#endif

int
direct_api_param_test(struct IMB_MGR *mb_mgr);

/* Used to restore environment after potential segfaults */
jmp_buf dir_api_param_env;

#ifndef DEBUG
#ifndef _WIN32
static void seg_handler(int signum) __attribute__((noreturn));
#endif
/* Signal handler to handle segfaults */
static void
seg_handler(int signum)
{
        (void) signum; /* unused */

        signal(SIGSEGV, seg_handler); /* reset handler */
        longjmp(dir_api_param_env, 1); /* reset dir_api_param_env */
}
#endif /* DEBUG */


/* Check if imb_errno contains unexpected value */
static int
unexpected_err(IMB_MGR *mgr, const IMB_ERR expected_err, const char *func_desc)
{
        const IMB_ERR err = imb_get_errno(mgr);

        if (err != expected_err) {
                printf("%s error: expected %s, got %s\n",
                       func_desc, imb_get_strerror(expected_err),
                       imb_get_strerror(err));
                return 1;
        }
        return 0;
}

/* GCM Encrypt and Decrypt tests */
static int
test_gcm_enc_dec(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                 const uint64_t len, struct gcm_key_data *key,
                 struct gcm_context_data *ctx, const uint8_t *iv,
                 const uint8_t *aad, uint8_t *tag)
{
        uint64_t i;
        const uint64_t aad_len = 28;
        const uint64_t tag_len = 16;
        const uint64_t invalid_msg_len = ((1ULL << 39) - 256);

        struct gcm_enc_dec_fn {
                aes_gcm_enc_dec_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gcm128_enc, "GCM-128 ENC" },
             { mgr->gcm192_enc, "GCM-192 ENC" },
             { mgr->gcm256_enc, "GCM-256 ENC" },
             { mgr->gcm128_dec, "GCM-128 DEC" },
             { mgr->gcm192_dec, "GCM-192 DEC" },
             { mgr->gcm256_dec, "GCM-256 DEC" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                uint8_t *out;
                uint8_t *in;
                const uint64_t len;
                const uint8_t *iv;
                const uint8_t *aad;
                const uint64_t aad_len;
                uint8_t *tag;
                const uint64_t tag_len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, out, in, len, iv, aad,
                 aad_len, tag, tag_len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, out, in, len, iv, aad,
                 aad_len, tag, tag_len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, in, len, iv, aad,
                 aad_len, tag, tag_len, IMB_ERR_NULL_DST },
                { key, ctx, out, NULL, len, iv, aad,
                 aad_len, tag, tag_len, IMB_ERR_NULL_SRC },
                { key, ctx, out, in, len, NULL, aad,
                 aad_len, tag, tag_len, IMB_ERR_NULL_IV },
                { key, ctx, out, in, len, iv, NULL,
                 aad_len, tag, tag_len, IMB_ERR_NULL_AAD },
                { key, ctx, out, in, len, iv, aad,
                 aad_len, NULL, tag_len, IMB_ERR_NULL_AUTH },
                { key, ctx, out, in, len, iv, aad,
                  aad_len, tag, 0, IMB_ERR_AUTH_TAG_LEN },
                { key, ctx, out, in, len, iv, aad,
                  aad_len, tag, 17, IMB_ERR_AUTH_TAG_LEN },
                { key, ctx, out, in, invalid_msg_len, iv, aad,
                  aad_len, tag, tag_len, IMB_ERR_CIPH_LEN }
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                memset(out, 0, len);
                memset(in, 0, len);

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->out, ap->in,
                                        ap->len, ap->iv, ap->aad, ap->aad_len,
                                        ap->tag, ap->tag_len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                                return 1;
                }
                /* Verify buffers not modified */
                if (memcmp(out, in, len) != 0) {
                        printf("%s: %s, invalid param test failed!\n",
                               __func__, fn_ptrs[i].func_name);
                        return 1;
                }
                printf(".");
        }
        return 0;
}

/* GCM key data pre-processing tests */
static int
test_gcm_precomp(struct IMB_MGR *mgr)
{
        uint64_t i;

        struct gcm_precomp_fn {
                aes_gcm_precomp_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gcm128_precomp, "GCM-128 PRECOMP" },
             { mgr->gcm192_precomp, "GCM-192 PRECOMP" },
             { mgr->gcm256_precomp, "GCM-256 PRECOMP" },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {

                /* NULL key pointer test */
                fn_ptrs[i].func(NULL);
                if (unexpected_err(mgr, IMB_ERR_NULL_EXP_KEY,
                                   fn_ptrs[i].func_name))
                        return 1;
                printf(".");
        }
        return 0;
}

/* GHASH key data pre-processing tests */
static int
test_gcm_pre(struct IMB_MGR *mgr,
             struct gcm_key_data *key_data,
             uint8_t *key)
{
        uint64_t i;

        struct gcm_pre_fn {
                aes_gcm_pre_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gcm128_pre, "GCM-128 PRE" },
             { mgr->gcm192_pre, "GCM-192 PRE" },
             { mgr->gcm256_pre, "GCM-256 PRE" },
             { mgr->ghash_pre,  "GHASH-PRE"   },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {

                memset(key, 0, sizeof(*key_data));
                memset(key_data, 0, sizeof(*key_data));

                /* NULL key pointer test */
                fn_ptrs[i].func(NULL, key_data);
                if (unexpected_err(mgr, IMB_ERR_NULL_KEY,
                                   fn_ptrs[i].func_name))
                        return 1;

                /* NULL key data pointer test */
                fn_ptrs[i].func(key, NULL);
                if (unexpected_err(mgr, IMB_ERR_NULL_EXP_KEY,
                                   fn_ptrs[i].func_name))
                        return 1;

                /* Verify no buffers have been modified */
                if (memcmp(key, key_data, sizeof(*key_data)) != 0) {
                        printf("%s: %s, invalid param test failed!\n",
                               __func__, fn_ptrs[i].func_name);
                        return 1;
                }

                /* Pass valid params to reset imb_errno */
                fn_ptrs[i].func(key, key_data);
                if (unexpected_err(mgr, 0, fn_ptrs[i].func_name))
                        return 1;
                printf(".");
        }
        return 0;
}


/* GCM Init tests */
static int
test_gcm_init(struct IMB_MGR *mgr, struct gcm_key_data *key,
              struct gcm_context_data *ctx, const uint8_t *iv,
              const uint8_t *aad)
{
        uint64_t i;
        const uint64_t aad_len = 28;

        struct gcm_init_fn {
                aes_gcm_init_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gcm128_init, "GCM-128 INIT" },
             { mgr->gcm192_init, "GCM-192 INIT" },
             { mgr->gcm256_init, "GCM-256 INIT" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                const uint8_t *iv;
                const uint8_t *aad;
                uint64_t aad_len;
                IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, iv, aad, aad_len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, iv, aad, aad_len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, aad, aad_len, IMB_ERR_NULL_IV },
                { key, ctx, iv, NULL, aad_len, IMB_ERR_NULL_AAD },
                { key, ctx, iv, aad, 0, 0 },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->iv,
                                        ap->aad, ap->aad_len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                                return 1;
                }
                printf(".");
        }
        return 0;
}

/* GCM Init variable IV len tests */
static int
test_gcm_init_var_iv(struct IMB_MGR *mgr, struct gcm_key_data *key,
                     struct gcm_context_data *ctx, const uint8_t *iv,
                     const uint8_t *aad)
{
        uint64_t i;
        const uint64_t aad_len = 28;
        const uint64_t iv_len = 16;

        struct gcm_init_var_iv_fn {
                aes_gcm_init_var_iv_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gcm128_init_var_iv, "GCM-128 INIT VAR IV" },
             { mgr->gcm192_init_var_iv, "GCM-192 INIT VAR IV" },
             { mgr->gcm256_init_var_iv, "GCM-256 INIT VAR IV" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                const uint8_t *iv;
                const uint64_t iv_len;
                const uint8_t *aad;
                uint64_t aad_len;
                IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, iv, iv_len, aad, aad_len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, iv, iv_len, aad, aad_len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, iv_len, aad, aad_len, IMB_ERR_NULL_IV },
                { key, ctx, iv, 0, aad, aad_len, IMB_ERR_IV_LEN },
                { key, ctx, iv, iv_len, NULL, aad_len, IMB_ERR_NULL_AAD },
                { key, ctx, iv, iv_len, aad, 0, 0 },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->iv,
                                        ap->iv_len, ap->aad, ap->aad_len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                                return 1;
                }
                printf(".");
        }
        return 0;
}

/* GCM Encrypt and Decrypt Update tests */
static int
test_gcm_enc_dec_update(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint64_t len, struct gcm_context_data *ctx,
                        struct gcm_key_data *key)
{
        uint64_t i;
        const uint64_t invalid_msg_len = ((1ULL << 39) - 256);

        struct gcm_enc_dec_update_fn {
                aes_gcm_enc_dec_update_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gcm128_enc_update, "GCM-128 ENC UPDATE" },
             { mgr->gcm192_enc_update, "GCM-192 ENC UPDATE" },
             { mgr->gcm256_enc_update, "GCM-256 ENC UPDATE" },
             { mgr->gcm128_dec_update, "GCM-128 DEC UPDATE" },
             { mgr->gcm192_dec_update, "GCM-192 DEC UPDATE" },
             { mgr->gcm256_dec_update, "GCM-256 DEC UPDATE" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                uint8_t *out;
                uint8_t *in;
                const uint64_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, out, in, len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, out, in, len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, in, len, IMB_ERR_NULL_DST },
                { key, ctx, out, NULL, len, IMB_ERR_NULL_SRC },
                { key, ctx, out, in, invalid_msg_len, IMB_ERR_CIPH_LEN },
                { key, ctx, out, in, 0, 0 },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                memset(out, 0, len);
                memset(in, 0, len);

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->out,
                                        ap->in, ap->len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                                return 1;
                }

                /* Verify buffers not modified */
                if (memcmp(out, in, len) != 0) {
                        printf("%s: %s, invalid param test failed!\n",
                               __func__, fn_ptrs[i].func_name);
                        return 1;
                }
                printf(".");
        }
        return 0;
}

/* GCM Encrypt and Decrypt Finalize tests */
static int
test_gcm_enc_dec_finalize(struct IMB_MGR *mgr, struct gcm_key_data *key,
                          struct gcm_context_data *ctx, uint8_t *tag,
                          uint8_t *zero_buf)
{
        uint64_t i;
        const uint64_t tag_len = 16;

        struct gcm_enc_dec_finalize_fn {
                aes_gcm_enc_dec_finalize_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gcm128_enc_finalize, "GCM-128 ENC FINALIZE" },
             { mgr->gcm192_enc_finalize, "GCM-192 ENC FINALIZE" },
             { mgr->gcm256_enc_finalize, "GCM-256 ENC FINALIZE" },
             { mgr->gcm128_dec_finalize, "GCM-128 DEC FINALIZE" },
             { mgr->gcm192_dec_finalize, "GCM-192 DEC FINALIZE" },
             { mgr->gcm256_dec_finalize, "GCM-256 DEC FINALIZE" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                uint8_t *tag;
                const uint64_t tag_len;
                IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, tag, tag_len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, tag, tag_len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, tag_len, IMB_ERR_NULL_AUTH },
                { key, ctx, tag, 0, IMB_ERR_AUTH_TAG_LEN },
                { key, ctx, tag, 17, IMB_ERR_AUTH_TAG_LEN },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                memset(tag, 0, tag_len);
                memset(zero_buf, 0, tag_len);

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->tag, ap->tag_len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                        return 1;
                }

                /* Verify tag buffer not modified */
                if (memcmp(tag, zero_buf, tag_len) != 0) {
                        printf("%s: %s, invalid param test failed!\n",
                               __func__,
                               fn_ptrs[i].func_name);
                        return 1;
                }
                printf(".");
        }
        return 0;
}

/* GMAC init tests */
static int
test_gmac_init(struct IMB_MGR *mgr,
               struct gcm_key_data *key,
               struct gcm_context_data *ctx,
               const uint8_t *iv)
{
        uint64_t i;
        const uint64_t iv_len = 16;

        struct gmac_init_fn {
                aes_gmac_init_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gmac128_init, "GMAC-128 INIT" },
             { mgr->gmac192_init, "GMAC-192 INIT" },
             { mgr->gmac256_init, "GMAC-256 INIT" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                const uint8_t *iv;
                uint64_t iv_len;
                IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, iv, iv_len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, iv, iv_len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, iv_len, IMB_ERR_NULL_IV },
                { key, ctx, iv, 0, IMB_ERR_IV_LEN },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->iv,
                                        ap->iv_len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                                return 1;
                }
                printf(".");
        }
        return 0;
}

/* GMAC Update tests */
static int
test_gmac_update(struct IMB_MGR *mgr, uint8_t *in,
                 const uint64_t len, struct gcm_context_data *ctx,
                 struct gcm_key_data *key)
{
        uint64_t i;

        struct gmac_update_fn {
                aes_gmac_update_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gmac128_update, "GMAC-128 UPDATE" },
             { mgr->gmac192_update, "GMAC-192 UPDATE" },
             { mgr->gmac256_update, "GMAC-256 UPDATE" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                uint8_t *in;
                const uint64_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, in, len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, in, len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, len, IMB_ERR_NULL_SRC },
                { key, ctx, in, 0, 0 },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->in, ap->len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                                return 1;
                }
                printf(".");
        }
        return 0;
}

/* GMAC Finalize tests */
static int
test_gmac_finalize(struct IMB_MGR *mgr, struct gcm_key_data *key,
                   struct gcm_context_data *ctx, uint8_t *tag,
                   uint8_t *zero_buf)
{
        uint64_t i;
        const uint64_t tag_len = 16;

        struct aes_gmac_finalize_fn {
                aes_gmac_finalize_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->gmac128_finalize, "GMAC-128 FINALIZE" },
             { mgr->gmac192_finalize, "GMAC-192 FINALIZE" },
             { mgr->gmac256_finalize, "GMAC-256 FINALIZE" },
        };

        struct fn_args {
                struct gcm_key_data *key;
                struct gcm_context_data *ctx;
                uint8_t *tag;
                const uint64_t tag_len;
                IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, tag, tag_len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, tag, tag_len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, tag_len, IMB_ERR_NULL_AUTH },
                { key, ctx, tag, 0, IMB_ERR_AUTH_TAG_LEN },
                { key, ctx, tag, 17, IMB_ERR_AUTH_TAG_LEN },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                uint64_t j;

                memset(tag, 0, tag_len);
                memset(zero_buf, 0, tag_len);

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->tag, ap->tag_len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                        return 1;
                }

                /* Verify tag buffer not modified */
                if (memcmp(tag, zero_buf, tag_len) != 0) {
                        printf("%s: %s, invalid param test failed!\n",
                               __func__,
                               fn_ptrs[i].func_name);
                        return 1;
                }
                printf(".");
        }
        return 0;
}

/* GHASH tests */
static int
test_ghash(struct IMB_MGR *mgr, struct gcm_key_data *key,
           uint8_t *in, const uint64_t len, uint8_t *tag)
{
        uint64_t i;
        const uint64_t tag_len = 16;

        struct fn_args {
                struct gcm_key_data *key;
                uint8_t *in;
                const uint64_t len;
                uint8_t *tag;
                const uint64_t tag_len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, in, len, tag, tag_len, IMB_ERR_NULL_EXP_KEY },
                { key, NULL, len, tag, tag_len, IMB_ERR_NULL_SRC },
                { key, in, 0, tag, tag_len, IMB_ERR_AUTH_LEN },
                { key, in, len, NULL, tag_len, IMB_ERR_NULL_AUTH },
                { key, in, len, tag, 0, IMB_ERR_AUTH_TAG_LEN },
        };

        memset(in, 0, tag_len);
        memset(tag, 0, tag_len);

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->ghash(ap->key, ap->in, ap->len,
                           ap->tag, ap->tag_len);
                if (unexpected_err(mgr, ap->exp_err, "GHASH"))
                        return 1;
        }
        /* Verify buffers not modified */
        if (memcmp(tag, in, tag_len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                       __func__, "GHASH");
                return 1;
        }
        printf(".");

        return 0;
}

/*
 * @brief Performs direct GCM API invalid param tests
 */
static int
test_gcm_api(struct IMB_MGR *mgr)
{
        const uint64_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        struct gcm_key_data *key_data = (struct gcm_key_data *)out_buf;
        struct gcm_context_data *ctx = (struct gcm_context_data *)out_buf;
        const uint8_t *iv = zero_buf;
        const uint8_t *aad = zero_buf;
        uint8_t *tag = out_buf;
        int seg_err; /* segfault flag */

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        /* GCM Encrypt and Decrypt tests */
        if (test_gcm_enc_dec(mgr, zero_buf, out_buf, text_len,
                             key_data, ctx, iv, aad, tag))
                return 1;

        /* GCM key data pre-processing tests */
        if (test_gcm_precomp(mgr))
                return 1;

        if (test_gcm_pre(mgr, key_data, zero_buf))
                return 1;

        /* GCM Init tests */
        if (test_gcm_init(mgr, key_data, ctx, iv, aad))
                return 1;

        /* GCM Init variable IV len tests */
        if (test_gcm_init_var_iv(mgr, key_data, ctx, iv, aad))
                return 1;

        /* GCM Encrypt and Decrypt update tests */
        if (test_gcm_enc_dec_update(mgr, zero_buf, out_buf,
                                    text_len, ctx, key_data))
                return 1;

        /* GCM Encrypt and Decrypt Finalize tests */
        if (test_gcm_enc_dec_finalize(mgr, key_data, ctx, tag, zero_buf))
                return 1;

        /* GMAC Init tests */
        if (test_gmac_init(mgr, key_data, ctx, iv))
                return 1;

	/* GMAC Update tests */
        if (test_gmac_update(mgr, out_buf, text_len, ctx, key_data))
                return 1;

        /* GMAC Finalize tests */
        if (test_gmac_finalize(mgr, key_data, ctx, tag, zero_buf))
                return 1;

        /* GHASH tests */
        if (test_ghash(mgr, key_data, zero_buf, text_len, out_buf))
                return 1;

        printf("\n");
        return 0;
}
static int
test_key_exp_gen_api_test(struct IMB_MGR *mgr, const void *key,
                          void *enc_exp_keys, void *dec_exp_keys)
{
        int seg_err; /* segfault flag */
        unsigned i, j;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        struct {
                keyexp_t fn;
                const char *name;
        } fn_ptrs[] = {
                { mgr->keyexp_128, "KEYEXP 128" },
                { mgr->keyexp_192, "KEYEXP 192" },
                { mgr->keyexp_256, "KEYEXP 256" },
        };

        struct fn_args {
                const void *key;
                void *enc_exp_keys;
                void *dec_exp_keys;
                IMB_ERR exp_err;
        } fn_args[] = {
                       { NULL, enc_exp_keys, dec_exp_keys, IMB_ERR_NULL_KEY },
                       { key, NULL, dec_exp_keys, IMB_ERR_NULL_EXP_KEY },
                       { key, enc_exp_keys, NULL, IMB_ERR_NULL_EXP_KEY },
                       { key, enc_exp_keys, dec_exp_keys, 0 },
        };
        for (i = 0; i < DIM(fn_ptrs); i++) {
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].fn(ap->key, ap->enc_exp_keys,
                                      ap->dec_exp_keys);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].name))
                        return 1;
                }
        }

        return 0;
}

/*
 * @brief Performs direct Key expansion and
 *        generation API invalid param tests
 */
static int
test_key_exp_gen_api(struct IMB_MGR *mgr)
{

        const uint32_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int seg_err; /* segfault flag */
        const void *key = zero_buf;
        void *enc_exp_keys = zero_buf;
        void *dec_exp_keys = zero_buf;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        if (test_key_exp_gen_api_test(mgr, key, enc_exp_keys, dec_exp_keys))
                return 1;

        return 0;
}

static int
test_cmac_subkey_gen_api_test(struct IMB_MGR *mgr, const void *key_exp,
                         void *key1, void *key2)
{

        const uint32_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int seg_err; /* segfault flag */
        unsigned i, j;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        struct {
                cmac_subkey_gen_t fn;
                const char *name;
        } fn_ptrs[] = {
                { mgr->cmac_subkey_gen_128, "CMAC SUBKEY GEN 128" },
                { mgr->cmac_subkey_gen_256, "CMAC SUBKEY GEN 256" },
        };
        struct fn_args {
                const void *key_exp;
                void *key1;
                void *key2;
                IMB_ERR exp_err;
        } fn_args[] = {
                       { NULL, key1, key2, IMB_ERR_NULL_EXP_KEY },
                       { key_exp, NULL, key2, IMB_ERR_NULL_KEY },
                       { key_exp, key1, NULL, IMB_ERR_NULL_KEY },
                       { key_exp, key1, key2, 0 },
        };
        for (i = 0; i < DIM(fn_ptrs); i++) {
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].fn(ap->key_exp, ap->key1,
                                      ap->key2);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].name))
                        return 1;
                }
        }

        return 0;
}

static int
test_cmac_subkey_gen_api(struct IMB_MGR *mgr)
{

        const uint32_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int seg_err; /* segfault flag */
        const void *key_exp = zero_buf;
        void *key1 = zero_buf;
        void *key2 = zero_buf;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        if (test_cmac_subkey_gen_api_test(mgr, key_exp, key1, key2))
                return 1;

        return 0;
}

/*
 * @brief Performs direct hash API invalid param tests
 */
static int
test_hash_api(struct IMB_MGR *mgr)
{
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int seg_err; /* segfault flag */
        unsigned i, j;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, sizeof(out_buf));
        memset(zero_buf, 0, sizeof(zero_buf));

        /* Test hash one block API's */

        struct {
                hash_one_block_t fn;
                const char *name;
        } fn1_ptrs[] = {
                { mgr->sha1_one_block, "SHA1 ONE BLOCK" },
                { mgr->sha224_one_block, "SHA224 ONE BLOCK" },
                { mgr->sha256_one_block, "SHA256 ONE BLOCK" },
                { mgr->sha384_one_block, "SHA384 ONE BLOCK" },
                { mgr->sha512_one_block, "SHA512 ONE BLOCK" },
                { mgr->md5_one_block, "MD5 ONE BLOCK" },
        };

        struct {
                const void *src;
                void *auth;
                IMB_ERR exp_err;
        } fn1_args[] = {
                { NULL, out_buf, IMB_ERR_NULL_SRC },
                { zero_buf, NULL, IMB_ERR_NULL_AUTH },
                { zero_buf, out_buf, 0 },
        };

        for (i = 0; i < DIM(fn1_ptrs); i++) {
                for (j = 0; j < DIM(fn1_args); j++) {
                        fn1_ptrs[i].fn(fn1_args[j].src,
                                       fn1_args[j].auth);

                        if (unexpected_err(mgr, fn1_args[j].exp_err,
                                           fn1_ptrs[i].name))
                        return 1;
                }
        }

        /* Test hash API's */

        struct {
                hash_fn_t fn;
                const char *name;
        } fn2_ptrs[] = {
                { mgr->sha1, "SHA1" },
                { mgr->sha224, "SHA224" },
                { mgr->sha256, "SHA256" },
                { mgr->sha384, "SHA384" },
                { mgr->sha512, "SHA512" },
        };

        struct {
                const void *src;
                uint64_t length;
                void *auth;
                IMB_ERR exp_err;
        } fn2_args[] = {
                { NULL, sizeof(zero_buf), out_buf, IMB_ERR_NULL_SRC },
                { zero_buf, sizeof(zero_buf), NULL, IMB_ERR_NULL_AUTH },
                { zero_buf, 0, out_buf, 0 },
                { zero_buf, sizeof(zero_buf), out_buf, 0 },
        };

        for (i = 0; i < DIM(fn2_ptrs); i++) {
                for (j = 0; j < DIM(fn2_args); j++) {
                        fn2_ptrs[i].fn(fn2_args[j].src,
                                       fn2_args[j].length,
                                       fn2_args[j].auth);

                        if (unexpected_err(mgr, fn2_args[j].exp_err,
                                           fn2_ptrs[i].name))
                        return 1;
                }
        }

        return 0;
}

static int
test_cfb_one(struct IMB_MGR *mgr, void *out, const void *in, const void *iv,
             const void *keys, uint64_t len)
{
        int seg_err; /* segfault flag */
        unsigned j;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        struct {
                aes_cfb_t fn;
                const char *name;
        } fn_ptrs = { mgr->aes128_cfb_one, "AES CFB ONE" };

        struct fn_args {
                void *out;
                const void *in;
                const void *iv;
                const void *keys;
                uint64_t len;
                IMB_ERR exp_err;
        } fn_args[] = {
                       { NULL, in, iv, keys, len, IMB_ERR_NULL_DST },
                       { out, NULL, iv, keys, len, IMB_ERR_NULL_SRC },
                       { out, in, NULL, keys, len, IMB_ERR_NULL_IV },
                       { out, in, iv, NULL, len, IMB_ERR_NULL_EXP_KEY },
        };
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->out, ap->in, ap->iv,
                           ap->keys, ap->len);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }
        return 0;
}

/*
 * @brief Performs direct AES API invalid param tests
 */
static int
test_aes_api(struct IMB_MGR *mgr)
{
        uint8_t buf[BUF_SIZE];
        int seg_err; /* segfault flag */
        const uint8_t *in = buf;
        uint8_t *out = buf;
        const uint8_t *iv = buf;
        uint8_t *keys = buf;
        uint64_t len = BUF_SIZE;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        if (test_cfb_one(mgr, out, in, iv, keys, len))
                return 1;

        return 0;
}

/* ZUC-EEA3 1 Buffer tests */
static int
test_zuc_eea3_1_buffer(struct IMB_MGR *mgr, const void *key, const void *iv,
                       const void *in, void *out, const uint32_t len)
{
        unsigned int i;
        const char func_name[] = "ZUC-EEA3 1 BUFFER";

        struct fn_args {
                const void *key;
                const void *iv;
                const void *in;
                void *out;
                const uint32_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, out, len, IMB_ERR_NULL_KEY },
                { key, NULL, in, out, len, IMB_ERR_NULL_IV },
                { key, iv, NULL, out, len, IMB_ERR_NULL_SRC },
                { key, iv, in, NULL, len, IMB_ERR_NULL_DST },
                { key, iv, in, out, 0, IMB_ERR_CIPH_LEN},
                { key, iv, in, out, ZUC_MAX_BYTELEN + 1, IMB_ERR_CIPH_LEN},
                { key, iv, in, out, len, 0},
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->eea3_1_buffer(ap->key, ap->iv, ap->in, ap->out, ap->len);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/* ZUC-EEA3 4 Buffer tests */
static int
test_zuc_eea3_4_buffer(struct IMB_MGR *mgr, const void **key, const void **iv,
                       const void **in, void **out,
                       const uint32_t *lens, const uint32_t *zero_lens,
                       const uint32_t *oversized_lens)
{
        unsigned int i;
        const char func_name[] = "ZUC-EEA3 4 BUFFER";

        struct fn_args {
                const void **key;
                const void **iv;
                const void *in;
                void *out;
                const uint32_t *lens;
                const IMB_ERR exp_err;
        } fn_args[] = {
                {NULL, iv, in, out, lens, IMB_ERR_NULL_KEY},
                {key, NULL, in, out, lens, IMB_ERR_NULL_IV},
                {key, iv, NULL, out, lens, IMB_ERR_NULL_SRC},
                {key, iv, in, NULL, lens, IMB_ERR_NULL_DST},
                {key, iv, in, out, zero_lens, IMB_ERR_CIPH_LEN},
                {key, iv, in, out, oversized_lens, IMB_ERR_CIPH_LEN},
                {key, iv, in, out, lens, 0},
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->eea3_4_buffer(ap->key, ap->iv, ap->in, ap->out, ap->lens);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/* ZUC-EEA3 N Buffer tests */
static int
test_zuc_eea3_n_buffer(struct IMB_MGR *mgr, const void **key, const void **iv,
                       const void **in, void **out,
                       const uint32_t *lens, const uint32_t *zero_lens,
                       const uint32_t *oversized_lens)
{
        unsigned int i;
        const char func_name[] = "ZUC-EEA3 N BUFFER";

        struct fn_args {
                const void **key;
                const void **iv;
                const void *in;
                void *out;
                const uint32_t *lens;
                const IMB_ERR exp_err;
        } fn_args[] = {
                {NULL, iv, in, out, lens, IMB_ERR_NULL_KEY},
                {key, NULL, in, out, lens, IMB_ERR_NULL_IV},
                {key, iv, NULL, out, lens, IMB_ERR_NULL_SRC},
                {key, iv, in, NULL, lens, IMB_ERR_NULL_DST},
                {key, iv, in, out, zero_lens, IMB_ERR_CIPH_LEN},
                {key, iv, in, out, oversized_lens, IMB_ERR_CIPH_LEN},
                {key, iv, in, out, lens, 0},
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->eea3_n_buffer(ap->key, ap->iv, ap->in, ap->out, ap->lens,
                                   NUM_BUFS);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/* ZUC-EIA3 1 Buffer tests */
static int
test_zuc_eia3_1_buffer(struct IMB_MGR *mgr, const void *key, const void *iv,
                       const void *in, uint32_t *tag, const uint32_t len)
{
        unsigned int i;
        const char func_name[] = "ZUC-EIA3 1 BUFFER";

        struct fn_args {
                const void *key;
                const void *iv;
                const void *in;
                const uint32_t len;
                uint32_t *tag;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, len, tag, IMB_ERR_NULL_KEY },
                { key, NULL, in, len, tag, IMB_ERR_NULL_IV },
                { key, iv, NULL, len, tag, IMB_ERR_NULL_SRC },
                { key, iv, in, len, NULL, IMB_ERR_NULL_AUTH },
                { key, iv, in, 0, tag, IMB_ERR_AUTH_LEN},
                { key, iv, in, ZUC_MAX_BITLEN + 1, tag, IMB_ERR_AUTH_LEN},
                { key, iv, in, len, tag, 0},
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->eia3_1_buffer(ap->key, ap->iv, ap->in, ap->len, ap->tag);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/* ZUC-EIA3 N Buffer tests */
static int
test_zuc_eia3_n_buffer(struct IMB_MGR *mgr, const void **key, const void **iv,
                       const void **in, uint32_t **tag, const uint32_t *len,
                       const uint32_t *zero_lens,
                       const uint32_t *oversized_lens)
{
        unsigned int i;
        const char func_name[] = "ZUC-EIA3 N BUFFER";

        struct fn_args {
                const void **key;
                const void **iv;
                const void **in;
                const uint32_t *len;
                uint32_t **tag;
                const IMB_ERR exp_err;
        } fn_args[] = {
                {NULL, iv, in, len, tag, IMB_ERR_NULL_KEY},
                {key, NULL, in, len, tag, IMB_ERR_NULL_IV},
                {key, iv, NULL, len, tag, IMB_ERR_NULL_SRC},
                {key, iv, in, len, NULL, IMB_ERR_NULL_AUTH},
                {key, iv, in, zero_lens, tag, IMB_ERR_AUTH_LEN},
                {key, iv, in, oversized_lens, tag, IMB_ERR_AUTH_LEN},
                {key, iv, in, len, tag, 0},
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->eia3_n_buffer(ap->key, ap->iv, ap->in, ap->len, ap->tag,
                                   NUM_BUFS);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/*
 * @brief Performs direct ZUC API invalid param tests
 */
static int
test_zuc_api(struct IMB_MGR *mgr)
{
        int seg_err; /* segfault flag */
        uint8_t in_bufs[NUM_BUFS][BUF_SIZE];
        uint8_t out_bufs[NUM_BUFS][BUF_SIZE];
        uint32_t tags[NUM_BUFS];
        uint32_t lens[NUM_BUFS];
        uint32_t zero_lens[NUM_BUFS];
        uint32_t oversized_lens[NUM_BUFS];
        const uint8_t key[NUM_BUFS][16];
        const uint8_t iv[NUM_BUFS][16];
        const void *key_ptrs[NUM_BUFS];
        const void *iv_ptrs[NUM_BUFS];
        const void *in_ptrs[NUM_BUFS];
        void *out_ptrs[NUM_BUFS];
        uint32_t *tag_ptrs[NUM_BUFS];
        unsigned int i;

        for (i = 0; i < NUM_BUFS; i++) {
                key_ptrs[i] = key[i];
                iv_ptrs[i] = iv[i];
                in_ptrs[i] = in_bufs[i];
                memset(in_bufs[i], 0, BUF_SIZE);
                out_ptrs[i] = out_bufs[i];
                tag_ptrs[i] = &tags[i];
                lens[i] = BUF_SIZE;
                zero_lens[i] = 0;
                oversized_lens[i] = ZUC_MAX_BYTELEN + 1;
        }
        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        if (test_zuc_eea3_1_buffer(mgr, key[0], iv[0], in_bufs[0],
                                   out_bufs[0], lens[0]))
                return 1;

        if (test_zuc_eea3_4_buffer(mgr, key_ptrs, iv_ptrs,
                                   in_ptrs, out_ptrs,
                                   lens, zero_lens, oversized_lens))
                return 1;

        if (test_zuc_eea3_n_buffer(mgr, key_ptrs, iv_ptrs,
                                   in_ptrs, out_ptrs,
                                   lens, zero_lens, oversized_lens))
                return 1;

        /* Convert byte to bit lengths for ZUC-EIA3 tests*/
        for (i = 0; i < NUM_BUFS; i++) {
                oversized_lens[i] *= 8;
                lens[i] *= 8;
        }
        if (test_zuc_eia3_1_buffer(mgr, key_ptrs[0], iv_ptrs[0],
                                   in_ptrs[0], tag_ptrs[0], lens[0]))
                return 1;

        if (test_zuc_eia3_n_buffer(mgr, key_ptrs, iv_ptrs, in_ptrs,
                                   tag_ptrs, lens,
                                   zero_lens, oversized_lens))
                return 1;

        return 0;
}

static int
test_kasumi_api_f8_1_buffer(struct IMB_MGR *mgr, const kasumi_key_sched_t *ctx,
                            const uint64_t iv, const void *in, void *out,
                            const uint32_t len)
{
        uint64_t j;

        struct {
                kasumi_f8_1_buffer_t fn;
                const char *name;
        } fn_ptrs = { mgr->f8_1_buffer, "KASUMI F8 1" };

        struct fn_args {
                const kasumi_key_sched_t *ctx;
                const uint64_t iv;
                const void *in;
                void *out;
                const uint32_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, out, len, IMB_ERR_NULL_EXP_KEY },
                { ctx, iv, NULL, out, len, IMB_ERR_NULL_SRC },
                { ctx, iv, in, NULL, len, IMB_ERR_NULL_DST },
                { ctx, iv, in, out, 0, IMB_ERR_CIPH_LEN },
        };

        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->ctx, ap->iv, ap->in, ap->out,
                                ap->len);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }

        return 0;
}

static int
test_kasumi_api_f8_1_buffer_bit(struct IMB_MGR *mgr,
                                const kasumi_key_sched_t *ctx,
                                const uint64_t iv, const void *in, void  *out,
                                const uint32_t len, const uint32_t offset)
{
        uint64_t j;

        struct {
                kasumi_f8_1_buffer_bit_t fn;
                const char *name;
        } fn_ptrs = { mgr->f8_1_buffer_bit, "KASUMI F8 1 BIT" };

        struct fn_args {
                const kasumi_key_sched_t *key_data;
                const uint64_t iv;
                const void *in;
                void *out;
                const uint32_t len;
                const uint32_t offset;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, out, len, offset, IMB_ERR_NULL_EXP_KEY },
                { ctx, iv, NULL, out, len, offset, IMB_ERR_NULL_SRC },
                { ctx, iv, in, NULL, len, offset, IMB_ERR_NULL_DST },
                { ctx, iv, in, out, 0, offset, IMB_ERR_CIPH_LEN },
        };

        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->key_data, ap->iv, ap->in, ap->out,
                           ap->len, ap->offset);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }
        return 0;
}

static int
test_kasumi_api_f8_2_buffer(struct IMB_MGR *mgr, const kasumi_key_sched_t *ctx,
                            const uint64_t iv, const uint64_t iv2,
                            const void *in, const void *in2,
                            void  *out, void  *out2,
                            const uint32_t len, const uint32_t len2)
{
        uint64_t j;

        struct {
                kasumi_f8_2_buffer_t fn;
                const char *name;
        } fn_ptrs = { mgr->f8_2_buffer, "KASUMI F8 2" };

        struct fn_args {
                const kasumi_key_sched_t *key_data;
                const uint64_t iv;
                const uint64_t iv2;
                const void *in;
                const void *in2;
                void *out;
                void *out2;
                const uint32_t len;
                const uint32_t len2;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, iv2, in, in2, out, out2, len, len2,
                  IMB_ERR_NULL_EXP_KEY },
                { ctx, iv, iv2, NULL, in2, out, out2, len, len2,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, in, NULL, out, out2, len, len2,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, in, in2, NULL, out2, len, len2,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv2, in, in2, out, NULL, len, len2,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv2, in, in2, out, out2, 0, len2,
                  IMB_ERR_CIPH_LEN },
                { ctx, iv, iv2, in, in2, out, out2, len, 0,
                  IMB_ERR_CIPH_LEN },
        };
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->key_data, ap->iv, ap->iv2, ap->in, ap->out,
                           ap->len,  ap->in2, ap->out2, ap->len2);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }
        return 0;
}

/*
 * @brief Performs direct KASUMI API invalid param tests
 */
static int
test_kasumi_api_f8_3_buffer(struct IMB_MGR *mgr, const kasumi_key_sched_t *ctx,
                            const uint64_t iv, const uint64_t iv2,
                            const uint64_t iv3, const void *in,
                            const void *in2, const void *in3,
                            void  *out, void  *out2, void  *out3,
                            const uint32_t len)
{
        uint64_t j;

        struct {
                kasumi_f8_3_buffer_t fn;
                const char *name;
        } fn_ptrs = { mgr->f8_3_buffer, "KASUMI F8 3" };

        struct fn_args {
                const kasumi_key_sched_t *key_data;
                const uint64_t iv; const uint64_t iv2; const uint64_t iv3;
                const void *in; const void *in2; const void *in3;
                void *out; void *out2; void *out3;
                const uint32_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, iv2, iv3, in, in2, in3,
                  out, out2, out3, len, IMB_ERR_NULL_EXP_KEY },
                { ctx, iv, iv2, iv3, NULL, in2, in3,
                  out, out2, out3, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, iv3, in, NULL, in3,
                  out, out2, out3, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, iv3, in, in2, NULL,
                  out, out2, out3, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, iv3, in, in2, in3,
                  NULL, out2, out3, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv2, iv3, in, in2, in3,
                  out, NULL, out3, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv2, iv3, in, in2, in3,
                  out, out2, NULL, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv2, iv3, in, in2, in3,
                  out, out2, out3, 0, IMB_ERR_CIPH_LEN },
        };
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->key_data, ap->iv, ap->iv2, ap->iv3,
                           ap->in, ap->out, ap->in2, ap->out2,
                           ap->in3, ap->out3, ap->len);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }
        return 0;
}

static int
test_kasumi_api_f8_4_buffer(struct IMB_MGR *mgr, const kasumi_key_sched_t *ctx,
                            const uint64_t iv, const uint64_t iv2,
                            const uint64_t iv3, const uint64_t iv4,
                            const void *in, const void *in2,
                            const void *in3, const void *in4,
                            void *out, void  *out2, void  *out3,
                            void *out4, const uint32_t len)
{
        uint64_t j;

        struct {
                kasumi_f8_4_buffer_t fn;
                const char *name;
        } fn_ptrs = { mgr->f8_4_buffer, "KASUMI F8 4" };

        struct fn_args {
                const kasumi_key_sched_t *key_data;
                const uint64_t iv; const uint64_t iv2; const uint64_t iv3;
                const uint64_t iv4;
                const void *in; const void *in2; const void *in3;
                const void *in4;
                void *out; void *out2; void *out3; void *out4;
                const uint32_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, iv2, iv3, iv4, in, in2, in3, in4,
                  out, out2, out3, out4, len, IMB_ERR_NULL_EXP_KEY },
                { ctx, iv, iv2, iv3, iv4, NULL, in2, in3, in4,
                  out, out2, out3, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, iv3, iv4, in, NULL, in3, in4,
                  out, out2, out3, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, iv3, iv4, in, in2, NULL, in4,
                  out, out2, out3, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, iv3, iv4, in, in2, in3, NULL,
                  out, out2, out3, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, iv3, iv4, in, in2, in3, in4,
                  NULL, out2, out3, out4, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv2, iv3, iv4, in, in2, in3, in4,
                  out, NULL, out3, out4, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv2, iv3, iv4, in, in2, in3, in4,
                  out, out2, NULL, out4, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv2, iv3, iv4, in, in2, in3, in4,
                  out, out2, out3, NULL, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv2, iv3, iv4, in, in2, in3, in4,
                  out, out2, out3, out4, 0, IMB_ERR_CIPH_LEN },
        };
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->key_data, ap->iv, ap->iv2,
                           ap->iv3, ap->iv4, ap->in, ap->out,
                           ap->in2, ap->out2, ap->in3, ap->out3,
                           ap->in4, ap->out4, ap->len);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }
        return 0;
}


static int
test_kasumi_api_f8_n_buffer(struct IMB_MGR *mgr, const kasumi_key_sched_t *ctx,
                            const uint64_t *iv, const void *in, void **out,
                            const uint32_t *len, const uint32_t count)
{
        uint64_t j;

        struct {
                kasumi_f8_n_buffer_t fn;
                const char *name;
        } fn_ptrs = { mgr->f8_n_buffer, "KASUMI F8 N" };

        struct fn_args {
                const kasumi_key_sched_t *ctx;
                const uint64_t *iv;
                const void *in;
                void **out;
                const uint32_t *len;
                const uint32_t count;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, out, len, count, IMB_ERR_NULL_EXP_KEY },
                { ctx, NULL, in, out, len, count, IMB_ERR_NULL_IV },
                { ctx, iv, NULL, out, len, count, IMB_ERR_NULL_SRC },
                { ctx, iv, in, NULL, len, count, IMB_ERR_NULL_DST },
                { ctx, iv, in, out, 0, count, IMB_ERR_CIPH_LEN },
        };

        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->ctx, ap->iv, ap->in, ap->out,
                           ap->len, ap->count);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }

        return 0;
}

/* KASUMI-F9 1 Buffer tests */
static int
test_kasumi_f9_1_buffer(struct IMB_MGR *mgr, const kasumi_key_sched_t *key,
                        const void *in, void *tag, const uint32_t len)
{
        unsigned int i;
        const char func_name[] = "KASUMI-F9 1 BUFFER";

        struct fn_args {
                const kasumi_key_sched_t *key;
                const void *in;
                const uint32_t len;
                void *tag;
                const IMB_ERR exp_err;
        } fn_args[] = {
                {NULL, in, len, tag, IMB_ERR_NULL_EXP_KEY},
                {key, NULL, len, tag, IMB_ERR_NULL_SRC },
                {key, in, len, NULL, IMB_ERR_NULL_AUTH },
                {key, in, 0, tag, IMB_ERR_AUTH_LEN},
                {key, in, KASUMI_MAX_BITLEN + 1, tag, IMB_ERR_AUTH_LEN},
                {key, in, len, tag, 0},
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->f9_1_buffer(ap->key, ap->in, ap->len, ap->tag);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/* KASUMI-F9 1 Buffer User tests */
static int
test_kasumi_f9_1_buffer_user(struct IMB_MGR *mgr, const kasumi_key_sched_t *key,
                             const uint64_t iv, const void *in, void *tag,
                             const uint32_t len)
{
        unsigned int i;
        const char func_name[] = "KASUMI-F9 1 BUFFER USER";

        struct fn_args {
                const kasumi_key_sched_t *key;
                const uint64_t iv;
                const void *in;
                const uint32_t len;
                void *tag;
                const uint32_t dir;
                const IMB_ERR exp_err;
        } fn_args[] = {
                {NULL, iv, in, len, tag, 0, IMB_ERR_NULL_EXP_KEY},
                {key, iv, NULL, len, tag, 0, IMB_ERR_NULL_SRC },
                {key, iv, in, len, NULL, 0, IMB_ERR_NULL_AUTH },
                {key, iv, in, 0, tag, 0, IMB_ERR_AUTH_LEN},
                {key, iv, in, KASUMI_MAX_BITLEN + 1, tag, 0,
                 IMB_ERR_AUTH_LEN},
                {key, iv, in, len, tag, 0, 0},
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->f9_1_buffer_user(ap->key, ap->iv, ap->in, ap->len,
                                      ap->tag, ap->dir);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/* Test KASUMI Init key */
static int
test_kasumi_init_key_sched(struct IMB_MGR *mgr, const void *key,
                           kasumi_key_sched_t *f8_key_sched,
                           kasumi_key_sched_t *f9_key_sched)
{
        mgr->kasumi_init_f8_key_sched(NULL, f8_key_sched);
        if (unexpected_err(mgr, IMB_ERR_NULL_KEY, "KASUMI F8 Key init"))
                return 1;

        mgr->kasumi_init_f8_key_sched(key, NULL);
        if (unexpected_err(mgr, IMB_ERR_NULL_EXP_KEY, "KASUMI F8 Key init"))
                return 1;

        mgr->kasumi_init_f8_key_sched(key, f8_key_sched);
        if (unexpected_err(mgr, 0, "KASUMI F8 Key init"))
                return 1;

        mgr->kasumi_init_f9_key_sched(NULL, f9_key_sched);
        if (unexpected_err(mgr, IMB_ERR_NULL_KEY, "KASUMI F9 Key init"))
                return 1;

        mgr->kasumi_init_f9_key_sched(key, NULL);
        if (unexpected_err(mgr, IMB_ERR_NULL_EXP_KEY, "KASUMI F9 Key init"))
                return 1;

        mgr->kasumi_init_f8_key_sched(key, f9_key_sched);
        if (unexpected_err(mgr, 0, "KASUMI F9 Key init"))
                return 1;

        return 0;
}

/*
 * @brief Performs direct KASUMI API invalid param tests
 */
static int
test_kasumi_api(struct IMB_MGR *mgr)
{
        uint32_t text_len = 16;
        uint64_t buf[BUF_SIZE];
        uint32_t buf2[BUF_SIZE];
        int seg_err; /* segfault flag */
        kasumi_key_sched_t f8_key;
        kasumi_key_sched_t f9_key;
        uint8_t key[16];
        uint64_t iv = text_len; uint64_t iv2 = text_len;
        uint64_t iv3 = text_len; uint64_t iv4 = text_len;
        const void *in = buf; const void *in2 = buf;
        const void *in3 = buf; const void *in4 = buf;
        void *out = buf; void *out2 = buf;
        void *out3 = buf; void *out4 = buf;
        const void *iv_ptr = buf;
        const uint32_t *lens = buf2;
        const uint32_t offset = 0;
        const uint32_t count = 16;
        uint8_t tag[4];

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        if (test_kasumi_init_key_sched(mgr, key, &f8_key, &f9_key))
                return 1;

        if (test_kasumi_api_f8_1_buffer(mgr, &f8_key, iv, in, out, text_len))
                return 1;

        if (test_kasumi_api_f8_1_buffer_bit(mgr, &f8_key, iv, in, out,
                                            text_len, offset))
                return 1;

        if (test_kasumi_api_f8_2_buffer(mgr, &f8_key, iv, iv2, in, in2, out,
                                        out2, text_len, text_len))
                return 1;

        if (test_kasumi_api_f8_3_buffer(mgr, &f8_key, iv, iv2, iv3, in, in2,
                                        in3, out, out2, out3, text_len))
                return 1;

        if (test_kasumi_api_f8_4_buffer(mgr, &f8_key, iv, iv2, iv3, iv4, in,
                                        in2, in3, in4, out, out2, out3,
                                        out4, text_len))
                return 1;

        if (test_kasumi_api_f8_n_buffer(mgr, &f8_key, iv_ptr, in, out,
                                        lens, count))
                return 1;

        if (test_kasumi_f9_1_buffer(mgr, &f9_key, in, (void *)tag, text_len))
                return 1;

        if (test_kasumi_f9_1_buffer_user(mgr, &f9_key, iv, in, (void *)tag,
                                         text_len))
                return 1;

        return 0;
}


/* SNOW3G bit len Encrypt and Decrypt tests, single buffer */
static int
test_snow3g_f8_1_buffer_bit(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                            const uint32_t len,
                            const snow3g_key_schedule_t *ctx,
                            const uint8_t *iv, const uint32_t offset)
{
        uint64_t j;
        const uint32_t zero_msg_len = 0;

        struct snow3g_f8_1_buffer_bit_fn {
                snow3g_f8_1_buffer_bit_t func;
                const char *func_name;
        } fn_ptr = { mgr->snow3g_f8_1_buffer_bit,
                     "SNOW3G-UEA2 bitlen single buffer"
                   };

        struct fn_args {
                const snow3g_key_schedule_t *key_data;
                const uint8_t *iv;
                uint8_t *in;
                uint8_t *out;
                const uint32_t len;
                const uint32_t offset;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, out, len, offset, IMB_ERR_NULL_EXP_KEY },
                { ctx, NULL, in, out, len, offset, IMB_ERR_NULL_IV },
                { ctx, iv, NULL, out, len, offset, IMB_ERR_NULL_SRC },
                { ctx, iv, in, NULL, len, offset, IMB_ERR_NULL_DST },
                { ctx, iv, in, out, zero_msg_len, offset, IMB_ERR_CIPH_LEN }
        };

        memset(out, 0, len);
        memset(in, 0, len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptr.func(ap->key_data, ap->iv, ap->in, ap->out,
                            ap->len, ap->offset);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptr.func_name))
                        return 1;
        }
        /* Verify buffers not modified */
        if (memcmp(out, in, len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, fn_ptr.func_name);
                return 1;
        }
        printf(".");
        return 0;
}

/* SNOW3G Encrypt and Decrypt tests, single buffer */
static int
test_snow3g_f8_1_buffer(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint32_t len,
                        const snow3g_key_schedule_t *ctx,
                        const uint8_t *iv)
{
        uint64_t j;
        const uint32_t zero_msg_len = 0;
        const uint32_t invalid_msg_len = 1ULL << 30;

        struct snow3g_f8_1_buffer_fn {
                snow3g_f8_1_buffer_t func;
                const char *func_name;
        } fn_ptr = { mgr->snow3g_f8_1_buffer, "SNOW3G-UEA2 single buffer" };

        struct fn_args {
                const snow3g_key_schedule_t *key_data;
                const uint8_t *iv;
                uint8_t *in;
                uint8_t *out;
                const uint32_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, out, len, IMB_ERR_NULL_EXP_KEY },
                { ctx, NULL, in, out, len, IMB_ERR_NULL_IV },
                { ctx, iv, NULL, out, len, IMB_ERR_NULL_SRC },
                { ctx, iv, in, NULL, len, IMB_ERR_NULL_DST },
                { ctx, iv, in, out, invalid_msg_len, IMB_ERR_CIPH_LEN },
                { ctx, iv, in, out, zero_msg_len, IMB_ERR_CIPH_LEN }
        };

        memset(out, 0, len);
        memset(in, 0, len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptr.func(ap->key_data, ap->iv, ap->in, ap->out,
                            ap->len);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptr.func_name))
                        return 1;
        }
        /* Verify buffers not modified */
        if (memcmp(out, in, len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, fn_ptr.func_name);
                return 1;
        }
        printf(".");
        return 0;
}

/* SNOW3G Encrypt and Decrypt tests, 2 buffers */
static int
test_snow3g_f8_2_buffer(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint32_t len,
                        const snow3g_key_schedule_t *ctx,
                        const uint8_t *iv)
{
        uint8_t *in2 = in + len;
        uint8_t *out2 = in + len;
        uint8_t *iv2 = in + len;
        uint64_t j;
        const uint32_t zero_msg_len = 0;
        const uint32_t invalid_msg_len = 1ULL << 30;

        struct snow3g_f8_2_buffer_fn {
                snow3g_f8_2_buffer_t func;
                const char *func_name;
        } fn_ptr = { mgr->snow3g_f8_2_buffer, "SNOW3G-UEA2 2 buffers" };

        struct fn_args {
                const snow3g_key_schedule_t *key_data;
                const uint8_t *iv;
                const uint8_t *iv2;
                uint8_t *in;
                uint8_t *out;
                const uint32_t len;
                uint8_t *in2;
                uint8_t *out2;
                const uint32_t len2;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, iv2, in, out, len, in2, out2, len,
                  IMB_ERR_NULL_EXP_KEY },
                { ctx, NULL, iv2, in, out, len, in2, out2, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, NULL, in, out, len, in2, out2, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv2, NULL, out, len, in2, out2, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, in, out, len, NULL, out2, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv2, in, NULL, len, in2, out2, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv2, in, out, len, in2, NULL, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv2, in, out, invalid_msg_len, in2, out2, len,
                  IMB_ERR_CIPH_LEN },
                { ctx, iv, iv2, in, out, len, in2, out2, invalid_msg_len,
                  IMB_ERR_CIPH_LEN },
                { ctx, iv, iv2, in, out, zero_msg_len, in2, out2, len,
                  IMB_ERR_CIPH_LEN },
                { ctx, iv, iv2, in, out, len, in2, out2, zero_msg_len,
                  IMB_ERR_CIPH_LEN }
        };

        memset(out, 0, 2*len);
        memset(in, 0, 2*len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptr.func(ap->key_data, ap->iv, ap->iv2, ap->in, ap->out,
                                ap->len, ap->in2, ap->out2, ap->len2);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptr.func_name))
                        return 1;
        }
        /* Verify buffers not modified */
        if (memcmp(out, in, 2*len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, fn_ptr.func_name);
                return 1;
        }
        printf(".");
        return 0;
}

/* SNOW3G Encrypt and Decrypt tests, 8 buffers */
static int
test_snow3g_f8_8_buffer(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint32_t len,
                        const snow3g_key_schedule_t *ctx,
                        const uint8_t *iv)
{
        uint64_t j;
        const uint32_t invalid_msg_len = 1ULL << 30;
        uint8_t buffers_num = 8;

        uint8_t *in2 = in + len;
        uint8_t *out2 = in + len;
        uint8_t *in3 = in + 2*len;
        uint8_t *out3 = in + 2*len;
        uint8_t *in4 = in + 3*len;
        uint8_t *out4 = in + 3*len;
        uint8_t *in5 = in + 4*len;
        uint8_t *out5 = in + 4*len;
        uint8_t *in6 = in + 5*len;
        uint8_t *out6 = in + 5*len;
        uint8_t *in7 = in + 6*len;
        uint8_t *out7 = in + 6*len;
        uint8_t *in8 = in + 7*len;
        uint8_t *out8 = in + 7*len;

        struct snow3g_f8_8_buffer_fn {
                snow3g_f8_8_buffer_t func;
                const char *func_name;
        } fn_ptr = { mgr->snow3g_f8_8_buffer, "SNOW3G-UEA2 8 buffers" };

        struct fn_args {
                const snow3g_key_schedule_t *key_data;
                const uint8_t *iv1;
                const uint8_t *iv2;
                const uint8_t *iv3;
                const uint8_t *iv4;
                const uint8_t *iv5;
                const uint8_t *iv6;
                const uint8_t *iv7;
                const uint8_t *iv8;
                uint8_t *in1; uint8_t *out1; const uint32_t len1;
                uint8_t *in2; uint8_t *out2; const uint32_t len2;
                uint8_t *in3; uint8_t *out3; const uint32_t len3;
                uint8_t *in4; uint8_t *out4; const uint32_t len4;
                uint8_t *in5; uint8_t *out5; const uint32_t len5;
                uint8_t *in6; uint8_t *out6; const uint32_t len6;
                uint8_t *in7; uint8_t *out7; const uint32_t len7;
                uint8_t *in8; uint8_t *out8; const uint32_t len8;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_EXP_KEY },
                { ctx, NULL, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, NULL, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv, NULL, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, NULL, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, iv, NULL, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, iv, iv, NULL, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, iv, iv, iv, NULL, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, iv, iv, iv, iv, NULL, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, NULL, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  NULL, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, NULL, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, NULL, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, NULL, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, NULL, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, NULL, out7, len, in8, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, NULL, out8, len,
                  IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, NULL, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, NULL, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, NULL, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, NULL, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, NULL,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, NULL, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, NULL, len, in8, out8, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, NULL, len,
                  IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, invalid_msg_len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8, len,
                  IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, invalid_msg_len, in3, out3, len, in4, out4, len,
                  in5, out5, len, in6, out6, len, in7, out7, len, in8, out8,
                  len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, invalid_msg_len, in4, out4, len,
                  in5, out5, len, in6, out6, len, in7, out7, len, in8, out8,
                  len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, invalid_msg_len,
                  in5, out5, len, in6, out6, len, in7, out7, len, in8, out8,
                  len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  invalid_msg_len, in6, out6, len, in7, out7, len, in8, out8,
                  len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, invalid_msg_len, in7, out7, len, in8, out8,
                  len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, invalid_msg_len, in8, out8,
                  len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, iv, iv, iv, iv, in, out, len,
                  in2, out2, len, in3, out3, len, in4, out4, len, in5, out5,
                  len, in6, out6, len, in7, out7, len, in8, out8,
                  invalid_msg_len, IMB_ERR_CIPH_LEN },
        };

        memset(out, 0, buffers_num * len);
        memset(in, 0, buffers_num * len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptr.func(ap->key_data, ap->iv1, ap->iv2, ap->iv3, ap->iv4,
                            ap->iv5, ap->iv6, ap->iv7, ap->iv8, ap->in1,
                            ap->out1, ap->len1, ap->in2, ap->out2, ap->len2,
                            ap->in3, ap->out3, ap->len3, ap->in4, ap->out4,
                            ap->len4, ap->in5, ap->out5, ap->len5, ap->in6,
                            ap->out6, ap->len6, ap->in7, ap->out7, ap->len7,
                            ap->in8, ap->out8, ap->len8);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptr.func_name))
                        return 1;
        }

        /* Verify buffers not modified */
        if (memcmp(out, in, buffers_num*len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, fn_ptr.func_name);
                return 1;
        }
        printf(".");
        return 0;
}

/* SNOW3G Encrypt and Decrypt tests, 4 buffers */
static int
test_snow3g_f8_4_buffer(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint32_t len,
                        const snow3g_key_schedule_t *ctx,
                        const uint8_t *iv)
{
        uint64_t j;
        const uint32_t invalid_msg_len = 1ULL << 30;
        uint8_t buffers_num = 4;

        uint8_t *in2 = in + len;
        uint8_t *out2 = in + len;
        uint8_t *in3 = in + 2*len;
        uint8_t *out3 = in + 2*len;
        uint8_t *in4 = in + 3*len;
        uint8_t *out4 = in + 3*len;

        struct snow3g_f8_4_buffer_fn {
                snow3g_f8_4_buffer_t func;
                const char *func_name;
        } fn_ptr = { mgr->snow3g_f8_4_buffer, "SNOW3G-UEA2 4 buffers" };

        struct fn_args {
                const snow3g_key_schedule_t *key_data;
                const uint8_t *iv1;
                const uint8_t *iv2;
                const uint8_t *iv3;
                const uint8_t *iv4;
                uint8_t *in1; uint8_t *out1; const uint32_t len1;
                uint8_t *in2; uint8_t *out2; const uint32_t len2;
                uint8_t *in3; uint8_t *out3; const uint32_t len3;
                uint8_t *in4; uint8_t *out4; const uint32_t len4;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, iv, iv, iv, in, out, len, in2, out2, len,
                  in3, out3, len, in4, out4, len, IMB_ERR_NULL_EXP_KEY },
                { ctx, NULL, iv, iv, iv, in, out, len, in2, out2, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_IV },
                { ctx, iv, NULL, iv, iv, in, out, len, in2, out2, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_IV },
                { ctx, iv, iv, NULL, iv, in, out, len, in2, out2, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, NULL, in, out, len, in2, out2, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_IV },
                { ctx, iv, iv, iv, iv, NULL, out, len, in2, out2, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, in, out, len, NULL, out2, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, in, out, len, in2, out2, len, NULL,
                  out3, len, in4, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, in, out, len, in2, out2, len, in3,
                  out3, len, NULL, out4, len, IMB_ERR_NULL_SRC },
                { ctx, iv, iv, iv, iv, in, NULL, len, in2, out2, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, in, out, len, in2, NULL, len, in3,
                  out3, len, in4, out4, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, in, out, len, in2, out2, len, in3,
                  NULL, len, in4, out4, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, in, out, len, in2, out2, len, in3,
                  out3, len, in4, NULL, len, IMB_ERR_NULL_DST },
                { ctx, iv, iv, iv, iv, in, out, invalid_msg_len, in2, out2,
                len, in3, out3, len, in4, out4, len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, in, out, len, in2, out2,
                invalid_msg_len, in3, out3, len, in4, out4, len,
                IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, in, out, len, in2, out2, len, in3, out3,
                invalid_msg_len, in4, out4, len, IMB_ERR_CIPH_LEN },
                { ctx, iv, iv, iv, iv, in, out, len, in2, out2, len, in3, out3,
                len, in4, out4, invalid_msg_len, IMB_ERR_CIPH_LEN }
        };

        memset(out, 0, buffers_num * len);
        memset(in, 0, buffers_num * len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptr.func(ap->key_data, ap->iv1, ap->iv2, ap->iv3, ap->iv4,
                            ap->in1, ap->out1, ap->len1, ap->in2, ap->out2,
                            ap->len2, ap->in3, ap->out3, ap->len3, ap->in4,
                            ap->out4, ap->len4);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptr.func_name))
                        return 1;
        }

        /* Verify buffers not modified */
        if (memcmp(out, in, buffers_num*len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, fn_ptr.func_name);
                return 1;
        }
        printf(".");
        return 0;
}

/* SNOW3G Encrypt and Decrypt tests, n buffers */
static int
test_snow3g_f8_n_buffer(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint32_t len,
                        const snow3g_key_schedule_t *ctx,
                        const uint8_t *iv)
{
        uint64_t j;
        const uint32_t invalid_msg_len = 1ULL << 30;

        const uint8_t *pIV[SNOW3G_N_TEST_COUNT];
        uint32_t packetLen[SNOW3G_N_TEST_COUNT];
        uint8_t *pSrcBuff[SNOW3G_N_TEST_COUNT];
        uint8_t *pDstBuff[SNOW3G_N_TEST_COUNT];
        uint32_t badPacketLen[SNOW3G_N_TEST_COUNT];

        for (j = 0; j < SNOW3G_N_TEST_COUNT; j++) {
                pIV[j] = iv;
                pSrcBuff[j] = in + len * j;
                pDstBuff[j] = out + len * j;
                packetLen[j] = len;
                badPacketLen[j] = invalid_msg_len;
        }

        struct snow3g_f8_n_buffer_fn {
                snow3g_f8_n_buffer_t func;
                const char *func_name;
        } fn_ptr = { mgr->snow3g_f8_n_buffer, "SNOW3G-UEA2 n buffers" };

        struct fn_args {
                const snow3g_key_schedule_t *key_data;
                const uint8_t **ivs;
                uint8_t **ins;
                uint8_t **outs;
                uint32_t *lens;
                const uint32_t count;
                const IMB_ERR exp_err;
        } fn_args[] = { { NULL, pIV, pSrcBuff, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_EXP_KEY },
                        { ctx, NULL, pSrcBuff, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_IV },
                        { NULL, pIV, pSrcBuff, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_EXP_KEY },
                        { ctx, pIV, NULL, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_SRC },
                        { ctx, pIV, pSrcBuff, NULL, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_DST },
                        { ctx, pIV, pSrcBuff, pDstBuff, NULL,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_CIPH_LEN },
                        { ctx, pIV, pSrcBuff, pDstBuff, badPacketLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_CIPH_LEN } };

        memset(out, 0, SNOW3G_N_TEST_COUNT * len);
        memset(in, 0, SNOW3G_N_TEST_COUNT * len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptr.func(ap->key_data, (const void * const *)ap->ivs,
                (const void * const *)ap->ins, (void **)ap->outs, ap->lens,
                            ap->count);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptr.func_name))
                        return 1;
        }

        /* Verify buffers not modified */
        if (memcmp(out, in, SNOW3G_N_TEST_COUNT * len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, fn_ptr.func_name);
                return 1;
        }
        printf(".");
        return 0;
}


/* SNOW3G Encrypt and Decrypt tests, n/8 buffers, multikey */
static int
test_snow3g_f8_n_buffer_multikey(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint32_t len,
                        const snow3g_key_schedule_t *ctx,
                        const uint8_t *iv)
{
        uint64_t j;
        const uint32_t invalid_msg_len = 1ULL << 30;
        const snow3g_key_schedule_t *pKeySched[SNOW3G_N_TEST_COUNT];
        const snow3g_key_schedule_t *pKeySchedInvalid[SNOW3G_N_TEST_COUNT];
        const uint8_t *pIV[SNOW3G_N_TEST_COUNT];
        uint32_t packetLen[SNOW3G_N_TEST_COUNT];
        uint8_t *pSrcBuff[SNOW3G_N_TEST_COUNT];
        uint8_t *pDstBuff[SNOW3G_N_TEST_COUNT];
        uint32_t badPacketLen[SNOW3G_N_TEST_COUNT];

        for (j = 0; j < SNOW3G_N_TEST_COUNT; j++) {
                pIV[j] = iv;
                pSrcBuff[j] = in + len * j;
                pDstBuff[j] = out + len * j;
                packetLen[j] = len;
                badPacketLen[j] = invalid_msg_len;
                pKeySched[j] = ctx;
                pKeySchedInvalid[j] = NULL;
        }

        struct fn_args {
                const snow3g_key_schedule_t **key_data;
                const uint8_t **ivs;
                uint8_t **ins;
                uint8_t **outs;
                uint32_t *lens;
                const uint32_t count;
                const IMB_ERR exp_err;
        } fn_args[] = { { NULL, pIV, pSrcBuff, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_EXP_KEY },
                        { pKeySchedInvalid, pIV, pSrcBuff, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_EXP_KEY },
                        { pKeySched, NULL, pSrcBuff, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_IV },
                        { NULL, pIV, pSrcBuff, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_EXP_KEY },
                        { pKeySched, pIV, NULL, pDstBuff, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_SRC },
                        { pKeySched, pIV, pSrcBuff, NULL, packetLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_NULL_DST },
                        { pKeySched, pIV, pSrcBuff, pDstBuff, NULL,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_CIPH_LEN },
                        { pKeySched, pIV, pSrcBuff, pDstBuff, badPacketLen,
                          SNOW3G_N_TEST_COUNT, IMB_ERR_CIPH_LEN } };

        memset(out, 0, SNOW3G_N_TEST_COUNT * len);
        memset(in, 0, SNOW3G_N_TEST_COUNT * len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                mgr->snow3g_f8_n_buffer_multikey(ap->key_data,
                                                 (const void * const *)ap->ivs,
                                                 (const void * const *)ap->ins,
                                                 (void **)ap->outs, ap->lens,
                                                 ap->count);
                if (unexpected_err(mgr, ap->exp_err,
                                   "SNOW3G-UEA2 n buffers multikey"))
                        return 1;
                mgr->snow3g_f8_8_buffer_multikey(ap->key_data,
                                                 (const void * const *)ap->ivs,
                                                 (const void * const *)ap->ins,
                                                 (void **)ap->outs, ap->lens);
                if (unexpected_err(mgr, ap->exp_err,
                                   "SNOW3G-UEA2 8 buffers multikey"))
                        return 1;
        }

        /* Verify buffers not modified */
        if (memcmp(out, in, SNOW3G_N_TEST_COUNT * len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, "SNOW3G-UEA2 n buffers multikey");
                return 1;
        }
        printf(".");
        return 0;
}

/* SNOW3G Authentication tests, single buffer */
static int
test_snow3g_f9_1_buffer(struct IMB_MGR *mgr, uint8_t *in, uint8_t *out,
                        const uint64_t len,
                        const snow3g_key_schedule_t *ctx,
                        const uint8_t *iv)
{
        uint64_t j;
        const uint64_t invalid_msg_len = 1ULL << 32;

        struct snow3g_f9_1_buffer_fn {
                snow3g_f9_1_buffer_t func;
                const char *func_name;
        } fn_ptr = { mgr->snow3g_f9_1_buffer,
                    "SNOW3G-UIA2 single buffer" };

        struct fn_args {
                const snow3g_key_schedule_t *key_data;
                const uint8_t *iv;
                uint8_t *in;
                const uint64_t len;
                uint8_t *out;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, iv, in, len, out, IMB_ERR_NULL_EXP_KEY },
                { ctx, NULL, in, len, out, IMB_ERR_NULL_IV },
                { ctx, iv, NULL, len, out, IMB_ERR_NULL_SRC },
                { ctx, iv, in, invalid_msg_len, out,  IMB_ERR_AUTH_LEN },
                { ctx, iv, in, len, NULL, IMB_ERR_NULL_AUTH }
        };

        memset(out, 0, len);
        memset(in, 0, len);

        /* Iterate over args */
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptr.func(ap->key_data, ap->iv, ap->in, ap->len, ap->out);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptr.func_name))
                        return 1;
        }
        /* Verify buffers not modified */
        if (memcmp(out, in, len) != 0) {
                printf("%s: %s, invalid param test failed!\n",
                        __func__, fn_ptr.func_name);
                return 1;
        }
        printf(".");
        return 0;
}

/* Test SNOW3G Init key */
static int
test_snow3g_init_key_sched(struct IMB_MGR *mgr, uint8_t *key)
{
        snow3g_key_schedule_t exp_key;

        mgr->snow3g_init_key_sched(NULL, &exp_key);
        if (unexpected_err(mgr, IMB_ERR_NULL_KEY, "SNOW3G Key init"))
                return 1;

        mgr->snow3g_init_key_sched(key, NULL);
        if (unexpected_err(mgr, IMB_ERR_NULL_EXP_KEY, "SNOW3G Key init"))
                return 1;

        return 0;
}
/*
 * @brief Performs direct SNOW3G API invalid param tests
 */
static int
test_snow3g_api(struct IMB_MGR *mgr)
{
        const uint32_t text_len = 16;
        uint8_t out_buf[SNOW3G_TOTAL_BUF_SIZE];
        uint8_t zero_buf[SNOW3G_TOTAL_BUF_SIZE];
        int seg_err; /* segfault flag */
        const snow3g_key_schedule_t ctx[NUM_BUFS];
        const uint8_t iv[SNOW3G_TOTAL_BUF_SIZE];
        const uint32_t offset = 0;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        /* SNOW3G Encrypt and Decrypt tests */
        if (test_snow3g_f8_1_buffer_bit(mgr, zero_buf, out_buf, text_len,
                                        ctx, iv, offset))
                return 1;
        if (test_snow3g_f8_1_buffer(mgr, zero_buf, out_buf, text_len,
                                    ctx, iv))
                return 1;
        if (test_snow3g_f8_2_buffer(mgr, zero_buf, out_buf, text_len,
                                    ctx, iv))
                return 1;
        if (test_snow3g_f8_8_buffer(mgr, zero_buf, out_buf, text_len,
                                    ctx, iv))
                return 1;
        if (test_snow3g_f8_4_buffer(mgr, zero_buf, out_buf, text_len,
                                    ctx, iv))
                return 1;
        if (test_snow3g_f9_1_buffer(mgr, zero_buf, out_buf, text_len,
                                    ctx, iv))
                return 1;
        if (test_snow3g_init_key_sched(mgr, zero_buf))
                return 1;
        if (test_snow3g_f8_n_buffer(mgr, zero_buf, out_buf, text_len,
                                    ctx, iv))
                return 1;
        if (test_snow3g_f8_n_buffer_multikey(mgr, zero_buf, out_buf, text_len,
                                    ctx, iv))
                return 1;
        return 0;
}

/*
 * @brief Performs direct hec API invalid param tests
 */
static int
test_hec_api(struct IMB_MGR *mgr)
{
        uint8_t out_buf[8];
        uint8_t zero_buf[8];
        int seg_err; /* segfault flag */

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, sizeof(out_buf));
        memset(zero_buf, 0, sizeof(zero_buf));

        /* Test HEC API's */
        IMB_HEC_32(mgr, NULL);
        if (unexpected_err(mgr, IMB_ERR_NULL_SRC, "HEC 32"))
                return 1;

        IMB_HEC_64(mgr, NULL);
        if (unexpected_err(mgr, IMB_ERR_NULL_SRC, "HEC 64"))
                return 1;

        return 0;
}

/*
 * @brief Performs direct CRC API invalid param tests
 */
static int
test_crc_api(struct IMB_MGR *mgr)
{
        uint8_t in_buf[BUF_SIZE] = { 0 };
        int seg_err; /* segfault flag */
        unsigned i, j;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        /* Test CRC API's */
        struct {
                crc32_fn_t fn;
                const char *name;
        } fn1_ptrs[] = {
                { mgr->crc32_ethernet_fcs, "CRC32 ETHERNET FCS" },
                { mgr->crc32_sctp, "CRC32 SCTP" },
                { mgr->crc32_wimax_ofdma_data, "CRC32 WIMAX OFDMA DATA" },
                { mgr->crc24_lte_a, "CRC24 LTE A" },
                { mgr->crc24_lte_b, "CRC24 LTE B" },
                { mgr->crc16_x25, "CRC16 X25" },
                { mgr->crc16_fp_data, "CRC16 FP DATA" },
                { mgr->crc11_fp_header, "CRC11 FP HEADER" },
                { mgr->crc10_iuup_data, "CRC10 IUUP DATA" },
                { mgr->crc8_wimax_ofdma_hcs, "CRC8 WIMAX OFDMA HCS" },
                { mgr->crc7_fp_header, "CRC7 FP HEADER" },
                { mgr->crc6_iuup_header, "CRC6 IUUP HEADER" },
        };

        struct {
                const void *src;
                const uint64_t len;
                IMB_ERR exp_err;
        } fn1_args[] = {
                { NULL, sizeof(in_buf), IMB_ERR_NULL_SRC },
                { NULL, 0, 0 },
                { in_buf, sizeof(in_buf), 0 },
        };

        for (i = 0; i < DIM(fn1_ptrs); i++) {
                for (j = 0; j < DIM(fn1_args); j++) {
                        fn1_ptrs[i].fn(fn1_args[j].src,
                                       fn1_args[j].len);

                        if (unexpected_err(mgr, fn1_args[j].exp_err,
                                           fn1_ptrs[i].name))
                        return 1;
                }
        }

        return 0;
}

/* CHACHA20-POLY1305 Init tests */
static int
test_chacha_poly_init(struct IMB_MGR *mgr,
                      struct chacha20_poly1305_context_data *ctx,
                      const void *key, const void *iv,
                      const uint8_t *aad)
{
        unsigned int i;
        const uint64_t aad_len = 28;
        const char func_name[] = "CHACHA20-POLY1305 INIT";

        struct fn_args {
                const void *key;
                struct chacha20_poly1305_context_data *ctx;
                const uint8_t *iv;
                const uint8_t *aad;
                uint64_t aad_len;
                IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, iv, aad, aad_len, IMB_ERR_NULL_KEY },
                { key, NULL, iv, aad, aad_len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, aad, aad_len, IMB_ERR_NULL_IV },
                { key, ctx, iv, NULL, aad_len, IMB_ERR_NULL_AAD },
                { key, ctx, iv, aad, 0, 0 },
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->chacha20_poly1305_init(ap->key, ap->ctx, ap->iv,
                                            ap->aad, ap->aad_len);
                if (unexpected_err(mgr, ap->exp_err,
                                   func_name))
                        return 1;
        }

        return 0;
}

/* CHACHA20-POLY1305 Enc/dec update tests */
static int
test_chacha_poly_enc_dec_update(struct IMB_MGR *mgr,
                      struct chacha20_poly1305_context_data *ctx,
                      const void *key)
{
        unsigned int i;
        uint8_t in[BUF_SIZE];
        uint8_t out[BUF_SIZE];
        uint32_t len = BUF_SIZE;

        struct chacha_poly_enc_dec_update_fn {
                chacha_poly_enc_dec_update_t func;
                const char *func_name;
        } fn_ptrs[] = {
             { mgr->chacha20_poly1305_enc_update,
               "CHACHA20-POLY1305 ENC UPDATE" },
             { mgr->chacha20_poly1305_dec_update,
               "CHACHA20-POLY1305 DEC UPDATE" },
        };

        struct fn_args {
                const void *key;
                struct chacha20_poly1305_context_data *ctx;
                uint8_t *out;
                uint8_t *in;
                const uint64_t len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, ctx, out, in, len, IMB_ERR_NULL_KEY },
                { key, NULL, out, in, len, IMB_ERR_NULL_CTX },
                { key, ctx, NULL, in, len, IMB_ERR_NULL_DST },
                { key, ctx, out, NULL, len, IMB_ERR_NULL_SRC },
                { key, ctx, NULL, NULL, 0, 0 },
                { key, ctx, out, in, 0, 0 },
        };

        /* Iterate over functions */
        for (i = 0; i < DIM(fn_ptrs); i++) {
                unsigned int j;

                /* Iterate over args */
                for (j = 0; j < DIM(fn_args); j++) {
                        const struct fn_args *ap = &fn_args[j];

                        fn_ptrs[i].func(ap->key, ap->ctx, ap->out,
                                        ap->in, ap->len);
                        if (unexpected_err(mgr, ap->exp_err,
                                           fn_ptrs[i].func_name))
                                return 1;
                }
        }

        return 0;
}

/* CHACHA20-POLY1305 Finalize tests */
static int
test_chacha_poly_finalize(struct IMB_MGR *mgr,
                          struct chacha20_poly1305_context_data *ctx)
{
        unsigned int i;
        uint8_t tag[16];
        const uint32_t tag_len = 16;
        const char func_name[] = "CHACHA20-POLY1305 FINALIZE";

        struct fn_args {
                struct chacha20_poly1305_context_data *ctx;
                uint8_t *tag;
                const uint64_t tag_len;
                const IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, tag, tag_len, IMB_ERR_NULL_CTX },
                { ctx, NULL, tag_len, IMB_ERR_NULL_AUTH },
                { ctx, tag, 0, IMB_ERR_AUTH_TAG_LEN },
                { ctx, tag, 17, IMB_ERR_AUTH_TAG_LEN },
        };

        /* Iterate over args */
        for (i = 0; i < DIM(fn_args); i++) {
                const struct fn_args *ap = &fn_args[i];

                mgr->chacha20_poly1305_finalize(ap->ctx,
                                ap->tag, ap->tag_len);
                if (unexpected_err(mgr, ap->exp_err, func_name))
                        return 1;
        }

        return 0;
}

/*
 * @brief Performs direct CHACHA-POLY API invalid param tests
 */
static int
test_chacha_poly_api(struct IMB_MGR *mgr)
{
        const uint8_t key[32];
        const uint8_t iv[12];
        const uint8_t aad[20];
        struct chacha20_poly1305_context_data ctx;
        int seg_err; /* segfault flag */

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        /* CHACHA20-POLY1305 Init */
        if (test_chacha_poly_init(mgr, &ctx, key, iv, aad))
                return 1;

        /* CHACHA20-POLY1305 Encrypt and Decrypt update */
        if (test_chacha_poly_enc_dec_update(mgr, &ctx, key))
                return 1;

        /* CHACHA20-POLY1305 Finalize */
        if (test_chacha_poly_finalize(mgr, &ctx))
                return 1;

        return 0;
}
static int
xcbc_keyexp_test(struct IMB_MGR *mgr, const void *key,
                          void *k1_exp, void *k2, void *k3)
{
        int seg_err; /* segfault flag */
        unsigned j;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        struct {
                xcbc_keyexp_t fn;
                const char *name;
        } fn_ptrs = { mgr->xcbc_keyexp, "XCBC KEYEXP" };

        struct fn_args {
                const void *key;
                void *k1_exp;
                void *k2;
                void *k3;
                IMB_ERR exp_err;
        } fn_args[] = {
                { NULL, k1_exp, k2, k3, IMB_ERR_NULL_KEY },
                { key, NULL, k2, k3, IMB_ERR_NULL_EXP_KEY },
                { key, k1_exp, NULL, k3, IMB_ERR_NULL_EXP_KEY },
                { key, k1_exp, k2, NULL, IMB_ERR_NULL_EXP_KEY },
                { key, k1_exp, k2, k3, 0 },
        };
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->key, ap->k1_exp,
                              ap->k2, ap->k3);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }

        return 0;
}

static int
test_xcbc_keyexp_api(struct IMB_MGR *mgr)
{
        uint8_t buf[BUF_SIZE];
        int seg_err; /* segfault flag */
        const void *key = buf;
        void *k1_exp = buf;
        void *k2 = buf;
        void *k3 = buf;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        if (xcbc_keyexp_test(mgr, key, k1_exp, k2, k3))
                return 1;

        return 0;
}

static int
des_keysched_test(struct IMB_MGR *mgr, uint64_t *ks, void *key)
{
        int seg_err; /* segfault flag */
        unsigned j;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        struct {
                des_keysched_t fn;
                const char *name;
        } fn_ptrs = { mgr->des_key_sched, "DES KEYSCHED" };

        struct fn_args {
                void *key;
                uint64_t *ks;
                IMB_ERR exp_err;
        } fn_args[] = {
                       { NULL, ks, IMB_ERR_NULL_KEY },
                       { key, NULL, IMB_ERR_NULL_EXP_KEY },
                       { key, ks, 0 },
        };
        for (j = 0; j < DIM(fn_args); j++) {
                const struct fn_args *ap = &fn_args[j];

                fn_ptrs.fn(ap->ks, ap->key);
                if (unexpected_err(mgr, ap->exp_err,
                                   fn_ptrs.name))
                        return 1;
        }

        return 0;
}

static int
test_des_keysched_api(struct IMB_MGR *mgr)
{
        uint64_t buf[BUF_SIZE] = {0};
        int seg_err; /* segfault flag */
        void *key = buf;
        uint64_t *ks = buf;

        seg_err = setjmp(dir_api_param_env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        if (des_keysched_test(mgr, ks, key))
                return 1;

        return 0;
}

int
direct_api_param_test(struct IMB_MGR *mb_mgr)
{
        struct test_suite_context ts;
        int errors = 0, run = 0;
#ifndef DEBUG
#if defined(__linux__)
        sighandler_t handler;
#else
        void *handler;
#endif
#endif
        printf("Extended Invalid Direct API arguments test:\n");
        test_suite_start(&ts, "INVALID-ARGS");

#ifndef DEBUG
        handler = signal(SIGSEGV, seg_handler);
#endif

        if ((mb_mgr->features & IMB_FEATURE_SAFE_PARAM) == 0) {
                printf("SAFE_PARAM feature disabled, "
                       "skipping remaining tests\n");
                goto dir_api_exit;
        }

        errors += test_gcm_api(mb_mgr);
        run++;

        errors += test_key_exp_gen_api(mb_mgr);
        run++;

        errors += test_hash_api(mb_mgr);
        run++;

        errors += test_aes_api(mb_mgr);
        run++;

        errors += test_zuc_api(mb_mgr);
        run++;

        errors += test_kasumi_api(mb_mgr);
        run++;

        errors += test_snow3g_api(mb_mgr);
        run++;

        errors += test_hec_api(mb_mgr);
        run++;

        errors += test_crc_api(mb_mgr);
        run++;

        errors += test_chacha_poly_api(mb_mgr);
        run++;

        errors += test_cmac_subkey_gen_api(mb_mgr);
        run++;

        errors += test_xcbc_keyexp_api(mb_mgr);
        run++;

        errors += test_des_keysched_api(mb_mgr);
        run++;

        test_suite_update(&ts, run - errors, errors);

 dir_api_exit:
        errors = test_suite_end(&ts);

#ifndef DEBUG
        signal(SIGSEGV, handler);
#endif
	return errors;
}
