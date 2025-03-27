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

int hmac_md5_test(struct IMB_MGR *mb_mgr);

#define block_size    64
#define digest_size   16
#define digest96_size 12

/*
 * Test vectors from https://tools.ietf.org/html/rfc2202
 */

/*
 * 2.  Test Case 1
 *
 *    Key =          0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b
 *
 *    Key length =   16
 *
 *    Data =         "Hi There"
 *
 *    Data length =  8
 *
 *    Digest =       0x9294727a3638bb1c13f48ef8158bfc9d
 *
 *    Digest96 =     0x9294727a3638bb1c13f48ef8
 */
#define test_case1      "1"
#define test_case_l1    "1_long"
#define key_len1        16
#define data_len1       8
#define digest_len1     digest96_size
#define digest_len_l1   digest_size
static const uint8_t key1[key_len1] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
};
static const char data1[] = "Hi There";
static const uint8_t digest1[digest_len_l1] = {
        0x92, 0x94, 0x72, 0x7a, 0x36, 0x38, 0xbb, 0x1c,
        0x13, 0xf4, 0x8e, 0xf8, 0x15, 0x8b, 0xfc, 0x9d
};

/*
 * 2.  Test Case 2
 *
 *    Key =          "Jefe"
 *
 *    Key length =   4
 *
 *    Data =         "what do ya want for nothing?"
 *
 *    Data length =  28
 *
 *    Digest =       0x750c783e6ab0b503eaa86e310a5db738
 *
 *    Digest96 =     0x750c783e6ab0b503eaa86e31
 */
#define test_case2    "2"
#define test_case_l2  "2_long"
#define key_len2      4
#define data_len2     28
#define digest_len2   digest96_size
#define digest_len_l2 digest_size
static const char key2[] = "Jefe";
static const char data2[] = "what do ya want for nothing?";
static const uint8_t digest2[digest_len_l2] = {
        0x75, 0x0c, 0x78, 0x3e, 0x6a, 0xb0, 0xb5, 0x03,
        0xea, 0xa8, 0x6e, 0x31, 0x0a, 0x5d, 0xb7, 0x38
};

/*
 * 2.  Test Case 3
 *
 *    Key =          0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
 *
 *    Key length =   16
 *
 *    Data =         0xdd (repeated 50 times)
 *
 *    Data length =  50
 *
 *    Digest =       0x56be34521d144c88dbb8c733f0e8b3f6
 *
 *    Digest96 =     0x56be34521d144c88dbb8c733
 */
#define test_case3    "3"
#define test_case_l3  "3_long"
#define key_len3      16
#define data_len3     50
#define digest_len3   digest96_size
#define digest_len_l3 digest_size
static const uint8_t key3[key_len3] = {
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa
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
static const uint8_t digest3[digest_len_l3] = {
        0x56, 0xbe, 0x34, 0x52, 0x1d, 0x14, 0x4c, 0x88,
        0xdb, 0xb8, 0xc7, 0x33, 0xf0, 0xe8, 0xb3, 0xf6
};

/*
 * 2.  Test Case 4
 *
 *    Key =          0x0102030405060708090a0b0c0d0e0f10111213141516171819
 *
 *    Key length =   25
 *
 *    Data =         0xcd (repeated 50 times)
 *
 *    Data length =  50
 *
 *    Digest =       0x697eaf0aca3a3aea3a75164746ffaa79
 *
 *    Digest96 =     0x697eaf0aca3a3aea3a751647
 */
#define test_case4    "4"
#define test_case_l4  "4_long"
#define key_len4      25
#define data_len4     50
#define digest_len4   digest96_size
#define digest_len_l4 digest_size
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
static const uint8_t digest4[digest_len_l4] = {
        0x69, 0x7e, 0xaf, 0x0a, 0xca, 0x3a, 0x3a, 0xea,
        0x3a, 0x75, 0x16, 0x47, 0x46, 0xff, 0xaa, 0x79
};

/*
 * 2.  Test Case 5
 *
 *    Key =          0x0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c
 *
 *    Key length =   16
 *
 *    Data =         "Test With Truncation"
 *
 *    Data length =  20
 *
 *    Digest =       0x56461ef2342edc00f9bab995690efd4c
 *
 *    Digest96 =     0x56461ef2342edc00f9bab995
 */
#define test_case5    "5"
#define test_case_l5  "5_long"
#define key_len5      16
#define data_len5     20
#define digest_len5   digest96_size
#define digest_len_l5 digest_size
static const uint8_t key5[key_len5] = {
        0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
        0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c
};
static const char data5[] = "Test With Truncation";
static const uint8_t digest5[digest_len_l5] = {
        0x56, 0x46, 0x1e, 0xf2, 0x34, 0x2e, 0xdc, 0x00,
        0xf9, 0xba, 0xb9, 0x95, 0x69, 0x0e, 0xfd, 0x4c
};

/*
 * 2.  Test Case 6
 *
 *    Key =          0xaa (repeated 80 times)
 *
 *    Key length =   80
 *
 *    Data =         "Test Using Larger Than Block-Size Key - Hash Key First"
 *
 *    Data length =  54
 *
 *    Digest =       0x6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd
 *
 *    Digest96 =     0x6b1ab7fe4bd7bf8f0b62e6ce
 */
/* #define test_case6  "6" */
/* #define key_len6    80 */
/* #define data_len6   54 */
/* #define digest_len6 digest96_size */
/* static const uint8_t key6[key_len6] = { */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa */
/* }; */
/* static const char data6[] = "Test Using Larger Than Block-Size " */
/*         "Key - Hash Key First"; */
/* static const uint8_t digest6[digest_len6] = { */
/*         0x6b, 0x1a, 0xb7, 0xfe, 0x4b, 0xd7, 0xbf, 0x8f, */
/*         0x0b, 0x62, 0xe6, 0xce */
/* }; */

/*
 * 2.  Test Case 7
 *
 *    Key =          0xaa (repeated 80 times)
 *
 *    Key length =   80
 *
 *    Data =         "Test Using Larger Than Block-Size Key and Larger"
 *
 *    Data length =  73
 *
 *    Digest =       0x6f630fad67cda0ee1fb1f562db3aa53e
 *
 *    Digest96 =     0x6f630fad67cda0ee1fb1f562
 */
/* #define test_case7  "7" */
/* #define key_len7    80 */
/* #define data_len7   73 */
/* #define digest_len7 digest96_size */
/* static const uint8_t key7[key_len7] = { */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, */
/*         0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa */
/* }; */
/* static const char data7[] = "Test Using Larger Than Block-Size " */
/*         "Key and Larger Than One Block-Size Data"; */
/* static const uint8_t digest7[digest_len7] = { */
/*         0x6f, 0x63, 0x0f, 0xad, 0x67, 0xcd, 0xa0, 0xee, */
/*         0x1f, 0xb1, 0xf5, 0x62 */
/* }; */

#define HMAC_MD5_TEST_VEC(num)                                          \
        { test_case##num,                                               \
                        (const uint8_t *) key##num, key_len##num,       \
                        (const uint8_t *) data##num, data_len##num,     \
                        (const uint8_t *) digest##num, digest_len##num }
#define HMAC_MD5_TEST_VEC_LONG(num)                                     \
        { test_case_l##num,                                             \
                        (const uint8_t *) key##num, key_len##num,       \
                        (const uint8_t *) data##num, data_len##num,     \
                        (const uint8_t *) digest##num, digest_len_l##num }

static const struct hmac_md5_rfc2202_vector {
        const char *test_case;
        const uint8_t *key;
        size_t key_len;
        const uint8_t *data;
        size_t data_len;
        const uint8_t *digest;
        size_t digest_len;
} hmac_md5_vectors[] = {
        HMAC_MD5_TEST_VEC(1),
        HMAC_MD5_TEST_VEC(2),
        HMAC_MD5_TEST_VEC(3),
        HMAC_MD5_TEST_VEC(4),
        HMAC_MD5_TEST_VEC(5),
        /* HMAC_MD5_TEST_VEC(6), */
        /* HMAC_MD5_TEST_VEC(7), */
        HMAC_MD5_TEST_VEC_LONG(1),
        HMAC_MD5_TEST_VEC_LONG(2),
        HMAC_MD5_TEST_VEC_LONG(3),
        HMAC_MD5_TEST_VEC_LONG(4),
        HMAC_MD5_TEST_VEC_LONG(5),
};

static int
hmac_md5_job_ok(const struct hmac_md5_rfc2202_vector *vec,
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
test_hmac_md5(struct IMB_MGR *mb_mgr,
               const struct hmac_md5_rfc2202_vector *vec,
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
                printf("Key length longer than block size is not supported "
                       "by MD5\n");
                ret = 0;
                goto end;
        }

        /* compute ipad hash */
        memset(buf, 0x36, sizeof(buf));
        for (i = 0; i < key_len; i++)
                buf[i] ^= key[i];
        IMB_MD5_ONE_BLOCK(mb_mgr, buf, ipad_hash);

        /* compute opad hash */
        memset(buf, 0x5c, sizeof(buf));
        for (i = 0; i < key_len; i++)
                buf[i] ^= key[i];
        IMB_MD5_ONE_BLOCK(mb_mgr, buf, opad_hash);

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
                job->hash_alg = IMB_AUTH_MD5;

                job->user_data = auths[i];

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job) {
                        jobs_rx++;
                        /*
                         * HMAC-MD5 requires 8 submissions to get one back
                         */
                        if (num_jobs < 8) {
                                printf("%d Unexpected return from submit_job\n",
                                       __LINE__);
                                goto end;
                        }
                        if (!hmac_md5_job_ok(vec, job, job->user_data,
                                              padding, sizeof(padding)))
                                goto end;
                }
        }

        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                jobs_rx++;
                if (!hmac_md5_job_ok(vec, job, job->user_data,
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
test_hmac_md5_std_vectors(struct IMB_MGR *mb_mgr,
                          const int num_jobs,
                          struct test_suite_context *ts)
{
	const int vectors_cnt = DIM(hmac_md5_vectors);
	int vect;

	printf("HMAC-MD5 standard test vectors (N jobs = %d):\n", num_jobs);
	for (vect = 1; vect <= vectors_cnt; vect++) {
                const int idx = vect - 1;
#ifdef DEBUG
		printf("[%d/%d] RFC2202 Test Case %s key_len:%d data_len:%d "
                       "digest_len:%d\n",
                       vect, vectors_cnt,
                       hmac_md5_vectors[idx].test_case,
                       (int) hmac_md5_vectors[idx].key_len,
                       (int) hmac_md5_vectors[idx].data_len,
                       (int) hmac_md5_vectors[idx].digest_len);
#else
		printf(".");
#endif

                if (test_hmac_md5(mb_mgr, &hmac_md5_vectors[idx], num_jobs)) {
                        printf("error #%d\n", vect);
                        test_suite_update(ts, 0, 1);
                } else {
                        test_suite_update(ts, 1, 0);
                }
	}
	printf("\n");
}

int
hmac_md5_test(struct IMB_MGR *mb_mgr)
{
        struct test_suite_context ts;
        int num_jobs, errors = 0;

        test_suite_start(&ts, "HMAC-MD5");
        for (num_jobs = 1; num_jobs <= 17; num_jobs++)
                test_hmac_md5_std_vectors(mb_mgr, num_jobs, &ts);
        errors = test_suite_end(&ts);

	return errors;
}
