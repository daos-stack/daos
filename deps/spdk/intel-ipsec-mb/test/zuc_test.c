/*****************************************************************************
 Copyright (c) 2009-2021, Intel Corporation

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


/*-----------------------------------------------------------------------
* Zuc functional test
*-----------------------------------------------------------------------
*
* A simple functional test for ZUC
*
*-----------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <intel-ipsec-mb.h>

#include "zuc_test_vectors.h"
#include "gcm_ctr_vectors_test.h"
#include "utils.h"

#define MAXBUFS 17
#define PASS_STATUS 0
#define FAIL_STATUS -1
#define DIM(_x) (sizeof(_x)/sizeof(_x[0]))

enum test_type {
        TEST_4_BUFFER,
        TEST_N_BUFFER
};

int zuc_test(struct IMB_MGR *mb_mgr);

int validate_zuc_algorithm(struct IMB_MGR *mb_mgr, uint8_t *pSrcData,
                           uint8_t *pDstData, uint8_t *pKeys, uint8_t *pIV);
int validate_zuc_EEA_1_block(struct IMB_MGR *mb_mgr, uint8_t *pSrcData,
                             uint8_t *pDstData, uint8_t *pKeys, uint8_t *pIV,
                             const unsigned int job_api);
int validate_zuc_EEA_4_block(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                             uint8_t **pDstData, uint8_t **pKeys,
                             uint8_t **pIV, const unsigned int job_api);
int validate_zuc_EEA_n_block(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                             uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                             uint32_t numBuffs, const unsigned int job_api);
int validate_zuc_EIA_1_block(struct IMB_MGR *mb_mgr, uint8_t *pSrcData,
                             uint8_t *pDstData, uint8_t *pKeys, uint8_t *pIV,
                             const unsigned int job_api);
int validate_zuc_EIA_n_block(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                             uint8_t **pDstData, uint8_t **pKeys,
                             uint8_t **pIV, uint32_t numBuffs,
                             const unsigned int job_api);
int validate_zuc256_EEA3(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                         uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                         uint32_t numBuffs);
int validate_zuc256_EIA3(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                         uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                         uint32_t numBuffs);

static void byte_hexdump(const char *message, const uint8_t *ptr, int len);

/******************************************************************************
 * @ingroup zuc_functionalTest_app
 *
 * @description
 * This function allocates memory for buffers and set random data in each buffer
 *
 * pSrcData = pointers to the new source buffers
 * numOfBuffs = number of buffers
 * ************************************************/
static uint32_t createData(uint8_t *pSrcData[MAXBUFS],
                                 uint32_t numOfBuffs)
{
        uint32_t i = 0, j = 0;

        for (i = 0; i < numOfBuffs; i++) {
                pSrcData[i] = (uint8_t *)malloc(MAX_BUFFER_LENGTH_IN_BYTES);

                if (!pSrcData[i]) {
                        printf("malloc(pSrcData[i]): failed!\n");

                        for (j = 0; j < i; j++) {
                                free(pSrcData[j]);
                                pSrcData[j] = NULL;
                        }

                        return FAIL_STATUS;
                }
        }
        return PASS_STATUS;
}

/******************************************************************************
 * @ingroup zuc_functionalTest_app
 *
 * @description
 * This function creates source data and vector buffers.
 *
 * keyLen = key length
 * pKeys = array of pointers to the new key buffers
 * ivLen = vector length
 * pIV = array of pointers to the new vector buffers
 * numOfBuffs = number of buffers
************************************************/
static uint32_t createKeyVecData(uint32_t keyLen, uint8_t *pKeys[MAXBUFS],
                                 uint32_t ivLen, uint8_t *pIV[MAXBUFS],
                                 uint32_t numOfBuffs)
{
        uint32_t i = 0, j = 0;

        for (i = 0; i < numOfBuffs; i++) {
                pIV[i] = (uint8_t *)malloc(ivLen);

                if (!pIV[i]) {
                        printf("malloc(pIV[i]): failed!\n");

                        for (j = 0; j < i; j++) {
                                free(pIV[j]);
                                free(pKeys[j]);
                        }

                        return FAIL_STATUS;
                }

                pKeys[i] = malloc(keyLen);

                if (!pKeys[i]) {
                        printf("malloc(pKeys[i]): failed!\n");

                        for (j = 0; j <= i; j++) {
                                free(pIV[j]);

                                if (j < i)
                                        free(pKeys[j]);
                        }
                        return FAIL_STATUS;
                }
        }

        return PASS_STATUS;
}

/******************************************************************************
 * @ingroup zuc_benchmark_app
 *
 * @description
 * This function free memory pointed to by an array of pointers
 *
 * arr = array of memory pointers
 * length = length of pointer array (or number of pointers whose buffers
 * should be freed)
 * ************************************************/
static void freePtrArray(uint8_t *pArr[MAXBUFS], uint32_t arrayLength)
{
        uint32_t i = 0;

        for (i = 0; i < arrayLength; i++)
                free(pArr[i]);
}

static uint32_t bswap4(const uint32_t val)
{
        return ((val >> 24) |             /**< A*/
                ((val & 0xff0000) >> 8) | /**< B*/
                ((val & 0xff00) << 8) |   /**< C*/
                (val << 24));             /**< D*/
}

int zuc_test(struct IMB_MGR *mb_mgr)
{

        const uint32_t numBuffs[] = {4, 8, 9, 16, 17};
        uint32_t i;
        int errors = 0;
        uint8_t *pKeys[MAXBUFS] = {0};
        uint8_t *pIV[MAXBUFS] = {0};
        uint8_t *pSrcData[MAXBUFS] = {0};
        uint8_t *pDstData[MAXBUFS] = {0};
        struct test_suite_context eea3_ctx;
        struct test_suite_context eia3_ctx;
        struct test_suite_context eea3_256_ctx;
        struct test_suite_context eia3_256_ctx;

        test_suite_start(&eea3_ctx, "ZUC-EEA3");
        test_suite_start(&eia3_ctx, "ZUC-EIA3");
        test_suite_start(&eea3_256_ctx, "ZUC-EEA3-256");
        test_suite_start(&eia3_256_ctx, "ZUC-EIA3-256");

        /*Create test data buffers + populate with random data*/
        if (createData(pSrcData, MAXBUFS)) {
                printf("createData() error\n");
                test_suite_update(&eea3_ctx, 0, 1);
                test_suite_update(&eia3_ctx, 0, 1);
                goto exit_zuc_test;
        }
        if (createData(pDstData, MAXBUFS)) {
                printf("createData() error\n");
                freePtrArray(pSrcData, MAXBUFS);
                test_suite_update(&eea3_ctx, 0, 1);
                test_suite_update(&eia3_ctx, 0, 1);
                goto exit_zuc_test;
        }

        /* Create random keys and vectors */
        if (createKeyVecData(ZUC256_KEY_LEN_IN_BYTES, pKeys,
                             ZUC256_IV_LEN_IN_BYTES,
                             pIV, MAXBUFS)) {
                printf("createKeyVecData() error\n");
                freePtrArray(pSrcData, MAXBUFS);
                freePtrArray(pDstData, MAXBUFS);
                test_suite_update(&eea3_ctx, 0, 1);
                test_suite_update(&eia3_ctx, 0, 1);
                goto exit_zuc_test;
        }

        if (validate_zuc_algorithm(mb_mgr, pSrcData[0], pSrcData[0], pKeys[0],
                                   pIV[0]))
                test_suite_update(&eea3_ctx, 0, 1);
        else
                test_suite_update(&eea3_ctx, 1, 0);

        /* Direct API tests */
        if (validate_zuc_EEA_1_block(mb_mgr, pSrcData[0], pSrcData[0], pKeys[0],
                                     pIV[0], 0))
                test_suite_update(&eea3_ctx, 0, 1);
        else
                test_suite_update(&eea3_ctx, 1, 0);

        if (validate_zuc_EEA_4_block(mb_mgr, pSrcData, pSrcData, pKeys, pIV, 0))
                test_suite_update(&eea3_ctx, 0, 1);
        else
                test_suite_update(&eea3_ctx, 1, 0);

        for (i = 0; i < DIM(numBuffs); i++) {
                if (validate_zuc_EEA_n_block(mb_mgr, pSrcData, pDstData, pKeys,
                                             pIV, numBuffs[i], 0))
                        test_suite_update(&eea3_ctx, 0, 1);
                else
                        test_suite_update(&eea3_ctx, 1, 0);
        }

        if (validate_zuc_EIA_1_block(mb_mgr, pSrcData[0], pDstData[0], pKeys[0],
                                     pIV[0], 0))
                test_suite_update(&eia3_ctx, 0, 1);
        else
                test_suite_update(&eia3_ctx, 1, 0);

        for (i = 0; i < DIM(numBuffs); i++) {
                if (validate_zuc_EIA_n_block(mb_mgr, pSrcData, pDstData, pKeys,
                                             pIV, numBuffs[i], 0))
                        test_suite_update(&eia3_ctx, 0, 1);
                else
                        test_suite_update(&eia3_ctx, 1, 0);
        }

        /* Job API tests */
        if (validate_zuc_EEA_1_block(mb_mgr, pSrcData[0], pSrcData[0], pKeys[0],
                                     pIV[0], 1))
                test_suite_update(&eea3_ctx, 0, 1);
        else
                test_suite_update(&eea3_ctx, 1, 0);

        if (validate_zuc_EEA_4_block(mb_mgr, pSrcData, pSrcData, pKeys, pIV, 1))
                test_suite_update(&eea3_ctx, 0, 1);
        else
                test_suite_update(&eea3_ctx, 1, 0);

        for (i = 0; i < DIM(numBuffs); i++) {
                if (validate_zuc_EEA_n_block(mb_mgr, pSrcData, pDstData, pKeys,
                                             pIV, numBuffs[i], 1))
                        test_suite_update(&eea3_ctx, 0, 1);
                else
                        test_suite_update(&eea3_ctx, 1, 0);
        }

        if (validate_zuc_EIA_1_block(mb_mgr, pSrcData[0], pDstData[0], pKeys[0],
                                     pIV[0], 1))
                test_suite_update(&eia3_ctx, 0, 1);
        else
                test_suite_update(&eia3_ctx, 1, 0);

        for (i = 0; i < DIM(numBuffs); i++) {
                if (validate_zuc_EIA_n_block(mb_mgr, pSrcData, pDstData, pKeys,
                                             pIV, numBuffs[i], 1))
                        test_suite_update(&eia3_ctx, 0, 1);
                else
                        test_suite_update(&eia3_ctx, 1, 0);
        }

        /* ZUC-EEA3-256 tests */
        for (i = 0; i < DIM(numBuffs); i++) {
                if (validate_zuc256_EEA3(mb_mgr, pSrcData, pDstData, pKeys,
                                         pIV, numBuffs[i]))
                        test_suite_update(&eea3_256_ctx, 0, 1);
                else
                        test_suite_update(&eea3_256_ctx, 1, 0);
        }

        /* ZUC-EIA3-256 tests */
        for (i = 0; i < DIM(numBuffs); i++) {
                if (validate_zuc256_EIA3(mb_mgr, pSrcData, pDstData, pKeys,
                                         pIV, numBuffs[i]))
                        test_suite_update(&eia3_256_ctx, 0, 1);
                else
                        test_suite_update(&eia3_256_ctx, 1, 0);
        }


exit_zuc_test:
        freePtrArray(pKeys, MAXBUFS);    /*Free the key buffers*/
        freePtrArray(pIV, MAXBUFS);      /*Free the vector buffers*/
        freePtrArray(pSrcData, MAXBUFS); /*Free the source buffers*/
        freePtrArray(pDstData, MAXBUFS); /*Free the destination buffers*/

        errors += test_suite_end(&eea3_ctx);
        errors += test_suite_end(&eia3_ctx);
        errors += test_suite_end(&eea3_256_ctx);
        errors += test_suite_end(&eia3_256_ctx);

        return errors;
}

static inline int
submit_eea3_jobs(struct IMB_MGR *mb_mgr, uint8_t **keys, uint8_t **ivs,
                 uint8_t **src, uint8_t **dst, const uint32_t *lens,
                 int dir, const unsigned int num_jobs,
                 const unsigned int key_len,
                 const unsigned int *iv_lens)
{
        IMB_JOB *job;
        unsigned int i;
        unsigned int jobs_rx = 0;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->cipher_direction = dir;
                job->chain_order = IMB_ORDER_CIPHER_HASH;
                job->cipher_mode = IMB_CIPHER_ZUC_EEA3;
                job->src = src[i];
                job->dst = dst[i];
                job->iv = ivs[i];
                job->iv_len_in_bytes = iv_lens[i];
                job->enc_keys = keys[i];
                job->key_len_in_bytes = key_len;

                job->cipher_start_src_offset_in_bytes = 0;
                job->msg_len_to_cipher_in_bytes = lens[i];
                job->hash_alg = IMB_AUTH_NULL;

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job != NULL) {
                        jobs_rx++;
                        if (job->status != IMB_STATUS_COMPLETED) {
                                printf("%d error status:%d, job %d",
                                       __LINE__, job->status, i);
                                return -1;
                        }
                }
        }

        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                jobs_rx++;
                if (job->status != IMB_STATUS_COMPLETED) {
                        printf("%d error status:%d, job %d",
                               __LINE__, job->status, i);
                        return -1;
                }
        }

        if (jobs_rx != num_jobs) {
                printf("Expected %d jobs, received %d\n", num_jobs, jobs_rx);
                return -1;
        }

        return 0;
}

static inline int
submit_eia3_jobs(struct IMB_MGR *mb_mgr, uint8_t **keys, uint8_t **iv,
                 uint8_t **src, uint8_t **tags, const uint32_t *lens,
                 const unsigned int num_jobs,
                 const unsigned int key_sz,
                 const unsigned int tag_sz,
                 const unsigned int *iv_lens)
{
        IMB_JOB *job;
        unsigned int i;
        unsigned int jobs_rx = 0;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->chain_order = IMB_ORDER_CIPHER_HASH;
                job->cipher_mode = IMB_CIPHER_NULL;
                job->src = src[i];
                if (iv_lens[i] == 23) {
                        job->u.ZUC_EIA3._iv = NULL;
                        job->u.ZUC_EIA3._iv23 = iv[i];
                } else {
                        job->u.ZUC_EIA3._iv = iv[i];
                        job->u.ZUC_EIA3._iv23 = NULL;
                }
                job->u.ZUC_EIA3._key = keys[i];

                job->hash_start_src_offset_in_bytes = 0;
                job->msg_len_to_hash_in_bits = lens[i];
                if (key_sz == 16)
                        job->hash_alg = IMB_AUTH_ZUC_EIA3_BITLEN;
                else
                        job->hash_alg = IMB_AUTH_ZUC256_EIA3_BITLEN;
                job->auth_tag_output = tags[i];
                job->auth_tag_output_len_in_bytes = tag_sz;

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job != NULL) {
                        jobs_rx++;
                        if (job->status != IMB_STATUS_COMPLETED) {
                                printf("%d error status:%d, job %d",
                                       __LINE__, job->status, i);
                                return -1;
                        }
                }
        }

        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                jobs_rx++;
                if (job->status != IMB_STATUS_COMPLETED) {
                        printf("%d error status:%d, job %d",
                               __LINE__, job->status, i);
                        return -1;
                }
        }

        if (jobs_rx != num_jobs) {
                printf("Expected %d jobs, received %d\n", num_jobs, jobs_rx);
                return -1;
        }

        return 0;
}

static int
test_output(const uint8_t *out, const uint8_t *ref, const uint32_t bytelen,
            const uint32_t bitlen, const char *err_msg)
{
        int ret = 0;
        uint32_t byteResidue;
        uint32_t bitResidue;

        if (bitlen % 8)
                /* Last byte is not a full byte */
                ret = memcmp(out, ref, bytelen - 1);
        else
                /* Last byte is a full byte */
                ret = memcmp(out, ref, bytelen);

        if (ret) {
                printf("%s : FAIL\n", err_msg);
                byte_hexdump("Expected", ref, bytelen);
                byte_hexdump("Found", out, bytelen);
                ret = -1;
        /*
         * Check last partial byte if there is one and
         * all previous full bytes are correct
         */
        } else if (bitlen % 8) {
                bitResidue = (0xFF00 >> (bitlen % 8)) & 0x00FF;
                byteResidue = (ref[bitlen / 8] ^ out[bitlen / 8]) & bitResidue;
                if (byteResidue) {
                        printf("%s : FAIL\n", err_msg);
                        printf("Expected: 0x%02X (last byte)\n",
                               0xFF & ref[bitlen / 8]);
                        printf("Found: 0x%02X (last byte)\n",
                               0xFF & out[bitlen / 8]);
                        ret = -1;
                }
#ifdef DEBUG
                else
                        printf("%s : PASS\n", err_msg);
#endif
        }
#ifdef DEBUG
        else
                printf("%s : PASS\n", err_msg);
#endif
        fflush(stdout);

        return ret;
}

int
validate_zuc_EEA_1_block(struct IMB_MGR *mb_mgr, uint8_t *pSrcData,
                         uint8_t *pDstData, uint8_t *pKeys, uint8_t *pIV,
                         const unsigned int job_api)
{
        uint32_t i;
        int ret = 0;

        /* ZUC-128-EEA3 */
        for (i = 0; i < NUM_ZUC_EEA3_TESTS; i++) {
                char msg[50];
                int retTmp;
                uint32_t byteLength;
                const unsigned int iv_len = ZUC_IV_LEN_IN_BYTES;

                memcpy(pKeys, testEEA3_vectors[i].CK, ZUC_KEY_LEN_IN_BYTES);
                zuc_eea3_iv_gen(testEEA3_vectors[i].count,
                                testEEA3_vectors[i].Bearer,
                                testEEA3_vectors[i].Direction,
                                pIV);
                byteLength = (testEEA3_vectors[i].length_in_bits + 7) / 8;
                memcpy(pSrcData, testEEA3_vectors[i].plaintext, byteLength);
                if (job_api)
                        submit_eea3_jobs(mb_mgr, &pKeys, &pIV, &pSrcData,
                                         &pDstData, &byteLength,
                                         IMB_DIR_ENCRYPT, 1,
                                         ZUC_KEY_LEN_IN_BYTES,
                                         &iv_len);
                else
                        IMB_ZUC_EEA3_1_BUFFER(mb_mgr, pKeys, pIV, pSrcData,
                                              pDstData, byteLength);

                snprintf(msg, sizeof(msg),
                         "Validate ZUC 1 block test %u (Enc):", i + 1);
                retTmp = test_output(pDstData, testEEA3_vectors[i].ciphertext,
                                     byteLength,
                                     testEEA3_vectors[i].length_in_bits, msg);
                if (retTmp < 0)
                        ret = retTmp;
        }

        return ret;
};

static int
submit_and_verify(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                  uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                  const unsigned int job_api, IMB_CIPHER_DIRECTION dir,
                  enum test_type type, const unsigned int var_bufs,
                  const unsigned int num_buffers, const uint32_t *buf_idx)
{
        unsigned int i;
        uint32_t packetLen[MAXBUFS];
        int ret = 0;
        const struct test128EEA3_vectors_t *vector;
        unsigned int iv_lens[MAXBUFS];

        for (i = 0; i < num_buffers; i++) {
                vector = &testEEA3_vectors[buf_idx[i]];
                packetLen[i] = (vector->length_in_bits + 7) / 8;
                memcpy(pKeys[i], vector->CK, ZUC_KEY_LEN_IN_BYTES);
                zuc_eea3_iv_gen(vector->count, vector->Bearer,
                        vector->Direction, pIV[i]);
                iv_lens[i] = ZUC_IV_LEN_IN_BYTES;
                if (dir == IMB_DIR_ENCRYPT)
                        memcpy(pSrcData[i], vector->plaintext, packetLen[i]);
                else
                        memcpy(pSrcData[i], vector->ciphertext, packetLen[i]);
        }

        if (job_api)
                submit_eea3_jobs(mb_mgr, pKeys, pIV, pSrcData,
                                 pDstData, packetLen, dir, num_buffers,
                                 ZUC_KEY_LEN_IN_BYTES, iv_lens);
        else {
                if (type == TEST_4_BUFFER)
                        IMB_ZUC_EEA3_4_BUFFER(mb_mgr,
                                              (const void * const *)pKeys,
                                              (const void * const *)pIV,
                                              (const void * const *)pSrcData,
                                              (void **)pDstData, packetLen);
                else /* TEST_N_BUFFER */
                        IMB_ZUC_EEA3_N_BUFFER(mb_mgr,
                                              (const void * const *)pKeys,
                                              (const void * const *)pIV,
                                              (const void * const *)pSrcData,
                                              (void **)pDstData, packetLen,
                                              num_buffers);
        }

        for (i = 0; i < num_buffers; i++) {
                uint8_t *pDst8 = (uint8_t *)pDstData[i];
                int retTmp;
                char msg_start[50];
                char msg[100];

                vector = &testEEA3_vectors[buf_idx[i]];
                if (var_bufs)
                        snprintf(msg_start, sizeof(msg_start),
                                 "Validate ZUC %c block multi-vector",
                                 type == TEST_4_BUFFER ? '4' : 'N');
                else
                        snprintf(msg_start, sizeof(msg_start),
                                 "Validate ZUC %c block",
                                 type == TEST_4_BUFFER ? '4' : 'N');

                if (dir == IMB_DIR_ENCRYPT) {
                        snprintf(msg, sizeof(msg),
                                 "%s test %u, index %u (Enc):",
                                 msg_start, buf_idx[i] + 1, i);
                        retTmp = test_output(pDst8, vector->ciphertext,
                                             packetLen[i],
                                             vector->length_in_bits, msg);
                } else { /* DECRYPT */
                        snprintf(msg, sizeof(msg),
                                 "%s test %u, index %u (Dec):",
                                 msg_start, buf_idx[i] + 1, i);
                        retTmp = test_output(pDst8, vector->plaintext,
                                             packetLen[i],
                                             vector->length_in_bits, msg);
                }
                if (retTmp < 0)
                        ret = retTmp;
        }

        return ret;
}

static int
submit_and_verify_zuc256(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                         uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                         IMB_CIPHER_DIRECTION dir,
                         const unsigned int var_bufs,
                         const unsigned int num_buffers,
                         const uint32_t *buf_idx)
{
        unsigned int i;
        uint32_t packetLen[MAXBUFS];
        int ret = 0;
        const struct test256EEA3_vectors_t *vector;
        unsigned int iv_lens[MAXBUFS];

        for (i = 0; i < num_buffers; i++) {
                vector = &test256EEA3_vectors[buf_idx[i]];
                packetLen[i] = (vector->length_in_bits + 7) / 8;
                memcpy(pKeys[i], vector->CK, ZUC256_KEY_LEN_IN_BYTES);
                memcpy(pIV[i], vector->IV, vector->iv_length);
                if (dir == IMB_DIR_ENCRYPT)
                        memcpy(pSrcData[i], vector->plaintext, packetLen[i]);
                else
                        memcpy(pSrcData[i], vector->ciphertext, packetLen[i]);
                iv_lens[i] = vector->iv_length;
        }

        submit_eea3_jobs(mb_mgr, pKeys, pIV, pSrcData,
                         pDstData, packetLen, dir, num_buffers,
                         ZUC256_KEY_LEN_IN_BYTES, iv_lens);

        for (i = 0; i < num_buffers; i++) {
                uint8_t *pDst8 = (uint8_t *)pDstData[i];
                int retTmp;
                char msg_start[50];
                char msg[100];

                vector = &test256EEA3_vectors[buf_idx[i]];
                if (var_bufs)
                        snprintf(msg_start, sizeof(msg_start),
                                 "Validate ZUC-256 multi-vector");
                else
                        snprintf(msg_start, sizeof(msg_start),
                                 "Validate ZUC-256");

                if (dir == IMB_DIR_ENCRYPT) {
                        snprintf(msg, sizeof(msg),
                                 "%s test %u, index %u (Enc):",
                                 msg_start, buf_idx[i] + 1, i);
                        retTmp = test_output(pDst8, vector->ciphertext,
                                             packetLen[i],
                                             vector->length_in_bits, msg);
                } else { /* DECRYPT */
                        snprintf(msg, sizeof(msg),
                                 "%s test %u, index %u (Dec):",
                                 msg_start, buf_idx[i] + 1, i);
                        retTmp = test_output(pDst8, vector->plaintext,
                                             packetLen[i],
                                             vector->length_in_bits, msg);
                }
                if (retTmp < 0)
                        ret = retTmp;
        }

        return ret;
}

int validate_zuc_EEA_4_block(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                             uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                             const unsigned int job_api)
{
        uint32_t i, j;
        int ret = 0;
        int retTmp;
        uint32_t buf_idx[4];

        for (i = 0; i < NUM_ZUC_EEA3_TESTS; i++) {
                for (j = 0; j < 4; j++)
                        buf_idx[j] = i;

                retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                           pIV, job_api, IMB_DIR_ENCRYPT,
                                           TEST_4_BUFFER, 0, 4, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;
                retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                           pIV, job_api, IMB_DIR_DECRYPT,
                                           TEST_4_BUFFER, 0, 4, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;
        }

        /* Encrypt 4 different buffers, grouping all available test vectors
         * in groups of 4 */
        for (i = 0; i < (NUM_ZUC_EEA3_TESTS - 4); i++) {
                for (j = 0; j < 4; j++)
                        buf_idx[j] = i+j;

                retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                           pIV, job_api, IMB_DIR_ENCRYPT,
                                           TEST_4_BUFFER, 1, 4, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;
                retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                           pIV, job_api, IMB_DIR_DECRYPT,
                                           TEST_4_BUFFER, 1, 4, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;
        }

        return ret;
};

int validate_zuc_EEA_n_block(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                             uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                             uint32_t numBuffs, const unsigned int job_api)
{
        uint32_t i, j;
        int ret = 0;
        int retTmp;
        uint32_t buf_idx[MAXBUFS];

        assert(numBuffs > 0);
        for (i = 0; i < NUM_ZUC_EEA3_TESTS; i++) {
                for (j = 0; j < numBuffs; j++)
                        buf_idx[j] = i;

                retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                           pIV, job_api,
                                           IMB_DIR_ENCRYPT, TEST_N_BUFFER,
                                           0, numBuffs, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;

                retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                           pIV, job_api, IMB_DIR_DECRYPT,
                                           TEST_N_BUFFER, 0, numBuffs, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;
        }

        /* Get all test vectors and encrypt them together */
        for (i = 0; i < numBuffs; i++)
                buf_idx[i] = i % NUM_ZUC_EEA3_TESTS;

        retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                   pIV, job_api, IMB_DIR_ENCRYPT, TEST_N_BUFFER,
                                   1, numBuffs, buf_idx);
        if (retTmp < 0)
                ret = retTmp;

        retTmp = submit_and_verify(mb_mgr, pSrcData, pDstData, pKeys,
                                   pIV, job_api, IMB_DIR_DECRYPT, TEST_N_BUFFER,
                                   1, numBuffs, buf_idx);
        if (retTmp < 0)
                ret = retTmp;

        return ret;
};

int validate_zuc256_EEA3(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                         uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                         uint32_t numBuffs)
{
        uint32_t i, j;
        int ret = 0;
        int retTmp;
        uint32_t buf_idx[MAXBUFS];

        assert(numBuffs > 0);
        for (i = 0; i < NUM_ZUC_256_EEA3_TESTS; i++) {
                for (j = 0; j < numBuffs; j++)
                        buf_idx[j] = i;

                retTmp = submit_and_verify_zuc256(mb_mgr, pSrcData, pDstData,
                                                  pKeys, pIV, IMB_DIR_ENCRYPT,
                                                  0, numBuffs, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;

                retTmp = submit_and_verify_zuc256(mb_mgr, pSrcData, pDstData,
                                                  pKeys, pIV, IMB_DIR_DECRYPT,
                                                  0, numBuffs, buf_idx);
                if (retTmp < 0)
                        ret = retTmp;
        }

        /* Get all test vectors and encrypt them together */
        for (i = 0; i < numBuffs; i++)
                buf_idx[i] = i % NUM_ZUC_256_EEA3_TESTS;

        retTmp = submit_and_verify_zuc256(mb_mgr, pSrcData, pDstData, pKeys,
                                          pIV, IMB_DIR_ENCRYPT,
                                          1, numBuffs, buf_idx);
        if (retTmp < 0)
                ret = retTmp;

        retTmp = submit_and_verify_zuc256(mb_mgr, pSrcData, pDstData, pKeys,
                                          pIV, IMB_DIR_DECRYPT,
                                          1, numBuffs, buf_idx);
        if (retTmp < 0)
                ret = retTmp;

        return ret;
}

int validate_zuc_EIA_1_block(struct IMB_MGR *mb_mgr, uint8_t *pSrcData,
                             uint8_t *pDstData, uint8_t *pKeys, uint8_t *pIV,
                             const unsigned int job_api)
{
        uint32_t i;
        int retTmp, ret = 0;
        uint32_t byteLength;
        uint32_t bitLength;

        for (i = 0; i < NUM_ZUC_EIA3_TESTS; i++) {
                const unsigned int iv_len = ZUC_IV_LEN_IN_BYTES;

                memcpy(pKeys, testEIA3_vectors[i].CK, ZUC_KEY_LEN_IN_BYTES);

                zuc_eia3_iv_gen(testEIA3_vectors[i].count,
                                testEIA3_vectors[i].Bearer,
                                testEIA3_vectors[i].Direction,
                                pIV);
                bitLength = testEIA3_vectors[i].length_in_bits;
                byteLength = (bitLength + 7) / 8;
                memcpy(pSrcData, testEIA3_vectors[i].message, byteLength);
                if (job_api)
                        submit_eia3_jobs(mb_mgr, &pKeys, &pIV,
                                         &pSrcData, &pDstData,
                                         &bitLength, 1, ZUC_KEY_LEN_IN_BYTES,
                                         ZUC_DIGEST_LEN, &iv_len);
                else
                        IMB_ZUC_EIA3_1_BUFFER(mb_mgr, pKeys, pIV, pSrcData,
                                              bitLength, (uint32_t *)pDstData);
                retTmp =
                    memcmp(pDstData, &testEIA3_vectors[i].mac,
                           sizeof(((struct test128EIA3_vectors_t *)0)->mac));
                if (retTmp) {
                        printf("Validate ZUC 1 block  test %d (Int): FAIL\n",
                               i + 1);
                        byte_hexdump("Expected",
                                     (const uint8_t *)&testEIA3_vectors[i].mac,
                                     ZUC_DIGEST_LEN);
                        byte_hexdump("Found", pDstData, ZUC_DIGEST_LEN);
                        ret = retTmp;
                }
#ifdef DEBUG
                else
                        printf("Validate ZUC 1 block  test %d (Int): PASS\n",
                               i + 1);
#endif
                fflush(stdout);
        }
        return ret;
};

int validate_zuc_EIA_n_block(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                             uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                             uint32_t numBuffs, const unsigned int job_api)
{
        uint32_t i, j;
        int retTmp, ret = 0;
        uint32_t byteLength;
        uint32_t bitLength[MAXBUFS];
        struct test128EIA3_vectors_t vector;
        unsigned int iv_lens[MAXBUFS];

        for (i = 0; i < NUM_ZUC_EIA3_TESTS; i++) {
                vector = testEIA3_vectors[i];
                for (j = 0; j < numBuffs; j++) {
                        memcpy(pKeys[j], vector.CK, ZUC_KEY_LEN_IN_BYTES);

                        zuc_eia3_iv_gen(vector.count, vector.Bearer,
                                        vector.Direction, pIV[j]);
                        bitLength[j] = vector.length_in_bits;
                        byteLength = (bitLength[j] + 7) / 8;
                        memcpy(pSrcData[j], vector.message, byteLength);
                        iv_lens[j] = ZUC_IV_LEN_IN_BYTES;
                }
                if (job_api)
                        submit_eia3_jobs(mb_mgr, pKeys, pIV,
                                         pSrcData, pDstData,
                                         bitLength, numBuffs,
                                         ZUC_KEY_LEN_IN_BYTES, ZUC_DIGEST_LEN,
                                         iv_lens);
                else
                        IMB_ZUC_EIA3_N_BUFFER(mb_mgr,
                                              (const void * const *)pKeys,
                                              (const void * const *)pIV,
                                              (const void * const *)pSrcData,
                                              bitLength, (uint32_t **)pDstData,
                                              numBuffs);

                for (j = 0; j < numBuffs; j++) {
                        retTmp =
                            memcmp(pDstData[j], &vector.mac, ZUC_DIGEST_LEN);
                        if (retTmp) {
                                printf("Validate ZUC n block test %u, index %u "
                                       "(Int): FAIL\n", i + 1, j);
                                byte_hexdump("Expected",
                                             (const uint8_t *)&vector.mac,
                                             ZUC_DIGEST_LEN);
                                byte_hexdump("Found", pDstData[j],
                                             ZUC_DIGEST_LEN);
                                ret = retTmp;
                        }
#ifdef DEBUG
                        else
                                printf("Validate ZUC n block test %u, index %u "
                                       "(Int): PASS\n", i + 1, j);
#endif
                        fflush(stdout);
                }
        }

        /* Generate digests for n different test vectors,
         * grouping all available tests vectors in groups of N buffers */
        for (i = 0; i < numBuffs; i++) {
                vector = testEIA3_vectors[i % NUM_ZUC_EIA3_TESTS];
                memcpy(pKeys[i], vector.CK,
                       ZUC_KEY_LEN_IN_BYTES);

                zuc_eia3_iv_gen(vector.count, vector.Bearer,
                                vector.Direction, pIV[i]);
                bitLength[i] = vector.length_in_bits;
                byteLength = (bitLength[i] + 7) / 8;
                memcpy(pSrcData[i], vector.message, byteLength);
                iv_lens[j] = ZUC_IV_LEN_IN_BYTES;
        }

        if (job_api)
                submit_eia3_jobs(mb_mgr, pKeys, pIV,
                                 pSrcData, pDstData,
                                 bitLength, numBuffs,
                                 ZUC_KEY_LEN_IN_BYTES, ZUC_DIGEST_LEN,
                                 iv_lens);
        else
                IMB_ZUC_EIA3_N_BUFFER(mb_mgr,
                                      (const void * const *)pKeys,
                                      (const void * const *)pIV,
                                      (const void * const *)pSrcData,
                                      bitLength, (uint32_t **)pDstData,
                                      numBuffs);

        for (i = 0; i < numBuffs; i++) {
                vector = testEIA3_vectors[i % NUM_ZUC_EIA3_TESTS];
                retTmp =
                    memcmp(pDstData[i], &vector.mac,
                           sizeof(((struct test128EIA3_vectors_t *)0)->mac));
                if (retTmp) {
                        printf("Validate ZUC n block multi-vector test "
                               "# jobs = %u, index %u (Int): FAIL\n",
                               numBuffs, i);
                        byte_hexdump("Expected",
                                     (const uint8_t *)&vector.mac,
                                     ZUC_DIGEST_LEN);
                        byte_hexdump("Found", pDstData[i], ZUC_DIGEST_LEN);
                        ret = retTmp;
                }
#ifdef DEBUG
                else
                        printf("Validate ZUC n block multi-vector test, "
                               "# jobs = %u, index %u (Int): PASS\n",
                               numBuffs, i);
#endif
                fflush(stdout);
        }
        return ret;
};

int validate_zuc256_EIA3(struct IMB_MGR *mb_mgr, uint8_t **pSrcData,
                         uint8_t **pDstData, uint8_t **pKeys, uint8_t **pIV,
                         uint32_t numBuffs)
{
        uint32_t i, j;
        int retTmp, ret = 0;
        uint32_t byteLength;
        uint32_t bitLength[MAXBUFS];
        const struct test256EIA3_vectors_t *vector;
        unsigned int iv_lens[MAXBUFS];

        /* TODO: check 8-byte and 16-byte digests */
        for (i = 0; i < NUM_ZUC_256_EIA3_TESTS; i++) {
                vector = &test256EIA3_vectors[i];
                for (j = 0; j < numBuffs; j++) {
                        memcpy(pKeys[j], vector->CK, ZUC256_KEY_LEN_IN_BYTES);
                        memcpy(pIV[j], vector->IV, vector->iv_length);
                        bitLength[j] = vector->length_in_bits;
                        byteLength = (bitLength[j] + 7) / 8;
                        memcpy(pSrcData[j], vector->message, byteLength);
                        iv_lens[j] = vector->iv_length;
                }
                submit_eia3_jobs(mb_mgr, pKeys, pIV,
                                 pSrcData, pDstData,
                                 bitLength, numBuffs,
                                 ZUC256_KEY_LEN_IN_BYTES, ZUC_DIGEST_LEN,
                                 iv_lens);

                for (j = 0; j < numBuffs; j++) {
                        retTmp =
                            memcmp(pDstData[j], &vector->mac4, ZUC_DIGEST_LEN);
                        if (retTmp) {
                                printf("Validate ZUC-256 n block test %u, "
                                       "index %u (Int): FAIL\n", i + 1, j);
                                byte_hexdump("Expected",
                                             (const uint8_t *)&vector->mac4,
                                             ZUC_DIGEST_LEN);
                                byte_hexdump("Found", pDstData[j],
                                             ZUC_DIGEST_LEN);
                                ret = retTmp;
                        }
#ifdef DEBUG
                        else
                                printf("Validate ZUC-256 n block test %u, "
                                       "index %u (Int): PASS\n", i + 1, j);
#endif
                        fflush(stdout);
                }
        }

        /* Generate digests for n different test vectors,
         * grouping all available tests vectors in groups of N buffers */
        for (i = 0; i < numBuffs; i++) {
                vector = &test256EIA3_vectors[i % NUM_ZUC_256_EIA3_TESTS];
                memcpy(pKeys[i], vector->CK, ZUC256_KEY_LEN_IN_BYTES);
                memcpy(pIV[i], vector->IV, vector->iv_length);

                bitLength[i] = vector->length_in_bits;
                byteLength = (bitLength[i] + 7) / 8;
                memcpy(pSrcData[i], vector->message, byteLength);
                iv_lens[i] = vector->iv_length;
        }

        submit_eia3_jobs(mb_mgr, pKeys, pIV,
                         pSrcData, pDstData,
                         bitLength, numBuffs,
                         ZUC256_KEY_LEN_IN_BYTES, ZUC_DIGEST_LEN,
                         iv_lens);

        for (i = 0; i < numBuffs; i++) {
                vector = &test256EIA3_vectors[i % NUM_ZUC_256_EIA3_TESTS];
                retTmp =
                    memcmp(pDstData[i], &vector->mac4,
                           sizeof(((struct test256EIA3_vectors_t *)0)->mac4));
                if (retTmp) {
                        printf("Validate ZUC-256 n block multi-vector test "
                               "# jobs = %u, index %u (Int): FAIL\n",
                               numBuffs, i);
                        byte_hexdump("Expected",
                                     (const uint8_t *)&vector->mac4,
                                     ZUC_DIGEST_LEN);
                        byte_hexdump("Found", pDstData[i], ZUC_DIGEST_LEN);
                        ret = retTmp;
                }
#ifdef DEBUG
                else
                        printf("Validate ZUC-256 n block multi-vector test, "
                               "# jobs = %u, index %u (Int): PASS\n",
                               numBuffs, i);
#endif
                fflush(stdout);
        }
        return ret;
};

int validate_zuc_algorithm(struct IMB_MGR *mb_mgr, uint8_t *pSrcData,
                           uint8_t *pDstData, uint8_t *pKeys, uint8_t *pIV)
{
        uint32_t i;
        int ret = 0;
        union SwapBytes {
                uint8_t sbb[8];
                uint32_t sbw[2];
        } swapBytes;

        for (i = 0; i < NUM_ZUC_ALG_TESTS; i++) {
                memcpy(pKeys, testZUC_vectors[i].CK, ZUC_KEY_LEN_IN_BYTES);
                memcpy(pIV, testZUC_vectors[i].IV, ZUC_IV_LEN_IN_BYTES);
                memset(pSrcData, 0, 8);
                IMB_ZUC_EEA3_1_BUFFER(mb_mgr, pKeys, pIV, pSrcData, pDstData,
                                      8);
                swapBytes.sbw[0] = bswap4(testZUC_vectors[i].Z[0]);
                swapBytes.sbw[1] = bswap4(testZUC_vectors[i].Z[1]);
                ret = memcmp(pDstData, swapBytes.sbb, 8);
                if (ret)
                        printf("ZUC 1 algorithm test %d: FAIL\n", i);
#ifdef DEBUG
                else
                        printf("ZUC 1 algorithm test %d: PASS\n", i);
#endif
        }
        return ret;
};
/*****************************************************************************
 ** @description - utility function to dump test buffers$
 ** $
 ** @param message [IN] - debug message to print$
 ** @param ptr [IN] - pointer to beginning of buffer.$
 ** @param len [IN] - length of buffer.$
 *****************************************************************************/
static void byte_hexdump(const char *message, const uint8_t *ptr, int len)
{
        int ctr;

        printf("%s:\n", message);
        for (ctr = 0; ctr < len; ctr++) {
                printf("0x%02X ", ptr[ctr] & 0xff);
                if (!((ctr + 1) % 16))
                        printf("\n");
        }
        printf("\n");
        printf("\n");
};
