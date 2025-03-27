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

#include "intel-ipsec-mb.h"

#ifndef _GCM_H_
#define _GCM_H_

/*
 * AVX512+VAES+VPCLMULQDQ GCM API
 * - intentionally this is not exposed in intel-ipsec-mb.h
 * - available through IMB_GCM_xxx() macros from intel-ipsec-mb.h
 */
IMB_DLL_EXPORT void
aes_gcm_enc_128_vaes_avx512(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            uint8_t *out, uint8_t const *in, uint64_t msg_len,
                            const uint8_t *iv,
                            uint8_t const *aad, uint64_t aad_len,
                            uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_vaes_avx512(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            uint8_t *out, uint8_t const *in, uint64_t msg_len,
                            const uint8_t *iv,
                            uint8_t const *aad, uint64_t aad_len,
                            uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_vaes_avx512(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            uint8_t *out, uint8_t const *in, uint64_t msg_len,
                            const uint8_t *iv,
                            uint8_t const *aad, uint64_t aad_len,
                            uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_vaes_avx512(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            uint8_t *out, uint8_t const *in, uint64_t msg_len,
                            const uint8_t *iv,
                            uint8_t const *aad, uint64_t aad_len,
                            uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_vaes_avx512(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            uint8_t *out, uint8_t const *in, uint64_t msg_len,
                            const uint8_t *iv,
                            uint8_t const *aad, uint64_t aad_len,
                            uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_vaes_avx512(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            uint8_t *out, uint8_t const *in, uint64_t msg_len,
                            const uint8_t *iv,
                            uint8_t const *aad, uint64_t aad_len,
                            uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_init_128_vaes_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_192_vaes_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_256_vaes_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_init_var_iv_128_vaes_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_192_vaes_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_256_vaes_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_enc_128_update_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_update_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_update_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_update_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_update_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_update_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_finalize_vaes_avx512(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_finalize_vaes_avx512(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_finalize_vaes_avx512(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_finalize_vaes_avx512(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_finalize_vaes_avx512(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_finalize_vaes_avx512(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_precomp_128_vaes_avx512(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_precomp_192_vaes_avx512(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_precomp_256_vaes_avx512(struct gcm_key_data *key_data);

IMB_DLL_EXPORT void
aes_gcm_pre_128_vaes_avx512(const void *key, struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_pre_192_vaes_avx512(const void *key, struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_pre_256_vaes_avx512(const void *key, struct gcm_key_data *key_data);

/*
 * AVX512 GCM API
 * - intentionally this is not exposed in intel-ipsec-mb.h
 * - available through IMB_GCM_xxx() macros from intel-ipsec-mb.h
 */
IMB_DLL_EXPORT void
aes_gcm_enc_128_avx512(const struct gcm_key_data *key_data,
                       struct gcm_context_data *context_data,
                       uint8_t *out, uint8_t const *in, uint64_t msg_len,
                       const uint8_t *iv,
                       uint8_t const *aad, uint64_t aad_len,
                       uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_avx512(const struct gcm_key_data *key_data,
                       struct gcm_context_data *context_data,
                       uint8_t *out, uint8_t const *in, uint64_t msg_len,
                       const uint8_t *iv,
                       uint8_t const *aad, uint64_t aad_len,
                       uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_avx512(const struct gcm_key_data *key_data,
                       struct gcm_context_data *context_data,
                       uint8_t *out, uint8_t const *in, uint64_t msg_len,
                       const uint8_t *iv,
                       uint8_t const *aad, uint64_t aad_len,
                       uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_avx512(const struct gcm_key_data *key_data,
                       struct gcm_context_data *context_data,
                       uint8_t *out, uint8_t const *in, uint64_t msg_len,
                       const uint8_t *iv,
                       uint8_t const *aad, uint64_t aad_len,
                       uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_avx512(const struct gcm_key_data *key_data,
                       struct gcm_context_data *context_data,
                       uint8_t *out, uint8_t const *in, uint64_t msg_len,
                       const uint8_t *iv,
                       uint8_t const *aad, uint64_t aad_len,
                       uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_avx512(const struct gcm_key_data *key_data,
                       struct gcm_context_data *context_data,
                       uint8_t *out, uint8_t const *in, uint64_t msg_len,
                       const uint8_t *iv,
                       uint8_t const *aad, uint64_t aad_len,
                       uint8_t *auth_tag, uint64_t auth_tag_len);

IMB_DLL_EXPORT void
aes_gcm_init_128_avx512(const struct gcm_key_data *key_data,
                        struct gcm_context_data *context_data,
                        const uint8_t *iv, uint8_t const *aad,
                        uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_192_avx512(const struct gcm_key_data *key_data,
                        struct gcm_context_data *context_data,
                        const uint8_t *iv, uint8_t const *aad,
                        uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_256_avx512(const struct gcm_key_data *key_data,
                        struct gcm_context_data *context_data,
                        const uint8_t *iv, uint8_t const *aad,
                        uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_init_var_iv_128_avx512(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_192_avx512(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv, const uint64_t iv_len,
                               const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_256_avx512(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv, const uint64_t iv_len,
                               const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_update_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_update_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_update_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_update_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_update_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_update_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_finalize_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_finalize_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_finalize_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_finalize_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_finalize_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_finalize_avx512(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_precomp_128_avx512(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_precomp_192_avx512(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_precomp_256_avx512(struct gcm_key_data *key_data);

IMB_DLL_EXPORT void
aes_gcm_pre_128_avx512(const void *key, struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_pre_192_avx512(const void *key, struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_pre_256_avx512(const void *key, struct gcm_key_data *key_data);

/*
 * AESNI emulation GCM API (based on SSE architecture)
 * - intentionally this is not exposed in intel-ipsec-mb.h
 * - available through IMB_GCM_xxx() macros from intel-ipsec-mb.h
 */
IMB_DLL_EXPORT void
aes_gcm_enc_128_sse_no_aesni(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *out, uint8_t const *in, uint64_t msg_len,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len, uint8_t *auth_tag,
                             uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_sse_no_aesni(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *out, uint8_t const *in, uint64_t msg_len,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len, uint8_t *auth_tag,
                             uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_sse_no_aesni(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *out, uint8_t const *in, uint64_t msg_len,
                             const uint8_t *iv,
                             uint8_t const *aad, uint64_t aad_len,
                             uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_sse_no_aesni(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *out, uint8_t const *in, uint64_t msg_len,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len, uint8_t *auth_tag,
                             uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_sse_no_aesni(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *out, uint8_t const *in, uint64_t msg_len,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len, uint8_t *auth_tag,
                             uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_sse_no_aesni(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             uint8_t *out, uint8_t const *in, uint64_t msg_len,
                             const uint8_t *iv, uint8_t const *aad,
                             uint64_t aad_len, uint8_t *auth_tag,
                             uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_init_128_sse_no_aesni(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              const uint8_t *iv, uint8_t const *aad,
                              uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_192_sse_no_aesni(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              const uint8_t *iv, uint8_t const *aad,
                              uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_256_sse_no_aesni(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              const uint8_t *iv, uint8_t const *aad,
                              uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     const uint8_t *iv, const uint64_t iv_len,
                                     const uint8_t *aad,
                                     const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     const uint8_t *iv, const uint64_t iv_len,
                                     const uint8_t *aad,
                                     const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     const uint8_t *iv, const uint64_t iv_len,
                                     const uint8_t *aad,
                                     const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_update_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_update_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_update_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_update_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_update_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_update_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    uint64_t msg_len);
IMB_DLL_EXPORT void
aes_gcm_enc_128_finalize_sse_no_aesni(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_192_finalize_sse_no_aesni(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_enc_256_finalize_sse_no_aesni(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_128_finalize_sse_no_aesni(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_192_finalize_sse_no_aesni(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_dec_256_finalize_sse_no_aesni(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag, uint64_t auth_tag_len);
IMB_DLL_EXPORT void
aes_gcm_precomp_128_sse_no_aesni(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_precomp_192_sse_no_aesni(struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_precomp_256_sse_no_aesni(struct gcm_key_data *key_data);

IMB_DLL_EXPORT void
aes_gcm_pre_128_sse_no_aesni(const void *key, struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_pre_192_sse_no_aesni(const void *key, struct gcm_key_data *key_data);
IMB_DLL_EXPORT void
aes_gcm_pre_256_sse_no_aesni(const void *key, struct gcm_key_data *key_data);

/*
 * Extra GCM API (for SSE/AVX/AVX2)
 * - intentionally this is not exposed in intel-ipsec-mb.h
 * - available through IMB_AESxxx_GCM_INIT_VAR() macros from intel-ipsec-mb.h
 */
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_128_sse(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            const uint8_t *iv, const uint64_t iv_len,
                            const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_128_avx_gen2(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *iv, const uint64_t iv_len,
                                 const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_128_avx_gen4(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *iv, const uint64_t iv_len,
                                 const uint8_t *aad, const uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_init_var_iv_192_sse(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            const uint8_t *iv, const uint64_t iv_len,
                            const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_192_avx_gen2(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *iv, const uint64_t iv_len,
                                 const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_192_avx_gen4(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *iv, const uint64_t iv_len,
                                 const uint8_t *aad, const uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_init_var_iv_256_sse(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            const uint8_t *iv, const uint64_t iv_len,
                            const uint8_t *aad, const uint64_t aad_len);

IMB_DLL_EXPORT void
aes_gcm_init_var_iv_256_avx_gen2(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *iv, const uint64_t iv_len,
                                 const uint8_t *aad, const uint64_t aad_len);
IMB_DLL_EXPORT void
aes_gcm_init_var_iv_256_avx_gen4(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *iv, const uint64_t iv_len,
                                 const uint8_t *aad, const uint64_t aad_len);

/*
 * Internal GCM API for SSE/AVX/AVX2/AVX512/AESNI emulation,
 * to be used only through job API.
 */

IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_128_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in,
                           const uint64_t msg_len,
                           const uint8_t *iv, const uint64_t iv_len,
                           const uint8_t *aad, const uint64_t aad_len,
                           uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_192_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in,
                           const uint64_t msg_len,
                           const uint8_t *iv, const uint64_t iv_len,
                           const uint8_t *aad, const uint64_t aad_len,
                           uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_256_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in,
                           const uint64_t msg_len,
                           const uint8_t *iv, const uint64_t iv_len,
                           const uint8_t *aad, const uint64_t aad_len,
                           uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_128_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in,
                           const uint64_t msg_len,
                           const uint8_t *iv, const uint64_t iv_len,
                           const uint8_t *aad, const uint64_t aad_len,
                           uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_192_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in,
                           const uint64_t msg_len,
                           const uint8_t *iv, const uint64_t iv_len,
                           const uint8_t *aad, const uint64_t aad_len,
                           uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_256_sse(const struct gcm_key_data *key_data,
                           struct gcm_context_data *context_data,
                           uint8_t *out, const uint8_t *in,
                           const uint64_t msg_len,
                           const uint8_t *iv, const uint64_t iv_len,
                           const uint8_t *aad, const uint64_t aad_len,
                           uint8_t *auth_tag, const uint64_t auth_tag_len);

IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_128_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_192_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_256_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_128_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_192_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_256_avx_gen2(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);

IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_128_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_192_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_256_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_128_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_192_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_256_avx_gen4(const struct gcm_key_data *key_data,
                                struct gcm_context_data *context_data,
                                uint8_t *out, const uint8_t *in,
                                const uint64_t msg_len,
                                const uint8_t *iv, const uint64_t iv_len,
                                const uint8_t *aad, const uint64_t aad_len,
                                uint8_t *auth_tag, const uint64_t auth_tag_len);

IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_128_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              const uint64_t msg_len,
                              const uint8_t *iv, const uint64_t iv_len,
                              const uint8_t *aad, const uint64_t aad_len,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_192_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              const uint64_t msg_len,
                              const uint8_t *iv, const uint64_t iv_len,
                              const uint8_t *aad, const uint64_t aad_len,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_256_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              const uint64_t msg_len,
                              const uint8_t *iv, const uint64_t iv_len,
                              const uint8_t *aad, const uint64_t aad_len,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_128_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              const uint64_t msg_len,
                              const uint8_t *iv, const uint64_t iv_len,
                              const uint8_t *aad, const uint64_t aad_len,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_192_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              const uint64_t msg_len,
                              const uint8_t *iv, const uint64_t iv_len,
                              const uint8_t *aad, const uint64_t aad_len,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_256_avx512(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *out, const uint8_t *in,
                              const uint64_t msg_len,
                              const uint8_t *iv, const uint64_t iv_len,
                              const uint8_t *aad, const uint64_t aad_len,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);

IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_128_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   const uint64_t msg_len,
                                   const uint8_t *iv, const uint64_t iv_len,
                                   const uint8_t *aad, const uint64_t aad_len,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_192_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   const uint64_t msg_len,
                                   const uint8_t *iv, const uint64_t iv_len,
                                   const uint8_t *aad, const uint64_t aad_len,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_256_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   const uint64_t msg_len,
                                   const uint8_t *iv, const uint64_t iv_len,
                                   const uint8_t *aad, const uint64_t aad_len,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_128_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   const uint64_t msg_len,
                                   const uint8_t *iv, const uint64_t iv_len,
                                   const uint8_t *aad, const uint64_t aad_len,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_192_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   const uint64_t msg_len,
                                   const uint8_t *iv, const uint64_t iv_len,
                                   const uint8_t *aad, const uint64_t aad_len,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_256_vaes_avx512(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *out, const uint8_t *in,
                                   const uint64_t msg_len,
                                   const uint8_t *iv, const uint64_t iv_len,
                                   const uint8_t *aad, const uint64_t aad_len,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);

IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    const uint64_t msg_len,
                                    const uint8_t *iv, const uint64_t iv_len,
                                    const uint8_t *aad, const uint64_t aad_len,
                                    uint8_t *auth_tag,
                                    const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    const uint64_t msg_len,
                                    const uint8_t *iv, const uint64_t iv_len,
                                    const uint8_t *aad, const uint64_t aad_len,
                                    uint8_t *auth_tag,
                                    const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_enc_var_iv_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    const uint64_t msg_len,
                                    const uint8_t *iv, const uint64_t iv_len,
                                    const uint8_t *aad, const uint64_t aad_len,
                                    uint8_t *auth_tag,
                                    const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    const uint64_t msg_len,
                                    const uint8_t *iv, const uint64_t iv_len,
                                    const uint8_t *aad, const uint64_t aad_len,
                                    uint8_t *auth_tag,
                                    const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    const uint64_t msg_len,
                                    const uint8_t *iv, const uint64_t iv_len,
                                    const uint8_t *aad, const uint64_t aad_len,
                                    uint8_t *auth_tag,
                                    const uint64_t auth_tag_len);
IMB_DLL_LOCAL void
aes_gcm_dec_var_iv_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    uint8_t *out, const uint8_t *in,
                                    const uint64_t msg_len,
                                    const uint8_t *iv, const uint64_t iv_len,
                                    const uint8_t *aad, const uint64_t aad_len,
                                    uint8_t *auth_tag,
                                    const uint64_t auth_tag_len);
/*
 * GHASH API for SSE/AVX/AVX2/AVX512/AESNI emulation
 */

IMB_DLL_EXPORT void
ghash_sse_no_aesni(struct gcm_key_data *key_data, const void *in,
                   const uint64_t in_len, void *io_tag, const uint64_t tag_len);
IMB_DLL_EXPORT void
ghash_sse(struct gcm_key_data *key_data, const void *in,
          const uint64_t in_len, void *io_tag, const uint64_t tag_len);
IMB_DLL_EXPORT void
ghash_avx_gen2(struct gcm_key_data *key_data, const void *in,
               const uint64_t in_len, void *io_tag,
               const uint64_t tag_len);
IMB_DLL_EXPORT void
ghash_avx_gen4(struct gcm_key_data *key_data, const void *in,
               const uint64_t in_len, void *io_tag,
               const uint64_t tag_len);
IMB_DLL_EXPORT void
ghash_avx512(struct gcm_key_data *key_data, const void *in,
             const uint64_t in_len, void *io_tag,
             const uint64_t tag_len);
IMB_DLL_EXPORT void
ghash_vaes_avx512(struct gcm_key_data *key_data, const void *in,
                  const uint64_t in_len, void *io_tag,
                  const uint64_t tag_len);

IMB_DLL_EXPORT void
ghash_pre_sse_no_aesni(const void *key, struct gcm_key_data *key_data);

IMB_DLL_EXPORT void
ghash_pre_sse(const void *key, struct gcm_key_data *key_data);

IMB_DLL_EXPORT void
ghash_pre_avx_gen2(const void *key, struct gcm_key_data *key_data);

IMB_DLL_EXPORT void
ghash_pre_vaes_avx512(const void *key, struct gcm_key_data *key_data);

/*
 * GMAC API for SSE/AVX/AVX2/AVX512/AESNI emulation
 */
IMB_DLL_EXPORT void
imb_aes_gmac_init_128_sse(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_192_sse(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_256_sse(const struct gcm_key_data *key_data,
                          struct gcm_context_data *context_data,
                          const uint8_t *iv, const uint64_t iv_len);

IMB_DLL_EXPORT void
imb_aes_gmac_init_128_avx_gen2(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_192_avx_gen2(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_256_avx_gen2(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv, const uint64_t iv_len);

IMB_DLL_EXPORT void
imb_aes_gmac_init_128_avx_gen4(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_192_avx_gen4(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_256_avx_gen4(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *iv, const uint64_t iv_len);

IMB_DLL_EXPORT void
imb_aes_gmac_init_128_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_192_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_256_avx512(const struct gcm_key_data *key_data,
                             struct gcm_context_data *context_data,
                             const uint8_t *iv, const uint64_t iv_len);

IMB_DLL_EXPORT void
imb_aes_gmac_init_128_vaes_avx512(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_192_vaes_avx512(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_256_vaes_avx512(const struct gcm_key_data *key_data,
                                  struct gcm_context_data *context_data,
                                  const uint8_t *iv, const uint64_t iv_len);

IMB_DLL_EXPORT void
imb_aes_gmac_init_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   const uint8_t *iv, const uint64_t iv_len);
IMB_DLL_EXPORT void
imb_aes_gmac_init_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   const uint8_t *iv, const uint64_t iv_len);

IMB_DLL_EXPORT void
imb_aes_gmac_update_128_sse(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_192_sse(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_256_sse(const struct gcm_key_data *key_data,
                            struct gcm_context_data *context_data,
                            const uint8_t *in, const uint64_t in_len);

IMB_DLL_EXPORT void
imb_aes_gmac_update_128_avx_gen2(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_192_avx_gen2(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_256_avx_gen2(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *in, const uint64_t in_len);

IMB_DLL_EXPORT void
imb_aes_gmac_update_128_avx_gen4(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_192_avx_gen4(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_256_avx_gen4(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 const uint8_t *in, const uint64_t in_len);

IMB_DLL_EXPORT void
imb_aes_gmac_update_128_avx512(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_192_avx512(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_256_avx512(const struct gcm_key_data *key_data,
                               struct gcm_context_data *context_data,
                               const uint8_t *in, const uint64_t in_len);

IMB_DLL_EXPORT void
imb_aes_gmac_update_128_vaes_avx512(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_192_vaes_avx512(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_256_vaes_avx512(const struct gcm_key_data *key_data,
                                    struct gcm_context_data *context_data,
                                    const uint8_t *in, const uint64_t in_len);

IMB_DLL_EXPORT void
imb_aes_gmac_update_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     const uint8_t *in, const uint64_t in_len);
IMB_DLL_EXPORT void
imb_aes_gmac_update_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                     struct gcm_context_data *context_data,
                                     const uint8_t *in, const uint64_t in_len);

IMB_DLL_EXPORT void
imb_aes_gmac_finalize_128_sse(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_192_sse(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_256_sse(const struct gcm_key_data *key_data,
                              struct gcm_context_data *context_data,
                              uint8_t *auth_tag, const uint64_t auth_tag_len);

IMB_DLL_EXPORT void
imb_aes_gmac_finalize_128_avx_gen2(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_192_avx_gen2(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_256_avx_gen2(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);

IMB_DLL_EXPORT void
imb_aes_gmac_finalize_128_avx_gen4(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_192_avx_gen4(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_256_avx_gen4(const struct gcm_key_data *key_data,
                                   struct gcm_context_data *context_data,
                                   uint8_t *auth_tag,
                                   const uint64_t auth_tag_len);

IMB_DLL_EXPORT void
imb_aes_gmac_finalize_128_avx512(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 uint8_t *auth_tag,
                                 const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_192_avx512(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 uint8_t *auth_tag,
                                 const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_256_avx512(const struct gcm_key_data *key_data,
                                 struct gcm_context_data *context_data,
                                 uint8_t *auth_tag,
                                 const uint64_t auth_tag_len);

IMB_DLL_EXPORT void
imb_aes_gmac_finalize_128_vaes_avx512(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag,
                                      const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_192_vaes_avx512(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag,
                                      const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_256_vaes_avx512(const struct gcm_key_data *key_data,
                                      struct gcm_context_data *context_data,
                                      uint8_t *auth_tag,
                                      const uint64_t auth_tag_len);

IMB_DLL_EXPORT void
imb_aes_gmac_finalize_128_sse_no_aesni(const struct gcm_key_data *key_data,
                                       struct gcm_context_data *context_data,
                                       uint8_t *auth_tag,
                                       const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_192_sse_no_aesni(const struct gcm_key_data *key_data,
                                       struct gcm_context_data *context_data,
                                       uint8_t *auth_tag,
                                       const uint64_t auth_tag_len);
IMB_DLL_EXPORT void
imb_aes_gmac_finalize_256_sse_no_aesni(const struct gcm_key_data *key_data,
                                       struct gcm_context_data *context_data,
                                       uint8_t *auth_tag,
                                       const uint64_t auth_tag_len);
#endif /* _GCM_H_ */
