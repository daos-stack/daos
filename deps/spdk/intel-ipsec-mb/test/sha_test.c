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

int sha_test(struct IMB_MGR *mb_mgr);

/*
 * Test vectors come from this NIST document:
 *
 * https://csrc.nist.gov/csrc/media/projects/
 *     cryptographic-standards-and-guidelines/documents/examples/sha_all.pdf
 */
static const char message1[] = "abc";
#define message1_len 3

static const char message2[] = "";
#define message2_len 0

static const char message3[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
#define message3_len 56

static const char message4[] =
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmn"
        "opjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
#define message4_len 112

/* macro converts one 32-bit word into four 8-bit word */
#define CONVERT_UINT32_TO_4xUINT8(v)                            \
        (((v) >> 24) & 0xff), (((v) >> 16) & 0xff),             \
                (((v) >> 8) & 0xff), (((v) >> 0) & 0xff)

/* macro converts one 64-bit word into eight 8-bit word */
#define CONVERT_UINT64_TO_8xUINT8(v)                            \
        (((v) >> 56) & 0xff), (((v) >> 48) & 0xff),             \
                (((v) >> 40) & 0xff), (((v) >> 32) & 0xff),     \
                (((v) >> 24) & 0xff), (((v) >> 16) & 0xff),     \
                (((v) >> 8) & 0xff), (((v) >> 0) & 0xff)

static const char test_case1[] = "SHA-1 MSG1";
#define data1 ((const uint8_t *)message1)
#define data_len1 message1_len
static const uint8_t digest1[] = {
        /* a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d */
         CONVERT_UINT32_TO_4xUINT8(0xa9993e36),
         CONVERT_UINT32_TO_4xUINT8(0x4706816a),
         CONVERT_UINT32_TO_4xUINT8(0xba3e2571),
         CONVERT_UINT32_TO_4xUINT8(0x7850c26c),
         CONVERT_UINT32_TO_4xUINT8(0x9cd0d89d)
};
#define digest_len1 sizeof(digest1)

static const char test_case2[] = "SHA-224 MSG1";
#define data2 ((const uint8_t *)message1)
#define data_len2 message1_len
static const uint8_t digest2[] = {
        /* 23097d22 3405d822 8642a477 bda255b3 */
        /* 2aadbce4 bda0b3f7 e36c9da7 */
        CONVERT_UINT32_TO_4xUINT8(0x23097d22),
        CONVERT_UINT32_TO_4xUINT8(0x3405d822),
        CONVERT_UINT32_TO_4xUINT8(0x8642a477),
        CONVERT_UINT32_TO_4xUINT8(0xbda255b3),
        CONVERT_UINT32_TO_4xUINT8(0x2aadbce4),
        CONVERT_UINT32_TO_4xUINT8(0xbda0b3f7),
        CONVERT_UINT32_TO_4xUINT8(0xe36c9da7)
};
#define digest_len2 sizeof(digest2)

static const char test_case3[] = "SHA-256 MSG1";
#define data3 ((const uint8_t *)message1)
#define data_len3 message1_len
static const uint8_t digest3[] = {
        /* ba7816bf 8f01cfea 414140de 5dae2223 */
        /* b00361a3 96177a9c b410ff61 f20015ad */
        CONVERT_UINT32_TO_4xUINT8(0xba7816bf),
        CONVERT_UINT32_TO_4xUINT8(0x8f01cfea),
        CONVERT_UINT32_TO_4xUINT8(0x414140de),
        CONVERT_UINT32_TO_4xUINT8(0x5dae2223),
        CONVERT_UINT32_TO_4xUINT8(0xb00361a3),
        CONVERT_UINT32_TO_4xUINT8(0x96177a9c),
        CONVERT_UINT32_TO_4xUINT8(0xb410ff61),
        CONVERT_UINT32_TO_4xUINT8(0xf20015ad)
};
#define digest_len3 sizeof(digest3)

static const char test_case4[] = "SHA-384 MSG1";
#define data4 ((const uint8_t *)message1)
#define data_len4 message1_len
static const uint8_t digest4[] = {
        /* cb00753f45a35e8b b5a03d699ac65007 */
        /* 272c32ab0eded163 1a8b605a43ff5bed */
        /* 8086072ba1e7cc23 58baeca134c825a7 */
        CONVERT_UINT64_TO_8xUINT8(0xcb00753f45a35e8b),
        CONVERT_UINT64_TO_8xUINT8(0xb5a03d699ac65007),
        CONVERT_UINT64_TO_8xUINT8(0x272c32ab0eded163),
        CONVERT_UINT64_TO_8xUINT8(0x1a8b605a43ff5bed),
        CONVERT_UINT64_TO_8xUINT8(0x8086072ba1e7cc23),
        CONVERT_UINT64_TO_8xUINT8(0x58baeca134c825a7)
};
#define digest_len4 sizeof(digest4)

static const char test_case5[] = "SHA-512 MSG1";
#define data5 ((const uint8_t *)message1)
#define data_len5 message1_len
static const uint8_t digest5[] = {
        /* ddaf35a193617aba cc417349ae204131 */
        /* 12e6fa4e89a97ea2 0a9eeee64b55d39a */
        /* 2192992a274fc1a8 36ba3c23a3feebbd */
        /* 454d4423643ce80e 2a9ac94fa54ca49f */
        CONVERT_UINT64_TO_8xUINT8(0xddaf35a193617aba),
        CONVERT_UINT64_TO_8xUINT8(0xcc417349ae204131),
        CONVERT_UINT64_TO_8xUINT8(0x12e6fa4e89a97ea2),
        CONVERT_UINT64_TO_8xUINT8(0x0a9eeee64b55d39a),
        CONVERT_UINT64_TO_8xUINT8(0x2192992a274fc1a8),
        CONVERT_UINT64_TO_8xUINT8(0x36ba3c23a3feebbd),
        CONVERT_UINT64_TO_8xUINT8(0x454d4423643ce80e),
        CONVERT_UINT64_TO_8xUINT8(0x2a9ac94fa54ca49f)
};
#define digest_len5 sizeof(digest5)

static const char test_case10[] = "SHA-1 MSG2";
#define data10 ((const uint8_t *)message2)
#define data_len10 message2_len
static const uint8_t digest10[] = {
        CONVERT_UINT32_TO_4xUINT8(0xda39a3ee),
        CONVERT_UINT32_TO_4xUINT8(0x5e6b4b0d),
        CONVERT_UINT32_TO_4xUINT8(0x3255bfef),
        CONVERT_UINT32_TO_4xUINT8(0x95601890),
        CONVERT_UINT32_TO_4xUINT8(0xafd80709)
};
#define digest_len10 sizeof(digest10)

static const char test_case11[] = "SHA-224 MSG2";
#define data11 ((const uint8_t *)message2)
#define data_len11 message2_len
static const uint8_t digest11[] = {
        CONVERT_UINT32_TO_4xUINT8(0xd14a028c),
        CONVERT_UINT32_TO_4xUINT8(0x2a3a2bc9),
        CONVERT_UINT32_TO_4xUINT8(0x476102bb),
        CONVERT_UINT32_TO_4xUINT8(0x288234c4),
        CONVERT_UINT32_TO_4xUINT8(0x15a2b01f),
        CONVERT_UINT32_TO_4xUINT8(0x828ea62a),
        CONVERT_UINT32_TO_4xUINT8(0xc5b3e42f)
};
#define digest_len11 sizeof(digest11)

static const char test_case12[] = "SHA-256 MSG2";
#define data12 ((const uint8_t *)message2)
#define data_len12 message2_len
static const uint8_t digest12[] = {
        CONVERT_UINT32_TO_4xUINT8(0xe3b0c442),
        CONVERT_UINT32_TO_4xUINT8(0x98fc1c14),
        CONVERT_UINT32_TO_4xUINT8(0x9afbf4c8),
        CONVERT_UINT32_TO_4xUINT8(0x996fb924),
        CONVERT_UINT32_TO_4xUINT8(0x27ae41e4),
        CONVERT_UINT32_TO_4xUINT8(0x649b934c),
        CONVERT_UINT32_TO_4xUINT8(0xa495991b),
        CONVERT_UINT32_TO_4xUINT8(0x7852b855)
};
#define digest_len12 sizeof(digest12)

static const char test_case13[] = "SHA-384 MSG2";
#define data13 ((const uint8_t *)message2)
#define data_len13 message2_len
static const uint8_t digest13[] = {
        CONVERT_UINT64_TO_8xUINT8(0x38b060a751ac9638),
        CONVERT_UINT64_TO_8xUINT8(0x4cd9327eb1b1e36a),
        CONVERT_UINT64_TO_8xUINT8(0x21fdb71114be0743),
        CONVERT_UINT64_TO_8xUINT8(0x4c0cc7bf63f6e1da),
        CONVERT_UINT64_TO_8xUINT8(0x274edebfe76f65fb),
        CONVERT_UINT64_TO_8xUINT8(0xd51ad2f14898b95b)
};
#define digest_len13 sizeof(digest13)

static const char test_case14[] = "SHA-512 MSG2";
#define data14 ((const uint8_t *)message2)
#define data_len14 message2_len
static const uint8_t digest14[] = {
        CONVERT_UINT64_TO_8xUINT8(0xcf83e1357eefb8bd),
        CONVERT_UINT64_TO_8xUINT8(0xf1542850d66d8007),
        CONVERT_UINT64_TO_8xUINT8(0xd620e4050b5715dc),
        CONVERT_UINT64_TO_8xUINT8(0x83f4a921d36ce9ce),
        CONVERT_UINT64_TO_8xUINT8(0x47d0d13c5d85f2b0),
        CONVERT_UINT64_TO_8xUINT8(0xff8318d2877eec2f),
        CONVERT_UINT64_TO_8xUINT8(0x63b931bd47417a81),
        CONVERT_UINT64_TO_8xUINT8(0xa538327af927da3e)
};
#define digest_len14 sizeof(digest14)

static const char test_case20[] = "SHA-1 MSG3";
#define data20 ((const uint8_t *)message3)
#define data_len20 message3_len
static const uint8_t digest20[] = {
        CONVERT_UINT32_TO_4xUINT8(0x84983e44),
        CONVERT_UINT32_TO_4xUINT8(0x1c3bd26e),
        CONVERT_UINT32_TO_4xUINT8(0xbaae4aa1),
        CONVERT_UINT32_TO_4xUINT8(0xf95129e5),
        CONVERT_UINT32_TO_4xUINT8(0xe54670f1)
};
#define digest_len20 sizeof(digest20)

static const char test_case21[] = "SHA-224 MSG3";
#define data21 ((const uint8_t *)message3)
#define data_len21 message3_len
static const uint8_t digest21[] = {
        CONVERT_UINT32_TO_4xUINT8(0x75388b16),
        CONVERT_UINT32_TO_4xUINT8(0x512776cc),
        CONVERT_UINT32_TO_4xUINT8(0x5dba5da1),
        CONVERT_UINT32_TO_4xUINT8(0xfd890150),
        CONVERT_UINT32_TO_4xUINT8(0xb0c6455c),
        CONVERT_UINT32_TO_4xUINT8(0xb4f58b19),
        CONVERT_UINT32_TO_4xUINT8(0x52522525)
};
#define digest_len21 sizeof(digest21)

static const char test_case22[] = "SHA-256 MSG3";
#define data22 ((const uint8_t *)message3)
#define data_len22 message3_len
static const uint8_t digest22[] = {
        CONVERT_UINT32_TO_4xUINT8(0x248d6a61),
        CONVERT_UINT32_TO_4xUINT8(0xd20638b8),
        CONVERT_UINT32_TO_4xUINT8(0xe5c02693),
        CONVERT_UINT32_TO_4xUINT8(0x0c3e6039),
        CONVERT_UINT32_TO_4xUINT8(0xa33ce459),
        CONVERT_UINT32_TO_4xUINT8(0x64ff2167),
        CONVERT_UINT32_TO_4xUINT8(0xf6ecedd4),
        CONVERT_UINT32_TO_4xUINT8(0x19db06c1)
};
#define digest_len22 sizeof(digest22)

static const char test_case23[] = "SHA-384 MSG3";
#define data23 ((const uint8_t *)message3)
#define data_len23 message3_len
static const uint8_t digest23[] = {
        CONVERT_UINT64_TO_8xUINT8(0x3391fdddfc8dc739),
        CONVERT_UINT64_TO_8xUINT8(0x3707a65b1b470939),
        CONVERT_UINT64_TO_8xUINT8(0x7cf8b1d162af05ab),
        CONVERT_UINT64_TO_8xUINT8(0xfe8f450de5f36bc6),
        CONVERT_UINT64_TO_8xUINT8(0xb0455a8520bc4e6f),
        CONVERT_UINT64_TO_8xUINT8(0x5fe95b1fe3c8452b)
};
#define digest_len23 sizeof(digest23)

static const char test_case24[] = "SHA-512 MSG3";
#define data24 ((const uint8_t *)message3)
#define data_len24 message3_len
static const uint8_t digest24[] = {
        CONVERT_UINT64_TO_8xUINT8(0x204a8fc6dda82f0a),
        CONVERT_UINT64_TO_8xUINT8(0x0ced7beb8e08a416),
        CONVERT_UINT64_TO_8xUINT8(0x57c16ef468b228a8),
        CONVERT_UINT64_TO_8xUINT8(0x279be331a703c335),
        CONVERT_UINT64_TO_8xUINT8(0x96fd15c13b1b07f9),
        CONVERT_UINT64_TO_8xUINT8(0xaa1d3bea57789ca0),
        CONVERT_UINT64_TO_8xUINT8(0x31ad85c7a71dd703),
        CONVERT_UINT64_TO_8xUINT8(0x54ec631238ca3445)
};
#define digest_len24 sizeof(digest24)

static const char test_case30[] = "SHA-1 MSG4";
#define data30 ((const uint8_t *)message4)
#define data_len30 message4_len
static const uint8_t digest30[] = {
        CONVERT_UINT32_TO_4xUINT8(0xa49b2446),
        CONVERT_UINT32_TO_4xUINT8(0xa02c645b),
        CONVERT_UINT32_TO_4xUINT8(0xf419f995),
        CONVERT_UINT32_TO_4xUINT8(0xb6709125),
        CONVERT_UINT32_TO_4xUINT8(0x3a04a259)
};
#define digest_len30 sizeof(digest30)

static const char test_case31[] = "SHA-224 MSG4";
#define data31 ((const uint8_t *)message4)
#define data_len31 message4_len
static const uint8_t digest31[] = {
        CONVERT_UINT32_TO_4xUINT8(0xc97ca9a5),
        CONVERT_UINT32_TO_4xUINT8(0x59850ce9),
        CONVERT_UINT32_TO_4xUINT8(0x7a04a96d),
        CONVERT_UINT32_TO_4xUINT8(0xef6d99a9),
        CONVERT_UINT32_TO_4xUINT8(0xe0e0e2ab),
        CONVERT_UINT32_TO_4xUINT8(0x14e6b8df),
        CONVERT_UINT32_TO_4xUINT8(0x265fc0b3)
};
#define digest_len31 sizeof(digest31)

static const char test_case32[] = "SHA-256 MSG4";
#define data32 ((const uint8_t *)message4)
#define data_len32 message4_len
static const uint8_t digest32[] = {
        CONVERT_UINT32_TO_4xUINT8(0xcf5b16a7),
        CONVERT_UINT32_TO_4xUINT8(0x78af8380),
        CONVERT_UINT32_TO_4xUINT8(0x036ce59e),
        CONVERT_UINT32_TO_4xUINT8(0x7b049237),
        CONVERT_UINT32_TO_4xUINT8(0x0b249b11),
        CONVERT_UINT32_TO_4xUINT8(0xe8f07a51),
        CONVERT_UINT32_TO_4xUINT8(0xafac4503),
        CONVERT_UINT32_TO_4xUINT8(0x7afee9d1)
};
#define digest_len32 sizeof(digest32)

static const char test_case33[] = "SHA-384 MSG4";
#define data33 ((const uint8_t *)message4)
#define data_len33 message4_len
static const uint8_t digest33[] = {
        CONVERT_UINT64_TO_8xUINT8(0x09330c33f71147e8),
        CONVERT_UINT64_TO_8xUINT8(0x3d192fc782cd1b47),
        CONVERT_UINT64_TO_8xUINT8(0x53111b173b3b05d2),
        CONVERT_UINT64_TO_8xUINT8(0x2fa08086e3b0f712),
        CONVERT_UINT64_TO_8xUINT8(0xfcc7c71a557e2db9),
        CONVERT_UINT64_TO_8xUINT8(0x66c3e9fa91746039)
};
#define digest_len33 sizeof(digest33)

static const char test_case34[] = "SHA-512 MSG4";
#define data34 ((const uint8_t *)message4)
#define data_len34 message4_len
static const uint8_t digest34[] = {
        CONVERT_UINT64_TO_8xUINT8(0x8e959b75dae313da),
        CONVERT_UINT64_TO_8xUINT8(0x8cf4f72814fc143f),
        CONVERT_UINT64_TO_8xUINT8(0x8f7779c6eb9f7fa1),
        CONVERT_UINT64_TO_8xUINT8(0x7299aeadb6889018),
        CONVERT_UINT64_TO_8xUINT8(0x501d289e4900f7e4),
        CONVERT_UINT64_TO_8xUINT8(0x331b99dec4b5433a),
        CONVERT_UINT64_TO_8xUINT8(0xc7d329eeb6dd2654),
        CONVERT_UINT64_TO_8xUINT8(0x5e96e55b874be909)
};
#define digest_len34 sizeof(digest34)

#define SHA_TEST_VEC(num, size)                                         \
        { test_case##num, size,                                         \
                        (const uint8_t *) data##num, data_len##num,     \
                        (const uint8_t *) digest##num, digest_len##num }

static const struct sha_vector {
        const char *test_case;
        int sha_type;           /* 1, 224, 256, 384 or 512 */
        const uint8_t *data;
        size_t data_len;
        const uint8_t *digest;
        size_t digest_len;
} sha_vectors[] = {
        SHA_TEST_VEC(1, 1),
        SHA_TEST_VEC(2, 224),
        SHA_TEST_VEC(3, 256),
        SHA_TEST_VEC(4, 384),
        SHA_TEST_VEC(5, 512),
        SHA_TEST_VEC(10, 1),
        SHA_TEST_VEC(11, 224),
        SHA_TEST_VEC(12, 256),
        SHA_TEST_VEC(13, 384),
        SHA_TEST_VEC(14, 512),
        SHA_TEST_VEC(20, 1),
        SHA_TEST_VEC(21, 224),
        SHA_TEST_VEC(22, 256),
        SHA_TEST_VEC(23, 384),
        SHA_TEST_VEC(24, 512),
        SHA_TEST_VEC(30, 1),
        SHA_TEST_VEC(31, 224),
        SHA_TEST_VEC(32, 256),
        SHA_TEST_VEC(33, 384),
        SHA_TEST_VEC(34, 512)
};

static int
sha_job_ok(const struct sha_vector *vec,
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
test_sha(struct IMB_MGR *mb_mgr,
         const struct sha_vector *vec,
         const int num_jobs)
{
        struct IMB_JOB *job;
        uint8_t padding[16];
        uint8_t **auths = malloc(num_jobs * sizeof(void *));
        int i = 0, jobs_rx = 0, ret = -1;

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

        /* empty the manager */
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);

                memset(job, 0, sizeof(*job));
                job->cipher_direction = IMB_DIR_ENCRYPT;
                job->chain_order = IMB_ORDER_HASH_CIPHER;
                job->auth_tag_output = auths[i] + sizeof(padding);
                job->auth_tag_output_len_in_bytes = vec->digest_len;
                job->src = vec->data;
                job->msg_len_to_hash_in_bytes = vec->data_len;
                job->cipher_mode = IMB_CIPHER_NULL;
                switch (vec->sha_type) {
                case 1:
                        job->hash_alg = IMB_AUTH_SHA_1;
                        break;
                case 224:
                        job->hash_alg = IMB_AUTH_SHA_224;
                        break;
                case 256:
                        job->hash_alg = IMB_AUTH_SHA_256;
                        break;
                case 384:
                        job->hash_alg = IMB_AUTH_SHA_384;
                        break;
                case 512:
                default:
                        job->hash_alg = IMB_AUTH_SHA_512;
                        break;
                }

                job->user_data = auths[i];

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job) {
                        jobs_rx++;
                        if (!sha_job_ok(vec, job, job->user_data,
                                        padding, sizeof(padding)))
                                goto end;
                }
        }

        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                jobs_rx++;
                if (!sha_job_ok(vec, job, job->user_data,
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
test_sha_vectors(struct IMB_MGR *mb_mgr,
                 struct test_suite_context *sha1_ctx,
                 struct test_suite_context *sha224_ctx,
                 struct test_suite_context *sha256_ctx,
                 struct test_suite_context *sha384_ctx,
                 struct test_suite_context *sha512_ctx,
                 const int num_jobs)
{
	const int vectors_cnt =
                sizeof(sha_vectors) / sizeof(sha_vectors[0]);
	int vect;
        struct test_suite_context *ctx;

	printf("SHA standard test vectors (N jobs = %d):\n", num_jobs);
	for (vect = 1; vect <= vectors_cnt; vect++) {
                const int idx = vect - 1;
#ifdef DEBUG
		printf("[%d/%d] SHA%d Test Case %s data_len:%d "
                       "digest_len:%d\n",
                       vect, vectors_cnt,
                       sha_vectors[idx].sha_type,
                       sha_vectors[idx].test_case,
                       (int) sha_vectors[idx].data_len,
                       (int) sha_vectors[idx].digest_len);
#endif
                switch (sha_vectors[idx].sha_type) {
                case 1:
                        ctx = sha1_ctx;
                        break;
                case 224:
                        ctx = sha224_ctx;
                        break;
                case 256:
                        ctx = sha256_ctx;
                        break;
                case 384:
                        ctx = sha384_ctx;
                        break;
                case 512:
                default:
                        ctx = sha512_ctx;
                        break;
                }

                if (test_sha(mb_mgr, &sha_vectors[idx], num_jobs)) {
                        printf("error #%d\n", vect);
                        test_suite_update(ctx, 0, 1);
                } else {
                        test_suite_update(ctx, 1, 0);
                }
	}
}

int
sha_test(struct IMB_MGR *mb_mgr)
{
        struct test_suite_context sha1_ctx, sha224_ctx, sha256_ctx;
        struct test_suite_context sha384_ctx, sha512_ctx;
        int errors;

        test_suite_start(&sha1_ctx, "SHA1");
        test_suite_start(&sha224_ctx, "SHA224");
        test_suite_start(&sha256_ctx, "SHA256");
        test_suite_start(&sha384_ctx, "SHA384");
        test_suite_start(&sha512_ctx, "SHA512");
        test_sha_vectors(mb_mgr, &sha1_ctx, &sha224_ctx,
                         &sha256_ctx, &sha384_ctx, &sha512_ctx, 1);
        errors = test_suite_end(&sha1_ctx);
        errors += test_suite_end(&sha224_ctx);
        errors += test_suite_end(&sha256_ctx);
        errors += test_suite_end(&sha384_ctx);
        errors += test_suite_end(&sha512_ctx);

	return errors;
}
