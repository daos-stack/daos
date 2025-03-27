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

#include <stdint.h>
#include "intel-ipsec-mb.h"
#include "gcm.h"
#include "noaesni.h"
#include "error.h"

/**
 * @brief Pre-processes GCM key data
 *
 * Prefills the gcm key data with key values for each round and
 * the initial sub hash key for tag encoding
 *
 * @param key pointer to key data
 * @param key_data GCM expanded key data
 *
 */

void aes_gcm_pre_128_sse(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_128_enc_sse(key, key_data->expanded_keys);
        aes_gcm_precomp_128_sse(key_data);
}

void aes_gcm_pre_128_sse_no_aesni(const void *key,
                                  struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_128_enc_sse_no_aesni(key, key_data->expanded_keys);
        aes_gcm_precomp_128_sse_no_aesni(key_data);
}

void aes_gcm_pre_128_avx_gen2(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_128_enc_avx(key, key_data->expanded_keys);
        aes_gcm_precomp_128_avx_gen2(key_data);
}

void aes_gcm_pre_128_avx_gen4(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_128_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_128_avx_gen4(key_data);
}

void aes_gcm_pre_128_avx512(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_128_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_128_avx512(key_data);
}

void aes_gcm_pre_128_vaes_avx512(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_128_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_128_vaes_avx512(key_data);
}

void aes_gcm_pre_192_sse(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_192_enc_sse(key, key_data->expanded_keys);
        aes_gcm_precomp_192_sse(key_data);
}

void aes_gcm_pre_192_sse_no_aesni(const void *key,
                                  struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_192_enc_sse_no_aesni(key, key_data->expanded_keys);
        aes_gcm_precomp_192_sse_no_aesni(key_data);
}

void aes_gcm_pre_192_avx_gen2(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_192_enc_avx(key, key_data->expanded_keys);
        aes_gcm_precomp_192_avx_gen2(key_data);
}

void aes_gcm_pre_192_avx_gen4(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_192_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_192_avx_gen4(key_data);
}

void aes_gcm_pre_192_avx512(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_192_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_192_avx512(key_data);
}

void aes_gcm_pre_192_vaes_avx512(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_192_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_192_vaes_avx512(key_data);
}

void aes_gcm_pre_256_sse(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_256_enc_sse(key, key_data->expanded_keys);
        aes_gcm_precomp_256_sse(key_data);
}

void aes_gcm_pre_256_sse_no_aesni(const void *key,
                                  struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_256_enc_sse_no_aesni(key, key_data->expanded_keys);
        aes_gcm_precomp_256_sse_no_aesni(key_data);
}

void aes_gcm_pre_256_avx_gen2(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_256_enc_avx(key, key_data->expanded_keys);
        aes_gcm_precomp_256_avx_gen2(key_data);
}

void aes_gcm_pre_256_avx_gen4(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_256_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_256_avx_gen4(key_data);
}

void aes_gcm_pre_256_avx512(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_256_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_256_avx512(key_data);
}

void aes_gcm_pre_256_vaes_avx512(const void *key, struct gcm_key_data *key_data)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (key_data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
#endif
        aes_keyexp_256_enc_avx2(key, key_data->expanded_keys);
        aes_gcm_precomp_256_vaes_avx512(key_data);
}

void
imb_aes_gmac_init_128_sse(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          const uint64_t iv_len)
{
        aes_gcm_init_var_iv_128_sse(key_data, context_data, iv,
                                    iv_len, NULL, 0);
}

void
imb_aes_gmac_init_192_sse(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          const uint64_t iv_len)
{
        aes_gcm_init_var_iv_192_sse(key_data, context_data, iv,
                                    iv_len, NULL, 0);
}

void
imb_aes_gmac_init_256_sse(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv,
                          const uint64_t iv_len)
{
        aes_gcm_init_var_iv_256_sse(key_data, context_data, iv,
                                    iv_len, NULL, 0);
}

void
imb_aes_gmac_finalize_128_sse(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *auth_tag,
                              const uint64_t  auth_tag_len)
{
        aes_gcm_enc_128_finalize_sse(key_data, context_data, auth_tag,
                                     auth_tag_len);
}

void
imb_aes_gmac_finalize_192_sse(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *auth_tag,
                              const uint64_t  auth_tag_len)
{
        aes_gcm_enc_192_finalize_sse(key_data, context_data, auth_tag,
                                     auth_tag_len);
}

void
imb_aes_gmac_finalize_256_sse(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *auth_tag,
                              const uint64_t  auth_tag_len)
{
        aes_gcm_enc_256_finalize_sse(key_data, context_data, auth_tag,
                                     auth_tag_len);
}

void
imb_aes_gmac_init_128_avx_gen2(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv,
                               const uint64_t iv_len)
{
        aes_gcm_init_var_iv_128_avx_gen2(key_data, context_data, iv,
                                         iv_len, NULL, 0);
}

void
imb_aes_gmac_init_192_avx_gen2(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv,
                               const uint64_t iv_len)
{
        aes_gcm_init_var_iv_192_avx_gen2(key_data, context_data, iv,
                                         iv_len, NULL, 0);
}

void
imb_aes_gmac_init_256_avx_gen2(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv,
                               const uint64_t iv_len)
{
        aes_gcm_init_var_iv_256_avx_gen2(key_data, context_data, iv,
                                         iv_len, NULL, 0);
}

void
imb_aes_gmac_finalize_128_avx_gen2(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t  auth_tag_len)
{
        aes_gcm_enc_128_finalize_avx_gen2(key_data, context_data, auth_tag,
                                          auth_tag_len);
}

void
imb_aes_gmac_finalize_192_avx_gen2(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t  auth_tag_len)
{
        aes_gcm_enc_192_finalize_avx_gen2(key_data, context_data, auth_tag,
                                          auth_tag_len);
}

void
imb_aes_gmac_finalize_256_avx_gen2(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t  auth_tag_len)
{
        aes_gcm_enc_256_finalize_avx_gen2(key_data, context_data, auth_tag,
                                          auth_tag_len);
}

void
imb_aes_gmac_init_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   const uint8_t *iv,
                                   const uint64_t iv_len)
{
        aes_gcm_init_var_iv_128_sse_no_aesni(key_data, context_data, iv,
                                             iv_len, NULL, 0);
}

void
imb_aes_gmac_init_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   const uint8_t *iv,
                                   const uint64_t iv_len)
{
        aes_gcm_init_var_iv_192_sse_no_aesni(key_data, context_data, iv,
                                             iv_len, NULL, 0);
}

void
imb_aes_gmac_init_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   const uint8_t *iv,
                                   const uint64_t iv_len)
{
        aes_gcm_init_var_iv_256_sse_no_aesni(key_data, context_data, iv,
                                             iv_len, NULL, 0);
}

void
imb_aes_gmac_finalize_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                       struct gcm_context_data *context_data,
                                       uint8_t *auth_tag,
                                       const uint64_t auth_tag_len)
{
        aes_gcm_enc_128_finalize_sse_no_aesni(key_data, context_data, auth_tag,
                                              auth_tag_len);
}

void
imb_aes_gmac_finalize_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                       struct gcm_context_data *context_data,
                                       uint8_t *auth_tag,
                                       const uint64_t auth_tag_len)
{
        aes_gcm_enc_192_finalize_sse_no_aesni(key_data, context_data, auth_tag,
                                              auth_tag_len);
}

void
imb_aes_gmac_finalize_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                       struct gcm_context_data *context_data,
                                       uint8_t *auth_tag,
                                       const uint64_t  auth_tag_len)
{
        aes_gcm_enc_256_finalize_sse_no_aesni(key_data, context_data, auth_tag,
                                              auth_tag_len);
}

void
imb_aes_gmac_init_128_avx_gen4(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv,
                               const uint64_t iv_len)
{
        aes_gcm_init_var_iv_128_avx_gen4(key_data, context_data, iv,
                                         iv_len, NULL, 0);
}

void
imb_aes_gmac_init_192_avx_gen4(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv,
                               const uint64_t iv_len)
{
        aes_gcm_init_var_iv_192_avx_gen4(key_data, context_data, iv,
                                         iv_len, NULL, 0);
}

void
imb_aes_gmac_init_256_avx_gen4(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv,
                               const uint64_t iv_len)
{
        aes_gcm_init_var_iv_256_avx_gen4(key_data, context_data, iv,
                                         iv_len, NULL, 0);
}

void
imb_aes_gmac_finalize_128_avx_gen4(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t  auth_tag_len)
{
        aes_gcm_enc_128_finalize_avx_gen4(key_data, context_data, auth_tag,
                                          auth_tag_len);
}

void
imb_aes_gmac_finalize_192_avx_gen4(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t  auth_tag_len)
{
        aes_gcm_enc_192_finalize_avx_gen4(key_data, context_data, auth_tag,
                                          auth_tag_len);
}

void
imb_aes_gmac_finalize_256_avx_gen4(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t  auth_tag_len)
{
        aes_gcm_enc_256_finalize_avx_gen4(key_data, context_data, auth_tag,
                                          auth_tag_len);
}

void
imb_aes_gmac_init_128_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv,
                             const uint64_t iv_len)
{
        aes_gcm_init_var_iv_128_avx512(key_data, context_data, iv,
                                       iv_len, NULL, 0);
}

void
imb_aes_gmac_init_192_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv,
                             const uint64_t iv_len)
{
        aes_gcm_init_var_iv_192_avx512(key_data, context_data, iv,
                                       iv_len, NULL, 0);
}

void
imb_aes_gmac_init_256_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv,
                             const uint64_t iv_len)
{
        aes_gcm_init_var_iv_256_avx512(key_data, context_data, iv,
                                       iv_len, NULL, 0);
}

void
imb_aes_gmac_finalize_128_avx512(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 uint8_t *auth_tag,
                                 const uint64_t  auth_tag_len)
{
        aes_gcm_enc_128_finalize_avx512(key_data, context_data, auth_tag,
                                        auth_tag_len);
}

void
imb_aes_gmac_finalize_192_avx512(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 uint8_t *auth_tag,
                                 const uint64_t  auth_tag_len)
{
        aes_gcm_enc_192_finalize_avx512(key_data, context_data, auth_tag,
                                        auth_tag_len);
}

void
imb_aes_gmac_finalize_256_avx512(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 uint8_t *auth_tag,
                                 const uint64_t  auth_tag_len)
{
        aes_gcm_enc_256_finalize_avx512(key_data, context_data, auth_tag,
                                        auth_tag_len);
}

void
imb_aes_gmac_init_128_vaes_avx512(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  const uint8_t *iv,
                                  const uint64_t iv_len)
{
        aes_gcm_init_var_iv_128_vaes_avx512(key_data, context_data, iv,
                                            iv_len, NULL, 0);
}

void
imb_aes_gmac_init_192_vaes_avx512(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  const uint8_t *iv,
                                  const uint64_t iv_len)
{
        aes_gcm_init_var_iv_192_vaes_avx512(key_data, context_data, iv,
                                            iv_len, NULL, 0);
}

void
imb_aes_gmac_init_256_vaes_avx512(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  const uint8_t *iv,
                                  const uint64_t iv_len)
{
        aes_gcm_init_var_iv_256_vaes_avx512(key_data, context_data, iv,
                                            iv_len, NULL, 0);
}

void
imb_aes_gmac_finalize_128_vaes_avx512(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag,
                                      const uint64_t  auth_tag_len)
{
        aes_gcm_enc_128_finalize_vaes_avx512(key_data, context_data, auth_tag,
                                             auth_tag_len);
}

void
imb_aes_gmac_finalize_192_vaes_avx512(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag,
                                      const uint64_t  auth_tag_len)
{
        aes_gcm_enc_192_finalize_vaes_avx512(key_data, context_data, auth_tag,
                                             auth_tag_len);
}

void
imb_aes_gmac_finalize_256_vaes_avx512(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag,
                                      const uint64_t  auth_tag_len)
{
        aes_gcm_enc_256_finalize_vaes_avx512(key_data, context_data, auth_tag,
                                             auth_tag_len);
}
