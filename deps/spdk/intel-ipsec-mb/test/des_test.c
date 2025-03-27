/*****************************************************************************
 Copyright (c) 2017-2021, Intel Corporation

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

int des_test(const enum arch_type arch, struct IMB_MGR *mb_mgr);

struct des_vector {
	const uint8_t *K;          /* key */
	const uint8_t *IV;         /* initialization vector */
	const uint8_t *P;          /* plain text */
	uint64_t       Plen;       /* plain text length */
	const uint8_t *C;          /* cipher text - same length as plain text */
};

struct des3_vector {
	const uint8_t *K1;         /* key */
	const uint8_t *K2;         /* key */
	const uint8_t *K3;         /* key */
	const uint8_t *IV;         /* initialization vector */
	const uint8_t *P;          /* plain text */
	uint64_t       Plen;       /* plain text length */
	const uint8_t *C;          /* cipher text - same length as plain text */
};

/* CM-SP-SECv3.1-I07-170111 I.7 */
static const uint8_t K1[] = {
        0xe6, 0x60, 0x0f, 0xd8, 0x85, 0x2e, 0xf5, 0xab
};
static const uint8_t IV1[] = {
        0x81, 0x0e, 0x52, 0x8e, 0x1c, 0x5f, 0xda, 0x1a
};
static const uint8_t P1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x88, 0x41, 0x65, 0x06
};
static const uint8_t C1[] = {
        0x0d, 0xda, 0x5a, 0xcb, 0xd0, 0x5e, 0x55, 0x67,
        0x9f, 0x04, 0xd1, 0xb6, 0x41, 0x3d, 0x4e, 0xed
};

static const uint8_t K2[] = {
        0x3b, 0x38, 0x98, 0x37, 0x15, 0x20, 0xf7, 0x5e
};
static const uint8_t IV2[] = {
        0x02, 0xa8, 0x11, 0x77, 0x4d, 0xcd, 0xe1, 0x3b
};
static const uint8_t P2[] = {
        0x05, 0xef, 0xf7, 0x00, 0xe9, 0xa1, 0x3a, 0xe5,
        0xca, 0x0b, 0xcb, 0xd0, 0x48, 0x47, 0x64, 0xbd,
        0x1f, 0x23, 0x1e, 0xa8, 0x1c, 0x7b, 0x64, 0xc5,
        0x14, 0x73, 0x5a, 0xc5, 0x5e, 0x4b, 0x79, 0x63,
        0x3b, 0x70, 0x64, 0x24, 0x11, 0x9e, 0x09, 0xdc,
        0xaa, 0xd4, 0xac, 0xf2, 0x1b, 0x10, 0xaf, 0x3b,
        0x33, 0xcd, 0xe3, 0x50, 0x48, 0x47, 0x15, 0x5c,
        0xbb, 0x6f, 0x22, 0x19, 0xba, 0x9b, 0x7d, 0xf5

};
static const uint8_t C2[] = {
        0xf3, 0x31, 0x8d, 0x01, 0x19, 0x4d, 0xa8, 0x00,
        0xa4, 0x2c, 0x10, 0xb5, 0x33, 0xd6, 0xbc, 0x11,
        0x97, 0x59, 0x2d, 0xcc, 0x9b, 0x5d, 0x35, 0x9a,
        0xc3, 0x04, 0x5d, 0x07, 0x4c, 0x86, 0xbf, 0x72,
        0xe5, 0x1a, 0x72, 0x25, 0x82, 0x22, 0x54, 0x03,
        0xde, 0x8b, 0x7a, 0x58, 0x5c, 0x6c, 0x28, 0xdf,
        0x41, 0x0e, 0x38, 0xd6, 0x2a, 0x86, 0xe3, 0x4f,
        0xa2, 0x7c, 0x22, 0x39, 0x60, 0x06, 0x03, 0x6f
};

static struct des_vector vectors[] = {
        {K1, IV1, P1, sizeof(P1), C1},
        {K2, IV2, P2, sizeof(P2), C2},
};

/* CM-SP-SECv3.1-I07-170111 I.7 */
static const uint8_t DK1[] = {
        0xe6, 0x60, 0x0f, 0xd8, 0x85, 0x2e, 0xf5, 0xab
};
static const uint8_t DIV1[] = {
        0x81, 0x0e, 0x52, 0x8e, 0x1c, 0x5f, 0xda, 0x1a
};
static const uint8_t DP1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x88, 0x41, 0x65, 0x06
};
static const uint8_t DC1[] = {
        0x0d, 0xda, 0x5a, 0xcb, 0xd0, 0x5e, 0x55, 0x67,
        0x9f, 0x04, 0xd1, 0xb6, 0x41, 0x3d, 0x4e, 0xed
};

static const uint8_t DK2[] = {
        0xe6, 0x60, 0x0f, 0xd8, 0x85, 0x2e, 0xf5, 0xab
};
static const uint8_t DIV2[] = {
        0x81, 0x0e, 0x52, 0x8e, 0x1c, 0x5f, 0xda, 0x1a
};
static const uint8_t DP2[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x91,
        0xd2, 0xd1, 0x9f
};
static const uint8_t DC2[] = {
        0x0d, 0xda, 0x5a, 0xcb, 0xd0, 0x5e, 0x55, 0x67,
        0x51, 0x47, 0x46, 0x86, 0x8a, 0x71, 0xe5, 0x77,
        0xef, 0xac, 0x88
};

static const uint8_t DK3[] = {
        0xe6, 0x60, 0x0f, 0xd8, 0x85, 0x2e, 0xf5, 0xab
};
static const uint8_t DIV3[] = {
        0x51, 0x47, 0x46, 0x86, 0x8a, 0x71, 0xe5, 0x77
};
static const uint8_t DP3[] = {
        0xd2, 0xd1, 0x9f
};
static const uint8_t DC3[] = {
        0xef, 0xac, 0x88
};


static struct des_vector docsis_vectors[] = {
        {DK1, DIV1, DP1, sizeof(DP1), DC1},
        {DK2, DIV2, DP2, sizeof(DP2), DC2},
        {DK3, DIV3, DP3, sizeof(DP3), DC3},
};

/* 3DES vectors - 2x and 3x keys */

static const uint8_t D3K1_1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3K2_1[] = {
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t D3K3_1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3IV_1[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};


static const uint8_t D3PT_1[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t D3CT_1[] = {
        0xdf, 0x0b, 0x6c, 0x9c, 0x31, 0xcd, 0x0c, 0xe4
};

#define D3PT_LEN_1 8

static const uint8_t D3K1_2[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3K2_2[] = {
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t D3K3_2[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3IV_2[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3PT_2[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t D3CT_2[] = {
        0xdd, 0xad, 0xa1, 0x61, 0xe8, 0xd7, 0x96, 0x73,
        0xed, 0x75, 0x32, 0xe5, 0x92, 0x23, 0xcd, 0x0d
};

#define D3PT_LEN_2 16

static const uint8_t D3K1_3[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3K2_3[] = {
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t D3K3_3[] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

static const uint8_t D3IV_3[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3PT_3[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t D3CT_3[] = {
        0x58, 0xed, 0x24, 0x8f, 0x77, 0xf6, 0xb1, 0x9e
};

#define D3PT_LEN_3 8

static const uint8_t D3K1_4[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3K2_4[] = {
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t D3K3_4[] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

static const uint8_t D3IV_4[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

static const uint8_t D3PT_4[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t D3CT_4[] = {
        0x89, 0x4b, 0xc3, 0x08, 0x54, 0x26, 0xa4, 0x41,
        0xf2, 0x7f, 0x73, 0xae, 0x26, 0xab, 0xbf, 0x74
};

#define D3PT_LEN_4 16

static struct des3_vector des3_vectors[] = {
        { D3K1_1, D3K2_1, D3K3_1, D3IV_1, D3PT_1, D3PT_LEN_1, D3CT_1 },
        { D3K1_2, D3K2_2, D3K3_2, D3IV_2, D3PT_2, D3PT_LEN_2, D3CT_2 },
        { D3K1_3, D3K2_3, D3K3_3, D3IV_3, D3PT_3, D3PT_LEN_3, D3CT_3 },
        { D3K1_4, D3K2_4, D3K3_4, D3IV_4, D3PT_4, D3PT_LEN_4, D3CT_4 },
};

static int
test_des_many(struct IMB_MGR *mb_mgr,
              const uint64_t *ks,
              const uint64_t *ks2,
              const uint64_t *ks3,
              const void *iv,
              const uint8_t *in_text,
              const uint8_t *out_text,
              unsigned text_len,
              int dir,
              int order,
              IMB_CIPHER_MODE cipher,
              const int in_place,
              const int num_jobs)
{
        const void *ks_ptr[3]; /* 3DES */
        struct IMB_JOB *job;
        uint8_t padding[16];
        uint8_t **targets = malloc(num_jobs * sizeof(void *));
        int i, jobs_rx = 0, ret = -1;

        assert(targets != NULL);

        memset(padding, -1, sizeof(padding));

        for (i = 0; i < num_jobs; i++) {
                targets[i] = malloc(text_len + (sizeof(padding) * 2));
                memset(targets[i], -1, text_len + (sizeof(padding) * 2));
                if (in_place) {
                        /* copy input text to the allocated buffer */
                        memcpy(targets[i] + sizeof(padding), in_text, text_len);
                }
        }

        /* Used in 3DES only */
        ks_ptr[0] = ks;
        ks_ptr[1] = ks2;
        ks_ptr[2] = ks3;

        /* flush the scheduler */
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->cipher_direction = dir;
                job->chain_order = order;
                if (!in_place) {
                        job->dst = targets[i] + sizeof(padding);
                        job->src = in_text;
                } else {
                        job->dst = targets[i] + sizeof(padding);
                        job->src = targets[i] + sizeof(padding);
                }
                job->cipher_mode = cipher;
                if (cipher == IMB_CIPHER_DES3) {
                        job->enc_keys = (const void *) ks_ptr;
                        job->dec_keys = (const void *) ks_ptr;
                        job->key_len_in_bytes = 24; /* 3x keys only */
                } else {
                        job->enc_keys = ks;
                        job->dec_keys = ks;
                        job->key_len_in_bytes = 8;
                }
                job->iv = iv;
                job->iv_len_in_bytes = 8;
                job->cipher_start_src_offset_in_bytes = 0;
                job->msg_len_to_cipher_in_bytes = text_len;
                job->user_data = (void *)((uint64_t)i);

                job->hash_alg = IMB_AUTH_NULL;

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job != NULL) {
                        const int num = (const int)((uint64_t)job->user_data);

                        jobs_rx++;
                        if (job->status != IMB_STATUS_COMPLETED) {
                                printf("%d error status:%d, job %d",
                                       __LINE__, job->status, num);
                                goto end;
                        }
                        if (memcmp(out_text, targets[num] + sizeof(padding),
                                   text_len)) {
                                printf("%d mismatched\n", num);
                                goto end;
                        }
                        if (memcmp(padding, targets[num], sizeof(padding))) {
                                printf("%d overwrite head\n", num);
                                goto end;
                        }
                        if (memcmp(padding,
                                   targets[num] + sizeof(padding) + text_len,
                                   sizeof(padding))) {
                                printf("%d overwrite tail\n", num);
                                goto end;
                        }
                }
        }

        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                const int num = (const int)((uint64_t)job->user_data);

                jobs_rx++;
                if (job->status != IMB_STATUS_COMPLETED) {
                        printf("%d Error status:%d, job %d",
                               __LINE__, job->status, num);
                        goto end;
                }
                if (memcmp(out_text, targets[num] + sizeof(padding),
                           text_len)) {
                        printf("%d mismatched\n", num);
                        goto end;
                }
                if (memcmp(padding, targets[num], sizeof(padding))) {
                        printf("%d overwrite head\n", num);
                        goto end;
                }
                if (memcmp(padding, targets[num] + sizeof(padding) + text_len,
                           sizeof(padding))) {
                        printf("%d overwrite tail\n", num);
                        goto end;
                }
        }

        if (jobs_rx != num_jobs) {
                printf("Expected %d jobs, received %d\n", num_jobs, jobs_rx);
                goto end;
        }
        ret = 0;

 end:
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++)
                free(targets[i]);
        free(targets);
        return ret;
}

static int
test_des(struct IMB_MGR *mb_mgr,
         const uint64_t *ks,
         const uint64_t *ks2,
         const uint64_t *ks3,
         const void *iv,
         const uint8_t *in_text,
         const uint8_t *out_text,
         unsigned text_len,
         int dir,
         int order,
         IMB_CIPHER_MODE cipher,
         const int in_place)
{
        int ret = 0;

        if (cipher == IMB_CIPHER_DES3) {
                if (ks2 == NULL && ks3 == NULL) {
                        ret |= test_des_many(mb_mgr, ks, ks, ks, iv, in_text,
                                             out_text, text_len, dir, order,
                                             cipher, in_place, 1);
                        ret |= test_des_many(mb_mgr, ks, ks, ks, iv, in_text,
                                             out_text, text_len, dir, order,
                                             cipher, in_place, 32);
                } else {
                        ret |= test_des_many(mb_mgr, ks, ks2, ks3, iv, in_text,
                                             out_text, text_len, dir, order,
                                             cipher, in_place, 1);
                        ret |= test_des_many(mb_mgr, ks, ks2, ks3, iv, in_text,
                                             out_text, text_len, dir, order,
                                             cipher, in_place, 32);
                }
        } else {
                ret |= test_des_many(mb_mgr, ks, NULL, NULL, iv, in_text,
                                     out_text, text_len, dir, order, cipher,
                                     in_place, 1);
                ret |= test_des_many(mb_mgr, ks, NULL, NULL, iv, in_text,
                                     out_text, text_len, dir, order, cipher,
                                     in_place, 32);
        }
        return ret;
}

static void
test_des_vectors(struct IMB_MGR *mb_mgr,
                 const int vec_cnt,
                 const struct des_vector *vec_tab,
                 const char *banner,
                 const IMB_CIPHER_MODE cipher,
                 struct test_suite_context *ctx)
{
	int vect;
        uint64_t ks[16];

	printf("%s:\n", banner);
	for (vect = 0; vect < vec_cnt; vect++) {
#ifdef DEBUG
		printf("Standard vector %d/%d  PTLen:%d\n",
                       vect + 1, vec_cnt,
                       (int) vec_tab[vect].Plen);
#else
		printf(".");
#endif
                des_key_schedule(ks, vec_tab[vect].K);

                if (test_des(mb_mgr, ks, NULL, NULL,
                             vec_tab[vect].IV,
                             vec_tab[vect].P, vec_tab[vect].C,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_ENCRYPT, IMB_ORDER_CIPHER_HASH,
                             cipher, 0)) {
                        printf("error #%d encrypt\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }

                if (test_des(mb_mgr, ks, NULL, NULL,
                             vec_tab[vect].IV,
                             vec_tab[vect].C, vec_tab[vect].P,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_DECRYPT, IMB_ORDER_HASH_CIPHER,
                             cipher, 0)) {
                        printf("error #%d decrypt\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }

                if (test_des(mb_mgr, ks, NULL, NULL,
                             vec_tab[vect].IV,
                             vec_tab[vect].P, vec_tab[vect].C,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_ENCRYPT, IMB_ORDER_CIPHER_HASH,
                             cipher, 1)) {
                        printf("error #%d encrypt in-place\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }

                if (test_des(mb_mgr, ks, NULL, NULL,
                             vec_tab[vect].IV,
                             vec_tab[vect].C, vec_tab[vect].P,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_DECRYPT, IMB_ORDER_HASH_CIPHER,
                             cipher, 1)) {
                        printf("error #%d decrypt in-place\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }
	}
	printf("\n");
}

static void
test_des3_vectors(struct IMB_MGR *mb_mgr,
                  const int vec_cnt,
                  const struct des3_vector *vec_tab,
                  const char *banner,
                  struct test_suite_context *ctx)
{
	int vect;
        uint64_t ks1[16];
        uint64_t ks2[16];
        uint64_t ks3[16];

	printf("%s:\n", banner);
	for (vect = 0; vect < vec_cnt; vect++) {
#ifdef DEBUG
		printf("Standard vector %d/%d  PTLen:%d\n",
                       vect + 1, vec_cnt,
                       (int) vec_tab[vect].Plen);
#else
		printf(".");
#endif
                des_key_schedule(ks1, vec_tab[vect].K1);
                des_key_schedule(ks2, vec_tab[vect].K2);
                des_key_schedule(ks3, vec_tab[vect].K3);

                if (test_des(mb_mgr, ks1, ks2, ks3,
                             vec_tab[vect].IV,
                             vec_tab[vect].P, vec_tab[vect].C,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_ENCRYPT, IMB_ORDER_CIPHER_HASH,
                             IMB_CIPHER_DES3, 0)) {
                        printf("error #%d encrypt\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }

                if (test_des(mb_mgr, ks1, ks2, ks3,
                             vec_tab[vect].IV,
                             vec_tab[vect].C, vec_tab[vect].P,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_DECRYPT, IMB_ORDER_HASH_CIPHER,
                             IMB_CIPHER_DES3, 0)) {
                        printf("error #%d decrypt\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }

                if (test_des(mb_mgr, ks1, ks2, ks3,
                             vec_tab[vect].IV,
                             vec_tab[vect].P, vec_tab[vect].C,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_ENCRYPT, IMB_ORDER_CIPHER_HASH,
                             IMB_CIPHER_DES3, 1)) {
                        printf("error #%d encrypt in-place\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }

                if (test_des(mb_mgr, ks1, ks2, ks3,
                             vec_tab[vect].IV,
                             vec_tab[vect].C, vec_tab[vect].P,
                             (unsigned) vec_tab[vect].Plen,
                             IMB_DIR_DECRYPT, IMB_ORDER_HASH_CIPHER,
                             IMB_CIPHER_DES3, 1)) {
                        printf("error #%d decrypt in-place\n", vect + 1);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }
	}
	printf("\n");
}

int
des_test(const enum arch_type arch,
         struct IMB_MGR *mb_mgr)
{
        struct test_suite_context ctx;
        int errors;

        (void) arch;

        test_suite_start(&ctx, "DES-CBC-64");
        test_des_vectors(mb_mgr, DIM(vectors), vectors,
                         "DES standard test vectors", IMB_CIPHER_DES, &ctx);
        errors = test_suite_end(&ctx);

        test_suite_start(&ctx, "DOCSIS-DES-64");
        test_des_vectors(mb_mgr, DIM(docsis_vectors), docsis_vectors,
                         "DOCSIS DES standard test vectors",
                         IMB_CIPHER_DOCSIS_DES, &ctx);
        errors += test_suite_end(&ctx);

        test_suite_start(&ctx, "3DES-CBC-192");
        test_des_vectors(mb_mgr, DIM(vectors), vectors,
                         "3DES (single key) standard test vectors",
                         IMB_CIPHER_DES3, &ctx);
        test_des3_vectors(mb_mgr, DIM(des3_vectors), des3_vectors,
                          "3DES (multiple keys) test vectors", &ctx);
        errors += test_suite_end(&ctx);

	return errors;
}
