/*****************************************************************************
 Copyright (c) 2012-2021, Intel Corporation

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef DO_TEST_H
#define DO_TEST_H

static unsigned char key[] = {
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
        0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
        0xa0
};

static unsigned char text[] = {
        0x53, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x20, 0x23, 0x34
};

static unsigned char hmac12[] = {
        0x9e, 0xa8, 0x86, 0xef, 0xe2, 0x68, 0xdb, 0xec, 0xce, 0x42, 0x0c, 0x75
};

#define KEYSIZE  sizeof(key)
#define TEXTSIZE sizeof(text)

static unsigned char plain[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
};

static unsigned char key128[] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

/* static unsigned char key256[] = { */
/*         0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, */
/*         0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81, */
/*         0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, */
/*         0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4 */
/* }; */

static unsigned char ic[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static unsigned char cipherCBC128[] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
        0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
        0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b,
        0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
        0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09,
        0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7
};

/* static unsigned char cipherCBC256[] = { */
/*         0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba, */
/*         0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6, */
/*         0x9c, 0xfc, 0x4e, 0x96, 0x7e, 0xdb, 0x80, 0x8d, */
/*         0x67, 0x9f, 0x77, 0x7b, 0xc6, 0x70, 0x2c, 0x7d, */
/*         0x39, 0xf2, 0x33, 0x69, 0xa9, 0xd9, 0xba, 0xcf, */
/*         0xa5, 0x30, 0xe2, 0x63, 0x04, 0x23, 0x14, 0x61, */
/*         0xb2, 0xeb, 0x05, 0xe2, 0xc3, 0x9b, 0xe9, 0xfc, */
/*         0xda, 0x6c, 0x19, 0x07, 0x8c, 0x6a, 0x9d, 0x1b */
/* }; */

#define NUMBLOCKS 4
#define NUMBYTES (NUMBLOCKS * 16)


static int
known_answer_test(IMB_MGR *mb_mgr)
{
        uint8_t test_buf[NUMBYTES];
        uint8_t buf[64];
        DECLARE_ALIGNED(uint32_t enc_keys[15*4], 16);
        DECLARE_ALIGNED(uint32_t dec_keys[15*4], 16);
        DECLARE_ALIGNED(uint8_t ipad_hash[5*4], 16);
        DECLARE_ALIGNED(uint8_t opad_hash[5*4], 16);
        IMB_JOB *job;
        uint8_t iv[16];
        uint8_t digest[12];
        uint32_t i;

        /* compute ipad hash */
        for (i = 0; i < 64; i++)
                buf[i] = 0x36;
        for (i = 0; i < KEYSIZE; i++)
                buf[i] ^= key[i];
        IMB_SHA1_ONE_BLOCK(mb_mgr, buf, ipad_hash);

        /* compute opad hash */
        for (i = 0; i < 64; i++)
                buf[i] = 0x5c;
        for (i = 0; i < KEYSIZE; i++)
                buf[i] ^= key[i];
        IMB_SHA1_ONE_BLOCK(mb_mgr, buf, opad_hash);


        /* Expand key */
        IMB_AES_KEYEXP_128(mb_mgr, key128, enc_keys, dec_keys);

	/* flush the scheduler */
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        /* test AES128 Dec */
        job = IMB_GET_NEXT_JOB(mb_mgr);

        job->enc_keys = enc_keys;
        job->dec_keys = dec_keys;
        job->cipher_direction = IMB_DIR_DECRYPT;
        job->chain_order = IMB_ORDER_HASH_CIPHER;
        job->dst = test_buf;
        job->key_len_in_bytes = 16;
        job->auth_tag_output = digest;
        job->auth_tag_output_len_in_bytes = 12;
        memcpy(iv, ic, sizeof(iv));
        job->iv = iv;
        job->iv_len_in_bytes = 16;
        job->src = cipherCBC128;
        job->cipher_start_src_offset_in_bytes = 0;
        job->msg_len_to_cipher_in_bytes = NUMBYTES;
        job->hash_start_src_offset_in_bytes = text - job->src;
        job->msg_len_to_hash_in_bytes = TEXTSIZE;
        job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
        job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
        job->cipher_mode = IMB_CIPHER_CBC;
        job->hash_alg = IMB_AUTH_HMAC_SHA_1;

        job = IMB_SUBMIT_JOB(mb_mgr);
        if (job) {
                printf("Unexpected return from submit_job\n");
                return 1;
        }
        job = IMB_FLUSH_JOB(mb_mgr);
        if (!job) {
                printf("Unexpected null return from flush_job\n");
                return 1;
        }
        for (i = 0; i < NUMBYTES; i++) {
                if (test_buf[i] != plain[i]) {
                        printf("AES128 Dec mismatch on byte %d\n", i);
                        return 1;
                }
        }

        for (i = 0; i < 12; i++) {
                if (digest[i] != hmac12[i]) {
                        printf("HMAC/SHA1 mismatch on byte %d\n", i);
                        return 1;
                }
        }
        printf("Known answer passes\n");
        return 0;
}

static void
test_aux_func(IMB_MGR *mgr)
{
        /* test aux functions */
        imb_uint128_t keys[15] = {{0, 0} };
        static uint8_t buf[4096+20];

        uint32_t digest1[8];
        uint64_t digest3[8];
        DECLARE_ALIGNED(uint32_t k1_exp[15*4],   16);
        DECLARE_ALIGNED(uint32_t k2[4],          16);
        DECLARE_ALIGNED(uint32_t k3[4],          16);

        printf("Testing aux funcs\n");

        IMB_SHA1_ONE_BLOCK(mgr, buf, digest1);
        IMB_SHA224_ONE_BLOCK(mgr, buf, digest1);
        IMB_SHA256_ONE_BLOCK(mgr, buf, digest1);
        IMB_SHA384_ONE_BLOCK(mgr, buf, digest3);
        IMB_SHA512_ONE_BLOCK(mgr, buf, digest3);
        IMB_MD5_ONE_BLOCK(mgr, buf, digest1);
        IMB_AES_XCBC_KEYEXP(mgr, buf + 1, k1_exp, k2, k3);
        IMB_AES_KEYEXP_128(mgr, keys, k1_exp, k1_exp);
        IMB_AES_KEYEXP_192(mgr, keys, k1_exp, k1_exp);
        IMB_AES_KEYEXP_256(mgr, keys, k1_exp, k1_exp);
}

static int
do_test(IMB_MGR *mb_mgr)
{
        uint32_t size;
        IMB_JOB *job;
        static imb_uint128_t IV = {0, 0};
        static uint32_t ipad[5], opad[5], digest[3];
        imb_uint128_t keys[15] = {{0, 0} };
        static uint8_t buf[4096 + 20];

        for (size = 32; size < 4096; size += 16) {
                job = IMB_GET_NEXT_JOB(mb_mgr);

                job->msg_len_to_cipher_in_bytes = size;
                job->msg_len_to_hash_in_bytes = size + 20;
                job->hash_start_src_offset_in_bytes = 0;
                job->cipher_start_src_offset_in_bytes = 20;

                job->auth_tag_output = (uint8_t *) digest;
                job->auth_tag_output_len_in_bytes = 12;
                job->u.HMAC._hashed_auth_key_xor_ipad = (uint8_t *)ipad;
                job->u.HMAC._hashed_auth_key_xor_opad = (uint8_t *)opad;

                job->enc_keys =
                        job->dec_keys = (uint32_t *) keys;
                job->src = buf;
                job->dst = buf + 20;
                job->iv = (uint8_t *) &IV;
                job->iv_len_in_bytes = 16;

                job->cipher_mode = IMB_CIPHER_CBC;
                job->hash_alg = IMB_AUTH_HMAC_SHA_1;

                switch (rand() % 3) {
                case 0:
                        job->key_len_in_bytes = 16;
                        break;
                case 1:
                        job->key_len_in_bytes = 24;
                        break;
                default:
                        job->key_len_in_bytes = 32;
                        break;
                }

                switch (rand() % 4) {
		case 0:
                        job->cipher_direction = IMB_DIR_ENCRYPT;
                        job->chain_order = IMB_ORDER_HASH_CIPHER;
			break;
		case 1:
                        job->cipher_direction = IMB_DIR_ENCRYPT;
                        job->chain_order = IMB_ORDER_CIPHER_HASH;
			break;
                case 2:
			job->cipher_direction = IMB_DIR_DECRYPT;
			job->chain_order = IMB_ORDER_CIPHER_HASH;
			break;
		case 3:
			job->cipher_direction = IMB_DIR_DECRYPT;
			job->chain_order = IMB_ORDER_HASH_CIPHER;
			break;
                }
                job = IMB_SUBMIT_JOB(mb_mgr);
                while (job)
                        job = IMB_GET_COMPLETED_JOB(mb_mgr);
        } /* end for size */

        while (IMB_FLUSH_JOB(mb_mgr) != NULL) {
                do {
                        job = IMB_GET_COMPLETED_JOB(mb_mgr);
                } while (job);
        }

        test_aux_func(mb_mgr);

        return 0;
}

#endif /* DO_TEST_H */
