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
* KASUMI functional test
*-----------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <intel-ipsec-mb.h>

#include "gcm_ctr_vectors_test.h"
#include "kasumi_test_vectors.h"
#include "utils.h"

#define KASUMIIVLEN 8
#define PAD_LEN 16

int kasumi_test(struct IMB_MGR *mb_mgr);
static int
validate_kasumi_f8_1_block(struct IMB_MGR *mb_mgr, const unsigned job_api);
static int
validate_kasumi_f8_1_bitblock(struct IMB_MGR *mb_mgr, const unsigned job_api);
static int
validate_kasumi_f8_1_bitblock_offset(struct IMB_MGR *mb_mgr,
                                     const unsigned job_api);
static int
validate_kasumi_f8_2_blocks(struct IMB_MGR *mb_mgr, const unsigned job_api);
static int
validate_kasumi_f8_3_blocks(struct IMB_MGR *mb_mgr, const unsigned job_api);
static int
validate_kasumi_f8_4_blocks(struct IMB_MGR *mb_mgr, const unsigned job_api);
static int
validate_kasumi_f8_n_blocks(struct IMB_MGR *mb_mgr, const unsigned job_api);
static int
validate_kasumi_f9(IMB_MGR *mgr, const unsigned job_api);
static int
validate_kasumi_f9_user(IMB_MGR *mgr, const unsigned job_api);

struct kasumi_test_case {
        int (*func)(struct IMB_MGR *, const unsigned job_api);
        const char *func_name;
};

/* kasumi f8 validation function pointer table */
struct kasumi_test_case kasumi_f8_func_tab[] = {
        {validate_kasumi_f8_1_block, "validate_kasumi_f8_1_block"},
        {validate_kasumi_f8_1_bitblock, "validate_kasumi_f8_1_bitblock"},
        {validate_kasumi_f8_1_bitblock_offset,
         "validate_kasumi_f8_1_bitblock_offset"},
        {validate_kasumi_f8_2_blocks, "validate_kasumi_f8_2_blocks"},
        {validate_kasumi_f8_3_blocks, "validate_kasumi_f8_3_blocks"},
        {validate_kasumi_f8_4_blocks, "validate_kasumi_f8_4_blocks"},
        {validate_kasumi_f8_n_blocks, "validate_kasumi_f8_n_blocks"}
};

/* kasumi f9 validation function pointer table */
struct kasumi_test_case kasumi_f9_func_tab[] = {
        {validate_kasumi_f9, "validate_kasumi_f9"},
        {validate_kasumi_f9_user, "validate_kasumi_f9_user"}
};

static int
submit_kasumi_f8_jobs(struct IMB_MGR *mb_mgr, kasumi_key_sched_t **keys,
                      uint64_t **ivs, uint8_t **src, uint8_t **dst,
                      const uint32_t *bitlens, const uint32_t *bit_offsets,
                      int dir, const unsigned int num_jobs)
{
        IMB_JOB *job;
        unsigned int i;
        unsigned int jobs_rx = 0;

        for (i = 0; i < num_jobs; i++) {
                job = IMB_GET_NEXT_JOB(mb_mgr);
                job->cipher_direction = dir;
                job->chain_order = IMB_ORDER_CIPHER_HASH;
                job->cipher_mode = IMB_CIPHER_KASUMI_UEA1_BITLEN;
                job->src = src[i];
                job->dst = dst[i];
                job->iv = (void *)ivs[i];
                job->iv_len_in_bytes = 8;
                job->enc_keys = (uint8_t *)keys[i];
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
                } else {
                        printf("Expected returned job, but got nothing\n");
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
submit_kasumi_f9_job(struct IMB_MGR *mb_mgr, kasumi_key_sched_t *key,
                     uint8_t *src, uint8_t *tag, const uint32_t len)
{
        IMB_JOB *job;

        job = IMB_GET_NEXT_JOB(mb_mgr);
        job->chain_order = IMB_ORDER_CIPHER_HASH;
        job->cipher_mode = IMB_CIPHER_NULL;
        job->src = src;
        job->u.KASUMI_UIA1._key = key;

        job->hash_start_src_offset_in_bytes = 0;
        job->msg_len_to_hash_in_bytes = len;
        job->hash_alg = IMB_AUTH_KASUMI_UIA1;
        job->auth_tag_output = tag;
        job->auth_tag_output_len_in_bytes = 4;

        job = IMB_SUBMIT_JOB(mb_mgr);
        if (job != NULL) {
                if (job->status != IMB_STATUS_COMPLETED) {
                        printf("%d error status:%d",
                               __LINE__, job->status);
                        return -1;
                }
        } else {
                printf("Expected returned job, but got nothing\n");
                return -1;
        }

        return 0;
}

static int validate_kasumi_f8_1_block(IMB_MGR *mgr, const unsigned job_api)
{
        int numKasumiTestVectors, i = 0;
        uint8_t *pKey = NULL;
        int keyLen = MAX_KEY_LEN;
        uint8_t srcBuff[MAX_DATA_LEN];
        uint8_t dstBuff[MAX_DATA_LEN];
        uint64_t IV;
        kasumi_key_sched_t *pKeySched = NULL;
        cipher_test_vector_t *kasumi_test_vectors = NULL;
        uint8_t *pSrcBuff = srcBuff;
        uint64_t *pIV = &IV;

        printf("Testing IMB_KASUMI_F8_1_BUFFER (%s):\n",
               job_api ? "Job API" : "Direct API");

        kasumi_test_vectors = kasumi_f8_vectors;
        numKasumiTestVectors = numCipherTestVectors[0];

        if (!numKasumiTestVectors) {
                printf("No Kasumi vectors found !\n");
                return 1;
        }
        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pKey):failed !\n");
                return 1;
        }
        pKeySched = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
        if (!pKeySched) {
                printf("malloc(IMB_KASUMI_KEY_SCHED_SIZE()): failed !\n");
                free(pKey);
                return 1;
        }

        /* Copy the data for for Kasumi_f8 1 Packet version */
        for (i = 0; i < numKasumiTestVectors; i++) {
                uint32_t byteLen = kasumi_test_vectors[i].dataLenInBytes;
                uint32_t bitLen = byteLen * 8;
                uint32_t bitOffset = 0;

                memcpy(pKey, kasumi_test_vectors[i].key,
                       kasumi_test_vectors[i].keyLenInBytes);
                memcpy(srcBuff, kasumi_test_vectors[i].plaintext,
                       kasumi_test_vectors[i].dataLenInBytes);
                memcpy(dstBuff, kasumi_test_vectors[i].ciphertext,
                       kasumi_test_vectors[i].dataLenInBytes);
                memcpy((uint8_t *)&IV, kasumi_test_vectors[i].iv,
                       kasumi_test_vectors[i].ivLenInBytes);

                /*setup the keysched to be used*/
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, pKey, pKeySched)) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        free(pKey);
                        free(pKeySched);
                        return 1;
                }

                /* Validate Encrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &pSrcBuff,
                                              &pSrcBuff, &bitLen, &bitOffset,
                                              IMB_DIR_ENCRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER(mgr, pKeySched, IV, srcBuff,
                                               srcBuff, byteLen);

                /*check against the cipher test in the vector against the
                 * encrypted
                 * plaintext*/
                if (memcmp(srcBuff, dstBuff,
                           kasumi_test_vectors[i].dataLenInBytes) != 0) {
                        printf("kasumi_f8_1_block(Enc)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", srcBuff,
                                kasumi_test_vectors[i].dataLenInBytes);
                        hexdump(stdout, "Expected:", dstBuff,
                                kasumi_test_vectors[i].dataLenInBytes);
                        free(pKey);
                        free(pKeySched);
                        return 1;
                }

                memcpy(dstBuff, kasumi_test_vectors[i].plaintext,
                       kasumi_test_vectors[i].dataLenInBytes);

                /*Validate Decrypt*/
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &pSrcBuff,
                                              &pSrcBuff, &bitLen,
                                              &bitOffset, IMB_DIR_DECRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER(mgr, pKeySched, IV, srcBuff,
                                               srcBuff, byteLen);

                if (memcmp(srcBuff, dstBuff,
                           kasumi_test_vectors[i].dataLenInBytes) != 0) {
                        printf("kasumi_f8_1_block(Dec)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", srcBuff,
                                kasumi_test_vectors[i].dataLenInBytes);
                        hexdump(stdout, "Expected:", dstBuff,
                                kasumi_test_vectors[i].dataLenInBytes);
                        free(pKey);
                        free(pKeySched);
                        return 1;
                }
        }

        free(pKey);
        free(pKeySched);
        printf("[%s]:  PASS, for %d single buffers.\n", __FUNCTION__, i);
        return 0;
}

/* Shift right a buffer by "offset" bits, "offset" < 8 */
static void buffer_shift_right(uint8_t *buffer, uint32_t length, uint8_t offset)
{
        uint8_t curr_byte, prev_byte;
        const uint32_t length_in_bytes = (length + offset + 7) / CHAR_BIT;
        const uint8_t lower_byte_mask = (1 << offset) - 1;
        uint32_t i;

        /* Padding */
        prev_byte = 0xff;

        for (i = 0; i < length_in_bytes; i++) {
                curr_byte = buffer[i];
                buffer[i] = ((prev_byte & lower_byte_mask) << (8 - offset)) |
                            (curr_byte >> offset);
                prev_byte = curr_byte;
        }
}

static void copy_test_bufs(uint8_t *plainBuff, uint8_t *wrkBuff,
                           uint8_t *ciphBuff, const uint8_t *src_test,
                           const uint8_t *dst_test, const uint32_t byte_len)
{
        /* Reset all buffers to -1 (for padding check) and copy test vectors */
        memset(wrkBuff, -1, (byte_len + PAD_LEN * 2));
        memset(plainBuff, -1, (byte_len + PAD_LEN * 2));
        memset(ciphBuff, -1, (byte_len + PAD_LEN * 2));
        memcpy(plainBuff + PAD_LEN, src_test, byte_len);
        memcpy(ciphBuff + PAD_LEN, dst_test, byte_len);
}

static int validate_kasumi_f8_1_bitblock(IMB_MGR *mgr, const unsigned job_api)
{
        int numKasumiTestVectors, i = 0;
        kasumi_key_sched_t *pKeySched = NULL;
        const cipherbit_test_vector_t *kasumi_bit_vectors = NULL;

        printf("Testing IMB_KASUMI_F8_1_BUFFER_BIT (%s):\n",
               job_api ? "Job API" : "Direct API");

        kasumi_bit_vectors = kasumi_f8_bitvectors;
        numKasumiTestVectors = numCipherTestVectors[1];

        uint8_t *pKey = NULL;
        int keyLen = MAX_KEY_LEN;
        uint8_t plainBuff[MAX_DATA_LEN];
        uint8_t ciphBuff[MAX_DATA_LEN];
        uint8_t wrkBuff[MAX_DATA_LEN];
        /* Adding extra byte for offset tests (shifting 4 bits) */
        uint8_t padding[PAD_LEN + 1];
        uint64_t IV;
        int ret = 1;
        uint64_t *pIV = &IV;

        memset(padding, -1, PAD_LEN + 1);

        if (!numKasumiTestVectors) {
                printf("No Kasumi vectors found !\n");
                return 1;
        }
        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pKey):failed !\n");
                return 1;
        }
        pKeySched = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
        if (!pKeySched) {
                printf("malloc(IMB_KASUMI_KEY_SCHED_SIZE()): failed !\n");
                free(pKey);
                return 1;
        }

        /* Copy the data for for Kasumi_f8 1 Packet version*/
        for (i = 0; i < numKasumiTestVectors; i++) {
                uint8_t *wrkBufBefPad = wrkBuff;
                uint8_t *wrkBufAftPad = wrkBuff + PAD_LEN;
                uint8_t *plainBufBefPad = plainBuff;
                uint8_t *plainBufAftPad = plainBuff + PAD_LEN;
                uint8_t *ciphBufBefPad = ciphBuff;
                uint8_t *ciphBufAftPad = ciphBuff + PAD_LEN;

                uint32_t bit_offset = 0;
                const uint32_t byte_len =
                        (kasumi_bit_vectors[i].LenInBits + 7) / CHAR_BIT;
                const uint32_t bit_len = kasumi_bit_vectors[i].LenInBits;

                memcpy(pKey, kasumi_bit_vectors[i].key,
                       kasumi_bit_vectors[i].keyLenInBytes);
                memcpy((uint8_t *)&IV, kasumi_bit_vectors[i].iv,
                       kasumi_bit_vectors[i].ivLenInBytes);
                copy_test_bufs(plainBufBefPad, wrkBufBefPad, ciphBufBefPad,
                               kasumi_bit_vectors[i].plaintext,
                               kasumi_bit_vectors[i].ciphertext,
                               byte_len);

                /* Setup the keysched to be used */
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, pKey, pKeySched)) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        goto end;
                }

                /* Validate Encrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &plainBufAftPad,
                                              &wrkBufAftPad, &bit_len,
                                              &bit_offset, IMB_DIR_ENCRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, pKeySched, IV,
                                                   plainBufAftPad, wrkBufAftPad,
                                                   bit_len, bit_offset);

                /* Check the ciphertext in the vector against the
                 * encrypted plaintext */
                if (membitcmp(wrkBufAftPad, ciphBufAftPad, 0, bit_len) != 0) {
                        printf("kasumi_f8_1_block(Enc) offset=0 vector:%d\n",
                               i);
                        hexdump(stdout, "Actual:", wrkBufAftPad, byte_len);
                        hexdump(stdout, "Expected:", ciphBufAftPad, byte_len);
                        goto end;
                }
                /* Check that data not to be ciphered was not overwritten */
                if (memcmp(wrkBufBefPad, ciphBufBefPad, PAD_LEN)) {
                        printf("overwrite head\n");
                        hexdump(stdout, "Head", wrkBufBefPad, PAD_LEN);
                        goto end;
                }
                if (memcmp(wrkBufAftPad + byte_len - 1,
                           ciphBufAftPad + byte_len - 1,
                           PAD_LEN + 1)) {
                        printf("overwrite tail\n");
                        hexdump(stdout, "Tail", wrkBufAftPad + byte_len - 1,
                                PAD_LEN + 1);
                        goto end;
                }
                /* Validate Decrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &ciphBufAftPad,
                                              &wrkBufAftPad, &bit_len,
                                              &bit_offset, IMB_DIR_DECRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, pKeySched, IV,
                                                   ciphBufAftPad, wrkBufAftPad,
                                                   bit_len, bit_offset);

                if (membitcmp(wrkBufAftPad, plainBufAftPad,
                              kasumi_bit_vectors[i].LenInBits, 0) != 0) {
                        printf("kasumi_f8_1_block(Dec) offset=0 vector:%d\n",
                               i);
                        hexdump(stdout, "Actual:", wrkBufAftPad, byte_len);
                        hexdump(stdout, "Expected:", plainBufAftPad, byte_len);
                        goto end;
                }
                copy_test_bufs(plainBufBefPad, wrkBufBefPad, ciphBufBefPad,
                               kasumi_bit_vectors[i].plaintext,
                               kasumi_bit_vectors[i].ciphertext,
                               byte_len);
                buffer_shift_right(plainBufBefPad, (byte_len + PAD_LEN * 2) * 8,
                                   4);
                buffer_shift_right(ciphBufBefPad, (byte_len + PAD_LEN * 2) * 8,
                                   4);
                bit_offset = 4;

                /* Validate Encrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &plainBufAftPad,
                                              &wrkBufAftPad, &bit_len,
                                              &bit_offset, IMB_DIR_ENCRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, pKeySched, IV,
                                                   plainBufAftPad, wrkBufAftPad,
                                                   bit_len, bit_offset);

                /* Check the ciphertext in the vector against the
                 * encrypted plaintext */
                if (membitcmp(wrkBufAftPad, ciphBufAftPad, bit_len, 4) != 0) {
                        printf("kasumi_f8_1_block(Enc) offset=4  vector:%d\n",
                               i);
                        hexdump(stdout, "Actual:", wrkBufAftPad, byte_len);
                        hexdump(stdout, "Expected:", ciphBufAftPad, byte_len);
                        goto end;
                }
                /*Validate Decrypt*/
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &ciphBufAftPad,
                                              &wrkBufAftPad, &bit_len,
                                              &bit_offset, IMB_DIR_DECRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, pKeySched, IV,
                                                   ciphBufAftPad, wrkBufAftPad,
                                                   bit_len, bit_offset);

                if (membitcmp(plainBufAftPad, plainBufAftPad,
                              bit_len, 4) != 0) {
                        printf("kasumi_f8_1_block(Dec) offset=4 vector:%d\n",
                               i);
                        hexdump(stdout, "Actual:", wrkBufAftPad, byte_len);
                        hexdump(stdout, "Expected:", plainBufAftPad, byte_len);
                        goto end;
                }
        }

        ret = 0;
        printf("[%s]:  PASS, for %d single buffers.\n", __FUNCTION__, i);
end:
        free(pKey);
        free(pKeySched);
        return ret;
}

static int validate_kasumi_f8_1_bitblock_offset(IMB_MGR *mgr,
                                                const unsigned job_api)
{
        int numKasumiTestVectors, i = 0;
        kasumi_key_sched_t *pKeySched = NULL;
        const cipherbit_test_linear_vector_t *kasumi_bit_vectors = NULL;

        printf("Testing IMB_KASUMI_F8_1_BUFFER_BIT (offset) (%s):\n",
               job_api ? "Job API" : "Direct API");

        kasumi_bit_vectors = &kasumi_f8_linear_bitvectors;
        numKasumiTestVectors = numCipherTestVectors[1];

        uint8_t *pKey = NULL;
        int keyLen = MAX_KEY_LEN;
        uint8_t srcBuff[MAX_DATA_LEN];
        uint8_t dstBuff[MAX_DATA_LEN];
        uint64_t IV;
        uint32_t bufferbytesize = 0;
        uint8_t wrkbuf[MAX_DATA_LEN];
        uint32_t offset = 0, byteoffset = 0, ret;
        uint8_t *pSrcBuff = srcBuff;
        uint8_t *pDstBuff = dstBuff;
        uint8_t *pWrkBuff = wrkbuf;
        uint64_t *pIV = &IV;

        memset(srcBuff, 0, sizeof(srcBuff));
        memset(dstBuff, 0, sizeof(dstBuff));
        memset(wrkbuf, 0, sizeof(wrkbuf));

        if (!numKasumiTestVectors) {
                printf("No Kasumi vectors found !\n");
                return 1;
        }
        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pKey):failed !\n");
                return 1;
        }
        pKeySched = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
        if (!pKeySched) {
                printf("malloc(IMB_KASUMI_KEY_SCHED_SIZE()): failed !\n");
                free(pKey);
                return 1;
        }
        for (i = 0; i < numKasumiTestVectors; i++)
                bufferbytesize += kasumi_bit_vectors->LenInBits[i];

        bufferbytesize = (bufferbytesize + 7) / CHAR_BIT;
        memcpy(srcBuff, kasumi_bit_vectors->plaintext, bufferbytesize);
        memcpy(dstBuff, kasumi_bit_vectors->ciphertext, bufferbytesize);

        /* Copy the data for for Kasumi_f8 1 Packet version */
        for (i = 0, offset = 0, byteoffset = 0; i < numKasumiTestVectors; i++) {
                uint32_t bit_len = kasumi_bit_vectors->LenInBits[i];

                memcpy(pKey, &kasumi_bit_vectors->key[i][0],
                       kasumi_bit_vectors->keyLenInBytes);
                memcpy((uint8_t *)&IV, &kasumi_bit_vectors->iv[i][0],
                       kasumi_bit_vectors->ivLenInBytes);

                /* Setup the keysched to be used */
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, pKey, pKeySched)) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        free(pKey);
                        free(pKeySched);
                        return 1;
                }

                /* Validate Encrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &pSrcBuff,
                                              &pWrkBuff, &bit_len,
                                              &offset, IMB_DIR_ENCRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, pKeySched, IV, srcBuff,
                                                   wrkbuf, bit_len, offset);

                /* Check against the ciphertext in the vector against the
                 * encrypted plaintext */
                ret = membitcmp(wrkbuf, dstBuff,
                                kasumi_bit_vectors->LenInBits[i], offset);
                if (ret != 0) {
                        printf("kasumi_f8_1_block_linear(Enc)  vector:%d, "
                               "index:%d\n",
                               i, ret);
                        hexdump(stdout, "Actual:", &wrkbuf[byteoffset],
                                (kasumi_bit_vectors->LenInBits[i] + 7) /
                                    CHAR_BIT);
                        hexdump(stdout, "Expected:", &dstBuff[byteoffset],
                                (kasumi_bit_vectors->LenInBits[i] + 7) /
                                    CHAR_BIT);
                        free(pKey);
                        free(pKeySched);
                        return 1;
                }
                offset += kasumi_bit_vectors->LenInBits[i];
                byteoffset = offset / CHAR_BIT;
        }
        for (i = 0, offset = 0, byteoffset = 0; i < numKasumiTestVectors; i++) {
                uint32_t bit_len = kasumi_bit_vectors->LenInBits[i];

                memcpy(pKey, &kasumi_bit_vectors->key[i][0],
                       kasumi_bit_vectors->keyLenInBytes);
                memcpy((uint8_t *)&IV, &kasumi_bit_vectors->iv[i][0],
                       kasumi_bit_vectors->ivLenInBytes);

                /* Setup the keysched to be used */
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, pKey, pKeySched)) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        free(pKey);
                        free(pKeySched);
                        return 1;
                }

                /* Validate Decrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, &pKeySched,
                                              &pIV, &pDstBuff,
                                              &pWrkBuff, &bit_len,
                                              &offset, IMB_DIR_DECRYPT, 1);
                else
                        IMB_KASUMI_F8_1_BUFFER_BIT(mgr, pKeySched, IV, dstBuff,
                                                   wrkbuf, bit_len, offset);

                ret = membitcmp(wrkbuf, srcBuff,
                                kasumi_bit_vectors->LenInBits[i], offset);
                if (ret != 0) {
                        printf("kasumi_f8_1_block_linear(Dec)  "
                               "vector:%d,index:%d\n",
                               i, ret);
                        hexdump(stdout, "Actual:", &wrkbuf[byteoffset],
                                (kasumi_bit_vectors->LenInBits[i] + 7) /
                                    CHAR_BIT);
                        hexdump(stdout, "Expected:", &srcBuff[byteoffset],
                                (kasumi_bit_vectors->LenInBits[i] + 7) /
                                    CHAR_BIT);
                        free(pKey);
                        free(pKeySched);
                        return 1;
                }
                offset += kasumi_bit_vectors->LenInBits[i];
                byteoffset = offset / CHAR_BIT;
        }

        free(pKey);
        free(pKeySched);
        printf("[%s]:  PASS, for %d single buffers.\n", __FUNCTION__, i);
        return 0;
}

static int validate_kasumi_f8_2_blocks(IMB_MGR *mgr, const unsigned job_api)
{

        int numKasumiTestVectors, i = 0, numPackets = 2;
        const cipher_test_vector_t *kasumi_test_vectors = NULL;
        kasumi_key_sched_t *keySched[3] = {NULL};

        printf("Testing IMB_KASUMI_F8_2_BUFFER (%s):\n",
               job_api ? "Job API" : "Direct API");

        kasumi_test_vectors = cipher_test_vectors[0];
        numKasumiTestVectors = numCipherTestVectors[0];

        uint8_t *key[3] = {NULL};
        int keyLen = MAX_KEY_LEN;
        uint64_t iv[3];
        uint8_t *srcBuff[3] = {NULL};
        uint8_t *dstBuff[3] = {NULL};
        uint32_t packetLen[3];
        uint32_t bitLens[3];
        uint32_t bitOffsets[3];
        uint64_t *pIV[3] = {NULL};
        int ret = 1;

        if (!numKasumiTestVectors) {
                printf("No Kasumi vectors found !\n");
                goto exit;
        }

        /* Create test Data for num Packets + 1 */
        for (i = 0; i < numPackets + 1; i++) {
                packetLen[i] = kasumi_test_vectors[i].dataLenInBytes;
                bitLens[i] = packetLen[i] * 8;
                bitOffsets[i] = 0;
                pIV[i] = &iv[i];

                key[i] = malloc(keyLen);
                if (!key[i]) {
                        printf("malloc(key):failed !\n");
                        goto exit;
                }
                keySched[i] = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
                if (!keySched[i]) {
                        printf("malloc(IMB_KASUMI_KEY_SCHED_SIZE()): "
                               "failed !\n");
                        goto exit;
                }
                srcBuff[i] = malloc(packetLen[i]);
                if (!srcBuff[i]) {
                        printf("malloc(srcBuff[%u]:failed !\n", i);
                        goto exit;
                }
                dstBuff[i] = malloc(packetLen[i]);
                if (!dstBuff[i]) {
                        printf("malloc(dstBuff[%u]:failed !\n", i);
                        goto exit;
                }

                memcpy(key[i], kasumi_test_vectors[i].key,
                       kasumi_test_vectors[i].keyLenInBytes);

                memcpy(srcBuff[i], kasumi_test_vectors[i].plaintext,
                       kasumi_test_vectors[i].dataLenInBytes);

                memcpy(dstBuff[i], kasumi_test_vectors[i].ciphertext,
                       kasumi_test_vectors[i].dataLenInBytes);

                memcpy(&iv[i], kasumi_test_vectors[i].iv,
                       kasumi_test_vectors[i].ivLenInBytes);

                /* init key schedule */
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, key[i], keySched[i])) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        goto exit;
                }

        }

        /* Test the encrypt */
        if (job_api)
                submit_kasumi_f8_jobs(mgr, keySched, (uint64_t **)&pIV,
                                      (uint8_t **)&srcBuff,
                                      (uint8_t **)&srcBuff,
                                      (uint32_t *)&bitLens,
                                      (uint32_t *)&bitOffsets,
                                      IMB_DIR_ENCRYPT, 2);
        else
                IMB_KASUMI_F8_2_BUFFER(mgr, keySched[0], iv[0], iv[1],
                                       srcBuff[0], srcBuff[0], packetLen[0],
                                       srcBuff[1], srcBuff[1], packetLen[1]);

        /* Compare the ciphertext with the encrypted plaintext */
        for (i = 0; i < numPackets; i++) {
                if (memcmp(srcBuff[i], kasumi_test_vectors[i].ciphertext,
                           packetLen[i]) != 0) {
                        printf("kasumi_f8_2_buffer(Enc)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", srcBuff[i], packetLen[i]);
                        hexdump(stdout, "Expected:",
                                kasumi_test_vectors[i].ciphertext,
                                packetLen[i]);
                        goto exit;
                }
        }
        for (i = 0; i < numPackets; i++)
                memcpy(srcBuff[i], kasumi_test_vectors[i].plaintext,
                       kasumi_test_vectors[i].dataLenInBytes);

        /* Test the encrypt reverse order (direct API only) */
        if (!job_api) {
                IMB_KASUMI_F8_2_BUFFER(mgr, keySched[0], iv[0], iv[1],
                                       srcBuff[1], srcBuff[1], packetLen[1],
                                       srcBuff[0], srcBuff[0], packetLen[0]);

                /* Compare the ciphertext with the encrypted plaintext */
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(srcBuff[i],
                                   kasumi_test_vectors[i].ciphertext,
                                   packetLen[i]) != 0) {
                                printf("kasumi_f8_2_buffer(Enc)  "
                                       "vector:%d\n", i);
                                hexdump(stdout, "Actual:", srcBuff[i],
                                        packetLen[i]);
                                hexdump(stdout, "Expected:",
                                        kasumi_test_vectors[i].ciphertext,
                                        packetLen[i]);
                                goto exit;
                        }
                }
                for (i = 0; i < numPackets + 1; i++)
                        memcpy(srcBuff[i], kasumi_test_vectors[i].plaintext,
                               kasumi_test_vectors[i].dataLenInBytes);

                /* Test the encrypt reverse order (direct API only) */
                IMB_KASUMI_F8_2_BUFFER(mgr, keySched[0], iv[0], iv[1],
                                       srcBuff[0], srcBuff[0], packetLen[0],
                                       srcBuff[2], srcBuff[2], packetLen[2]);

                /* Compare the ciphertext with the encrypted plaintext*/
                for (i = 0; i < numPackets + 1; i++) {
                        if (i == 1)
                                continue;
                        if (memcmp(srcBuff[i],
                                   kasumi_test_vectors[i].ciphertext,
                                   packetLen[i]) != 0) {
                                printf("kasumi_f8_2_buffer(Enc) "
                                       "vector:%d\n", i);
                                hexdump(stdout, "Actual:", srcBuff[i],
                                        packetLen[i]);
                                hexdump(stdout, "Expected:",
                                        kasumi_test_vectors[i].ciphertext,
                                        packetLen[i]);
                                goto exit;
                        }
                }
        }
        /* Test the decrypt */
        if (job_api)
                submit_kasumi_f8_jobs(mgr, keySched, (uint64_t **)&pIV,
                                      (uint8_t **)&dstBuff,
                                      (uint8_t **)&dstBuff,
                                      (uint32_t *)&bitLens,
                                      (uint32_t *)&bitOffsets,
                                      IMB_DIR_DECRYPT, 2);
        else
                IMB_KASUMI_F8_2_BUFFER(mgr, keySched[0], iv[0], iv[1],
                                       dstBuff[0], dstBuff[0], packetLen[0],
                                       dstBuff[1], dstBuff[1], packetLen[1]);

        /* Compare the plaintext with the decrypted ciphertext */
        for (i = 0; i < numPackets; i++) {
                if (memcmp(dstBuff[i], kasumi_test_vectors[i].plaintext,
                           packetLen[i]) != 0) {
                        printf("kasumi_f8_2_buffer(Dec)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", dstBuff[i], packetLen[i]);
                        hexdump(stdout, "Expected:",
                                kasumi_test_vectors[i].plaintext,
                                packetLen[i]);
                        goto exit;
                }
        }

        /* Test the decrypt reverse order (direct API only) */
        if (!job_api) {
                for (i = 0; i < numPackets; i++)
                        memcpy(dstBuff[i], kasumi_test_vectors[i].ciphertext,
                               kasumi_test_vectors[i].dataLenInBytes);

                IMB_KASUMI_F8_2_BUFFER(mgr, keySched[0], iv[0], iv[1],
                                       dstBuff[1], dstBuff[1], packetLen[1],
                                       dstBuff[0], dstBuff[0], packetLen[0]);

                /* Compare the plaintext with the decrypted ciphertext */
                for (i = 0; i < numPackets; i++) {
                        if (memcmp(dstBuff[i], kasumi_test_vectors[i].plaintext,
                                   packetLen[i]) != 0) {
                                printf("kasumi_f8_2_buffer(Dec) "
                                       "vector:%d\n", i);
                                hexdump(stdout, "Actual:", dstBuff[i],
                                        packetLen[i]);
                                hexdump(stdout, "Expected:",
                                        kasumi_test_vectors[i].plaintext,
                                        packetLen[i]);
                                goto exit;
                        }
                }
        }
        ret = 0;

        printf("[%s]: PASS.\n", __FUNCTION__);
exit:
        for (i = 0; i < numPackets + 1; i++) {
                free(key[i]);
                free(keySched[i]);
                free(srcBuff[i]);
                free(dstBuff[i]);
        }
        return ret;
}

static int validate_kasumi_f8_3_blocks(IMB_MGR *mgr, const unsigned job_api)
{
        int numKasumiTestVectors, i = 0, numPackets = 3;
        const cipher_test_vector_t *kasumi_test_vectors = NULL;
        kasumi_key_sched_t *keySched[3] = {NULL};

        printf("Testing IMB_KASUMI_F8_3_BUFFER (%s):\n",
               job_api ? "Job API" : "Direct API");

        kasumi_test_vectors = cipher_test_vectors[0];
        numKasumiTestVectors = numCipherTestVectors[0];

        uint8_t *key[3] = {NULL};
        int keyLen = MAX_KEY_LEN;
        uint64_t iv[3];
        uint8_t *srcBuff[3] = {NULL};
        uint8_t *dstBuff[3] = {NULL};
        uint32_t packetLen[3];
        uint32_t bitLens[3];
        uint32_t bitOffsets[3];
        uint64_t *pIV[3] = {NULL};
        int ret = 1;

        if (!numKasumiTestVectors) {
                printf("No Kasumi vectors found !\n");
                goto exit;
        }

        /* Create test Data for num Packets */
        for (i = 0; i < numPackets; i++) {
                packetLen[i] = kasumi_test_vectors[0].dataLenInBytes;
                bitLens[i] = packetLen[i] * 8;
                bitOffsets[i] = 0;
                pIV[i] = &iv[i];

                key[i] = malloc(keyLen);
                if (!key[i]) {
                        printf("malloc(key):failed !\n");
                        goto exit;
                }
                keySched[i] = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
                if (!keySched[i]) {
                        printf("malloc(IMB_KASUMI_KEY_SCHED_SIZE()): "
                               "failed !\n");
                        goto exit;
                }
                srcBuff[i] = malloc(packetLen[0]);
                if (!srcBuff[i]) {
                        printf("malloc(srcBuff[%u]:failed !\n", i);
                        goto exit;
                }
                dstBuff[i] = malloc(packetLen[0]);
                if (!dstBuff[i]) {
                        printf("malloc(dstBuff[%u]:failed !\n", i);
                        goto exit;
                }

                memcpy(key[i], kasumi_test_vectors[0].key,
                       kasumi_test_vectors[0].keyLenInBytes);

                memcpy(srcBuff[i], kasumi_test_vectors[0].plaintext,
                       kasumi_test_vectors[0].dataLenInBytes);

                memcpy(dstBuff[i], kasumi_test_vectors[0].ciphertext,
                       kasumi_test_vectors[0].dataLenInBytes);

                memcpy(&iv[i], kasumi_test_vectors[0].iv,
                       kasumi_test_vectors[0].ivLenInBytes);

                /* init key schedule */
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, key[0], keySched[i])) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        goto exit;
                }
        }

        /* Test the encrypt */
        if (job_api)
                submit_kasumi_f8_jobs(mgr, keySched, (uint64_t **)&pIV,
                                      (uint8_t **)&srcBuff,
                                      (uint8_t **)&srcBuff,
                                      (uint32_t *)&bitLens,
                                      (uint32_t *)&bitOffsets,
                                      IMB_DIR_ENCRYPT, 3);
        else
                IMB_KASUMI_F8_3_BUFFER(mgr, keySched[0], iv[0], iv[1], iv[2],
                                       srcBuff[0], srcBuff[0], srcBuff[1],
                                       srcBuff[1], srcBuff[2], srcBuff[2],
                                       packetLen[0]);

        /* Compare the ciphertext with the encrypted plaintext */
        for (i = 0; i < numPackets; i++) {
                if (memcmp(srcBuff[i], kasumi_test_vectors[0].ciphertext,
                           packetLen[0]) != 0) {
                        printf("kasumi_f8_3_buffer(Enc)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", srcBuff[i], packetLen[0]);
                        hexdump(stdout, "Expected:",
                                kasumi_test_vectors[0].ciphertext,
                                packetLen[0]);
                        goto exit;
                }
        }

        /* Test the decrypt */
        if (job_api)
                submit_kasumi_f8_jobs(mgr, keySched, (uint64_t **)&pIV,
                                      (uint8_t **)&dstBuff,
                                      (uint8_t **)&dstBuff,
                                      (uint32_t *)&bitLens,
                                      (uint32_t *)&bitOffsets,
                                      IMB_DIR_DECRYPT, 3);
        else
                IMB_KASUMI_F8_3_BUFFER(mgr, keySched[0], iv[0], iv[1], iv[2],
                                       dstBuff[0], dstBuff[0], dstBuff[1],
                                       dstBuff[1], dstBuff[2], dstBuff[2],
                                       packetLen[0]);

        /* Compare the plaintext with the decrypted ciphertext */
        for (i = 0; i < numPackets; i++) {
                if (memcmp(dstBuff[i], kasumi_test_vectors[0].plaintext,
                           packetLen[0]) != 0) {
                        printf("kasumi_f8_3_buffer(Dec)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", dstBuff[i], packetLen[0]);
                        hexdump(stdout, "Expected:",
                                kasumi_test_vectors[0].plaintext,
                                packetLen[0]);
                        goto exit;
                }
        }

        ret = 0;
        printf("[%s]: PASS.\n", __FUNCTION__);
exit:
        for (i = 0; i < numPackets; i++) {
                free(key[i]);
                free(keySched[i]);
                free(srcBuff[i]);
                free(dstBuff[i]);
        }
        return ret;
}

static int validate_kasumi_f8_4_blocks(IMB_MGR *mgr, const unsigned job_api)
{
        int numKasumiTestVectors, i = 0, numPackets = 4;
        const cipher_test_vector_t *kasumi_test_vectors = NULL;
        kasumi_key_sched_t *keySched[4] = {NULL};

        printf("Testing IMB_KASUMI_F8_4_BUFFER (%s):\n",
               job_api ? "Job API" : "Direct API");

        kasumi_test_vectors = cipher_test_vectors[0];
        numKasumiTestVectors = numCipherTestVectors[0];

        uint8_t *key[4] = {NULL};
        int keyLen = MAX_KEY_LEN;
        uint64_t iv[4];
        uint8_t *srcBuff[4] = {NULL};
        uint8_t *dstBuff[4] = {NULL};
        uint32_t packetLen[4];
        uint32_t bitLens[4];
        uint32_t bitOffsets[4];
        uint64_t *pIV[4] = {NULL};
        int ret = 1;

        if (!numKasumiTestVectors) {
                printf("No Kasumi vectors found !\n");
                goto exit;
        }

        /* Create test Data for num Packets */
        for (i = 0; i < numPackets; i++) {
                packetLen[i] = kasumi_test_vectors[0].dataLenInBytes;
                bitLens[i] = packetLen[i] * 8;
                bitOffsets[i] = 0;
                pIV[i] = &iv[i];

                key[i] = malloc(keyLen);
                if (!key[i]) {
                        printf("malloc(key):failed !\n");
                        goto exit;
                }
                keySched[i] = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
                if (!keySched[i]) {
                        printf("malloc(IMB_KASUMI_KEY_SCHED_SIZE()): "
                               "failed !\n");
                        goto exit;
                }
                srcBuff[i] = malloc(packetLen[0]);
                if (!srcBuff[i]) {
                        printf("malloc(srcBuff[%u]:failed !\n", i);
                        goto exit;
                }
                dstBuff[i] = malloc(packetLen[0]);
                if (!dstBuff[i]) {
                        printf("malloc(dstBuff[%u]:failed !\n", i);
                        goto exit;
                }

                memcpy(key[i], kasumi_test_vectors[0].key,
                       kasumi_test_vectors[0].keyLenInBytes);

                memcpy(srcBuff[i], kasumi_test_vectors[0].plaintext,
                       kasumi_test_vectors[0].dataLenInBytes);

                memcpy(dstBuff[i], kasumi_test_vectors[0].ciphertext,
                       kasumi_test_vectors[0].dataLenInBytes);

                memcpy(&iv[i], kasumi_test_vectors[0].iv,
                       kasumi_test_vectors[0].ivLenInBytes);

                /* init key schedule */
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, key[0], keySched[i])) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        goto exit;
                }
        }

        /* Test the encrypt */
        if (job_api)
                submit_kasumi_f8_jobs(mgr, keySched, (uint64_t **)&pIV,
                                      (uint8_t **)&srcBuff,
                                      (uint8_t **)&srcBuff,
                                      (uint32_t *)&bitLens,
                                      (uint32_t *)&bitOffsets,
                                      IMB_DIR_ENCRYPT, 4);
        else
                IMB_KASUMI_F8_4_BUFFER(mgr, keySched[0], iv[0], iv[1], iv[2],
                                       iv[3], srcBuff[0], srcBuff[0],
                                       srcBuff[1], srcBuff[1], srcBuff[2],
                                       srcBuff[2], srcBuff[3], srcBuff[3],
                                       packetLen[0]);

        /* Compare the ciphertext with the encrypted plaintext */
        for (i = 0; i < numPackets; i++) {
                if (memcmp(srcBuff[i], kasumi_test_vectors[0].ciphertext,
                           packetLen[0]) != 0) {
                        printf("kasumi_f8_4_buffer(Enc)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", srcBuff[i], packetLen[0]);
                        hexdump(stdout, "Expected:",
                                kasumi_test_vectors[0].ciphertext,
                                packetLen[0]);
                        goto exit;
                }
        }

        /* Test the decrypt */
        if (job_api)
                submit_kasumi_f8_jobs(mgr, keySched, (uint64_t **)&pIV,
                                      (uint8_t **)&dstBuff,
                                      (uint8_t **)&dstBuff,
                                      (uint32_t *)&bitLens,
                                      (uint32_t *)&bitOffsets,
                                      IMB_DIR_DECRYPT, 4);
        else
                IMB_KASUMI_F8_4_BUFFER(mgr, keySched[0], iv[0], iv[1], iv[2],
                                       iv[3], dstBuff[0], dstBuff[0],
                                       dstBuff[1], dstBuff[1], dstBuff[2],
                                       dstBuff[2], dstBuff[3], dstBuff[3],
                                       packetLen[0]);

        /*Compare the plaintext with the decrypted cipher text*/
        for (i = 0; i < numPackets; i++) {
                if (memcmp(dstBuff[i], kasumi_test_vectors[0].plaintext,
                           packetLen[0]) != 0) {
                        printf("kasumi_f8_4_buffer(Dec)  vector:%d\n", i);
                        hexdump(stdout, "Actual:", dstBuff[i], packetLen[0]);
                        hexdump(stdout, "Expected:",
                                kasumi_test_vectors[0].plaintext,
                                packetLen[0]);
                        goto exit;
                }
        }

        ret = 0;
        printf("[%s]: PASS.\n", __FUNCTION__);
exit:
        for (i = 0; i < numPackets; i++) {
                free(key[i]);
                free(keySched[i]);
                free(srcBuff[i]);
                free(dstBuff[i]);
        }
        return ret;
}

static int validate_kasumi_f8_n_blocks(IMB_MGR *mgr, const unsigned job_api)
{
        kasumi_key_sched_t *pKeySched[NUM_SUPPORTED_BUFFERS] = {NULL};
        uint64_t IV[NUM_SUPPORTED_BUFFERS];
        uint64_t *pIV[NUM_SUPPORTED_BUFFERS];
        uint32_t buffLenInBytes[NUM_SUPPORTED_BUFFERS];
        uint8_t *srcBuff[NUM_SUPPORTED_BUFFERS] = {NULL};
        uint8_t *refBuff[NUM_SUPPORTED_BUFFERS] = {NULL};
        uint8_t *key[NUM_SUPPORTED_BUFFERS] = {NULL};
        uint32_t bitLens[NUM_SUPPORTED_BUFFERS];
        uint32_t bitOffsets[NUM_SUPPORTED_BUFFERS];

        int i = 0, j = 0;
        int ret = -1;

        printf("Testing IMB_KASUMI_F8_N_BUFFER (%s):\n",
               job_api ? "Job API" : "Direct API");

        /* Allocate memory for the buffers fill them with data */
        for (i = 0; i < NUM_SUPPORTED_BUFFERS; i++) {
                bitOffsets[i] = 0;

                key[i] = malloc(IMB_KASUMI_KEY_SIZE);
                if (!key[i]) {
                        printf("malloc(key[%u]:failed !\n", i);
                        goto exit;
                }

                pKeySched[i] = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
                if (!pKeySched[i]) {
                        printf("malloc(pKeySched[%u]:failed !\n", i);
                        goto exit;
                }

                srcBuff[i] = malloc(MAX_DATA_LEN);
                if (!srcBuff[i]) {
                        printf("malloc(srcBuff[%u]:failed !\n", i);
                        goto exit;
                }
                refBuff[i] = malloc(MAX_DATA_LEN);
                if (!refBuff[i]) {
                        printf("malloc(refBuff[%u]:failed !\n", i);
                        goto exit;
                }

                memset(key[i], 0xAA, IMB_KASUMI_KEY_SIZE);
                if (IMB_KASUMI_INIT_F8_KEY_SCHED(mgr, key[i], pKeySched[i])) {
                        printf("IMB_KASUMI_INIT_F8_KEY_SCHED() error\n");
                        goto exit;
                }

                IV[i] = (uint64_t)i;
                pIV[i] = &IV[i];
        }

        /* Testing multiple buffers of equal size */
        for (i = 0; i < NUM_SUPPORTED_BUFFERS; i++) {
                /* Testing Buffer sizes for 128 */
                buffLenInBytes[i] = 128;

                for (j = 0; j <= i; j++) {
                        bitLens[j] = buffLenInBytes[i] * 8;

                        /* Reset input buffers with test data */
                        memset(srcBuff[j], i, buffLenInBytes[i]);
                        memset(refBuff[j], i, buffLenInBytes[i]);
                }

                /* Test the encrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, pKeySched,
                                              (uint64_t **)pIV,
                                              (uint8_t **)srcBuff,
                                              (uint8_t **)srcBuff,
                                              (uint32_t *)bitLens,
                                              (uint32_t *)bitOffsets,
                                              IMB_DIR_ENCRYPT, i + 1);
                else
                        /* All buffers share the same key */
                        IMB_KASUMI_F8_N_BUFFER(mgr, pKeySched[i], IV,
                                               (const void * const *)srcBuff,
                                               (void **)srcBuff,
                                               buffLenInBytes, i + 1);
                if (srcBuff[i] == NULL) {
                        printf("N buffer failure\n");
                        goto exit;
                }

                /* Test the Decrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, pKeySched,
                                              (uint64_t **)pIV,
                                              (uint8_t **)srcBuff,
                                              (uint8_t **)srcBuff,
                                              (uint32_t *)bitLens,
                                              (uint32_t *)bitOffsets,
                                              IMB_DIR_DECRYPT, i + 1);
                else
                        /* All buffers share the same key */
                        IMB_KASUMI_F8_N_BUFFER(mgr, pKeySched[i], IV,
                                               (const void * const *)srcBuff,
                                               (void **)srcBuff,
                                               buffLenInBytes, i + 1);
                if (srcBuff[i] == NULL) {
                        printf("N buffer failure\n");
                        goto exit;
                }

                for (j = 0; j <= i; j++) {
                        if (memcmp(srcBuff[j], refBuff[j],
                                   buffLenInBytes[j]) != 0) {
                                printf("kasumi_f8_n_buffer equal sizes, "
                                       "numBuffs:%d\n", i + 1);
                                hexdump(stdout, "Actual:", srcBuff[j],
                                        buffLenInBytes[j]);
                                hexdump(stdout, "Expected:", refBuff[j],
                                        buffLenInBytes[j]);
                                goto exit;
                        }
                }
        }
        printf("[%s]: PASS, 1 to %d buffers of equal size.\n", __FUNCTION__, i);

        /* Testing multiple buffers of increasing size */
        for (i = 0; i < NUM_SUPPORTED_BUFFERS; i++) {

                /* Testing different Buffer sizes*/
                buffLenInBytes[i] = i + 131 * 8;

                for (j = 0; j <= i; j++) {
                        bitLens[j] = buffLenInBytes[i] * 8;

                        /* Reset input buffers with test data */
                        memset(srcBuff[j], i, buffLenInBytes[i]);
                        memset(refBuff[j], i, buffLenInBytes[i]);
                }

                /* Test the encrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, pKeySched,
                                              (uint64_t **)pIV,
                                              (uint8_t **)srcBuff,
                                              (uint8_t **)srcBuff,
                                              (uint32_t *)bitLens,
                                              (uint32_t *)bitOffsets,
                                              IMB_DIR_DECRYPT, i + 1);
                else
                        /* All buffers share the same key */
                        IMB_KASUMI_F8_N_BUFFER(mgr, pKeySched[i], IV,
                                               (const void * const *)srcBuff,
                                               (void **)srcBuff,
                                               buffLenInBytes, i + 1);
                if (srcBuff[i] == NULL) {
                        printf("N buffer failure\n");
                        goto exit;
                }

                /* Test the Decrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, pKeySched,
                                              (uint64_t **)pIV,
                                              (uint8_t **)srcBuff,
                                              (uint8_t **)srcBuff,
                                              (uint32_t *)bitLens,
                                              (uint32_t *)bitOffsets,
                                              IMB_DIR_DECRYPT, i + 1);
                else
                        /* All buffers share the same key */
                        IMB_KASUMI_F8_N_BUFFER(mgr, pKeySched[i], IV,
                                               (const void * const *)srcBuff,
                                               (void **)srcBuff,
                                               buffLenInBytes, i + 1);
                if (srcBuff[i] == NULL) {
                        printf("N buffer failure\n");
                        goto exit;
                }

                for (j = 0; j <= i; j++) {
                        if (memcmp(srcBuff[j], refBuff[j],
                                   buffLenInBytes[j]) != 0) {
                                printf("kasumi_f8_n_buffer increasing sizes, "
                                       "numBuffs:%d\n", i + 1);
                                hexdump(stdout, "Actual:", srcBuff[j],
                                        buffLenInBytes[j]);
                                hexdump(stdout, "Expected:", refBuff[j],
                                        buffLenInBytes[j]);
                                goto exit;
                        }
                }
        }

        printf("[%s]: PASS, 1 to %d buffers of increasing size.\n",
               __FUNCTION__, i);

        /* Testing multiple buffers of decreasing size */
        for (i = 0; i < NUM_SUPPORTED_BUFFERS; i++) {

                /* Testing Buffer sizes from 3048 to 190 */
                buffLenInBytes[i] = MAX_DATA_LEN / (1 + i);

                for (j = 0; j <= i; j++) {
                        bitLens[j] = buffLenInBytes[i] * 8;

                        /* Reset input buffers with test data */
                        memset(srcBuff[j], i, buffLenInBytes[i]);
                        memset(refBuff[j], i, buffLenInBytes[i]);
                }

                /* Test the encrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, pKeySched,
                                              (uint64_t **)pIV,
                                              (uint8_t **)srcBuff,
                                              (uint8_t **)srcBuff,
                                              (uint32_t *)bitLens,
                                              (uint32_t *)bitOffsets,
                                              IMB_DIR_DECRYPT, i + 1);
                else
                        IMB_KASUMI_F8_N_BUFFER(mgr, pKeySched[i], IV,
                                               (const void * const *)srcBuff,
                                               (void **)srcBuff,
                                               buffLenInBytes, i + 1);
                if (srcBuff[i] == NULL) {
                        printf("N buffer failure\n");
                        goto exit;
                }

                /* Test the Decrypt */
                if (job_api)
                        submit_kasumi_f8_jobs(mgr, pKeySched,
                                              (uint64_t **)pIV,
                                              (uint8_t **)srcBuff,
                                              (uint8_t **)srcBuff,
                                              (uint32_t *)bitLens,
                                              (uint32_t *)bitOffsets,
                                              IMB_DIR_DECRYPT, i + 1);
                else
                        IMB_KASUMI_F8_N_BUFFER(mgr, pKeySched[i], IV,
                                               (const void * const *)srcBuff,
                                               (void **)srcBuff,
                                               buffLenInBytes, i + 1);
                if (srcBuff[i] == NULL) {
                        printf("N buffer failure\n");
                        goto exit;
                }

                for (j = 0; j <= i; j++) {
                        if (memcmp(srcBuff[j], refBuff[j],
                                   buffLenInBytes[j]) != 0) {
                                printf("kasumi_f8_n_buffer decreasing sizes, "
                                       "numBuffs:%d\n", i + 1);
                                hexdump(stdout, "Actual:", srcBuff[j],
                                        buffLenInBytes[j]);
                                hexdump(stdout, "Expected:", refBuff[j],
                                        buffLenInBytes[j]);
                                goto exit;
                        }
                }
        }

        ret = 0;
        printf("[%s]: PASS, 1 to %d buffers of decreasing size.\n",
               __FUNCTION__, i);
exit:
        /* free up test buffers */
        for (i = 0; i < NUM_SUPPORTED_BUFFERS; i++) {
                free(key[i]);
                free(pKeySched[i]);
                free(srcBuff[i]);
                free(refBuff[i]);
        }

        return ret;
}

static int validate_kasumi_f9(IMB_MGR *mgr, const unsigned job_api)
{
        kasumi_key_sched_t *pKeySched = NULL;
        uint8_t *pKey = NULL;
        int keyLen = 16;
        uint8_t srcBuff[MAX_DATA_LEN];
        uint8_t digest[IMB_KASUMI_DIGEST_SIZE];
        int numKasumiF9TestVectors, i;
        hash_test_vector_t *kasumiF9_test_vectors = NULL;
        int ret = 1;

        printf("Testing IMB_KASUMI_F9_1_BUFFER (%s):\n",
               job_api ? "Job API" : "Direct API");

        kasumiF9_test_vectors = kasumi_f9_vectors;
        numKasumiF9TestVectors = numHashTestVectors[0];

        if (!numKasumiF9TestVectors) {
                printf("No Kasumi vectors found !\n");
                goto exit;
        }
        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pkey):failed!\n");
                goto exit;
        }

        pKeySched = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
        if (!pKeySched) {
                printf("malloc (IMB_KASUMI_KEY_SCHED_SIZE()): failed !\n");
                goto exit;
        }

        /* Create the test Data */
        for (i = 0; i < numKasumiF9TestVectors; i++) {
                uint32_t byteLen = kasumiF9_test_vectors[i].lengthInBytes;

                memcpy(pKey, kasumiF9_test_vectors[i].key,
                       kasumiF9_test_vectors[i].keyLenInBytes);

                memcpy(srcBuff, kasumiF9_test_vectors[i].input, byteLen);

                memcpy(digest, kasumiF9_test_vectors[i].exp_out,
                       IMB_KASUMI_DIGEST_SIZE);

                if (IMB_KASUMI_INIT_F9_KEY_SCHED(mgr, pKey, pKeySched)) {
                        printf("IMB_KASUMI_INIT_F9_KEY_SCHED()error\n");
                        goto exit;
                }

                /* Test F9 integrity */
                if (job_api)
                        submit_kasumi_f9_job(mgr, pKeySched, srcBuff,
                                             digest, byteLen);
                else
                        IMB_KASUMI_F9_1_BUFFER(mgr, pKeySched, srcBuff,
                                               byteLen, digest);

                /* Compare the digest with the expected in the vectors */
                if (memcmp(digest, kasumiF9_test_vectors[i].exp_out,
                           IMB_KASUMI_DIGEST_SIZE) != 0) {
                        hexdump(stdout, "Actual", digest,
                                IMB_KASUMI_DIGEST_SIZE);
                        hexdump(stdout, "Expected",
                                kasumiF9_test_vectors[i].exp_out,
                                IMB_KASUMI_DIGEST_SIZE);
                        printf("F9 integrity %d Failed\n", i);
                        goto exit;
                }
        }

        ret = 0;
        printf("[%s]: PASS, for %d single buffers.\n", __FUNCTION__,
               numKasumiF9TestVectors);
exit:
        free(pKey);
        free(pKeySched);
        return ret;
}

static int validate_kasumi_f9_user(IMB_MGR *mgr, const unsigned job_api)
{
        int numKasumiF9IV_TestVectors = 0, i = 0;
        hash_iv_test_vector_t *kasumiF9_vectors = NULL;

        kasumiF9_vectors = kasumi_f9_IV_vectors;
        numKasumiF9IV_TestVectors = numHashTestVectors[1];

        kasumi_key_sched_t *pKeySched = NULL;
        uint8_t *pKey = NULL;
        int keyLen = MAX_KEY_LEN;

        uint64_t iv[MAX_IV_LEN];
        uint8_t srcBuff[MAX_DATA_LEN];
        uint8_t digest[IMB_KASUMI_DIGEST_SIZE];
        uint32_t direction;
        int ret = 1;

        (void)job_api; /* unused parameter */

        if (!numKasumiF9IV_TestVectors) {
                printf("No Kasumi vectors found !\n");
                goto exit;
        }

        pKey = malloc(keyLen);
        if (!pKey) {
                printf("malloc(pkey):failed!\n");
                goto exit;
        }

        pKeySched = malloc(IMB_KASUMI_KEY_SCHED_SIZE(mgr));
        if (!pKeySched) {
                printf("malloc (IMB_KASUMI_KEY_SCHED_SIZE()): failed !\n");
                goto exit;
        }

        /* Create the test data */
        for (i = 0; i < numKasumiF9IV_TestVectors; i++) {
                memcpy(pKey, kasumiF9_vectors[i].key,
                       kasumiF9_vectors[i].keyLenInBytes);

                memcpy(srcBuff, kasumiF9_vectors[i].input,
                       (kasumiF9_vectors[i].lengthInBits + 7 / CHAR_BIT));

                memcpy(iv, kasumiF9_vectors[i].iv,
                       kasumiF9_vectors[i].ivLenInBytes);

                direction = kasumiF9_vectors[i].direction;

                /* Only 1 key sched is used */
                if (IMB_KASUMI_INIT_F9_KEY_SCHED(mgr, pKey, pKeySched)) {
                        printf("IMB_KASUMI_INIT_F9_KEY_SCHED() error\n");
                        goto exit;
                }
                /* Test the integrity for f9_user with IV */
                IMB_KASUMI_F9_1_BUFFER_USER(mgr, pKeySched, iv[0], srcBuff,
                                            kasumiF9_vectors[i].lengthInBits,
                                            digest, direction);

                /* Compare the digest with the expected in the vectors */
                if (memcmp(digest, kasumiF9_vectors[i].exp_out,
                           IMB_KASUMI_DIGEST_SIZE) != 0) {
                        hexdump(stdout, "digest", digest,
                                IMB_KASUMI_DIGEST_SIZE);
                        hexdump(stdout, "exp_out", kasumiF9_vectors[i].exp_out,
                                IMB_KASUMI_DIGEST_SIZE);
                        printf("direction %d\n", direction);
                        printf("F9 integrity %d Failed\n", i);
                        goto exit;
                }
        }

        ret = 0;
        printf("[%s]:     PASS, for %d single buffers.\n", __FUNCTION__, i);
exit:
        free(pKey);
        free(pKeySched);
        return ret;
}

int kasumi_test(struct IMB_MGR *mb_mgr)
{
        struct test_suite_context ts;
        int errors = 0;
        unsigned i;

        test_suite_start(&ts, "KASUMI-F8");
        for (i = 0; i < DIM(kasumi_f8_func_tab); i++) {
                /* validate direct api */
                if (kasumi_f8_func_tab[i].func(mb_mgr, 0)) {
                        printf("%s: FAIL\n", kasumi_f8_func_tab[i].func_name);
                        test_suite_update(&ts, 0, 1);
                } else {
                        test_suite_update(&ts, 1, 0);
                }

                /* validate job api */
                if (kasumi_f8_func_tab[i].func(mb_mgr, 1)) {
                        printf("%s: FAIL\n", kasumi_f8_func_tab[i].func_name);
                        test_suite_update(&ts, 0, 1);
                } else {
                        test_suite_update(&ts, 1, 0);
                }
        }
        errors += test_suite_end(&ts);

        test_suite_start(&ts, "KASUMI-F9");
        for (i = 0; i < DIM(kasumi_f9_func_tab); i++) {
                /* validate direct api */
                if (kasumi_f9_func_tab[i].func(mb_mgr, 0)) {
                        printf("%s: FAIL\n", kasumi_f9_func_tab[i].func_name);
                        test_suite_update(&ts, 0, 1);
                } else {
                        test_suite_update(&ts, 1, 0);
                }

                /* validate job api */
                if (kasumi_f9_func_tab[i].func(mb_mgr, 1)) {
                        printf("%s: FAIL\n", kasumi_f9_func_tab[i].func_name);
                        test_suite_update(&ts, 0, 1);
                } else {
                        test_suite_update(&ts, 1, 0);
                }
        }
        errors += test_suite_end(&ts);

        return errors;
}
