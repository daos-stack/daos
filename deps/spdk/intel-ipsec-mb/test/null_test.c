/*****************************************************************************
 Copyright (c) 2020-2021, Intel Corporation

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

#include <intel-ipsec-mb.h>
#include "utils.h"

#define PAD_SIZE 16
#define BUF_SIZE 32

int null_test(struct IMB_MGR *mb_mgr);

static void
test_null_hash(struct IMB_MGR *mb_mgr,
               struct test_suite_context *ctx,
               IMB_CIPHER_DIRECTION cipher_dir,
               IMB_CHAIN_ORDER chain_order)
{
        DECLARE_ALIGNED(uint8_t cipher_key[16], 16) = {0};
        DECLARE_ALIGNED(uint32_t expkey[4*15], 16);
        DECLARE_ALIGNED(uint32_t dust[4*15], 16);
        DECLARE_ALIGNED(uint8_t iv[16], 16);
        DECLARE_ALIGNED(uint8_t digest[16], 16) = {0};
        DECLARE_ALIGNED(uint8_t all_zeros[16], 16) = {0};
        struct IMB_JOB *job;
        uint8_t padding[PAD_SIZE];
        uint8_t target[BUF_SIZE + 2*PAD_SIZE];
        uint8_t in_text[BUF_SIZE];
        int ret = -1;

        memset(target, -1, sizeof(target));
        memset(padding, -1, sizeof(padding));

        IMB_AES_KEYEXP_128(mb_mgr, cipher_key, expkey, dust);
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        job = IMB_GET_NEXT_JOB(mb_mgr);
        job->cipher_direction = cipher_dir;
        job->chain_order = chain_order;
        job->dst = target + PAD_SIZE;
        job->src = in_text;
        job->cipher_mode = IMB_CIPHER_CNTR;
        job->enc_keys = expkey;
        job->dec_keys = expkey;
        job->key_len_in_bytes = 16;
        job->iv = iv;
        job->iv_len_in_bytes = 16;
        job->cipher_start_src_offset_in_bytes = 0;
        job->msg_len_to_cipher_in_bytes = BUF_SIZE;

        job->hash_alg = IMB_AUTH_NULL;
        job->auth_tag_output = digest;
        job->auth_tag_output_len_in_bytes = 0;

        job = IMB_SUBMIT_JOB(mb_mgr);
        if (!job)
                job = IMB_FLUSH_JOB(mb_mgr);

        if (!job) {
                printf("%d Unexpected null return from submit/flush_job\n",
                       __LINE__);
                goto end;
        }

        /* Check that padding has not been changed */
        if (memcmp(padding, target, PAD_SIZE)) {
                printf("overwrite head\n");
                hexdump(stderr, "Target", target, BUF_SIZE + PAD_SIZE*2);
                goto end;
        }
        if (memcmp(padding, target + PAD_SIZE + BUF_SIZE,
                   PAD_SIZE)) {
                printf("overwrite tail\n");
                hexdump(stderr, "Target", target, BUF_SIZE + PAD_SIZE*2);
                goto end;
        }
        /* Check that authentication tag has not been modified */
        if (memcmp(digest, all_zeros, 16) != 0) {
                printf("overwrite auth tag\n");
                hexdump(stderr, "Auth tag", digest, 16);
                goto end;
        }

        ret = 0;
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;
end:
        if (ret == 0)
                test_suite_update(ctx, 1, 0);
        else
                test_suite_update(ctx, 0, 1);
}

static void
test_null_cipher(struct IMB_MGR *mb_mgr,
                 struct test_suite_context *ctx,
                 IMB_CIPHER_DIRECTION cipher_dir,
                 IMB_CHAIN_ORDER chain_order)
{
        DECLARE_ALIGNED(uint8_t auth_key[16], 16);
        DECLARE_ALIGNED(uint32_t expkey[4*15], 16);
        DECLARE_ALIGNED(uint32_t dust[4*15], 16);
        uint32_t skey1[4], skey2[4];
        DECLARE_ALIGNED(uint8_t digest[16], 16) = {0};
        struct IMB_JOB *job;
        uint8_t in_text[BUF_SIZE] = {0};
        DECLARE_ALIGNED(uint8_t all_zeros[BUF_SIZE], 16) = {0};
        int ret = -1;

        memset(auth_key, 0x55, sizeof(auth_key));

        IMB_AES_KEYEXP_128(mb_mgr, auth_key, expkey, dust);
        IMB_AES_CMAC_SUBKEY_GEN_128(mb_mgr, expkey, skey1, skey2);
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        job = IMB_GET_NEXT_JOB(mb_mgr);
        memset(job, 0, sizeof(*job));
        job->cipher_direction = cipher_dir;
        job->chain_order = chain_order;
        job->src = in_text;
        job->cipher_mode = IMB_CIPHER_NULL;
        job->enc_keys = expkey;
        job->dec_keys = expkey;

        job->hash_alg = IMB_AUTH_AES_CMAC;
        job->u.CMAC._key_expanded = expkey;
        job->u.CMAC._skey1 = skey1;
        job->u.CMAC._skey2 = skey2;
        job->auth_tag_output = digest;
        job->auth_tag_output_len_in_bytes = 16;

        job = IMB_SUBMIT_JOB(mb_mgr);
        if (!job)
                job = IMB_FLUSH_JOB(mb_mgr);

        if (!job) {
                printf("%d Unexpected null return from submit/flush_job\n",
                       __LINE__);
                goto end;
        }
        if (job->status != IMB_STATUS_COMPLETED) {
                printf("%d Error status:%d", __LINE__, job->status);
                goto end;
        }

        /* Check that input text has not been changed */
        if (memcmp(in_text, all_zeros, BUF_SIZE)) {
                printf("overwrite source\n");
                hexdump(stderr, "Source", in_text, BUF_SIZE);
                goto end;
        }
        /* Check that authentication tag has been modified */
        if (memcmp(digest, all_zeros, 16) == 0) {
                printf("auth tag still zeros\n");
                hexdump(stderr, "Auth tag", digest, 16);
                goto end;
        }

        ret = 0;
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;
end:
        if (ret == 0)
                test_suite_update(ctx, 1, 0);
        else
                test_suite_update(ctx, 0, 1);
}

int
null_test(struct IMB_MGR *mb_mgr)
{
        int errors = 0;
        struct test_suite_context ctx;

        /* NULL-HASH test */
        test_suite_start(&ctx, "NULL-HASH");
        test_null_hash(mb_mgr, &ctx, IMB_DIR_ENCRYPT, IMB_ORDER_CIPHER_HASH);
        test_null_hash(mb_mgr, &ctx, IMB_DIR_ENCRYPT, IMB_ORDER_HASH_CIPHER);
        test_null_hash(mb_mgr, &ctx, IMB_DIR_DECRYPT, IMB_ORDER_CIPHER_HASH);
        test_null_hash(mb_mgr, &ctx, IMB_DIR_DECRYPT, IMB_ORDER_HASH_CIPHER);
        errors += test_suite_end(&ctx);

        /* NULL-CIPHER test */
        test_suite_start(&ctx, "NULL-CIPHER");
        test_null_cipher(mb_mgr, &ctx, IMB_DIR_ENCRYPT, IMB_ORDER_CIPHER_HASH);
        test_null_cipher(mb_mgr, &ctx, IMB_DIR_ENCRYPT, IMB_ORDER_HASH_CIPHER);
        test_null_cipher(mb_mgr, &ctx, IMB_DIR_DECRYPT, IMB_ORDER_CIPHER_HASH);
        test_null_cipher(mb_mgr, &ctx, IMB_DIR_DECRYPT, IMB_ORDER_HASH_CIPHER);
        errors += test_suite_end(&ctx);

	return errors;
}
