/*******************************************************************************
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
*******************************************************************************/

/**
 * DOCSIS AES (AES CBC + AES CFB, for 128 and 256-bit keys)
 * and DOCSIS DES (DES CBC + DES CFB).
 * JOB submit and flush helper functions to be used from mb_mgr_code.h
 *
 * @note These need to be defined prior to including this file:
 *           ETHERNET_FCS, AES_CFB_ONE, SUBMIT_JOB_AES128_DEC and
 *           SUBMIT_JOB_AES128_ENC, SUBMIT_JOB_AES256_DEC and
 *           SUBMIT_JOB_AES256_DEC.
 *
 * @note The file defines the following:
 *           DOCSIS_LAST_BLOCK, DOCSIS_FIRST_BLOCK,
 *           SUBMIT_JOB_DOCSIS_SEC_ENC, FLUSH_JOB_DOCSIS_SEC_ENC,
 *           SUBMIT_JOB_DOCSIS_SEC_DEC,
 *           SUBMIT_JOB_DOCSIS_SEC_CRC_ENC, FLUSH_JOB_DOCSIS_SEC_CRC_ENC,
 *           SUBMIT_JOB_DOCSIS_SEC_CRC_DEC,
 *           DOCSIS_DES_ENC and DOCSIS_DES_DEC.
 */

#ifndef DOCSIS_COMMON_H
#define DOCSIS_COMMON_H

#include <stdint.h>
#include "include/des.h"

/* ========================================================================= */
/* DOCSIS SEC BPI / AES  (AES128-CBC + AES128-CFB) */
/* ========================================================================= */

#ifndef IMB_AES_BLOCK_SIZE
#define IMB_AES_BLOCK_SIZE 16
#endif

IMB_DLL_LOCAL void aes_cfb_256_one_sse_no_aesni(void *out, const void *in,
                                                const void *iv,
                                                const void *keys,
                                                const uint64_t len);

IMB_DLL_LOCAL void aes_cfb_256_one_sse(void *out, const void *in,
                                       const void *iv, const void *keys,
                                       const uint64_t len);

IMB_DLL_LOCAL void aes_cfb_256_one_avx(void *out, const void *in,
                                       const void *iv, const void *keys,
                                       const uint64_t len);

IMB_DLL_LOCAL void aes_cfb_256_one_avx2(void *out, const void *in,
                                       const void *iv, const void *keys,
                                       const uint64_t len);

IMB_DLL_LOCAL void aes_cfb_256_one_avx512(void *out, const void *in,
                                          const void *iv, const void *keys,
                                          const uint64_t len);

/**
 * @brief Encrypts/decrypts the last partial block for DOCSIS SEC v3.1 BPI
 *
 * The last partial block is encrypted/decrypted using AES CFB128.
 * IV is always the last complete cipher-text block.
 *
 * @note It is assumed that length is bigger than one AES 128 block.
 *
 * @param job description of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
IMB_JOB *
DOCSIS_LAST_BLOCK(IMB_JOB *job, const uint64_t key_size)
{
        const void *iv = NULL;
        uint64_t offset = 0;
        uint64_t partial_bytes = 0;

        if (job == NULL)
                return job;

        IMB_ASSERT((job->cipher_direction == IMB_DIR_DECRYPT) ||
                   (job->status & IMB_STATUS_COMPLETED_CIPHER));

        partial_bytes = job->msg_len_to_cipher_in_bytes &
                        (IMB_AES_BLOCK_SIZE - 1);
        offset = job->msg_len_to_cipher_in_bytes & (~(IMB_AES_BLOCK_SIZE - 1));

        if (!partial_bytes)
                return job;

        /* in either case IV has to be the last cipher-text block */
        if (job->cipher_direction == IMB_DIR_ENCRYPT)
                iv = job->dst + offset - IMB_AES_BLOCK_SIZE;
        else
                iv = job->src + job->cipher_start_src_offset_in_bytes +
                        offset - IMB_AES_BLOCK_SIZE;

        IMB_ASSERT(partial_bytes <= IMB_AES_BLOCK_SIZE);
        if (key_size == 16)
                AES_CFB_128_ONE(job->dst + offset,
                            job->src + job->cipher_start_src_offset_in_bytes +
                            offset,
                            iv, job->enc_keys, partial_bytes);
        else /* 32 */
                AES_CFB_256_ONE(job->dst + offset,
                            job->src + job->cipher_start_src_offset_in_bytes +
                            offset,
                            iv, job->enc_keys, partial_bytes);

        return job;
}

/**
 * @brief Encrypts/decrypts the first and only partial block for
 *        DOCSIS SEC v3.1 BPI
 *
 * The first partial block is encrypted/decrypted using AES CFB128.
 *
 * @param job description of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
IMB_JOB *
DOCSIS_FIRST_BLOCK(IMB_JOB *job, const uint64_t key_size)
{
        IMB_ASSERT(!(job->status & IMB_STATUS_COMPLETED_CIPHER));
        IMB_ASSERT(job->msg_len_to_cipher_in_bytes <= IMB_AES_BLOCK_SIZE);
        if (key_size == 16)
                AES_CFB_128_ONE(job->dst,
                            job->src + job->cipher_start_src_offset_in_bytes,
                            job->iv, job->enc_keys,
                            job->msg_len_to_cipher_in_bytes);
        else /* 32 */
                AES_CFB_256_ONE(job->dst,
                            job->src + job->cipher_start_src_offset_in_bytes,
                            job->iv, job->enc_keys,
                            job->msg_len_to_cipher_in_bytes);

        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

/**
 * @brief JOB submit helper function for DOCSIS SEC encryption
 *
 * @param state OOO manager structure
 * @param job description of performed crypto operation
 *
 * @return Pointer to completed JOB or NULL
 */
__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS_SEC_ENC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job,
                          const uint64_t key_size)
{
        IMB_JOB *tmp;

        if (key_size == 16) {
                if (job->msg_len_to_cipher_in_bytes >= IMB_AES_BLOCK_SIZE) {
                        tmp = SUBMIT_JOB_AES128_ENC((MB_MGR_AES_OOO *)state,
                                                    job);

                        return DOCSIS_LAST_BLOCK(tmp, 16);
                } else
                        return DOCSIS_FIRST_BLOCK(job, 16);
        } else { /* Key length = 32 */
                if (job->msg_len_to_cipher_in_bytes >= IMB_AES_BLOCK_SIZE) {
                        tmp = SUBMIT_JOB_AES256_ENC((MB_MGR_AES_OOO *)state,
                                                    job);

                        return DOCSIS_LAST_BLOCK(tmp, 32);
                } else
                        return DOCSIS_FIRST_BLOCK(job, 32);
        }
}

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS128_SEC_ENC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_ENC(state, job, 16);
}

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS256_SEC_ENC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_ENC(state, job, 32);
}

/**
 * @brief JOB flush helper function for DOCSIS SEC encryption
 *
 * @param state OOO manager structure
 *
 * @return Pointer to completed JOB or NULL
 */
__forceinline
IMB_JOB *
FLUSH_JOB_DOCSIS_SEC_ENC(MB_MGR_DOCSIS_AES_OOO *state, const uint64_t key_size)
{
        IMB_JOB *tmp;

        if (key_size == 16) {
                tmp = FLUSH_JOB_AES128_ENC((MB_MGR_AES_OOO *)state);

                return DOCSIS_LAST_BLOCK(tmp, 16);
        } else { /* 32 */
                tmp = FLUSH_JOB_AES256_ENC((MB_MGR_AES_OOO *)state);

                return DOCSIS_LAST_BLOCK(tmp, 32);
        }
}

__forceinline
IMB_JOB *
FLUSH_JOB_DOCSIS128_SEC_ENC(MB_MGR_DOCSIS_AES_OOO *state)
{
        return FLUSH_JOB_DOCSIS_SEC_ENC(state, 16);
}
__forceinline
IMB_JOB *
FLUSH_JOB_DOCSIS256_SEC_ENC(MB_MGR_DOCSIS_AES_OOO *state)
{
        return FLUSH_JOB_DOCSIS_SEC_ENC(state, 32);
}

/**
 * @brief JOB submit helper function for DOCSIS SEC decryption
 *
 * @param state OOO manager structure (unused here)
 * @param job description of performed crypto operation
 *
 * @return Pointer to completed JOB or NULL
 */
__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS_SEC_DEC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job,
                          const uint64_t key_size)
{
        (void) state;

        if (key_size == 16) {
                if (job->msg_len_to_cipher_in_bytes >= IMB_AES_BLOCK_SIZE) {
                        DOCSIS_LAST_BLOCK(job, 16);
                        return SUBMIT_JOB_AES128_DEC(job);
                } else
                        return DOCSIS_FIRST_BLOCK(job, 16);
        } else { /* 32 */
                if (job->msg_len_to_cipher_in_bytes >= IMB_AES_BLOCK_SIZE) {
                        DOCSIS_LAST_BLOCK(job, 32);
                        return SUBMIT_JOB_AES256_DEC(job);
                } else
                        return DOCSIS_FIRST_BLOCK(job, 32);
        }
}

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS128_SEC_DEC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_DEC(state, job, 16);
}

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS256_SEC_DEC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_DEC(state, job, 32);
}

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS_SEC_CRC_ENC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job,
                              const uint64_t key_size)
{
        if (job->msg_len_to_hash_in_bytes >=
            IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE) {
                uint32_t *p_crc = (uint32_t *) job->auth_tag_output;

                (*p_crc) =
                        ETHERNET_FCS(job->src +
                                     job->hash_start_src_offset_in_bytes,
                                     job->msg_len_to_hash_in_bytes,
                                     job->src +
                                     job->hash_start_src_offset_in_bytes +
                                     job->msg_len_to_hash_in_bytes);
        }
        return SUBMIT_JOB_DOCSIS_SEC_ENC(state, job, key_size);
}

#ifndef AVX512
__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS128_SEC_CRC_ENC(MB_MGR_DOCSIS_AES_OOO *state,
                                 IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_CRC_ENC(state, job, 16);
}

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS256_SEC_CRC_ENC(MB_MGR_DOCSIS_AES_OOO *state,
                                 IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_CRC_ENC(state, job, 32);
}
#endif

__forceinline
IMB_JOB *
FLUSH_JOB_DOCSIS_SEC_CRC_ENC(MB_MGR_DOCSIS_AES_OOO *state,
                             const uint64_t key_size)
{
        /**
         * CRC has been already calculated.
         * Normal cipher flush only required.
         */
        return FLUSH_JOB_DOCSIS_SEC_ENC(state, key_size);
}

#ifndef AVX512
__forceinline
IMB_JOB *
FLUSH_JOB_DOCSIS128_SEC_CRC_ENC(MB_MGR_DOCSIS_AES_OOO *state)
{
        return FLUSH_JOB_DOCSIS_SEC_CRC_ENC(state, 16);
}
__forceinline
IMB_JOB *
FLUSH_JOB_DOCSIS256_SEC_CRC_ENC(MB_MGR_DOCSIS_AES_OOO *state)
{
        return FLUSH_JOB_DOCSIS_SEC_CRC_ENC(state, 32);
}
#endif

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS_SEC_CRC_DEC(MB_MGR_DOCSIS_AES_OOO *state, IMB_JOB *job,
                              const uint64_t key_size)
{
        (void) state;

        if (job->msg_len_to_cipher_in_bytes >= IMB_AES_BLOCK_SIZE) {
                DOCSIS_LAST_BLOCK(job, key_size);
                if (key_size == 16)
                        job = SUBMIT_JOB_AES128_DEC(job);
                else /* 32 */
                        job = SUBMIT_JOB_AES256_DEC(job);
        } else {
                job = DOCSIS_FIRST_BLOCK(job, key_size);
        }

        if (job->msg_len_to_hash_in_bytes >=
            IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE) {
                uint32_t *p_crc = (uint32_t *) job->auth_tag_output;

                (*p_crc) =
                        ETHERNET_FCS(job->src +
                                     job->hash_start_src_offset_in_bytes,
                                     job->msg_len_to_hash_in_bytes,
                                     NULL);
        }

        return job;
}

#ifndef AVX512
__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS128_SEC_CRC_DEC(MB_MGR_DOCSIS_AES_OOO *state,
                                 IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_CRC_DEC(state, job, 16);
}

__forceinline
IMB_JOB *
SUBMIT_JOB_DOCSIS256_SEC_CRC_DEC(MB_MGR_DOCSIS_AES_OOO *state,
                                 IMB_JOB *job)
{
        return SUBMIT_JOB_DOCSIS_SEC_CRC_DEC(state, job, 32);
}
#endif

/* ========================================================================= */
/* DES, 3DES and DOCSIS DES (DES CBC + DES CFB) */
/* ========================================================================= */

/**
 * @brief DOCSIS DES cipher encryption
 *
 * @param job description of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
IMB_JOB *
DOCSIS_DES_ENC(IMB_JOB *job)
{
        IMB_ASSERT(!(job->status & IMB_STATUS_COMPLETED_CIPHER));
        docsis_des_enc_basic(job->src + job->cipher_start_src_offset_in_bytes,
                             job->dst,
                             (int) job->msg_len_to_cipher_in_bytes,
                             job->enc_keys,
                             (const uint64_t *)job->iv);
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

/**
 * @brief DOCSIS DES cipher decryption
 *
 * @param job description of performed crypto operation
 * @return It always returns value passed in \a job
 */
__forceinline
IMB_JOB *
DOCSIS_DES_DEC(IMB_JOB *job)
{
        IMB_ASSERT(!(job->status & IMB_STATUS_COMPLETED_CIPHER));
        docsis_des_dec_basic(job->src + job->cipher_start_src_offset_in_bytes,
                             job->dst,
                             (int) job->msg_len_to_cipher_in_bytes,
                             job->dec_keys,
                             (const uint64_t *)job->iv);
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

#endif /* DOCSIS_COMMON_H */
