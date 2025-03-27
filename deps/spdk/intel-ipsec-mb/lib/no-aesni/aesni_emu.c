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

/* ========================================================================== */
/* AESNI emulation API and helper functions */
/* ========================================================================== */

#include "intel-ipsec-mb.h"
#include "aesni_emu.h"
#include "include/constant_lookup.h"

#ifdef LINUX
#include <x86intrin.h>
#else
#include <intrin.h>
#endif

typedef union {
        uint32_t i;
        uint8_t byte[4];
} byte_split_t;

static const DECLARE_ALIGNED(uint8_t aes_sbox[16][16], 16) = {
        { 0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
          0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76 },
        { 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
          0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0 },
        { 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
          0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15 },
        { 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
          0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75 },
        { 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
          0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84 },
        { 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
          0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf },
        { 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
          0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8 },
        { 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
          0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2 },
        { 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
          0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73 },
        { 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
          0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb },
        { 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
          0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79 },
        { 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
          0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08 },
        { 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
          0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a },
        { 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
          0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e },
        { 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
          0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf },
        { 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
          0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 }
};

static const DECLARE_ALIGNED(uint8_t aes_isbox[16][16], 16) = {
        { 0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
          0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb },
        { 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
          0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb },
        { 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
          0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e },
        { 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
          0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25 },
        { 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
          0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92 },
        { 0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
          0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84 },
        { 0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
          0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06 },
        { 0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
          0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b },
        { 0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
          0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73 },
        { 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
          0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e },
        { 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
          0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b },
        { 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
          0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4 },
        { 0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
          0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f },
        { 0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
          0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef },
        { 0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
          0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61 },
        { 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
          0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d }
};

/* ========================================================================== */
/* Emulation API helper functions */
/* ========================================================================== */

static void xor_xmm(union xmm_reg *d,
                    const union xmm_reg *s1,
                    const union xmm_reg *s2)
{
        uint32_t i;

        for (i = 0; i < MAX_QWORDS_PER_XMM; i++)
                d->qword[i] = s1->qword[i] ^ s2->qword[i];
}

static uint32_t rot(const uint32_t x)
{
        uint32_t y = (x>>8) | (x<<24);

        return y;
}

static void substitute_bytes(union xmm_reg *dst, const union xmm_reg *src)
{
        __m128i vx = _mm_loadu_si128((const __m128i *) &src->byte[0]);

        IMB_ASSERT(MAX_BYTES_PER_XMM == 16);

        vx = lookup_16x8bit_sse(vx, aes_sbox);
        _mm_storeu_si128((__m128i *) &dst->byte[0], vx);
}

static void inverse_substitute_bytes(union xmm_reg *dst,
                                     const union xmm_reg *src)
{
        __m128i vx = _mm_loadu_si128((const __m128i *) &src->byte[0]);

        IMB_ASSERT(MAX_BYTES_PER_XMM == 16);

        vx = lookup_16x8bit_sse(vx, aes_isbox);
        _mm_storeu_si128((__m128i *) &dst->byte[0], vx);
}

static uint8_t gfmul(const uint8_t x, const uint8_t y)
{
        uint32_t i;
        uint8_t multiplier = y;
        uint8_t out = 0;

        for (i = 0; i < 7; i++) {
                if (i >= 1) {
                        /* GFMUL by 2. "xtimes" operation from FIPS document */
                        uint8_t t = multiplier << 1; /* lop of the high bit */

                        if (multiplier >> 7) /* look at the old high bit */
                                multiplier = t ^ 0x1B; /* polynomial division */
                        else
                                multiplier = t;
                }
                if ((x >> i) & 1)
                        out = out ^ multiplier;
        }

        return out;
}

static void mix_columns(union xmm_reg *dst, const union xmm_reg *src)
{
        uint32_t c;

        for (c = 0; c < MAX_DWORDS_PER_XMM; c++) {
                uint8_t s0c = src->byte[c*4+0];
                uint8_t s1c = src->byte[c*4+1];
                uint8_t s2c = src->byte[c*4+2];
                uint8_t s3c = src->byte[c*4+3];

                dst->byte[c*4+0] = gfmul(2, s0c) ^ gfmul(3, s1c) ^ s2c ^ s3c;
                dst->byte[c*4+1] = s0c ^ gfmul(2, s1c) ^ gfmul(3, s2c) ^ s3c;
                dst->byte[c*4+2] = s0c ^ s1c ^ gfmul(2, s2c) ^ gfmul(3, s3c);
                dst->byte[c*4+3] = gfmul(3, s0c) ^ s1c ^ s2c ^ gfmul(2, s3c);
        }
}

static void inverse_mix_columns(union xmm_reg *dst,
                                const union xmm_reg *src)
{
        uint32_t c;

        for (c = 0; c < MAX_DWORDS_PER_XMM; c++) {
                uint8_t s0c = src->byte[c*4+0];
                uint8_t s1c = src->byte[c*4+1];
                uint8_t s2c = src->byte[c*4+2];
                uint8_t s3c = src->byte[c*4+3];

                dst->byte[c*4+0] = gfmul(0xe, s0c) ^ gfmul(0xb, s1c) ^
                        gfmul(0xd, s2c) ^ gfmul(0x9, s3c);
                dst->byte[c*4+1] = gfmul(0x9, s0c) ^ gfmul(0xe, s1c) ^
                        gfmul(0xb, s2c) ^ gfmul(0xd, s3c);
                dst->byte[c*4+2] = gfmul(0xd, s0c) ^ gfmul(0x9, s1c) ^
                        gfmul(0xe, s2c) ^ gfmul(0xb, s3c);
                dst->byte[c*4+3] = gfmul(0xb, s0c) ^ gfmul(0xd, s1c) ^
                        gfmul(0x9, s2c) ^ gfmul(0xe, s3c);
        }
}

static uint32_t wrap_neg(const int x)
{
        /* make sure we stay in 0..3 */
        return (x >= 0) ? x : (x + 4);
}

static uint32_t wrap_pos(const int x)
{
        /* make sure we stay in 0..3 */
        return (x <= 3) ? x : (x - 4);
}

static void shift_rows(union xmm_reg *dst, const union xmm_reg *src)
{
        /* cyclic shift last 3 rows of the input */
        int j;
        union xmm_reg tmp = *src;

        /* bytes to matrix:
           0 1 2 3 < columns (i)
           ----------+
           0 4 8 C  | 0 < rows (j)
           1 5 9 D  | 1
           2 6 A E  | 2
           3 7 B F  | 3

           THIS IS THE KEY: progressively move elements to HIGHER
           numbered columnar values within a row.

           Each dword is a column with the MSB as the bottom element
           i is the column index, selects the dword
           j is the row index,
           we shift row zero by zero, row 1 by 1 and row 2 by 2 and
           row 3 by 3, cyclically */
        for (j = 0; j < MAX_DWORDS_PER_XMM; j++) {
                int i;

                for (i = 0; i < MAX_DWORDS_PER_XMM; i++)
                        dst->byte[i*4+j] = tmp.byte[wrap_pos(i+j)*4+j];
        }

}

static void inverse_shift_rows(union xmm_reg *dst, const union xmm_reg *src)
{
        uint32_t j;
        union xmm_reg tmp = *src;

        /* THIS IS THE KEY: progressively move elements to LOWER
           numbered columnar values within a row.

           Each dword is a column with the MSB as the bottom element
           i is the column index, selects the dword
           j is the row index,
           we shift row zero by zero, row 1 by 1 and row 2 by 2 and
           row 3 by 3, cyclically */
        for (j = 0; j < MAX_DWORDS_PER_XMM; j++) {
                uint32_t i;

                for (i = 0; i < MAX_DWORDS_PER_XMM; i++)
                        dst->byte[i*4+j] = tmp.byte[wrap_neg(i - j) * 4 + j];
        }
}

/* ========================================================================== */
/* AESNI emulation functions */
/* ========================================================================== */

IMB_DLL_LOCAL void emulate_AESKEYGENASSIST(union xmm_reg *dst,
                                           const union xmm_reg *src,
                                           const uint32_t imm8)
{
        union xmm_reg tmp;
        const uint32_t rcon = (imm8 & 0xFF);

        substitute_bytes(&tmp, src);

        dst->dword[3] = rot(tmp.dword[3]) ^ rcon;
        dst->dword[2] = tmp.dword[3];
        dst->dword[1] = rot(tmp.dword[1]) ^ rcon;
        dst->dword[0] = tmp.dword[1];
}

IMB_DLL_LOCAL void emulate_AESENC(union xmm_reg *dst,
                                  const union xmm_reg *src)
{
        union xmm_reg tmp = *dst;

        shift_rows(&tmp, &tmp);
        substitute_bytes(&tmp, &tmp);
        mix_columns(&tmp, &tmp);
        xor_xmm(dst, &tmp, src);
}

IMB_DLL_LOCAL void emulate_AESENCLAST(union xmm_reg *dst,
                                      const union xmm_reg *src)
{
        union xmm_reg tmp = *dst;

        shift_rows(&tmp, &tmp);
        substitute_bytes(&tmp, &tmp);
        xor_xmm(dst, &tmp, src);
}

IMB_DLL_LOCAL void emulate_AESDEC(union xmm_reg *dst,
                                  const union xmm_reg *src)
{
        union xmm_reg tmp = *dst;

        inverse_shift_rows(&tmp, &tmp);
        inverse_substitute_bytes(&tmp, &tmp);
        inverse_mix_columns(&tmp, &tmp);
        xor_xmm(dst, &tmp, src);
}

IMB_DLL_LOCAL void emulate_AESDECLAST(union xmm_reg *dst,
                                      const union xmm_reg *src)
{
        union xmm_reg tmp = *dst;

        inverse_shift_rows(&tmp, &tmp);
        inverse_substitute_bytes(&tmp, &tmp);
        xor_xmm(dst, &tmp, src);
}

IMB_DLL_LOCAL void emulate_AESIMC(union xmm_reg *dst,
                                  const union xmm_reg *src)
{
        inverse_mix_columns(dst, src);
}

/* ========================================================================== */
/* PCLMULQDQ emulation function */
/* ========================================================================== */

IMB_DLL_LOCAL void
emulate_PCLMULQDQ(union xmm_reg *src1_dst, const union xmm_reg *src2,
                  const uint32_t imm8)
{
        uint64_t x; /* input 64-bit word */
        uint64_t r0 = 0, r1 = 0; /* result 128-bit word (2x64) */
        uint64_t y0 = 0, y1 = 0; /* y0/y1 - input word; 128 bits */
        uint64_t mask;

        if (imm8 & 0x01)
                x = src1_dst->qword[1];
        else
                x = src1_dst->qword[0];

        if (imm8 & 0x10)
                y0 = src2->qword[1];
        else
                y0 = src2->qword[0];

        /*
         * Implementation based on PCLMULQDQ helper function from Intel(R) SDM.
         *   X[] = one of 64-bit words from src1_dst
         *   Y[] = one of 64-bit words from src2
         *
         *     define PCLMUL128(X, Y) :
         *         FOR i <-- 0 to 63 :
         *             TMP[i] <-- X[0] and Y[i]
         *             FOR j <-- 1 to i :
         *                 TMP[i] <-- TMP[i] xor (X[j] and Y[i - j])
         *             DEST[i] <-- TMP[i]
         *         FOR i <-- 64 to 126 :
         *             TMP[i] <-- 0
         *             FOR j <-- i - 63 to 63 :
         *                 TMP[i] <-- TMP[i] xor (X[j] and Y[i - j])
         *             DEST[i] <-- TMP[i]
         *         DEST[127] <-- 0;
         *         RETURN DEST
         */

        /*
         * This implementation is focused on reducing number of operations per
         * bit and it deverges from the above by doing series of 128-bit xor's
         * and shifts.
         *   Y := zero extended src2 (128 bits)
         *   R := zero (128 bits)
         *   for N := 0 to 63 step 1
         *       if (bit N of src1 == 1) then R := R XOR Y
         *       Y := Y << 1
         *   return R
         */
        for (mask = 1ULL; mask != 0ULL; mask <<= 1) {
                const uint64_t mask_bit63 = (1ULL << 63);

                if (x & mask) {
                        r0 ^= y0;
                        r1 ^= y1;
                }
                y1 <<= 1;
                if (y0 & mask_bit63)
                        y1 |= 1ULL;
                y0 <<= 1;
        }

        /* save the result */
        src1_dst->qword[0] = r0;
        src1_dst->qword[1] = r1;
}
