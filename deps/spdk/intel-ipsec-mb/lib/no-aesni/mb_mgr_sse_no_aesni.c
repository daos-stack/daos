/*******************************************************************************
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
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLEAR_SCRATCH_SIMD_REGS clear_scratch_xmms_sse

#include "intel-ipsec-mb.h"
#include "include/ipsec_ooo_mgr.h"
#include "include/kasumi_internal.h"
#include "include/zuc_internal.h"
#include "include/snow3g.h"
#include "include/chacha20_poly1305.h"

#include "include/save_xmms.h"
#include "include/asm.h"
#include "include/des.h"
#include "include/gcm.h"
#include "include/noaesni.h"
#include "include/error.h"

/* ====================================================================== */

IMB_JOB *submit_job_aes128_enc_sse_no_aesni(MB_MGR_AES_OOO *state,
                                                 IMB_JOB *job);
IMB_JOB *flush_job_aes128_enc_sse_no_aesni(MB_MGR_AES_OOO *state);

IMB_JOB *submit_job_aes192_enc_sse_no_aesni(MB_MGR_AES_OOO *state,
                                                 IMB_JOB *job);
IMB_JOB *flush_job_aes192_enc_sse_no_aesni(MB_MGR_AES_OOO *state);

IMB_JOB *submit_job_aes256_enc_sse_no_aesni(MB_MGR_AES_OOO *state,
                                                 IMB_JOB *job);
IMB_JOB *flush_job_aes256_enc_sse_no_aesni(MB_MGR_AES_OOO *state);

IMB_JOB *submit_job_hmac_sse(MB_MGR_HMAC_SHA_1_OOO *state,
                                  IMB_JOB *job);
IMB_JOB *flush_job_hmac_sse(MB_MGR_HMAC_SHA_1_OOO *state);

IMB_JOB *submit_job_hmac_sha_224_sse(MB_MGR_HMAC_SHA_256_OOO *state,
                                          IMB_JOB *job);
IMB_JOB *flush_job_hmac_sha_224_sse(MB_MGR_HMAC_SHA_256_OOO *state);

IMB_JOB *submit_job_hmac_sha_256_sse(MB_MGR_HMAC_SHA_256_OOO *state,
                                          IMB_JOB *job);
IMB_JOB *flush_job_hmac_sha_256_sse(MB_MGR_HMAC_SHA_256_OOO *state);

IMB_JOB *submit_job_hmac_sha_384_sse(MB_MGR_HMAC_SHA_512_OOO *state,
                                          IMB_JOB *job);
IMB_JOB *flush_job_hmac_sha_384_sse(MB_MGR_HMAC_SHA_512_OOO *state);

IMB_JOB *submit_job_hmac_sha_512_sse(MB_MGR_HMAC_SHA_512_OOO *state,
                                          IMB_JOB *job);
IMB_JOB *flush_job_hmac_sha_512_sse(MB_MGR_HMAC_SHA_512_OOO *state);

IMB_JOB *submit_job_hmac_md5_sse(MB_MGR_HMAC_MD5_OOO *state,
                                      IMB_JOB *job);
IMB_JOB *flush_job_hmac_md5_sse(MB_MGR_HMAC_MD5_OOO *state);

IMB_JOB *submit_job_aes_xcbc_sse_no_aesni(MB_MGR_AES_XCBC_OOO *state,
                                               IMB_JOB *job);
IMB_JOB *flush_job_aes_xcbc_sse_no_aesni(MB_MGR_AES_XCBC_OOO *state);

IMB_JOB *submit_job_aes128_cmac_auth_sse_no_aesni(MB_MGR_CMAC_OOO *state,
                                                    IMB_JOB *job);
IMB_JOB *flush_job_aes128_cmac_auth_sse_no_aesni(MB_MGR_CMAC_OOO *state);

IMB_JOB *submit_job_aes256_cmac_auth_sse_no_aesni(MB_MGR_CMAC_OOO *state,
                                                    IMB_JOB *job);
IMB_JOB *flush_job_aes256_cmac_auth_sse_no_aesni(MB_MGR_CMAC_OOO *state);

IMB_JOB *submit_job_aes128_ccm_auth_sse_no_aesni(MB_MGR_CCM_OOO *state,
                                                 IMB_JOB *job);

IMB_JOB *flush_job_aes128_ccm_auth_sse_no_aesni(MB_MGR_CCM_OOO *state);

IMB_JOB *submit_job_aes256_ccm_auth_sse_no_aesni(MB_MGR_CCM_OOO *state,
                                                 IMB_JOB *job);

IMB_JOB *flush_job_aes256_ccm_auth_sse_no_aesni(MB_MGR_CCM_OOO *state);

IMB_JOB *submit_job_aes_cntr_sse_no_aesni(IMB_JOB *job);

IMB_JOB *submit_job_aes_cntr_bit_sse_no_aesni(IMB_JOB *job);

IMB_JOB *submit_job_zuc_eea3_sse_no_aesni(MB_MGR_ZUC_OOO *state,
                                               IMB_JOB *job);
IMB_JOB *flush_job_zuc_eea3_sse_no_aesni(MB_MGR_ZUC_OOO *state);

IMB_JOB *submit_job_zuc256_eea3_sse_no_aesni(MB_MGR_ZUC_OOO *state,
                                               IMB_JOB *job);
IMB_JOB *flush_job_zuc256_eea3_sse_no_aesni(MB_MGR_ZUC_OOO *state);

IMB_JOB *submit_job_zuc_eia3_sse_no_aesni(MB_MGR_ZUC_OOO *state,
                                               IMB_JOB *job);
IMB_JOB *flush_job_zuc_eia3_sse_no_aesni(MB_MGR_ZUC_OOO *state);

IMB_JOB *submit_job_zuc256_eia3_sse_no_aesni(MB_MGR_ZUC_OOO *state,
                                             IMB_JOB *job);
IMB_JOB *flush_job_zuc256_eia3_sse_no_aesni(MB_MGR_ZUC_OOO *state);

uint32_t hec_32_sse_no_aesni(const uint8_t *in);
uint64_t hec_64_sse_no_aesni(const uint8_t *in);

IMB_JOB *submit_job_aes128_cbcs_1_9_enc_sse_no_aesni(MB_MGR_AES_OOO *state,
                                                     IMB_JOB *job);
IMB_JOB *flush_job_aes128_cbcs_1_9_enc_sse_no_aesni(MB_MGR_AES_OOO *state);

IMB_JOB *submit_job_chacha20_enc_dec_sse(IMB_JOB *job);

void *poly1305_mac_scalar(IMB_JOB *job);

IMB_JOB *snow_v_sse_no_aesni(IMB_JOB *job);
IMB_JOB *snow_v_aead_init_sse_no_aesni(IMB_JOB *job);

#define SAVE_XMMS               save_xmms
#define RESTORE_XMMS            restore_xmms

#define SUBMIT_JOB_AES128_ENC submit_job_aes128_enc_sse_no_aesni
#define SUBMIT_JOB_AES128_DEC submit_job_aes128_dec_sse_no_aesni
#define FLUSH_JOB_AES128_ENC  flush_job_aes128_enc_sse_no_aesni
#define SUBMIT_JOB_AES192_ENC submit_job_aes192_enc_sse_no_aesni
#define SUBMIT_JOB_AES192_DEC submit_job_aes192_dec_sse_no_aesni
#define FLUSH_JOB_AES192_ENC  flush_job_aes192_enc_sse_no_aesni
#define SUBMIT_JOB_AES256_ENC submit_job_aes256_enc_sse_no_aesni
#define SUBMIT_JOB_AES256_DEC submit_job_aes256_dec_sse_no_aesni
#define FLUSH_JOB_AES256_ENC  flush_job_aes256_enc_sse_no_aesni
#define SUBMIT_JOB_AES_ECB_128_ENC submit_job_aes_ecb_128_enc_sse_no_aesni
#define SUBMIT_JOB_AES_ECB_128_DEC submit_job_aes_ecb_128_dec_sse_no_aesni
#define SUBMIT_JOB_AES_ECB_192_ENC submit_job_aes_ecb_192_enc_sse_no_aesni
#define SUBMIT_JOB_AES_ECB_192_DEC submit_job_aes_ecb_192_dec_sse_no_aesni
#define SUBMIT_JOB_AES_ECB_256_ENC submit_job_aes_ecb_256_enc_sse_no_aesni
#define SUBMIT_JOB_AES_ECB_256_DEC submit_job_aes_ecb_256_dec_sse_no_aesni
#define SUBMIT_JOB_HMAC       submit_job_hmac_sse
#define FLUSH_JOB_HMAC        flush_job_hmac_sse
#define SUBMIT_JOB_HMAC_NI    submit_job_hmac_sse
#define FLUSH_JOB_HMAC_NI     flush_job_hmac_sse
#define SUBMIT_JOB_HMAC_SHA_224       submit_job_hmac_sha_224_sse
#define FLUSH_JOB_HMAC_SHA_224        flush_job_hmac_sha_224_sse
#define SUBMIT_JOB_HMAC_SHA_224_NI    submit_job_hmac_sha_224_sse
#define FLUSH_JOB_HMAC_SHA_224_NI     flush_job_hmac_sha_224_sse
#define SUBMIT_JOB_HMAC_SHA_256       submit_job_hmac_sha_256_sse
#define FLUSH_JOB_HMAC_SHA_256        flush_job_hmac_sha_256_sse
#define SUBMIT_JOB_HMAC_SHA_256_NI    submit_job_hmac_sha_256_sse
#define FLUSH_JOB_HMAC_SHA_256_NI     flush_job_hmac_sha_256_sse
#define SUBMIT_JOB_HMAC_SHA_384       submit_job_hmac_sha_384_sse
#define FLUSH_JOB_HMAC_SHA_384        flush_job_hmac_sha_384_sse
#define SUBMIT_JOB_HMAC_SHA_512       submit_job_hmac_sha_512_sse
#define FLUSH_JOB_HMAC_SHA_512        flush_job_hmac_sha_512_sse
#define SUBMIT_JOB_HMAC_MD5   submit_job_hmac_md5_sse
#define FLUSH_JOB_HMAC_MD5    flush_job_hmac_md5_sse
#define SUBMIT_JOB_AES_XCBC   submit_job_aes_xcbc_sse_no_aesni
#define FLUSH_JOB_AES_XCBC    flush_job_aes_xcbc_sse_no_aesni

#define SUBMIT_JOB_AES_CNTR   submit_job_aes_cntr_sse_no_aesni
#define SUBMIT_JOB_AES_CNTR_BIT   submit_job_aes_cntr_bit_sse_no_aesni

#define SUBMIT_JOB_ZUC_EEA3   submit_job_zuc_eea3_sse_no_aesni
#define FLUSH_JOB_ZUC_EEA3    flush_job_zuc_eea3_sse_no_aesni
#define SUBMIT_JOB_ZUC_EIA3   submit_job_zuc_eia3_sse_no_aesni
#define FLUSH_JOB_ZUC_EIA3    flush_job_zuc_eia3_sse_no_aesni
#define SUBMIT_JOB_ZUC256_EEA3   submit_job_zuc256_eea3_sse_no_aesni
#define FLUSH_JOB_ZUC256_EEA3    flush_job_zuc256_eea3_sse_no_aesni
#define SUBMIT_JOB_ZUC256_EIA3   submit_job_zuc256_eia3_sse_no_aesni
#define FLUSH_JOB_ZUC256_EIA3    flush_job_zuc256_eia3_sse_no_aesni

#define AES_CBC_DEC_128       aes_cbc_dec_128_sse_no_aesni
#define AES_CBC_DEC_192       aes_cbc_dec_192_sse_no_aesni
#define AES_CBC_DEC_256       aes_cbc_dec_256_sse_no_aesni

#define AES_CNTR_128       aes_cntr_128_sse_no_aesni
#define AES_CNTR_192       aes_cntr_192_sse_no_aesni
#define AES_CNTR_256       aes_cntr_256_sse_no_aesni

#define AES_CNTR_CCM_128   aes_cntr_ccm_128_sse_no_aesni
#define AES_CNTR_CCM_256   aes_cntr_ccm_256_sse_no_aesni

#define AES_ECB_ENC_128       aes_ecb_enc_128_sse_no_aesni
#define AES_ECB_ENC_192       aes_ecb_enc_192_sse_no_aesni
#define AES_ECB_ENC_256       aes_ecb_enc_256_sse_no_aesni
#define AES_ECB_DEC_128       aes_ecb_dec_128_sse_no_aesni
#define AES_ECB_DEC_192       aes_ecb_dec_192_sse_no_aesni
#define AES_ECB_DEC_256       aes_ecb_dec_256_sse_no_aesni

#define SUBMIT_JOB_PON_ENC        submit_job_pon_enc_sse_no_aesni
#define SUBMIT_JOB_PON_DEC        submit_job_pon_dec_sse_no_aesni
#define SUBMIT_JOB_PON_ENC_NO_CTR submit_job_pon_enc_no_ctr_sse_no_aesni
#define SUBMIT_JOB_PON_DEC_NO_CTR submit_job_pon_dec_no_ctr_sse_no_aesni

#define AES_GCM_DEC_128   aes_gcm_dec_128_sse_no_aesni
#define AES_GCM_ENC_128   aes_gcm_enc_128_sse_no_aesni
#define AES_GCM_DEC_192   aes_gcm_dec_192_sse_no_aesni
#define AES_GCM_ENC_192   aes_gcm_enc_192_sse_no_aesni
#define AES_GCM_DEC_256   aes_gcm_dec_256_sse_no_aesni
#define AES_GCM_ENC_256   aes_gcm_enc_256_sse_no_aesni

#define AES_GCM_DEC_IV_128   aes_gcm_dec_var_iv_128_sse_no_aesni
#define AES_GCM_ENC_IV_128   aes_gcm_enc_var_iv_128_sse_no_aesni
#define AES_GCM_DEC_IV_192   aes_gcm_dec_var_iv_192_sse_no_aesni
#define AES_GCM_ENC_IV_192   aes_gcm_enc_var_iv_192_sse_no_aesni
#define AES_GCM_DEC_IV_256   aes_gcm_dec_var_iv_256_sse_no_aesni
#define AES_GCM_ENC_IV_256   aes_gcm_enc_var_iv_256_sse_no_aesni

#define SUBMIT_JOB_AES_GCM_DEC submit_job_aes_gcm_dec_sse_no_aesni
#define SUBMIT_JOB_AES_GCM_ENC submit_job_aes_gcm_enc_sse_no_aesni

/* ====================================================================== */

#define SUBMIT_JOB         submit_job_sse_no_aesni
#define FLUSH_JOB          flush_job_sse_no_aesni
#define SUBMIT_JOB_NOCHECK submit_job_nocheck_sse_no_aesni
#define GET_NEXT_JOB       get_next_job_sse_no_aesni
#define GET_COMPLETED_JOB  get_completed_job_sse_no_aesni

#define SUBMIT_JOB_AES128_DEC submit_job_aes128_dec_sse_no_aesni
#define SUBMIT_JOB_AES192_DEC submit_job_aes192_dec_sse_no_aesni
#define SUBMIT_JOB_AES256_DEC submit_job_aes256_dec_sse_no_aesni
#define QUEUE_SIZE queue_size_sse_no_aesni

/* ====================================================================== */

#define SUBMIT_JOB_AES_ENC SUBMIT_JOB_AES_ENC_SSE
#define FLUSH_JOB_AES_ENC  FLUSH_JOB_AES_ENC_SSE
#define SUBMIT_JOB_AES_DEC SUBMIT_JOB_AES_DEC_SSE
#define SUBMIT_JOB_HASH    SUBMIT_JOB_HASH_SSE
#define FLUSH_JOB_HASH     FLUSH_JOB_HASH_SSE

/* ====================================================================== */

#define AES_CFB_128_ONE    aes_cfb_128_one_sse_no_aesni
#define AES_CFB_256_ONE    aes_cfb_256_one_sse_no_aesni

void aes128_cbc_mac_x4_no_aesni(AES_ARGS *args, uint64_t len);

#define AES128_CBC_MAC     aes128_cbc_mac_x4_no_aesni

#define FLUSH_JOB_AES128_CCM_AUTH     flush_job_aes128_ccm_auth_sse_no_aesni
#define SUBMIT_JOB_AES128_CCM_AUTH    submit_job_aes128_ccm_auth_sse_no_aesni

#define FLUSH_JOB_AES256_CCM_AUTH     flush_job_aes256_ccm_auth_sse_no_aesni
#define SUBMIT_JOB_AES256_CCM_AUTH    submit_job_aes256_ccm_auth_sse_no_aesni

#define FLUSH_JOB_AES128_CMAC_AUTH    flush_job_aes128_cmac_auth_sse_no_aesni
#define SUBMIT_JOB_AES128_CMAC_AUTH   submit_job_aes128_cmac_auth_sse_no_aesni

#define FLUSH_JOB_AES256_CMAC_AUTH    flush_job_aes256_cmac_auth_sse_no_aesni
#define SUBMIT_JOB_AES256_CMAC_AUTH   submit_job_aes256_cmac_auth_sse_no_aesni

/* ====================================================================== */

#define SUBMIT_JOB_AES128_CBCS_1_9_ENC \
        submit_job_aes128_cbcs_1_9_enc_sse_no_aesni
#define FLUSH_JOB_AES128_CBCS_1_9_ENC  \
        flush_job_aes128_cbcs_1_9_enc_sse_no_aesni
#define SUBMIT_JOB_AES128_CBCS_1_9_DEC \
        submit_job_aes128_cbcs_1_9_dec_sse_no_aesni
#define AES_CBCS_1_9_DEC_128           \
        aes_cbcs_1_9_dec_128_sse_no_aesni
#define SUBMIT_JOB_CHACHA20_ENC_DEC submit_job_chacha20_enc_dec_sse
#define SUBMIT_JOB_CHACHA20_POLY1305 aead_chacha20_poly1305_sse
#define SUBMIT_JOB_CHACHA20_POLY1305_SGL aead_chacha20_poly1305_sgl_sse
#define POLY1305_MAC poly1305_mac_scalar

#define SUBMIT_JOB_SNOW_V snow_v_sse_no_aesni
#define SUBMIT_JOB_SNOW_V_AEAD snow_v_aead_init_sse_no_aesni

/* ====================================================================== */

uint32_t
ethernet_fcs_sse_no_aesni_local(const void *msg, const uint64_t len,
                                const void *tag_ouput);

#define ETHERNET_FCS ethernet_fcs_sse_no_aesni_local

uint32_t ethernet_fcs_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc16_x25_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc32_sctp_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc24_lte_a_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc24_lte_b_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc16_fp_data_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc11_fp_header_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc7_fp_header_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc10_iuup_data_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc6_iuup_header_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t
crc32_wimax_ofdma_data_sse_no_aesni(const void *msg, const uint64_t len);
uint32_t crc8_wimax_ofdma_hcs_sse_no_aesni(const void *msg, const uint64_t len);

/* ====================================================================== */

/*
 * GCM submit / flush API for SSE arch without AESNI
 */
static IMB_JOB *
submit_job_aes_gcm_dec_sse_no_aesni(IMB_MGR *state, IMB_JOB *job)
{
        DECLARE_ALIGNED(struct gcm_context_data ctx, 16);
        (void) state;

        if (16 == job->key_len_in_bytes) {
                AES_GCM_DEC_IV_128(job->dec_keys,
                                   &ctx, job->dst,
                                   job->src +
                                   job->cipher_start_src_offset_in_bytes,
                                   job->msg_len_to_cipher_in_bytes,
                                   job->iv, job->iv_len_in_bytes,
                                   job->u.GCM.aad,
                                   job->u.GCM.aad_len_in_bytes,
                                   job->auth_tag_output,
                                   job->auth_tag_output_len_in_bytes);
        } else if (24 == job->key_len_in_bytes) {
                AES_GCM_DEC_IV_192(job->dec_keys,
                                   &ctx, job->dst,
                                   job->src +
                                   job->cipher_start_src_offset_in_bytes,
                                   job->msg_len_to_cipher_in_bytes,
                                   job->iv, job->iv_len_in_bytes,
                                   job->u.GCM.aad,
                                   job->u.GCM.aad_len_in_bytes,
                                   job->auth_tag_output,
                                   job->auth_tag_output_len_in_bytes);
        } else { /* assume 32 bytes */
                AES_GCM_DEC_IV_256(job->dec_keys,
                                   &ctx, job->dst,
                                   job->src +
                                   job->cipher_start_src_offset_in_bytes,
                                   job->msg_len_to_cipher_in_bytes,
                                   job->iv, job->iv_len_in_bytes,
                                   job->u.GCM.aad,
                                   job->u.GCM.aad_len_in_bytes,
                                   job->auth_tag_output,
                                   job->auth_tag_output_len_in_bytes);
        }

        job->status = IMB_STATUS_COMPLETED;
        return job;
}

static IMB_JOB *
submit_job_aes_gcm_enc_sse_no_aesni(IMB_MGR *state, IMB_JOB *job)
{
        DECLARE_ALIGNED(struct gcm_context_data ctx, 16);
        (void) state;

        if (16 == job->key_len_in_bytes) {
                AES_GCM_ENC_IV_128(job->enc_keys,
                                   &ctx, job->dst,
                                   job->src +
                                   job->cipher_start_src_offset_in_bytes,
                                   job->msg_len_to_cipher_in_bytes,
                                   job->iv, job->iv_len_in_bytes,
                                   job->u.GCM.aad,
                                   job->u.GCM.aad_len_in_bytes,
                                   job->auth_tag_output,
                                   job->auth_tag_output_len_in_bytes);
        } else if (24 == job->key_len_in_bytes) {
                AES_GCM_ENC_IV_192(job->enc_keys,
                                   &ctx, job->dst,
                                   job->src +
                                   job->cipher_start_src_offset_in_bytes,
                                   job->msg_len_to_cipher_in_bytes,
                                   job->iv, job->iv_len_in_bytes,
                                   job->u.GCM.aad,
                                   job->u.GCM.aad_len_in_bytes,
                                   job->auth_tag_output,
                                   job->auth_tag_output_len_in_bytes);
        } else { /* assume 32 bytes */
                AES_GCM_ENC_IV_256(job->enc_keys,
                                   &ctx, job->dst,
                                   job->src +
                                   job->cipher_start_src_offset_in_bytes,
                                   job->msg_len_to_cipher_in_bytes,
                                   job->iv, job->iv_len_in_bytes,
                                   job->u.GCM.aad,
                                   job->u.GCM.aad_len_in_bytes,
                                   job->auth_tag_output,
                                   job->auth_tag_output_len_in_bytes);
        }

        job->status = IMB_STATUS_COMPLETED;
        return job;
}

IMB_DLL_LOCAL IMB_JOB *
submit_job_aes_cntr_sse_no_aesni(IMB_JOB *job)
{
        if (16 == job->key_len_in_bytes)
                AES_CNTR_128(job->src + job->cipher_start_src_offset_in_bytes,
                             job->iv,
                             job->enc_keys,
                             job->dst,
                             job->msg_len_to_cipher_in_bytes,
                             job->iv_len_in_bytes);
        else if (24 == job->key_len_in_bytes)
                AES_CNTR_192(job->src + job->cipher_start_src_offset_in_bytes,
                             job->iv,
                             job->enc_keys,
                             job->dst,
                             job->msg_len_to_cipher_in_bytes,
                             job->iv_len_in_bytes);
        else /* assume 32 bytes */
                AES_CNTR_256(job->src + job->cipher_start_src_offset_in_bytes,
                             job->iv,
                             job->enc_keys,
                             job->dst,
                             job->msg_len_to_cipher_in_bytes,
                             job->iv_len_in_bytes);

        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

IMB_DLL_LOCAL IMB_JOB *
submit_job_aes_cntr_bit_sse_no_aesni(IMB_JOB *job)
{
        const uint64_t offset = job->cipher_start_src_offset_in_bytes;

        if (16 == job->key_len_in_bytes)
                aes_cntr_bit_128_sse_no_aesni(job->src + offset,
                                              job->iv,
                                              job->enc_keys,
                                              job->dst,
                                              job->msg_len_to_cipher_in_bits,
                                              job->iv_len_in_bytes);
        else if (24 == job->key_len_in_bytes)
                aes_cntr_bit_192_sse_no_aesni(job->src + offset,
                                              job->iv,
                                              job->enc_keys,
                                              job->dst,
                                              job->msg_len_to_cipher_in_bits,
                                              job->iv_len_in_bytes);
        else /* assume 32 bytes */
                aes_cntr_bit_256_sse_no_aesni(job->src + offset,
                                              job->iv,
                                              job->enc_keys,
                                              job->dst,
                                              job->msg_len_to_cipher_in_bits,
                                              job->iv_len_in_bytes);

        job->status |= IMB_STATUS_COMPLETED_CIPHER;
        return job;
}

/* ====================================================================== */

static void
reset_ooo_mgrs(IMB_MGR *state)
{
        unsigned int j;
        uint8_t *p;
        size_t size;
        MB_MGR_AES_OOO *aes128_ooo = state->aes128_ooo;
        MB_MGR_AES_OOO *aes192_ooo = state->aes192_ooo;
        MB_MGR_AES_OOO *aes256_ooo = state->aes256_ooo;
        MB_MGR_DOCSIS_AES_OOO *docsis128_sec_ooo = state->docsis128_sec_ooo;
        MB_MGR_DOCSIS_AES_OOO *docsis128_crc32_sec_ooo =
                                                state->docsis128_crc32_sec_ooo;
        MB_MGR_DOCSIS_AES_OOO *docsis256_sec_ooo = state->docsis256_sec_ooo;
        MB_MGR_DOCSIS_AES_OOO *docsis256_crc32_sec_ooo =
                                                state->docsis256_crc32_sec_ooo;
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
        MB_MGR_ZUC_OOO *zuc_eea3_ooo = state->zuc_eea3_ooo;
        MB_MGR_ZUC_OOO *zuc256_eea3_ooo = state->zuc256_eea3_ooo;
        MB_MGR_ZUC_OOO *zuc_eia3_ooo = state->zuc_eia3_ooo;
        MB_MGR_AES_OOO *aes128_cbcs_ooo = state->aes128_cbcs_ooo;
        MB_MGR_ZUC_OOO *zuc256_eia3_ooo = state->zuc256_eia3_ooo;

        /* Init AES out-of-order fields */
        memset(aes128_ooo->lens, 0xFF,
               sizeof(aes128_ooo->lens));
        memset(&aes128_ooo->lens[0], 0,
               sizeof(aes128_ooo->lens[0]) * 4);
        memset(aes128_ooo->job_in_lane, 0,
               sizeof(aes128_ooo->job_in_lane));
        aes128_ooo->unused_lanes = 0xF3210;
        aes128_ooo->num_lanes_inuse = 0;


        memset(aes192_ooo->lens, 0xFF,
               sizeof(aes192_ooo->lens));
        memset(&aes192_ooo->lens[0], 0,
               sizeof(aes192_ooo->lens[0]) * 4);
        memset(aes192_ooo->job_in_lane, 0,
               sizeof(aes192_ooo->job_in_lane));
        aes192_ooo->unused_lanes = 0xF3210;
        aes192_ooo->num_lanes_inuse = 0;


        memset(aes256_ooo->lens, 0xFF,
               sizeof(aes256_ooo->lens));
        memset(&aes256_ooo->lens[0], 0,
               sizeof(aes256_ooo->lens[0]) * 4);
        memset(aes256_ooo->job_in_lane, 0,
               sizeof(aes256_ooo->job_in_lane));
        aes256_ooo->unused_lanes = 0xF3210;
        aes256_ooo->num_lanes_inuse = 0;


        /* DOCSIS SEC BPI uses same settings as AES CBC */
        memset(docsis128_sec_ooo->lens, 0xFF,
               sizeof(docsis128_sec_ooo->lens));
        memset(&docsis128_sec_ooo->lens[0], 0,
               sizeof(docsis128_sec_ooo->lens[0]) * 4);
        memset(docsis128_sec_ooo->job_in_lane, 0,
               sizeof(docsis128_sec_ooo->job_in_lane));
        docsis128_sec_ooo->unused_lanes = 0xF3210;
        docsis128_sec_ooo->num_lanes_inuse = 0;

        memset(docsis128_crc32_sec_ooo->lens, 0xFF,
               sizeof(docsis128_crc32_sec_ooo->lens));
        memset(&docsis128_crc32_sec_ooo->lens[0], 0,
               sizeof(docsis128_crc32_sec_ooo->lens[0]) * 4);
        memset(docsis128_crc32_sec_ooo->job_in_lane, 0,
               sizeof(docsis128_crc32_sec_ooo->job_in_lane));
        docsis128_crc32_sec_ooo->unused_lanes = 0xF3210;
        docsis128_crc32_sec_ooo->num_lanes_inuse = 0;

        memset(docsis256_sec_ooo->lens, 0xFF,
               sizeof(docsis256_sec_ooo->lens));
        memset(&docsis256_sec_ooo->lens[0], 0,
               sizeof(docsis256_sec_ooo->lens[0]) * 4);
        memset(docsis256_sec_ooo->job_in_lane, 0,
               sizeof(docsis256_sec_ooo->job_in_lane));
        docsis256_sec_ooo->unused_lanes = 0xF3210;
        docsis256_sec_ooo->num_lanes_inuse = 0;

        memset(docsis256_crc32_sec_ooo->lens, 0xFF,
               sizeof(docsis256_crc32_sec_ooo->lens));
        memset(&docsis256_crc32_sec_ooo->lens[0], 0,
               sizeof(docsis256_crc32_sec_ooo->lens[0]) * 4);
        memset(docsis256_crc32_sec_ooo->job_in_lane, 0,
               sizeof(docsis256_crc32_sec_ooo->job_in_lane));
        docsis256_crc32_sec_ooo->unused_lanes = 0xF3210;
        docsis256_crc32_sec_ooo->num_lanes_inuse = 0;

        /* Init ZUC out-of-order fields */
        memset(zuc_eea3_ooo->lens, 0,
               sizeof(zuc_eea3_ooo->lens));
        memset(zuc_eea3_ooo->job_in_lane, 0,
               sizeof(zuc_eea3_ooo->job_in_lane));
        zuc_eea3_ooo->unused_lanes = 0xFF03020100;
        zuc_eea3_ooo->num_lanes_inuse = 0;
        memset(&zuc_eea3_ooo->state, 0,
               sizeof(zuc_eea3_ooo->state));
        zuc_eea3_ooo->init_not_done = 0;
        zuc_eea3_ooo->unused_lane_bitmask = 0x0f;

        memset(zuc_eia3_ooo->lens, 0xFF,
               sizeof(zuc_eia3_ooo->lens));
        memset(zuc_eia3_ooo->job_in_lane, 0,
               sizeof(zuc_eia3_ooo->job_in_lane));
        zuc_eia3_ooo->unused_lanes = 0xFF03020100;
        zuc_eia3_ooo->num_lanes_inuse = 0;
        memset(&zuc_eia3_ooo->state, 0,
               sizeof(zuc_eia3_ooo->state));
        zuc_eia3_ooo->init_not_done = 0;
        zuc_eia3_ooo->unused_lane_bitmask = 0x0f;

        memset(zuc256_eea3_ooo->lens, 0,
               sizeof(zuc256_eea3_ooo->lens));
        memset(zuc256_eea3_ooo->job_in_lane, 0,
               sizeof(zuc256_eea3_ooo->job_in_lane));
        zuc256_eea3_ooo->unused_lanes = 0xFF03020100;
        zuc256_eea3_ooo->num_lanes_inuse = 0;
        memset(&zuc256_eea3_ooo->state, 0,
               sizeof(zuc256_eea3_ooo->state));
        zuc256_eea3_ooo->init_not_done = 0;
        zuc256_eea3_ooo->unused_lane_bitmask = 0x0f;

        memset(zuc256_eia3_ooo->lens, 0xFF,
               sizeof(zuc256_eia3_ooo->lens));
        memset(zuc256_eia3_ooo->job_in_lane, 0,
               sizeof(zuc256_eia3_ooo->job_in_lane));
        zuc256_eia3_ooo->unused_lanes = 0xFF03020100;
        zuc256_eia3_ooo->num_lanes_inuse = 0;
        memset(&zuc256_eia3_ooo->state, 0,
               sizeof(zuc256_eia3_ooo->state));
        zuc256_eia3_ooo->init_not_done = 0;
        zuc256_eia3_ooo->unused_lane_bitmask = 0x0f;

        /* Init HMAC/SHA1 out-of-order fields */
        hmac_sha_1_ooo->lens[0] = 0;
        hmac_sha_1_ooo->lens[1] = 0;
        hmac_sha_1_ooo->lens[2] = 0;
        hmac_sha_1_ooo->lens[3] = 0;
        hmac_sha_1_ooo->lens[4] = 0xFFFF;
        hmac_sha_1_ooo->lens[5] = 0xFFFF;
        hmac_sha_1_ooo->lens[6] = 0xFFFF;
        hmac_sha_1_ooo->lens[7] = 0xFFFF;
        hmac_sha_1_ooo->unused_lanes = 0xFF03020100;
        for (j = 0; j < SSE_NUM_SHA1_LANES; j++) {
                hmac_sha_1_ooo->ldata[j].job_in_lane = NULL;
                hmac_sha_1_ooo->ldata[j].extra_block[64] = 0x80;
                memset(hmac_sha_1_ooo->ldata[j].extra_block + 65,
                       0x00,
                       64+7);
                p = hmac_sha_1_ooo->ldata[j].outer_block;
                memset(p + 5*4 + 1,
                       0x00,
                       64 - 5*4 - 1 - 2);
                p[5*4] = 0x80;
                p[64-2] = 0x02;
                p[64-1] = 0xA0;
        }

        /* Init HMAC/SHA224 out-of-order fields */
        hmac_sha_224_ooo->lens[0] = 0;
        hmac_sha_224_ooo->lens[1] = 0;
        hmac_sha_224_ooo->lens[2] = 0;
        hmac_sha_224_ooo->lens[3] = 0;
        hmac_sha_224_ooo->lens[4] = 0xFFFF;
        hmac_sha_224_ooo->lens[5] = 0xFFFF;
        hmac_sha_224_ooo->lens[6] = 0xFFFF;
        hmac_sha_224_ooo->lens[7] = 0xFFFF;
        hmac_sha_224_ooo->unused_lanes = 0xFF03020100;
        for (j = 0; j < SSE_NUM_SHA256_LANES; j++) {
                hmac_sha_224_ooo->ldata[j].job_in_lane = NULL;

                p = hmac_sha_224_ooo->ldata[j].extra_block;
                size = sizeof(hmac_sha_224_ooo->ldata[j].extra_block);
                memset (p, 0x00, size);
                p[64] = 0x80;

                p = hmac_sha_224_ooo->ldata[j].outer_block;
                size = sizeof(hmac_sha_224_ooo->ldata[j].outer_block);
                memset(p, 0x00, size);
                p[7*4] = 0x80;  /* digest 7 words long */
                p[64-2] = 0x02; /* length in little endian = 0x02E0 */
                p[64-1] = 0xE0;
        }

        /* Init HMAC/SHA_256 out-of-order fields */
        hmac_sha_256_ooo->lens[0] = 0;
        hmac_sha_256_ooo->lens[1] = 0;
        hmac_sha_256_ooo->lens[2] = 0;
        hmac_sha_256_ooo->lens[3] = 0;
        hmac_sha_256_ooo->lens[4] = 0xFFFF;
        hmac_sha_256_ooo->lens[5] = 0xFFFF;
        hmac_sha_256_ooo->lens[6] = 0xFFFF;
        hmac_sha_256_ooo->lens[7] = 0xFFFF;
        hmac_sha_256_ooo->unused_lanes = 0xFF03020100;
        for (j = 0; j < SSE_NUM_SHA256_LANES; j++) {
                hmac_sha_256_ooo->ldata[j].job_in_lane = NULL;
                hmac_sha_256_ooo->ldata[j].extra_block[64] = 0x80;
                memset(hmac_sha_256_ooo->ldata[j].extra_block + 65,
                       0x00,
                       64+7);
                p = hmac_sha_256_ooo->ldata[j].outer_block;
                memset(p + 8*4 + 1,
                       0x00,
                       64 - 8*4 - 1 - 2); /* digest is 8*4 bytes long */
                p[8*4] = 0x80;
                p[64-2] = 0x03; /* length of (opad (64*8) bits + 256 bits)
                                 * in hex is 0x300 */
                p[64-1] = 0x00;
        }

        /* Init HMAC/SHA384 out-of-order fields */
        hmac_sha_384_ooo->lens[0] = 0;
        hmac_sha_384_ooo->lens[1] = 0;
        hmac_sha_384_ooo->lens[2] = 0xFFFF;
        hmac_sha_384_ooo->lens[3] = 0xFFFF;
        hmac_sha_384_ooo->lens[4] = 0xFFFF;
        hmac_sha_384_ooo->lens[5] = 0xFFFF;
        hmac_sha_384_ooo->lens[6] = 0xFFFF;
        hmac_sha_384_ooo->lens[7] = 0xFFFF;
        hmac_sha_384_ooo->unused_lanes = 0xFF0100;
        for (j = 0; j < SSE_NUM_SHA512_LANES; j++) {
                MB_MGR_HMAC_SHA_512_OOO *ctx = hmac_sha_384_ooo;

                ctx->ldata[j].job_in_lane = NULL;
                ctx->ldata[j].extra_block[IMB_SHA_384_BLOCK_SIZE] = 0x80;
                memset(ctx->ldata[j].extra_block + (IMB_SHA_384_BLOCK_SIZE + 1),
                       0x00, IMB_SHA_384_BLOCK_SIZE + 7);

                p = ctx->ldata[j].outer_block;
                memset(p + IMB_SHA384_DIGEST_SIZE_IN_BYTES  + 1, 0x00,
                       /* special end point because this length is constant */
                       IMB_SHA_384_BLOCK_SIZE -
                       IMB_SHA384_DIGEST_SIZE_IN_BYTES - 1 - 2);
                p[IMB_SHA384_DIGEST_SIZE_IN_BYTES] = 0x80; /* mark the end */
                /*
                 * hmac outer block length always of fixed size, it is OKey
                 * length, a whole message block length, 1024 bits, with padding
                 * plus the length of the inner digest, which is 384 bits
                 * 1408 bits == 0x0580. The input message block needs to be
                 * converted to big endian within the sha implementation
                 * before use.
                 */
                p[IMB_SHA_384_BLOCK_SIZE - 2] = 0x05;
                p[IMB_SHA_384_BLOCK_SIZE - 1] = 0x80;
        }

        /* Init HMAC/SHA512 out-of-order fields */
        hmac_sha_512_ooo->lens[0] = 0;
        hmac_sha_512_ooo->lens[1] = 0;
        hmac_sha_512_ooo->lens[2] = 0xFFFF;
        hmac_sha_512_ooo->lens[3] = 0xFFFF;
        hmac_sha_512_ooo->lens[4] = 0xFFFF;
        hmac_sha_512_ooo->lens[5] = 0xFFFF;
        hmac_sha_512_ooo->lens[6] = 0xFFFF;
        hmac_sha_512_ooo->lens[7] = 0xFFFF;
        hmac_sha_512_ooo->unused_lanes = 0xFF0100;
        for (j = 0; j < SSE_NUM_SHA512_LANES; j++) {
                MB_MGR_HMAC_SHA_512_OOO *ctx = hmac_sha_512_ooo;

                ctx->ldata[j].job_in_lane = NULL;
                ctx->ldata[j].extra_block[IMB_SHA_512_BLOCK_SIZE] = 0x80;
                memset(ctx->ldata[j].extra_block + (IMB_SHA_512_BLOCK_SIZE + 1),
                       0x00, IMB_SHA_512_BLOCK_SIZE + 7);

                p = ctx->ldata[j].outer_block;
                memset(p + IMB_SHA512_DIGEST_SIZE_IN_BYTES  + 1, 0x00,
                       /* special end point because this length is constant */
                       IMB_SHA_512_BLOCK_SIZE -
                       IMB_SHA512_DIGEST_SIZE_IN_BYTES  - 1 - 2);
                p[IMB_SHA512_DIGEST_SIZE_IN_BYTES] = 0x80; /* mark the end */
                /*
                 * hmac outer block length always of fixed size, it is OKey
                 * length, a whole message block length, 1024 bits, with padding
                 * plus the length of the inner digest, which is 512 bits
                 * 1536 bits == 0x600. The input message block needs to be
                 * converted to big endian within the sha implementation
                 * before use.
                 */
                p[IMB_SHA_512_BLOCK_SIZE - 2] = 0x06;
                p[IMB_SHA_512_BLOCK_SIZE - 1] = 0x00;
        }

        /* Init HMAC/MD5 out-of-order fields */
        hmac_md5_ooo->lens[0] = 0;
        hmac_md5_ooo->lens[1] = 0;
        hmac_md5_ooo->lens[2] = 0;
        hmac_md5_ooo->lens[3] = 0;
        hmac_md5_ooo->lens[4] = 0;
        hmac_md5_ooo->lens[5] = 0;
        hmac_md5_ooo->lens[6] = 0;
        hmac_md5_ooo->lens[7] = 0;
        hmac_md5_ooo->lens[8] = 0xFFFF;
        hmac_md5_ooo->lens[9] = 0xFFFF;
        hmac_md5_ooo->lens[10] = 0xFFFF;
        hmac_md5_ooo->lens[11] = 0xFFFF;
        hmac_md5_ooo->lens[12] = 0xFFFF;
        hmac_md5_ooo->lens[13] = 0xFFFF;
        hmac_md5_ooo->lens[14] = 0xFFFF;
        hmac_md5_ooo->lens[15] = 0xFFFF;
        hmac_md5_ooo->unused_lanes = 0xF76543210;
        for (j = 0; j < SSE_NUM_MD5_LANES; j++) {
                hmac_md5_ooo->ldata[j].job_in_lane = NULL;

                p = hmac_md5_ooo->ldata[j].extra_block;
                size = sizeof(hmac_md5_ooo->ldata[j].extra_block);
                memset (p, 0x00, size);
                p[64] = 0x80;

                p = hmac_md5_ooo->ldata[j].outer_block;
                size = sizeof(hmac_md5_ooo->ldata[j].outer_block);
                memset(p, 0x00, size);
                p[4*4] = 0x80;
                p[64-7] = 0x02;
                p[64-8] = 0x80;
        }

        /* Init AES/XCBC OOO fields */
        memset(aes_xcbc_ooo->lens, 0xff,
               sizeof(aes_xcbc_ooo->lens));
        aes_xcbc_ooo->unused_lanes = 0xFF03020100;
        for (j = 0; j < 4; j++) {
                aes_xcbc_ooo->lens[j] = 0xFFFF;
                aes_xcbc_ooo->ldata[j].job_in_lane = NULL;
                aes_xcbc_ooo->ldata[j].final_block[16] = 0x80;
                memset(aes_xcbc_ooo->ldata[j].final_block + 17, 0x00, 15);
        }
        aes_xcbc_ooo->num_lanes_inuse = 0;

        /* Init AES-CCM auth out-of-order fields */
        memset(aes_ccm_ooo, 0, sizeof(MB_MGR_CCM_OOO));
        for (j = 4; j < 16; j++)
                aes_ccm_ooo->lens[j] = 0xFFFF;
        aes_ccm_ooo->unused_lanes = 0xF3210;
        aes_ccm_ooo->num_lanes_inuse = 0;

        memset(aes256_ccm_ooo, 0, sizeof(MB_MGR_CCM_OOO));
        for (j = 4; j < 16; j++)
                aes256_ccm_ooo->lens[j] = 0xFFFF;
        aes256_ccm_ooo->unused_lanes = 0xF3210;
        aes256_ccm_ooo->num_lanes_inuse = 0;

        /* Init AES-CMAC auth out-of-order fields */
        aes_cmac_ooo->lens[0] = 0;
        aes_cmac_ooo->lens[1] = 0;
        aes_cmac_ooo->lens[2] = 0;
        aes_cmac_ooo->lens[3] = 0;
        aes_cmac_ooo->lens[4] = 0xFFFF;
        aes_cmac_ooo->lens[5] = 0xFFFF;
        aes_cmac_ooo->lens[6] = 0xFFFF;
        aes_cmac_ooo->lens[7] = 0xFFFF;
        for (j = 0; j < 4; j++) {
                aes_cmac_ooo->init_done[j] = 0;
                aes_cmac_ooo->job_in_lane[j] = NULL;
        }
        aes_cmac_ooo->unused_lanes = 0xF3210;
        aes_cmac_ooo->num_lanes_inuse = 0;

	aes256_cmac_ooo->lens[0] = 0;
        aes256_cmac_ooo->lens[1] = 0;
        aes256_cmac_ooo->lens[2] = 0;
        aes256_cmac_ooo->lens[3] = 0;
        aes256_cmac_ooo->lens[4] = 0xFFFF;
        aes256_cmac_ooo->lens[5] = 0xFFFF;
        aes256_cmac_ooo->lens[6] = 0xFFFF;
        aes256_cmac_ooo->lens[7] = 0xFFFF;
        for (j = 0; j < 4; j++) {
                aes256_cmac_ooo->init_done[j] = 0;
                aes256_cmac_ooo->job_in_lane[j] = NULL;
        }
        aes256_cmac_ooo->unused_lanes = 0xF3210;
        aes256_cmac_ooo->num_lanes_inuse = 0;

        /* Init AES-CBCS out-of-order fields */
        memset(aes128_cbcs_ooo->lens, 0xFF, sizeof(aes128_cbcs_ooo->lens));
        memset(aes128_cbcs_ooo->job_in_lane, 0,
               sizeof(aes128_cbcs_ooo->job_in_lane));
        aes128_cbcs_ooo->num_lanes_inuse = 0;
        aes128_cbcs_ooo->unused_lanes = 0xF3210;
}

IMB_DLL_LOCAL void
init_mb_mgr_sse_no_aesni_internal(IMB_MGR *state, const int reset_mgrs)
{
#ifdef SAFE_PARAM
        if (state == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_MBMGR);
                return;
        }
#endif

        /* reset error status */
        imb_set_errno(state, 0);

        /* Set architecture for future checks */
        state->used_arch = (uint32_t) IMB_ARCH_NOAESNI;

        if (reset_mgrs) {
                reset_ooo_mgrs(state);

                /* Init "in order" components */
                state->next_job = 0;
                state->earliest_job = -1;
        }

        /* set SSE NO AESNI handlers */
        state->get_next_job        = get_next_job_sse_no_aesni;
        state->submit_job          = submit_job_sse_no_aesni;
        state->submit_job_nocheck  = submit_job_nocheck_sse_no_aesni;
        state->get_completed_job   = get_completed_job_sse_no_aesni;
        state->flush_job           = flush_job_sse_no_aesni;
        state->queue_size          = queue_size_sse_no_aesni;
        state->keyexp_128          = aes_keyexp_128_sse_no_aesni;
        state->keyexp_192          = aes_keyexp_192_sse_no_aesni;
        state->keyexp_256          = aes_keyexp_256_sse_no_aesni;
        state->cmac_subkey_gen_128 = aes_cmac_subkey_gen_sse_no_aesni;
        state->cmac_subkey_gen_256 = aes_cmac_256_subkey_gen_sse_no_aesni;
        state->xcbc_keyexp         = aes_xcbc_expand_key_sse_no_aesni;
        state->des_key_sched       = des_key_schedule;
        state->sha1_one_block      = sha1_one_block_sse;
        state->sha1                = sha1_sse;
        state->sha224_one_block    = sha224_one_block_sse;
        state->sha224              = sha224_sse;
        state->sha256_one_block    = sha256_one_block_sse;
        state->sha256              = sha256_sse;
        state->sha384_one_block    = sha384_one_block_sse;
        state->sha384              = sha384_sse;
        state->sha512_one_block    = sha512_one_block_sse;
        state->sha512              = sha512_sse;
        state->md5_one_block       = md5_one_block_sse;
        state->aes128_cfb_one      = aes_cfb_128_one_sse_no_aesni;

        state->eea3_1_buffer       = zuc_eea3_1_buffer_sse_no_aesni;
        state->eea3_4_buffer       = zuc_eea3_4_buffer_sse_no_aesni;
        state->eea3_n_buffer       = zuc_eea3_n_buffer_sse_no_aesni;
        state->eia3_1_buffer       = zuc_eia3_1_buffer_sse_no_aesni;
        state->eia3_n_buffer       = zuc_eia3_n_buffer_sse_no_aesni;

        state->f8_1_buffer         = kasumi_f8_1_buffer_sse;
        state->f8_1_buffer_bit     = kasumi_f8_1_buffer_bit_sse;
        state->f8_2_buffer         = kasumi_f8_2_buffer_sse;
        state->f8_3_buffer         = kasumi_f8_3_buffer_sse;
        state->f8_4_buffer         = kasumi_f8_4_buffer_sse;
        state->f8_n_buffer         = kasumi_f8_n_buffer_sse;
        state->f9_1_buffer         = kasumi_f9_1_buffer_sse;
        state->f9_1_buffer_user    = kasumi_f9_1_buffer_user_sse;
        state->kasumi_init_f8_key_sched = kasumi_init_f8_key_sched_sse;
        state->kasumi_init_f9_key_sched = kasumi_init_f9_key_sched_sse;
        state->kasumi_key_sched_size = kasumi_key_sched_size_sse;

        state->snow3g_f8_1_buffer_bit = snow3g_f8_1_buffer_bit_sse_no_aesni;
        state->snow3g_f8_1_buffer  = snow3g_f8_1_buffer_sse_no_aesni;
        state->snow3g_f8_2_buffer  = snow3g_f8_2_buffer_sse_no_aesni;
        state->snow3g_f8_4_buffer  = snow3g_f8_4_buffer_sse_no_aesni;
        state->snow3g_f8_8_buffer  = snow3g_f8_8_buffer_sse_no_aesni;
        state->snow3g_f8_n_buffer  = snow3g_f8_n_buffer_sse_no_aesni;
        state->snow3g_f8_8_buffer_multikey =
                snow3g_f8_8_buffer_multikey_sse_no_aesni;
        state->snow3g_f8_n_buffer_multikey =
                snow3g_f8_n_buffer_multikey_sse_no_aesni;
        state->snow3g_f9_1_buffer = snow3g_f9_1_buffer_sse_no_aesni;
        state->snow3g_init_key_sched = snow3g_init_key_sched_sse_no_aesni;
        state->snow3g_key_sched_size = snow3g_key_sched_size_sse_no_aesni;

        state->gcm128_enc          = aes_gcm_enc_128_sse_no_aesni;
        state->gcm192_enc          = aes_gcm_enc_192_sse_no_aesni;
        state->gcm256_enc          = aes_gcm_enc_256_sse_no_aesni;
        state->gcm128_dec          = aes_gcm_dec_128_sse_no_aesni;
        state->gcm192_dec          = aes_gcm_dec_192_sse_no_aesni;
        state->gcm256_dec          = aes_gcm_dec_256_sse_no_aesni;
        state->gcm128_init         = aes_gcm_init_128_sse_no_aesni;
        state->gcm192_init         = aes_gcm_init_192_sse_no_aesni;
        state->gcm256_init         = aes_gcm_init_256_sse_no_aesni;
        state->gcm128_init_var_iv  = aes_gcm_init_var_iv_128_sse_no_aesni;
        state->gcm192_init_var_iv  = aes_gcm_init_var_iv_192_sse_no_aesni;
        state->gcm256_init_var_iv  = aes_gcm_init_var_iv_256_sse_no_aesni;
        state->gcm128_enc_update   = aes_gcm_enc_128_update_sse_no_aesni;
        state->gcm192_enc_update   = aes_gcm_enc_192_update_sse_no_aesni;
        state->gcm256_enc_update   = aes_gcm_enc_256_update_sse_no_aesni;
        state->gcm128_dec_update   = aes_gcm_dec_128_update_sse_no_aesni;
        state->gcm192_dec_update   = aes_gcm_dec_192_update_sse_no_aesni;
        state->gcm256_dec_update   = aes_gcm_dec_256_update_sse_no_aesni;
        state->gcm128_enc_finalize = aes_gcm_enc_128_finalize_sse_no_aesni;
        state->gcm192_enc_finalize = aes_gcm_enc_192_finalize_sse_no_aesni;
        state->gcm256_enc_finalize = aes_gcm_enc_256_finalize_sse_no_aesni;
        state->gcm128_dec_finalize = aes_gcm_dec_128_finalize_sse_no_aesni;
        state->gcm192_dec_finalize = aes_gcm_dec_192_finalize_sse_no_aesni;
        state->gcm256_dec_finalize = aes_gcm_dec_256_finalize_sse_no_aesni;
        state->gcm128_precomp      = aes_gcm_precomp_128_sse_no_aesni;
        state->gcm192_precomp      = aes_gcm_precomp_192_sse_no_aesni;
        state->gcm256_precomp      = aes_gcm_precomp_256_sse_no_aesni;
        state->gcm128_pre          = aes_gcm_pre_128_sse_no_aesni;
        state->gcm192_pre          = aes_gcm_pre_192_sse_no_aesni;
        state->gcm256_pre          = aes_gcm_pre_256_sse_no_aesni;
        state->ghash               = ghash_sse_no_aesni;
        state->ghash_pre           = ghash_pre_sse_no_aesni;

        state->gmac128_init        = imb_aes_gmac_init_128_sse_no_aesni;
        state->gmac192_init        = imb_aes_gmac_init_192_sse_no_aesni;
        state->gmac256_init        = imb_aes_gmac_init_256_sse_no_aesni;
        state->gmac128_update      = imb_aes_gmac_update_128_sse_no_aesni;
        state->gmac192_update      = imb_aes_gmac_update_192_sse_no_aesni;
        state->gmac256_update      = imb_aes_gmac_update_256_sse_no_aesni;
        state->gmac128_finalize    = imb_aes_gmac_finalize_128_sse_no_aesni;
        state->gmac192_finalize    = imb_aes_gmac_finalize_192_sse_no_aesni;
        state->gmac256_finalize    = imb_aes_gmac_finalize_256_sse_no_aesni;

        state->hec_32              = hec_32_sse_no_aesni;
        state->hec_64              = hec_64_sse_no_aesni;
        state->crc32_ethernet_fcs  = ethernet_fcs_sse_no_aesni;
        state->crc16_x25           = crc16_x25_sse_no_aesni;
        state->crc32_sctp          = crc32_sctp_sse_no_aesni;
        state->crc24_lte_a         = crc24_lte_a_sse_no_aesni;
        state->crc24_lte_b         = crc24_lte_b_sse_no_aesni;
        state->crc16_fp_data       = crc16_fp_data_sse_no_aesni;
        state->crc11_fp_header     = crc11_fp_header_sse_no_aesni;
        state->crc7_fp_header      = crc7_fp_header_sse_no_aesni;
        state->crc10_iuup_data     = crc10_iuup_data_sse_no_aesni;
        state->crc6_iuup_header    = crc6_iuup_header_sse_no_aesni;
        state->crc32_wimax_ofdma_data = crc32_wimax_ofdma_data_sse_no_aesni;
        state->crc8_wimax_ofdma_hcs = crc8_wimax_ofdma_hcs_sse_no_aesni;

        state->chacha20_poly1305_init = init_chacha20_poly1305_sse;
        state->chacha20_poly1305_enc_update = update_enc_chacha20_poly1305_sse;
        state->chacha20_poly1305_dec_update = update_dec_chacha20_poly1305_sse;
        state->chacha20_poly1305_finalize = finalize_chacha20_poly1305_sse;
}

void
init_mb_mgr_sse_no_aesni(IMB_MGR *state)
{
        init_mb_mgr_sse_no_aesni_internal(state, 1);
}

#include "mb_mgr_code.h"
