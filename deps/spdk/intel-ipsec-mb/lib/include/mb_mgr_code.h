/*******************************************************************************
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
*******************************************************************************/

#ifndef MB_MGR_CODE_H
#define MB_MGR_CODE_H

/*
 * This contains the bulk of the mb_mgr code, with #define's to build
 * an SSE, AVX, AVX2 or AVX512 version (see mb_mgr_sse.c, mb_mgr_avx.c, etc.)
 *
 * get_next_job() returns a job object. This must be filled in and returned
 * via submit_job() before get_next_job() is called again.
 *
 * submit_job() and flush_job() returns a job object. This job object ceases
 * to be usable at the next call to get_next_job()
 */

#include <string.h> /* memcpy(), memset() */

#include "include/clear_regs_mem.h"
#include "include/des.h"
#include "intel-ipsec-mb.h"
#include "error.h"
#include "include/snow3g_submit.h"

#ifdef LINUX
#define BSWAP64 __builtin_bswap64
#else
#define BSWAP64 _byteswap_uint64
#endif

#define CRC(func, state, job) *((uint32_t *)job->auth_tag_output) = \
                func(state, job->src + job->hash_start_src_offset_in_bytes, \
                     job->msg_len_to_hash_in_bytes)
/*
 * JOBS() and ADV_JOBS() moved into mb_mgr_code.h
 * get_next_job() and get_completed_job() API's are no longer inlines.
 * For binary compatibility they have been made proper symbols.
 */
__forceinline
IMB_JOB *JOBS(IMB_MGR *state, const int offset)
{
        char *cp = (char *)state->jobs;

        return (IMB_JOB *)(cp + offset);
}

__forceinline
void ADV_JOBS(int *ptr)
{
        *ptr += sizeof(IMB_JOB);
        if (*ptr >= (int) (IMB_MAX_JOBS * sizeof(IMB_JOB)))
                *ptr = 0;
}

/* ========================================================================= */
/* Lower level "out of order" schedulers */
/* ========================================================================= */

__forceinline
IMB_JOB *
SUBMIT_JOB_AES128_DEC(IMB_JOB *job)
{
        AES_CBC_DEC_128(job->src + job->cipher_start_src_offset_in_bytes,
                        job->iv,
                        job->dec_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES192_DEC(IMB_JOB *job)
{
        AES_CBC_DEC_192(job->src + job->cipher_start_src_offset_in_bytes,
                        job->iv,
                        job->dec_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes);
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES256_DEC(IMB_JOB *job)
{
        AES_CBC_DEC_256(job->src + job->cipher_start_src_offset_in_bytes,
                        job->iv,
                        job->dec_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES_ECB_128_ENC(IMB_JOB *job)
{
        AES_ECB_ENC_128(job->src + job->cipher_start_src_offset_in_bytes,
                        job->enc_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES_ECB_192_ENC(IMB_JOB *job)
{
        AES_ECB_ENC_192(job->src + job->cipher_start_src_offset_in_bytes,
                        job->enc_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES_ECB_256_ENC(IMB_JOB *job)
{
        AES_ECB_ENC_256(job->src + job->cipher_start_src_offset_in_bytes,
                        job->enc_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES_ECB_128_DEC(IMB_JOB *job)
{
        AES_ECB_DEC_128(job->src + job->cipher_start_src_offset_in_bytes,
                        job->dec_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES_ECB_192_DEC(IMB_JOB *job)
{
        AES_ECB_DEC_192(job->src + job->cipher_start_src_offset_in_bytes,
                        job->dec_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES_ECB_256_DEC(IMB_JOB *job)
{
        AES_ECB_DEC_256(job->src + job->cipher_start_src_offset_in_bytes,
                        job->dec_keys,
                        job->dst,
                        job->msg_len_to_cipher_in_bytes & (~15));
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES128_CBCS_1_9_DEC(IMB_JOB *job)
{
        AES_CBCS_1_9_DEC_128(job->src + job->cipher_start_src_offset_in_bytes,
                             job->iv,
                             job->dec_keys,
                             job->dst,
                             job->msg_len_to_cipher_in_bytes & (~15),
                             job->cipher_fields.CBCS.next_iv);
        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

/* ========================================================================= */
/* DOCSIS functions */
/* ========================================================================= */

#include "include/docsis_common.h"

/* ========================================================================= */
/* Custom hash / cipher */
/* ========================================================================= */

__forceinline
IMB_JOB *
JOB_CUSTOM_CIPHER(IMB_JOB *job)
{
        if (!(job->status & IMB_STATUS_COMPLETED_CIPHER)) {
                if (job->cipher_func(job))
                        job->status = IMB_STATUS_INTERNAL_ERROR;
                else
                        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        }
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_CUSTOM_CIPHER(IMB_JOB *job)
{
        return JOB_CUSTOM_CIPHER(job);
}

__forceinline
IMB_JOB *
FLUSH_JOB_CUSTOM_CIPHER(IMB_JOB *job)
{
        return JOB_CUSTOM_CIPHER(job);
}

__forceinline
IMB_JOB *
JOB_CUSTOM_HASH(IMB_JOB *job)
{
        if (!(job->status & IMB_STATUS_COMPLETED_AUTH)) {
                if (job->hash_func(job))
                        job->status = IMB_STATUS_INTERNAL_ERROR;
                else
                        job->status |= IMB_STATUS_COMPLETED_AUTH;
        }
        return job;
}

__forceinline
IMB_JOB *
SUBMIT_JOB_CUSTOM_HASH(IMB_JOB *job)
{
        return JOB_CUSTOM_HASH(job);
}

__forceinline
IMB_JOB *
FLUSH_JOB_CUSTOM_HASH(IMB_JOB *job)
{
        return JOB_CUSTOM_HASH(job);
}

__forceinline
IMB_JOB *
submit_kasumi_uea1_job(IMB_MGR *state, IMB_JOB *job)
{
        const kasumi_key_sched_t *key = job->enc_keys;
        const uint64_t iv = *(const uint64_t *)job->iv;
        const uint32_t msg_bitlen =
                        (const uint32_t)job->msg_len_to_cipher_in_bits;
        const uint32_t msg_bitoff =
                        (const uint32_t)job->cipher_start_src_offset_in_bits;

        /* Use bit length API if
         * - msg length is not a multiple of bytes
         * - bit offset is not a multiple of bytes
         */
        if ((msg_bitlen & 0x07) || (msg_bitoff & 0x07)) {
                IMB_KASUMI_F8_1_BUFFER_BIT(state, key, iv, job->src, job->dst,
                                           msg_bitlen, msg_bitoff);

        } else {
                const uint32_t msg_bytelen = msg_bitlen >> 3;
                const uint32_t msg_byteoff = msg_bitoff >> 3;
                const void *src = job->src + msg_byteoff;
                void *dst = job->dst + msg_byteoff;

                IMB_KASUMI_F8_1_BUFFER(state, key, iv, src, dst,
                                       msg_bytelen);
        }

        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

__forceinline
IMB_JOB *
submit_docsis_enc_job(IMB_MGR *state, IMB_JOB *job)
{
        if (16 == job->key_len_in_bytes) {
                if (job->hash_alg == IMB_AUTH_DOCSIS_CRC32) {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis128_crc32_sec_ooo;

                        return SUBMIT_JOB_DOCSIS128_SEC_CRC_ENC(p_ooo, job);
                } else {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis128_sec_ooo;

                        return SUBMIT_JOB_DOCSIS128_SEC_ENC(p_ooo, job);
                }
        } else { /* 32 */
                if (job->hash_alg == IMB_AUTH_DOCSIS_CRC32) {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis256_crc32_sec_ooo;

                        return SUBMIT_JOB_DOCSIS256_SEC_CRC_ENC(p_ooo, job);
                } else {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis256_sec_ooo;

                        return SUBMIT_JOB_DOCSIS256_SEC_ENC(p_ooo, job);
                }
        }
}

__forceinline
IMB_JOB *
flush_docsis_enc_job(IMB_MGR *state, IMB_JOB *job)
{
        if (16 == job->key_len_in_bytes) {
                if (job->hash_alg == IMB_AUTH_DOCSIS_CRC32) {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis128_crc32_sec_ooo;

                        return FLUSH_JOB_DOCSIS128_SEC_CRC_ENC(p_ooo);
                } else {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis128_sec_ooo;

                        return FLUSH_JOB_DOCSIS128_SEC_ENC(p_ooo);
                }
        } else { /* 32 */
                if (job->hash_alg == IMB_AUTH_DOCSIS_CRC32) {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis256_crc32_sec_ooo;

                        return FLUSH_JOB_DOCSIS256_SEC_CRC_ENC(p_ooo);
                } else {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis256_sec_ooo;

                        return FLUSH_JOB_DOCSIS256_SEC_ENC(p_ooo);
                }
        }
}

__forceinline
IMB_JOB *
submit_docsis_dec_job(IMB_MGR *state, IMB_JOB *job)
{
        if (16 == job->key_len_in_bytes) {
                if (job->hash_alg == IMB_AUTH_DOCSIS_CRC32) {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis128_crc32_sec_ooo;

                        return SUBMIT_JOB_DOCSIS128_SEC_CRC_DEC(p_ooo, job);
                } else {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis128_sec_ooo;

                        return SUBMIT_JOB_DOCSIS128_SEC_DEC(p_ooo, job);
                }
        } else { /* 32 */
                if (job->hash_alg == IMB_AUTH_DOCSIS_CRC32) {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis256_crc32_sec_ooo;

                        return SUBMIT_JOB_DOCSIS256_SEC_CRC_DEC(p_ooo, job);
                } else {
                        MB_MGR_DOCSIS_AES_OOO *p_ooo =
                                state->docsis256_sec_ooo;

                        return SUBMIT_JOB_DOCSIS256_SEC_DEC(p_ooo, job);
                }
        }
}

__forceinline
IMB_JOB *
submit_snow_v_aead_job(IMB_MGR *state, IMB_JOB *job)
{
        struct gcm_key_data gdata_key;
        imb_uint128_t *auth = (imb_uint128_t *) job->auth_tag_output;
        imb_uint128_t temp;
        imb_uint128_t hkey_endpad[2];

        temp.low = BSWAP64((job->u.SNOW_V_AEAD.aad_len_in_bytes << 3));
        temp.high = BSWAP64((job->msg_len_to_cipher_in_bytes << 3));

        /* if hkey_endpad[1].high == 0:
         *      SUBMIT_JOB_SNOW_V_AEAD does enc/decrypt operation
         *      and fills hkey_endpad with first 2 keystreams
         * else
         *      SUBMIT_JOB_SNOW_V_AEAD fills hkey_endpad with first
         *      2 keystreams (no operations on src vector are done)
         */
        if(job->cipher_direction == IMB_DIR_ENCRYPT)
                hkey_endpad[1].high = 0;
        else
                hkey_endpad[1].high = 1;

        job->u.SNOW_V_AEAD.reserved = hkey_endpad;
        job = SUBMIT_JOB_SNOW_V_AEAD(job);

        memset(auth, 0, sizeof(imb_uint128_t));

        /* GHASH key H */
        IMB_GHASH_PRE(state, (void *)hkey_endpad,  &gdata_key);

        /* push AAD into GHASH */
        IMB_GHASH(state, &gdata_key, job->u.SNOW_V_AEAD.aad,
                  job->u.SNOW_V_AEAD.aad_len_in_bytes,
                  (void *)auth, sizeof(imb_uint128_t));

        if (job->cipher_direction == IMB_DIR_ENCRYPT)
                IMB_GHASH(state, &gdata_key, job->dst,
                          job->msg_len_to_cipher_in_bytes,
                          (void *)auth, sizeof(imb_uint128_t));
        else
                IMB_GHASH(state, &gdata_key, job->src,
                          job->msg_len_to_cipher_in_bytes,
                          (void *)auth, sizeof(imb_uint128_t));

        IMB_GHASH(state, &gdata_key, (void *)&temp, sizeof(temp),
                  (void *)auth, sizeof(imb_uint128_t));

        /* The resulting AuthTag */
        auth->low = auth->low ^ hkey_endpad[1].low;
        auth->high = auth->high ^ hkey_endpad[1].high;

        if (job->cipher_direction == IMB_DIR_DECRYPT) {
                hkey_endpad[1].high = 0;
                job = SUBMIT_JOB_SNOW_V_AEAD(job);
        }
        return job;
}

__forceinline
IMB_JOB *
submit_gcm_sgl_enc(IMB_MGR *state, IMB_JOB *job)
{
        switch (job->key_len_in_bytes) {
        case IMB_KEY_128_BYTES:
                if (job->sgl_state == IMB_SGL_INIT)
                        IMB_AES128_GCM_INIT_VAR_IV(state, job->enc_keys,
                                                   job->u.GCM.ctx,
                                                   job->iv,
                                                   job->iv_len_in_bytes,
                                                   job->u.GCM.aad,
                                                   job->u.GCM.aad_len_in_bytes);
                else if (job->sgl_state == IMB_SGL_UPDATE)
                        IMB_AES128_GCM_ENC_UPDATE(state, job->enc_keys,
                                               job->u.GCM.ctx,
                                               job->dst, job->src,
                                               job->msg_len_to_cipher_in_bytes);
                else /* FINALIZE */
                        IMB_AES128_GCM_ENC_FINALIZE(state, job->enc_keys,
                                             job->u.GCM.ctx,
                                             job->auth_tag_output,
                                             job->auth_tag_output_len_in_bytes);
                break;
        case IMB_KEY_192_BYTES:
                if (job->sgl_state == IMB_SGL_INIT)
                        IMB_AES192_GCM_INIT_VAR_IV(state, job->enc_keys,
                                                   job->u.GCM.ctx,
                                                   job->iv,
                                                   job->iv_len_in_bytes,
                                                   job->u.GCM.aad,
                                                   job->u.GCM.aad_len_in_bytes);
                else if (job->sgl_state == IMB_SGL_UPDATE)
                        IMB_AES192_GCM_ENC_UPDATE(state, job->enc_keys,
                                               job->u.GCM.ctx,
                                               job->dst, job->src,
                                               job->msg_len_to_cipher_in_bytes);
                else /* FINALIZE */
                        IMB_AES192_GCM_ENC_FINALIZE(state, job->enc_keys,
                                             job->u.GCM.ctx,
                                             job->auth_tag_output,
                                             job->auth_tag_output_len_in_bytes);
                break;
        case IMB_KEY_256_BYTES:
        default:
                if (job->sgl_state == IMB_SGL_INIT)
                        IMB_AES256_GCM_INIT_VAR_IV(state, job->enc_keys,
                                                   job->u.GCM.ctx,
                                                   job->iv,
                                                   job->iv_len_in_bytes,
                                                   job->u.GCM.aad,
                                                   job->u.GCM.aad_len_in_bytes);
                else if (job->sgl_state == IMB_SGL_UPDATE)
                        IMB_AES256_GCM_ENC_UPDATE(state, job->enc_keys,
                                               job->u.GCM.ctx,
                                               job->dst, job->src,
                                               job->msg_len_to_cipher_in_bytes);
                else /* FINALIZE */
                        IMB_AES256_GCM_ENC_FINALIZE(state, job->enc_keys,
                                             job->u.GCM.ctx,
                                             job->auth_tag_output,
                                             job->auth_tag_output_len_in_bytes);
                break;
        }

        job->status |= IMB_STATUS_COMPLETED_CIPHER;

        return job;
}

__forceinline
IMB_JOB *
submit_gcm_sgl_dec(IMB_MGR *state, IMB_JOB *job)
{
        switch (job->key_len_in_bytes) {
        case IMB_KEY_128_BYTES:
                if (job->sgl_state == IMB_SGL_INIT)
                        IMB_AES128_GCM_INIT_VAR_IV(state, job->enc_keys,
                                                   job->u.GCM.ctx,
                                                   job->iv,
                                                   job->iv_len_in_bytes,
                                                   job->u.GCM.aad,
                                                   job->u.GCM.aad_len_in_bytes);
                else if (job->sgl_state == IMB_SGL_UPDATE)
                        IMB_AES128_GCM_DEC_UPDATE(state, job->enc_keys,
                                               job->u.GCM.ctx,
                                               job->dst, job->src,
                                               job->msg_len_to_cipher_in_bytes);
                else /* FINALIZE */
                        IMB_AES128_GCM_DEC_FINALIZE(state, job->enc_keys,
                                             job->u.GCM.ctx,
                                             job->auth_tag_output,
                                             job->auth_tag_output_len_in_bytes);
                break;
        case IMB_KEY_192_BYTES:
                if (job->sgl_state == IMB_SGL_INIT)
                        IMB_AES192_GCM_INIT_VAR_IV(state, job->enc_keys,
                                                   job->u.GCM.ctx,
                                                   job->iv,
                                                   job->iv_len_in_bytes,
                                                   job->u.GCM.aad,
                                                   job->u.GCM.aad_len_in_bytes);
                else if (job->sgl_state == IMB_SGL_UPDATE)
                        IMB_AES192_GCM_DEC_UPDATE(state, job->enc_keys,
                                               job->u.GCM.ctx,
                                               job->dst, job->src,
                                               job->msg_len_to_cipher_in_bytes);
                else /* FINALIZE */
                        IMB_AES192_GCM_DEC_FINALIZE(state, job->enc_keys,
                                             job->u.GCM.ctx,
                                             job->auth_tag_output,
                                             job->auth_tag_output_len_in_bytes);
                break;
        case IMB_KEY_256_BYTES:
        default:
                if (job->sgl_state == IMB_SGL_INIT)
                        IMB_AES256_GCM_INIT_VAR_IV(state, job->enc_keys,
                                                   job->u.GCM.ctx,
                                                   job->iv,
                                                   job->iv_len_in_bytes,
                                                   job->u.GCM.aad,
                                                   job->u.GCM.aad_len_in_bytes);
                else if (job->sgl_state == IMB_SGL_UPDATE)
                        IMB_AES256_GCM_DEC_UPDATE(state, job->enc_keys,
                                               job->u.GCM.ctx,
                                               job->dst, job->src,
                                               job->msg_len_to_cipher_in_bytes);
                else /* FINALIZE */
                        IMB_AES256_GCM_DEC_FINALIZE(state, job->enc_keys,
                                             job->u.GCM.ctx,
                                             job->auth_tag_output,
                                             job->auth_tag_output_len_in_bytes);
                break;
        }

        job->status |= IMB_STATUS_COMPLETED_CIPHER;

        return job;
}

/* ========================================================================= */
/* Cipher submit & flush functions */
/* ========================================================================= */
__forceinline
IMB_JOB *
SUBMIT_JOB_AES_ENC(IMB_MGR *state, IMB_JOB *job)
{
        if (IMB_CIPHER_CBC == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        MB_MGR_AES_OOO *aes128_ooo = state->aes128_ooo;

                        return SUBMIT_JOB_AES128_ENC(aes128_ooo, job);
                } else if (24 == job->key_len_in_bytes) {
                        MB_MGR_AES_OOO *aes192_ooo = state->aes192_ooo;

                        return SUBMIT_JOB_AES192_ENC(aes192_ooo, job);
                } else { /* assume 32 */
                        MB_MGR_AES_OOO *aes256_ooo = state->aes256_ooo;

                        return SUBMIT_JOB_AES256_ENC(aes256_ooo, job);
                }
        } else if (IMB_CIPHER_CNTR == job->cipher_mode) {
                return SUBMIT_JOB_AES_CNTR(job);
        } else if (IMB_CIPHER_CNTR_BITLEN == job->cipher_mode) {
                return SUBMIT_JOB_AES_CNTR_BIT(job);
        } else if (IMB_CIPHER_ECB == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        return SUBMIT_JOB_AES_ECB_128_ENC(job);
                } else if (24 == job->key_len_in_bytes) {
                        return SUBMIT_JOB_AES_ECB_192_ENC(job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES_ECB_256_ENC(job);
                }
        } else if (IMB_CIPHER_DOCSIS_SEC_BPI == job->cipher_mode) {
                return submit_docsis_enc_job(state, job);
        } else if (IMB_CIPHER_PON_AES_CNTR == job->cipher_mode) {
                if (job->msg_len_to_cipher_in_bytes == 0)
                        return SUBMIT_JOB_PON_ENC_NO_CTR(job);
                else
                        return SUBMIT_JOB_PON_ENC(job);
        } else if (IMB_CIPHER_GCM == job->cipher_mode) {
                return SUBMIT_JOB_AES_GCM_ENC(state, job);
        } else if (IMB_CIPHER_GCM_SGL == job->cipher_mode) {
                return submit_gcm_sgl_enc(state, job);
        } else if (IMB_CIPHER_CUSTOM == job->cipher_mode) {
                return SUBMIT_JOB_CUSTOM_CIPHER(job);
        } else if (IMB_CIPHER_DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DES_CBC_ENC
                MB_MGR_DES_OOO *des_enc_ooo = state->des_enc_ooo;

                return SUBMIT_JOB_DES_CBC_ENC(des_enc_ooo, job);
#else
                return DES_CBC_ENC(job);
#endif /* SUBMIT_JOB_DES_CBC_ENC */
        } else if (IMB_CIPHER_CHACHA20 == job->cipher_mode) {
                return SUBMIT_JOB_CHACHA20_ENC_DEC(job);
        } else if (IMB_CIPHER_CHACHA20_POLY1305 == job->cipher_mode) {
                return SUBMIT_JOB_CHACHA20_POLY1305(state, job);
        } else if (IMB_CIPHER_CHACHA20_POLY1305_SGL == job->cipher_mode) {
                return SUBMIT_JOB_CHACHA20_POLY1305_SGL(state, job);
        } else if (IMB_CIPHER_DOCSIS_DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DOCSIS_DES_ENC
                MB_MGR_DES_OOO *docsis_des_enc_ooo = state->docsis_des_enc_ooo;

                return SUBMIT_JOB_DOCSIS_DES_ENC(docsis_des_enc_ooo,
                                                 job);
#else
                return DOCSIS_DES_ENC(job);
#endif /* SUBMIT_JOB_DOCSIS_DES_ENC */
        } else if (IMB_CIPHER_DES3 == job->cipher_mode) {
#ifdef SUBMIT_JOB_3DES_CBC_ENC
                MB_MGR_DES_OOO *des3_enc_ooo = state->des3_enc_ooo;

                return SUBMIT_JOB_3DES_CBC_ENC(des3_enc_ooo, job);
#else
                return DES3_CBC_ENC(job);
#endif
        } else if (IMB_CIPHER_CCM == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        return AES_CNTR_CCM_128(job);
                } else { /* assume 32 */
                        return AES_CNTR_CCM_256(job);
                }
        } else if (IMB_CIPHER_ZUC_EEA3 == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        MB_MGR_ZUC_OOO *zuc_eea3_ooo = state->zuc_eea3_ooo;

                        return SUBMIT_JOB_ZUC_EEA3(zuc_eea3_ooo, job);
                } else { /* assume 32 */
                        MB_MGR_ZUC_OOO *zuc256_eea3_ooo =
                                state->zuc256_eea3_ooo;

                        return SUBMIT_JOB_ZUC256_EEA3(zuc256_eea3_ooo, job);
                }
        } else if (IMB_CIPHER_SNOW3G_UEA2_BITLEN == job->cipher_mode) {
#ifdef SUBMIT_JOB_SNOW3G_UEA2
                return SUBMIT_JOB_SNOW3G_UEA2(state, job);
#else
                return def_submit_snow3g_uea2_job(state, job);
#endif
        } else if (IMB_CIPHER_KASUMI_UEA1_BITLEN == job->cipher_mode) {
                return submit_kasumi_uea1_job(state, job);
        } else if (IMB_CIPHER_CBCS_1_9 == job->cipher_mode) {
                MB_MGR_AES_OOO *aes128_cbcs_ooo = state->aes128_cbcs_ooo;

                return SUBMIT_JOB_AES128_CBCS_1_9_ENC(aes128_cbcs_ooo, job);
        } else if (IMB_CIPHER_SNOW_V == job->cipher_mode) {
                return SUBMIT_JOB_SNOW_V(job);
        } else if (IMB_CIPHER_SNOW_V_AEAD == job->cipher_mode) {
                return submit_snow_v_aead_job(state, job);
        } else { /* assume IMB_CIPHER_NULL */
                job->status |= IMB_STATUS_COMPLETED_CIPHER;
                return job;
        }
}

__forceinline
IMB_JOB *
FLUSH_JOB_AES_ENC(IMB_MGR *state, IMB_JOB *job)
{
        if (IMB_CIPHER_CBC == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        MB_MGR_AES_OOO *aes128_ooo = state->aes128_ooo;

                        return FLUSH_JOB_AES128_ENC(aes128_ooo);
                } else if (24 == job->key_len_in_bytes) {
                        MB_MGR_AES_OOO *aes192_ooo = state->aes192_ooo;

                        return FLUSH_JOB_AES192_ENC(aes192_ooo);
                } else  { /* assume 32 */
                        MB_MGR_AES_OOO *aes256_ooo = state->aes256_ooo;

                        return FLUSH_JOB_AES256_ENC(aes256_ooo);
                }
        } else if (IMB_CIPHER_DOCSIS_SEC_BPI == job->cipher_mode) {
                return flush_docsis_enc_job(state, job);
#ifdef FLUSH_JOB_DES_CBC_ENC
        } else if (IMB_CIPHER_DES == job->cipher_mode) {
                MB_MGR_DES_OOO *des_enc_ooo = state->des_enc_ooo;

                return FLUSH_JOB_DES_CBC_ENC(des_enc_ooo);
#endif /* FLUSH_JOB_DES_CBC_ENC */
#ifdef FLUSH_JOB_3DES_CBC_ENC
        } else if (IMB_CIPHER_DES3 == job->cipher_mode) {
                MB_MGR_DES_OOO *des3_enc_ooo = state->des3_enc_ooo;

                return FLUSH_JOB_3DES_CBC_ENC(des3_enc_ooo);
#endif /* FLUSH_JOB_3DES_CBC_ENC */
#ifdef FLUSH_JOB_DOCSIS_DES_ENC
        } else if (IMB_CIPHER_DOCSIS_DES == job->cipher_mode) {
                MB_MGR_DES_OOO *docsis_des_enc_ooo = state->docsis_des_enc_ooo;

                return FLUSH_JOB_DOCSIS_DES_ENC(docsis_des_enc_ooo);
#endif /* FLUSH_JOB_DOCSIS_DES_ENC */
        } else if (IMB_CIPHER_CUSTOM == job->cipher_mode) {
                return FLUSH_JOB_CUSTOM_CIPHER(job);
        } else if (IMB_CIPHER_ZUC_EEA3 == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        MB_MGR_ZUC_OOO *zuc_eea3_ooo = state->zuc_eea3_ooo;

                        return FLUSH_JOB_ZUC_EEA3(zuc_eea3_ooo);
                } else { /* assume 32 */
                        MB_MGR_ZUC_OOO *zuc256_eea3_ooo =
                                state->zuc256_eea3_ooo;

                        return FLUSH_JOB_ZUC256_EEA3(zuc256_eea3_ooo);
                }
        } else if (IMB_CIPHER_CBCS_1_9 == job->cipher_mode) {
                MB_MGR_AES_OOO *aes128_cbcs_ooo = state->aes128_cbcs_ooo;

                return FLUSH_JOB_AES128_CBCS_1_9_ENC(aes128_cbcs_ooo);
#ifdef FLUSH_JOB_SNOW3G_UEA2
        } else if (IMB_CIPHER_SNOW3G_UEA2_BITLEN == job->cipher_mode) {
                return FLUSH_JOB_SNOW3G_UEA2(state);
#endif
        /**
         * assume IMB_CIPHER_CNTR/CNTR_BITLEN, IMB_CIPHER_ECB,
         * IMB_CIPHER_CCM, IMB_CIPHER_NULL or IMB_CIPHER_GCM
         */
        } else {
                return NULL;
        }
}

__forceinline
IMB_JOB *
SUBMIT_JOB_AES_DEC(IMB_MGR *state, IMB_JOB *job)
{
        if (IMB_CIPHER_CBC == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        return SUBMIT_JOB_AES128_DEC(job);
                } else if (24 == job->key_len_in_bytes) {
                        return SUBMIT_JOB_AES192_DEC(job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES256_DEC(job);
                }
        } else if (IMB_CIPHER_CNTR == job->cipher_mode) {
                return SUBMIT_JOB_AES_CNTR(job);
        } else if (IMB_CIPHER_CNTR_BITLEN == job->cipher_mode) {
                return SUBMIT_JOB_AES_CNTR_BIT(job);
        } else if (IMB_CIPHER_ECB == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        return SUBMIT_JOB_AES_ECB_128_DEC(job);
                } else if (24 == job->key_len_in_bytes) {
                        return SUBMIT_JOB_AES_ECB_192_DEC(job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES_ECB_256_DEC(job);
                }
        } else if (IMB_CIPHER_DOCSIS_SEC_BPI == job->cipher_mode) {
                return submit_docsis_dec_job(state, job);
        } else if (IMB_CIPHER_PON_AES_CNTR == job->cipher_mode) {
                if (job->msg_len_to_cipher_in_bytes == 0)
                        return SUBMIT_JOB_PON_DEC_NO_CTR(job);
                else
                        return SUBMIT_JOB_PON_DEC(job);
        } else if (IMB_CIPHER_GCM == job->cipher_mode) {
                return SUBMIT_JOB_AES_GCM_DEC(state, job);
        } else if (IMB_CIPHER_GCM_SGL == job->cipher_mode) {
                return submit_gcm_sgl_dec(state, job);
        } else if (IMB_CIPHER_DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DES_CBC_DEC
                MB_MGR_DES_OOO *des_dec_ooo = state->des_dec_ooo;

                return SUBMIT_JOB_DES_CBC_DEC(des_dec_ooo, job);
#else
                (void) state;
                return DES_CBC_DEC(job);
#endif /* SUBMIT_JOB_DES_CBC_DEC */
        } else if (IMB_CIPHER_CHACHA20 == job->cipher_mode) {
                return SUBMIT_JOB_CHACHA20_ENC_DEC(job);
        } else if (IMB_CIPHER_CHACHA20_POLY1305 == job->cipher_mode) {
                return SUBMIT_JOB_CHACHA20_POLY1305(state, job);
        } else if (IMB_CIPHER_CHACHA20_POLY1305_SGL == job->cipher_mode) {
                return SUBMIT_JOB_CHACHA20_POLY1305_SGL(state, job);
        } else if (IMB_CIPHER_DOCSIS_DES == job->cipher_mode) {
#ifdef SUBMIT_JOB_DOCSIS_DES_DEC
                MB_MGR_DES_OOO *docsis_des_dec_ooo = state->docsis_des_dec_ooo;

                return SUBMIT_JOB_DOCSIS_DES_DEC(docsis_des_dec_ooo,
                                                 job);
#else
                return DOCSIS_DES_DEC(job);
#endif /* SUBMIT_JOB_DOCSIS_DES_DEC */
        } else if (IMB_CIPHER_DES3 == job->cipher_mode) {
#ifdef SUBMIT_JOB_3DES_CBC_DEC
                MB_MGR_DES_OOO *des3_dec_ooo = state->des3_dec_ooo;

                return SUBMIT_JOB_3DES_CBC_DEC(des3_dec_ooo, job);
#else
                return DES3_CBC_DEC(job);
#endif
        } else if (IMB_CIPHER_CUSTOM == job->cipher_mode) {
                return SUBMIT_JOB_CUSTOM_CIPHER(job);
        } else if (IMB_CIPHER_CCM == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        return AES_CNTR_CCM_128(job);
                } else { /* assume 32 */
                        return AES_CNTR_CCM_256(job);
                }
        } else if (IMB_CIPHER_ZUC_EEA3 == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        MB_MGR_ZUC_OOO *zuc_eea3_ooo = state->zuc_eea3_ooo;

                        return SUBMIT_JOB_ZUC_EEA3(zuc_eea3_ooo, job);
                } else { /* assume 32 */
                        MB_MGR_ZUC_OOO *zuc256_eea3_ooo =
                                state->zuc256_eea3_ooo;

                        return SUBMIT_JOB_ZUC256_EEA3(zuc256_eea3_ooo, job);
                }
        } else if (IMB_CIPHER_SNOW3G_UEA2_BITLEN == job->cipher_mode) {
#ifdef SUBMIT_JOB_SNOW3G_UEA2
                return SUBMIT_JOB_SNOW3G_UEA2(state, job);
#else
                return def_submit_snow3g_uea2_job(state, job);
#endif
        } else if (IMB_CIPHER_KASUMI_UEA1_BITLEN == job->cipher_mode) {
                return submit_kasumi_uea1_job(state, job);
        } else if (IMB_CIPHER_CBCS_1_9 == job->cipher_mode) {
                return SUBMIT_JOB_AES128_CBCS_1_9_DEC(job);
        } else if (IMB_CIPHER_SNOW_V == job->cipher_mode) {
                return SUBMIT_JOB_SNOW_V(job);
        } else if (IMB_CIPHER_SNOW_V_AEAD == job->cipher_mode) {
                return submit_snow_v_aead_job(state, job);
        } else {
                /* assume IMB_CIPHER_NULL */
                job->status |= IMB_STATUS_COMPLETED_CIPHER;
                return job;
        }
}

__forceinline
IMB_JOB *
FLUSH_JOB_AES_DEC(IMB_MGR *state, IMB_JOB *job)
{
#ifdef FLUSH_JOB_SNOW3G_UEA2
        if (IMB_CIPHER_SNOW3G_UEA2_BITLEN == job->cipher_mode)
                return FLUSH_JOB_SNOW3G_UEA2(state);
#endif

#ifdef FLUSH_JOB_DES_CBC_DEC
        if (IMB_CIPHER_DES == job->cipher_mode) {
                MB_MGR_DES_OOO *des_dec_ooo = state->des_dec_ooo;

                return FLUSH_JOB_DES_CBC_DEC(des_dec_ooo);
        }
#endif /* FLUSH_JOB_DES_CBC_DEC */

#ifdef FLUSH_JOB_3DES_CBC_DEC
        if (IMB_CIPHER_DES3 == job->cipher_mode) {
                MB_MGR_DES_OOO *des3_dec_ooo = state->des3_dec_ooo;

                return FLUSH_JOB_3DES_CBC_DEC(des3_dec_ooo);
        }
#endif /* FLUSH_JOB_3DES_CBC_DEC */

#ifdef FLUSH_JOB_DOCSIS_DES_DEC

        if (IMB_CIPHER_DOCSIS_DES == job->cipher_mode) {
                MB_MGR_DES_OOO *docsis_des_dec_ooo = state->docsis_des_dec_ooo;

                return FLUSH_JOB_DOCSIS_DES_DEC(docsis_des_dec_ooo);
        }
#endif /* FLUSH_JOB_DOCSIS_DES_DEC */

        if (IMB_CIPHER_ZUC_EEA3 == job->cipher_mode) {
                if (16 == job->key_len_in_bytes) {
                        MB_MGR_ZUC_OOO *zuc_eea3_ooo = state->zuc_eea3_ooo;

                        return FLUSH_JOB_ZUC_EEA3(zuc_eea3_ooo);
                } else { /* assume 32 */
                        MB_MGR_ZUC_OOO *zuc256_eea3_ooo =
                                state->zuc256_eea3_ooo;

                        return FLUSH_JOB_ZUC256_EEA3(zuc256_eea3_ooo);
                }
        }

        return NULL;
}

/* ========================================================================= */
/* Hash submit & flush functions */
/* ========================================================================= */

__forceinline
void
process_gmac(IMB_MGR *state, IMB_JOB *job, const IMB_KEY_SIZE_BYTES key_size)
{
        struct gcm_context_data ctx;
        const struct gcm_key_data *key = job->u.GMAC._key;
        const uint8_t *iv = job->u.GMAC._iv;
        const uint64_t iv_len = job->u.GMAC.iv_len_in_bytes;
        const uint8_t *src = job->src + job->hash_start_src_offset_in_bytes;
        const uint64_t src_len = job->msg_len_to_hash_in_bytes;

        if (key_size == IMB_KEY_128_BYTES) {
                IMB_AES128_GMAC_INIT(state, key, &ctx, iv, iv_len);
                IMB_AES128_GMAC_UPDATE(state, key, &ctx, src, src_len);
                IMB_AES128_GMAC_FINALIZE(state, key, &ctx,
                                         job->auth_tag_output,
                                         job->auth_tag_output_len_in_bytes);
        } else if (key_size == IMB_KEY_192_BYTES) {
                IMB_AES192_GMAC_INIT(state, key, &ctx, iv, iv_len);
                IMB_AES192_GMAC_UPDATE(state, key, &ctx, src, src_len);
                IMB_AES192_GMAC_FINALIZE(state, key, &ctx,
                                         job->auth_tag_output,
                                         job->auth_tag_output_len_in_bytes);
        } else { /* key_size == 256 */
                IMB_AES256_GMAC_INIT(state, key, &ctx, iv, iv_len);
                IMB_AES256_GMAC_UPDATE(state, key, &ctx, src, src_len);
                IMB_AES256_GMAC_FINALIZE(state, key, &ctx,
                                         job->auth_tag_output,
                                         job->auth_tag_output_len_in_bytes);
        }
}

__forceinline
IMB_JOB *
SUBMIT_JOB_HASH(IMB_MGR *state, IMB_JOB *job)
{
        MB_MGR_HMAC_SHA_1_OOO *hmac_sha_1_ooo = state->hmac_sha_1_ooo;
        MB_MGR_HMAC_SHA_256_OOO *hmac_sha_224_ooo = state->hmac_sha_224_ooo;
        MB_MGR_HMAC_SHA_256_OOO *hmac_sha_256_ooo = state->hmac_sha_256_ooo;
        MB_MGR_HMAC_SHA_512_OOO *hmac_sha_384_ooo = state->hmac_sha_384_ooo;
        MB_MGR_HMAC_SHA_512_OOO *hmac_sha_512_ooo = state->hmac_sha_512_ooo;
        MB_MGR_HMAC_MD5_OOO *hmac_md5_ooo = state->hmac_md5_ooo;
        MB_MGR_AES_XCBC_OOO *aes_xcbc_ooo = state->aes_xcbc_ooo;
        MB_MGR_CCM_OOO *aes_ccm_ooo = state->aes_ccm_ooo;
	MB_MGR_CCM_OOO *aes256_ccm_ooo = state->aes256_ccm_ooo;
        MB_MGR_CMAC_OOO *aes_cmac_ooo = state->aes_cmac_ooo;
	MB_MGR_CMAC_OOO *aes256_cmac_ooo = state->aes256_cmac_ooo;
        MB_MGR_ZUC_OOO *zuc_eia3_ooo = state->zuc_eia3_ooo;
        MB_MGR_ZUC_OOO *zuc256_eia3_ooo = state->zuc256_eia3_ooo;
#ifdef AVX512
        MB_MGR_SNOW3G_OOO *snow3g_uia2_ooo = state->snow3g_uia2_ooo;
#endif


        switch (job->hash_alg) {
        case IMB_AUTH_HMAC_SHA_1:
#ifdef HASH_USE_SHAEXT
                if (state->features & IMB_FEATURE_SHANI)
                        return SUBMIT_JOB_HMAC_NI(hmac_sha_1_ooo, job);
#endif
                return SUBMIT_JOB_HMAC(hmac_sha_1_ooo, job);
        case IMB_AUTH_HMAC_SHA_224:
#ifdef HASH_USE_SHAEXT
                if (state->features & IMB_FEATURE_SHANI)
                        return SUBMIT_JOB_HMAC_SHA_224_NI
                                (hmac_sha_224_ooo, job);
#endif
                return SUBMIT_JOB_HMAC_SHA_224(hmac_sha_224_ooo, job);
        case IMB_AUTH_HMAC_SHA_256:
#ifdef HASH_USE_SHAEXT
                if (state->features & IMB_FEATURE_SHANI)
                        return SUBMIT_JOB_HMAC_SHA_256_NI
                                (hmac_sha_256_ooo, job);
#endif
                return SUBMIT_JOB_HMAC_SHA_256(hmac_sha_256_ooo, job);
        case IMB_AUTH_HMAC_SHA_384:
                return SUBMIT_JOB_HMAC_SHA_384(hmac_sha_384_ooo, job);
        case IMB_AUTH_HMAC_SHA_512:
                return SUBMIT_JOB_HMAC_SHA_512(hmac_sha_512_ooo, job);
        case IMB_AUTH_AES_XCBC:
                return SUBMIT_JOB_AES_XCBC(aes_xcbc_ooo, job);
        case IMB_AUTH_MD5:
                return SUBMIT_JOB_HMAC_MD5(hmac_md5_ooo, job);
        case IMB_AUTH_CUSTOM:
                return SUBMIT_JOB_CUSTOM_HASH(job);
        case IMB_AUTH_AES_CCM:
                if (16 == job->key_len_in_bytes) {
                        return SUBMIT_JOB_AES128_CCM_AUTH(aes_ccm_ooo, job);
                } else { /* assume 32 */
                        return SUBMIT_JOB_AES256_CCM_AUTH(aes256_ccm_ooo, job);
                }
        case IMB_AUTH_AES_CMAC:
                /*
                 * CMAC OOO MGR assumes job len in bits
                 * (for CMAC length is provided in bytes)
                 */
                job->msg_len_to_hash_in_bits =
                        job->msg_len_to_hash_in_bytes * 8;
                return SUBMIT_JOB_AES128_CMAC_AUTH(aes_cmac_ooo, job);
        case IMB_AUTH_AES_CMAC_BITLEN:
                return SUBMIT_JOB_AES128_CMAC_AUTH(aes_cmac_ooo, job);
        case IMB_AUTH_AES_CMAC_256:
                job->msg_len_to_hash_in_bits =
                        job->msg_len_to_hash_in_bytes * 8;
                return SUBMIT_JOB_AES256_CMAC_AUTH(aes256_cmac_ooo, job);
        case IMB_AUTH_SHA_1:
                IMB_SHA1(state,
                         job->src + job->hash_start_src_offset_in_bytes,
                         job->msg_len_to_hash_in_bytes, job->auth_tag_output);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_SHA_224:
                IMB_SHA224(state,
                           job->src + job->hash_start_src_offset_in_bytes,
                           job->msg_len_to_hash_in_bytes, job->auth_tag_output);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_SHA_256:
                IMB_SHA256(state,
                           job->src + job->hash_start_src_offset_in_bytes,
                           job->msg_len_to_hash_in_bytes, job->auth_tag_output);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_SHA_384:
                IMB_SHA384(state,
                           job->src + job->hash_start_src_offset_in_bytes,
                           job->msg_len_to_hash_in_bytes, job->auth_tag_output);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_SHA_512:
                IMB_SHA512(state,
                           job->src + job->hash_start_src_offset_in_bytes,
                           job->msg_len_to_hash_in_bytes, job->auth_tag_output);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_ZUC_EIA3_BITLEN:
                return SUBMIT_JOB_ZUC_EIA3(zuc_eia3_ooo, job);
        case IMB_AUTH_ZUC256_EIA3_BITLEN:
                return SUBMIT_JOB_ZUC256_EIA3(zuc256_eia3_ooo, job);
        case IMB_AUTH_SNOW3G_UIA2_BITLEN:
#ifdef AVX512
                return SUBMIT_JOB_SNOW3G_UIA2(snow3g_uia2_ooo, job);
#else
                IMB_SNOW3G_F9_1_BUFFER(state, (const snow3g_key_schedule_t *)
                               job->u.SNOW3G_UIA2._key,
                               job->u.SNOW3G_UIA2._iv,
                               job->src + job->hash_start_src_offset_in_bytes,
                               job->msg_len_to_hash_in_bits,
                               job->auth_tag_output);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
#endif
        case IMB_AUTH_KASUMI_UIA1:
                IMB_KASUMI_F9_1_BUFFER(state, (const kasumi_key_sched_t *)
                               job->u.KASUMI_UIA1._key,
                               job->src + job->hash_start_src_offset_in_bytes,
                               (const uint32_t) job->msg_len_to_hash_in_bytes,
                               job->auth_tag_output);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_AES_GMAC_128:
                process_gmac(state, job, IMB_KEY_128_BYTES);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_AES_GMAC_192:
                process_gmac(state, job, IMB_KEY_192_BYTES);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_AES_GMAC_256:
                process_gmac(state, job, IMB_KEY_256_BYTES);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_POLY1305:
                POLY1305_MAC(job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC32_ETHERNET_FCS:
                CRC(IMB_CRC32_ETHERNET_FCS, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC32_SCTP:
                CRC(IMB_CRC32_SCTP, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC32_WIMAX_OFDMA_DATA:
                CRC(IMB_CRC32_WIMAX_OFDMA_DATA, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC24_LTE_A:
                CRC(IMB_CRC24_LTE_A, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC24_LTE_B:
                CRC(IMB_CRC24_LTE_B, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC16_X25:
                CRC(IMB_CRC16_X25, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC16_FP_DATA:
                CRC(IMB_CRC16_FP_DATA, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC11_FP_HEADER:
                CRC(IMB_CRC11_FP_HEADER, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC10_IUUP_DATA:
                CRC(IMB_CRC10_IUUP_DATA, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC8_WIMAX_OFDMA_HCS:
                CRC(IMB_CRC8_WIMAX_OFDMA_HCS, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC7_FP_HEADER:
                CRC(IMB_CRC7_FP_HEADER, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        case IMB_AUTH_CRC6_IUUP_HEADER:
                CRC(IMB_CRC6_IUUP_HEADER, state, job);
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
                /**
                 * assume IMB_AUTH_GCM, IMB_AUTH_PON_CRC_BIP,
                 *  IMB_AUTH_SNOW_V_AEAD or IMB_AUTH_NULL
                 */
        default:
                job->status |= IMB_STATUS_COMPLETED_AUTH;
                return job;
        }
}

__forceinline
IMB_JOB *
FLUSH_JOB_HASH(IMB_MGR *state, IMB_JOB *job)
{
        MB_MGR_HMAC_SHA_1_OOO *hmac_sha_1_ooo = state->hmac_sha_1_ooo;
        MB_MGR_HMAC_SHA_256_OOO *hmac_sha_224_ooo = state->hmac_sha_224_ooo;
        MB_MGR_HMAC_SHA_256_OOO *hmac_sha_256_ooo = state->hmac_sha_256_ooo;
        MB_MGR_HMAC_SHA_512_OOO *hmac_sha_384_ooo = state->hmac_sha_384_ooo;
        MB_MGR_HMAC_SHA_512_OOO *hmac_sha_512_ooo = state->hmac_sha_512_ooo;
        MB_MGR_HMAC_MD5_OOO *hmac_md5_ooo = state->hmac_md5_ooo;
        MB_MGR_AES_XCBC_OOO *aes_xcbc_ooo = state->aes_xcbc_ooo;
        MB_MGR_CCM_OOO *aes_ccm_ooo = state->aes_ccm_ooo;
	MB_MGR_CCM_OOO *aes256_ccm_ooo = state->aes256_ccm_ooo;
        MB_MGR_CMAC_OOO *aes_cmac_ooo = state->aes_cmac_ooo;
	MB_MGR_CMAC_OOO *aes256_cmac_ooo = state->aes256_cmac_ooo;
        MB_MGR_ZUC_OOO *zuc_eia3_ooo = state->zuc_eia3_ooo;
        MB_MGR_ZUC_OOO *zuc256_eia3_ooo = state->zuc256_eia3_ooo;
#ifdef AVX512
        MB_MGR_SNOW3G_OOO *snow3g_uia2_ooo = state->snow3g_uia2_ooo;
#endif

        switch (job->hash_alg) {
        case IMB_AUTH_HMAC_SHA_1:
#ifdef HASH_USE_SHAEXT
                if (state->features & IMB_FEATURE_SHANI)
                        return FLUSH_JOB_HMAC_NI(hmac_sha_1_ooo);
#endif
                return FLUSH_JOB_HMAC(hmac_sha_1_ooo);
        case IMB_AUTH_HMAC_SHA_224:
#ifdef HASH_USE_SHAEXT
                if (state->features & IMB_FEATURE_SHANI)
                        return FLUSH_JOB_HMAC_SHA_224_NI
                                (hmac_sha_224_ooo);
#endif
                return FLUSH_JOB_HMAC_SHA_224(hmac_sha_224_ooo);
        case IMB_AUTH_HMAC_SHA_256:
#ifdef HASH_USE_SHAEXT
                if (state->features & IMB_FEATURE_SHANI)
                        return FLUSH_JOB_HMAC_SHA_256_NI
                                (hmac_sha_256_ooo);
#endif
                return FLUSH_JOB_HMAC_SHA_256(hmac_sha_256_ooo);
        case IMB_AUTH_HMAC_SHA_384:
                return FLUSH_JOB_HMAC_SHA_384(hmac_sha_384_ooo);
        case IMB_AUTH_HMAC_SHA_512:
                return FLUSH_JOB_HMAC_SHA_512(hmac_sha_512_ooo);
        case IMB_AUTH_AES_XCBC:
                return FLUSH_JOB_AES_XCBC(aes_xcbc_ooo);
        case IMB_AUTH_MD5:
                return FLUSH_JOB_HMAC_MD5(hmac_md5_ooo);
        case IMB_AUTH_CUSTOM:
                return FLUSH_JOB_CUSTOM_HASH(job);
        case IMB_AUTH_AES_CCM:
                if (16 == job->key_len_in_bytes) {
                        return FLUSH_JOB_AES128_CCM_AUTH(aes_ccm_ooo);
                } else { /* assume 32 */
                        return FLUSH_JOB_AES256_CCM_AUTH(aes256_ccm_ooo);
                }
        case IMB_AUTH_AES_CMAC:
        case IMB_AUTH_AES_CMAC_BITLEN:
                return FLUSH_JOB_AES128_CMAC_AUTH(aes_cmac_ooo);
        case IMB_AUTH_AES_CMAC_256:
                return FLUSH_JOB_AES256_CMAC_AUTH(aes256_cmac_ooo);
        case IMB_AUTH_ZUC_EIA3_BITLEN:
                return FLUSH_JOB_ZUC_EIA3(zuc_eia3_ooo);
        case IMB_AUTH_ZUC256_EIA3_BITLEN:
                return FLUSH_JOB_ZUC256_EIA3(zuc256_eia3_ooo);
#ifdef AVX512
        case IMB_AUTH_SNOW3G_UIA2_BITLEN:
                return FLUSH_JOB_SNOW3G_UIA2(snow3g_uia2_ooo);
#endif
        default: /* assume GCM or IMB_AUTH_NULL */
                if (!(job->status & IMB_STATUS_COMPLETED_AUTH)) {
                        job->status |= IMB_STATUS_COMPLETED_AUTH;
                        return job;
                }
                /* if HMAC is complete then return NULL */
                return NULL;
        }
}


/* ========================================================================= */
/* Job submit & flush functions */
/* ========================================================================= */

/* GCM NIST standard: len(M) < 2^39 - 256 */
#define GCM_MAX_LEN  UINT64_C(((1ULL << 39) - 256) - 1)
#define SNOW3G_MAX_BITLEN (UINT32_MAX)
#define MB_MAX_LEN16 ((1 << 16) - 2)

__forceinline int
is_job_invalid(IMB_MGR *state, const IMB_JOB *job)
{
        const uint64_t auth_tag_len_fips[] = {
                0,  /* INVALID selection */
                20, /* IMB_AUTH_HMAC_SHA_1 */
                28, /* IMB_AUTH_HMAC_SHA_224 */
                32, /* IMB_AUTH_HMAC_SHA_256 */
                48, /* IMB_AUTH_HMAC_SHA_384 */
                64, /* IMB_AUTH_HMAC_SHA_512 */
                12, /* IMB_AUTH_AES_XCBC */
                16, /* IMB_AUTH_MD5 */
                0,  /* IMB_AUTH_NULL */
                16, /* IMB_AUTH_AES_GMAC */
                0,  /* IMB_AUTH_CUSTOM */
                0,  /* IMB_AUTH_AES_CCM */
                16, /* IMB_AUTH_AES_CMAC */
                20, /* IMB_AUTH_SHA_1 */
                28, /* IMB_AUTH_SHA_224 */
                32, /* IMB_AUTH_SHA_256 */
                48, /* IMB_AUTH_SHA_384 */
                64, /* IMB_AUTH_SHA_512 */
                4,  /* IMB_AUTH_AES_CMAC 3GPP */
                8,  /* IMB_AUTH_PON_CRC_BIP */
                4,  /* IMB_AUTH_ZUC_EIA3_BITLEN */
                4,  /* IMB_AUTH_DOCSIS_CRC32 */
                4,  /* IMB_AUTH_SNOW3G_UIA2_BITLEN */
                4,  /* IMB_AUTH_KASUMI_UIA1 */
                16, /* IMB_AUTH_AES_GMAC_128 */
                16, /* IMB_AUTH_AES_GMAC_192 */
                16, /* IMB_AUTH_AES_GMAC_256 */
                16, /* IMB_AUTH_AES_CMAC_256 */
                16, /* IMB_AUTH_POLY1305 */
                16, /* IMB_AUTH_CHACHA_POLY1305 */
                16, /* IMB_AUTH_CHACHA_POLY1305_SGL */
                4,  /* IMB_AUTH_ZUC256_EIA3_BITLEN */
                16, /* IMB_AUTH_SNOW_V_AEAD */
                16, /* IMB_AUTH_AES_GCM_SGL */
                4,  /* IMB_AUTH_CRC32_ETHERNET_FCS */
                4,  /* IMB_AUTH_CRC32_SCTP */
                4,  /* IMB_AUTH_CRC32_WIMAX_OFDMA_DATA */
                4,  /* IMB_AUTH_CRC24_LTE_A */
                4,  /* IMB_AUTH_CRC24_LTE_B */
                4,  /* IMB_AUTH_CRC16_X25 */
                4,  /* IMB_AUTH_CRC16_FP_DATA */
                4,  /* IMB_AUTH_CRC11_FP_HEADER */
                4,  /* IMB_AUTH_CRC10_IUUP_DATA */
                4,  /* IMB_AUTH_CRC8_WIMAX_OFDMA_HCS */
                4,  /* IMB_AUTH_CRC7_FP_HEADER */
                4,  /* IMB_AUTH_CRC6_IUUP_HEADER */
        };
        const uint64_t auth_tag_len_ipsec[] = {
                0,  /* INVALID selection */
                12, /* IMB_AUTH_HMAC_SHA_1 */
                14, /* IMB_AUTH_HMAC_SHA_224 */
                16, /* IMB_AUTH_HMAC_SHA_256 */
                24, /* IMB_AUTH_HMAC_SHA_384 */
                32, /* IMB_AUTH_HMAC_SHA_512 */
                12, /* IMB_AUTH_AES_XCBC */
                12, /* IMB_AUTH_MD5 */
                0,  /* IMB_AUTH_NULL */
                16, /* IMB_AUTH_AES_GMAC */
                0,  /* IMB_AUTH_CUSTOM */
                0,  /* IMB_AUTH_AES_CCM */
                16, /* IMB_AUTH_AES_CMAC */
                20, /* IMB_AUTH_SHA_1 */
                28, /* IMB_AUTH_SHA_224 */
                32, /* IMB_AUTH_SHA_256 */
                48, /* IMB_AUTH_SHA_384 */
                64, /* IMB_AUTH_SHA_512 */
                4,  /* IMB_AUTH_AES_CMAC 3GPP */
                8,  /* IMB_AUTH_PON_CRC_BIP */
                4,  /* IMB_AUTH_ZUC_EIA3_BITLEN */
                4,  /* IMB_AUTH_DOCSIS_CRC32 */
                4,  /* IMB_AUTH_SNOW3G_UIA2_BITLEN */
                4,  /* IMB_AUTH_KASUMI_UIA1 */
                16, /* IMB_AUTH_AES_GMAC_128 */
                16, /* IMB_AUTH_AES_GMAC_192 */
                16, /* IMB_AUTH_AES_GMAC_256 */
                16, /* IMB_AUTH_AES_CMAC_256 */
                16, /* IMB_AUTH_POLY1305 */
                16, /* IMB_AUTH_CHACHA_POLY1305 */
                16, /* IMB_AUTH_CHACHA_POLY1305_SGL */
                4,  /* IMB_AUTH_ZUC256_EIA3_BITLEN */
                16, /* IMB_AUTH_SNOW_V_AEAD */
                16, /* IMB_AUTH_AES_GCM_SGL */
                4,  /* IMB_AUTH_CRC32_ETHERNET_FCS */
                4,  /* IMB_AUTH_CRC32_SCTP */
                4,  /* IMB_AUTH_CRC32_WIMAX_OFDMA_DATA */
                4,  /* IMB_AUTH_CRC24_LTE_A */
                4,  /* IMB_AUTH_CRC24_LTE_B */
                4,  /* IMB_AUTH_CRC16_X25 */
                4,  /* IMB_AUTH_CRC16_FP_DATA */
                4,  /* IMB_AUTH_CRC11_FP_HEADER */
                4,  /* IMB_AUTH_CRC10_IUUP_DATA */
                4,  /* IMB_AUTH_CRC8_WIMAX_OFDMA_HCS */
                4,  /* IMB_AUTH_CRC7_FP_HEADER */
                4,  /* IMB_AUTH_CRC6_IUUP_HEADER */
        };

        /* Maximum length of buffer in PON is 2^14 + 8, since maximum
         * PLI value is 2^14 - 1 + 1 extra byte of padding + 8 bytes
         * of XGEM header */
        const uint64_t max_pon_len = (1 << 14) + 8;

        switch (job->cipher_mode) {
        case IMB_CIPHER_CBC:
        case IMB_CIPHER_CBCS_1_9:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_ENCRYPT &&
                    job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_DECRYPT &&
                    job->dec_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(16) &&
                    job->key_len_in_bytes != UINT64_C(24) &&
                    job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes & UINT64_C(15)) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->cipher_mode == IMB_CIPHER_CBCS_1_9) {
                        if (job->msg_len_to_cipher_in_bytes >
                            ((1ULL << (60)) - 1)) {
                                imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                                return 1;
                        }
                        if (job->cipher_fields.CBCS.next_iv == NULL) {
                                imb_set_errno(state,
                                              IMB_ERR_JOB_NULL_NEXT_IV);
                                return 1;
                        }
                } else if (job->cipher_direction == IMB_DIR_ENCRYPT &&
                           job->msg_len_to_cipher_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_ECB:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(16) &&
                    job->key_len_in_bytes != UINT64_C(24) &&
                    job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0 ||
                    job->msg_len_to_cipher_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes & UINT64_C(15)) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(0)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                break;
        case IMB_CIPHER_CNTR:
        case IMB_CIPHER_CNTR_BITLEN:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(16) &&
                    job->key_len_in_bytes != UINT64_C(24) &&
                    job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if ((job->cipher_mode == IMB_CIPHER_CNTR &&
                     job->iv_len_in_bytes != UINT64_C(16) &&
                     job->iv_len_in_bytes != UINT64_C(12)) ||
                     (job->cipher_mode == IMB_CIPHER_CNTR_BITLEN &&
                      job->iv_len_in_bytes != UINT64_C(16))) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                /*
                 * msg_len_to_cipher_in_bits is used with CNTR_BITLEN, but it is
                 * effectively the same field as msg_len_to_cipher_in_bytes,
                 * since it is part of the same union
                 */
                if (job->msg_len_to_cipher_in_bytes == 0) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_NULL:
                /*
                 * No checks required for this mode
                 * @note NULL cipher doesn't perform memory copy operation
                 *       from source to destination
                 */
                break;
        case IMB_CIPHER_DOCSIS_SEC_BPI:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        /* it has to be set regardless of direction (AES-CFB) */
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_DECRYPT &&
                    job->dec_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if ((job->key_len_in_bytes != UINT64_C(16)) &&
                    (job->key_len_in_bytes != UINT64_C(32))) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_GCM:
        case IMB_CIPHER_GCM_SGL:
                if (job->msg_len_to_cipher_in_bytes > GCM_MAX_LEN) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes != 0 && job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes != 0 && job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                /* Same key structure used for encrypt and decrypt */
                if (job->cipher_direction == IMB_DIR_ENCRYPT &&
                    job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_DECRYPT &&
                    job->dec_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(16) &&
                    job->key_len_in_bytes != UINT64_C(24) &&
                    job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes == 0) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                if (job->cipher_mode == IMB_CIPHER_GCM &&
                    job->hash_alg != IMB_AUTH_AES_GMAC) {
                        imb_set_errno(state, IMB_ERR_HASH_ALGO);
                        return 1;
                }
                if (job->cipher_mode == IMB_CIPHER_GCM_SGL &&
                    job->hash_alg != IMB_AUTH_GCM_SGL) {
                        imb_set_errno(state, IMB_ERR_HASH_ALGO);
                        return 1;
                }
                break;
        case IMB_CIPHER_CUSTOM:
                /* no checks here */
                if (job->cipher_func == NULL) {
                        imb_set_errno(state, EFAULT);
                        return 1;
                }
                break;
        case IMB_CIPHER_DES:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_ENCRYPT &&
                    job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_DECRYPT &&
                    job->dec_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(8)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0 ||
                    job->msg_len_to_cipher_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes & UINT64_C(7)) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(8)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_DOCSIS_DES:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_ENCRYPT &&
                    job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_DECRYPT &&
                    job->dec_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(8)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0 ||
                    job->msg_len_to_cipher_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(8)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_CCM:
                if (job->msg_len_to_cipher_in_bytes != 0) {
                        if (job->src == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                                return 1;
                        }
                        if (job->dst == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                                return 1;
                        }
                }
                if (job->msg_len_to_cipher_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        /* AES-CTR and CBC-MAC use only encryption keys */
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                /* currently only AES-CCM-128 and AES-CCM-256 supported */
                if (job->key_len_in_bytes != UINT64_C(16) &&
                    job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                /*
                 * From RFC3610:
                 *     Nonce length = 15 - L
                 *     Valid L values are: 2 to 8
                 * Then valid nonce lengths 13 to 7 (inclusive).
                 */
                if (job->iv_len_in_bytes > UINT64_C(13) ||
                    job->iv_len_in_bytes < UINT64_C(7)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                if (job->hash_alg != IMB_AUTH_AES_CCM) {
                        imb_set_errno(state, IMB_ERR_HASH_ALGO);
                        return 1;
                }
                break;
        case IMB_CIPHER_DES3:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(24)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0 ||
                    job->msg_len_to_cipher_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes & UINT64_C(7)) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(8)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                if (job->cipher_direction == IMB_DIR_ENCRYPT) {
                        const void * const *ks_ptr =
                                (const void * const *)job->enc_keys;

                        if (ks_ptr == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                                return 1;
                        }
                        if (ks_ptr[0] == NULL || ks_ptr[1] == NULL ||
                            ks_ptr[2] == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                                return 1;
                        }
                } else {
                        const void * const *ks_ptr =
                                (const void * const *)job->dec_keys;

                        if (ks_ptr == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                                return 1;
                        }
                        if (ks_ptr[0] == NULL || ks_ptr[1] == NULL ||
                            ks_ptr[2] == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                                return 1;
                        }
                }
                break;
        case IMB_CIPHER_PON_AES_CNTR:
                /*
                 * CRC and cipher are done together. A few assumptions:
                 * - CRC and cipher start offsets are the same
                 * - last 4 bytes (32 bits) of the buffer is CRC
                 * - updated CRC value is put into the source buffer
                 *   (encryption only)
                 * - CRC length is msg_len_to_cipher_in_bytes - 4 bytes
                 * - msg_len_to_cipher_in_bytes is aligned to 4 bytes
                 * - If msg_len_to_cipher_in_bytes is 0, IV and key pointers
                 *   are not required, as encryption is not done
                 */
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }

                /* source and destination buffer pointers cannot be the same,
                 * as there are always 8 bytes that are not ciphered */
                if ((job->src + job->cipher_start_src_offset_in_bytes)
                    != job->dst) {
                        imb_set_errno(state, EINVAL);
                        return 1;
                }
                if (job->hash_alg != IMB_AUTH_PON_CRC_BIP) {
                        imb_set_errno(state, IMB_ERR_HASH_ALGO);
                        return 1;
                }
                /*
                 * If message length to cipher != 0, AES-CTR is performed and
                 * key and IV require to be set properly
                 */
                if (job->msg_len_to_cipher_in_bytes != UINT64_C(0)) {

                        /* message size needs to be aligned to 4 bytes */
                        if ((job->msg_len_to_cipher_in_bytes & 3) != 0) {
                                imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                                return 1;
                        }

                        /* Subtract 8 bytes to maximum length since
                         * XGEM header is not ciphered */
                        if ((job->msg_len_to_cipher_in_bytes >
                             (max_pon_len - 8))) {
                                imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                                return 1;
                        }

                        if (job->key_len_in_bytes != UINT64_C(16)) {
                                imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                                return 1;
                        }
                        if (job->iv_len_in_bytes != UINT64_C(16)) {
                                imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                                return 1;
                        }
                        if (job->iv == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                                return 1;
                        }
                        if (job->enc_keys == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                                return 1;
                        }
                }
                if (job->msg_len_to_cipher_in_bytes >= 4) {
                        const uint64_t xgem_hdr = *(const uint64_t *)
                                (job->src +
                                 job->hash_start_src_offset_in_bytes);

                        /* PLI is 14 MS bits of XGEM header */
                        const uint16_t pli = BSWAP64(xgem_hdr) >> 50;

                        /* CRC only if PLI is more than 4 bytes */
                        if (pli > 4) {
                                const uint16_t crc_len = pli - 4;

                                if (crc_len >
                                    job->msg_len_to_cipher_in_bytes - 4) {
                                        imb_set_errno(state,
                                                      IMB_ERR_JOB_PON_PLI);
                                        return 1;
                                }
                        }
                }
                break;
        case IMB_CIPHER_ZUC_EEA3:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(16) &&
                    job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes == 0 ||
                    job->msg_len_to_cipher_in_bytes > ZUC_MAX_BYTELEN) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->key_len_in_bytes == UINT64_C(16)) {
                        if (job->iv_len_in_bytes != UINT64_C(16)) {
                                imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                                return 1;
                        }
                } else {
                        if (job->iv_len_in_bytes != UINT64_C(23) &&
                            job->iv_len_in_bytes != UINT64_C(25)) {
                                imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                                return 1;
                        }
                }
                break;
        case IMB_CIPHER_SNOW3G_UEA2_BITLEN:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bits == 0 ||
                    job->msg_len_to_cipher_in_bits > SNOW3G_MAX_BITLEN) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_KASUMI_UEA1_BITLEN:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bits == 0 ||
                    job->msg_len_to_cipher_in_bits > KASUMI_MAX_LEN) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(8)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_CHACHA20:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                /* Per RFC 7539, max cipher size is (2^32 - 1) x 64 */
                if (job->msg_len_to_cipher_in_bytes == 0 ||
                    job->msg_len_to_cipher_in_bytes > ((1ULL << 38) - 64)) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(12)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_CHACHA20_POLY1305:
        case IMB_CIPHER_CHACHA20_POLY1305_SGL:
                if (job->msg_len_to_cipher_in_bytes != 0 && job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes != 0 && job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                /* Per RFC 7539, max cipher size is (2^32 - 1) x 64 */
                if (job->msg_len_to_cipher_in_bytes > ((1ULL << 38) - 64)) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(12)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                break;
        case IMB_CIPHER_SNOW_V_AEAD:
        case IMB_CIPHER_SNOW_V:
                if (job->msg_len_to_cipher_in_bytes != 0 && job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes != 0 && job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->enc_keys == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->key_len_in_bytes != UINT64_C(32)) {
                        imb_set_errno(state, IMB_ERR_JOB_KEY_LEN);
                        return 1;
                }
                if (job->iv_len_in_bytes != UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                if (job->cipher_mode == IMB_CIPHER_SNOW_V_AEAD &&
                    job->hash_alg != IMB_AUTH_SNOW_V_AEAD) {
                        imb_set_errno(state, IMB_ERR_HASH_ALGO);
                        return 1;
                }
                break;
        default:
                imb_set_errno(state, IMB_ERR_CIPH_MODE);
                return 1;
        }

        switch (job->hash_alg) {
        case IMB_AUTH_HMAC_SHA_1:
        case IMB_AUTH_MD5:
        case IMB_AUTH_HMAC_SHA_224:
        case IMB_AUTH_HMAC_SHA_256:
        case IMB_AUTH_HMAC_SHA_384:
        case IMB_AUTH_HMAC_SHA_512:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg] &&
                    job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_fips[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes == 0 ||
                    job->msg_len_to_hash_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                break;
        case IMB_AUTH_AES_XCBC:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg] &&
                    job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_fips[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                break;
        case IMB_AUTH_NULL:
                break;
        case IMB_AUTH_CRC32_ETHERNET_FCS:
        case IMB_AUTH_CRC32_SCTP:
        case IMB_AUTH_CRC32_WIMAX_OFDMA_DATA:
        case IMB_AUTH_CRC24_LTE_A:
        case IMB_AUTH_CRC24_LTE_B:
        case IMB_AUTH_CRC16_X25:
        case IMB_AUTH_CRC16_FP_DATA:
        case IMB_AUTH_CRC11_FP_HEADER:
        case IMB_AUTH_CRC10_IUUP_DATA:
        case IMB_AUTH_CRC8_WIMAX_OFDMA_HCS:
        case IMB_AUTH_CRC7_FP_HEADER:
        case IMB_AUTH_CRC6_IUUP_HEADER:
                if (job->src == NULL && job->msg_len_to_hash_in_bytes != 0) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                break;
        case IMB_AUTH_AES_GMAC:
                if (job->auth_tag_output_len_in_bytes < UINT64_C(1) ||
                    job->auth_tag_output_len_in_bytes > UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if ((job->u.GCM.aad_len_in_bytes > 0) &&
                    (job->u.GCM.aad == NULL)) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AAD);
                        return 1;
                }
                if (job->cipher_mode != IMB_CIPHER_GCM) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                /*
                 * msg_len_to_hash_in_bytes not checked against zero.
                 * It is not used for AES-GCM & GMAC - see
                 * SUBMIT_JOB_AES_GCM_ENC and SUBMIT_JOB_AES_GCM_DEC functions.
                 */
                break;
        case IMB_AUTH_GCM_SGL:
                if (job->cipher_mode != IMB_CIPHER_GCM_SGL) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->u.GCM.ctx == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SGL_CTX);
                        return 1;
                }
                if (job->sgl_state == IMB_SGL_COMPLETE) {
                        if (job->auth_tag_output_len_in_bytes < UINT64_C(1) ||
                            job->auth_tag_output_len_in_bytes > UINT64_C(16)) {
                                imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                                return 1;
                        }
                        if (job->auth_tag_output == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                                return 1;
                        }
                }
                if (job->sgl_state == IMB_SGL_INIT) {
                        if ((job->u.GCM.aad_len_in_bytes > 0) &&
                            (job->u.GCM.aad == NULL)) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_AAD);
                                return 1;
                        }
                }
                break;
        case IMB_AUTH_AES_GMAC_128:
        case IMB_AUTH_AES_GMAC_192:
        case IMB_AUTH_AES_GMAC_256:
                if (job->auth_tag_output_len_in_bytes < UINT64_C(1) ||
                    job->auth_tag_output_len_in_bytes > UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                /* This GMAC mode is to be used as stand-alone,
                 * not combined with GCM */
                if (job->cipher_mode == IMB_CIPHER_GCM) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->u.GMAC._key == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->u.GMAC._iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->u.GMAC.iv_len_in_bytes == 0) {
                        imb_set_errno(state, IMB_ERR_JOB_IV_LEN);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes != 0 && job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                break;
        case IMB_AUTH_CUSTOM:
                if (job->hash_func == NULL) {
                        imb_set_errno(state, EFAULT);
                        return 1;
                }
                break;
        case IMB_AUTH_AES_CCM:
                if (job->msg_len_to_hash_in_bytes != 0 && job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->u.CCM.aad_len_in_bytes > 46) {
                        /* 3 x AES_BLOCK - 2 bytes for AAD len */
                        imb_set_errno(state, IMB_ERR_JOB_AAD_LEN);
                        return 1;
                }
                if ((job->u.CCM.aad_len_in_bytes > 0) &&
                    (job->u.CCM.aad == NULL)) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AAD);
                        return 1;
                }
                /* M can be any even number from 4 to 16 */
                if (job->auth_tag_output_len_in_bytes < UINT64_C(4) ||
                    job->auth_tag_output_len_in_bytes > UINT64_C(16) ||
                    ((job->auth_tag_output_len_in_bytes & 1) != 0)) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                                return 1;
                }
                if (job->cipher_mode != IMB_CIPHER_CCM) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                /*
                 * AES-CCM allows for only one message for
                 * cipher and authentication.
                 * AAD can be used to extend authentication over
                 * clear text fields.
                 */
                if (job->msg_len_to_cipher_in_bytes !=
                    job->msg_len_to_hash_in_bytes) {
                        imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                        return 1;
                }
                if (job->cipher_start_src_offset_in_bytes !=
                    job->hash_start_src_offset_in_bytes) {
                        imb_set_errno(state, IMB_ERR_JOB_SRC_OFFSET);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                break;
        case IMB_AUTH_AES_CMAC:
        case IMB_AUTH_AES_CMAC_BITLEN:
        case IMB_AUTH_AES_CMAC_256:
                /*
                 * WARNING: When using IMB_AUTH_AES_CMAC_BITLEN, length of
                 * message is passed in bits, using job->msg_len_to_hash_in_bits
                 * (unlike "normal" IMB_AUTH_AES_CMAC, where is passed in bytes,
                 * using job->msg_len_to_hash_in_bytes).
                 */
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if ((job->u.CMAC._key_expanded == NULL) ||
                    (job->u.CMAC._skey1 == NULL) ||
                    (job->u.CMAC._skey2 == NULL)) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                /* T is 128 bits but 96 bits is also allowed due to
                 * IPsec use case (RFC 4494) and 32 bits for CMAC 3GPP.
                 */
                if (job->auth_tag_output_len_in_bytes < UINT64_C(4) ||
                    job->auth_tag_output_len_in_bytes > UINT64_C(16)) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                break;
        case IMB_AUTH_SHA_1:
        case IMB_AUTH_SHA_224:
        case IMB_AUTH_SHA_256:
        case IMB_AUTH_SHA_384:
        case IMB_AUTH_SHA_512:
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                break;
        case IMB_AUTH_PON_CRC_BIP:
                /*
                 * Authentication tag in PON is BIP 32-bit value only
                 * CRC is done together with cipher,
                 * its initial value is read from the source buffer and
                 * updated value put into the destination buffer.
                 * - msg_len_to_hash_in_bytes is aligned to 4 bytes
                 */
                if (((job->msg_len_to_hash_in_bytes & UINT64_C(3)) != 0) ||
                    (job->msg_len_to_hash_in_bytes < UINT64_C(8)) ||
                    (job->msg_len_to_hash_in_bytes > max_pon_len)) {
                        /*
                         * Length aligned to 4 bytes (and at least 8 bytes,
                         * including 8-byte XGEM header and no more
                         * than max length)
                         */
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        /* 64-bits:
                         * - BIP 32-bits
                         * - CRC 32-bits
                         */
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->cipher_mode != IMB_CIPHER_PON_AES_CNTR) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                break;
        case IMB_AUTH_ZUC_EIA3_BITLEN:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if ((job->msg_len_to_hash_in_bits < ZUC_MIN_BITLEN) ||
                    (job->msg_len_to_hash_in_bits > ZUC_MAX_BITLEN)) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                if (job->u.ZUC_EIA3._key == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->u.ZUC_EIA3._iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                break;
        case IMB_AUTH_ZUC256_EIA3_BITLEN:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if ((job->msg_len_to_hash_in_bits < ZUC_MIN_BITLEN) ||
                    (job->msg_len_to_hash_in_bits > ZUC_MAX_BITLEN)) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                if (job->u.ZUC_EIA3._key == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->u.ZUC_EIA3._iv == NULL) {
                        /* If 25-byte IV is NULL, check 23-byte IV */
                        if (job->u.ZUC_EIA3._iv23 == NULL) {
                                imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                                return 1;
                        }
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                break;
        case IMB_AUTH_DOCSIS_CRC32:
                /**
                 * Use only in combination with DOCSIS_SEC_BPI.
                 * Assumptions about Ethernet PDU carried over DOCSIS:
                 * - cipher_start_src_offset_in_bytes <=
                 *       (hash_start_src_offset_in_bytes + 12)
                 * - msg_len_to_cipher_in_bytes <=
                 *       (msg_len_to_hash_in_bytes - 12 + 4)
                 * - @note: in-place operation allowed only
                 * - authentication tag size is 4 bytes
                 * - @note: in encrypt direction, computed CRC value is put into
                 *   the source buffer
                 * - encrypt chain order: hash, cipher
                 * - decrypt chain order: cipher, hash
                 */
                if (job->cipher_mode != IMB_CIPHER_DOCSIS_SEC_BPI) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->msg_len_to_cipher_in_bytes &&
                    job->msg_len_to_hash_in_bytes) {
                        const uint64_t ciph_adjust =
                                IMB_DOCSIS_CRC32_MIN_ETH_PDU_SIZE -
                                2 - /* ETH TYPE */
                                IMB_DOCSIS_CRC32_TAG_SIZE;

                        if ((job->msg_len_to_cipher_in_bytes + ciph_adjust) >
                            job->msg_len_to_hash_in_bytes) {
                                imb_set_errno(state, IMB_ERR_JOB_CIPH_LEN);
                                return 1;
                        }
                        if (job->cipher_start_src_offset_in_bytes <
                            (job->hash_start_src_offset_in_bytes + 12)) {
                                imb_set_errno(state, IMB_ERR_JOB_SRC_OFFSET);
                                return 1;
                        }
                }
                if (job->msg_len_to_hash_in_bytes > MB_MAX_LEN16) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        /* Ethernet FCS CRC is 32-bits */
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if ((job->cipher_direction == IMB_DIR_ENCRYPT &&
                     job->chain_order != IMB_ORDER_HASH_CIPHER) ||
                    (job->cipher_direction == IMB_DIR_DECRYPT &&
                     job->chain_order != IMB_ORDER_CIPHER_HASH)) {
                        imb_set_errno(state, IMB_ERR_JOB_CHAIN_ORDER);
                        return 1;
                }
                break;
        case IMB_AUTH_SNOW3G_UIA2_BITLEN:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if ((job->msg_len_to_hash_in_bits == 0) ||
                    (job->msg_len_to_hash_in_bits > SNOW3G_MAX_BITLEN)) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                if (job->u.SNOW3G_UIA2._key == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->u.SNOW3G_UIA2._iv == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_IV);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                break;
        case IMB_AUTH_KASUMI_UIA1:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                /*
                 * KASUMI-UIA1 needs to be at least 8 bytes
                 * (IV + direction bit + '1' + 0s to align to byte boundary)
                 */
                if ((job->msg_len_to_hash_in_bytes <
                     (IMB_KASUMI_BLOCK_SIZE + 1)) ||
                    (job->msg_len_to_hash_in_bytes >
                     (KASUMI_MAX_LEN / BYTESIZE))) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_LEN);
                        return 1;
                }
                if (job->u.KASUMI_UIA1._key == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_KEY);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                break;
        case IMB_AUTH_POLY1305:
                if (job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->u.POLY1305._key == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH_KEY);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                break;
        case IMB_AUTH_CHACHA20_POLY1305:
                if (job->msg_len_to_hash_in_bytes != 0 && job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes != 0 && job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->cipher_mode != IMB_CIPHER_CHACHA20_POLY1305) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->u.CHACHA20_POLY1305.aad == NULL &&
                    job->u.CHACHA20_POLY1305.aad_len_in_bytes > 0) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AAD);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                break;
        case IMB_AUTH_CHACHA20_POLY1305_SGL:
                if (job->msg_len_to_hash_in_bytes != 0 && job->src == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SRC);
                        return 1;
                }
                if (job->msg_len_to_hash_in_bytes != 0 && job->dst == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_DST);
                        return 1;
                }
                if (job->cipher_mode != IMB_CIPHER_CHACHA20_POLY1305_SGL) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                if (job->u.CHACHA20_POLY1305.aad == NULL &&
                    job->u.CHACHA20_POLY1305.aad_len_in_bytes > 0) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AAD);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->u.CHACHA20_POLY1305.ctx == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_SGL_CTX);
                        return 1;
                }
                break;
        case IMB_AUTH_SNOW_V_AEAD:
                if ((job->u.SNOW_V_AEAD.aad_len_in_bytes > 0) &&
                    (job->u.SNOW_V_AEAD.aad == NULL)) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AAD);
                        return 1;
                }
                if (job->auth_tag_output == NULL) {
                        imb_set_errno(state, IMB_ERR_JOB_NULL_AUTH);
                        return 1;
                }
                if (job->auth_tag_output_len_in_bytes !=
                    auth_tag_len_ipsec[job->hash_alg]) {
                        imb_set_errno(state, IMB_ERR_JOB_AUTH_TAG_LEN);
                        return 1;
                }
                if (job->cipher_mode != IMB_CIPHER_SNOW_V_AEAD) {
                        imb_set_errno(state, IMB_ERR_CIPH_MODE);
                        return 1;
                }
                break;
        default:
                imb_set_errno(state, IMB_ERR_HASH_ALGO);
                return 1;
        }
        return 0;
}

__forceinline
IMB_JOB *SUBMIT_JOB_AES(IMB_MGR *state, IMB_JOB *job)
{
	if (job->cipher_direction == IMB_DIR_ENCRYPT)
		job = SUBMIT_JOB_AES_ENC(state, job);
	else
		job = SUBMIT_JOB_AES_DEC(state, job);

	return job;
}

__forceinline
IMB_JOB *FLUSH_JOB_AES(IMB_MGR *state, IMB_JOB *job)
{
	if (job->cipher_direction == IMB_DIR_ENCRYPT)
		job = FLUSH_JOB_AES_ENC(state, job);
	else
		job = FLUSH_JOB_AES_DEC(state, job);

	return job;
}

/* submit a half-completed job, based on the status */
__forceinline
IMB_JOB *RESUBMIT_JOB(IMB_MGR *state, IMB_JOB *job)
{
        while (job != NULL && job->status < IMB_STATUS_COMPLETED) {
                if (job->status == IMB_STATUS_COMPLETED_AUTH)
                        job = SUBMIT_JOB_AES(state, job);
                else /* assumed job->status = IMB_STATUS_COMPLETED_CIPHER */
                        job = SUBMIT_JOB_HASH(state, job);
        }

	return job;
}

__forceinline
IMB_JOB *submit_new_job(IMB_MGR *state, IMB_JOB *job)
{
	if (job->chain_order == IMB_ORDER_CIPHER_HASH)
		job = SUBMIT_JOB_AES(state, job);
	else
		job = SUBMIT_JOB_HASH(state, job);

        job = RESUBMIT_JOB(state, job);
	return job;
}

__forceinline
void complete_job(IMB_MGR *state, IMB_JOB *job)
{
        if (job->chain_order == IMB_ORDER_CIPHER_HASH) {
                /* while() loop optimized for cipher_hash order */
                while (job->status < IMB_STATUS_COMPLETED) {
                        IMB_JOB *tmp = FLUSH_JOB_AES(state, job);

                        if (tmp == NULL)
                                tmp = FLUSH_JOB_HASH(state, job);

                        (void) RESUBMIT_JOB(state, tmp);
                }
        } else {
                /* while() loop optimized for hash_cipher order */
                while (job->status < IMB_STATUS_COMPLETED) {
                        IMB_JOB *tmp = FLUSH_JOB_HASH(state, job);

                        if (tmp == NULL)
                                tmp = FLUSH_JOB_AES(state, job);

                        (void) RESUBMIT_JOB(state, tmp);
                }
        }
}

__forceinline
IMB_JOB *
submit_job_and_check(IMB_MGR *state, const int run_check)
{
        IMB_JOB *job = NULL;
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);

        SAVE_XMMS(xmm_save);
#endif

        job = JOBS(state, state->next_job);

        if (run_check) {
                if (is_job_invalid(state, job)) {
                        job->status = IMB_STATUS_INVALID_ARGS;
                } else {
                        job->status = IMB_STATUS_BEING_PROCESSED;
                        job = submit_new_job(state, job);
                }
        } else {
                job->status = IMB_STATUS_BEING_PROCESSED;
                job = submit_new_job(state, job);
        }

        if (state->earliest_job < 0) {
                /* state was previously empty */
                if (job == NULL)
                        state->earliest_job = state->next_job;
                ADV_JOBS(&state->next_job);
                goto exit;
        }

        ADV_JOBS(&state->next_job);

        if (state->earliest_job == state->next_job) {
                /* Full */
                job = JOBS(state, state->earliest_job);
                complete_job(state, job);
                ADV_JOBS(&state->earliest_job);
                goto exit;
        }

        /* not full */
        job = JOBS(state, state->earliest_job);
        if (job->status < IMB_STATUS_COMPLETED) {
                job = NULL;
                goto exit;
        }

        ADV_JOBS(&state->earliest_job);
exit:
#ifdef SAFE_DATA
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
        return job;
}

IMB_JOB *
SUBMIT_JOB(IMB_MGR *state)
{
        /* reset error status */
        imb_set_errno(state, 0);

#ifdef SAFE_PARAM
        if (state == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_MBMGR);
                return NULL;
        }
#endif

        return submit_job_and_check(state, 1);
}

IMB_JOB *
SUBMIT_JOB_NOCHECK(IMB_MGR *state)
{
        /* reset error status */
        imb_set_errno(state, 0);

#ifdef SAFE_PARAM
        if (state == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_MBMGR);
                return NULL;
        }
#endif

        return submit_job_and_check(state, 0);
}

IMB_JOB *
FLUSH_JOB(IMB_MGR *state)
{
        /* reset error status */
        imb_set_errno(state, 0);

#ifdef SAFE_PARAM
        if (state == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_MBMGR);
                return NULL;
        }
#endif
        IMB_JOB *job;
#ifndef LINUX
        DECLARE_ALIGNED(imb_uint128_t xmm_save[10], 16);
#endif
        if (state->earliest_job < 0)
                return NULL; /* empty */

#ifndef LINUX
        SAVE_XMMS(xmm_save);
#endif
        job = JOBS(state, state->earliest_job);
        complete_job(state, job);

        ADV_JOBS(&state->earliest_job);

        if (state->earliest_job == state->next_job)
                state->earliest_job = -1; /* becomes empty */

#ifdef SAFE_DATA
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

#ifndef LINUX
        RESTORE_XMMS(xmm_save);
#endif
        return job;
}

/* ========================================================================= */
/* ========================================================================= */

uint32_t
QUEUE_SIZE(IMB_MGR *state)
{
        /* reset error status */
        imb_set_errno(state, 0);

#ifdef SAFE_PARAM
        if (state == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_MBMGR);
                return 0;
        }
#endif
        int a, b;

        if (state->earliest_job < 0)
                return 0;
        a = state->next_job / sizeof(IMB_JOB);
        b = state->earliest_job / sizeof(IMB_JOB);
        return ((a-b) & (IMB_MAX_JOBS-1));
}

IMB_JOB *
GET_COMPLETED_JOB(IMB_MGR *state)
{
        /* reset error status */
        imb_set_errno(state, 0);

#ifdef SAFE_PARAM
        if (state == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_MBMGR);
                return NULL;
        }
#endif
        IMB_JOB *job;

        if (state->earliest_job < 0)
                return NULL;

        job = JOBS(state, state->earliest_job);
        if (job->status < IMB_STATUS_COMPLETED)
                return NULL;

        ADV_JOBS(&state->earliest_job);

        if (state->earliest_job == state->next_job)
                state->earliest_job = -1;

        return job;
}

IMB_JOB *
GET_NEXT_JOB(IMB_MGR *state)
{
        /* reset error status */
        imb_set_errno(state, 0);

#ifdef SAFE_PARAM
        if (state == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_MBMGR);
                return NULL;
        }
#endif

        return JOBS(state, state->next_job);
}

#endif /* MB_MGR_CODE_H */
