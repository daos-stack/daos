/*******************************************************************************
  Copyright (c) 2020-2021, Intel Corporation

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

#ifndef IMB_CHACHA20POLY1305_H
#define IMB_CHACHA20POLY1305_H

#include "intel-ipsec-mb.h"

/* new internal API's */
IMB_JOB *aead_chacha20_poly1305_sse(IMB_MGR *mgr, IMB_JOB *job);
IMB_JOB *aead_chacha20_poly1305_avx(IMB_MGR *mgr, IMB_JOB *job);
IMB_JOB *aead_chacha20_poly1305_avx2(IMB_MGR *mgr, IMB_JOB *job);
IMB_JOB *aead_chacha20_poly1305_avx512(IMB_MGR *mgr, IMB_JOB *job);

IMB_JOB *aead_chacha20_poly1305_sgl_sse(IMB_MGR *mgr, IMB_JOB *job);
IMB_JOB *aead_chacha20_poly1305_sgl_avx(IMB_MGR *mgr, IMB_JOB *job);
IMB_JOB *aead_chacha20_poly1305_sgl_avx2(IMB_MGR *mgr, IMB_JOB *job);
IMB_JOB *aead_chacha20_poly1305_sgl_avx512(IMB_MGR *mgr, IMB_JOB *job);

/* external symbols needed to implement the above */
IMB_JOB *submit_job_chacha20_enc_dec_sse(IMB_JOB *);
void chacha20_enc_dec_ks_sse(const void *src, void *dst,
                             const uint64_t length, const void *key,
                             const struct chacha20_poly1305_context_data *ctx);
IMB_JOB *submit_job_chacha20_poly_dec_sse(IMB_JOB *, const void *ks,
                                          const uint64_t len_to_xor);
IMB_JOB *submit_job_chacha20_enc_dec_avx(IMB_JOB *);
void chacha20_enc_dec_ks_avx(const void *src, void *dst,
                             const uint64_t length, const void *key,
                             const struct chacha20_poly1305_context_data *ctx);
IMB_JOB *submit_job_chacha20_enc_dec_avx2(IMB_JOB *);
void chacha20_enc_dec_ks_avx2(const void *src, void *dst,
                              const uint64_t length, const void *key,
                              const struct chacha20_poly1305_context_data *ctx);
IMB_JOB *submit_job_chacha20_enc_dec_avx512(IMB_JOB *);
void chacha20_enc_dec_ks_avx512(const void *src, void *dst,
                              const uint64_t length, const void *key,
                              const struct chacha20_poly1305_context_data *ctx);
IMB_JOB *submit_job_chacha20_poly_enc_avx512(IMB_JOB *, void *poly_key);
IMB_JOB *submit_job_chacha20_poly_dec_avx512(IMB_JOB *, const void *ks,
                                             const uint64_t len_to_xor);
IMB_JOB *submit_job_chacha20_poly_enc_sse(IMB_JOB *, void *poly_key);

void poly1305_key_gen_sse(const void *key, const void *iv, void *poly_key);
void poly1305_key_gen_avx(const void *key, const void *iv, void *poly_key);

void poly1305_aead_update_scalar(const void *msg, const uint64_t msg_len,
                                 void *hash, const void *key);
void poly1305_aead_complete_scalar(const void *hash, const void *key,
                                   void *tag);

void poly1305_aead_update_avx512(const void *msg, const uint64_t msg_len,
                                 void *hash, const void *key);
void poly1305_aead_complete_avx512(const void *hash, const void *key,
                                   void *tag);

void poly1305_aead_update_fma_avx512(const void *msg, const uint64_t msg_len,
                                     void *hash, const void *key);
void poly1305_aead_complete_fma_avx512(const void *hash, const void *key,
                                       void *tag);

void gen_keystr_poly_key_sse(const void *key, const void *iv,
                             const uint64_t len, void *ks);

void gen_keystr_poly_key_avx512(const void *key, const void *iv,
                                const uint64_t len, void *ks);

void init_chacha20_poly1305_sse(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len);
void init_chacha20_poly1305_avx(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len);
void init_chacha20_poly1305_avx512(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len);
void init_chacha20_poly1305_fma_avx512(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len);
void update_enc_chacha20_poly1305_sse(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_enc_chacha20_poly1305_avx(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_enc_chacha20_poly1305_avx2(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_enc_chacha20_poly1305_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_enc_chacha20_poly1305_fma_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_dec_chacha20_poly1305_sse(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_dec_chacha20_poly1305_avx(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_dec_chacha20_poly1305_avx2(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_dec_chacha20_poly1305_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void update_dec_chacha20_poly1305_fma_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len);
void finalize_chacha20_poly1305_sse(struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len);
void finalize_chacha20_poly1305_avx(struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len);
void finalize_chacha20_poly1305_avx512(
                                    struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len);
void finalize_chacha20_poly1305_fma_avx512(
                                    struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len);
#endif /* IMB_CHACHA20POLY1305_H */
