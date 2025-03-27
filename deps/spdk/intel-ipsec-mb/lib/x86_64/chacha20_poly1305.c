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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define HASH_LEN_CLAMP    0xfffffffffffffff0ULL
#define HASH_REMAIN_CLAMP 0x000000000000000fULL

#include "intel-ipsec-mb.h"
#include "include/clear_regs_mem.h"
#include "include/memcpy.h"
#include "include/chacha20_poly1305.h"
#include "include/error.h"

__forceinline
void memcpy_asm(void *dst, const void *src, const size_t size,
                const IMB_ARCH arch)
{
        if (arch == IMB_ARCH_SSE)
                memcpy_fn_sse_16(dst, src, size);
        else
                memcpy_fn_avx_16(dst, src, size);
}

__forceinline
void chacha20_enc_dec_ks(const void *src, void *dst,
                         const uint64_t length, const void *key,
                         struct chacha20_poly1305_context_data *ctx,
                         const IMB_ARCH arch)
{
        if (arch == IMB_ARCH_SSE)
                chacha20_enc_dec_ks_sse(src, dst, length, key, ctx);
        else if (arch == IMB_ARCH_AVX)
                chacha20_enc_dec_ks_avx(src, dst, length, key, ctx);
        else if (arch == IMB_ARCH_AVX2)
                chacha20_enc_dec_ks_avx2(src, dst, length, key, ctx);
        else /* IMB_ARCH_AVX512 */
                chacha20_enc_dec_ks_avx512(src, dst, length, key, ctx);
}

__forceinline
void poly1305_aead_update(const void *msg, const uint64_t msg_len,
                          void *hash, const void *key, const IMB_ARCH arch,
                          const unsigned ifma)
{
        if (arch == IMB_ARCH_AVX512) {
                if (ifma)
                        poly1305_aead_update_fma_avx512(msg, msg_len, hash,
                                                        key);
                else
                        poly1305_aead_update_avx512(msg, msg_len, hash, key);
        } else
                poly1305_aead_update_scalar(msg, msg_len, hash, key);
}

__forceinline
void poly1305_aead_complete(const void *hash, const void *key,
                            void *tag, const IMB_ARCH arch, const unsigned ifma)
{
        if (arch == IMB_ARCH_AVX512) {
                if (ifma)
                        poly1305_aead_complete_fma_avx512(hash, key, tag);
                else
                        poly1305_aead_complete_avx512(hash, key, tag);
        } else
                poly1305_aead_complete_scalar(hash, key, tag);
}

__forceinline
void init_chacha20_poly1305(IMB_JOB *job, const IMB_ARCH arch,
                            const unsigned ifma)
{
        struct chacha20_poly1305_context_data *ctx =
                                                job->u.CHACHA20_POLY1305.ctx;
        const uint64_t hash_len =
                        (job->msg_len_to_hash_in_bytes & HASH_LEN_CLAMP);
        const uint64_t remain_ct_bytes =
                        (job->msg_len_to_hash_in_bytes & HASH_REMAIN_CLAMP);
        const uint8_t *remain_ct_ptr;

        ctx->hash[0] = 0;
        ctx->hash[1] = 0;
        ctx->hash[2] = 0;
        ctx->aad_len = job->u.CHACHA20_POLY1305.aad_len_in_bytes;
        ctx->hash_len = job->msg_len_to_hash_in_bytes;
        ctx->last_block_count = 0;
        ctx->remain_ks_bytes = 0;
        ctx->remain_ct_bytes = remain_ct_bytes;

        /* Store IV */
        memcpy_asm(ctx->IV, job->iv, 12, arch);

        /* Generate Poly key */
        if (arch == IMB_ARCH_SSE)
                poly1305_key_gen_sse(job->enc_keys, job->iv, ctx->poly_key);
        else
                poly1305_key_gen_avx(job->enc_keys, job->iv, ctx->poly_key);

        /* Calculate hash over AAD */
        poly1305_aead_update(job->u.CHACHA20_POLY1305.aad,
                             ctx->aad_len,
                             ctx->hash, ctx->poly_key, arch, ifma);

        if (job->cipher_direction == IMB_DIR_ENCRYPT) {
                chacha20_enc_dec_ks(job->src +
                                    job->cipher_start_src_offset_in_bytes,
                                    job->dst, job->msg_len_to_cipher_in_bytes,
                                    job->enc_keys, ctx, arch);
                /*
                 * Compute hash after cipher on encrypt
                 * (only on multiple of 16 bytes)
                 */
                poly1305_aead_update(job->dst, hash_len, ctx->hash,
                                     ctx->poly_key, arch, ifma);
                remain_ct_ptr = job->dst + hash_len;
                /* Copy last bytes of ciphertext (less than 16 bytes) */
                memcpy_asm(ctx->poly_scratch, remain_ct_ptr, remain_ct_bytes,
                           arch);
        } else {
                /*
                 * Compute hash first on decrypt
                 * (only on multiple of 16 bytes)
                 */
                poly1305_aead_update(job->src +
                                     job->hash_start_src_offset_in_bytes,
                                     hash_len, ctx->hash, ctx->poly_key, arch,
                                     ifma);

                remain_ct_ptr = job->src + job->hash_start_src_offset_in_bytes
                                + hash_len;
                /* Copy last bytes of ciphertext (less than 16 bytes) */
                memcpy_asm(ctx->poly_scratch, remain_ct_ptr, remain_ct_bytes,
                           arch);
                chacha20_enc_dec_ks(job->src +
                                    job->cipher_start_src_offset_in_bytes,
                                    job->dst, job->msg_len_to_cipher_in_bytes,
                                    job->enc_keys, ctx, arch);
        }
        job->status |= IMB_STATUS_COMPLETED;
}

__forceinline
void update_chacha20_poly1305(IMB_JOB *job, const IMB_ARCH arch,
                              const unsigned ifma)
{
        struct chacha20_poly1305_context_data *ctx =
                                                job->u.CHACHA20_POLY1305.ctx;
        uint64_t hash_len = job->msg_len_to_hash_in_bytes;
        uint64_t bytes_to_copy = 0;
        uint64_t remain_bytes_to_fill = (16 - ctx->remain_ct_bytes);
        uint64_t remain_ct_bytes;
        const uint8_t *remain_ct_ptr;

        /* Need to copy more bytes into scratchpad */
        if ((ctx->remain_ct_bytes > 0) && (remain_bytes_to_fill > 0)) {
                if (hash_len < remain_bytes_to_fill)
                        bytes_to_copy = hash_len;
                else
                        bytes_to_copy = remain_bytes_to_fill;
        }

        /* Increment total hash length */
        ctx->hash_len += job->msg_len_to_hash_in_bytes;

        if (job->cipher_direction == IMB_DIR_ENCRYPT) {
                chacha20_enc_dec_ks(job->src +
                                    job->cipher_start_src_offset_in_bytes,
                                    job->dst, job->msg_len_to_cipher_in_bytes,
                                    job->enc_keys, ctx, arch);

                /* Copy more bytes on Poly scratchpad */
                memcpy_asm(ctx->poly_scratch + ctx->remain_ct_bytes,
                           job->dst, bytes_to_copy, arch);
                ctx->remain_ct_bytes += bytes_to_copy;

                /*
                 * Compute hash on remaining bytes of previous segment and
                 * first bytes of this segment (if there are 16 bytes)
                 */
                if (ctx->remain_ct_bytes == 16) {
                        poly1305_aead_update(ctx->poly_scratch, 16, ctx->hash,
                                             ctx->poly_key, arch, ifma);
                        ctx->remain_ct_bytes = 0;
                }

                hash_len -= bytes_to_copy;
                remain_ct_bytes = hash_len & HASH_REMAIN_CLAMP;
                hash_len &= hash_len & HASH_LEN_CLAMP;

                /* compute hash after cipher on encrypt */
                poly1305_aead_update(job->dst + bytes_to_copy,
                                     hash_len, ctx->hash, ctx->poly_key, arch,
                                     ifma);

                remain_ct_ptr = job->dst + bytes_to_copy + hash_len;
                /* copy last bytes of ciphertext (less than 16 bytes) */
                memcpy_asm(ctx->poly_scratch, remain_ct_ptr, remain_ct_bytes,
                           arch);
                ctx->remain_ct_bytes += remain_ct_bytes;
        } else {
                /* Copy more bytes on Poly scratchpad */
                memcpy_asm(ctx->poly_scratch + ctx->remain_ct_bytes,
                           job->src + job->hash_start_src_offset_in_bytes,
                           bytes_to_copy, arch);
                ctx->remain_ct_bytes += bytes_to_copy;

                /*
                 * Compute hash on remaining bytes of previous segment and
                 * first bytes of this segment (if there are 16 bytes)
                 */
                if (ctx->remain_ct_bytes == 16) {
                        poly1305_aead_update(ctx->poly_scratch, 16, ctx->hash,
                                             ctx->poly_key, arch, ifma);
                        ctx->remain_ct_bytes = 0;
                }

                hash_len -= bytes_to_copy;
                remain_ct_bytes = hash_len & HASH_REMAIN_CLAMP;
                hash_len &= hash_len & HASH_LEN_CLAMP;

                /* compute hash first on decrypt */
                poly1305_aead_update(job->src +
                             job->hash_start_src_offset_in_bytes +
                             bytes_to_copy, hash_len, ctx->hash, ctx->poly_key,
                             arch, ifma);

                remain_ct_ptr = job->src + job->hash_start_src_offset_in_bytes
                                + bytes_to_copy + hash_len;
                /* copy last bytes of ciphertext (less than 16 bytes) */
                memcpy_asm(ctx->poly_scratch, remain_ct_ptr, remain_ct_bytes,
                           arch);
                ctx->remain_ct_bytes += remain_ct_bytes;
                chacha20_enc_dec_ks(job->src +
                                    job->cipher_start_src_offset_in_bytes,
                                    job->dst, job->msg_len_to_cipher_in_bytes,
                                    job->enc_keys, ctx, arch);
        }
        job->status |= IMB_STATUS_COMPLETED;
}

__forceinline
void complete_chacha20_poly1305(IMB_JOB *job, const IMB_ARCH arch,
                                const unsigned ifma)
{
        struct chacha20_poly1305_context_data *ctx =
                                                job->u.CHACHA20_POLY1305.ctx;
        uint64_t hash_len = job->msg_len_to_hash_in_bytes;
        uint64_t last[2];
        uint64_t bytes_to_copy = 0;
        uint64_t remain_bytes_to_fill = (16 - ctx->remain_ct_bytes);

        /* Need to copy more bytes into scratchpad */
        if ((ctx->remain_ct_bytes > 0) && (remain_bytes_to_fill > 0)) {
                if (hash_len < remain_bytes_to_fill)
                        bytes_to_copy = hash_len;
                else
                        bytes_to_copy = remain_bytes_to_fill;
        }

        /* Increment total hash length */
        ctx->hash_len += job->msg_len_to_hash_in_bytes;

        if (job->cipher_direction == IMB_DIR_ENCRYPT) {
                chacha20_enc_dec_ks(job->src +
                                    job->cipher_start_src_offset_in_bytes,
                                    job->dst, job->msg_len_to_cipher_in_bytes,
                                    job->enc_keys, ctx, arch);

                /* Copy more bytes on Poly scratchpad */
                memcpy_asm(ctx->poly_scratch + ctx->remain_ct_bytes,
                           job->dst, bytes_to_copy, arch);
                ctx->remain_ct_bytes += bytes_to_copy;

                /*
                 * Compute hash on remaining bytes of previous segment and
                 * first bytes of this segment (can be less than 16,
                 * as this is the last segment)
                 */
                if (ctx->remain_ct_bytes > 0) {
                        poly1305_aead_update(ctx->poly_scratch,
                                             ctx->remain_ct_bytes,
                                             ctx->hash,
                                             ctx->poly_key,
                                             arch, ifma);
                        ctx->remain_ct_bytes = 0;
                }
                hash_len -= bytes_to_copy;
                /* compute hash after cipher on encrypt */
                if (hash_len != 0)
                        poly1305_aead_update(job->dst + bytes_to_copy,
                                             hash_len, ctx->hash,
                                             ctx->poly_key,
                                             arch, ifma);
        } else {
                /* Copy more bytes on Poly scratchpad */
                memcpy_asm(ctx->poly_scratch + ctx->remain_ct_bytes,
                           job->src + job->hash_start_src_offset_in_bytes,
                           bytes_to_copy, arch);
                ctx->remain_ct_bytes += bytes_to_copy;

                /*
                 * Compute hash on remaining bytes of previous segment and
                 * first bytes of this segment (can be less than 16,
                 * as this is the last segment)
                 */
                if (ctx->remain_ct_bytes > 0) {
                        poly1305_aead_update(ctx->poly_scratch,
                                             ctx->remain_ct_bytes,
                                             ctx->hash,
                                             ctx->poly_key,
                                             arch, ifma);
                        ctx->remain_ct_bytes = 0;
                }

                hash_len -= bytes_to_copy;
                /* compute hash first on decrypt */
                if (hash_len != 0)
                        poly1305_aead_update(job->src +
                                job->hash_start_src_offset_in_bytes +
                                bytes_to_copy,
                                hash_len, ctx->hash, ctx->poly_key, arch, ifma);

                chacha20_enc_dec_ks(job->src +
                                    job->cipher_start_src_offset_in_bytes,
                                    job->dst, job->msg_len_to_cipher_in_bytes,
                                    job->enc_keys, ctx, arch);
        }

        /*
         * Construct extra block with AAD and message lengths for
         * authentication
         */
        last[0] = ctx->aad_len;
        last[1] = ctx->hash_len;
        poly1305_aead_update(last, sizeof(last), ctx->hash, ctx->poly_key,
                             arch, ifma);

        /* Finalize AEAD Poly1305 (final reduction and +S) */
        poly1305_aead_complete(ctx->hash, ctx->poly_key, job->auth_tag_output,
                               arch, ifma);

        /* Clear sensitive data from the context */
#ifdef SAFE_DATA
        clear_mem(ctx->last_ks, sizeof(ctx->last_ks));
        clear_mem(ctx->poly_key, sizeof(ctx->poly_key));
#endif
        job->status |= IMB_STATUS_COMPLETED;
}

__forceinline
IMB_JOB *aead_chacha20_poly1305_sgl(IMB_JOB *job, const IMB_ARCH arch,
                                    const unsigned ifma)
{
        switch (job->sgl_state) {
        case IMB_SGL_INIT:
                init_chacha20_poly1305(job, arch, ifma);
                break;
        case IMB_SGL_UPDATE:
                update_chacha20_poly1305(job, arch, ifma);
                break;
        case IMB_SGL_COMPLETE:
        default:
                complete_chacha20_poly1305(job, arch, ifma);
        }

        return job;
}

__forceinline
IMB_JOB *aead_chacha20_poly1305(IMB_JOB *job, const IMB_ARCH arch,
                                const unsigned ifma)
{
        DECLARE_ALIGNED(uint8_t ks[16*64], 64);
        uint64_t hash[3] = {0, 0, 0};
        const uint64_t aad_len = job->u.CHACHA20_POLY1305.aad_len_in_bytes;
        const uint64_t hash_len = job->msg_len_to_hash_in_bytes;
        uint64_t cipher_len = job->msg_len_to_cipher_in_bytes;
        uint64_t last[2];

        if (job->cipher_direction == IMB_DIR_ENCRYPT) {
                switch (arch) {
                case IMB_ARCH_SSE:
                        submit_job_chacha20_poly_enc_sse(job, ks);
                        break;
                case IMB_ARCH_AVX:
                        submit_job_chacha20_enc_dec_avx(job);
                        poly1305_key_gen_avx(job->enc_keys, job->iv, ks);
                        break;
                case IMB_ARCH_AVX2:
                        submit_job_chacha20_enc_dec_avx2(job);
                        poly1305_key_gen_avx(job->enc_keys, job->iv, ks);
                        break;
                case IMB_ARCH_AVX512:
                default:
                        submit_job_chacha20_poly_enc_avx512(job, ks);
                }

                /* Calculate hash over AAD */
                poly1305_aead_update(job->u.CHACHA20_POLY1305.aad, aad_len,
                                     hash, ks, arch, ifma);

                /* compute hash after cipher on encrypt */
                poly1305_aead_update(job->dst, hash_len, hash, ks, arch, ifma);
        } else {
                uint64_t len_to_gen;

                /* generate key for authentication */
                switch (arch) {
                case IMB_ARCH_SSE:
                        len_to_gen = (cipher_len >= (256 - 64)) ?
                                                256 : (cipher_len + 64);
                        gen_keystr_poly_key_sse(job->enc_keys, job->iv,
                                                len_to_gen, ks);
                        break;
                case IMB_ARCH_AVX:
                case IMB_ARCH_AVX2:
                        poly1305_key_gen_avx(job->enc_keys, job->iv, ks);
                        break;
                case IMB_ARCH_AVX512:
                default:
                        len_to_gen = (cipher_len >= (1024 - 64)) ?
                                                1024 : (cipher_len + 64);
                        gen_keystr_poly_key_avx512(job->enc_keys, job->iv,
                                                   len_to_gen, ks);
                }

                /* Calculate hash over AAD */
                poly1305_aead_update(job->u.CHACHA20_POLY1305.aad, aad_len,
                                     hash, ks, arch, ifma);

                /* compute hash first on decrypt */
                poly1305_aead_update(job->src +
                                     job->hash_start_src_offset_in_bytes,
                                     hash_len, hash, ks, arch, ifma);

                switch (arch) {
                case IMB_ARCH_SSE:
                        submit_job_chacha20_poly_dec_sse(job, ks + 64,
                                                         len_to_gen - 64);
                        break;
                case IMB_ARCH_AVX:
                        submit_job_chacha20_enc_dec_avx(job);
                        break;
                case IMB_ARCH_AVX2:
                        submit_job_chacha20_enc_dec_avx2(job);
                        break;
                case IMB_ARCH_AVX512:
                default:
                        /* Skip first 64 bytes of KS, as that's used only
                           for Poly key */
                        submit_job_chacha20_poly_dec_avx512(job, ks + 64,
                                                            len_to_gen - 64);
                }
        }

        /*
         * Construct extra block with AAD and message lengths for
         * authentication
         */
        last[0] = aad_len;
        last[1] = hash_len;
        poly1305_aead_update(last, sizeof(last), hash, ks, arch, ifma);

        /* Finalize AEAD Poly1305 (final reduction and +S) */
        poly1305_aead_complete(hash, ks, job->auth_tag_output, arch, ifma);

        job->status |= IMB_STATUS_COMPLETED;

        return job;
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_sse(IMB_MGR *mgr, IMB_JOB *job)
{
        (void) mgr;
        return aead_chacha20_poly1305(job, IMB_ARCH_SSE, 0);
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_avx(IMB_MGR *mgr, IMB_JOB *job)
{
        (void) mgr;
        return aead_chacha20_poly1305(job, IMB_ARCH_AVX, 0);
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_avx2(IMB_MGR *mgr, IMB_JOB *job)
{
        (void) mgr;
        return aead_chacha20_poly1305(job, IMB_ARCH_AVX2, 0);
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_avx512(IMB_MGR *mgr, IMB_JOB *job)
{
        if (mgr->features & IMB_FEATURE_AVX512_IFMA)
                return aead_chacha20_poly1305(job, IMB_ARCH_AVX512, 1);
        else
                return aead_chacha20_poly1305(job, IMB_ARCH_AVX512, 0);
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_sgl_sse(IMB_MGR *mgr, IMB_JOB *job)
{
        (void) mgr;
        return aead_chacha20_poly1305_sgl(job, IMB_ARCH_SSE, 0);
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_sgl_avx(IMB_MGR *mgr, IMB_JOB *job)
{
        (void) mgr;
        return aead_chacha20_poly1305_sgl(job, IMB_ARCH_AVX, 0);
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_sgl_avx2(IMB_MGR *mgr, IMB_JOB *job)
{
        (void) mgr;
        return aead_chacha20_poly1305_sgl(job, IMB_ARCH_AVX2, 0);
}

IMB_DLL_LOCAL
IMB_JOB *aead_chacha20_poly1305_sgl_avx512(IMB_MGR *mgr, IMB_JOB *job)
{
        if (mgr->features & IMB_FEATURE_AVX512_IFMA)
                return aead_chacha20_poly1305_sgl(job, IMB_ARCH_AVX512, 1);
        else
                return aead_chacha20_poly1305_sgl(job, IMB_ARCH_AVX512, 0);
}

__forceinline
void init_chacha20_poly1305_direct(const void *key,
                                   struct chacha20_poly1305_context_data *ctx,
                                   const void *iv, const void *aad,
                                   const uint64_t aad_len, const IMB_ARCH arch,
                                   const unsigned ifma)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (ctx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_CTX);
                return;
        }
        if (iv == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }
        if (aad == NULL && aad_len != 0) {
                imb_set_errno(NULL, IMB_ERR_NULL_AAD);
                return;
        }
#endif
        ctx->hash[0] = 0;
        ctx->hash[1] = 0;
        ctx->hash[2] = 0;
        ctx->aad_len = aad_len;
        ctx->hash_len = 0;
        ctx->last_block_count = 0;
        ctx->remain_ks_bytes = 0;
        ctx->remain_ct_bytes = 0;

        /* Store IV */
        memcpy_asm(ctx->IV, iv, 12, arch);

        /* Generate Poly key */
        if (arch == IMB_ARCH_SSE)
                poly1305_key_gen_sse(key, iv, ctx->poly_key);
        else
                poly1305_key_gen_avx(key, iv, ctx->poly_key);

        /* Calculate hash over AAD */
        poly1305_aead_update(aad, aad_len, ctx->hash, ctx->poly_key,
                             arch, ifma);
}

IMB_DLL_LOCAL
void init_chacha20_poly1305_sse(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len)
{
        init_chacha20_poly1305_direct(key, ctx, iv, aad,
                                      aad_len, IMB_ARCH_SSE, 0);
}

IMB_DLL_LOCAL
void init_chacha20_poly1305_avx(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len)
{
        init_chacha20_poly1305_direct(key, ctx, iv, aad,
                                      aad_len, IMB_ARCH_AVX, 0);
}

IMB_DLL_LOCAL
void init_chacha20_poly1305_avx512(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len)
{
        init_chacha20_poly1305_direct(key, ctx, iv, aad,
                                      aad_len, IMB_ARCH_AVX512, 0);
}

IMB_DLL_LOCAL
void init_chacha20_poly1305_fma_avx512(const void *key,
                                struct chacha20_poly1305_context_data *ctx,
                                const void *iv, const void *aad,
                                const uint64_t aad_len)
{
        init_chacha20_poly1305_direct(key, ctx, iv, aad,
                                      aad_len, IMB_ARCH_AVX512, 1);
}

__forceinline
void update_chacha20_poly1305_direct(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len,
                                     const IMB_CIPHER_DIRECTION dir,
                                     const IMB_ARCH arch,
                                     const unsigned ifma)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
        if (ctx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_CTX);
                return;
        }
        if (src == NULL && len != 0) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (dst == NULL && len != 0) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
#endif
        uint64_t bytes_to_copy = 0;
        uint64_t remain_bytes_to_fill = (16 - ctx->remain_ct_bytes);
        uint64_t remain_ct_bytes;
        const uint8_t *remain_ct_ptr;
        const uint8_t *src8 = (const uint8_t *) src;
        uint8_t *dst8 = (uint8_t *) dst;
        uint64_t length = len;

        /* Need to copy more bytes into scratchpad */
        if ((ctx->remain_ct_bytes > 0) && (remain_bytes_to_fill > 0)) {
                if (len < remain_bytes_to_fill)
                        bytes_to_copy = length;
                else
                        bytes_to_copy = remain_bytes_to_fill;
        }

        /* Increment total hash length */
        ctx->hash_len += length;

        if (dir == IMB_DIR_ENCRYPT) {
                chacha20_enc_dec_ks(src, dst, length, key, ctx, arch);

                /* Copy more bytes on Poly scratchpad */
                memcpy_asm(ctx->poly_scratch + ctx->remain_ct_bytes,
                           dst, bytes_to_copy, arch);
                ctx->remain_ct_bytes += bytes_to_copy;

                /*
                 * Compute hash on remaining bytes of previous segment and
                 * first bytes of this segment (if there are 16 bytes)
                 */
                if (ctx->remain_ct_bytes == 16) {
                        poly1305_aead_update(ctx->poly_scratch, 16, ctx->hash,
                                             ctx->poly_key, arch, ifma);
                        ctx->remain_ct_bytes = 0;
                }

                length -= bytes_to_copy;
                remain_ct_bytes = length & HASH_REMAIN_CLAMP;
                length &= length & HASH_LEN_CLAMP;

                /* compute hash after cipher on encrypt */
                poly1305_aead_update(dst8 + bytes_to_copy,
                                     length, ctx->hash, ctx->poly_key, arch,
                                     ifma);

                remain_ct_ptr = dst8 + bytes_to_copy + length;
                /* copy last bytes of ciphertext (less than 16 bytes) */
                memcpy_asm(ctx->poly_scratch, remain_ct_ptr, remain_ct_bytes,
                           arch);
                ctx->remain_ct_bytes += remain_ct_bytes;
        } else {
                /* Copy more bytes on Poly scratchpad */
                memcpy_asm(ctx->poly_scratch + ctx->remain_ct_bytes,
                           src, bytes_to_copy, arch);
                ctx->remain_ct_bytes += bytes_to_copy;

                /*
                 * Compute hash on remaining bytes of previous segment and
                 * first bytes of this segment (if there are 16 bytes)
                 */
                if (ctx->remain_ct_bytes == 16) {
                        poly1305_aead_update(ctx->poly_scratch, 16, ctx->hash,
                                             ctx->poly_key, arch, ifma);
                        ctx->remain_ct_bytes = 0;
                }

                length -= bytes_to_copy;
                remain_ct_bytes = length & HASH_REMAIN_CLAMP;
                length &= length & HASH_LEN_CLAMP;

                /* compute hash first on decrypt */
                poly1305_aead_update(src8 + bytes_to_copy, length, ctx->hash,
                                     ctx->poly_key, arch, ifma);

                remain_ct_ptr = src8 + bytes_to_copy + length;
                /* copy last bytes of ciphertext (less than 16 bytes) */
                memcpy_asm(ctx->poly_scratch, remain_ct_ptr, remain_ct_bytes,
                           arch);
                ctx->remain_ct_bytes += remain_ct_bytes;
                chacha20_enc_dec_ks(src, dst, len, key, ctx, arch);
        }
}

void update_enc_chacha20_poly1305_sse(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_ENCRYPT, IMB_ARCH_SSE, 0);
}

void update_enc_chacha20_poly1305_avx(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_ENCRYPT, IMB_ARCH_AVX, 0);
}

void update_enc_chacha20_poly1305_avx2(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_ENCRYPT, IMB_ARCH_AVX2, 0);
}


void update_enc_chacha20_poly1305_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_ENCRYPT, IMB_ARCH_AVX512, 0);
}

void update_enc_chacha20_poly1305_fma_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_ENCRYPT, IMB_ARCH_AVX512, 1);
}

void update_dec_chacha20_poly1305_sse(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_DECRYPT, IMB_ARCH_SSE, 0);
}

void update_dec_chacha20_poly1305_avx(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_DECRYPT, IMB_ARCH_AVX, 0);
}

void update_dec_chacha20_poly1305_avx2(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_DECRYPT, IMB_ARCH_AVX2, 0);
}

void update_dec_chacha20_poly1305_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_DECRYPT, IMB_ARCH_AVX512, 0);
}

void update_dec_chacha20_poly1305_fma_avx512(const void *key,
                                     struct chacha20_poly1305_context_data *ctx,
                                     void *dst, const void *src,
                                     const uint64_t len)
{
        update_chacha20_poly1305_direct(key, ctx, dst, src, len,
                                        IMB_DIR_DECRYPT, IMB_ARCH_AVX512, 1);
}

__forceinline
void
finalize_chacha20_poly1305_direct(struct chacha20_poly1305_context_data *ctx,
                                  void *tag, const uint64_t tag_len,
                                  const IMB_ARCH arch, const unsigned ifma)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (ctx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_CTX);
                return;
        }
        if (tag == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }
        if (tag_len == 0 || tag_len > 16) {
                imb_set_errno(NULL, IMB_ERR_AUTH_TAG_LEN);
                return;
        }
#endif
        uint64_t last[2];
        uint8_t auth_tag[16];

        if (ctx->remain_ct_bytes > 0) {
                poly1305_aead_update(ctx->poly_scratch,
                                     ctx->remain_ct_bytes,
                                     ctx->hash,
                                     ctx->poly_key,
                                     arch, ifma);
                ctx->remain_ct_bytes = 0;
        }

        /*
         * Construct extra block with AAD and message lengths for
         * authentication
         */
        last[0] = ctx->aad_len;
        last[1] = ctx->hash_len;
        poly1305_aead_update(last, sizeof(last), ctx->hash, ctx->poly_key,
                             arch, ifma);

        /* Finalize AEAD Poly1305 (final reduction and +S) */
        poly1305_aead_complete(ctx->hash, ctx->poly_key, auth_tag, arch, ifma);

        /* Copy N bytes of tag */
        memcpy_asm((uint8_t *) tag, auth_tag, tag_len, arch);

        /* Clear sensitive data from the context */
#ifdef SAFE_DATA
        clear_mem(ctx->last_ks, sizeof(ctx->last_ks));
        clear_mem(ctx->poly_key, sizeof(ctx->poly_key));
#endif
}

void finalize_chacha20_poly1305_sse(struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len)
{
        finalize_chacha20_poly1305_direct(ctx, tag, tag_len, IMB_ARCH_SSE, 0);
}

void finalize_chacha20_poly1305_avx(struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len)
{
        finalize_chacha20_poly1305_direct(ctx, tag, tag_len, IMB_ARCH_AVX, 0);
}

void finalize_chacha20_poly1305_avx512(
                                    struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len)
{
        finalize_chacha20_poly1305_direct(ctx, tag, tag_len,
                                          IMB_ARCH_AVX512, 0);
}

void finalize_chacha20_poly1305_fma_avx512(
                                    struct chacha20_poly1305_context_data *ctx,
                                    void *tag, const uint64_t tag_len)
{
        finalize_chacha20_poly1305_direct(ctx, tag, tag_len,
                                          IMB_ARCH_AVX512, 1);
}
