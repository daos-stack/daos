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
#include <stdint.h>
#include "intel-ipsec-mb.h"
#include "include/clear_regs_mem.h"
#include "include/error.h"

#ifdef LINUX
#define ROTATE(a, n) (((a) << (n)) ^ ((a) >> (32 - (n))))
#else
#include <intrin.h>
#define ROTATE(a, n) _rotl(a, n)
#endif

#define H0 0x67452301
#define H1 0xefcdab89
#define H2 0x98badcfe
#define H3 0x10325476

#define	F1(b, c, d)	((((c) ^ (d)) & (b)) ^ (d))
#define	F2(b, c, d)	((((b) ^ (c)) & (d)) ^ (c))
#define	F3(b, c, d)	((b) ^ (c) ^ (d))
#define	F4(b, c, d)	(((~(d)) | (b)) ^ (c))

#define STEP1(a, b, c, d, k, w, r) {            \
                a += w + k + F1(b, c, d);       \
                a = ROTATE(a, r);               \
                a += b;                         \
        }
#define STEP2(a, b, c, d, k, w, r) {            \
                a += w + k + F2(b, c, d);       \
                a = ROTATE(a, r);               \
                a += b;                         \
        }
#define STEP3(a, b, c, d, k, w, r) {            \
                a += w + k + F3(b, c, d);       \
                a = ROTATE(a, r);               \
                a += b;                         \
        }
#define STEP4(a, b, c, d, k, w, r) {            \
                a += w + k + F4(b, c, d);       \
                a = ROTATE(a, r);               \
                a += b;                         \
        }

enum arch_type {
        ARCH_SSE = 0,
        ARCH_AVX,
        ARCH_AVX2,
        ARCH_AVX512,
};

__forceinline
void
md5_one_block_common(const uint8_t *data, uint32_t digest[4],
                     const enum arch_type arch)
{
#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);
        if (data == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (digest == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }
#endif
        uint32_t a, b, c, d;
        uint32_t w[16];
        const uint32_t *data32 = (const uint32_t *)data;

        a = H0;
        b = H1;
        c = H2;
        d = H3;

        w[0] = data32[0];
        w[1] = data32[1];

        STEP1(a, b, c, d, 0xd76aa478, w[0], 7);
        w[2] = data32[2];
        STEP1(d, a, b, c, 0xe8c7b756, w[1], 12);
        w[3] = data32[3];
        STEP1(c, d, a, b, 0x242070db, w[2], 17);
        w[4] = data32[4];
        STEP1(b, c, d, a, 0xc1bdceee, w[3], 22);
        w[5] = data32[5];
        STEP1(a, b, c, d, 0xf57c0faf, w[4], 7);
        w[6] = data32[6];
        STEP1(d, a, b, c, 0x4787c62a, w[5], 12);
        w[7] = data32[7];
        STEP1(c, d, a, b, 0xa8304613, w[6], 17);
        w[8] = data32[8];
        STEP1(b, c, d, a, 0xfd469501, w[7], 22);
        w[9] = data32[9];
        STEP1(a, b, c, d, 0x698098d8, w[8], 7);
        w[10] = data32[10];
        STEP1(d, a, b, c, 0x8b44f7af, w[9], 12);
        w[11] = data32[11];
        STEP1(c, d, a, b, 0xffff5bb1, w[10], 17);
        w[12] = data32[12];
        STEP1(b, c, d, a, 0x895cd7be, w[11], 22);
        w[13] = data32[13];
        STEP1(a, b, c, d, 0x6b901122, w[12], 7);
        w[14] = data32[14];
        STEP1(d, a, b, c, 0xfd987193, w[13], 12);
        w[15] = data32[15];
        STEP1(c, d, a, b, 0xa679438e, w[14], 17);
        STEP1(b, c, d, a, 0x49b40821, w[15], 22);
        STEP2(a, b, c, d, 0xf61e2562, w[1], 5);
        STEP2(d, a, b, c, 0xc040b340, w[6], 9);
        STEP2(c, d, a, b, 0x265e5a51, w[11], 14);
        STEP2(b, c, d, a, 0xe9b6c7aa, w[0], 20);
        STEP2(a, b, c, d, 0xd62f105d, w[5], 5);
        STEP2(d, a, b, c, 0x02441453, w[10], 9);
        STEP2(c, d, a, b, 0xd8a1e681, w[15], 14);
        STEP2(b, c, d, a, 0xe7d3fbc8, w[4], 20);
        STEP2(a, b, c, d, 0x21e1cde6, w[9], 5);
        STEP2(d, a, b, c, 0xc33707d6, w[14], 9);
        STEP2(c, d, a, b, 0xf4d50d87, w[3], 14);
        STEP2(b, c, d, a, 0x455a14ed, w[8], 20);
        STEP2(a, b, c, d, 0xa9e3e905, w[13], 5);
        STEP2(d, a, b, c, 0xfcefa3f8, w[2], 9);
        STEP2(c, d, a, b, 0x676f02d9, w[7], 14);
        STEP2(b, c, d, a, 0x8d2a4c8a, w[12], 20);
        STEP3(a, b, c, d, 0xfffa3942, w[5], 4);
        STEP3(d, a, b, c, 0x8771f681, w[8], 11);
        STEP3(c, d, a, b, 0x6d9d6122, w[11], 16);
        STEP3(b, c, d, a, 0xfde5380c, w[14], 23);
        STEP3(a, b, c, d, 0xa4beea44, w[1], 4);
        STEP3(d, a, b, c, 0x4bdecfa9, w[4], 11);
        STEP3(c, d, a, b, 0xf6bb4b60, w[7], 16);
        STEP3(b, c, d, a, 0xbebfbc70, w[10], 23);
        STEP3(a, b, c, d, 0x289b7ec6, w[13], 4);
        STEP3(d, a, b, c, 0xeaa127fa, w[0], 11);
        STEP3(c, d, a, b, 0xd4ef3085, w[3], 16);
        STEP3(b, c, d, a, 0x04881d05, w[6], 23);
        STEP3(a, b, c, d, 0xd9d4d039, w[9], 4);
        STEP3(d, a, b, c, 0xe6db99e5, w[12], 11);
        STEP3(c, d, a, b, 0x1fa27cf8, w[15], 16);
        STEP3(b, c, d, a, 0xc4ac5665, w[2], 23);
        STEP4(a, b, c, d, 0xf4292244, w[0], 6);
        STEP4(d, a, b, c, 0x432aff97, w[7], 10);
        STEP4(c, d, a, b, 0xab9423a7, w[14], 15);
        STEP4(b, c, d, a, 0xfc93a039, w[5], 21);
        STEP4(a, b, c, d, 0x655b59c3, w[12], 6);
        STEP4(d, a, b, c, 0x8f0ccc92, w[3], 10);
        STEP4(c, d, a, b, 0xffeff47d, w[10], 15);
        STEP4(b, c, d, a, 0x85845dd1, w[1], 21);
        STEP4(a, b, c, d, 0x6fa87e4f, w[8], 6);
        STEP4(d, a, b, c, 0xfe2ce6e0, w[15], 10);
        STEP4(c, d, a, b, 0xa3014314, w[6], 15);
        STEP4(b, c, d, a, 0x4e0811a1, w[13], 21);
        STEP4(a, b, c, d, 0xf7537e82, w[4], 6);
        STEP4(d, a, b, c, 0xbd3af235, w[11], 10);
        STEP4(c, d, a, b, 0x2ad7d2bb, w[2], 15);
        STEP4(b, c, d, a, 0xeb86d391, w[9], 21);

        digest[0] = a + H0;
        digest[1] = b + H1;
        digest[2] = c + H2;
        digest[3] = d + H3;
#ifdef SAFE_DATA
        clear_var(&a, sizeof(a));
        clear_var(&b, sizeof(b));
        clear_var(&c, sizeof(c));
        clear_var(&d, sizeof(d));
        clear_mem(w, sizeof(w));
        clear_scratch_gps();
        switch(arch) {
        case ARCH_SSE:
                clear_scratch_xmms_sse();
                break;
        case ARCH_AVX:
                clear_scratch_xmms_avx();
                break;
        case ARCH_AVX2:
                clear_scratch_ymms();
                break;
        case ARCH_AVX512:
                clear_scratch_zmms();
                break;
        default:
                break;
        }
#else
        (void) arch;  /* unused */
#endif
}

void
md5_one_block_sse(const void *data, void *digest)
{
        md5_one_block_common(data, digest, ARCH_SSE);
}

void
md5_one_block_avx(const void *data, void *digest)
{
        md5_one_block_common(data, digest, ARCH_AVX);
}

void
md5_one_block_avx2(const void *data, void *digest)
{
        md5_one_block_common(data, digest, ARCH_AVX2);
}

void
md5_one_block_avx512(const void *data, void *digest)
{
        md5_one_block_common(data, digest, ARCH_AVX512);
}
