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
#include <assert.h>

#include <intel-ipsec-mb.h>

#include "gcm_ctr_vectors_test.h"
#include "utils.h"

int chained_test(struct IMB_MGR *mb_mgr);

struct chained_vector {
        const uint8_t *cipher_key;     /* cipher key */
        uint32_t       cipher_key_len; /* cipher key length */
        const uint8_t *IV;             /* initialization vector */
        const uint8_t *PT;              /* plaintext */
        uint64_t       PTlen;           /* plaintext length */
        const uint8_t *CT;              /* ciphertext - same length as PT */
        const uint8_t *hash_key;       /* hash key */
        uint32_t       hash_key_len;   /* hash key length */
        const uint8_t *Digest_PT;       /* digest for plaintext */
        const uint8_t *Digest_CT;       /* digest for ciphertext */
        uint32_t       Digest_len;     /* digest length */
};

const struct test_set {
        IMB_CIPHER_DIRECTION dir;
        IMB_CHAIN_ORDER order;
        const char *set_name;
} test_sets[] = {
        {
                .dir = IMB_DIR_ENCRYPT,
                .order = IMB_ORDER_CIPHER_HASH,
                .set_name = "encrypt-hash"
        },
        {
                .dir = IMB_DIR_DECRYPT,
                .order = IMB_ORDER_CIPHER_HASH,
                .set_name = "decrypt-hash"
        },
        {
                .dir = IMB_DIR_ENCRYPT,
                .order = IMB_ORDER_HASH_CIPHER,
                .set_name = "hash-encrypt"
        },
        {
                .dir = IMB_DIR_DECRYPT,
                .order = IMB_ORDER_HASH_CIPHER,
                .set_name = "hash-decrypt"
        },

};

const char *place_str[] = {"out-of-place", "in-place"};

/* AES-CBC + SHA1-HMAC test vectors */

/*  128-bit */
static const uint8_t K1[] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};
static const uint8_t IV1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
static const uint8_t P1[] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
};
static const uint8_t C1[] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
        0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
        0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b,
        0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
        0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09,
        0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7
};
static const uint8_t DP1[] = {
        0x6F, 0xA4, 0x7D, 0x1B, 0x8E, 0xAB, 0x1D, 0xB9,
        0x8B, 0x62, 0xC9, 0xF2, 0xDF, 0xA2, 0xCC, 0x46,
        0x37, 0xB8, 0xD7, 0xB1
};
static const uint8_t DC1[] = {
        0xDF, 0x1E, 0x5A, 0xDB, 0xE7, 0x5A, 0xAB, 0xAE,
        0x0B, 0x98, 0x34, 0x30, 0xE8, 0x40, 0x8B, 0xB4,
        0xDB, 0x22, 0x3A, 0x89
};

/* Same key for cipher and hash */
static const struct chained_vector chained_vectors[] = {
        {K1, sizeof(K1), IV1, P1, sizeof(P1), C1,
         K1, sizeof(K1), DP1, DC1, sizeof(DP1)},
};

static int
chained_job_ok(const IMB_JOB *job,
               const unsigned num_vec,
               const uint8_t *expected_text,
               const unsigned text_len,
               const uint8_t *received_text,
               const uint8_t *expected_digest,
               const unsigned digest_len,
               const uint8_t *received_digest,
               const uint8_t *padding,
               const size_t sizeof_padding)
{
        if (job->status != IMB_STATUS_COMPLETED) {
                printf("%d error status:%d, job %d",
                       __LINE__, job->status, num_vec);
                return 0;
        }

        /* cipher checks */
        if (memcmp(expected_text, received_text + sizeof_padding,
                   text_len)) {
                printf("cipher %d mismatched\n", num_vec);
                hexdump(stderr, "Received", received_text + sizeof_padding,
                        text_len);
                hexdump(stderr, "Expected", expected_text,
                        text_len);
                return 0;
        }

        if (memcmp(padding, received_text, sizeof_padding)) {
                printf("cipher %d overwrite head\n", num_vec);
                hexdump(stderr, "Target", received_text, sizeof_padding);
                return 0;
        }

        if (memcmp(padding,
                   received_text + sizeof_padding + text_len,
                   sizeof_padding)) {
                printf("cipher %d overwrite tail\n", num_vec);
                hexdump(stderr, "Target",
                        received_text + sizeof_padding + text_len,
                        sizeof_padding);
                return 0;
        }

        /* hash checks */
        if (memcmp(expected_digest, received_digest + sizeof_padding,
                   digest_len)) {
                printf("hash %d mismatched\n", num_vec);
                hexdump(stderr, "Received", received_digest + sizeof_padding,
                        digest_len);
                hexdump(stderr, "Expected", expected_digest,
                        digest_len);
                return 0;
        }

        if (memcmp(padding, received_digest, sizeof_padding)) {
                printf("hash %d overwrite head\n", num_vec);
                hexdump(stderr, "Target", received_digest, sizeof_padding);
                return 0;
        }

        if (memcmp(padding, received_digest + sizeof_padding + digest_len,
                   sizeof_padding)) {
                printf("hash %d overwrite tail\n", num_vec);
                hexdump(stderr, "Target",
                        received_digest + sizeof_padding + digest_len,
                        sizeof_padding);
                return 0;
        }


        return 1;
}

static int
test_chained_many(struct IMB_MGR *mb_mgr,
                  const void *enc_keys,
                  const void *dec_keys,
                  const struct chained_vector *vec,
                  IMB_CIPHER_DIRECTION dir,
                  IMB_CHAIN_ORDER order,
                  IMB_CIPHER_MODE cipher,
                  IMB_HASH_ALG hash,
                  const void *ipad_hash,
                  const void *opad_hash,
                  const unsigned in_place,
                  const unsigned num_jobs)
{
        struct IMB_JOB *job;
        uint8_t padding[16];
        uint8_t **targets = NULL;
        uint8_t **auths = NULL;
        unsigned i, jobs_rx = 0;
        int ret = -1;
        const unsigned cipher_key_size = vec->cipher_key_len;
        const void *iv = vec->IV;
        const unsigned text_len = (unsigned) vec->PTlen;
        const unsigned digest_size = vec->Digest_len;
        const uint8_t *in_text = (dir == IMB_DIR_ENCRYPT) ? vec->PT : vec->CT;
        const uint8_t *out_text = (dir == IMB_DIR_ENCRYPT) ? vec->CT : vec->PT;
        const uint8_t *digest;

        if (num_jobs == 0)
                return 0;

        if ((dir == IMB_DIR_ENCRYPT && order == IMB_ORDER_CIPHER_HASH) ||
            (dir == IMB_DIR_DECRYPT && order == IMB_ORDER_HASH_CIPHER))
                digest = vec->Digest_CT;
        else
                digest = vec->Digest_PT;

        targets = malloc(num_jobs * sizeof(void *));
        if (targets == NULL) {
                fprintf(stderr, "Can't allocate memory for targets array\n");
                goto end;
        }
        memset(targets, 0, num_jobs * sizeof(void *));
        auths = malloc(num_jobs * sizeof(void *));
        if (auths == NULL) {
                fprintf(stderr, "Can't allocate memory for auths array\n");
                goto end;
        }
        memset(auths, 0, num_jobs * sizeof(void *));

        memset(padding, -1, sizeof(padding));

        for (i = 0; i < num_jobs; i++) {
                targets[i] = malloc(text_len + (sizeof(padding) * 2));
                if (targets[i] == NULL) {
                        fprintf(stderr, "Can't allocate buffer memory\n");
                        goto end;
                }
                memset(targets[i], -1, text_len + (sizeof(padding) * 2));
                if (in_place) {
                        /* copy input text to the allocated buffer */
                        memcpy(targets[i] + sizeof(padding), in_text, text_len);
                }

                auths[i] = malloc(digest_size + (sizeof(padding) * 2));
                if (auths[i] == NULL) {
                        fprintf(stderr, "Can't allocate buffer memory\n");
                        goto end;
                }
                memset(auths[i], -1, digest_size + (sizeof(padding) * 2));
        }

        /* flush the scheduler */
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->cipher_direction = dir;
                job->chain_order = order;
                if (in_place) {
                        job->dst = targets[i] + sizeof(padding);
                        job->src = targets[i] + sizeof(padding);
                } else {
                        job->dst = targets[i] + sizeof(padding);
                        job->src = in_text;
                }
                job->cipher_mode = cipher;
                job->enc_keys = enc_keys;
                job->dec_keys = dec_keys;
                job->key_len_in_bytes = cipher_key_size;

                job->iv = iv;
                job->iv_len_in_bytes = 16;
                job->cipher_start_src_offset_in_bytes = 0;
                job->msg_len_to_cipher_in_bytes = text_len;
                job->user_data = (void *)((uint64_t)i);

                job->hash_alg = hash;
                job->auth_tag_output = auths[i] + sizeof(padding);
                job->auth_tag_output_len_in_bytes = digest_size;
                /*
                 * If operation is out of place and hash operation is done
                 * after encryption/decryption, hash operation needs to be
                 * done in the destination buffer.
                 * Since hash_start_src_offset_in_bytes refers to the offset
                 * in the source buffer, this offset is set to point at
                 * the destination buffer.
                 */
                if (!in_place && (job->chain_order == IMB_ORDER_CIPHER_HASH)) {
                        const uintptr_t u_src = (const uintptr_t) job->src;
                        const uintptr_t u_dst = (const uintptr_t) job->dst;
                        const uintptr_t offset = (u_dst > u_src) ?
                                        (u_dst - u_src) :
                                        (UINTPTR_MAX - u_src + u_dst + 1);

                        job->hash_start_src_offset_in_bytes = (uint64_t)offset;
                } else {
                        job->hash_start_src_offset_in_bytes = 0;
                }
                job->msg_len_to_hash_in_bytes = text_len;
                job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
                job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job != NULL) {
                        jobs_rx++;
                        const unsigned num =
                                (const unsigned)((uint64_t)job->user_data);

                        if (!chained_job_ok(job, num, out_text, text_len,
                                            targets[num],
                                            digest, digest_size, auths[num],
                                            padding, sizeof(padding)))
                                goto end;
                }
        }

        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                jobs_rx++;
                const int num = (const unsigned)((uint64_t)job->user_data);

                if (!chained_job_ok(job, num, out_text, text_len, targets[num],
                                    digest, digest_size, auths[num],
                                    padding, sizeof(padding)))
                        goto end;
        }

        if (jobs_rx != num_jobs) {
                printf("Expected %d jobs, received %d\n", num_jobs, jobs_rx);
                goto end;
        }
        ret = 0;

 end:
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++) {
                if (targets != NULL)
                        free(targets[i]);
                if (auths != NULL)
                        free(auths[i]);
        }
        free(targets);
        free(auths);
        return ret;
}

static void
test_chained_vectors(struct IMB_MGR *mb_mgr,
                     struct test_suite_context *ctx,
                     const int vec_cnt,
                     const struct chained_vector *vec_tab, const char *banner,
                     const IMB_CIPHER_MODE cipher,
                     const IMB_HASH_ALG hash,
                     unsigned hash_block_size, int num_jobs)
{
        int vect;
        DECLARE_ALIGNED(uint32_t enc_keys[15*4], 16);
        DECLARE_ALIGNED(uint32_t dec_keys[15*4], 16);
        uint8_t *buf = NULL;
        uint8_t *hash_key = NULL;
        DECLARE_ALIGNED(uint8_t ipad_hash[128], 16);
        DECLARE_ALIGNED(uint8_t opad_hash[128], 16);
        unsigned hash_key_len, i;

        buf = malloc(hash_block_size);
        if (buf == NULL) {
                fprintf(stderr, "Can't allocate buffer memory\n");
                goto exit;
        }

        hash_key = malloc(hash_block_size);
        if (hash_key == NULL) {
                fprintf(stderr, "Can't allocate key memory\n");
                goto exit;
        }

        printf("%s (N jobs = %d):\n", banner, num_jobs);
        for (vect = 0; vect < vec_cnt; vect++) {
#ifdef DEBUG
                printf("[%d/%d] Standard vector key_len:%d\n",
                       vect + 1, vec_cnt,
                       (int) vec_tab[vect].cipher_key_len);
#else
                printf(".");
#endif
                /* prepare the cipher key */
                switch (vec_tab[vect].cipher_key_len) {
                case 16:
                        IMB_AES_KEYEXP_128(mb_mgr, vec_tab[vect].cipher_key,
                                           enc_keys, dec_keys);
                        break;
                case 24:
                        IMB_AES_KEYEXP_192(mb_mgr, vec_tab[vect].cipher_key,
                                           enc_keys, dec_keys);
                        break;
                case 32:
                default:
                        IMB_AES_KEYEXP_256(mb_mgr, vec_tab[vect].cipher_key,
                                           enc_keys, dec_keys);
                        break;
                }

                /* prepare the hash key */
                memset(hash_key, 0, hash_block_size);
                if (vec_tab[vect].hash_key_len <= hash_block_size) {
                        memcpy(hash_key, vec_tab[vect].hash_key,
                               vec_tab[vect].hash_key_len);
                        hash_key_len = (int) vec_tab[vect].hash_key_len;
                } else {
                        IMB_SHA1(mb_mgr, vec_tab[vect].hash_key,
                                 vec_tab[vect].hash_key_len, hash_key);
                        hash_key_len = hash_block_size;
                }

                /* compute ipad hash */
                memset(buf, 0x36, hash_block_size);
                for (i = 0; i < hash_key_len; i++)
                        buf[i] ^= hash_key[i];
                IMB_SHA1_ONE_BLOCK(mb_mgr, buf, ipad_hash);

                /* compute opad hash */
                memset(buf, 0x5c, hash_block_size);
                for (i = 0; i < hash_key_len; i++)
                        buf[i] ^= hash_key[i];
                IMB_SHA1_ONE_BLOCK(mb_mgr, buf, opad_hash);

                for (i = 0; i < DIM(test_sets); i++) {
                        unsigned in_place;

                        for (in_place = 0; in_place < DIM(place_str);
                             in_place++) {
                                if (test_chained_many(mb_mgr,
                                                      enc_keys, dec_keys,
                                                      &vec_tab[vect],
                                                      test_sets[i].dir,
                                                      test_sets[i].order,
                                                      cipher, hash,
                                                      ipad_hash, opad_hash,
                                                      in_place,  num_jobs)) {
                                        printf("error #%d %s %s\n", vect + 1,
                                               test_sets[i].set_name,
                                               place_str[in_place]);
                                        test_suite_update(ctx, 0, 1);
                                } else {
                                        test_suite_update(ctx, 1, 0);
                                }
                        }
                }
        }
        printf("\n");

exit:
        free(buf);
        free(hash_key);
}

int
chained_test(struct IMB_MGR *mb_mgr)
{
        const int num_jobs_tab[] = {
                1, 3, 4, 5, 7, 8, 9, 15, 16, 17
        };
        unsigned i;
        int errors = 0;
        struct test_suite_context ctx;

        test_suite_start(&ctx, "CHAINED-OP");
        for (i = 0; i < DIM(num_jobs_tab); i++)
                test_chained_vectors(mb_mgr, &ctx,
                                     DIM(chained_vectors),
                                     chained_vectors,
                                     "AES-CBC + SHA1-HMAC standard test vectors",
                                     IMB_CIPHER_CBC, IMB_AUTH_HMAC_SHA_1,
                                     IMB_SHA1_BLOCK_SIZE, num_jobs_tab[i]);

        errors += test_suite_end(&ctx);

        return errors;
}
