/*****************************************************************************
 Copyright (c) 2019-2021, Intel Corporation

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

#ifdef _WIN32
#define __func__ __FUNCTION__
#endif

int
direct_api_test(struct IMB_MGR *mb_mgr);

/* Used to restore environment after potential segfaults */
jmp_buf env;

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
        longjmp(env, 1); /* reset env */
}
#endif /* DEBUG */

/*
 * @brief Performs direct GCM API invalid param tests
 */
static int
test_gcm_api(struct IMB_MGR *mgr)
{
        const uint32_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        struct gcm_key_data *key_data = (struct gcm_key_data *)out_buf;
        int seg_err; /* segfault flag */

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        /* GCM Encrypt API tests */
        IMB_AES128_GCM_ENC(mgr, NULL, NULL, NULL, NULL, -1,
                           NULL, NULL, -1, NULL, -1);
        IMB_AES128_GCM_ENC(mgr, NULL, NULL, out_buf, zero_buf,
                           text_len, NULL, NULL, -1, NULL, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_ENC, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_ENC(mgr, NULL, NULL, NULL, NULL, -1,
                           NULL, NULL, -1, NULL, -1);
        IMB_AES192_GCM_ENC(mgr, NULL, NULL, out_buf, zero_buf,
                           text_len, NULL, NULL, -1, NULL, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_ENC, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_ENC(mgr, NULL, NULL, NULL, NULL, -1,
                           NULL, NULL, -1, NULL, -1);
        IMB_AES256_GCM_ENC(mgr, NULL, NULL, out_buf, zero_buf,
                           text_len, NULL, NULL, -1, NULL, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_ENC, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        /* GCM Decrypt API tests */
        IMB_AES128_GCM_DEC(mgr, NULL, NULL, NULL, NULL, -1,
                           NULL, NULL, -1, NULL, -1);
        IMB_AES128_GCM_DEC(mgr, NULL, NULL, out_buf, zero_buf,
                           text_len, NULL, NULL, -1, NULL, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_DEC, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_ENC(mgr, NULL, NULL, NULL, NULL, -1,
                           NULL, NULL, -1, NULL, -1);
        IMB_AES192_GCM_ENC(mgr, NULL, NULL, out_buf, zero_buf,
                           text_len, NULL, NULL, -1, NULL, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_DEC, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_DEC(mgr, NULL, NULL, NULL, NULL, -1,
                           NULL, NULL, -1, NULL, -1);
        IMB_AES256_GCM_DEC(mgr, NULL, NULL, out_buf, zero_buf,
                           text_len, NULL, NULL, -1, NULL, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_DEC, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        /* GCM Init tests */
        IMB_AES128_GCM_INIT(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES128_GCM_INIT(mgr, NULL, (struct gcm_context_data *)out_buf,
                            NULL, NULL, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_INIT, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_INIT(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES192_GCM_INIT(mgr, NULL, (struct gcm_context_data *)out_buf,
                            NULL, NULL, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_INIT, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_INIT(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES256_GCM_INIT(mgr, NULL, (struct gcm_context_data *)out_buf,
                            NULL, NULL, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_INIT, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        /* GCM Encrypt update tests */
        IMB_AES128_GCM_ENC_UPDATE(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES128_GCM_ENC_UPDATE(mgr, NULL, NULL, out_buf, zero_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_ENC_UPDATE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_ENC_UPDATE(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES192_GCM_ENC_UPDATE(mgr, NULL, NULL, out_buf, zero_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_ENC_UPDATE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_ENC_UPDATE(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES256_GCM_ENC_UPDATE(mgr, NULL, NULL, out_buf, zero_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_ENC_UPDATE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        /* GCM Decrypt update tests */
        IMB_AES128_GCM_DEC_UPDATE(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES128_GCM_DEC_UPDATE(mgr, NULL, NULL, out_buf, zero_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_DEC_UPDATE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_DEC_UPDATE(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES192_GCM_DEC_UPDATE(mgr, NULL, NULL, out_buf, zero_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_DEC_UPDATE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_DEC_UPDATE(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES256_GCM_DEC_UPDATE(mgr, NULL, NULL, out_buf, zero_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_DEC_UPDATE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        /* GCM Encrypt complete tests */
        IMB_AES128_GCM_ENC_FINALIZE(mgr, NULL, NULL, NULL, -1);
        IMB_AES128_GCM_ENC_FINALIZE(mgr, NULL, NULL, out_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_ENC_FINALIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_ENC_FINALIZE(mgr, NULL, NULL, NULL, -1);
        IMB_AES192_GCM_ENC_FINALIZE(mgr, NULL, NULL, out_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_ENC_FINALIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_ENC_FINALIZE(mgr, NULL, NULL, NULL, -1);
        IMB_AES256_GCM_ENC_FINALIZE(mgr, NULL, NULL, out_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_ENC_FINALIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        /* GCM Decrypt complete tests */
        IMB_AES128_GCM_DEC_FINALIZE(mgr, NULL, NULL, NULL, -1);
        IMB_AES128_GCM_DEC_FINALIZE(mgr, NULL, NULL, out_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_DEC_FINALIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_DEC_FINALIZE(mgr, NULL, NULL, NULL, -1);
        IMB_AES192_GCM_DEC_FINALIZE(mgr, NULL, NULL, out_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_DEC_FINALIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_DEC_FINALIZE(mgr, NULL, NULL, NULL, -1);
        IMB_AES256_GCM_DEC_FINALIZE(mgr, NULL, NULL, out_buf, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_DEC_FINALIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        /* GCM key data pre-processing tests */
        IMB_AES128_GCM_PRECOMP(mgr, NULL);
        printf(".");

        IMB_AES192_GCM_PRECOMP(mgr, NULL);
        printf(".");

        IMB_AES256_GCM_PRECOMP(mgr, NULL);
        printf(".");

        IMB_AES128_GCM_PRE(mgr, NULL, NULL);
        IMB_AES128_GCM_PRE(mgr, NULL, key_data);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_GCM_PRE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES192_GCM_PRE(mgr, NULL, NULL);
        IMB_AES192_GCM_PRE(mgr, NULL, key_data);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES192_GCM_PRE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES256_GCM_PRE(mgr, NULL, NULL);
        IMB_AES256_GCM_PRE(mgr, NULL, key_data);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES256_GCM_PRE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
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

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        IMB_AES_KEYEXP_128(mgr, NULL, NULL, NULL);
        IMB_AES_KEYEXP_128(mgr, NULL, out_buf, zero_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES_KEYEXP_128, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES_KEYEXP_192(mgr, NULL, NULL, NULL);
        IMB_AES_KEYEXP_192(mgr, NULL, out_buf, zero_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES_KEYEXP_192, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES_KEYEXP_256(mgr, NULL, NULL, NULL);
        IMB_AES_KEYEXP_256(mgr, NULL, out_buf, zero_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES_KEYEXP_256, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES_CMAC_SUBKEY_GEN_128(mgr, NULL, NULL, NULL);
        IMB_AES_CMAC_SUBKEY_GEN_128(mgr, NULL, out_buf, zero_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES_CMAC_SUBKEY_GEN_128, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_AES_XCBC_KEYEXP(mgr, NULL, NULL, NULL, NULL);
        IMB_AES_XCBC_KEYEXP(mgr, NULL, out_buf, out_buf, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES_XCBC_KEYEXP, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_DES_KEYSCHED(mgr, NULL, NULL);
        IMB_DES_KEYSCHED(mgr, (uint64_t *)out_buf, NULL);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_DES_KEYSCHED, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
        return 0;
}

/*
 * @brief Performs direct hash API invalid param tests
 */
static int
test_hash_api(struct IMB_MGR *mgr)
{
        const uint32_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int seg_err; /* segfault flag */

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        IMB_SHA1_ONE_BLOCK(mgr, NULL, NULL);
        IMB_SHA1_ONE_BLOCK(mgr, NULL, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA1_ONE_BLOCK, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA1(mgr, NULL, -1, NULL);
        IMB_SHA1(mgr, NULL, BUF_SIZE, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA1, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA224_ONE_BLOCK(mgr, NULL, NULL);
        IMB_SHA224_ONE_BLOCK(mgr, NULL, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA224_ONE_BLOCK, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA224(mgr, NULL, -1, NULL);
        IMB_SHA224(mgr, NULL, BUF_SIZE, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA224, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA256_ONE_BLOCK(mgr, NULL, NULL);
        IMB_SHA256_ONE_BLOCK(mgr, NULL, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA256_ONE_BLOCK, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA256(mgr, NULL, -1, NULL);
        IMB_SHA256(mgr, NULL, BUF_SIZE, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA256, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA384_ONE_BLOCK(mgr, NULL, NULL);
        IMB_SHA384_ONE_BLOCK(mgr, NULL, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA384_ONE_BLOCK, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA384(mgr, NULL, -1, NULL);
        IMB_SHA384(mgr, NULL, BUF_SIZE, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA384, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA512_ONE_BLOCK(mgr, NULL, NULL);
        IMB_SHA512_ONE_BLOCK(mgr, NULL, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA512_ONE_BLOCK, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SHA512(mgr, NULL, -1, NULL);
        IMB_SHA512(mgr, NULL, BUF_SIZE, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SHA512, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_MD5_ONE_BLOCK(mgr, NULL, NULL);
        IMB_MD5_ONE_BLOCK(mgr, NULL, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_MD5_ONE_BLOCK, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
        return 0;
}

/*
 * @brief Performs direct AES API invalid param tests
 */
static int
test_aes_api(struct IMB_MGR *mgr)
{
        const uint32_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int seg_err; /* segfault flag */

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        IMB_AES128_CFB_ONE(mgr, NULL, NULL, NULL, NULL, -1);
        IMB_AES128_CFB_ONE(mgr, out_buf, NULL, NULL, NULL, -1);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_AES128_CFB_ONE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
        return 0;
}

/*
 * @brief Performs direct ZUC API invalid param tests
 */
static int
test_zuc_api(struct IMB_MGR *mgr)
{
        const uint32_t text_len = BUF_SIZE;
        const uint32_t inv_len = -1;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int i, ret1, ret2, seg_err; /* segfault flag */
        void *out_bufs[NUM_BUFS];
        uint32_t lens[NUM_BUFS];

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        for (i = 0; i < NUM_BUFS; i++) {
                out_bufs[i] = (void *)&out_buf;
                lens[i] = text_len;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        ret1 = zuc_eea3_iv_gen(inv_len, (const uint8_t)inv_len,
                               (const uint8_t)inv_len, NULL);
        ret2 = zuc_eea3_iv_gen(inv_len, (const uint8_t)inv_len,
                               (const uint8_t)inv_len, out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: zuc_eea3_iv_gen, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        ret1 = zuc_eia3_iv_gen(inv_len, (const uint8_t)inv_len,
                               (const uint8_t)inv_len, NULL);
        ret2 = zuc_eia3_iv_gen(inv_len, (const uint8_t)inv_len,
                               (const uint8_t)inv_len, out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: zuc_eia3_iv_gen, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_ZUC_EEA3_1_BUFFER(mgr, NULL, NULL, NULL, NULL, inv_len);
        IMB_ZUC_EEA3_1_BUFFER(mgr, NULL, NULL, NULL, out_buf, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_ZUC_EEA3_1_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_ZUC_EEA3_4_BUFFER(mgr, NULL, NULL, NULL, NULL, NULL);
        IMB_ZUC_EEA3_4_BUFFER(mgr, NULL, NULL, NULL, out_bufs, lens);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_ZUC_EEA3_4_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_ZUC_EEA3_N_BUFFER(mgr, NULL, NULL, NULL,
                              NULL, NULL, inv_len);
        IMB_ZUC_EEA3_N_BUFFER(mgr, NULL, NULL, NULL,
                              out_bufs, lens, NUM_BUFS);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_ZUC_EEA3_N_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_ZUC_EIA3_1_BUFFER(mgr, NULL, NULL, NULL, inv_len, NULL);
        IMB_ZUC_EIA3_1_BUFFER(mgr, NULL, NULL, NULL, text_len, out_bufs[0]);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_ZUC_EIA3_1_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
        return 0;
}

/*
 * @brief Performs direct KASUMI API invalid param tests
 */
static int
test_kasumi_api(struct IMB_MGR *mgr)
{
        const uint32_t text_len = BUF_SIZE;
        const uint32_t inv_len = -1;
        const uint64_t inv_iv = -1;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int i, ret1, ret2, seg_err; /* segfault flag */
        void *out_bufs[NUM_BUFS];
        uint32_t lens[NUM_BUFS];

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        for (i = 0; i < NUM_BUFS; i++) {
                out_bufs[i] = (void *)&out_buf;
                lens[i] = text_len;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        ret1 = kasumi_f8_iv_gen(inv_len, (const uint8_t)inv_len,
                                (const uint8_t)inv_len, NULL);
        ret2 = kasumi_f8_iv_gen(inv_len, (const uint8_t)inv_len,
                                (const uint8_t)inv_len, out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: kasumi_f8_iv_gen, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        ret1 = kasumi_f9_iv_gen(inv_len, inv_len, NULL);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) || ret1 == 0) {
                printf("%s: kasumi_f9_iv_gen, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F8_1_BUFFER(mgr, NULL, inv_iv, NULL, NULL, inv_len);
        IMB_KASUMI_F8_1_BUFFER(mgr, NULL, inv_iv, NULL, out_buf, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F8_1_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, NULL, inv_iv, NULL,
                                   NULL, inv_len, inv_len);
        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, NULL, inv_iv, NULL,
                                   out_buf, text_len, 0);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F8_1_BUFFER_BIT, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F8_2_BUFFER(mgr, NULL, inv_iv, inv_iv, NULL,
                               NULL, inv_len, NULL, NULL, inv_len);
        IMB_KASUMI_F8_2_BUFFER(mgr, NULL, inv_iv, inv_iv, NULL,
                               out_buf, text_len, NULL, out_buf, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F8_2_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F8_3_BUFFER(mgr, NULL, inv_iv, inv_iv, inv_iv, NULL,
                               NULL, NULL, NULL, NULL, NULL, inv_len);
        IMB_KASUMI_F8_3_BUFFER(mgr, NULL, inv_iv, inv_iv, inv_iv, NULL,
                               out_buf, NULL, out_buf, NULL, out_buf, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F8_3_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F8_4_BUFFER(mgr, NULL, inv_iv, inv_iv, inv_iv, inv_iv,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               inv_len);
        IMB_KASUMI_F8_4_BUFFER(mgr, NULL, inv_iv, inv_iv, inv_iv, inv_iv,
                               NULL, out_buf, NULL, out_buf, NULL, out_buf,
                               NULL, out_buf, inv_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F8_4_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F8_N_BUFFER(mgr, NULL, NULL, NULL,
                               NULL, NULL, inv_len);
        IMB_KASUMI_F8_N_BUFFER(mgr, NULL, NULL, NULL,
                               out_bufs, lens, NUM_BUFS);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F8_N_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F9_1_BUFFER(mgr, NULL, NULL, inv_len, NULL);
        IMB_KASUMI_F9_1_BUFFER(mgr, NULL, NULL, text_len, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F9_1_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_KASUMI_F9_1_BUFFER_USER(mgr, NULL, inv_iv, NULL,
                                    inv_len, NULL, inv_len);
        IMB_KASUMI_F9_1_BUFFER_USER(mgr, NULL, inv_iv, NULL,
                                    text_len, out_buf, 0);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_KASUMI_F9_1_BUFFER_USER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        ret1 = IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, NULL, NULL);
        ret2 = IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, NULL,
                                            (kasumi_key_sched_t *)out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: IMB_KASUMI_INIT_F8_KEY_SCHED, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        ret1 = IMB_KASUMI_INIT_F9_KEY_SCHED(mgr, NULL, NULL);
        ret2 = IMB_KASUMI_INIT_F9_KEY_SCHED(mgr, NULL,
                                            (kasumi_key_sched_t *)out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: IMB_KASUMI_INIT_F9_KEY_SCHED, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        if (IMB_KASUMI_KEY_SCHED_SIZE(mgr) <= 0) {
                printf("%s: IMB_KASUMI_KEY_SCHED_SIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
        return 0;
}

/*
 * @brief Performs direct SNOW3G API invalid param tests
 */
static int
test_snow3g_api(struct IMB_MGR *mgr)
{
        const uint32_t text_len = BUF_SIZE;
        const uint32_t inv_len = -1;
        uint8_t out_buf[BUF_SIZE];
        uint8_t zero_buf[BUF_SIZE];
        int i, ret1, ret2, seg_err; /* segfault flag */
        void *out_bufs[NUM_BUFS];
        uint32_t lens[NUM_BUFS];

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        for (i = 0; i < NUM_BUFS; i++) {
                out_bufs[i] = (void *)&out_buf;
                lens[i] = text_len;
        }

        memset(out_buf, 0, text_len);
        memset(zero_buf, 0, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        ret1 = snow3g_f8_iv_gen(inv_len, (const uint8_t)inv_len,
                                (const uint8_t)inv_len, NULL);
        ret2 = snow3g_f8_iv_gen(inv_len, (const uint8_t)inv_len,
                                (const uint8_t)inv_len, out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: snow3g_f8_iv_gen, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        ret1 = snow3g_f9_iv_gen(inv_len, (const uint8_t)inv_len,
                                (const uint8_t)inv_len, NULL);
        ret2 = snow3g_f9_iv_gen(inv_len, (const uint8_t)inv_len,
                                (const uint8_t)inv_len, out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: snow3g_f9_iv_gen, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_1_BUFFER(mgr, NULL, NULL, NULL, NULL, inv_len);
        IMB_SNOW3G_F8_1_BUFFER(mgr, NULL, NULL, NULL, out_buf, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_1_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_1_BUFFER_BIT(mgr, NULL, NULL, NULL, NULL,
                                   inv_len, inv_len);
        IMB_SNOW3G_F8_1_BUFFER_BIT(mgr, NULL, NULL, NULL, out_buf,
                                   text_len, 0);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_1_BUFFER_BIT, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_2_BUFFER(mgr, NULL, NULL, NULL, NULL,
                               NULL, inv_len, NULL, NULL, inv_len);
        IMB_SNOW3G_F8_2_BUFFER(mgr, NULL, NULL, NULL, NULL,
                               out_buf, text_len, NULL, out_buf, text_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_2_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_4_BUFFER(mgr, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, inv_len, NULL, NULL, inv_len,
                               NULL, NULL, inv_len, NULL, NULL, inv_len);
        IMB_SNOW3G_F8_4_BUFFER(mgr, NULL, NULL, NULL, NULL, NULL,
                               NULL, out_buf, inv_len, NULL, out_buf, inv_len,
                               NULL, out_buf, inv_len, NULL, out_buf, inv_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_4_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_8_BUFFER(mgr, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, inv_len, NULL, NULL, inv_len,
                               NULL, NULL, inv_len, NULL, NULL, inv_len,
                               NULL, NULL, inv_len, NULL, NULL, inv_len,
                               NULL, NULL, inv_len, NULL, NULL, inv_len);
        IMB_SNOW3G_F8_8_BUFFER(mgr, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               NULL, out_buf, inv_len, NULL, out_buf, inv_len,
                               NULL, out_buf, inv_len, NULL, out_buf, inv_len,
                               NULL, out_buf, inv_len, NULL, out_buf, inv_len,
                               NULL, out_buf, inv_len, NULL, out_buf, inv_len);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_8_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_8_BUFFER_MULTIKEY(mgr, NULL, NULL, NULL, NULL, &inv_len);
        IMB_SNOW3G_F8_8_BUFFER_MULTIKEY(mgr, NULL, NULL, NULL, out_bufs, lens);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_8_BUFFER_MULTIKEY, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_N_BUFFER(mgr, NULL, NULL, NULL, NULL, NULL, inv_len);
        IMB_SNOW3G_F8_N_BUFFER(mgr, NULL, NULL, NULL, out_bufs, lens, NUM_BUFS);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_N_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F8_N_BUFFER_MULTIKEY(mgr, NULL, NULL, NULL, NULL,
                                        NULL, inv_len);
        IMB_SNOW3G_F8_N_BUFFER_MULTIKEY(mgr, NULL, NULL, NULL, out_bufs,
                                        lens, NUM_BUFS);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F8_N_BUFFER_MULTIKEY, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        IMB_SNOW3G_F9_1_BUFFER(mgr, NULL, NULL, NULL, inv_len, NULL);
        IMB_SNOW3G_F9_1_BUFFER(mgr, NULL, NULL, NULL, text_len, out_buf);
        if (memcmp(out_buf, zero_buf, text_len) != 0) {
                printf("%s: IMB_SNOW3G_F9_1_BUFFER, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        ret1 = IMB_SNOW3G_INIT_KEY_SCHED(mgr, NULL, NULL);
        ret2 = IMB_SNOW3G_INIT_KEY_SCHED(mgr, NULL,
                                         (snow3g_key_schedule_t *)out_buf);
        if ((memcmp(out_buf, zero_buf, text_len) != 0) ||
            ret1 == 0 || ret2 == 0) {
                printf("%s: IMB_SNOW3G_INIT_KEY_SCHED, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        if (IMB_SNOW3G_KEY_SCHED_SIZE(mgr) <= 0) {
                printf("%s: IMB_SNOW3G_KEY_SCHED_SIZE, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
        return 0;
}

/*
 * @brief Performs direct clear memory API invalid param tests
 */
static int
test_clear_mem_api(void)
{
        const uint32_t text_len = BUF_SIZE;
        uint8_t out_buf[BUF_SIZE];
        uint8_t cmp_buf[BUF_SIZE];
        int seg_err; /* segfault flag */

        seg_err = setjmp(env);
        if (seg_err) {
                printf("%s: segfault occurred!\n", __func__);
                return 1;
        }

        memset(out_buf, 0xff, text_len);
        memset(cmp_buf, 0xff, text_len);

        /**
         * API are generally tested twice:
         * 1. test with all invalid params
         * 2. test with some valid params (in, out, len)
         *    and verify output buffer is not modified
         */

        imb_clear_mem(NULL, text_len);
        if (memcmp(out_buf, cmp_buf, text_len) != 0) {
                printf("%s: imb_clear_mem, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        imb_clear_mem(out_buf, 0);
        if (memcmp(out_buf, cmp_buf, text_len) != 0) {
                printf("%s: imb_clear_mem, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        imb_clear_mem(out_buf, text_len);
        if (memcmp(out_buf, cmp_buf, text_len) == 0) {
                printf("%s: imb_clear_mem, invalid "
                       "param test failed!\n", __func__);
                return 1;
        }
        printf(".");

        printf("\n");
        return 0;
}

int
direct_api_test(struct IMB_MGR *mb_mgr)
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
        printf("Invalid Direct API arguments test:\n");
        test_suite_start(&ts, "INVALID-ARGS");

#ifndef DEBUG
        handler = signal(SIGSEGV, seg_handler);
#endif

        errors += test_clear_mem_api();
        run++;

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

        test_suite_update(&ts, run - errors, errors);

 dir_api_exit:
        errors = test_suite_end(&ts);

#ifndef DEBUG
        signal(SIGSEGV, handler);
#endif
	return errors;
}
