/*****************************************************************************
 Copyright (c) 2018-2021, Intel Corporation

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
#include "gcm_ctr_vectors_test.h"
#include "utils.h"

int hmac_sha1_test(struct IMB_MGR *mb_mgr);

#define block_size    64
#define digest_size   20
#define digest96_size 12

/*
 * Test vectors from https://tools.ietf.org/html/rfc2202
 */

/*
 * test_case =     1
 * key =           0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b
 * key_len =       20
 * data =          "Hi There"
 * data_len =      8
 * digest =        0xb617318655057264e28bc0b6fb378c8ef146be00
 */
#define test_case1  "1"
#define key_len1    20
#define data_len1   8
#define digest_len1 digest_size
static const uint8_t key1[key_len1] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b
};
static const char data1[] = "Hi There";
static const uint8_t digest1[digest_len1] = {
        0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64,
        0xe2, 0x8b, 0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e,
        0xf1, 0x46, 0xbe, 0x00
};

/*
 * test_case =     2
 * key =           "Jefe"
 * key_len =       4
 * data =          "what do ya want for nothing?"
 * data_len =      28
 * digest =        0xeffcdf6ae5eb2fa2d27416d5f184df9c259a7c79
 */
#define test_case2  "2"
#define key_len2    4
#define data_len2   28
#define digest_len2 digest_size
static const char key2[] = "Jefe";
static const char data2[] = "what do ya want for nothing?";
static const uint8_t digest2[digest_len2] = {
        0xef, 0xfc, 0xdf, 0x6a, 0xe5, 0xeb, 0x2f, 0xa2,
        0xd2, 0x74, 0x16, 0xd5, 0xf1, 0x84, 0xdf, 0x9c,
        0x25, 0x9a, 0x7c, 0x79
};

/*
 * test_case =     3
 * key =           0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
 * key_len =       20
 * data =          0xdd repeated 50 times
 * data_len =      50
 * digest =        0x125d7342b9ac11cd91a39af48aa17b4f63f175d3
 */
#define test_case3  "3"
#define key_len3    20
#define data_len3   50
#define digest_len3 digest_size
static const uint8_t key3[key_len3] = {
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa
};
static const uint8_t data3[data_len3] = {
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd
};
static const uint8_t digest3[digest_len3] = {
        0x12, 0x5d, 0x73, 0x42, 0xb9, 0xac, 0x11, 0xcd,
        0x91, 0xa3, 0x9a, 0xf4, 0x8a, 0xa1, 0x7b, 0x4f,
        0x63, 0xf1, 0x75, 0xd3
};

/*
 * test_case =     4
 * key =           0x0102030405060708090a0b0c0d0e0f10111213141516171819
 * key_len =       25
 * data =          0xcd repeated 50 times
 * data_len =      50
 * digest =        0x4c9007f4026250c6bc8414f9bf50c86c2d7235da
 */
#define test_case4  "4"
#define key_len4    25
#define data_len4   50
#define digest_len4 digest_size
static const uint8_t key4[key_len4] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19
};
static const uint8_t data4[data_len4] = {
        0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
        0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
        0xcd, 0xcd
};
static const uint8_t digest4[digest_len4] = {
        0x4c, 0x90, 0x07, 0xf4, 0x02, 0x62, 0x50, 0xc6,
        0xbc, 0x84, 0x14, 0xf9, 0xbf, 0x50, 0xc8, 0x6c,
        0x2d, 0x72, 0x35, 0xda
};

/*
 * test_case =     5
 * key =           0x0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c
 * key_len =       20
 * data =          "Test With Truncation"
 * data_len =      20
 * digest =        0x4c1a03424b55e07fe7f27be1d58bb9324a9a5a04
 * digest-96 =     0x4c1a03424b55e07fe7f27be1
 */
#define test_case5  "5"
#define key_len5    20
#define data_len5   20
#define digest_len5 digest_size
static const uint8_t key5[key_len5] = {
        0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
        0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
        0x0c, 0x0c, 0x0c, 0x0c
};
static const char data5[] = "Test With Truncation";
static const uint8_t digest5[digest_len5] = {
        0x4c, 0x1a, 0x03, 0x42, 0x4b, 0x55, 0xe0, 0x7f,
        0xe7, 0xf2, 0x7b, 0xe1, 0xd5, 0x8b, 0xb9, 0x32,
        0x4a, 0x9a, 0x5a, 0x04
};

#define test_case5_96  "5-96"
#define key_len5_96    key_len5
#define data_len5_96   data_len5
#define digest_len5_96 digest96_size
#define key5_96 key5
#define data5_96 data5
static const uint8_t digest5_96[digest_len5_96] = {
        0x4c, 0x1a, 0x03, 0x42, 0x4b, 0x55, 0xe0, 0x7f,
        0xe7, 0xf2, 0x7b, 0xe1
};

/*
 * test_case =     6
 * key =           0xaa repeated 80 times
 * key_len =       80
 * data =          "Test Using Larger Than Block-Size Key - Hash Key First"
 * data_len =      54
 * digest =        0xaa4ae5e15272d00e95705637ce8a3b55ed402112
 */
#define test_case6  "6"
#define key_len6    80
#define data_len6   54
#define digest_len6 digest_size
static const uint8_t key6[key_len6] = {
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
};
static const char data6[] =
        "Test Using Larger Than Block-Size Key - Hash Key First";
static const uint8_t digest6[digest_len6] = {
        0xaa, 0x4a, 0xe5, 0xe1, 0x52, 0x72, 0xd0, 0x0e,
        0x95, 0x70, 0x56, 0x37, 0xce, 0x8a, 0x3b, 0x55,
        0xed, 0x40, 0x21, 0x12
};

/*
 * test_case =     7
 * key =           0xaa repeated 80 times
 * key_len =       80
 * data =          "Test Using Larger Than Block-Size Key and Larger
 *         Than One Block-Size Data"
 * data_len =      73
 * digest =        0xe8e99d0f45237d786d6bbaa7965c7808bbff1a91
 */
#define test_case7  "7"
#define key_len7    80
#define data_len7   73
#define digest_len7 digest_size
static const uint8_t key7[key_len7] = {
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
};
static const char data7[] =
        "Test Using Larger Than Block-Size Key and "
        "Larger Than One Block-Size Data";
static const uint8_t digest7[digest_len7] = {
        0xe8, 0xe9, 0x9d, 0x0f, 0x45, 0x23, 0x7d, 0x78,
        0x6d, 0x6b, 0xba, 0xa7, 0x96, 0x5c, 0x78, 0x08,
        0xbb, 0xff, 0x1a, 0x91
};

/*
 * Test vector from https://csrc.nist.gov/csrc/media/publications/fips/198/
 * archive/2002-03-06/documents/fips-198a.pdf
 */
#define test_case8  "8"
#define key_len8    49
#define data_len8   9
#define digest_len8 digest96_size
static const uint8_t key8[key_len8] = {
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
        0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
        0xa0
};
static const char data8[] = "Sample #4";
static const uint8_t digest8[digest_len8] = {
        0x9e, 0xa8, 0x86, 0xef, 0xe2, 0x68, 0xdb, 0xec,
        0xce, 0x42, 0x0c, 0x75
};

#define HMAC_SHA1_TEST_VEC(num)                                         \
        { test_case##num,                                               \
                        (const uint8_t *) key##num, key_len##num,       \
                        (const uint8_t *) data##num, data_len##num,     \
                        (const uint8_t *) digest##num, digest_len##num }

static const struct hmac_sha1_rfc2202_vector {
        const char *test_case;
        const uint8_t *key;
        size_t key_len;
        const uint8_t *data;
        size_t data_len;
        const uint8_t *digest;
        size_t digest_len;
} hmac_sha1_vectors[] = {
        HMAC_SHA1_TEST_VEC(1),
        HMAC_SHA1_TEST_VEC(2),
        HMAC_SHA1_TEST_VEC(3),
        HMAC_SHA1_TEST_VEC(4),
        HMAC_SHA1_TEST_VEC(5),
        HMAC_SHA1_TEST_VEC(5_96),
        HMAC_SHA1_TEST_VEC(6),
        HMAC_SHA1_TEST_VEC(7),
        HMAC_SHA1_TEST_VEC(8)
};

static int
hmac_sha1_job_ok(const struct hmac_sha1_rfc2202_vector *vec,
                 const struct IMB_JOB *job,
                 const uint8_t *auth,
                 const uint8_t *padding,
                 const size_t sizeof_padding)
{
        if (job->status != IMB_STATUS_COMPLETED) {
                printf("line:%d job error status:%d ", __LINE__, job->status);
                return 0;
        }

        /* hash checks */
        if (memcmp(padding, &auth[sizeof_padding + vec->digest_len],
                   sizeof_padding)) {
                printf("hash overwrite tail\n");
                hexdump(stderr, "Target",
                        &auth[sizeof_padding + vec->digest_len],
                        sizeof_padding);
                return 0;
        }

        if (memcmp(padding, &auth[0], sizeof_padding)) {
                printf("hash overwrite head\n");
                hexdump(stderr, "Target", &auth[0], sizeof_padding);
                return 0;
        }

        if (memcmp(vec->digest, &auth[sizeof_padding],
                   vec->digest_len)) {
                printf("hash mismatched\n");
                hexdump(stderr, "Received", &auth[sizeof_padding],
                        vec->digest_len);
                hexdump(stderr, "Expected", vec->digest,
                        vec->digest_len);
                return 0;
        }
        return 1;
}

static int
test_hmac_sha1(struct IMB_MGR *mb_mgr,
               const struct hmac_sha1_rfc2202_vector *vec,
               const int num_jobs)
{
        struct IMB_JOB *job;
        uint8_t padding[16];
        uint8_t **auths = malloc(num_jobs * sizeof(void *));
        int i = 0, jobs_rx = 0, ret = -1;
        uint8_t key[block_size];
        uint8_t buf[block_size];
        DECLARE_ALIGNED(uint8_t ipad_hash[digest_size], 16);
        DECLARE_ALIGNED(uint8_t opad_hash[digest_size], 16);
        int key_len = 0;

        if (auths == NULL) {
		fprintf(stderr, "Can't allocate buffer memory\n");
		goto end2;
        }

        memset(padding, -1, sizeof(padding));
        memset(auths, 0, num_jobs * sizeof(void *));

        for (i = 0; i < num_jobs; i++) {
                const size_t alloc_len =
                        vec->digest_len + (sizeof(padding) * 2);

                auths[i] = malloc(alloc_len);
                if (auths[i] == NULL) {
                        fprintf(stderr, "Can't allocate buffer memory\n");
                        goto end;
                }
                memset(auths[i], -1, alloc_len);
        }

        /* prepare the key */
        memset(key, 0, sizeof(key));
        if (vec->key_len <= block_size) {
                memcpy(key, vec->key, vec->key_len);
                key_len = (int) vec->key_len;
        } else {
                IMB_SHA1(mb_mgr, vec->key, vec->key_len, key);
                key_len = digest_size;
        }

        /* compute ipad hash */
        memset(buf, 0x36, sizeof(buf));
        for (i = 0; i < key_len; i++)
                buf[i] ^= key[i];
        IMB_SHA1_ONE_BLOCK(mb_mgr, buf, ipad_hash);

        /* compute opad hash */
        memset(buf, 0x5c, sizeof(buf));
        for (i = 0; i < key_len; i++)
                buf[i] ^= key[i];
        IMB_SHA1_ONE_BLOCK(mb_mgr, buf, opad_hash);

        /* empty the manager */
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->enc_keys = NULL;
                job->dec_keys = NULL;
                job->cipher_direction = IMB_DIR_ENCRYPT;
                job->chain_order = IMB_ORDER_HASH_CIPHER;
                job->dst = NULL;
                job->key_len_in_bytes = 0;
                job->auth_tag_output = auths[i] + sizeof(padding);
                job->auth_tag_output_len_in_bytes = vec->digest_len;
                job->iv = NULL;
                job->iv_len_in_bytes = 0;
                job->src = vec->data;
                job->cipher_start_src_offset_in_bytes = 0;
                job->msg_len_to_cipher_in_bytes = 0;
                job->hash_start_src_offset_in_bytes = 0;
                job->msg_len_to_hash_in_bytes = vec->data_len;
                job->u.HMAC._hashed_auth_key_xor_ipad = ipad_hash;
                job->u.HMAC._hashed_auth_key_xor_opad = opad_hash;
                job->cipher_mode = IMB_CIPHER_NULL;
                job->hash_alg = IMB_AUTH_HMAC_SHA_1;

                job->user_data = auths[i];

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job) {
                        jobs_rx++;
                        /*
                         * SHANI HMAC-SHA implementation can return a completed
                         * job after 2nd submission
                         */
                        if (num_jobs < 2) {
                                printf("%d Unexpected return from submit_job\n",
                                       __LINE__);
                                goto end;
                        }
                        if (!hmac_sha1_job_ok(vec, job, job->user_data,
                                              padding, sizeof(padding)))
                                goto end;
                }
        }

        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                jobs_rx++;
                if (!hmac_sha1_job_ok(vec, job, job->user_data,
                                      padding, sizeof(padding)))
                        goto end;
        }

        if (jobs_rx != num_jobs) {
                printf("Expected %d jobs, received %d\n", num_jobs, jobs_rx);
                goto end;
        }
        ret = 0;

 end:
        /* empty the manager before next tests */
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++) {
                if (auths[i] != NULL)
                        free(auths[i]);
        }

 end2:
        if (auths != NULL)
                free(auths);

        return ret;
}

static void
test_hmac_sha1_std_vectors(struct IMB_MGR *mb_mgr,
                           const int num_jobs,
                           struct test_suite_context *ts)
{
	const int vectors_cnt = DIM(hmac_sha1_vectors);
	int vect;

	printf("HMAC-SHA1 standard test vectors (N jobs = %d):\n", num_jobs);
	for (vect = 1; vect <= vectors_cnt; vect++) {
                const int idx = vect - 1;
#ifdef DEBUG
		printf("[%d/%d] RFC2202 Test Case %s key_len:%d data_len:%d "
                       "digest_len:%d\n",
                       vect, vectors_cnt,
                       hmac_sha1_vectors[idx].test_case,
                       (int) hmac_sha1_vectors[idx].key_len,
                       (int) hmac_sha1_vectors[idx].data_len,
                       (int) hmac_sha1_vectors[idx].digest_len);
#else
		printf(".");
#endif

                if (test_hmac_sha1(mb_mgr, &hmac_sha1_vectors[idx], num_jobs)) {
                        printf("error #%d\n", vect);
                        test_suite_update(ts, 0, 1);
                } else {
                        test_suite_update(ts, 1, 0);
                }
	}
	printf("\n");
}

int
hmac_sha1_test(struct IMB_MGR *mb_mgr)
{
        struct test_suite_context ts;
        int num_jobs, errors = 0;

        test_suite_start(&ts, "HMAC-SHA1");
        for (num_jobs = 1; num_jobs <= 17; num_jobs++)
                test_hmac_sha1_std_vectors(mb_mgr, num_jobs, &ts);
        errors = test_suite_end(&ts);

	return errors;
}
