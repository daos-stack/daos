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


#include <stdio.h>
#include "intel-ipsec-mb.h"

#include "noaesni.h"
#include "asm.h"
#include "include/clear_regs_mem.h"
#include "include/error.h"

static uint32_t in[4*3] = {
        0x01010101, 0x01010101, 0x01010101, 0x01010101,
        0x02020202, 0x02020202, 0x02020202, 0x02020202,
        0x03030303, 0x03030303, 0x03030303, 0x03030303
};

void
aes_xcbc_expand_key_sse(const void *key, void *k1_exp, void *k2, void *k3)
{
#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);
        if (k1_exp == NULL || k2 == NULL || k3 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
#endif
        DECLARE_ALIGNED(uint32_t keys_exp_enc[11*4], 16);

        aes_keyexp_128_enc_sse(key, keys_exp_enc);

        aes128_ecbenc_x3_sse(in, keys_exp_enc, k1_exp, k2, k3);

        aes_keyexp_128_enc_sse(k1_exp, k1_exp);

#ifdef SAFE_DATA
        clear_mem(&keys_exp_enc, sizeof(keys_exp_enc));
#endif
}

void
aes_xcbc_expand_key_sse_no_aesni(const void *key, void *k1_exp,
                                 void *k2, void *k3)
{
#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);
        if (k1_exp == NULL || k2 == NULL || k3 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
#endif
        DECLARE_ALIGNED(uint32_t keys_exp_enc[11*4], 16);

        aes_keyexp_128_enc_sse_no_aesni(key, keys_exp_enc);

        aes128_ecbenc_x3_sse_no_aesni(in, keys_exp_enc, k1_exp, k2, k3);

        aes_keyexp_128_enc_sse_no_aesni(k1_exp, k1_exp);

#ifdef SAFE_DATA
        clear_mem(&keys_exp_enc, sizeof(keys_exp_enc));
#endif
}

__forceinline
void
aes_xcbc_expand_key_avx_common(const void *key,
                               void *k1_exp, void *k2, void *k3)
{
#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);
        if (k1_exp == NULL || k2 == NULL || k3 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return;
        }
#endif
        DECLARE_ALIGNED(uint32_t keys_exp_enc[11*4], 16);

        aes_keyexp_128_enc_avx(key, keys_exp_enc);

        aes128_ecbenc_x3_avx(in, keys_exp_enc, k1_exp, k2, k3);

        aes_keyexp_128_enc_avx(k1_exp, k1_exp);

#ifdef SAFE_DATA
        clear_mem(&keys_exp_enc, sizeof(keys_exp_enc));
#endif
}

void
aes_xcbc_expand_key_avx(const void *key, void *k1_exp, void *k2, void *k3)
{
        aes_xcbc_expand_key_avx_common(key, k1_exp, k2, k3);
}

void
aes_xcbc_expand_key_avx2(const void *key, void *k1_exp, void *k2, void *k3)
{
        aes_xcbc_expand_key_avx_common(key, k1_exp, k2, k3);
}

void
aes_xcbc_expand_key_avx512(const void *key, void *k1_exp, void *k2, void *k3)
{
        aes_xcbc_expand_key_avx_common(key, k1_exp, k2, k3);
}
