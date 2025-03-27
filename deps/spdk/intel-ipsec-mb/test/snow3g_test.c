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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intel-ipsec-mb.h"

#include "gcm_ctr_vectors_test.h"
#include "utils.h"

#include "snow3g_test_vectors.h"

#define SNOW3GIVLEN 8
#define PAD_LEN 16

int snow3g_test(struct IMB_MGR *mb_mgr);
static void
validate_snow3g_f8_1_block(struct IMB_MGR *mb_mgr, unsigned int job_api,
                           struct test_suite_context *uea2_ctx,
                           struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f8_1_bitblock(struct IMB_MGR *mb_mgr, unsigned int job_api,
                              struct test_suite_context *uea2_ctx,
                              struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f8_2_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f8_4_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f8_8_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f8_8_blocks_multi_key(struct IMB_MGR *mb_mgr,
                                      uint32_t job_api,
                                      struct test_suite_context *uea2_ctx,
                                      struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f8_n_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f8_n_blocks_multi(struct IMB_MGR *mb_mgr,
                                  uint32_t job_api,
                                  struct test_suite_context *uea2_ctx,
                                  struct test_suite_context *uia2_ctx);
static void
validate_snow3g_f9(struct IMB_MGR *mb_mgr, uint32_t job_api,
                   struct test_suite_context *uea2_ctx,
                   struct test_suite_context *uia2_ctx);
/* snow3g validation function pointer table */
struct {
        void (*func)(struct IMB_MGR *, uint32_t job_api,
                     struct test_suite_context *uea2_ctx,
                     struct test_suite_context *uia2_ctx);
        const char *func_name;
} snow3g_func_tab[] = {
        {validate_snow3g_f8_1_bitblock,
         "validate_snow3g_f8_1_bitblock"},
        {validate_snow3g_f8_1_block,
         "validate_snow3g_f8_1_block"},
        {validate_snow3g_f8_2_blocks,
         "validate_snow3g_f8_2_blocks"},
        {validate_snow3g_f8_4_blocks,
         "validate_snow3g_f8_4_blocks"},
        {validate_snow3g_f8_8_blocks,
         "validate_snow3g_f8_8_blocks"},
        {validate_snow3g_f8_8_blocks_multi_key,
         "validate_snow3g_f8_8_blocks_multi_key"},
        {validate_snow3g_f8_n_blocks,
         "validate_snow3g_f8_n_blocks"},
        {validate_snow3g_f8_n_blocks_multi,
         "validate_snow3g_f8_n_blocks_multi"},
        {validate_snow3g_f9,
         "validate_snow3g_f9"}
};

/******************************************************************************
 * @description - utility function to dump test buffers
 *
 * @param message [IN] - debug message to print
 * @param ptr [IN] - pointer to beginning of buffer.
 * @param len [IN] - length of buffer.
 ******************************************************************************/
static inline void snow3g_hexdump(const char *message, uint8_t *ptr, int len)
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
}

static inline int
submit_uea2_jobs(struct IMB_MGR *mb_mgr, uint8_t **keys, uint8_t **ivs,
                 uint8_t **src, uint8_t **dst, const uint32_t *bitlens,
                 const uint32_t *bit_offsets, int dir,
                 const unsigned int num_jobs)
{
        IMB_JOB *job;
        unsigned int i;
        unsigned int jobs_rx = 0;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->cipher_direction = dir;
                job->chain_order = IMB_ORDER_CIPHER_HASH;
                job->cipher_mode = IMB_CIPHER_SNOW3G_UEA2_BITLEN;
                job->src = src[i];
                job->dst = dst[i];
                job->iv = ivs[i];
                job->iv_len_in_bytes = 16;
                job->enc_keys = keys[i];
                job->key_len_in_bytes = 16;

                job->cipher_start_src_offset_in_bits = bit_offsets[i];
                job->msg_len_to_cipher_in_bits = bitlens[i];
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
submit_uia2_job(struct IMB_MGR *mb_mgr, uint8_t *key, uint8_t *iv,
                uint8_t *src, uint8_t *tag, const uint32_t bitlen,
                uint8_t *exp_out, const int num_jobs)
{
        int i, err, jobs_rx = 0;
        IMB_JOB *job;

        /* flush the scheduler */
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->chain_order = IMB_ORDER_CIPHER_HASH;
                job->cipher_mode = IMB_CIPHER_NULL;
                job->src = src;
                job->u.SNOW3G_UIA2._iv = iv;
                job->u.SNOW3G_UIA2._key = key;

                job->hash_start_src_offset_in_bytes = 0;
                job->msg_len_to_hash_in_bits = bitlen;
                job->hash_alg = IMB_AUTH_SNOW3G_UIA2_BITLEN;
                job->auth_tag_output = tag;
                job->auth_tag_output_len_in_bytes = 4;

                job = IMB_SUBMIT_JOB(mb_mgr);
                if (job != NULL) {
                        /* got job back */
                        jobs_rx++;
                        if (job->status != IMB_STATUS_COMPLETED) {
                                printf("%d error status:%d",
                                       __LINE__, job->status);
                                goto end;
                        }
                        /*Compare the digest with the expected in the vectors*/
                        if (memcmp(job->auth_tag_output,
                                   exp_out, DIGEST_LEN) != 0) {
                                printf("IMB_AUTH_SNOW3G_UIA2_BITLEN "
                                       "job num:%d\n", i);
                                snow3g_hexdump("Actual:", job->auth_tag_output,
                                               DIGEST_LEN);
                                snow3g_hexdump("Expected:", exp_out,
                                               DIGEST_LEN);
                                goto end;
                        }
                } else {
                        /* no job returned - check for error */
                        err = imb_get_errno(mb_mgr);
                        if (err != 0) {
                                printf("Error: %s!\n", imb_get_strerror(err));
                                goto end;
                        }
                }
        }

        /* flush any outstanding jobs */
        while ((job = IMB_FLUSH_JOB(mb_mgr)) != NULL) {
                jobs_rx++;

                err = imb_get_errno(mb_mgr);
                if (err != 0) {
                        printf("Error: %s!\n", imb_get_strerror(err));
                        goto end;
                }

                if (memcmp(job->auth_tag_output, exp_out, DIGEST_LEN) != 0) {
                        printf("IMB_AUTH_SNOW3G_UIA2_BITLEN job num:%d\n", i);
                        snow3g_hexdump("Actual:", job->auth_tag_output,
                                       DIGEST_LEN);
                        snow3g_hexdump("Expected:", exp_out, DIGEST_LEN);
                        goto end;
                }
        }

        if (jobs_rx != num_jobs) {
                printf("Expected %d jobs, received %d\n", num_jobs, jobs_rx);
                goto end;
        }

        return 0;

 end:
        while (IMB_FLUSH_JOB(mb_mgr) != NULL)
                ;

        return -1;
}

static void
validate_snow3g_f8_1_block(struct IMB_MGR *mb_mgr, uint32_t job_api,
                           struct test_suite_context *uea2_ctx,
                           struct test_suite_context *uia2_ctx)
{
        int numVectors, i, length;
        size_t size = 0;
        cipher_test_vector_t *testVectors = snow3g_cipher_test_vectors[1];
        /* snow3g f8 test vectors are located at index 1 */
        numVectors = numSnow3gCipherTestVectors[1];

        snow3g_key_schedule_t *pKeySched = NULL;
        uint8_t *pKey = NULL;
        int keyLen = MAX_KEY_LEN;
        uint8_t srcBuff[MAX_DATA_LEN];
        uint8_t dstBuff[MAX_DATA_LEN];
        uint8_t *pSrcBuff = srcBuff;
        uint8_t *pIV = NULL;
        int status = -1;

        (void) uia2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_1_BUFFER (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(srcBuff, 0, sizeof(srcBuff));
        memset(dstBuff, 0, sizeof(dstBuff));

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f8_1_buffer_exit;
        }

        pIV = malloc(SNOW3G_IV_LEN_IN_BYTES);
        if (!pIV) {
                printf("malloc(pIV):failed !\n");
                goto snow3g_f8_1_buffer_exit;
        }

        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pKey):failed !\n");
                goto snow3g_f8_1_buffer_exit;
        }
        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f8_1_buffer_exit;

        pKeySched = malloc(size);
        if (!pKeySched) {
                printf("malloc(IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr)): failed ! "
                       "\n");
                goto snow3g_f8_1_buffer_exit;
        }

        /*Copy the data for for Snow3g 1 Packet version*/
        for (i = 0; i < numVectors; i++) {

                length = testVectors[i].dataLenInBytes;

                memcpy(pKey, testVectors[i].key, testVectors[i].keyLenInBytes);
                memcpy(srcBuff, testVectors[i].plaintext, length);

                memcpy(dstBuff, testVectors[i].ciphertext, length);
                memcpy(pIV, testVectors[i].iv, testVectors[i].ivLenInBytes);

                /*setup the keysched to be used*/
                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey, pKeySched) == -1) {
                        printf("CPU check failed\n");
                        goto snow3g_f8_1_buffer_exit;
                }

                /*Validate encrypt*/
                if (job_api) {
                        uint32_t bit_len = length << 3;
                        uint32_t bit_offset = 0;

                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &pSrcBuff, &pSrcBuff,
                                         &bit_len, &bit_offset,
                                         IMB_DIR_ENCRYPT, 1);
                } else
                        IMB_SNOW3G_F8_1_BUFFER(mb_mgr, pKeySched, pIV, srcBuff,
                                               srcBuff, length);

                /*check against the ciphertext in the vector against the
                 * encrypted plaintext*/
                if (memcmp(srcBuff, dstBuff, length) != 0) {
                        printf("IMB_SNOW3G_F8_1_BUFFER(Enc) vector:%d\n", i);
                        snow3g_hexdump("Actual:", srcBuff, length);
                        snow3g_hexdump("Expected:", dstBuff, length);
                        goto snow3g_f8_1_buffer_exit;
                }

                memcpy(dstBuff, testVectors[i].plaintext, length);

                /*Validate Decrypt*/
                if (job_api) {
                        unsigned bit_len = length << 3;
                        uint32_t bit_offset = 0;

                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &pSrcBuff, &pSrcBuff,
                                         &bit_len, &bit_offset,
                                         IMB_DIR_ENCRYPT, 1);
                } else
                        IMB_SNOW3G_F8_1_BUFFER(mb_mgr, pKeySched, pIV, srcBuff,
                                               srcBuff, length);

                if (memcmp(srcBuff, dstBuff, length) != 0) {
                        printf("IMB_SNOW3G_F8_1_BUFFER(Dec) vector:%d\n", i);
                        snow3g_hexdump("Actual:", srcBuff, length);
                        snow3g_hexdump("Expected:", dstBuff, length);
                        goto snow3g_f8_1_buffer_exit;
                }
        } /* for numVectors */

        /* no errors detected */
        status = 0;

snow3g_f8_1_buffer_exit:
        free(pIV);
        free(pKey);
        free(pKeySched);

        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

/* Shift right a buffer by "offset" bits, "offset" < 8 */
static void buffer_shift_right(uint8_t *buffer,
                               const uint32_t length,
                               const uint8_t offset)
{
        uint8_t prev_byte;
        const uint32_t length_in_bytes = (length * 8 + offset + 7) / 8;
        const uint8_t lower_byte_mask = (1 << offset) - 1;
        uint32_t i;

        prev_byte = buffer[0];
        buffer[0] >>= offset;

        for (i = 1; i < length_in_bytes; i++) {
                const uint8_t curr_byte = buffer[i];

                buffer[i] = ((prev_byte & lower_byte_mask) << (8 - offset)) |
                            (curr_byte >> offset);
                prev_byte = curr_byte;
        }
}

static void copy_test_bufs(uint8_t *plainBuff, uint8_t *wrkBuff,
                           uint8_t *ciphBuff, const uint8_t *src_test,
                           const uint8_t *dst_test, const uint32_t byte_len)
{
        /*
         * Reset all buffers
         * - plain and cipher buffers to 0
         * - working buffer to -1 (for padding check)
         * and copy test vectors
         */
        memset(wrkBuff, -1, (byte_len + PAD_LEN * 2));
        memset(plainBuff, 0, (byte_len + PAD_LEN * 2));
        memset(ciphBuff, 0, (byte_len + PAD_LEN * 2));
        memcpy(plainBuff + PAD_LEN, src_test, byte_len);
        memcpy(ciphBuff + PAD_LEN, dst_test, byte_len);
}

static void
validate_snow3g_f8_1_bitblock(struct IMB_MGR *mb_mgr,
                              uint32_t job_api,
                              struct test_suite_context *uea2_ctx,
                              struct test_suite_context *uia2_ctx)
{
        int i, length;
        size_t size = 0;
        cipherbit_test_linear_vector_t *testVectors =
                &snow3g_f8_linear_bitvectors /*snow3g_cipher_test_vectors[1]*/;
        cipher_test_vector_t *testStandardVectors =
                snow3g_f8_vectors;  /* scipher_test_vectors[1]; */
        /* snow3g f8 test vectors are located at index 1 */
        const int numVectors =
                MAX_BIT_BUFFERS;  /* numSnow3gCipherTestVectors[3]; */

        snow3g_key_schedule_t *pKeySched = NULL;
        uint8_t *pKey = NULL;
        int keyLen = MAX_KEY_LEN;
        uint8_t srcBuff[MAX_DATA_LEN];
        uint8_t midBuff[MAX_DATA_LEN];
        uint8_t dstBuff[MAX_DATA_LEN];
        /* Adding extra byte for offset tests (shifting up to 7 bits) */
        uint8_t padding[PAD_LEN + 1];
        uint8_t *pIV = NULL;
        int status = -1;

        (void) uia2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_1_BUFFER_BIT: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(padding, -1, sizeof(padding));

        pIV = malloc(SNOW3G_IV_LEN_IN_BYTES);
        if (!pIV) {
                printf("malloc(pIV):failed !\n");
                goto snow3g_f8_1_buffer_bit_exit;
        }

        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pKey):failed !\n");
                goto snow3g_f8_1_buffer_bit_exit;
        }
        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f8_1_buffer_bit_exit;

        pKeySched = malloc(size);
        if (!pKeySched) {
                printf("malloc(IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr)): failed ! "
                       "\n");
                goto snow3g_f8_1_buffer_bit_exit;
        }

        /*Copy the data for for Snow3g 1 Packet version*/
        for (i = 0; i < numVectors; i++) {
                uint8_t *midBufBefPad = midBuff;
                uint8_t *midBufAftPad = midBuff + PAD_LEN;
                uint8_t *srcBufBefPad = srcBuff;
                uint8_t *srcBufAftPad = srcBuff + PAD_LEN;
                uint8_t *dstBufBefPad = dstBuff;
                uint8_t *dstBufAftPad = dstBuff + PAD_LEN;

                const uint32_t byte_len =
                        (testVectors->dataLenInBits[i] + 7) / 8;
                uint32_t bit_len = testVectors->dataLenInBits[i];
                uint32_t head_offset = i % 8;
                const uint32_t tail_offset = (head_offset + bit_len) % 8;
                const uint32_t final_byte_offset = (bit_len + head_offset) / 8;
                const uint32_t byte_len_with_offset =
                        (bit_len + head_offset + 7) / 8;

                memcpy(pKey, testVectors->key[i], testVectors->keyLenInBytes);
                memcpy(pIV, testVectors->iv[i], testVectors->ivLenInBytes);
                copy_test_bufs(srcBufBefPad, midBufBefPad, dstBufBefPad,
                               testVectors->plaintext[i],
                               testVectors->ciphertext[i],
                               byte_len);

                /* shift buffers by offset for this round */
                buffer_shift_right(srcBufBefPad, (byte_len + PAD_LEN * 2) * 8,
                                   head_offset);
                buffer_shift_right(dstBufBefPad, (byte_len + PAD_LEN * 2) * 8,
                                   head_offset);

                /*setup the keysched to be used*/
                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey, pKeySched) == -1) {
                        printf("CPU check failed\n");
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                /*Validate Encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &srcBufAftPad, &midBufAftPad,
                                         &bit_len, &head_offset,
                                         IMB_DIR_ENCRYPT, 1);
                else
                        IMB_SNOW3G_F8_1_BUFFER_BIT(mb_mgr, pKeySched, pIV,
                                                   srcBufAftPad, midBufAftPad,
                                                   bit_len, head_offset);

                /*check against the ciphertext in the vector against the
                 * encrypted plaintext*/
                if (membitcmp(midBufAftPad, dstBufAftPad,
                              bit_len, head_offset) != 0) {
                        printf("Test1: snow3g_f8_1_bitbuffer(Enc) buffer:%d "
                               "size:%d offset:%d\n", i, bit_len, head_offset);
                        snow3g_hexdump("Actual:", midBufAftPad,
                                       byte_len_with_offset);
                        snow3g_hexdump("Expected:", dstBufAftPad,
                                       byte_len_with_offset);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                /* Check that data not to be ciphered was not overwritten */
                if (membitcmp(midBufBefPad, padding,
                              (PAD_LEN * 8) + head_offset, 0)) {
                        printf("overwrite head\n");
                        snow3g_hexdump("Head", midBufBefPad, PAD_LEN + 1);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                if (membitcmp(midBufAftPad + final_byte_offset, padding,
                              (PAD_LEN * 8) - tail_offset, tail_offset)) {
                        printf("overwrite tail\n");
                        snow3g_hexdump("Tail", midBufAftPad + final_byte_offset,
                                       PAD_LEN + 1);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                /* reset working buffer */
                memset(midBufBefPad, -1, (byte_len + PAD_LEN * 2));

                /*Validate Decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &dstBufAftPad, &midBufAftPad,
                                         &bit_len, &head_offset,
                                         IMB_DIR_DECRYPT, 1);
                else
                        IMB_SNOW3G_F8_1_BUFFER_BIT(mb_mgr, pKeySched, pIV,
                                                   dstBufAftPad, midBufAftPad,
                                                   bit_len, head_offset);

                if (membitcmp(midBufAftPad, srcBufAftPad, bit_len,
                              head_offset) != 0) {
                        printf("Test2: snow3g_f8_1_bitbuffer(Dec) buffer:%d "
                               "size:%d offset:%d\n", i, bit_len, head_offset);
                        snow3g_hexdump("Actual:", midBufAftPad,
                                       byte_len_with_offset);
                        snow3g_hexdump("Expected:", srcBufAftPad,
                                       byte_len_with_offset);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                /* Check that data not to be ciphered was not overwritten */
                if (membitcmp(midBufBefPad, padding,
                              (PAD_LEN * 8) + head_offset, 0)) {
                        printf("overwrite head\n");
                        snow3g_hexdump("Head", midBufBefPad, PAD_LEN + 1);
                        goto snow3g_f8_1_buffer_bit_exit;
                }
                if (membitcmp(midBufAftPad + final_byte_offset, padding,
                              (PAD_LEN * 8) - tail_offset, tail_offset)) {
                        printf("overwrite tail\n");
                        snow3g_hexdump("Tail", midBufAftPad + final_byte_offset,
                                       PAD_LEN + 1);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                /* Another test with Standard 3GPP table */
                head_offset = 0;
                length = testStandardVectors[i].dataLenInBytes;
                bit_len = length * 8;
                memcpy(srcBuff, testStandardVectors[i].plaintext, length);
                memcpy(dstBuff, testStandardVectors[i].ciphertext, length);

                /*Validate Encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &srcBufBefPad, &midBufBefPad,
                                         &bit_len, &head_offset,
                                         IMB_DIR_ENCRYPT, 1);
                else
                        IMB_SNOW3G_F8_1_BUFFER_BIT(mb_mgr, pKeySched, pIV,
                                                   srcBuff, midBuff,
                                                   bit_len, head_offset);

                /*check against the ciphertext in the vector against the
                 * encrypted plaintext*/
                if (membitcmp(midBuff, dstBuff, bit_len, 0) != 0) {
                        printf("Test3: snow3g_f8_1_bitbuffer(Enc) buffer:%d "
                               "size:%d offset:0\n", i, bit_len);
                        snow3g_hexdump("Actual:", &midBuff[0], length);
                        snow3g_hexdump("Expected:", &dstBuff[0], length);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                /*Validate Decrypt*/
                if (job_api) {
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &midBufBefPad, &dstBufBefPad,
                                         &bit_len, &head_offset,
                                         IMB_DIR_DECRYPT, 1);
                } else
                        IMB_SNOW3G_F8_1_BUFFER_BIT(mb_mgr, pKeySched, pIV,
                                                   midBuff, dstBuff,
                                                   bit_len, head_offset);

                if (membitcmp(dstBuff, srcBuff, bit_len, 0) != 0) {
                        printf("Test4: snow3g_f8_1_bitbuffer(Dec) buffer:%d "
                               "size:%d offset:0\n", i, bit_len);
                        snow3g_hexdump("Actual:", &dstBuff[0], length);
                        snow3g_hexdump("Expected:", &srcBuff[0], length);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                memcpy(srcBuff, testStandardVectors[i].plaintext, length);

                memcpy(dstBuff, testStandardVectors[i].ciphertext, length);

                buffer_shift_right(srcBuff, length, 4);
                buffer_shift_right(dstBuff, length, 4);

                /*Validate Encrypt with offset */
                head_offset = 4;

                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &srcBufBefPad, &midBufBefPad,
                                         &bit_len, &head_offset,
                                         IMB_DIR_ENCRYPT, 1);
                else
                        IMB_SNOW3G_F8_1_BUFFER_BIT(mb_mgr, pKeySched, pIV,
                                                   srcBuff, midBuff,
                                                   bit_len, head_offset);

                /*check against the ciphertext in the vector against the
                 * encrypted plaintext*/
                if (membitcmp(midBuff, dstBuff, bit_len, 4) != 0) {
                        printf("Test5:snow3g_f8_1_bitbuffer(Enc) buffer:%d "
                               "size:%d offset:4\n", i, bit_len);
                        snow3g_hexdump("Actual:", &midBuff[0],
                                       (length * 8 + 4 + 7) / 8);
                        snow3g_hexdump("Expected:", &dstBuff[0],
                                       (length * 8 + 4 + 7) / 8);
                        goto snow3g_f8_1_buffer_bit_exit;
                }

                /*Validate Decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         &pIV, &dstBufBefPad, &midBufBefPad,
                                         &bit_len, &head_offset,
                                         IMB_DIR_DECRYPT, 1);
                else
                        IMB_SNOW3G_F8_1_BUFFER_BIT(mb_mgr, pKeySched, pIV,
                                                   dstBuff, midBuff,
                                                   bit_len, head_offset);

                if (membitcmp(midBuff, srcBuff, bit_len, 4) != 0) {
                        printf("Test6: snow3g_f8_1_bitbuffer(Dec) buffer:%d "
                               "size:%d offset:4\n", i, bit_len);
                        snow3g_hexdump("Actual:", &midBuff[0],
                                       (length * 8 + 4 + 7) / 8);
                        snow3g_hexdump("Expected:", &srcBuff[0],
                                       (length * 8 + 4 + 7) / 8);
                        goto snow3g_f8_1_buffer_bit_exit;
                }
        }  /* for numVectors */

        /* no errors detected */
        status = 0;

snow3g_f8_1_buffer_bit_exit:
        free(pIV);
        free(pKey);
        free(pKeySched);

        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

static void
validate_snow3g_f8_2_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx)
{
        int length, numVectors, i = 0, j = 0, numPackets = 2;
        size_t size = 0;
        cipher_test_vector_t *testVectors = snow3g_cipher_test_vectors[1];
        /* snow3g f8 test vectors are located at index 1 */
        numVectors = numSnow3gCipherTestVectors[1];

        snow3g_key_schedule_t *pKeySched[NUM_SUPPORTED_BUFFERS];
        uint8_t *pKey[NUM_SUPPORTED_BUFFERS];
        uint8_t *pSrcBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pDstBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pIV[NUM_SUPPORTED_BUFFERS];
        uint32_t packetLen[NUM_SUPPORTED_BUFFERS];
        int keyLen = MAX_KEY_LEN;
        int status = -1;

        (void) uia2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_2_BUFFER: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(pSrcBuff, 0, sizeof(pSrcBuff));
        memset(pDstBuff, 0, sizeof(pDstBuff));
        memset(pIV, 0, sizeof(pIV));
        memset(pKeySched, 0, sizeof(pKeySched));
        memset(pKey, 0, sizeof(pKey));

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f8_2_buffer_exit;
        }

        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f8_2_buffer_exit;

        /* Test with all vectors */
        for (j = 0; j < numVectors; j++) {
                uint32_t bitOffsets[NUM_SUPPORTED_BUFFERS];
                uint32_t bitLens[NUM_SUPPORTED_BUFFERS];

                length = testVectors[j].dataLenInBytes;

                /* Create test Data for num Packets */
                for (i = 0; i < numPackets; i++) {
                        packetLen[i] = length;
                        bitLens[i] = length * 8;
                        bitOffsets[i] = 0;

                        pKey[i] = malloc(keyLen);
                        if (!pKey[i]) {
                                printf("malloc(pKey[%d]):failed !\n", i);
                                goto snow3g_f8_2_buffer_exit;
                        }
                        pKeySched[i] = malloc(size);
                        if (!pKeySched[i]) {
                                printf("malloc(pKeySched[%d]): failed !\n", i);
                                goto snow3g_f8_2_buffer_exit;
                        }
                        pSrcBuff[i] = malloc(length);
                        if (!pSrcBuff[i]) {
                                printf("malloc(pSrcBuff[%d]):failed !\n", i);
                                goto snow3g_f8_2_buffer_exit;
                        }
                        pDstBuff[i] = malloc(length);
                        if (!pDstBuff[i]) {
                                printf("malloc(pDstBuff[%d]):failed !\n", i);
                                goto snow3g_f8_2_buffer_exit;
                        }
                        pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                        if (!pIV[i]) {
                                printf("malloc(pIV[%d]):failed !\n", i);
                                goto snow3g_f8_2_buffer_exit;
                        }

                        memcpy(pKey[i], testVectors[j].key,
                               testVectors[j].keyLenInBytes);

                        memcpy(pSrcBuff[i], testVectors[j].plaintext, length);

                        memset(pDstBuff[i], 0, length);

                        memcpy(pIV[i], testVectors[j].iv,
                               testVectors[j].ivLenInBytes);

                        /* init key shed */
                        if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i],
                                                      pKeySched[i])) {
                                printf("IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr) "
                                       "error\n");
                                goto snow3g_f8_2_buffer_exit;
                        }
                }

                /* TEST IN-PLACE ENCRYPTION/DECRYPTION */
                /*Test the encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pSrcBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_ENCRYPT, 2);
                else
                        IMB_SNOW3G_F8_2_BUFFER(mb_mgr, pKeySched[0], pIV[0],
                                               pIV[1], pSrcBuff[0], pSrcBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pSrcBuff[1], packetLen[1]);

                /*compare the ciphertext with the encryped plaintext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pSrcBuff[i], testVectors[j].ciphertext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_2_BUFFER(Enc) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pSrcBuff[i],
                                               packetLen[0]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].ciphertext,
                                               packetLen[0]);
                                goto snow3g_f8_2_buffer_exit;
                        }
                }

                /* Set the source buffer with ciphertext, and clear destination
                 * buffer */
                for (i = 0; i < numPackets; i++)
                        memcpy(pSrcBuff[i], testVectors[j].ciphertext, length);

                /*Test the decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pSrcBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_DECRYPT, 2);
                else
                        IMB_SNOW3G_F8_2_BUFFER(mb_mgr, pKeySched[0], pIV[0],
                                               pIV[1], pSrcBuff[0], pSrcBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pSrcBuff[1], packetLen[1]);

                /*Compare the plaintext with the decrypted ciphertext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pSrcBuff[i], testVectors[j].plaintext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_2_BUFFER(Dec) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pSrcBuff[i],
                                               packetLen[0]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].plaintext,
                                               packetLen[i]);
                                goto snow3g_f8_2_buffer_exit;
                        }
                }

                /* TEST OUT-OF-PLACE ENCRYPTION/DECRYPTION */
                /*Test the encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_ENCRYPT, 2);
                else
                        IMB_SNOW3G_F8_2_BUFFER(mb_mgr, pKeySched[0], pIV[0],
                                               pIV[1], pSrcBuff[0], pDstBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pDstBuff[1], packetLen[1]);

                /*compare the ciphertext with the encryped plaintext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pDstBuff[i], testVectors[j].ciphertext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_2_BUFFER(Enc) vector:%d "
                                       "buffer:%d\n",
                                       j, i);
                                snow3g_hexdump("Actual:", pDstBuff[i],
                                               packetLen[0]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].ciphertext,
                                               packetLen[0]);
                                goto snow3g_f8_2_buffer_exit;
                        }
                }
                /* Set the source buffer with ciphertext, and clear destination
                 * buffer */
                for (i = 0; i < numPackets; i++) {
                        memcpy(pSrcBuff[i], testVectors[j].ciphertext, length);
                        memset(pDstBuff[i], 0, length);
                }

                /*Test the decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_DECRYPT, 2);
                else
                        IMB_SNOW3G_F8_2_BUFFER(mb_mgr, pKeySched[0], pIV[0],
                                               pIV[1], pSrcBuff[0], pDstBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pDstBuff[1], packetLen[1]);

                /*Compare the plaintext with the decrypted ciphertext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pDstBuff[i], testVectors[j].plaintext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_2_BUFFER(Dec) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pDstBuff[i],
                                               packetLen[0]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].plaintext,
                                               packetLen[i]);
                                goto snow3g_f8_2_buffer_exit;
                        }
                }
                /* free buffers before next iteration */
                for (i = 0; i < numPackets; i++) {
                        if (pKey[i] != NULL) {
                                free(pKey[i]);
                                pKey[i] = NULL;
                        }
                        if (pKeySched[i] != NULL) {
                                free(pKeySched[i]);
                                pKeySched[i] = NULL;
                        }
                        if (pSrcBuff[i] != NULL) {
                                free(pSrcBuff[i]);
                                pSrcBuff[i] = NULL;
                        }
                        if (pDstBuff[i] != NULL) {
                                free(pDstBuff[i]);
                                pDstBuff[i] = NULL;
                        }
                        if (pIV[i] != NULL) {
                                free(pIV[i]);
                                pIV[i] = NULL;
                        }
                }
        }

        /* no errors detected */
        status = 0;

snow3g_f8_2_buffer_exit:
        for (i = 0; i < numPackets; i++) {
                if (pKey[i] != NULL)
                        free(pKey[i]);
                if (pKeySched[i] != NULL)
                        free(pKeySched[i]);
                if (pSrcBuff[i] != NULL)
                        free(pSrcBuff[i]);
                if (pDstBuff[i] != NULL)
                        free(pDstBuff[i]);
                if (pIV[i] != NULL)
                        free(pIV[i]);
        }
        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

static void
validate_snow3g_f8_4_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx)
{
        int length, numVectors, i = 0, j = 0, numPackets = 4;
        size_t size = 0;
        cipher_test_vector_t *testVectors = snow3g_cipher_test_vectors[1];
        /* snow3g f8 test vectors are located at index 1 */
        numVectors = numSnow3gCipherTestVectors[1];

        snow3g_key_schedule_t *pKeySched[NUM_SUPPORTED_BUFFERS];
        uint8_t *pKey[NUM_SUPPORTED_BUFFERS];
        uint8_t *pSrcBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pDstBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pIV[NUM_SUPPORTED_BUFFERS];
        uint32_t packetLen[NUM_SUPPORTED_BUFFERS];
        uint32_t bitOffsets[NUM_SUPPORTED_BUFFERS];
        uint32_t bitLens[NUM_SUPPORTED_BUFFERS];
        int keyLen = MAX_KEY_LEN;
        int status = -1;

        (void) uia2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_4_BUFFER: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(pSrcBuff, 0, sizeof(pSrcBuff));
        memset(pDstBuff, 0, sizeof(pDstBuff));
        memset(pIV, 0, sizeof(pIV));
        memset(pKeySched, 0, sizeof(pKeySched));
        memset(pKey, 0, sizeof(pKey));

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f8_4_buffer_exit;
        }

        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f8_4_buffer_exit;

        /* Test with all vectors */
        for (j = 0; j < numVectors; j++) {
                length = testVectors[j].dataLenInBytes;

                /* Create test Data for num Packets */
                for (i = 0; i < numPackets; i++) {
                        packetLen[i] = length;
                        bitLens[i] = length * 8;
                        bitOffsets[i] = 0;

                        pKey[i] = malloc(keyLen);
                        if (!pKey[i]) {
                                printf("malloc(pKey[%d]):failed !\n", i);
                                goto snow3g_f8_4_buffer_exit;
                        }
                        pKeySched[i] = malloc(size);
                        if (!pKeySched[i]) {
                                printf("malloc(pKeySched[%d]): failed !\n", i);
                                goto snow3g_f8_4_buffer_exit;
                        }
                        pSrcBuff[i] = malloc(length);
                        if (!pSrcBuff[i]) {
                                printf("malloc(pSrcBuff[%d]):failed !\n", i);
                                goto snow3g_f8_4_buffer_exit;
                        }
                        pDstBuff[i] = malloc(length);
                        if (!pDstBuff[i]) {
                                printf("malloc(pDstBuff[%d]):failed !\n", i);
                                goto snow3g_f8_4_buffer_exit;
                        }
                        pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                        if (!pIV[i]) {
                                printf("malloc(pIV[%d]):failed !\n", i);
                                goto snow3g_f8_4_buffer_exit;
                        }

                        memcpy(pKey[i], testVectors[j].key,
                               testVectors[j].keyLenInBytes);

                        memcpy(pSrcBuff[i], testVectors[j].plaintext, length);

                        memset(pDstBuff[i], 0, length);

                        memcpy(pIV[i], testVectors[j].iv,
                               testVectors[j].ivLenInBytes);

                        /* init key shed */
                        if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i],
                                                      pKeySched[i])) {
                                printf("IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr) "
                                       "error\n");
                                goto snow3g_f8_4_buffer_exit;
                        }
                }

                /* TEST IN-PLACE ENCRYPTION/DECRYPTION */
                /*Test the encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pSrcBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_ENCRYPT, 4);
                else
                        IMB_SNOW3G_F8_4_BUFFER(mb_mgr, pKeySched[0],
                                               pIV[0], pIV[1], pIV[2], pIV[3],
                                               pSrcBuff[0], pSrcBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pSrcBuff[1], packetLen[1],
                                               pSrcBuff[2], pSrcBuff[2],
                                               packetLen[2], pSrcBuff[3],
                                               pSrcBuff[3], packetLen[3]);

                /* compare the ciphertext with the encryped plaintext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pSrcBuff[i], testVectors[j].ciphertext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_4_BUFFER(Enc) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pSrcBuff[i],
                                               packetLen[i]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].ciphertext,
                                               packetLen[i]);
                                goto snow3g_f8_4_buffer_exit;
                        }
                }

                /* Set the source buffer with ciphertext, and clear destination
                 * buffer */
                for (i = 0; i < numPackets; i++)
                        memcpy(pSrcBuff[i], testVectors[j].ciphertext, length);

                /*Test the decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pSrcBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_DECRYPT, 4);
                else
                        IMB_SNOW3G_F8_4_BUFFER(mb_mgr, pKeySched[0],
                                               pIV[0], pIV[1], pIV[2], pIV[3],
                                               pSrcBuff[0], pSrcBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pSrcBuff[1], packetLen[1],
                                               pSrcBuff[2], pSrcBuff[2],
                                               packetLen[2], pSrcBuff[3],
                                               pSrcBuff[3], packetLen[3]);

                /*Compare the plaintext with the decrypted ciphertext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pSrcBuff[i], testVectors[j].plaintext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_4_BUFFER(Dec) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pSrcBuff[i],
                                               packetLen[i]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].plaintext,
                                               packetLen[i]);
                                goto snow3g_f8_4_buffer_exit;
                        }
                }
                /* TEST OUT-OF-PLACE ENCRYPTION/DECRYPTION */
                /*Test the encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_ENCRYPT, 4);
                else
                        IMB_SNOW3G_F8_4_BUFFER(mb_mgr, pKeySched[0],
                                               pIV[0], pIV[1], pIV[2], pIV[3],
                                               pSrcBuff[0], pDstBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pDstBuff[1], packetLen[1],
                                               pSrcBuff[2], pDstBuff[2],
                                               packetLen[2], pSrcBuff[3],
                                               pDstBuff[3], packetLen[3]);

                /*compare the ciphertext with the encryped plaintext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pDstBuff[i], testVectors[j].ciphertext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_4_BUFFER(Enc) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pDstBuff[i],
                                               packetLen[i]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].ciphertext,
                                               packetLen[i]);
                                goto snow3g_f8_4_buffer_exit;
                        }
                }

                /* Set the source buffer with ciphertext, and clear destination
                 * buffer */
                for (i = 0; i < numPackets; i++) {
                        memcpy(pSrcBuff[i], testVectors[j].ciphertext, length);
                        memset(pDstBuff[i], 0, length);
                }
                /*Test the decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_DECRYPT, 4);
                else
                        IMB_SNOW3G_F8_4_BUFFER(mb_mgr, pKeySched[0],
                                               pIV[0], pIV[1], pIV[2], pIV[3],
                                               pSrcBuff[0], pDstBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pDstBuff[1], packetLen[1],
                                               pSrcBuff[2], pDstBuff[2],
                                               packetLen[2], pSrcBuff[3],
                                               pDstBuff[3], packetLen[3]);

                /*Compare the plaintext with the decrypted ciphertext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pDstBuff[i], testVectors[j].plaintext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_4_BUFFER(Dec) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pDstBuff[i],
                                               packetLen[i]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].plaintext,
                                               packetLen[i]);
                                goto snow3g_f8_4_buffer_exit;
                        }
                }
                /* free buffers before next iteration */
                for (i = 0; i < numPackets; i++) {
                        if (pKey[i] != NULL) {
                                free(pKey[i]);
                                pKey[i] = NULL;
                        }
                        if (pKeySched[i] != NULL) {
                                free(pKeySched[i]);
                                pKeySched[i] = NULL;
                        }
                        if (pSrcBuff[i] != NULL) {
                                free(pSrcBuff[i]);
                                pSrcBuff[i] = NULL;
                        }
                        if (pDstBuff[i] != NULL) {
                                free(pDstBuff[i]);
                                pDstBuff[i] = NULL;
                        }
                        if (pIV[i] != NULL) {
                                free(pIV[i]);
                                pIV[i] = NULL;
                        }
                }
        }

        /*vectors are in bits used to round up to bytes*/
        length = testVectors[1].dataLenInBytes;

        /*Create test Data for num Packets*/
        for (i = 0; i < numPackets; i++) {
                /* Test for packets of different length. */
                packetLen[i] = length - (i * 12);
                bitLens[i] = packetLen[i] * 8;
                bitOffsets[i] = 0;

                pKey[i] = malloc(keyLen);
                if (!pKey[i]) {
                        printf("malloc(pKey[%d]):failed !\n", i);
                        goto snow3g_f8_4_buffer_exit;
                }
                pKeySched[i] = malloc(size);
                if (!pKeySched[i]) {
                        printf("malloc(pKeySched[%d]): failed !\n", i);
                        goto snow3g_f8_4_buffer_exit;
                }
                pSrcBuff[i] = malloc(packetLen[i]);
                if (!pSrcBuff[i]) {
                        printf("malloc(pSrcBuff[%d]):failed !\n", i);
                        goto snow3g_f8_4_buffer_exit;
                }
                pDstBuff[i] = malloc(packetLen[i]);
                if (!pDstBuff[i]) {
                        printf("malloc(pDstBuff[%d]):failed !\n", i);
                        goto snow3g_f8_4_buffer_exit;
                }
                pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                if (!pIV[i]) {
                        printf("malloc(pIV[%d]):failed !\n", i);
                        goto snow3g_f8_4_buffer_exit;
                }
                memcpy(pKey[i], testVectors[1].key,
                       testVectors[1].keyLenInBytes);

                memcpy(pSrcBuff[i], testVectors[1].plaintext, packetLen[i]);

                memset(pDstBuff[i], 0, packetLen[i]);

                memcpy(pIV[i], testVectors[1].iv, testVectors[1].ivLenInBytes);

                /* init key shed */
                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i], pKeySched[i])) {
                        printf("IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr) error\n");
                        goto snow3g_f8_4_buffer_exit;
                }
        }

        /* Test the encrypt */
        if (job_api)
                submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                 pIV, pSrcBuff, pDstBuff,
                                 bitLens, bitOffsets, IMB_DIR_ENCRYPT, 4);
        else
                IMB_SNOW3G_F8_4_BUFFER(mb_mgr, pKeySched[0], pIV[0], pIV[1],
                                       pIV[2], pIV[3], pSrcBuff[0], pDstBuff[0],
                                       packetLen[0], pSrcBuff[1], pDstBuff[1],
                                       packetLen[1], pSrcBuff[2], pDstBuff[2],
                                       packetLen[2], pSrcBuff[3], pDstBuff[3],
                                       packetLen[3]);

        /*compare the ciphertext with the encryped plaintext*/
        for (i = 0; i < numPackets; i++) {
                if (memcmp(pDstBuff[i], testVectors[1].ciphertext,
                           packetLen[i]) != 0) {
                        printf("IMB_SNOW3G_F8_4_BUFFER(Enc, diff size) "
                               "vector:%d buffer:%d\n", 1, i);
                        snow3g_hexdump("Actual:", pDstBuff[i], packetLen[i]);
                        snow3g_hexdump("Expected:", testVectors[1].ciphertext,
                                       packetLen[i]);
                        goto snow3g_f8_4_buffer_exit;
                }
        }

        /* no errors detected */
        status = 0;

snow3g_f8_4_buffer_exit:
        for (i = 0; i < numPackets; i++) {
                if (pKey[i] != NULL)
                        free(pKey[i]);
                if (pKeySched[i] != NULL)
                        free(pKeySched[i]);
                if (pSrcBuff[i] != NULL)
                        free(pSrcBuff[i]);
                if (pDstBuff[i] != NULL)
                        free(pDstBuff[i]);
                if (pIV[i] != NULL)
                        free(pIV[i]);
        }
        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

static void
validate_snow3g_f8_8_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx)
{
        int length, numVectors, i, j, numPackets = 8;
        size_t size = 0;
        cipher_test_vector_t *testVectors = snow3g_cipher_test_vectors[1];
        /* snow3g f8 test vectors are located at index 1 */
        numVectors = numSnow3gCipherTestVectors[1];

        snow3g_key_schedule_t *pKeySched[NUM_SUPPORTED_BUFFERS];
        uint8_t *pKey[NUM_SUPPORTED_BUFFERS];
        uint8_t *pSrcBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pDstBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pIV[NUM_SUPPORTED_BUFFERS];
        uint32_t packetLen[NUM_SUPPORTED_BUFFERS];
        uint32_t bitOffsets[NUM_SUPPORTED_BUFFERS];
        uint32_t bitLens[NUM_SUPPORTED_BUFFERS];
        int keyLen = MAX_KEY_LEN;
        int status = -1;

        (void) uia2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_8_BUFFER: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(pSrcBuff, 0, sizeof(pSrcBuff));
        memset(pDstBuff, 0, sizeof(pDstBuff));
        memset(pIV, 0, sizeof(pIV));
        memset(pKeySched, 0, sizeof(pKeySched));
        memset(pKey, 0, sizeof(pKey));

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f8_8_buffer_exit;
        }

        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f8_8_buffer_exit;

        /* Test with all vectors */
        for (j = 0; j < numVectors; j++) {
                length = testVectors[j].dataLenInBytes;

                /* Create test Data for num Packets */
                for (i = 0; i < numPackets; i++) {
                        packetLen[i] = length;
                        bitLens[i] = length * 8;
                        bitOffsets[i] = 0;

                        pKey[i] = malloc(keyLen);
                        if (!pKey[i]) {
                                printf("malloc(pKey[%d]):failed !\n", i);
                                goto snow3g_f8_8_buffer_exit;
                        }
                        pKeySched[i] = malloc(size);
                        if (!pKeySched[i]) {
                                printf("malloc(pKeySched[%d]): failed !\n", i);
                                goto snow3g_f8_8_buffer_exit;
                        }
                        pSrcBuff[i] = malloc(length);
                        if (!pSrcBuff[i]) {
                                printf("malloc(pSrcBuff[%d]):failed !\n", i);
                                goto snow3g_f8_8_buffer_exit;
                        }
                        pDstBuff[i] = malloc(length);
                        if (!pDstBuff[i]) {
                                printf("malloc(pDstBuff[%d]):failed !\n", i);
                                goto snow3g_f8_8_buffer_exit;
                        }
                        pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                        if (!pIV[i]) {
                                printf("malloc(pIV[%d]):failed !\n", i);
                                goto snow3g_f8_8_buffer_exit;
                        }

                        memcpy(pKey[i], testVectors[j].key,
                               testVectors[j].keyLenInBytes);

                        memcpy(pSrcBuff[i], testVectors[j].plaintext, length);

                        memset(pDstBuff[i], 0, length);

                        memcpy(pIV[i], testVectors[j].iv,
                               testVectors[j].ivLenInBytes);

                        /* init key shed */
                        if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i],
                                                      pKeySched[i])) {
                                printf("IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr) "
                                       "error\n");
                                goto snow3g_f8_8_buffer_exit;
                        }
                }

                /*Test the encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_ENCRYPT, 8);
                else
                        IMB_SNOW3G_F8_8_BUFFER(mb_mgr, pKeySched[0], pIV[0],
                                               pIV[1], pIV[2], pIV[3], pIV[4],
                                               pIV[5], pIV[6], pIV[7],
                                               pSrcBuff[0], pDstBuff[0],
                                               packetLen[0], pSrcBuff[1],
                                               pDstBuff[1], packetLen[1],
                                               pSrcBuff[2], pDstBuff[2],
                                               packetLen[2], pSrcBuff[3],
                                               pDstBuff[3], packetLen[3],
                                               pSrcBuff[4], pDstBuff[4],
                                               packetLen[4], pSrcBuff[5],
                                               pDstBuff[5], packetLen[5],
                                               pSrcBuff[6], pDstBuff[6],
                                               packetLen[6], pSrcBuff[7],
                                               pDstBuff[7], packetLen[7]);

                /*compare the ciphertext with the encryped plaintext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pDstBuff[i], testVectors[j].ciphertext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_8_BUFFER(Enc) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pDstBuff[i],
                                               packetLen[i]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].ciphertext,
                                               packetLen[i]);
                                goto snow3g_f8_8_buffer_exit;
                        }
                }

                /*Test the decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pDstBuff, pSrcBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_DECRYPT, 8);
                else
                        IMB_SNOW3G_F8_8_BUFFER(mb_mgr, pKeySched[0], pIV[0],
                                               pIV[1], pIV[2], pIV[3], pIV[4],
                                               pIV[5], pIV[6], pIV[7],
                                               pDstBuff[0], pSrcBuff[0],
                                               packetLen[0], pDstBuff[1],
                                               pSrcBuff[1], packetLen[1],
                                               pDstBuff[2], pSrcBuff[2],
                                               packetLen[2], pDstBuff[3],
                                               pSrcBuff[3], packetLen[3],
                                               pDstBuff[4], pSrcBuff[4],
                                               packetLen[4], pDstBuff[5],
                                               pSrcBuff[5], packetLen[5],
                                               pDstBuff[6], pSrcBuff[6],
                                               packetLen[6], pDstBuff[7],
                                               pSrcBuff[7], packetLen[7]);

                /*Compare the plaintext with the decrypted ciphertext*/
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(pSrcBuff[i], testVectors[j].plaintext,
                                   packetLen[i]) != 0) {
                                printf("IMB_SNOW3G_F8_8_BUFFER(Dec) vector:%d "
                                       "buffer:%d\n", j, i);
                                snow3g_hexdump("Actual:", pSrcBuff[i],
                                               packetLen[i]);
                                snow3g_hexdump("Expected:",
                                               testVectors[j].plaintext,
                                               packetLen[i]);
                                goto snow3g_f8_8_buffer_exit;
                        }
                }
                /* free buffers before next iteration */
                for (i = 0; i < numPackets; i++) {
                        if (pKey[i] != NULL) {
                                free(pKey[i]);
                                pKey[i] = NULL;
                        }
                        if (pKeySched[i] != NULL) {
                                free(pKeySched[i]);
                                pKeySched[i] = NULL;
                        }
                        if (pSrcBuff[i] != NULL) {
                                free(pSrcBuff[i]);
                                pSrcBuff[i] = NULL;
                        }
                        if (pDstBuff[i] != NULL) {
                                free(pDstBuff[i]);
                                pDstBuff[i] = NULL;
                        }
                        if (pIV[i] != NULL) {
                                free(pIV[i]);
                                pIV[i] = NULL;
                        }
                }
        }

        /*vectors are in bits used to round up to bytes*/
        length = testVectors[1].dataLenInBytes;

        /*Create test Data for num Packets*/
        for (i = 0; i < numPackets; i++) {
                /* Test for packets of different length. */
                packetLen[i] = length - (i * 12);
                bitLens[i] = packetLen[i] * 8;
                bitOffsets[i] = 0;

                pKey[i] = malloc(keyLen);
                if (!pKey[i]) {
                        printf("malloc(pKey[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_exit;
                }
                pKeySched[i] = malloc(size);
                if (!pKeySched[i]) {
                        printf("malloc(pKeySched[%d]): failed !\n", i);
                        goto snow3g_f8_8_buffer_exit;
                }
                pSrcBuff[i] = malloc(packetLen[i]);
                if (!pSrcBuff[i]) {
                        printf("malloc(pSrcBuff[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_exit;
                }
                pDstBuff[i] = malloc(packetLen[i]);
                if (!pDstBuff[i]) {
                        printf("malloc(pDstBuff[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_exit;
                }
                pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                if (!pIV[i]) {
                        printf("malloc(pIV[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_exit;
                }
                memcpy(pKey[i], testVectors[1].key,
                       testVectors[1].keyLenInBytes);

                memcpy(pSrcBuff[i], testVectors[1].plaintext, packetLen[i]);

                memset(pDstBuff[i], 0, packetLen[i]);

                memcpy(pIV[i], testVectors[1].iv, testVectors[1].ivLenInBytes);

                /* init key shed */
                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i], pKeySched[i])) {
                        printf("IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr) error\n");
                        goto snow3g_f8_8_buffer_exit;
                }
        }

        /* Test the encrypt */
        if (job_api)
                submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                 pIV, pSrcBuff, pDstBuff,
                                 bitLens, bitOffsets, IMB_DIR_ENCRYPT, 8);
        else
                IMB_SNOW3G_F8_8_BUFFER(mb_mgr, pKeySched[0], pIV[0], pIV[1],
                                       pIV[2], pIV[3], pIV[4], pIV[5], pIV[6],
                                       pIV[7], pSrcBuff[0], pDstBuff[0],
                                       packetLen[0], pSrcBuff[1], pDstBuff[1],
                                       packetLen[1], pSrcBuff[2], pDstBuff[2],
                                       packetLen[2], pSrcBuff[3], pDstBuff[3],
                                       packetLen[3], pSrcBuff[4], pDstBuff[4],
                                       packetLen[4], pSrcBuff[5], pDstBuff[5],
                                       packetLen[5], pSrcBuff[6], pDstBuff[6],
                                       packetLen[6], pSrcBuff[7], pDstBuff[7],
                                       packetLen[7]);

        /*compare the ciphertext with the encryped plaintext*/
        for (i = 0; i < numPackets; i++) {
                if (memcmp(pDstBuff[i], testVectors[1].ciphertext,
                           packetLen[i]) != 0) {
                        printf("IMB_SNOW3G_F8_8_BUFFER(Enc, diff size) "
                               "vector:%d buffer:%d\n",
                               1, i);
                        snow3g_hexdump("Actual:", pDstBuff[i], packetLen[i]);
                        snow3g_hexdump("Expected:", testVectors[1].ciphertext,
                                       packetLen[i]);
                        goto snow3g_f8_8_buffer_exit;
                }
        }
        /* no errors detected */
        status = 0;

snow3g_f8_8_buffer_exit:
        for (i = 0; i < numPackets; i++) {
                if (pKey[i] != NULL)
                        free(pKey[i]);
                if (pKeySched[i] != NULL)
                        free(pKeySched[i]);
                if (pSrcBuff[i] != NULL)
                        free(pSrcBuff[i]);
                if (pDstBuff[i] != NULL)
                        free(pDstBuff[i]);
                if (pIV[i] != NULL)
                        free(pIV[i]);
        }

        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

static void
validate_snow3g_f8_8_blocks_multi_key(struct IMB_MGR *mb_mgr,
                                      uint32_t job_api,
                                      struct test_suite_context *uea2_ctx,
                                      struct test_suite_context *uia2_ctx)
{
        int length, numVectors, i, j;
        const int numPackets = 8;
        size_t size = 0;

        cipher_test_vector_t *testVectors = snow3g_cipher_test_vectors[1];
        /* snow3g f8 test vectors are located at index 1 */
        numVectors = numSnow3gCipherTestVectors[1];

        snow3g_key_schedule_t *pKeySched[NUM_SUPPORTED_BUFFERS];
        uint8_t *pKey[NUM_SUPPORTED_BUFFERS];
        uint8_t *pSrcBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pDstBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pIV[NUM_SUPPORTED_BUFFERS];
        uint32_t packetLen[NUM_SUPPORTED_BUFFERS];
        uint32_t bitOffsets[NUM_SUPPORTED_BUFFERS];
        uint32_t bitLens[NUM_SUPPORTED_BUFFERS];

        int status = -1;

        (void) uia2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_8_BUFFER_MULTIKEY: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(pSrcBuff, 0, sizeof(pSrcBuff));
        memset(pDstBuff, 0, sizeof(pDstBuff));
        memset(pIV, 0, sizeof(pIV));
        memset(pKey, 0, sizeof(pKey));
        memset(packetLen, 0, sizeof(packetLen));
        memset(pKeySched, 0, sizeof(pKeySched));

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f8_8_buffer_multikey_exit;
        }

        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size) {
                printf("snow3g_key_sched_multi_size() failure !\n");
                goto snow3g_f8_8_buffer_multikey_exit;
        }

        for (i = 0; i < numPackets; i++) {
                j = i % numVectors;

                length = testVectors[j].dataLenInBytes;
                packetLen[i] = length;
                bitLens[i] = length * 8;
                bitOffsets[i] = 0;

                pKeySched[i] = malloc(size);
                if (!pKeySched[i]) {
                        printf("malloc(pKeySched[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_multikey_exit;
                }
                pSrcBuff[i] = malloc(length);
                if (!pSrcBuff[i]) {
                        printf("malloc(pSrcBuff[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_multikey_exit;
                }
                pDstBuff[i] = malloc(length);
                if (!pDstBuff[i]) {
                        printf("malloc(pDstBuff[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_multikey_exit;
                }
                pKey[i] = malloc(testVectors[j].keyLenInBytes);
                if (!pKey[i]) {
                        printf("malloc(pKey[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_multikey_exit;
                }
                pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                if (!pIV[i]) {
                        printf("malloc(pIV[%d]):failed !\n", i);
                        goto snow3g_f8_8_buffer_multikey_exit;
                }

                memcpy(pKey[i], testVectors[j].key,
                       testVectors[j].keyLenInBytes);

                memcpy(pSrcBuff[i], testVectors[j].plaintext, length);

                memcpy(pIV[i], testVectors[j].iv, testVectors[j].ivLenInBytes);

                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i], pKeySched[i])) {
                        printf("IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr) error\n");
                        goto snow3g_f8_8_buffer_multikey_exit;
                }
        }

        /*Test the encrypt*/
        if (job_api)
                submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched, pIV, pSrcBuff,
                                 pDstBuff, bitLens, bitOffsets,
                                 IMB_DIR_ENCRYPT, 8);
        else
                IMB_SNOW3G_F8_8_BUFFER_MULTIKEY(mb_mgr,
                                        (const snow3g_key_schedule_t * const *)
                                        pKeySched,
                                        (const void * const *)pIV,
                                        (const void * const *)pSrcBuff,
                                        (void **)pDstBuff,
                                        packetLen);

        /*compare the ciphertext with the encrypted plaintext*/
        for (i = 0; i < numPackets; i++) {
                j = i % numVectors;
                if (memcmp(pDstBuff[i], testVectors[j].ciphertext,
                           packetLen[i]) != 0) {
                        printf("snow3g_f8_8_multi_buffer(Enc) vector:%d "
                               "buffer:%d\n",
                               j, i);
                        snow3g_hexdump("Actual:", pDstBuff[i], packetLen[i]);
                        snow3g_hexdump("Expected:", testVectors[j].ciphertext,
                                       packetLen[i]);
                        goto snow3g_f8_8_buffer_multikey_exit;
                }
        }

        /*Test the decrypt*/
        if (job_api)
                submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched, pIV, pSrcBuff,
                                 pDstBuff, bitLens, bitOffsets,
                                 IMB_DIR_DECRYPT, 8);
        else
                IMB_SNOW3G_F8_8_BUFFER_MULTIKEY(mb_mgr,
                        (const snow3g_key_schedule_t * const *) pKeySched,
                        (const void * const *)pIV,
                        (const void * const *)pDstBuff,
                        (void **)pSrcBuff, packetLen);

        /*Compare the plaintext with the decrypted ciphertext*/
        for (i = 0; i < numPackets; i++) {
                j = i % numVectors;
                if (memcmp(pSrcBuff[i], testVectors[j].plaintext,
                           packetLen[i]) != 0) {
                        printf("snow3g_f8_8_multi_buffer(Dec) vector:%d "
                               "buffer:%d\n", j, i);
                        snow3g_hexdump("Actual:", pSrcBuff[i], packetLen[i]);
                        snow3g_hexdump("Expected:", testVectors[j].plaintext,
                                       packetLen[i]);
                        goto snow3g_f8_8_buffer_multikey_exit;
                }
        }
        /* no errors detected */
        status = 0;

snow3g_f8_8_buffer_multikey_exit:
        for (i = 0; i < numPackets; i++) {
                if (pSrcBuff[i] != NULL)
                        free(pSrcBuff[i]);
                if (pDstBuff[i] != NULL)
                        free(pDstBuff[i]);
                if (pIV[i] != NULL)
                        free(pIV[i]);
                if (pKey[i] != NULL)
                        free(pKey[i]);
                if (pKeySched[i] != NULL)
                        free(pKeySched[i]);
        }

        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

static void
validate_snow3g_f8_n_blocks(struct IMB_MGR *mb_mgr, uint32_t job_api,
                            struct test_suite_context *uea2_ctx,
                            struct test_suite_context *uia2_ctx)
{
        int length, numVectors, i, numPackets = 16;
        size_t size = 0;
        cipher_test_vector_t *testVectors = snow3g_cipher_test_vectors[1];
        /* snow3g f8 test vectors are located at index 1 */
        numVectors = numSnow3gCipherTestVectors[1];

        snow3g_key_schedule_t *pKeySched[NUM_SUPPORTED_BUFFERS];
        uint8_t *pKey[NUM_SUPPORTED_BUFFERS];
        uint8_t *pSrcBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pDstBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pIV[NUM_SUPPORTED_BUFFERS];
        uint32_t packetLen[NUM_SUPPORTED_BUFFERS];
        uint32_t bitOffsets[NUM_SUPPORTED_BUFFERS];
        uint32_t bitLens[NUM_SUPPORTED_BUFFERS];
        int keyLen = MAX_KEY_LEN;
        int status = -1;

        (void) uia2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_N_BUFFER: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(pSrcBuff, 0, sizeof(pSrcBuff));
        memset(pDstBuff, 0, sizeof(pDstBuff));
        memset(pIV, 0, sizeof(pIV));
        memset(pKey, 0, sizeof(pKey));
        memset(packetLen, 0, sizeof(packetLen));
        memset(pKeySched, 0, sizeof(pKeySched));

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f8_n_buffer_exit;
        }

        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f8_n_buffer_exit;

        /*vectors are in bits used to round up to bytes*/
        length = testVectors[0].dataLenInBytes;

        /*	Create test Data for num Packets*/
        for (i = 0; i < numPackets; i++) {

                packetLen[i] = length;
                bitLens[i] = length * 8;
                bitOffsets[i] = 0;

                pKey[i] = malloc(keyLen);
                if (!pKey[i]) {
                        printf("malloc(pKey[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_exit;
                }
                pKeySched[i] = malloc(size);
                if (!pKeySched[i]) {
                        printf("malloc(pKeySched[%d]): failed !\n", i);
                        goto snow3g_f8_n_buffer_exit;
                }
                pSrcBuff[i] = malloc(length);
                if (!pSrcBuff[i]) {
                        printf("malloc(pSrcBuff[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_exit;
                }
                pDstBuff[i] = malloc(length);
                if (!pDstBuff[i]) {
                        printf("malloc(pDstBuff[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_exit;
                }
                pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                if (!pIV[i]) {
                        printf("malloc(pIV[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_exit;
                }

                memcpy(pKey[i], testVectors[0].key,
                       testVectors[0].keyLenInBytes);

                memcpy(pSrcBuff[i], testVectors[0].plaintext, length);

                memset(pDstBuff[i], 0, length);

                memcpy(pIV[i], testVectors[0].iv,
                       testVectors[0].ivLenInBytes);

                /* init key shed */
                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i],
                                              pKeySched[i])) {
                        printf("IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr) error\n");
                        goto snow3g_f8_n_buffer_exit;
                }
        }

        for (i = 0; i < NUM_SUPPORTED_BUFFERS; i++) {
                /*Test the encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_ENCRYPT, i + 1);
                else
                        IMB_SNOW3G_F8_N_BUFFER(mb_mgr, *pKeySched,
                                               (const void * const *)pIV,
                                               (const void * const *)pSrcBuff,
                                               (void **)pDstBuff,
                                               packetLen, i + 1);

                if (pDstBuff[0] == NULL) {
                        printf("N buffer failure\n");
                        goto snow3g_f8_n_buffer_exit;
                }

                /*Compare the data in the pDstBuff with the cipher pattern*/
                if (memcmp(testVectors[0].ciphertext, pDstBuff[i],
                           packetLen[i]) != 0) {
                        printf("IMB_SNOW3G_F8_N_BUFFER(Enc) , vector:%d\n", i);
                        snow3g_hexdump("Actual:", pDstBuff[i], packetLen[0]);
                        snow3g_hexdump("Expected:", testVectors[0].ciphertext,
                                       packetLen[0]);
                        goto snow3g_f8_n_buffer_exit;
                }

                /*Test the Decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_DECRYPT, i + 1);
                else
                        IMB_SNOW3G_F8_N_BUFFER(mb_mgr, *pKeySched,
                                               (const void * const *)pIV,
                                               (const void * const *)pDstBuff,
                                               (void **)pSrcBuff,
                                               packetLen, i + 1);
                if (pSrcBuff[0] == NULL) {
                        printf("N buffer failure\n");
                        goto snow3g_f8_n_buffer_exit;
                }

                /*Compare the data in the pSrcBuff with the pDstBuff*/
                if (memcmp(pSrcBuff[i], testVectors[0].plaintext,
                           packetLen[i]) != 0) {
                        printf("snow3g_f8_n_buffer equal sizes, vector:%d\n",
                               i);
                        snow3g_hexdump("Actual:", pSrcBuff[i], packetLen[i]);
                        snow3g_hexdump("Expected:", testVectors[0].plaintext,
                                       packetLen[0]);
                        goto snow3g_f8_n_buffer_exit;
                }
        }
        /* no errors detected */
        status = 0;

snow3g_f8_n_buffer_exit:
        for (i = 0; i < numPackets; i++) {
                if (pKey[i] != NULL)
                        free(pKey[i]);
                if (pKeySched[i] != NULL)
                        free(pKeySched[i]);
                if (pSrcBuff[i] != NULL)
                        free(pSrcBuff[i]);
                if (pDstBuff[i] != NULL)
                        free(pDstBuff[i]);
                if (pIV[i] != NULL)
                        free(pIV[i]);
        }

        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

static void
validate_snow3g_f8_n_blocks_multi(struct IMB_MGR *mb_mgr,
                                  uint32_t job_api,
                                  struct test_suite_context *uea2_ctx,
                                  struct test_suite_context *uia2_ctx)
{
        int length, numVectors, i, numPackets = NUM_SUPPORTED_BUFFERS;
        size_t size = 0;
        cipher_test_vector_t *testVectors = snow3g_cipher_test_vectors[1];
        /* snow3g f8 test vectors are located at index 1 */
        numVectors = numSnow3gCipherTestVectors[1];

        snow3g_key_schedule_t *pKeySched[NUM_SUPPORTED_BUFFERS];
        uint8_t *pKey[NUM_SUPPORTED_BUFFERS];
        uint8_t *pSrcBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pDstBuff[NUM_SUPPORTED_BUFFERS];
        uint8_t *pIV[NUM_SUPPORTED_BUFFERS];
        uint32_t packetLen[NUM_SUPPORTED_BUFFERS];
        uint32_t bitOffsets[NUM_SUPPORTED_BUFFERS];
        uint32_t bitLens[NUM_SUPPORTED_BUFFERS];
        int status = -1;

        (void) uia2_ctx;

#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F8_N_BUFFER_MULTIKEY: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        memset(pSrcBuff, 0, sizeof(pSrcBuff));
        memset(pDstBuff, 0, sizeof(pDstBuff));
        memset(pIV, 0, sizeof(pIV));
        memset(pKeySched, 0, sizeof(pKeySched));
        memset(pKey, 0, sizeof(pKey));
        memset(packetLen, 0, sizeof(packetLen));

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f8_n_buffer_multikey_exit;
        }

        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f8_n_buffer_multikey_exit;

        for (i = 0; i < numPackets; i++) {
                length = testVectors[0].dataLenInBytes;
                packetLen[i] = length;
                bitLens[i] = length * 8;
                bitOffsets[i] = 0;

                pKeySched[i] = malloc(size);
                if (!pKeySched[i]) {
                        printf("malloc(pKeySched[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_multikey_exit;
                }
                pSrcBuff[i] = malloc(length);
                if (!pSrcBuff[i]) {
                        printf("malloc(pSrcBuff[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_multikey_exit;
                }
                pDstBuff[i] = malloc(length);
                if (!pDstBuff[i]) {
                        printf("malloc(pDstBuff[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_multikey_exit;
                }
                pKey[i] = malloc(testVectors[0].keyLenInBytes);
                if (!pKey[i]) {
                        printf("malloc(pKey[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_multikey_exit;
                }
                pIV[i] = malloc(SNOW3G_IV_LEN_IN_BYTES);
                if (!pIV[i]) {
                        printf("malloc(pIV[%d]):failed !\n", i);
                        goto snow3g_f8_n_buffer_multikey_exit;
                }

                memcpy(pKey[i], testVectors[0].key,
                       testVectors[0].keyLenInBytes);

                memcpy(pSrcBuff[i], testVectors[0].plaintext, length);

                memcpy(pIV[i], testVectors[0].iv, testVectors[0].ivLenInBytes);

                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey[i], pKeySched[i])) {
                        printf("IMB_SNOW3G_INIT_KEY_SCHED() error\n");
                        goto snow3g_f8_n_buffer_multikey_exit;
                }
        }

        for (i = 0; i < numPackets; i++) {
                /*Test the encrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pSrcBuff, pDstBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_ENCRYPT, i + 1);
                else
                        IMB_SNOW3G_F8_N_BUFFER_MULTIKEY(mb_mgr,
                                (const snow3g_key_schedule_t * const *)
                                pKeySched,
                                (const void * const *)pIV,
                                (const void * const *)pSrcBuff,
                                (void **)pDstBuff, packetLen, i + 1);

                if (pDstBuff[0] == NULL) {
                        printf("N buffer failure\n");
                        goto snow3g_f8_n_buffer_multikey_exit;
                }

                /*Compare the data in the pDstBuff with the cipher pattern*/
                if (memcmp(testVectors[0].ciphertext, pDstBuff[i],
                           packetLen[i]) != 0) {
                        printf("IMB_SNOW3G_F8_N_BUFFER(Enc) , vector:%d "
                               "buffer: %d\n", 0, i);
                        snow3g_hexdump("Actual:", pDstBuff[i], packetLen[i]);
                        snow3g_hexdump("Expected:", testVectors[0].ciphertext,
                                       packetLen[i]);
                        goto snow3g_f8_n_buffer_multikey_exit;
                }

                /*Test the Decrypt*/
                if (job_api)
                        submit_uea2_jobs(mb_mgr, (uint8_t **)&pKeySched,
                                         pIV, pDstBuff, pSrcBuff,
                                         bitLens, bitOffsets,
                                         IMB_DIR_DECRYPT, i + 1);
                else
                        IMB_SNOW3G_F8_N_BUFFER_MULTIKEY(mb_mgr,
                                (const snow3g_key_schedule_t * const *)
                                pKeySched,
                                (const void * const *)pIV,
                                (const void * const *)pDstBuff,
                                (void **)pSrcBuff, packetLen, i + 1);

                if (pSrcBuff[0] == NULL) {
                        printf("N buffer failure\n");
                        goto snow3g_f8_n_buffer_multikey_exit;
                }

                /*Compare the data in the pSrcBuff with the pDstBuff*/
                if (memcmp(pSrcBuff[i], testVectors[0].plaintext,
                           packetLen[i]) != 0) {
                        printf("snow3g_f8_n_buffer equal sizes, vector:%d "
                               "buffer: %d\n", 0, i);
                        snow3g_hexdump("Actual:", pSrcBuff[i], packetLen[i]);
                        snow3g_hexdump("Expected:", testVectors[0].plaintext,
                                       packetLen[i]);
                        goto snow3g_f8_n_buffer_multikey_exit;
                }
        }
        /* no errors detected */
        status = 0;

snow3g_f8_n_buffer_multikey_exit:
        for (i = 0; i < numPackets; i++) {
                if (pSrcBuff[i] != NULL)
                        free(pSrcBuff[i]);
                if (pDstBuff[i] != NULL)
                        free(pDstBuff[i]);
                if (pIV[i] != NULL)
                        free(pIV[i]);
                if (pKey[i] != NULL)
                        free(pKey[i]);
                if (pKeySched[i] != NULL)
                        free(pKeySched[i]);

        }
        if (status < 0)
                test_suite_update(uea2_ctx, 0, 1);
        else
                test_suite_update(uea2_ctx, 1, 0);
}

static void
validate_snow3g_f9(struct IMB_MGR *mb_mgr, uint32_t job_api,
                   struct test_suite_context *uea2_ctx,
                   struct test_suite_context *uia2_ctx)
{
        int numVectors, i, inputLen;
        size_t size = 0;
        hash_test_vector_t *testVectors = snow3g_hash_test_vectors[2];
        /* snow3g f9 test vectors are located at index 2 */
        numVectors = numSnow3gHashTestVectors[2];

        snow3g_key_schedule_t *pKeySched = NULL;
        uint8_t *pKey = NULL;
        int keyLen = MAX_KEY_LEN;
        uint8_t srcBuff[MAX_DATA_LEN];
        uint8_t digest[DIGEST_LEN];
        uint8_t *pIV = NULL;
        int status = -1;

        (void) uea2_ctx;
#ifdef DEBUG
        printf("Testing IMB_SNOW3G_F9_1_BUFFER: (%s):\n",
               job_api ? "Job API" : "Direct API");
#endif

        if (!numVectors) {
                printf("No Snow3G test vectors found !\n");
                goto snow3g_f9_1_buffer_exit;
        }

        pIV = malloc(SNOW3G_IV_LEN_IN_BYTES);
        if (!pIV) {
                printf("malloc(pIV):failed !\n");
                goto snow3g_f9_1_buffer_exit;
        }

        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pKey):failed !\n");
                goto snow3g_f9_1_buffer_exit;
        }
        size = IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr);
        if (!size)
                goto snow3g_f9_1_buffer_exit;

        pKeySched = malloc(size);
        if (!pKeySched) {
                printf("malloc(IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr)): "
                       "failed !\n");
                goto snow3g_f9_1_buffer_exit;
        }

        /*Get test data for for Snow3g 1 Packet version*/
        for (i = 0; i < numVectors; i++) {
                inputLen = (testVectors[i].lengthInBits + 7) / 8;

                memcpy(pKey, testVectors[i].key, testVectors[i].keyLenInBytes);
                memcpy(srcBuff, testVectors[i].input, inputLen);
                memcpy(pIV, testVectors[i].iv, testVectors[i].ivLenInBytes);

                /*Only 1 key sched is used*/
                if (IMB_SNOW3G_INIT_KEY_SCHED(mb_mgr, pKey, pKeySched)) {
                        printf("IMB_SNOW3G_KEY_SCHED_SIZE(mb_mgr): error\n");
                        goto snow3g_f9_1_buffer_exit;
                }

                /*test the integrity for f9_user with IV*/
                if (job_api) {
                        unsigned j;
                        const unsigned num_jobs_tab[] = {
                                1, 3, 4, 5, 7, 8, 9, 15, 16, 17
                        };

                        for (j = 0; j < DIM(num_jobs_tab); j++) {
                                int ret = submit_uia2_job(mb_mgr,
                                                    (uint8_t *)pKeySched,
                                                    pIV, srcBuff, digest,
                                                    testVectors[i].lengthInBits,
                                                    testVectors[i].exp_out,
                                                    num_jobs_tab[j]);
                                if (ret < 0) {
                                        printf("IMB_SNOW3G_F9 JOB API "
                                               "vector num:%d\n", i);
                                        goto snow3g_f9_1_buffer_exit;
                                }
                        }
                } else {
                        IMB_SNOW3G_F9_1_BUFFER(mb_mgr, pKeySched, pIV, srcBuff,
                                               testVectors[i].lengthInBits,
                                               digest);

                        /*Compare the digest with the expected in the vectors*/
                        if (memcmp(digest, testVectors[i].exp_out,
                                   DIGEST_LEN) != 0) {
                                printf("IMB_SNOW3G_F9_1_BUFFER() "
                                       "vector num:%d\n", i);
                                snow3g_hexdump("Actual:", digest, DIGEST_LEN);
                                snow3g_hexdump("Expected:",
                                               testVectors[i].exp_out,
                                               DIGEST_LEN);
                                goto snow3g_f9_1_buffer_exit;
                        }
                }

        } /* for numVectors */
        /* no errors detected */
        status = 0;

snow3g_f9_1_buffer_exit:
        free(pIV);
        free(pKey);
        free(pKeySched);

        if (status < 0)
                test_suite_update(uia2_ctx, 0, 1);
        else
                test_suite_update(uia2_ctx, 1, 0);
}

static int validate_f8_iv_gen(void)
{
        uint32_t i;
        uint8_t IV[16];
        const uint32_t numVectors = MAX_BIT_BUFFERS;

#ifdef DEBUG
        printf("Testing snow3g_f8_iv_gen:\n");
#endif

        /* skip first vector as it's not part of test data */
        for (i = 1; i < numVectors; i++) {
                cipher_iv_gen_params_t *iv_params =
                        &snow3g_f8_linear_bitvectors.iv_params[i];

                memset(IV, 0, sizeof(IV));

                /* generate IV */
                if (snow3g_f8_iv_gen(iv_params->count, iv_params->bearer,
                                     iv_params->dir, &IV) < 0)
                        return 1;

                /* validate result */
                if (memcmp(IV, snow3g_f8_linear_bitvectors.iv[i], 16) != 0) {
                        printf("snow3g_f8_iv_gen vector num: %d\n", i);
                        snow3g_hexdump("Actual", IV, 16);
                        snow3g_hexdump("Expected",
                                       snow3g_f8_linear_bitvectors.iv[i], 16);
                        return 1;
                }
        }

        return 0;
}

static int validate_f9_iv_gen(void)
{
        uint32_t i;
        uint8_t IV[16];
        /* snow3g f9 test vectors are located at index 2 */
        const uint32_t numVectors = numSnow3gHashTestVectors[2];

#ifdef DEBUG
        printf("Testing snow3g_f9_iv_gen:\n");
#endif

        /* 6 test sets */
        for (i = 0; i < numVectors; i++) {
                hash_iv_gen_params_t *iv_params =
                        &snow_f9_vectors[i].iv_params;

                memset(IV, 0, sizeof(IV));

                /* generate IV */
                if (snow3g_f9_iv_gen(iv_params->count, iv_params->fresh,
                                     iv_params->dir, &IV) < 0)
                        return 1;

                /* validate result */
                if (memcmp(IV, snow_f9_vectors[i].iv, 16) != 0) {
                        printf("snow3g_f9_iv_gen vector num: %d\n", i);
                        snow3g_hexdump("Actual", IV, 16);
                        snow3g_hexdump("Expected", snow_f9_vectors[i].iv, 16);
                        return 1;
                }
        }

        return 0;
}

int snow3g_test(struct IMB_MGR *mb_mgr)
{
        int errors = 0;
        uint32_t i;
        struct test_suite_context uea2_ctx;
        struct test_suite_context uia2_ctx;

        test_suite_start(&uea2_ctx, "SNOW3G-UEA2");
        test_suite_start(&uia2_ctx, "SNOW3G-UIA2");

        if (validate_f8_iv_gen()) {
                printf("validate_snow3g_f8_iv_gen:: FAIL\n");
                test_suite_update(&uea2_ctx, 0, 1);
        } else
                test_suite_update(&uea2_ctx, 1, 0);
        if (validate_f9_iv_gen()) {
                printf("validate_snow3g_f9_iv_gen:: FAIL\n");
                test_suite_update(&uia2_ctx, 0, 1);
        } else
                test_suite_update(&uia2_ctx, 1, 0);

        /* validate direct api */
        for (i = 0; i < DIM(snow3g_func_tab); i++)
                snow3g_func_tab[i].func(mb_mgr, 0, &uea2_ctx, &uia2_ctx);

        /* validate job api */
        for (i = 0; i < DIM(snow3g_func_tab); i++)
                snow3g_func_tab[i].func(mb_mgr, 1, &uea2_ctx, &uia2_ctx);

        errors += test_suite_end(&uea2_ctx);
        errors += test_suite_end(&uia2_ctx);

        return errors;
}
