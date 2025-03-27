/*******************************************************************************
  Copyright (c) 2009-2021, Intel Corporation

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

/*-----------------------------------------------------------------------
 *
 * An implementation of SNOW 3G, the core algorithm for the
 * 3GPP Confidentiality and Integrity algorithms.
 *
 *-----------------------------------------------------------------------*/

#ifndef SNOW3G_COMMON_H
#define SNOW3G_COMMON_H

#include <stdio.h> /* printf() */
#include <string.h> /* memset(), memcpy() */
#include <stdint.h>

#include "include/constant_lookup.h"
#include "intel-ipsec-mb.h"
#include "wireless_common.h"
#include "include/snow3g.h"
#include "include/snow3g_tables.h"
#ifdef NO_AESNI
#include "include/aesni_emu.h"
#endif
#include "clear_regs_mem.h"
#ifdef SAFE_PARAM
#include "include/error.h"
#endif

#define CLEAR_MEM clear_mem
#define CLEAR_VAR clear_var

#define MAX_KEY_LEN (16)
#define SNOW3G_4_BYTES (4)
#define SNOW3G_8_BYTES (8)
#define SNOW3G_8_BITS (8)
#define SNOW3G_16_BYTES (16)
#define SNOW3G_16_BITS (16)

#define SNOW3G_BLOCK_SIZE (8)

#define SNOW3G_KEY_LEN_IN_BYTES (16) /* 128b */
#define SNOW3G_IV_LEN_IN_BYTES (16)  /* 128b */

#define SNOW3GCONSTANT (0x1b)

/* Range of input data for SNOW3G is from 1 to 2^32 bits */
#define SNOW3G_MIN_LEN 1
#define SNOW3G_MAX_BITLEN (UINT32_MAX)
#define SNOW3G_MAX_BYTELEN (UINT32_MAX / 8)

typedef union SafeBuffer {
        uint64_t b64;
        uint32_t b32[2];
        uint8_t b8[SNOW3G_8_BYTES];
} SafeBuf;

typedef struct snow3gKeyState1_s {
        /* 16 LFSR stages */
        uint32_t LFSR_S[16];
        /* 3 FSM states */
        uint32_t FSM_R3;
        uint32_t FSM_R2;
        uint32_t FSM_R1;
} DECLARE_ALIGNED(snow3gKeyState1_t, 16);

typedef struct snow3gKeyState4_s {
        /* 16 LFSR stages */
        __m128i LFSR_X[16];
        /* 3 FSM states */
        __m128i FSM_X[3];
        uint32_t iLFSR_X;
} snow3gKeyState4_t;

#ifdef AVX2
typedef struct snow3gKeyState8_s {
        /* 16 LFSR stages */
        __m256i LFSR_X[16];
        /* 3 FSM states */
        __m256i FSM_X[3];
        uint32_t iLFSR_X;
} snow3gKeyState8_t;
#endif /* AVX2 */

/**
 * @brief Finds minimum 32-bit value in an array
 * @return Min 32-bit value
 */
static inline uint32_t
length_find_min(const uint32_t *out_array, const size_t dim_array)
{
        size_t i;
        uint32_t min = 0;

        if (dim_array > 0)
                min  = out_array[0];

        for (i = 1; i < dim_array; i++)
                if (out_array[i] < min)
                        min = out_array[i];

        return min;
}

/**
 * @brief Subtracts \a subv from a vector of 32-bit words
 */
static inline void
length_sub(uint32_t *out_array, const size_t dim_array, const uint32_t subv)
{
        size_t i;

        for (i = 0; i < dim_array; i++)
                out_array[i] -= subv;
}

#ifdef SAFE_PARAM
/**
 * @brief Checks vector of length values against 0 and SNOW3G_MAX_BYTELEN values
 * @retval 0 incorrect length value found
 * @retval 1 all OK
 */
static inline uint32_t
length_check(const uint32_t *out_array, const size_t dim_array)
{
        size_t i;

        if (out_array == NULL) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return 0;
        }

        for (i = 0; i < dim_array; i++) {
                if ((out_array[i] == 0) ||
                    (out_array[i] > SNOW3G_MAX_BYTELEN)) {
                        imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                        return 0;
                    }
        }

        return 1;
}
#endif
/**
 * @brief Copies 4 32-bit length values into an array
 */
static inline void
length_copy_4(uint32_t *out_array,
              const uint32_t length1, const uint32_t length2,
              const uint32_t length3, const uint32_t length4)
{
        out_array[0] = length1;
        out_array[1] = length2;
        out_array[2] = length3;
        out_array[3] = length4;
}

/**
 * @brief Copies 8 32-bit length values into an array
 */
static inline void
length_copy_8(uint32_t *out_array,
              const uint32_t length1, const uint32_t length2,
              const uint32_t length3, const uint32_t length4,
              const uint32_t length5, const uint32_t length6,
              const uint32_t length7, const uint32_t length8)
{
        out_array[0] = length1;
        out_array[1] = length2;
        out_array[2] = length3;
        out_array[3] = length4;
        out_array[4] = length5;
        out_array[5] = length6;
        out_array[6] = length7;
        out_array[7] = length8;
}
#ifdef SAFE_PARAM
/**
 * @brief Checks vector of pointers against NULL
 * @retval 0 incorrect pointer found
 * @retval 1 all OK
 */
static inline int
ptr_check(void *out_array[], const size_t dim_array, const int errnum)
{
        size_t i;

        if (out_array == NULL) {
                imb_set_errno(NULL, errnum);
                return 0;
        }
        for (i = 0; i < dim_array; i++)
                if (out_array[i] == NULL) {
                        imb_set_errno(NULL, errnum);
                        return 0;
                }
        return 1;
}
#endif

#ifdef SAFE_PARAM
/**
 * @brief Checks vector of const pointers against NULL
 * @retval 0 incorrect pointer found
 * @retval 1 all OK
 */
static inline int
cptr_check(const void * const out_array[],
           const size_t dim_array,
           const int errnum)
{
        size_t i;

        if (out_array == NULL) {
                imb_set_errno(NULL, errnum);
                return 0;
        }
        for (i = 0; i < dim_array; i++)
                if (out_array[i] == NULL) {
                        imb_set_errno(NULL, errnum);
                        return 0;
                }

        return 1;
}
#endif

/**
 * @brief Copies 4 pointers into an array
 */
static inline void
ptr_copy_4(void *out_array[],
           void *ptr1, void *ptr2, void *ptr3, void *ptr4)
{
        out_array[0] = ptr1;
        out_array[1] = ptr2;
        out_array[2] = ptr3;
        out_array[3] = ptr4;
}

/**
 * @brief Copies 4 const pointers into an array
 */
static inline void
cptr_copy_4(const void *out_array[],
            const void *ptr1, const void *ptr2,
            const void *ptr3, const void *ptr4)
{
        out_array[0] = ptr1;
        out_array[1] = ptr2;
        out_array[2] = ptr3;
        out_array[3] = ptr4;
}

/**
 * @brief Copies 8 pointers into an array
 */
static inline void
ptr_copy_8(void *out_array[],
           void *ptr1, void *ptr2, void *ptr3, void *ptr4,
           void *ptr5, void *ptr6, void *ptr7, void *ptr8)
{
        out_array[0] = ptr1;
        out_array[1] = ptr2;
        out_array[2] = ptr3;
        out_array[3] = ptr4;
        out_array[4] = ptr5;
        out_array[5] = ptr6;
        out_array[6] = ptr7;
        out_array[7] = ptr8;
}

/**
 * @brief Copies 8 const pointers into an array
 */
static inline void
cptr_copy_8(const void *out_array[],
            const void *ptr1, const void *ptr2,
            const void *ptr3, const void *ptr4,
            const void *ptr5, const void *ptr6,
            const void *ptr7, const void *ptr8)
{
        out_array[0] = ptr1;
        out_array[1] = ptr2;
        out_array[2] = ptr3;
        out_array[3] = ptr4;
        out_array[4] = ptr5;
        out_array[5] = ptr6;
        out_array[6] = ptr7;
        out_array[7] = ptr8;
}

#ifdef AVX2
/**
 * @brief Loads 2x128-bit vectors into one 256-bit vector
 * @param[in] hi  pointer to 128-bit vector (high)
 * @param[in] lo  pointer to 128-bit vector (low)
 * @return 256-bit vector
 */
static inline __m256i load_2xm128i_into_m256i(const void *hi, const void *lo)
{
        const __m128i lo128 = _mm_loadu_si128((const __m128i *) lo);
        const __m128i hi128 = _mm_loadu_si128((const __m128i *) hi);

        return _mm256_inserti128_si256(_mm256_castsi128_si256(lo128), hi128, 1);
}

/**
 * @brief Broadcasts 128-bit data into 256-bit vector
 * @param[in] ptr  pointer to a 128-bit vector
 * @return 256-bit vector
 */
static inline __m256i broadcast_m128i_to_m256i(const void *ptr)
{
        return _mm256_castps_si256(_mm256_broadcast_ps((const __m128 *)ptr));
}
#endif /* AVX2 */

/**
 * @brief Wrapper for safe lookup of 16 indexes in 256x8-bit table (sse/avx)
 * @param[in] indexes  vector of 16x8-bit indexes to be looked up
 * @param[in] lut      pointer to a 256x8-bit table
 * @return 16x8-bit values looked in \a lut using 16x8-bit \a indexes
 */
static inline __m128i lut16x8b_256(const __m128i indexes, const void *lut)
{
#if defined(AVX2) || defined(AVX)
        return lookup_16x8bit_avx(indexes, lut);
#else
        return lookup_16x8bit_sse(indexes, lut);
#endif
}

/**
 * @brief LFSR array shift by 2 positions
 * @param[in/out] pCtx  key state context structure
 */
static inline void ShiftTwiceLFSR_1(snow3gKeyState1_t *pCtx)
{
        int i;

        for (i = 0; i < 14; i++)
                pCtx->LFSR_S[i] = pCtx->LFSR_S[i + 2];
}

/**
 * @brief SNOW3G S2 mix column correction function vs AESENC operation
 *
 * Mix column AES GF() reduction poly is 0x1B and SNOW3G reduction poly is 0x69.
 * The fix-up value is 0x1B ^ 0x69 = 0x72 and needs to be applied on selected
 * bytes of the 32-bit word.
 *
 * 'aesenclast' operation does not perform mix column operation and
 * allows to determine the fix-up value to be applied on result of 'aesenc'
 * in order to produce correct result for SNOW3G.
 *
 * This function implements more scalable SIMD method to apply the fix-up value
 * for multiple stream at the same time.
 *
 * a = \a no_mixc bit-31
 * b = \a no_mixc bit-23
 * c = \a no_mixc bit-15
 * d = \a no_mixc bit-7
 *
 * mask0_f(), mask1_f(), mask2_f() and mask3_f() functions
 * specify if corresponding byte of \a mixc word, i.e. 0, 1, 2 or 3
 * respectively, should be corrected.
 * Definition of the functions:
 *     mask0_f(a, b, c, d) = c'd + cd' => c xor d
 *     mask1_f(a, b, c, d) = b'c + bc' => b xor c
 *     mask2_f(a, b, c, d) = a'b + ab' => a xor b
 *     mask3_f(a, b, c, d) = a'd + ad' => d xor a
 * The above are resolved through SIMD instructions: and, cmpgt, shuffle and
 * xor. As the result mask is obtained with 0xff byte value at positions
 * that require 0x72 fix up value to be applied.
 *
 * @param no_mixc result of 'aesenclast' operation, 4 x 32-bit words
 * @param mixc    result of 'aesenc' operation, 4 x 32-bit words
 *
 * @return corrected \a mixc for SNOW3G S2, 4 x 32-bit words
 */
static inline __m128i s2_mixc_fixup_4(const __m128i no_mixc, const __m128i mixc)
{
        const __m128i m_shuf = _mm_set_epi32(0x0c0f0e0d, 0x080b0a09,
                                             0x04070605, 0x00030201);
        const __m128i m_zero = _mm_setzero_si128();
        const __m128i m_mask = _mm_set1_epi32(0x72727272);
        __m128i pattern, pattern_shuf, fixup;

        /* Using signed compare to return 0xFF when
         * the most significant bit of no_mixc is set.
         */
        pattern = _mm_cmpgt_epi8(m_zero, no_mixc);
        pattern_shuf = _mm_shuffle_epi8(pattern, m_shuf);
        pattern = _mm_xor_si128(pattern, pattern_shuf);

        fixup = _mm_and_si128(m_mask, pattern);

        return _mm_xor_si128(fixup, mixc);
}
#ifdef AVX2
static inline __m256i
s2_mixc_fixup_avx2(const __m256i no_mixc, const __m256i mixc)
{
        const __m256i m_shuf =
                _mm256_set_epi32(0x0c0f0e0d, 0x080b0a09,
                                 0x04070605, 0x00030201,
                                 0x0c0f0e0d, 0x080b0a09,
                                 0x04070605, 0x00030201);
        const __m256i m_zero = _mm256_setzero_si256();
        const __m256i m_mask = _mm256_set1_epi32(0x72727272);
        __m256i pattern, pattern_shuf, fixup;

        /* Using signed compare to return 0xFF when
         * the most significant bit of no_mixc is set.
         */
        pattern = _mm256_cmpgt_epi8(m_zero, no_mixc);
        pattern_shuf = _mm256_shuffle_epi8(pattern, m_shuf);
        pattern = _mm256_xor_si256(pattern, pattern_shuf);

        fixup = _mm256_and_si256(m_mask, pattern);

        return _mm256_xor_si256(fixup, mixc);
}
#endif

/**
 * @brief SNOW3G S2 mix column correction function vs AESENC operation
 *
 * @param no_mixc result of 'aesenclast' operation, 32-bit word index 0 only
 * @param mixc    result of 'aesenc' operation, 32-bit word index 0 only
 *
 * @return corrected \a mixc 32-bit word for SNOW3G S2
 */
static inline uint32_t
s2_mixc_fixup_scalar(const __m128i no_mixc, const __m128i mixc)
{
        return _mm_cvtsi128_si32(s2_mixc_fixup_4(no_mixc, mixc));
}

/**
 * @brief Sbox S1 maps a 32bit input to a 32bit output
 *
 * @param[in] x  32-bit word to be passed through S1 box
 *
 * @return \a x transformed through S1 box
 */
static inline uint32_t S1_box(const uint32_t x)
{
#ifdef NO_AESNI
        union xmm_reg key, v;

        key.qword[0] = key.qword[1] = 0;

        v.dword[0] = v.dword[1] =
                v.dword[2] = v.dword[3] = x;

        emulate_AESENC(&v, &key);
        return v.dword[0];
#else
        __m128i m;

        /*
         * Because of mix column operation the 32-bit word has to be
         * broadcasted across the 128-bit vector register for S1/AESENC
         */
        m = _mm_shuffle_epi32(_mm_cvtsi32_si128(x), 0);
        m = _mm_aesenc_si128(m, _mm_setzero_si128());
        return _mm_cvtsi128_si32(m);
#endif
}

/**
 * @brief Sbox S1 maps a 2x32bit input to a 2x32bit output
 *
 * @param[in] x1  32-bit word to be passed through S1 box
 * @param[in] x2  32-bit word to be passed through S1 box
 */
static inline void S1_box_2(uint32_t *x1, uint32_t *x2)
{
#ifdef NO_AESNI
        /* reuse S1_box() for NO_AESNI path */
        *x1 = S1_box(*x1);
        *x2 = S1_box(*x2);
#else
        const __m128i m_zero = _mm_setzero_si128();
        __m128i m1, m2;

        m1 = _mm_shuffle_epi32(_mm_cvtsi32_si128(*x1), 0);
        m1 = _mm_aesenc_si128(m1, m_zero);
        m2 = _mm_shuffle_epi32(_mm_cvtsi32_si128(*x2), 0);
        m2 = _mm_aesenc_si128(m2, m_zero);
        *x1 = _mm_cvtsi128_si32(m1);
        *x2 = _mm_cvtsi128_si32(m2);
#endif
}

/**
 * @brief Sbox S1 maps a 4x32bit input to a 4x32bit output
 *
 * @param[in] x  vector of 4 32-bit words to be passed through S1 box
 *
 * @return 4x32-bits from \a x transformed through S1 box
 */
static inline __m128i S1_box_4(const __m128i x)
{
        const __m128i m_shuf_r = _mm_set_epi32(0x0306090c, 0x0f020508,
                                               0x0b0e0104, 0x070a0d00);
        const __m128i m1 = _mm_shuffle_epi8(x, m_shuf_r);
        const __m128i m_zero = _mm_setzero_si128();

        /*
         * Previously 32-bit word from one stream was broadcasted
         * across 128-bit word for AESENC. Then the 1st word was
         * used as output.
         * With this method, words from multiple streams are
         * pre-shuffled and one AESENC can process all four words.
         */
#ifdef NO_AESNI
        union xmm_reg key, vt;

        _mm_storeu_si128((__m128i *) &key.qword[0], m_zero);
        _mm_storeu_si128((__m128i *) &vt.qword[0], m1);

        emulate_AESENC(&vt, &key);

        return _mm_loadu_si128((const __m128i *) &vt.qword[0]);
#else
        return _mm_aesenc_si128(m1, m_zero);
#endif
}

#ifdef AVX2
/**
 * @brief Sbox S1 maps a 8x32bit input to a 8x32bit output
 *
 * @param[in] x  vector of 8 32-bit words to be passed through S1 box
 *
 * @return 8x32-bits from \a x transformed through S1 box
 */
static inline __m256i S1_box_8(const __m256i x)
{
        const __m128i x1 = _mm256_castsi256_si128(x);
        const __m128i x2 = _mm256_extractf128_si256(x, 1);
        const __m128i m_zero = _mm_setzero_si128();
        const __m128i m_shuf_r = _mm_set_epi32(0x0306090c, 0x0f020508,
                                               0x0b0e0104, 0x070a0d00);
        __m128i m1, m2;

        m1 = _mm_shuffle_epi8(x1, m_shuf_r);
        m2 = _mm_shuffle_epi8(x2, m_shuf_r);

        m1 = _mm_aesenc_si128(m1, m_zero);
        m2 = _mm_aesenc_si128(m2, m_zero);

        /* return [ 255 - 128 : m5 | 127 - 0 : m1 ] */
        return _mm256_inserti128_si256(_mm256_castsi128_si256(m1), m2, 1);
}
#endif /* AVX2 */

/**
 * @brief Sbox S2 maps a 32-bit input to a 32-bit output
 *
 * @param[in] x  32-bit word to be passed through S2 box
 *
 * @return \a x transformed through S2 box
 */
static inline uint32_t S2_box(const uint32_t x)
{
#ifdef NO_AESNI
        /* Perform invSR(SQ(x)) transform */
        const __m128i par_lut =
                lut16x8b_256(_mm_cvtsi32_si128(x), snow3g_invSR_SQ);
        const uint32_t new_x = _mm_cvtsi128_si32(par_lut);
        union xmm_reg key, v, v_fixup;

        key.qword[0] = key.qword[1] = 0;

        v.dword[0] = v.dword[1] =
                v.dword[2] = v.dword[3] = new_x;

        v_fixup = v;

        emulate_AESENC(&v, &key);
        emulate_AESENCLAST(&v_fixup, &key);

        const __m128i ret_mixc =
                _mm_loadu_si128((const __m128i *) &v.qword[0]);
        const __m128i ret_nomixc =
                _mm_loadu_si128((const __m128i *) &v_fixup.qword[0]);

        return s2_mixc_fixup_scalar(ret_nomixc, ret_mixc);
#else

#ifndef SAFE_LOOKUP
        const uint8_t *w3 = (const uint8_t *)&snow3g_table_S2[x & 0xff];
        const uint8_t *w1 = (const uint8_t *)&snow3g_table_S2[(x >> 16) & 0xff];
        const uint8_t *w2 = (const uint8_t *)&snow3g_table_S2[(x >> 8) & 0xff];
        const uint8_t *w0 = (const uint8_t *)&snow3g_table_S2[(x >> 24) & 0xff];

        return *((const uint32_t *)&w3[3]) ^
                *((const uint32_t *)&w1[1]) ^
                *((const uint32_t *)&w2[2]) ^
                *((const uint32_t *)&w0[0]);

#else
        /*
         * Because of mix column operation the 32-bit word has to be
         * broadcasted across the 128-bit vector register for S1/AESENC
         */
        /* Perform invSR(SQ(x)) transform */
        const __m128i par_lut =
                lut16x8b_256(_mm_cvtsi32_si128(x), snow3g_invSR_SQ);
        const __m128i m = _mm_shuffle_epi32(par_lut, 0);

        /*
         * aesenclast does not perform mix column operation and
         * allows to determine the fix-up value to be applied
         * on result of aesenc to produce correct result for SNOW3G.
         */
        const __m128i ret_nomixc =
                _mm_aesenclast_si128(m, _mm_setzero_si128());
        const __m128i ret_mixc =
                _mm_aesenc_si128(m, _mm_setzero_si128());

        return s2_mixc_fixup_scalar(ret_nomixc, ret_mixc);
#endif

#endif
}

/**
 * @brief Sbox S2 maps a 2x32bit input to a 2x32bit output
 *
 * @param[in/out] x1  32-bit word to be passed through S2 box
 * @param[in/out] x2  32-bit word to be passed through S2 box
 */
static inline void S2_box_2(uint32_t *x1, uint32_t *x2)
{
#ifdef NO_AESNI
        *x1 = S2_box(*x1);
        *x2 = S2_box(*x2);
#else

#ifdef SAFE_LOOKUP
        /* Perform invSR(SQ(x)) transform through a lookup table */
        const __m128i m_zero = _mm_setzero_si128();
#ifdef SSE
        const __m128i x_vec = _mm_insert_epi32(_mm_cvtsi32_si128(*x1), *x2, 1);
#else
        const __m128i x_vec = _mm_set_epi32(0, 0, *x2, *x1);
#endif
        const __m128i new_x = lut16x8b_256(x_vec, snow3g_invSR_SQ);
        __m128i m1, m2, f1, f2;

        m1 = _mm_shuffle_epi32(new_x, 0b00000000);
        m2 = _mm_shuffle_epi32(new_x, 0b01010101);

        f1 = _mm_aesenclast_si128(m1, m_zero);
        m1 = _mm_aesenc_si128(m1, m_zero);
        f2 = _mm_aesenclast_si128(m2, m_zero);
        m2 = _mm_aesenc_si128(m2, m_zero);

        /*
         * Put results of AES operations back into one vector
         * for further fix up
         * m1 = [ 0-31 m1 | 0-31 m2 | 32-63 m1 | 32-63 m2 ]
         */
        m1 = _mm_unpacklo_epi32(m1, m2);
        f1 = _mm_unpacklo_epi32(f1, f2);

        m1 = s2_mixc_fixup_4(f1, m1);

        *x1 = _mm_extract_epi32(m1, 0);
        *x2 = _mm_extract_epi32(m1, 1);
#else
        *x1 = S2_box(*x1);
        *x2 = S2_box(*x2);
#endif

#endif /* !NO_AESNI */
}

/**
 * @brief Sbox S2 maps a 4x32bit input to a 4x32bit output
 *
 * @param[in] x  vector of 4 32-bit words to be passed through S2 box
 *
 * @return 4x32-bits from \a x transformed through S2 box
 */
static inline __m128i S2_box_4(const __m128i x)
{
        const __m128i m_zero = _mm_setzero_si128();
        const __m128i m_shuf_r = _mm_set_epi32(0x0306090c, 0x0f020508,
                                               0x0b0e0104, 0x070a0d00);

        /* Perform invSR(SQ(x)) transform through a lookup table */
        const __m128i new_x = lut16x8b_256(x, snow3g_invSR_SQ);
        __m128i m1 = _mm_shuffle_epi8(new_x, m_shuf_r);

        /* use AESNI operations for the rest of the S2 box */
#ifdef NO_AESNI
        union xmm_reg key;
        union xmm_reg vt, ft;

        _mm_storeu_si128((__m128i *) &key.qword[0], m_zero);

        _mm_storeu_si128((__m128i *) &vt.qword[0], m1);
        _mm_storeu_si128((__m128i *) &ft.qword[0], m1);
        emulate_AESENC(&vt, &key);
        emulate_AESENCLAST(&ft, &key);

        return s2_mixc_fixup_4(_mm_loadu_si128((const __m128i *) &ft.qword[0]),
                               _mm_loadu_si128((const __m128i *) &vt.qword[0]));
#else
        __m128i f1 = _mm_aesenclast_si128(m1, m_zero);

        m1 = _mm_aesenc_si128(m1, m_zero);

        return s2_mixc_fixup_4(f1, m1);
#endif
}

/**
 * @brief Sbox S2 maps a 2x4x32bit input to a 2x4x32bit output
 *
 * @param[in/out] in_out1  vector of 4 32-bit words to be passed through S2 box
 * @param[in/out] in_out2  vector of 4 32-bit words to be passed through S2 box
 */
static inline void S2_box_2x4(__m128i *in_out1, __m128i *in_out2)
{
#ifdef NO_AESNI
        *in_out1 = S2_box_4(*in_out1);
        *in_out2 = S2_box_4(*in_out2);
#else
        /*
         * Perform invSR(SQ(x)) transform through a lookup table and
         * use AESNI operations for the rest of the S2 box
         */
        const __m128i m_zero = _mm_setzero_si128();
        const __m128i x1 = lut16x8b_256(*in_out1, snow3g_invSR_SQ);
        const __m128i x2 = lut16x8b_256(*in_out2, snow3g_invSR_SQ);
        const __m128i m_shuf_r = _mm_set_epi32(0x0306090c, 0x0f020508,
                                               0x0b0e0104, 0x070a0d00);
        __m128i m1, m2, f1, f2;

        m1 = _mm_shuffle_epi8(x1, m_shuf_r);
        m2 = _mm_shuffle_epi8(x2, m_shuf_r);

        f1 = _mm_aesenclast_si128(m1, m_zero);
        m1 = _mm_aesenc_si128(m1, m_zero);
        f2 = _mm_aesenclast_si128(m2, m_zero);
        m2 = _mm_aesenc_si128(m2, m_zero);

        *in_out1 = s2_mixc_fixup_4(f1, m1);
        *in_out2 = s2_mixc_fixup_4(f2, m2);
#endif
}

#ifdef AVX2
/**
 * @brief Sbox S2 maps a 8x32bit input to a 8x32bit output
 *
 * @param[in] x  vector of 8 32-bit words to be passed through S2 box
 *
 * @return 8x32-bits from \a x transformed through S2 box
 */
static inline __m256i S2_box_8(const __m256i x)
{
        /* Perform invSR(SQ(x)) transform through a lookup table */
        const __m256i new_x = lookup_32x8bit_avx2(x, snow3g_invSR_SQ);

        /* use AESNI operations for the rest of the S2 box */
        const __m128i m_zero = _mm_setzero_si128();
        const __m128i x1 = (__m128i) _mm256_castsi256_si128(new_x);
        const __m128i x2 = (__m128i) _mm256_extractf128_si256(new_x, 1);
        const __m128i m_shuf_r = _mm_set_epi32(0x0306090c, 0x0f020508,
                                               0x0b0e0104, 0x070a0d00);
        __m128i m1, m2, f1, f2;
        __m256i m, f;

        m1 = _mm_shuffle_epi8(x1, m_shuf_r);
        m2 = _mm_shuffle_epi8(x2, m_shuf_r);

        f1 = _mm_aesenclast_si128(m1, m_zero);
        m1 = _mm_aesenc_si128(m1, m_zero);
        f2 = _mm_aesenclast_si128(m2, m_zero);
        m2 = _mm_aesenc_si128(m2, m_zero);

        m = _mm256_castsi128_si256(m1);
        f = _mm256_castsi128_si256(f1);

        m = _mm256_inserti128_si256(m, m2, 1);
        f = _mm256_inserti128_si256(f, f2, 1);

        return s2_mixc_fixup_avx2(f, m);
}
#endif /* AVX2 */

/**
 * @brief MULalpha SNOW3G operation on 4 8-bit values at the same time
 *
 * Function picks the right byte from the register to run MULalpha operation on.
 * MULalpha is implemented through 8 16-byte tables and pshufb is used to
 * look the tables up. This approach is possible because
 * MULalpha operation has linear nature.
 * Final operation result is calculated via byte re-arrangement on
 * the lookup results and an XOR operation.
 *
 * @param [in] L0       4 x 32-bit LFSR[0]
 * @return 4 x 32-bit MULalpha(L0 >> 24)
 */
static inline
__m128i MULa_4(const __m128i L0)
{
#ifdef SAFE_LOOKUP
        const __m128i gather_clear_mask =
                _mm_set_epi8(0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                             0x80, 0x80, 0x80, 0x80, 15, 11, 7, 3);
        const __m128i low_nibble_mask = _mm_set1_epi32(0x0f0f0f0f);
        __m128i b0, b1, b2, b3, tl, th;

        th = _mm_shuffle_epi8(L0, gather_clear_mask);

        tl = _mm_and_si128(th, low_nibble_mask);
        b0 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte0_low);
        b1 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte1_low);
        b2 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte2_low);
        b3 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte3_low);

        b0 = _mm_shuffle_epi8(b0, tl);
        b1 = _mm_shuffle_epi8(b1, tl);
        b2 = _mm_shuffle_epi8(b2, tl);
        b3 = _mm_shuffle_epi8(b3, tl);

        b0 = _mm_unpacklo_epi8(b0, b1);
        b2 = _mm_unpacklo_epi8(b2, b3);
        tl = _mm_unpacklo_epi16(b0, b2);

        b0 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte0_hi);
        b1 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte1_hi);
        b2 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte2_hi);
        b3 = _mm_loadu_si128((const __m128i *)snow3g_MULa_byte3_hi);

        th = _mm_and_si128(_mm_srli_epi32(th, 4), low_nibble_mask);

        b0 = _mm_shuffle_epi8(b0, th);
        b1 = _mm_shuffle_epi8(b1, th);
        b2 = _mm_shuffle_epi8(b2, th);
        b3 = _mm_shuffle_epi8(b3, th);

        b0 = _mm_unpacklo_epi8(b0, b1);
        b2 = _mm_unpacklo_epi8(b2, b3);
        th = _mm_unpacklo_epi16(b0, b2);

        return _mm_xor_si128(th, tl);
#else
        const uint8_t L0IDX0 = _mm_extract_epi8(L0, 3);
        const uint8_t L0IDX1 = _mm_extract_epi8(L0, 7);
        const uint8_t L0IDX2 = _mm_extract_epi8(L0, 11);
        const uint8_t L0IDX3 = _mm_extract_epi8(L0, 15);

        return _mm_setr_epi32(snow3g_table_A_mul[L0IDX0],
                              snow3g_table_A_mul[L0IDX1],
                              snow3g_table_A_mul[L0IDX2],
                              snow3g_table_A_mul[L0IDX3]);
#endif
}

/**
 * @brief MULalpha SNOW3G operation on 2 8-bit values at the same time
 *
 * @param [in/out] L0_1  On input, 32-bit LFSR[0].
 *                       On output, 32-bit MULalpha(L0 >> 24)
 * @param [in/out] L0_2  On input, 32-bit LFSR[0].
 *                       On output, 32-bit MULalpha(L0 >> 24)
 */
static inline
void MULa_2(uint32_t *L0_1, uint32_t *L0_2)
{
#ifdef SAFE_LOOKUP
        __m128i in, out;

        in = _mm_cvtsi32_si128(*L0_1);
        in = _mm_insert_epi32(in, *L0_2, 1);
        out = MULa_4(in);

        *L0_1 = _mm_cvtsi128_si32(out);
        *L0_2 = _mm_extract_epi32(out, 1);
#else
        *L0_1 = snow3g_table_A_mul[*L0_1 >> 24];
        *L0_2 = snow3g_table_A_mul[*L0_2 >> 24];
#endif
}

/**
 * @brief MULalpha SNOW3G operation on a 8-bit value.
 *
 * @param [in] L0       32-bit LFSR[0]
 * @return 32-bit MULalpha(L0 >> 24)
 */
static inline
uint32_t MULa(const uint32_t L0)
{
#ifdef SAFE_LOOKUP
        const __m128i L0_vec = _mm_cvtsi32_si128(L0);

        return _mm_cvtsi128_si32(MULa_4(L0_vec));
#else
        return snow3g_table_A_mul[L0 >> 24];
#endif
}

#ifdef AVX2
/**
 * @brief MULalpha SNOW3G operation on 8 8-bit values at the same time
 *
 * Function picks the right byte from the register to run MULalpha operation on.
 * MULalpha is implemented through 8 16-byte tables and pshufb is used to
 * look the tables up. This approach is possible because
 * MULalpha operation has linear nature.
 * Final operation result is calculated via byte re-arrangement on
 * the lookup results and an XOR operation.
 *
 * @param [in] L0       8 x 32-bit LFSR[0]
 * @return 8 x 32-bit MULalpha(L0 >> 24)
 */
static inline
__m256i MULa_8(const __m256i L0)
{
#ifdef SAFE_LOOKUP
        const __m256i byte0_mask = _mm256_set1_epi64x(0x000000ff000000ffULL);
        const __m256i byte1_mask = _mm256_set1_epi64x(0x0000ff000000ff00ULL);
        const __m256i byte2_mask = _mm256_set1_epi64x(0x00ff000000ff0000ULL);
        const __m256i byte3_mask = _mm256_set1_epi64x(0xff000000ff000000ULL);
        const __m256i gather_clear_mask =
                _mm256_set_epi8(0x0f, 0x0f, 0x0f, 0x0f, 0x0b, 0x0b, 0x0b, 0x0b,
                                0x07, 0x07, 0x07, 0x07, 0x03, 0x03, 0x03, 0x03,
                                0x0f, 0x0f, 0x0f, 0x0f, 0x0b, 0x0b, 0x0b, 0x0b,
                                0x07, 0x07, 0x07, 0x07, 0x03, 0x03, 0x03, 0x03);
        const __m256i low_nibble_mask = _mm256_set1_epi32(0x0f0f0f0f);
        __m256i b0, b1, b2, b3, tl, th;

        th = _mm256_shuffle_epi8(L0, gather_clear_mask);

        tl = _mm256_and_si256(th, low_nibble_mask);

        b0 = broadcast_m128i_to_m256i(snow3g_MULa_byte0_low);
        b1 = broadcast_m128i_to_m256i(snow3g_MULa_byte1_low);
        b2 = broadcast_m128i_to_m256i(snow3g_MULa_byte2_low);
        b3 = broadcast_m128i_to_m256i(snow3g_MULa_byte3_low);

        b0 = _mm256_shuffle_epi8(b0, tl);
        b1 = _mm256_shuffle_epi8(b1, tl);
        b2 = _mm256_shuffle_epi8(b2, tl);
        b3 = _mm256_shuffle_epi8(b3, tl);

        b0 = _mm256_and_si256(b0, byte0_mask);
        b1 = _mm256_and_si256(b1, byte1_mask);
        b2 = _mm256_and_si256(b2, byte2_mask);
        b3 = _mm256_and_si256(b3, byte3_mask);

        b0 = _mm256_or_si256(b0, b1);
        b2 = _mm256_or_si256(b2, b3);
        tl = _mm256_or_si256(b0, b2);

        th = _mm256_and_si256(_mm256_srli_epi32(th, 4), low_nibble_mask);

        b0 = broadcast_m128i_to_m256i(snow3g_MULa_byte0_hi);
        b1 = broadcast_m128i_to_m256i(snow3g_MULa_byte1_hi);
        b2 = broadcast_m128i_to_m256i(snow3g_MULa_byte2_hi);
        b3 = broadcast_m128i_to_m256i(snow3g_MULa_byte3_hi);

        b0 = _mm256_shuffle_epi8(b0, th);
        b1 = _mm256_shuffle_epi8(b1, th);
        b2 = _mm256_shuffle_epi8(b2, th);
        b3 = _mm256_shuffle_epi8(b3, th);

        b0 = _mm256_and_si256(b0, byte0_mask);
        b1 = _mm256_and_si256(b1, byte1_mask);
        b2 = _mm256_and_si256(b2, byte2_mask);
        b3 = _mm256_and_si256(b3, byte3_mask);

        b0 = _mm256_or_si256(b0, b1);
        b2 = _mm256_or_si256(b2, b3);
        th = _mm256_or_si256(b0, b2);

        return _mm256_xor_si256(th, tl);
#else
        static const __m256i mask = {
                0x8080800780808003ULL, 0x8080800F8080800BULL,
                0x8080800780808003ULL, 0x8080800F8080800BULL
        };

        return _mm256_i32gather_epi32(snow3g_table_A_mul,
                                      _mm256_shuffle_epi8(L0, mask), 4);
#endif
}
#endif /* AVX2 */

/**
 * @brief DIValpha SNOW3G operation on 4 8-bit values at the same time
 *
 * Function picks the right byte from the register to run DIValpha operation on.
 * DIValpha is implemented through 8 16-byte tables and pshufb is used to
 * look the tables up. This approach is possible because
 * DIValpha operation has linear nature.
 * Final operation result is calculated via byte re-arrangement on
 * the lookup results and an XOR operation.
 *
 * @param [in] L11      4 x 32-bit LFSR[11]
 * @return 4 x 32-bit DIValpha(L11 & 0xff)
 */
static inline
__m128i DIVa_4(const __m128i L11)
{
#ifdef SAFE_LOOKUP
        const __m128i gather_clear_mask =
                _mm_set_epi8(0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                             0x80, 0x80, 0x80, 0x80, 0x0c, 0x08, 0x04, 0x00);
        const __m128i low_nibble_mask = _mm_set1_epi32(0x0f0f0f0f);
        __m128i b0, b1, b2, b3, tl, th;

        th = _mm_shuffle_epi8(L11, gather_clear_mask);

        tl = _mm_and_si128(th, low_nibble_mask);
        b0 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte0_low);
        b1 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte1_low);
        b2 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte2_low);
        b3 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte3_low);

        b0 = _mm_shuffle_epi8(b0, tl);
        b1 = _mm_shuffle_epi8(b1, tl);
        b2 = _mm_shuffle_epi8(b2, tl);
        b3 = _mm_shuffle_epi8(b3, tl);

        b0 = _mm_unpacklo_epi8(b0, b1);
        b2 = _mm_unpacklo_epi8(b2, b3);
        tl = _mm_unpacklo_epi16(b0, b2);

        b0 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte0_hi);
        b1 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte1_hi);
        b2 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte2_hi);
        b3 = _mm_loadu_si128((const __m128i *)snow3g_DIVa_byte3_hi);

        th = _mm_and_si128(_mm_srli_epi32(th, 4), low_nibble_mask);

        b0 = _mm_shuffle_epi8(b0, th);
        b1 = _mm_shuffle_epi8(b1, th);
        b2 = _mm_shuffle_epi8(b2, th);
        b3 = _mm_shuffle_epi8(b3, th);

        b0 = _mm_unpacklo_epi8(b0, b1);
        b2 = _mm_unpacklo_epi8(b2, b3);
        th = _mm_unpacklo_epi16(b0, b2);

        return _mm_xor_si128(th, tl);
#else
        const uint8_t L11IDX0 = _mm_extract_epi8(L11, 0);
        const uint8_t L11IDX1 = _mm_extract_epi8(L11, 4);
        const uint8_t L11IDX2 = _mm_extract_epi8(L11, 8);
        const uint8_t L11IDX3 = _mm_extract_epi8(L11, 12);

        return _mm_setr_epi32(snow3g_table_A_div[L11IDX0],
                              snow3g_table_A_div[L11IDX1],
                              snow3g_table_A_div[L11IDX2],
                              snow3g_table_A_div[L11IDX3]);
#endif
}

/**
 * @brief DIValpha SNOW3G operation on 2 8-bit values at the same time
 *
 * @param [in/out] L11_1 On input, 32-bit LFSR[11].
 *                       On output, 32-bit DIValpha(L11 & 0xff)
 * @param [in/out] L11_2 On input, 32-bit LFSR[11].
 *                       On output, 32-bit DIValpha(L11 & 0xff)
 */
static inline
void DIVa_2(uint32_t *L11_1, uint32_t *L11_2)
{
#ifdef SAFE_LOOKUP
        __m128i in, out;

        in = _mm_cvtsi32_si128(*L11_1);
        in = _mm_insert_epi32(in, *L11_2, 1);
        out = DIVa_4(in);

        *L11_1 = _mm_cvtsi128_si32(out);
        *L11_2 = _mm_extract_epi32(out, 1);
#else
        *L11_1 = snow3g_table_A_div[*L11_1 & 0xff];
        *L11_2 = snow3g_table_A_div[*L11_2 & 0xff];
#endif
}

/**
 * @brief DIValpha SNOW3G operation on a 8-bit value.
 *
 * @param [in] L11       32-bit LFSR[11]
 * @return 32-bit DIValpha(L11 & 0xff)
 */
static inline
uint32_t DIVa(const uint32_t L11)
{
#ifdef SAFE_LOOKUP
        const __m128i L11_vec = _mm_cvtsi32_si128(L11);

        return _mm_cvtsi128_si32(DIVa_4(L11_vec));
#else
        return snow3g_table_A_div[L11 & 0xff];
#endif
}

#ifdef AVX2
/**
 * @brief DIValpha SNOW3G operation on 8 8-bit values at the same time
 *
 * Function picks the right byte from the register to run DIValpha operation on.
 * DIValpha is implemented through 8 16-byte tables and pshufb is used to
 * look the tables up. This approach is possible because
 * DIValpha operation has linear nature.
 * Final operation result is calculated via byte re-arrangement on
 * the lookup results and an XOR operation.
 *
 * @param [in] L11       8 x 32-bit LFSR[11]
 * @return 8 x 32-bit DIValpha(L11 & 0xff)
 */
static inline
__m256i DIVa_8(const __m256i L11)
{
#ifdef SAFE_LOOKUP
        const __m256i byte0_mask = _mm256_set1_epi64x(0x000000ff000000ffULL);
        const __m256i byte1_mask = _mm256_set1_epi64x(0x0000ff000000ff00ULL);
        const __m256i byte2_mask = _mm256_set1_epi64x(0x00ff000000ff0000ULL);
        const __m256i byte3_mask = _mm256_set1_epi64x(0xff000000ff000000ULL);
        const __m256i gather_clear_mask =
                _mm256_set_epi8(0x0c, 0x0c, 0x0c, 0x0c, 0x08, 0x08, 0x08, 0x08,
                                0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00,
                                0x0c, 0x0c, 0x0c, 0x0c, 0x08, 0x08, 0x08, 0x08,
                                0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00);
        const __m256i low_nibble_mask = _mm256_set1_epi32(0x0f0f0f0f);
        __m256i b0, b1, b2, b3, tl, th;

        th = _mm256_shuffle_epi8(L11, gather_clear_mask);

        tl = _mm256_and_si256(th, low_nibble_mask);

        b0 = broadcast_m128i_to_m256i(snow3g_DIVa_byte0_low);
        b1 = broadcast_m128i_to_m256i(snow3g_DIVa_byte1_low);
        b2 = broadcast_m128i_to_m256i(snow3g_DIVa_byte2_low);
        b3 = broadcast_m128i_to_m256i(snow3g_DIVa_byte3_low);

        b0 = _mm256_shuffle_epi8(b0, tl);
        b1 = _mm256_shuffle_epi8(b1, tl);
        b2 = _mm256_shuffle_epi8(b2, tl);
        b3 = _mm256_shuffle_epi8(b3, tl);

        b0 = _mm256_and_si256(b0, byte0_mask);
        b1 = _mm256_and_si256(b1, byte1_mask);
        b2 = _mm256_and_si256(b2, byte2_mask);
        b3 = _mm256_and_si256(b3, byte3_mask);

        b0 = _mm256_or_si256(b0, b1);
        b2 = _mm256_or_si256(b2, b3);
        tl = _mm256_or_si256(b0, b2);

        th = _mm256_and_si256(_mm256_srli_epi32(th, 4), low_nibble_mask);

        b0 = broadcast_m128i_to_m256i(snow3g_DIVa_byte0_hi);
        b1 = broadcast_m128i_to_m256i(snow3g_DIVa_byte1_hi);
        b2 = broadcast_m128i_to_m256i(snow3g_DIVa_byte2_hi);
        b3 = broadcast_m128i_to_m256i(snow3g_DIVa_byte3_hi);

        b0 = _mm256_shuffle_epi8(b0, th);
        b1 = _mm256_shuffle_epi8(b1, th);
        b2 = _mm256_shuffle_epi8(b2, th);
        b3 = _mm256_shuffle_epi8(b3, th);

        b0 = _mm256_and_si256(b0, byte0_mask);
        b1 = _mm256_and_si256(b1, byte1_mask);
        b2 = _mm256_and_si256(b2, byte2_mask);
        b3 = _mm256_and_si256(b3, byte3_mask);

        b0 = _mm256_or_si256(b0, b1);
        b2 = _mm256_or_si256(b2, b3);
        th = _mm256_or_si256(b0, b2);

        return _mm256_xor_si256(th, tl);
#else
        static const __m256i mask = {
                0x8080800480808000ULL, 0x8080800C80808008ULL,
                0x8080800480808000ULL, 0x8080800C80808008ULL
        };

        return _mm256_i32gather_epi32(snow3g_table_A_div,
                                      _mm256_shuffle_epi8(L11, mask), 4);
#endif
}
#endif /* AVX2 */

/**
 * @brief ClockFSM function as defined in SNOW3G standard
 *
 * The FSM has 2 input words S5 and S15 from the LFSR
 * produces a 32 bit output word F.
 *
 * @param[in/out] pCtx  context structure
 */
static inline uint32_t ClockFSM_1(snow3gKeyState1_t *pCtx)
{
        const uint32_t F = (pCtx->LFSR_S[15] + pCtx->FSM_R1) ^ pCtx->FSM_R2;
        const uint32_t R = (pCtx->FSM_R3 ^ pCtx->LFSR_S[5]) + pCtx->FSM_R2;

        pCtx->FSM_R3 = S2_box(pCtx->FSM_R2);
        pCtx->FSM_R2 = S1_box(pCtx->FSM_R1);
        pCtx->FSM_R1 = R;

        return F;
}

/**
 * @brief ClockLFSR function as defined in SNOW3G standard
 * @param[in/out] pCtx  context structure
 */
static inline void ClockLFSR_1(snow3gKeyState1_t *pCtx)
{
        const uint32_t S0 = pCtx->LFSR_S[0];
        const uint32_t S11 = pCtx->LFSR_S[11];
        const uint32_t V = pCtx->LFSR_S[2] ^
                MULa(S0) ^
                DIVa(S11) ^
                (S0 << 8) ^
                (S11 >> 8);
        unsigned i;

        /* LFSR array shift by 1 position */
        for (i = 0; i < 15; i++)
                pCtx->LFSR_S[i] = pCtx->LFSR_S[i + 1];

        pCtx->LFSR_S[15] = V;
}

/**
 * @brief Initializes the key schedule for 1 buffer for SNOW3G f8/f9.
 *
 * @param[in/out]  pCtx        Context where the scheduled keys are stored
 * @param[in]      pKeySched   Key schedule
 * @param[in]      pIV         IV
 */
static inline void
snow3gStateInitialize_1(snow3gKeyState1_t *pCtx,
                        const snow3g_key_schedule_t *pKeySched,
                        const void *pIV)
{
        uint32_t FSM1, FSM2, FSM3;
        const uint32_t *pIV32 = pIV;
        int i;

        /* LFSR initialisation */
        for (i = 0; i < 4; i++) {
                const uint32_t K = pKeySched->k[i];
                const uint32_t L = ~K;

                pCtx->LFSR_S[i + 4] = K;
                pCtx->LFSR_S[i + 12] = K;
                pCtx->LFSR_S[i + 0] = L;
                pCtx->LFSR_S[i + 8] = L;
        }

        pCtx->LFSR_S[15] ^= BSWAP32(pIV32[3]);
        pCtx->LFSR_S[12] ^= BSWAP32(pIV32[2]);
        pCtx->LFSR_S[10] ^= BSWAP32(pIV32[1]);
        pCtx->LFSR_S[9] ^= BSWAP32(pIV32[0]);

        /* FSM initialization */
        FSM2 = 0;
        FSM3 = 0;
        FSM1 = 0;

        for (i = 0; i < 16; i++) {
                const uint32_t L0 = pCtx->LFSR_S[0];
                const uint32_t L1 = pCtx->LFSR_S[1];
                const uint32_t L11 = pCtx->LFSR_S[11];
                const uint32_t L12 = pCtx->LFSR_S[12];
                uint32_t MULa_L0 = L0;
                uint32_t MULa_L1 = L1;
                uint32_t DIVa_L11 = L11;
                uint32_t DIVa_L12 = L12;

                MULa_2(&MULa_L0, &MULa_L1);
                DIVa_2(&DIVa_L11, &DIVa_L12);

                /* clock FSM + clock LFSR + clockFSM + clock LFSR */
                const uint32_t F0 =
                        (pCtx->LFSR_S[15] + FSM1) ^ FSM2; /* (s15 + R1) ^ R2 */

                const uint32_t V0 =
                        pCtx->LFSR_S[2] ^
                        MULa_L0 ^ /* MUL(s0,0 ) */
                        DIVa_L11 ^ /* DIV(s11,3 )*/
                        (L0 << 8) ^ /*  (s0,1 || s0,2 || s0,3 || 0x00) */
                        (L11 >> 8) ^ /* (0x00 || s11,0 || s11,1 || s11,2 ) */
                        F0;

                const uint32_t R0 =
                        (FSM3 ^ pCtx->LFSR_S[5]) + FSM2; /* R2 + (R3 ^ s5 ) */

                uint32_t s1_box_step1 = FSM1;
                uint32_t s1_box_step2 = R0;

                S1_box_2(&s1_box_step1, &s1_box_step2);

                uint32_t s2_box_step1 = FSM2;
                uint32_t s2_box_step2 = s1_box_step1; /* S1_box(R0) */

                S2_box_2(&s2_box_step1, &s2_box_step2);

                FSM1 = (s2_box_step1 ^ pCtx->LFSR_S[6]) + s1_box_step1;

                const uint32_t F1 = (V0 + R0) ^ s1_box_step1;

                const uint32_t V1 = pCtx->LFSR_S[3] ^
                             MULa_L1 ^
                             DIVa_L12 ^
                             (L1 << 8) ^ (L12 >> 8) ^ F1;

                FSM2 = s1_box_step2;
                FSM3 = s2_box_step2;

                /* shift LFSR twice */
                ShiftTwiceLFSR_1(pCtx);

                pCtx->LFSR_S[14] = V0;
                pCtx->LFSR_S[15] = V1;
        }

        /* set FSM into scheduling structure */
        pCtx->FSM_R3 = FSM3;
        pCtx->FSM_R2 = FSM2;
        pCtx->FSM_R1 = FSM1;
}

/**
 * @brief Generates 5 words of key stream used in the initial stages of F9.
 *
 * @param[in]     pCtx        Context where the scheduled keys are stored
 * @param[in/out] pKeyStream  Pointer to the generated keystream
 */
static inline void snow3g_f9_keystream_words(snow3gKeyState1_t *pCtx,
                                             uint32_t *pKeyStream)
{
        int i;

        (void) ClockFSM_1(pCtx);
        ClockLFSR_1(pCtx);

        for (i = 0; i < 5; i++) {
                pKeyStream[i] = ClockFSM_1(pCtx) ^ pCtx->LFSR_S[0];
                ClockLFSR_1(pCtx);
        }
}

#ifdef AVX2
/**
 * @brief LFSR array shift by one (8 lanes)
 * @param[in]     pCtx       Context where the scheduled keys are stored
 */
static inline void ShiftLFSR_8(snow3gKeyState8_t *pCtx)
{
        pCtx->iLFSR_X = (pCtx->iLFSR_X + 1) & 15;
}
#endif /* AVX2 */

/**
 * @brief LFSR array shift by one (4 lanes)
 * @param[in]     pCtx       Context where the scheduled keys are stored
 */
static inline void ShiftLFSR_4(snow3gKeyState4_t *pCtx)
{
        pCtx->iLFSR_X = (pCtx->iLFSR_X + 1) & 15;
}

#ifdef AVX2
/**
 * @brief ClockLFSR sub-function as defined in SNOW3G standard (8 lanes)
 *
 * @param[in] L0        LFSR[0]
 * @param[in] L11       LFSR[11]
 * @return table_Alpha_div[LFSR[11] & 0xff] ^ table_Alpha_mul[LFSR[0] & 0xff]
 */
static inline __m256i C0_C11_8(const __m256i L0, const __m256i L11)
{
        const __m256i S1 = DIVa_8(L11);
        const __m256i S2 = MULa_8(L0);

        return _mm256_xor_si256(S1, S2);
}
#endif /* AVX2 */

/**
 * @brief ClockLFSR sub-function as defined in SNOW3G standard (4 lanes)
 *
 * @param[in] L0        LFSR[0]
 * @param[in] L11       LFSR[11]
 * @return table_Alpha_div[LFSR[11] & 0xff] ^ table_Alpha_mul[LFSR[0] & 0xff]
 */
static inline __m128i C0_C11_4(const __m128i L0, const __m128i L11)
{
        const __m128i SL11 = DIVa_4(L11);
        const __m128i SL0 = MULa_4(L0);

        return _mm_xor_si128(SL11, SL0);
}

#ifdef AVX2
/**
 * @brief ClockLFSR function as defined in SNOW3G standard (8 lanes)
 *
 * S =  table_Alpha_div[LFSR[11] & 0xff]
 *       ^ table_Alpha_mul[LFSR[0] >> 24]
 *       ^ LFSR[2] ^ LFSR[0] << 8 ^ LFSR[11] >> 8
 *
 * @param[in]     pCtx       Context where the scheduled keys are stored
 */
static inline void ClockLFSR_8(snow3gKeyState8_t *pCtx)
{
        __m256i X2;
        __m256i S, T, U;

        U = pCtx->LFSR_X[pCtx->iLFSR_X];
        S = pCtx->LFSR_X[(pCtx->iLFSR_X + 11) & 15];

        X2 = C0_C11_8(U, S);

        T = _mm256_slli_epi32(U, 8);
        S = _mm256_srli_epi32(S, 8);
        U = _mm256_xor_si256(T, pCtx->LFSR_X[(pCtx->iLFSR_X + 2) & 15]);

        ShiftLFSR_8(pCtx);

        S = _mm256_xor_si256(S, U);
        S = _mm256_xor_si256(S, X2);
        pCtx->LFSR_X[(pCtx->iLFSR_X + 15) & 15] = S;
}
#endif /* AVX2 */

/**
 * @brief ClockLFSR function as defined in SNOW3G standard (4 lanes)
 *
 * S =  table_Alpha_div[LFSR[11] & 0xff]
 *       ^ table_Alpha_mul[LFSR[0] >> 24]
 *       ^ LFSR[2] ^ LFSR[0] << 8 ^ LFSR[11] >> 8
 *
 * @param[in]     pCtx       Context where the scheduled keys are stored
 */
static inline void ClockLFSR_4(snow3gKeyState4_t *pCtx)
{
        __m128i S, T, U;

        U = pCtx->LFSR_X[pCtx->iLFSR_X];
        S = pCtx->LFSR_X[(pCtx->iLFSR_X + 11) & 15];
        const __m128i X2 = C0_C11_4(U, S);

        T = _mm_slli_epi32(U, 8);
        S = _mm_srli_epi32(S, 8);
        U = _mm_xor_si128(T, pCtx->LFSR_X[(pCtx->iLFSR_X + 2) & 15]);
        ShiftLFSR_4(pCtx);

        S = _mm_xor_si128(S, U);
        S = _mm_xor_si128(S, X2);
        pCtx->LFSR_X[(pCtx->iLFSR_X + 15) & 15] = S;
}

#ifdef AVX2
/**
 * @brief ClockFSM function as defined in SNOW3G standard
 *
 * It operates on 8 packets/lanes at a time
 *
 * @param[in]     pCtx       Context where the scheduled keys are stored
 * @return 8 x 4bytes of key stream
 */
static inline __m256i ClockFSM_8(snow3gKeyState8_t *pCtx)
{
        const uint32_t iLFSR_X_5 = (pCtx->iLFSR_X + 5) & 15;
        const uint32_t iLFSR_X_15 = (pCtx->iLFSR_X + 15) & 15;

        const __m256i F =
                _mm256_add_epi32(pCtx->LFSR_X[iLFSR_X_15], pCtx->FSM_X[0]);

        const __m256i ret = _mm256_xor_si256(F, pCtx->FSM_X[1]);

        const __m256i R =
                _mm256_add_epi32(_mm256_xor_si256(pCtx->LFSR_X[iLFSR_X_5],
                                                  pCtx->FSM_X[2]),
                                 pCtx->FSM_X[1]);

        pCtx->FSM_X[2] = S2_box_8(pCtx->FSM_X[1]);
        pCtx->FSM_X[1] = S1_box_8(pCtx->FSM_X[0]);
        pCtx->FSM_X[0] = R;

        return ret;
}
#endif /* AVX2 */

/**
 * @brief ClockFSM function as defined in SNOW3G standard
 *
 * It operates on 4 packets/lanes at a time
 *
 * @param[in]     pCtx       Context where the scheduled keys are stored
 * @return 4 x 4bytes of key stream
 */
static inline __m128i ClockFSM_4(snow3gKeyState4_t *pCtx)
{
        const uint32_t iLFSR_X = pCtx->iLFSR_X;
        const __m128i F =
                _mm_add_epi32(pCtx->LFSR_X[(iLFSR_X + 15) & 15],
                              pCtx->FSM_X[0]);
        const __m128i R =
                _mm_add_epi32(_mm_xor_si128(pCtx->LFSR_X[(iLFSR_X + 5) & 15],
                                            pCtx->FSM_X[2]),
                              pCtx->FSM_X[1]);

        const __m128i ret = _mm_xor_si128(F, pCtx->FSM_X[1]);

        pCtx->FSM_X[2] = S2_box_4(pCtx->FSM_X[1]);
        pCtx->FSM_X[1] = S1_box_4(pCtx->FSM_X[0]);
        pCtx->FSM_X[0] = R;

        return ret;
}

/**
 * @brief Generates 4 bytes of key stream 1 buffer at a time
 *
 * @param[in]     pCtx       Context where the scheduled keys are stored
 * @return 4 bytes of key stream
 */
static inline uint32_t snow3g_keystream_1_4(snow3gKeyState1_t *pCtx)
{
        const uint32_t F = ClockFSM_1(pCtx);
        const uint32_t ks = F ^ pCtx->LFSR_S[0];

        ClockLFSR_1(pCtx);
        return ks;
}

/**
 * @brief Generates 8 bytes of key stream for 1 buffer at a time
 *
 * @param[in] pCtx Context where the scheduled keys are stored
 * @return 8 bytes of a key stream
 */
static inline uint64_t snow3g_keystream_1_8(snow3gKeyState1_t *pCtx)
{
        /*
         * Merged clock FSM + clock LFSR + clock FSM + clockLFSR
         * in order to avoid redundancies in function processing
         * and less instruction immediate dependencies
         */
        const uint32_t L0 = pCtx->LFSR_S[0];
        const uint32_t L1 = pCtx->LFSR_S[1];
        const uint32_t L11 = pCtx->LFSR_S[11];
        const uint32_t L12 = pCtx->LFSR_S[12];
        uint32_t MULa_L0 = L0;
        uint32_t MULa_L1 = L1;
        uint32_t DIVa_L11 = L11;
        uint32_t DIVa_L12 = L12;

        MULa_2(&MULa_L0, &MULa_L1);
        DIVa_2(&DIVa_L11, &DIVa_L12);

        const uint32_t V0 =
                pCtx->LFSR_S[2] ^
                MULa_L0 ^
                DIVa_L11 ^
                (L0 << 8) ^
                (L11 >> 8);

        const uint32_t V1 =
                pCtx->LFSR_S[3] ^
                MULa_L1 ^
                DIVa_L12 ^
                (L1 << 8) ^
                (L12 >> 8);

        const uint32_t F0 =
                (pCtx->LFSR_S[15] + pCtx->FSM_R1) ^ L0 ^ pCtx->FSM_R2;
        const uint32_t R0 =
                (pCtx->FSM_R3 ^ pCtx->LFSR_S[5]) + pCtx->FSM_R2;

        uint32_t s1_box_step1 = pCtx->FSM_R1;
        uint32_t s1_box_step2 = R0;

        S1_box_2(&s1_box_step1, &s1_box_step2);

        uint32_t s2_box_step1 = pCtx->FSM_R2;
        uint32_t s2_box_step2 = s1_box_step1;

        S2_box_2(&s2_box_step1, &s2_box_step2);

        /*
         * At this stage FSM_R mapping is as follows:
         *    FSM_R2 = s1_box_step1
         *    FSM_R3 = s2_box_step1
         */
        const uint32_t F1 = (V0 + R0) ^ L1 ^ s1_box_step1;

        pCtx->FSM_R3 = s2_box_step2;
        pCtx->FSM_R2 = s1_box_step2;
        pCtx->FSM_R1 = (s2_box_step1 ^ pCtx->LFSR_S[6]) + s1_box_step1;

        /* Shift LFSR twice */
        ShiftTwiceLFSR_1(pCtx);

        /* key stream mode LFSR update */
        pCtx->LFSR_S[14] = V0;
        pCtx->LFSR_S[15] = V1;

        return (((uint64_t) F0) << 32) | ((uint64_t) F1);
}

#ifdef AVX2
/**
 * @brief Generates 8 bytes of key stream 8 buffers at a time
 *
 * @param[in]      pCtx         Context where the scheduled keys are stored
 * @param[in/out]  pKeyStreamLo Pointer to generated key stream
 * @param[in/out]  pKeyStreamHi Pointer to generated key stream
 */
static inline void snow3g_keystream_8_8(snow3gKeyState8_t *pCtx,
                                        __m256i *pKeyStreamLo,
                                        __m256i *pKeyStreamHi)
{
        /* first set of 4 bytes */
        const __m256i L = _mm256_xor_si256(ClockFSM_8(pCtx),
                                           pCtx->LFSR_X[pCtx->iLFSR_X]);
        ClockLFSR_8(pCtx);

        /* second set of 4 bytes */
        const __m256i H = _mm256_xor_si256(ClockFSM_8(pCtx),
                                           pCtx->LFSR_X[pCtx->iLFSR_X]);
        ClockLFSR_8(pCtx);

        /* merge the 2 sets */
        *pKeyStreamLo = _mm256_unpacklo_epi32(H, L);
        *pKeyStreamHi = _mm256_unpackhi_epi32(H, L);
}

/**
 * @brief Generates 4 bytes of key stream 8 buffers at a time
 *
 * @param[in]      pCtx         Context where the scheduled keys are stored
 * @return 8 x 4 bytes vaector with key stream data
 */
static inline __m256i snow3g_keystream_8_4(snow3gKeyState8_t *pCtx)
{
        const __m256i keyStream = _mm256_xor_si256(ClockFSM_8(pCtx),
                                                   pCtx->LFSR_X[pCtx->iLFSR_X]);

        ClockLFSR_8(pCtx);
        return keyStream;
}

/**
 * @brief Generates 32 bytes of key stream 8 buffers at a time
 *
 * @param[in]     pCtx         Context where the scheduled keys are stored
 * @param[in/out] pKeyStream   Array of generated key streams
 */
static inline void snow3g_keystream_8_32(snow3gKeyState8_t *pCtx,
                                         __m256i *pKeyStream)
{

        __m256i temp[8];

        /** produces the next 4 bytes for each buffer */
        int i;

        /** Byte reversal on each KS */
        static const __m256i mask1 = {
                0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL,
                0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL
        };
        /** Reversal, shifted 4 bytes right */
        static const __m256i mask2 = {
                0x0405060708090a0bULL, 0x0c0d0e0f00010203ULL,
                0x0405060708090a0bULL, 0x0c0d0e0f00010203ULL
        };
        /** Reversal, shifted 8 bytes right */
        static const __m256i mask3 = {
                0x08090a0b0c0d0e0fULL, 0x0001020304050607ULL,
                0x08090a0b0c0d0e0fULL, 0x0001020304050607ULL
        };
        /** Reversal, shifted 12 bytes right */
        static const __m256i mask4 = {
                0x0c0d0e0f00010203ULL, 0x0405060708090a0bULL,
                0x0c0d0e0f00010203ULL, 0x0405060708090a0bULL
        };

        temp[0] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask1);
        temp[1] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask2);
        temp[2] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask3);
        temp[3] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask4);
        temp[4] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask1);
        temp[5] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask2);
        temp[6] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask3);
        temp[7] = _mm256_shuffle_epi8(snow3g_keystream_8_4(pCtx), mask4);

        __m256i blended[8];
        /* blends KS together: 128bit slice consists
           of 4 32-bit words for one packet */
        blended[0] = _mm256_blend_epi32(temp[0], temp[1], 0xaa);
        blended[1] = _mm256_blend_epi32(temp[0], temp[1], 0x55);
        blended[2] = _mm256_blend_epi32(temp[2], temp[3], 0xaa);
        blended[3] = _mm256_blend_epi32(temp[2], temp[3], 0x55);
        blended[4] = _mm256_blend_epi32(temp[4], temp[5], 0xaa);
        blended[5] = _mm256_blend_epi32(temp[4], temp[5], 0x55);
        blended[6] = _mm256_blend_epi32(temp[6], temp[7], 0xaa);
        blended[7] = _mm256_blend_epi32(temp[6], temp[7], 0x55);

        temp[0] = _mm256_blend_epi32(blended[0], blended[2], 0xcc);
        temp[1] = _mm256_blend_epi32(blended[1], blended[3], 0x99);
        temp[2] = _mm256_blend_epi32(blended[0], blended[2], 0x33);
        temp[3] = _mm256_blend_epi32(blended[1], blended[3], 0x66);
        temp[4] = _mm256_blend_epi32(blended[4], blended[6], 0xcc);
        temp[5] = _mm256_blend_epi32(blended[5], blended[7], 0x99);
        temp[6] = _mm256_blend_epi32(blended[4], blended[6], 0x33);
        temp[7] = _mm256_blend_epi32(blended[5], blended[7], 0x66);

        /** sorts 32 bit words back into order */
        blended[0] = temp[0];
        blended[1] = _mm256_shuffle_epi32(temp[1], 0x39);
        blended[2] = _mm256_shuffle_epi32(temp[2], 0x4e);
        blended[3] = _mm256_shuffle_epi32(temp[3], 0x93);
        blended[4] = temp[4];
        blended[5] = _mm256_shuffle_epi32(temp[5], 0x39);
        blended[6] = _mm256_shuffle_epi32(temp[6], 0x4e);
        blended[7] = _mm256_shuffle_epi32(temp[7], 0x93);

        for (i = 0; i < 4; i++) {
                pKeyStream[i] =
                        _mm256_permute2x128_si256(blended[i],
                                                  blended[i + 4], 0x20);
                pKeyStream[i + 4] =
                        _mm256_permute2x128_si256(blended[i],
                                                   blended[i + 4], 0x31);
        }
}
#endif /* AVX2 */

/**
 * @brief Generates 4 bytes of key stream 4 buffers at a time
 *
 * @param[in]      pCtx         Context where the scheduled keys are stored
 * @param[in/out]  pKeyStream   Pointer to generated key stream
 */
static inline __m128i snow3g_keystream_4_4(snow3gKeyState4_t *pCtx)
{
        const __m128i keyStream = _mm_xor_si128(ClockFSM_4(pCtx),
                                                pCtx->LFSR_X[pCtx->iLFSR_X]);

        ClockLFSR_4(pCtx);
        return keyStream;
}

/**
 * @brief Generates 8 bytes of key stream 4 buffers at a time
 *
 * @param[in]      pCtx         Context where the scheduled keys are stored
 * @param[in/out]  pKeyStreamLo Pointer to lower end of generated key stream
 * @param[in/out]  pKeyStreamHi Pointer to higher end of generated key stream
 */
static inline void snow3g_keystream_4_8(snow3gKeyState4_t *pCtx,
                                        __m128i *pKeyStreamLo,
                                        __m128i *pKeyStreamHi)
{
        const __m128i L0 = pCtx->LFSR_X[pCtx->iLFSR_X];
        const __m128i L2 = pCtx->LFSR_X[(pCtx->iLFSR_X + 2) & 15];
        const __m128i L11 = pCtx->LFSR_X[(pCtx->iLFSR_X + 11) & 15];

        const __m128i L1 = pCtx->LFSR_X[(pCtx->iLFSR_X + 1) & 15];
        const __m128i L3 = pCtx->LFSR_X[(pCtx->iLFSR_X + 3) & 15];
        const __m128i L12 = pCtx->LFSR_X[(pCtx->iLFSR_X + 12) & 15];

        const __m128i L5 = pCtx->LFSR_X[(pCtx->iLFSR_X + 5) & 15];
        const __m128i L6 = pCtx->LFSR_X[(pCtx->iLFSR_X + 6) & 15];
        const __m128i L15 = pCtx->LFSR_X[(pCtx->iLFSR_X + 15) & 15];

        const __m128i V0 = _mm_xor_si128(_mm_xor_si128(C0_C11_4(L0, L11), L2),
                                         _mm_xor_si128(_mm_slli_epi32(L0, 8),
                                                       _mm_srli_epi32(L11, 8)));

        const __m128i V1 = _mm_xor_si128(_mm_xor_si128(C0_C11_4(L1, L12), L3),
                                         _mm_xor_si128(_mm_slli_epi32(L1, 8),
                                                       _mm_srli_epi32(L12, 8)));

        /* ======== first set of 4 bytes */

        const __m128i s1_box_step1 = S1_box_4(pCtx->FSM_X[0]); /* do early */

        const __m128i R0 = _mm_add_epi32(_mm_xor_si128(L5, pCtx->FSM_X[2]),
                                         pCtx->FSM_X[1]);

        const __m128i F0 = _mm_xor_si128(_mm_add_epi32(L15, pCtx->FSM_X[0]),
                                         pCtx->FSM_X[1]);
        const __m128i L = _mm_xor_si128(F0, L0);

        const __m128i F1 = _mm_xor_si128(_mm_add_epi32(V0, R0), s1_box_step1);
        const __m128i H = _mm_xor_si128(F1, L1);

        /* Merge L & H sets for output */
        *pKeyStreamLo = _mm_unpacklo_epi32(H, L);
        *pKeyStreamHi = _mm_unpackhi_epi32(H, L);

        __m128i s2_box_step1 = pCtx->FSM_X[1];
        __m128i s2_box_step2 = s1_box_step1;

        S2_box_2x4(&s2_box_step1, &s2_box_step2);

        /*
         * At this stage FSM_X mapping is as follows:
         *    FSM_X[2] = s2_box_step1
         *    FSM_X[1] = s1_box_step1
         *    FSM_X[0] = R0
         */

        /* Shift LFSR twice */
        pCtx->iLFSR_X = (pCtx->iLFSR_X + 2) & 15;

        /* LFSR Update */
        pCtx->LFSR_X[(pCtx->iLFSR_X + 14) & 15] = V0;
        pCtx->LFSR_X[(pCtx->iLFSR_X + 15) & 15] = V1;

        const __m128i s1_box_step2 = S1_box_4(R0);

        const __m128i R1 = _mm_add_epi32(_mm_xor_si128(L6, s2_box_step1),
                                         s1_box_step1);

        /* Final FSM_X update
         *    FSM_X[2] = s2_box_step2
         *    FSM_X[1] = s1_box_step2
         *    FSM_X[0] = R1
         */
        pCtx->FSM_X[2] = s2_box_step2;
        pCtx->FSM_X[1] = s1_box_step2;
        pCtx->FSM_X[0] = R1;
}

/**
 * @brief Generates 16 bytes of key stream 4 buffers at a time
 *
 * @param[in]     pCtx         Context where the scheduled keys are stored
 * @param[in/out] pKeyStream   Pointer to store generated key stream
 */
static inline void snow3g_keystream_4_16(snow3gKeyState4_t *pCtx,
                                         __m128i pKeyStream[4])
{
        static const uint64_t sm[2] = {
                /* mask for byte swapping 64-bit words */
                0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL
        };
        __m128i ksL1, ksL2, ksH1, ksH2;

        snow3g_keystream_4_8(pCtx, &ksL1, &ksH1);
        snow3g_keystream_4_8(pCtx, &ksL2, &ksH2);

        const __m128i swapMask = _mm_loadu_si128((const __m128i *)sm);

        pKeyStream[0] = _mm_shuffle_epi8(_mm_unpacklo_epi64(ksL1, ksL2),
                                         swapMask);
        pKeyStream[1] = _mm_shuffle_epi8(_mm_unpackhi_epi64(ksL1, ksL2),
                                         swapMask);
        pKeyStream[2] = _mm_shuffle_epi8(_mm_unpacklo_epi64(ksH1, ksH2),
                                         swapMask);
        pKeyStream[3] = _mm_shuffle_epi8(_mm_unpackhi_epi64(ksH1, ksH2),
                                         swapMask);
}

/**
 * @brief Initializes the key schedule for 4 buffers for SNOW3G f8/f9.
 *
 * @param [in]      pCtx        Context where the scheduled keys are stored
 * @param [in]      pKeySched   Key schedule
 * @param [in]      pIV1        IV for buffer 1
 * @param [in]      pIV2        IV for buffer 2
 * @param [in]      pIV3        IV for buffer 3
 * @param [in]      pIV4        IV for buffer 4
 */
static inline void
snow3gStateInitialize_4(snow3gKeyState4_t *pCtx,
                        const snow3g_key_schedule_t *pKeySched,
                        const void *pIV1, const void *pIV2,
                        const void *pIV3, const void *pIV4)
{
        __m128i R, S, T, U;
        __m128i T0, T1;
        int i;

        /* Initialize the LFSR table from constants, Keys, and IV */

        /* Load complete 128b IV into register (SSE2)*/
        static const uint64_t sm[2] = {
                0x0405060700010203ULL, 0x0c0d0e0f08090a0bULL
        };

        R = _mm_loadu_si128((const __m128i *)pIV1);
        S = _mm_loadu_si128((const __m128i *)pIV2);
        T = _mm_loadu_si128((const __m128i *)pIV3);
        U = _mm_loadu_si128((const __m128i *)pIV4);

        /* initialize the array block (SSE4) */
        for (i = 0; i < 4; i++) {
                const uint32_t K = pKeySched->k[i];
                const uint32_t L = ~K;
                const __m128i VK = _mm_set1_epi32(K);
                const __m128i VL = _mm_set1_epi32(L);

                pCtx->LFSR_X[i + 4] =
                        pCtx->LFSR_X[i + 12] = VK;
                pCtx->LFSR_X[i + 0] =
                        pCtx->LFSR_X[i + 8] = VL;
        }
        /* Update the schedule structure with IVs */
        /* Store the 4 IVs in LFSR by a column/row matrix swap
         * after endianness correction */

        /* endianness swap (SSSE3) */
        const __m128i swapMask = _mm_loadu_si128((const __m128i *) sm);

        R = _mm_shuffle_epi8(R, swapMask);
        S = _mm_shuffle_epi8(S, swapMask);
        T = _mm_shuffle_epi8(T, swapMask);
        U = _mm_shuffle_epi8(U, swapMask);

        /* row/column dword inversion (SSE2) */
        T0 = _mm_unpacklo_epi32(R, S);
        R = _mm_unpackhi_epi32(R, S);
        T1 = _mm_unpacklo_epi32(T, U);
        T = _mm_unpackhi_epi32(T, U);

        /* row/column qword inversion (SSE2) */
        U = _mm_unpackhi_epi64(R, T);
        T = _mm_unpacklo_epi64(R, T);
        S = _mm_unpackhi_epi64(T0, T1);
        R = _mm_unpacklo_epi64(T0, T1);

        /* IV ^ LFSR (SSE2) */
        pCtx->LFSR_X[15] = _mm_xor_si128(pCtx->LFSR_X[15], U);
        pCtx->LFSR_X[12] = _mm_xor_si128(pCtx->LFSR_X[12], T);
        pCtx->LFSR_X[10] = _mm_xor_si128(pCtx->LFSR_X[10], S);
        pCtx->LFSR_X[9] = _mm_xor_si128(pCtx->LFSR_X[9], R);
        pCtx->iLFSR_X = 0;

        /* FSM initialization (SSE2) */
        pCtx->FSM_X[0] = pCtx->FSM_X[1] =
                pCtx->FSM_X[2] = _mm_setzero_si128();

        /* Initialisation rounds */
        for (i = 0; i < 32; i++) {
                T1 = ClockFSM_4(pCtx);
                ClockLFSR_4(pCtx);
                pCtx->LFSR_X[(pCtx->iLFSR_X + 15) & 15] =
                        _mm_xor_si128(pCtx->LFSR_X[(pCtx->iLFSR_X + 15) & 15],
                                      T1);
        }
}

#ifdef AVX2
/**
 * @brief Initializes the key schedule for 8 buffers with individual keys
 *
 * It can be used for SNOW3G F8/F9
 *
 * @param[in/out] pCtx      pointer to an array with 8 key stream states
 * @param[in]     pKeySched pointer to an array with 8 key schedules
 * @param[in]     pIV       pointer to an array with 8 IV's
 */
static inline void
snow3gStateInitialize_8_multiKey(snow3gKeyState8_t *pCtx,
                                 const snow3g_key_schedule_t * const KeySched[],
                                 const void * const pIV[])
{
        DECLARE_ALIGNED(uint32_t k[8], 32);
        DECLARE_ALIGNED(uint32_t l[8], 32);
        __m256i *K = (__m256i *)k;
        __m256i *L = (__m256i *)l;

        int i, j;
        __m256i mR, mS, mT, mU, T0, T1;

        /* Initialize the LFSR table from constants, Keys, and IV */

        /* Load complete 256b IV into register (SSE2)*/
        static const __m256i swapMask = {
                0x0405060700010203ULL, 0x0c0d0e0f08090a0bULL,
                0x0405060700010203ULL, 0x0c0d0e0f08090a0bULL
        };
        mR = load_2xm128i_into_m256i(pIV[4], pIV[0]);
        mS = load_2xm128i_into_m256i(pIV[5], pIV[1]);
        mT = load_2xm128i_into_m256i(pIV[6], pIV[2]);
        mU = load_2xm128i_into_m256i(pIV[7], pIV[3]);

        /* initialize the array block (SSE4) */
        for (i = 0; i < 4; i++) {
                for (j = 0; j < 8; j++) {
                        k[j] = KeySched[j]->k[i];
                        l[j] = ~k[j];
                }

                pCtx->LFSR_X[i + 4] = *K;
                pCtx->LFSR_X[i + 12] = *K;
                pCtx->LFSR_X[i + 0] = *L;
                pCtx->LFSR_X[i + 8] = *L;
        }

        /* Update the schedule structure with IVs */
        /* Store the 4 IVs in LFSR by a column/row matrix swap
         * after endianness correction */

        /* endianness swap */
        mR = _mm256_shuffle_epi8(mR, swapMask);
        mS = _mm256_shuffle_epi8(mS, swapMask);
        mT = _mm256_shuffle_epi8(mT, swapMask);
        mU = _mm256_shuffle_epi8(mU, swapMask);

        /* row/column dword inversion */
        T0 = _mm256_unpacklo_epi32(mR, mS);
        mR = _mm256_unpackhi_epi32(mR, mS);
        T1 = _mm256_unpacklo_epi32(mT, mU);
        mT = _mm256_unpackhi_epi32(mT, mU);

        /* row/column qword inversion  */
        mU = _mm256_unpackhi_epi64(mR, mT);
        mT = _mm256_unpacklo_epi64(mR, mT);
        mS = _mm256_unpackhi_epi64(T0, T1);
        mR = _mm256_unpacklo_epi64(T0, T1);

        /*IV ^ LFSR  */
        pCtx->LFSR_X[15] = _mm256_xor_si256(pCtx->LFSR_X[15], mU);
        pCtx->LFSR_X[12] = _mm256_xor_si256(pCtx->LFSR_X[12], mT);
        pCtx->LFSR_X[10] = _mm256_xor_si256(pCtx->LFSR_X[10], mS);
        pCtx->LFSR_X[9] = _mm256_xor_si256(pCtx->LFSR_X[9], mR);
        pCtx->iLFSR_X = 0;

        /* FSM initialization  */
        pCtx->FSM_X[0] =
                pCtx->FSM_X[1] =
                pCtx->FSM_X[2] = _mm256_setzero_si256();

        /* Initialisation rounds */
        for (i = 0; i < 32; i++) {
                mS = ClockFSM_8(pCtx);
                ClockLFSR_8(pCtx);

                const uint32_t idx = (pCtx->iLFSR_X + 15) & 15;

                pCtx->LFSR_X[idx] = _mm256_xor_si256(pCtx->LFSR_X[idx], mS);
        }
}

/**
 * @brief Initializes the key schedule for 8 buffers for SNOW3G f8/f9.
 *
 * @param [in]     pCtx         Context where the scheduled keys are stored
 * @param [in]     pKeySched    Key schedule
 * @param [in]     pIV1         IV for buffer 1
 * @param [in]     pIV2         IV for buffer 2
 * @param [in]     pIV3         IV for buffer 3
 * @param [in]     pIV4         IV for buffer 4
 * @param [in]     pIV5         IV for buffer 5
 * @param [in]     pIV6         IV for buffer 6
 * @param [in]     pIV7         IV for buffer 7
 * @param [in]     pIV8         IV for buffer 8
 */
static inline void
snow3gStateInitialize_8(snow3gKeyState8_t *pCtx,
                        const snow3g_key_schedule_t *pKeySched,
                        const void *pIV1, const void *pIV2,
                        const void *pIV3, const void *pIV4,
                        const void *pIV5, const void *pIV6,
                        const void *pIV7, const void *pIV8)
{
        __m256i mR, mS, mT, mU, T0, T1;
        int i;

        /* Initialize the LFSR table from constants, Keys, and IV */

        /* Load complete 256b IV into register (SSE2)*/
        static const __m256i swapMask = {
                0x0405060700010203ULL, 0x0c0d0e0f08090a0bULL,
                0x0405060700010203ULL, 0x0c0d0e0f08090a0bULL
        };

        mR = load_2xm128i_into_m256i(pIV5, pIV1);
        mS = load_2xm128i_into_m256i(pIV6, pIV2);
        mT = load_2xm128i_into_m256i(pIV7, pIV3);
        mU = load_2xm128i_into_m256i(pIV8, pIV4);

        /* initialize the array block (SSE4) */
        for (i = 0; i < 4; i++) {
                const uint32_t K = pKeySched->k[i];
                const uint32_t L = ~K;
                const __m256i V0 = _mm256_set1_epi32(K);
                const __m256i V1 = _mm256_set1_epi32(L);

                pCtx->LFSR_X[i + 4] =
                        pCtx->LFSR_X[i + 12] = V0;
                pCtx->LFSR_X[i + 0] =
                        pCtx->LFSR_X[i + 8] = V1;
        }

        /* Update the schedule structure with IVs */
        /* Store the 4 IVs in LFSR by a column/row matrix swap
         * after endianness correction */

        /* endianness swap (SSSE3) */
        mR = _mm256_shuffle_epi8(mR, swapMask);
        mS = _mm256_shuffle_epi8(mS, swapMask);
        mT = _mm256_shuffle_epi8(mT, swapMask);
        mU = _mm256_shuffle_epi8(mU, swapMask);

        /* row/column dword inversion (SSE2) */
        T0 = _mm256_unpacklo_epi32(mR, mS);
        mR = _mm256_unpackhi_epi32(mR, mS);
        T1 = _mm256_unpacklo_epi32(mT, mU);
        mT = _mm256_unpackhi_epi32(mT, mU);

        /* row/column qword inversion (SSE2) */
        mU = _mm256_unpackhi_epi64(mR, mT);
        mT = _mm256_unpacklo_epi64(mR, mT);
        mS = _mm256_unpackhi_epi64(T0, T1);
        mR = _mm256_unpacklo_epi64(T0, T1);

        /*IV ^ LFSR (SSE2) */
        pCtx->LFSR_X[15] = _mm256_xor_si256(pCtx->LFSR_X[15], mU);
        pCtx->LFSR_X[12] = _mm256_xor_si256(pCtx->LFSR_X[12], mT);
        pCtx->LFSR_X[10] = _mm256_xor_si256(pCtx->LFSR_X[10], mS);
        pCtx->LFSR_X[9] = _mm256_xor_si256(pCtx->LFSR_X[9], mR);
        pCtx->iLFSR_X = 0;

        /* FSM initialization (SSE2) */
        pCtx->FSM_X[0] =
                pCtx->FSM_X[1] =
                pCtx->FSM_X[2] = _mm256_setzero_si256();

        /* Initialisation rounds */
        for (i = 0; i < 32; i++) {
                mS = ClockFSM_8(pCtx);
                ClockLFSR_8(pCtx);

                const uint32_t idx = (pCtx->iLFSR_X + 15) & 15;

                pCtx->LFSR_X[idx] = _mm256_xor_si256(pCtx->LFSR_X[idx], mS);
        }
}
#endif /* AVX2 */

static inline void
preserve_bits(uint64_t *KS,
              const uint8_t *pcBufferOut, const uint8_t *pcBufferIn,
              SafeBuf *safeOutBuf, SafeBuf *safeInBuf,
              const uint8_t bit_len, const uint8_t byte_len)
{
        const uint64_t mask = UINT64_MAX << (SNOW3G_BLOCK_SIZE * 8 - bit_len);

        /* Clear the last bits of the key stream and the input
         * (input only in out-of-place case) */
        *KS &= mask;
        if (pcBufferIn != pcBufferOut) {
                const uint64_t swapMask = BSWAP64(mask);

                safeInBuf->b64 &= swapMask;

                /*
                 * Merge the last bits from the output, to be preserved,
                 * in the key stream, to be XOR'd with the input
                 * (which last bits are 0, maintaining the output bits)
                 */
                memcpy_keystrm(safeOutBuf->b8, pcBufferOut, byte_len);
                *KS |= BSWAP64(safeOutBuf->b64 & ~swapMask);
        }
}

/**
 * @brief Core SNOW3G F8 bit algorithm for the 3GPP confidentiality algorithm
 *
 * @param[in]    pCtx          Context where the scheduled keys are stored
 * @param[in]    pIn           Input buffer
 * @param[out]   pOut          Output buffer
 * @param[in]    lengthInBits  length in bits of the data to be encrypted
 * @param[in]    offsetinBits  offset in input buffer, where data are valid
 */
static inline void f8_snow3g_bit(snow3gKeyState1_t *pCtx,
                                 const void *pIn,
                                 void *pOut,
                                 const uint32_t lengthInBits,
                                 const uint32_t offsetInBits)
{
        const uint8_t *pBufferIn = pIn;
        uint8_t *pBufferOut = pOut;
        uint32_t cipherLengthInBits = lengthInBits;
        uint64_t shiftrem = 0;
        uint64_t KS8, KS8bit; /* 8 bytes of key stream */
        const uint8_t *pcBufferIn = pBufferIn + (offsetInBits / 8);
        uint8_t *pcBufferOut = pBufferOut + (offsetInBits / 8);
        /* Offset into the first byte (0 - 7 bits) */
        uint32_t remainOffset = offsetInBits % 8;
        SafeBuf safeInBuf = {0};
        SafeBuf safeOutBuf = {0};

        /* Now run the block cipher */

        /* Start with potential partial block (due to offset and length) */
        KS8 = snow3g_keystream_1_8(pCtx);
        KS8bit = KS8 >> remainOffset;
        /* Only one block to encrypt */
        if (cipherLengthInBits < (64 - remainOffset)) {
                const uint32_t byteLength = (cipherLengthInBits + 7) / 8;

                memcpy_keystrm(safeInBuf.b8, pcBufferIn, byteLength);
                /*
                 * If operation is Out-of-place and there is offset
                 * to be applied, "remainOffset" bits from the output buffer
                 * need to be preserved (only applicable to first byte,
                 * since remainOffset is up to 7 bits)
                 */
                if ((pIn != pOut) && remainOffset) {
                        const uint8_t mask8 = (uint8_t)
                                (1 << (8 - remainOffset)) - 1;

                        safeInBuf.b8[0] = (safeInBuf.b8[0] & mask8) |
                                (pcBufferOut[0] & ~mask8);
                }
                /* If last byte is a partial byte, the last bits of the output
                 * need to be preserved */
                const uint8_t bitlen_with_off = remainOffset +
                        cipherLengthInBits;

                if ((bitlen_with_off & 0x7) != 0)
                        preserve_bits(&KS8bit, pcBufferOut, pcBufferIn,
                                      &safeOutBuf, &safeInBuf,
                                      bitlen_with_off, byteLength);

                xor_keystrm_rev(safeOutBuf.b8, safeInBuf.b8, KS8bit);
                memcpy_keystrm(pcBufferOut, safeOutBuf.b8, byteLength);
                return;
        }
        /*
         * If operation is Out-of-place and there is offset
         * to be applied, "remainOffset" bits from the output buffer
         * need to be preserved (only applicable to first byte,
         * since remainOffset is up to 7 bits)
         */
        if ((pIn != pOut) && remainOffset) {
                const uint8_t mask8 = (uint8_t)(1 << (8 - remainOffset)) - 1;

                memcpy_keystrm(safeInBuf.b8, pcBufferIn, 8);
                safeInBuf.b8[0] = (safeInBuf.b8[0] & mask8) |
                        (pcBufferOut[0] & ~mask8);
                xor_keystrm_rev(pcBufferOut, safeInBuf.b8, KS8bit);
                pcBufferIn += SNOW3G_BLOCK_SIZE;
        } else {
                /* At least 64 bits to produce (including offset) */
                pcBufferIn = xor_keystrm_rev(pcBufferOut, pcBufferIn, KS8bit);
        }

        if (remainOffset != 0)
                shiftrem = KS8 << (64 - remainOffset);
        cipherLengthInBits -= SNOW3G_BLOCK_SIZE * 8 - remainOffset;
        pcBufferOut += SNOW3G_BLOCK_SIZE;

        while (cipherLengthInBits) {
                /* produce the next block of key stream */
                KS8 = snow3g_keystream_1_8(pCtx);
                KS8bit = (KS8 >> remainOffset) | shiftrem;
                if (remainOffset != 0)
                        shiftrem = KS8 << (64 - remainOffset);
                if (cipherLengthInBits >= SNOW3G_BLOCK_SIZE * 8) {
                        pcBufferIn = xor_keystrm_rev(pcBufferOut,
                                                     pcBufferIn, KS8bit);
                        cipherLengthInBits -= SNOW3G_BLOCK_SIZE * 8;
                        pcBufferOut += SNOW3G_BLOCK_SIZE;
                        /* loop variant */
                } else {
                        /* end of the loop, handle the last bytes */
                        const uint32_t byteLength =
                                (cipherLengthInBits + 7) / 8;

                        memcpy_keystrm(safeInBuf.b8, pcBufferIn,
                                       byteLength);

                        /* If last byte is a partial byte, the last bits
                         * of the output need to be preserved */
                        if ((cipherLengthInBits & 0x7) != 0)
                                preserve_bits(&KS8bit, pcBufferOut, pcBufferIn,
                                              &safeOutBuf, &safeInBuf,
                                              cipherLengthInBits, byteLength);

                        xor_keystrm_rev(safeOutBuf.b8, safeInBuf.b8, KS8bit);
                        memcpy_keystrm(pcBufferOut, safeOutBuf.b8, byteLength);
                        cipherLengthInBits = 0;
                }
        }
#ifdef SAFE_DATA
        CLEAR_VAR(&KS8, sizeof(KS8));
        CLEAR_VAR(&KS8bit, sizeof(KS8bit));
        CLEAR_MEM(&safeInBuf, sizeof(safeInBuf));
        CLEAR_MEM(&safeOutBuf, sizeof(safeOutBuf));
#endif
}

/**
 * @brief Core SNOW3G F8 algorithm for the 3GPP confidentiality algorithm
 *
 * @param[in]  pCtx           Context where the scheduled keys are stored
 * @param[in]  pIn            Input buffer
 * @param[out] pOut           Output buffer
 * @param[in]  lengthInBytes  length in bytes of the data to be encrypted
 */
static inline void f8_snow3g(snow3gKeyState1_t *pCtx,
                             const void *pIn,
                             void *pOut,
                             const uint32_t lengthInBytes)
{
        uint32_t qwords = lengthInBytes / SNOW3G_8_BYTES; /* number of qwords */
        const uint32_t words = lengthInBytes & 4; /* remaining word if not 0 */
        const uint32_t bytes = lengthInBytes & 3; /* remaining bytes */
        uint32_t KS4;                       /* 4 bytes of key stream */
        uint64_t KS8;                       /* 8 bytes of key stream */
        const uint8_t *pBufferIn = pIn;
        uint8_t *pBufferOut = pOut;

        /* process 64 bits at a time */
        while (qwords--) {
                /* generate key stream 8 bytes at a time */
                KS8 = snow3g_keystream_1_8(pCtx);

                /* xor key stream 8 bytes at a time */
                pBufferIn = xor_keystrm_rev(pBufferOut, pBufferIn, KS8);
                pBufferOut += SNOW3G_8_BYTES;
        }

        /* check for remaining 0 to 7 bytes */
        if (0 != words) {
                if (bytes) {
                        /* 5 to 7 last bytes, process 8 bytes */
                        uint8_t buftemp[8];
                        uint8_t safeBuff[8];

                        memset(safeBuff, 0, SNOW3G_8_BYTES);
                        KS8 = snow3g_keystream_1_8(pCtx);
                        memcpy_keystrm(safeBuff, pBufferIn, 4 + bytes);
                        xor_keystrm_rev(buftemp, safeBuff, KS8);
                        memcpy_keystrm(pBufferOut, buftemp, 4 + bytes);
#ifdef SAFE_DATA
                        CLEAR_MEM(&safeBuff, sizeof(safeBuff));
                        CLEAR_MEM(&buftemp, sizeof(buftemp));
#endif
                } else {
                        /* exactly 4 last bytes */
                        KS4 = snow3g_keystream_1_4(pCtx);
                        xor_keystream_reverse_32(pBufferOut, pBufferIn, KS4);
                }
        } else if (0 != bytes) {
                /* 1 to 3 last bytes */
                uint8_t buftemp[4];
                uint8_t safeBuff[4];

                memset(safeBuff, 0, SNOW3G_4_BYTES);
                KS4 = snow3g_keystream_1_4(pCtx);
                memcpy_keystream_32(safeBuff, pBufferIn, bytes);
                xor_keystream_reverse_32(buftemp, safeBuff, KS4);
                memcpy_keystream_32(pBufferOut, buftemp, bytes);
#ifdef SAFE_DATA
                CLEAR_MEM(&safeBuff, sizeof(safeBuff));
                CLEAR_MEM(&buftemp, sizeof(buftemp));
#endif
        }

#ifdef SAFE_DATA
        CLEAR_VAR(&KS4, sizeof(KS4));
        CLEAR_VAR(&KS8, sizeof(KS8));
#endif
}

#ifdef AVX2
/**
 * @brief Extracts one state from a 8 buffer state structure.
 *
 * @param[in]  pSrcState   Pointer to the source state
 * @param[in]  pDstState   Pointer to the destination state
 * @param[in]  NumBuffer   Buffer number
 */
static inline void snow3gStateConvert_8(const snow3gKeyState8_t *pSrcState,
                                        snow3gKeyState1_t *pDstState,
                                        const uint32_t NumBuffer)
{
        const uint32_t iLFSR_X = pSrcState->iLFSR_X;
        const __m256i *LFSR_X = pSrcState->LFSR_X;
        uint32_t i;

        for (i = 0; i < 16; i++) {
                const uint32_t *pLFSR_X =
                        (const uint32_t *) &LFSR_X[(i + iLFSR_X) & 15];

                pDstState->LFSR_S[i] = pLFSR_X[NumBuffer];
        }

        const uint32_t *pFSM_X0 = (const uint32_t *)&pSrcState->FSM_X[0];
        const uint32_t *pFSM_X1 = (const uint32_t *)&pSrcState->FSM_X[1];
        const uint32_t *pFSM_X2 = (const uint32_t *)&pSrcState->FSM_X[2];

        pDstState->FSM_R1 = pFSM_X0[NumBuffer];
        pDstState->FSM_R2 = pFSM_X1[NumBuffer];
        pDstState->FSM_R3 = pFSM_X2[NumBuffer];
}
#endif /* AVX2 */

/**
 * @brief Extracts one state from a 4 buffer state structure.
 *
 * @param[in]  pSrcState   Pointer to the source state
 * @param[in]  pDstState   Pointer to the destination state
 * @param[in]  NumBuffer   Buffer number
 */
static inline void snow3gStateConvert_4(const snow3gKeyState4_t *pSrcState,
                                        snow3gKeyState1_t *pDstState,
                                        const uint32_t NumBuffer)
{
        const uint32_t iLFSR_X = pSrcState->iLFSR_X;
        const __m128i *LFSR_X = pSrcState->LFSR_X;
        uint32_t i;

        for (i = 0; i < 16; i++) {
                const uint32_t *pLFSR_X =
                        (const uint32_t *) &LFSR_X[(i + iLFSR_X) & 15];

                pDstState->LFSR_S[i] = pLFSR_X[NumBuffer];
        }

        const uint32_t *pFSM_X0 = (const uint32_t *)&pSrcState->FSM_X[0];
        const uint32_t *pFSM_X1 = (const uint32_t *)&pSrcState->FSM_X[1];
        const uint32_t *pFSM_X2 = (const uint32_t *)&pSrcState->FSM_X[2];

        pDstState->FSM_R1 = pFSM_X0[NumBuffer];
        pDstState->FSM_R2 = pFSM_X1[NumBuffer];
        pDstState->FSM_R3 = pFSM_X2[NumBuffer];
}

/**
 * @brief Provides size of key schedule structure
 * @return Key schedule structure in bytes
 */
size_t SNOW3G_KEY_SCHED_SIZE(void)
{
        return sizeof(snow3g_key_schedule_t);
}

/**
 * @brief Key schedule initialisation
 * @param[in]  pKey  pointer to a 16-byte key
 * @param[out] pCtx  pointer to key schedule structure
 * @return Operation status
 * @retval 0 all OK
 * @retval -1 parameter error
 */
int SNOW3G_INIT_KEY_SCHED(const void *pKey, snow3g_key_schedule_t *pCtx)
{
#ifdef SAFE_PARAM
        if ((pKey == NULL) || (pCtx == NULL))
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (pKey == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return -1;
        }
        if (pCtx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return -1;
        }
#endif

        const uint32_t *pKey32 = pKey;

        pCtx->k[3] = BSWAP32(pKey32[0]);
        pCtx->k[2] = BSWAP32(pKey32[1]);
        pCtx->k[1] = BSWAP32(pKey32[2]);
        pCtx->k[0] = BSWAP32(pKey32[3]);

        return 0;
}

/**
 * @brief Single buffer F8 encrypt/decrypt
 *
 * Single buffer enc/dec with IV and precomputed key schedule
 *
 * @param[in]  pHandle       pointer to precomputed key schedule
 * @param[in]  pIV           pointer to IV
 * @param[in]  pBufferIn     pointer to an input buffer
 * @param[out] pBufferOut    pointer to an output buffer
 * @param[in]  lengthInBytes message length in bits
 */
void SNOW3G_F8_1_BUFFER(const snow3g_key_schedule_t *pHandle,
                        const void *pIV,
                        const void *pBufferIn,
                        void  *pBufferOut,
                        const uint32_t lengthInBytes)
{
#ifdef SAFE_PARAM

        /* reset error status */
        imb_set_errno(NULL, 0);

        if (pHandle == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pIV == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pBufferOut == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
        if ((lengthInBytes == 0) || (lengthInBytes > SNOW3G_MAX_BYTELEN)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        snow3gKeyState1_t ctx;

        /* Initialize the schedule from the IV */
        snow3gStateInitialize_1(&ctx, pHandle, pIV);

        /* Clock FSM and LFSR once, ignore the key stream */
        (void) snow3g_keystream_1_4(&ctx);

        f8_snow3g(&ctx, pBufferIn, pBufferOut, lengthInBytes);

#ifdef SAFE_DATA
        CLEAR_MEM(&ctx, sizeof(ctx));
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */
}

/**
 * @brief Single bit-length buffer F8 encrypt/decrypt
 * @param[in] pHandle      pointer to precomputed key schedule
 * @param[in] pIV          pointer to IV
 * @param[in] pBufferIn    pointer to an input buffer
 * @param[out] pBufferOut  pointer to an output buffer
 * @param[in] lengthInBits message length in bits
 * @param[in] offsetInBits message offset in bits
 */
void SNOW3G_F8_1_BUFFER_BIT(const snow3g_key_schedule_t *pHandle,
                            const void *pIV,
                            const void *pBufferIn,
                            void *pBufferOut,
                            const uint32_t lengthInBits,
                            const uint32_t offsetInBits)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (pHandle == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pIV == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pBufferOut == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
        if ((lengthInBits == 0) || (lengthInBits > SNOW3G_MAX_BITLEN)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        snow3gKeyState1_t ctx;

        /* Initialize the schedule from the IV */
        snow3gStateInitialize_1(&ctx, pHandle, pIV);

        /* Clock FSM and LFSR once, ignore the key stream */
        (void) snow3g_keystream_1_4(&ctx);

        f8_snow3g_bit(&ctx, pBufferIn, pBufferOut, lengthInBits, offsetInBits);

#ifdef SAFE_DATA
        CLEAR_MEM(&ctx, sizeof(ctx));
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */
}

/**
 * @brief Two buffer F8 encrypt/decrypt with the same key schedule
 *
 * Two buffers enc/dec with the same key schedule.
 * The 2 IVs are independent and are passed as an array of pointers.
 * Each buffer and data length are separate.
 *
 * @param[in] pHandle      pointer to precomputed key schedule
 * @param[in] pIV1         pointer to IV
 * @param[in] pIV2         pointer to IV
 * @param[in] pBufIn1      pointer to an input buffer
 * @param[in] pBufOut1     pointer to an output buffer
 * @param[in] lenInBytes1  message size in bytes
 * @param[in] pBufIn2      pointer to an input buffer
 * @param[in] pBufOut2     pointer to an output buffer
 * @param[in] lenInBytes2  message size in bytes
 */
void SNOW3G_F8_2_BUFFER(const snow3g_key_schedule_t *pHandle,
                        const void *pIV1,
                        const void *pIV2,
                        const void *pBufIn1,
                        void *pBufOut1,
                        const uint32_t lenInBytes1,
                        const void *pBufIn2,
                        void *pBufOut2,
                        const uint32_t lenInBytes2)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (pHandle == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pIV1 == NULL || pIV2 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }

        if (pBufIn1 == NULL || pBufIn2 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pBufOut1 == NULL || pBufOut2 == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_DST);
                return;
        }
        if ((lenInBytes1 == 0) || (lenInBytes1 > SNOW3G_MAX_BYTELEN) ||
            (lenInBytes2 == 0) || (lenInBytes2 > SNOW3G_MAX_BYTELEN)) {
                imb_set_errno(NULL, IMB_ERR_CIPH_LEN);
                return;
        }
#endif
#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        snow3gKeyState1_t ctx1, ctx2;

        /* Initialize the schedule from the IV */
        snow3gStateInitialize_1(&ctx1, pHandle, pIV1);

        /* Clock FSM and LFSR once, ignore the key stream */
        (void) snow3g_keystream_1_4(&ctx1);

        /* data processing for packet 1 */
        f8_snow3g(&ctx1, pBufIn1, pBufOut1, lenInBytes1);

        /* Initialize the schedule from the IV */
        snow3gStateInitialize_1(&ctx2, pHandle, pIV2);

        /* Clock FSM and LFSR once, ignore the key stream */
        (void) snow3g_keystream_1_4(&ctx2);

        /* data processing for packet 2 */
        f8_snow3g(&ctx2, pBufIn2, pBufOut2, lenInBytes2);

#ifdef SAFE_DATA
        CLEAR_MEM(&ctx1, sizeof(ctx1));
        CLEAR_MEM(&ctx2, sizeof(ctx2));
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

}

/**
 * @brief Four buffer F8 encrypt/decrypt with the same key schedule
 *
 * Four packets enc/dec with the same key schedule.
 * The 4 IVs are independent and are passed as an array of pointers.
 * Each buffer and data length are separate.
 *
 * @param[in] pHandle         pointer to precomputed key schedule
 * @param[in] pIV1            pointer to IV
 * @param[in] pIV2            pointer to IV
 * @param[in] pIV3            pointer to IV
 * @param[in] pIV4            pointer to IV
 * @param[in] pBufferIn1      pointer to an input buffer
 * @param[in] pBufferOut1     pointer to an output buffer
 * @param[in] lengthInBytes1  message size in bytes
 * @param[in] pBufferIn2      pointer to an input buffer
 * @param[in] pBufferOut2     pointer to an output buffer
 * @param[in] lengthInBytes2  message size in bytes
 * @param[in] pBufferIn3      pointer to an input buffer
 * @param[in] pBufferOut3     pointer to an output buffer
 * @param[in] lengthInBytes3  message size in bytes
 * @param[in] pBufferIn4      pointer to an input buffer
 * @param[in] pBufferOut4     pointer to an output buffer
 * @param[in] lengthInBytes4  message size in bytes
 */
void SNOW3G_F8_4_BUFFER(const snow3g_key_schedule_t *pHandle,
                        const void *pIV1,
                        const void *pIV2,
                        const void *pIV3,
                        const void *pIV4,
                        const void *pBufferIn1,
                        void *pBufferOut1,
                        const uint32_t lengthInBytes1,
                        const void *pBufferIn2,
                        void *pBufferOut2,
                        const uint32_t lengthInBytes2,
                        const void *pBufferIn3,
                        void *pBufferOut3,
                        const uint32_t lengthInBytes3,
                        const void *pBufferIn4,
                        void *pBufferOut4,
                        const uint32_t lengthInBytes4)
{
        const size_t num_lanes = 4;
        snow3gKeyState4_t ctx;
        uint32_t lenInBytes[4];
        uint8_t *pBufferOut[4];
        const uint8_t *pBufferIn[4];
        uint32_t bytes, qwords, i;

        cptr_copy_4((const void **)pBufferIn,
                    pBufferIn1, pBufferIn2, pBufferIn3, pBufferIn4);
        ptr_copy_4((void **)pBufferOut, pBufferOut1, pBufferOut2,
                   pBufferOut3, pBufferOut4);
        length_copy_4(lenInBytes, lengthInBytes1, lengthInBytes2,
                      lengthInBytes3, lengthInBytes4);

#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (pHandle == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if ((pIV1 == NULL) || pIV2 == NULL ||
            (pIV3 == NULL) || (pIV4 == NULL)) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }
        if (!cptr_check((const void * const *)pBufferIn, num_lanes,
                        IMB_ERR_NULL_SRC))
                return;
        if (!ptr_check((void **)pBufferOut, num_lanes, IMB_ERR_NULL_DST))
                return;
        if (!length_check(lenInBytes, num_lanes))
                return;
#endif

#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        /* find min common length */
        bytes = length_find_min(lenInBytes, num_lanes);
        qwords = bytes / SNOW3G_8_BYTES;

        /* subtract min common length from all buffers */
        length_sub(lenInBytes, num_lanes, qwords * SNOW3G_8_BYTES);

        /* Initialize the schedule from the IV */
        snow3gStateInitialize_4(&ctx, pHandle, pIV1, pIV2, pIV3, pIV4);

        /* Clock FSM and LFSR once, ignore the key stream */
        (void) snow3g_keystream_4_4(&ctx);

        /* generates 8 bytes at a time on all streams */
        while (qwords >= 2) {
                __m128i ks[4];

                snow3g_keystream_4_16(&ctx, ks);

                for (i = 0; i < num_lanes; i++) {
                        const __m128i in =
                                _mm_loadu_si128((const __m128i *)pBufferIn[i]);

                        _mm_storeu_si128((__m128i *)pBufferOut[i],
                                         _mm_xor_si128(in, ks[i]));

                        pBufferOut[i] += (2 * SNOW3G_8_BYTES);
                        pBufferIn[i] += (2 * SNOW3G_8_BYTES);
                }

                qwords = qwords - 2;
        }

        while (qwords--) {
                __m128i H, L; /* 4 bytes of key stream */

                snow3g_keystream_4_8(&ctx, &L, &H);

                pBufferIn[0] = xor_keystrm_rev(pBufferOut[0], pBufferIn[0],
                                               _mm_extract_epi64(L, 0));
                pBufferIn[1] = xor_keystrm_rev(pBufferOut[1], pBufferIn[1],
                                               _mm_extract_epi64(L, 1));
                pBufferIn[2] = xor_keystrm_rev(pBufferOut[2], pBufferIn[2],
                                               _mm_extract_epi64(H, 0));
                pBufferIn[3] = xor_keystrm_rev(pBufferOut[3], pBufferIn[3],
                                               _mm_extract_epi64(H, 1));

                for (i = 0; i < num_lanes; i++)
                        pBufferOut[i] += SNOW3G_8_BYTES;
        }

        /* process the remaining of each buffer
         *  - extract the LFSR and FSM structures
         *  - Continue process 1 buffer
         */
        for (i = 0; i < num_lanes; i++) {
                snow3gKeyState1_t ctx_t;

                if (lenInBytes[i] == 0)
                        continue;
                snow3gStateConvert_4(&ctx, &ctx_t, i);
                f8_snow3g(&ctx_t, pBufferIn[i], pBufferOut[i], lenInBytes[i]);
        }

#ifdef SAFE_DATA
        CLEAR_MEM(&ctx, sizeof(ctx));
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */
}

#ifdef AVX2
/**
 * @brief Multiple-key 8 buffer F8/F9 key stream generation
 *
 * Processes 8 packets 32 or 8 bytes at a time.
 * Use different key schedule for each buffer.
 *
 * @param[in] pKey          pointer to an array of key schedules
 * @param[in] IV            pointer to an array of IV's
 * @param[in] pBufferIn     pointer to an array of input buffers
 * @param[out] pBufferOut   pointer to an array of output buffers
 * @param[in] lengthInBytes pointer to an array of message lengths in bytes
 */
static inline void
snow3g_8_buffer_ks_32_8_multi(const snow3g_key_schedule_t * const pKey[],
                              const void * const IV[],
                              const void * const pBufferIn[],
                              void *pBufferOut[],
                              const uint32_t *lengthInBytes)
{
        const size_t num_lanes = 8;
        const size_t big_block_size = 32;
        const size_t small_block_size = SNOW3G_BLOCK_SIZE;
        const uint8_t *tBufferIn[8];
        uint8_t *tBufferOut[8];
        uint32_t tLenInBytes[8];
        snow3gKeyState8_t ctx;
        const uint32_t bytes = length_find_min(lengthInBytes, num_lanes);
        uint32_t bytes_left = bytes & (~(small_block_size - 1));
        size_t i;

        memcpy((void *)tBufferIn, (const void *)pBufferIn,
               sizeof(tBufferIn));
        memcpy((void *)tBufferOut, (const void *)pBufferOut,
               sizeof(tBufferOut));
        memcpy((void *)tLenInBytes, (const void *)lengthInBytes,
               sizeof(tLenInBytes));

        /* Initialize the schedule from the IV */
        snow3gStateInitialize_8_multiKey(&ctx, pKey, IV);

        /* Clock FSM and LFSR once, ignore the key stream */
        (void) snow3g_keystream_8_4(&ctx);

        if (bytes_left >= big_block_size) {
                const uint32_t blocks = bytes / big_block_size;
                __m256i ks[8];

                /*
                 * subtract common, multiple of block size,
                 * length from all lanes
                 */
                length_sub(tLenInBytes, num_lanes, blocks * big_block_size);
                bytes_left -= blocks * big_block_size;

                /* generates 8 sets at a time on all streams */
                for (i = 0; i < blocks; i++) {
                        size_t j;

                        snow3g_keystream_8_32(&ctx, ks);

                        for (j = 0; j < num_lanes; j++) {
                                const __m256i *in_ptr =
                                        (const __m256i *)tBufferIn[j];
                                const __m256i in_val =
                                        _mm256_loadu_si256(in_ptr);
                                const __m256i xor_val =
                                        _mm256_xor_si256(in_val, ks[j]);

                                _mm256_storeu_si256((__m256i *)tBufferOut[j],
                                                    xor_val);

                                tBufferOut[j] += big_block_size;
                                tBufferIn[j] += big_block_size;
                        }
                }

#ifdef SAFE_DATA
                CLEAR_MEM(ks, sizeof(ks));
#endif /* SAFE_DATA */
        }

        if (bytes_left >= small_block_size) {
                const uint32_t blocks = bytes_left / small_block_size;

                length_sub(tLenInBytes, num_lanes, blocks * small_block_size);

                /* generates 8 sets at a time on all streams */
                for (i = 0; i < blocks; i++) {
                        __m256i H, L; /* 8 bytes of key stream */
                        size_t j;

                        snow3g_keystream_8_8(&ctx, &L, &H);

                        tBufferIn[0] =
                                xor_keystrm_rev(tBufferOut[0], tBufferIn[0],
                                                _mm256_extract_epi64(L, 0));
                        tBufferIn[1] =
                                xor_keystrm_rev(tBufferOut[1], tBufferIn[1],
                                                _mm256_extract_epi64(L, 1));
                        tBufferIn[2] =
                                xor_keystrm_rev(tBufferOut[2], tBufferIn[2],
                                                _mm256_extract_epi64(H, 0));
                        tBufferIn[3] =
                                xor_keystrm_rev(tBufferOut[3], tBufferIn[3],
                                                _mm256_extract_epi64(H, 1));
                        tBufferIn[4] =
                                xor_keystrm_rev(tBufferOut[4], tBufferIn[4],
                                                _mm256_extract_epi64(L, 2));
                        tBufferIn[5] =
                                xor_keystrm_rev(tBufferOut[5], tBufferIn[5],
                                                _mm256_extract_epi64(L, 3));
                        tBufferIn[6] =
                                xor_keystrm_rev(tBufferOut[6], tBufferIn[6],
                                                _mm256_extract_epi64(H, 2));
                        tBufferIn[7] =
                                xor_keystrm_rev(tBufferOut[7], tBufferIn[7],
                                                _mm256_extract_epi64(H, 3));

                        for (j = 0; j < num_lanes; j++)
                                tBufferOut[j] += small_block_size;
                }
        }

        /* process the remaining of each buffer
         *  - extract the LFSR and FSM structures
         *  - Continue process 1 buffer
         */
        for (i = 0; i < num_lanes; i++) {
                snow3gKeyState1_t t_ctx;

                if (tLenInBytes[i] == 0)
                        continue;

                snow3gStateConvert_8(&ctx, &t_ctx, i);
                f8_snow3g(&t_ctx, tBufferIn[i], tBufferOut[i], tLenInBytes[i]);
        }

#ifdef SAFE_DATA
        CLEAR_MEM(&ctx, sizeof(ctx));
#endif /* SAFE_DATA */
}

/**
 * @brief Single-key 8 buffer F8/F9 key stream generation
 *
 * Processes 8 packets 32 or 8 bytes at a time.
 * Use same key schedule for each buffer.
 *
 * @param[in] pKey          pointer to a key schedule
 * @param[in] IV            pointer to an array of IV's
 * @param[in] pBufferIn     pointer to an array of input buffers
 * @param[out] pBufferOut   pointer to an array of output buffers
 * @param[in] lengthInBytes pointer to an array of message lengths in bytes
 */
static inline void
snow3g_8_buffer_ks_32_8(const snow3g_key_schedule_t *pKey,
                        const void * const IV[],
                        const uint8_t *pBufferIn[],
                        uint8_t *pBufferOut[],
                        uint32_t *lengthInBytes)
{
        const size_t num_lanes = 8;
        const size_t big_block_size = 32;
        const size_t small_block_size = SNOW3G_8_BYTES;
        const uint32_t bytes = length_find_min(lengthInBytes, num_lanes);
        snow3gKeyState8_t ctx;
        uint32_t i, bytes_left = bytes & (~(small_block_size - 1));

        /* Initialize the schedule from the IV */
        snow3gStateInitialize_8(&ctx, pKey, IV[0], IV[1], IV[2],
                                IV[3], IV[4], IV[5], IV[6], IV[7]);

        /* Clock FSM and LFSR once, ignore the key stream */
        (void) snow3g_keystream_8_4(&ctx);

        if (bytes_left >= big_block_size) {
                const uint32_t blocks = bytes_left / big_block_size;
                __m256i ks[8];

                length_sub(lengthInBytes, num_lanes, blocks * big_block_size);
                bytes_left -= blocks * big_block_size;

                /* generates 8 sets at a time on all streams */
                for (i = 0; i < blocks; i++) {
                        uint32_t j;

                        snow3g_keystream_8_32(&ctx, ks);

                        for (j = 0; j < num_lanes; j++) {
                                const __m256i *in_ptr =
                                        (const __m256i *)pBufferIn[j];
                                const __m256i in_val =
                                        _mm256_loadu_si256(in_ptr);
                                const __m256i xor_val =
                                        _mm256_xor_si256(in_val, ks[j]);

                                _mm256_storeu_si256((__m256i *)pBufferOut[j],
                                                    xor_val);

                                pBufferOut[j] += big_block_size;
                                pBufferIn[j] += big_block_size;
                        }
                }

#ifdef SAFE_DATA
                CLEAR_MEM(ks, sizeof(ks));
#endif /* SAFE_DATA */
        }

        if (bytes_left >= small_block_size) {
                const uint32_t blocks = bytes_left / small_block_size;

                length_sub(lengthInBytes, num_lanes, blocks * small_block_size);

                /* generates 8 sets at a time on all streams */
                for (i = 0; i < blocks; i++) {
                        __m256i H, L; /* 8 bytes of key stream */
                        uint32_t j;

                        snow3g_keystream_8_8(&ctx, &L, &H);

                        pBufferIn[0] =
                                xor_keystrm_rev(pBufferOut[0], pBufferIn[0],
                                                _mm256_extract_epi64(L, 0));
                        pBufferIn[1] =
                                xor_keystrm_rev(pBufferOut[1], pBufferIn[1],
                                                _mm256_extract_epi64(L, 1));
                        pBufferIn[2] =
                                xor_keystrm_rev(pBufferOut[2], pBufferIn[2],
                                                _mm256_extract_epi64(H, 0));
                        pBufferIn[3] =
                                xor_keystrm_rev(pBufferOut[3], pBufferIn[3],
                                                _mm256_extract_epi64(H, 1));
                        pBufferIn[4] =
                                xor_keystrm_rev(pBufferOut[4], pBufferIn[4],
                                                _mm256_extract_epi64(L, 2));
                        pBufferIn[5] =
                                xor_keystrm_rev(pBufferOut[5], pBufferIn[5],
                                                _mm256_extract_epi64(L, 3));
                        pBufferIn[6] =
                                xor_keystrm_rev(pBufferOut[6], pBufferIn[6],
                                                _mm256_extract_epi64(H, 2));
                        pBufferIn[7] =
                                xor_keystrm_rev(pBufferOut[7], pBufferIn[7],
                                                _mm256_extract_epi64(H, 3));

                        for (j = 0; j < num_lanes; j++)
                                pBufferOut[j] += small_block_size;
                }
        }

        /* process the remaining of each buffer
         *  - extract the LFSR and FSM structures
         *  - Continue process 1 buffer
         */
        for (i = 0; i < num_lanes; i++) {
                snow3gKeyState1_t ctx_t;

                if (lengthInBytes[i] == 0)
                        continue;

                snow3gStateConvert_8(&ctx, &ctx_t, i);
                f8_snow3g(&ctx_t, pBufferIn[i], pBufferOut[i],
                          lengthInBytes[i]);
        }

#ifdef SAFE_DATA
        CLEAR_MEM(&ctx, sizeof(ctx));
#endif /* SAFE_DATA */
}

#endif /* AVX2 */

/**
 * @brief Multiple-key 8 buffer F8 encrypt/decrypt
 *
 * Eight packets enc/dec with eight respective key schedules.
 * The 8 IVs are independent and are passed as an array of pointers.
 * Each buffer and data length are separate.
 *
 * @param[in] pKey          pointer to an array of key schedules
 * @param[in] IV            pointer to an array of IV's
 * @param[in] pBufferIn     pointer to an array of input buffers
 * @param[out] pBufferOut   pointer to an array of output buffers
 * @param[in] lengthInBytes pointer to an array of message lengths in bytes
 */
void SNOW3G_F8_8_BUFFER_MULTIKEY(const snow3g_key_schedule_t * const pKey[],
                                 const void * const IV[],
                                 const void * const BufferIn[],
                                 void *BufferOut[],
                                 const uint32_t lengthInBytes[])
{
        const size_t num_lanes = 8;

#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (!cptr_check((const void * const *)pKey,
                        num_lanes,
                        IMB_ERR_NULL_EXP_KEY))
                return;
        if (!cptr_check((const void * const *)IV,
                        num_lanes,
                        IMB_ERR_NULL_IV))
                return;
        if (!cptr_check((const void * const *)BufferIn,
                        num_lanes,
                        IMB_ERR_NULL_SRC))
                return;
        if (!ptr_check((void **)BufferOut, num_lanes, IMB_ERR_NULL_DST))
                return;
        if (!length_check(lengthInBytes, num_lanes))
                return;
#endif

#ifndef AVX2
        /* Basic C workaround for lack of non AVX2 implementation */
        size_t i;

        for (i = 0; i < num_lanes; i++)
                SNOW3G_F8_1_BUFFER(pKey[i], IV[i], BufferIn[i], BufferOut[i],
                                   lengthInBytes[i]);
#else

#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        (void) num_lanes; /* avoid compiler warning */
        snow3g_8_buffer_ks_32_8_multi(pKey, IV, BufferIn,
                                      BufferOut, lengthInBytes);

#ifdef SAFE_DATA
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#endif /* AVX2 */
}

/**
 * @brief 8 buffer F8 encrypt/decrypt with the same key schedule
 *
 * Eight packets enc/dec with the same key schedule.
 * The 8 IVs are independent and are passed as an array of pointers.
 * Each buffer and data length are separate.
 *
 * @param[in] pHandle         pointer to precomputed key schedule
 * @param[in] pIV1            pointer to IV
 * @param[in] pIV2            pointer to IV
 * @param[in] pIV3            pointer to IV
 * @param[in] pIV4            pointer to IV
 * @param[in] pIV5            pointer to IV
 * @param[in] pIV6            pointer to IV
 * @param[in] pIV7            pointer to IV
 * @param[in] pIV8            pointer to IV
 * @param[in] pBufIn1         pointer to an input buffer
 * @param[in] pBufOut1        pointer to an output buffer
 * @param[in] lenInBytes1     message size in bytes
 * @param[in] pBufIn2         pointer to an input buffer
 * @param[in] pBufOut2        pointer to an output buffer
 * @param[in] lenInBytes2     message size in bytes
 * @param[in] pBufIn3         pointer to an input buffer
 * @param[in] pBufOut3        pointer to an output buffer
 * @param[in] lenInBytes3     message size in bytes
 * @param[in] pBufIn4         pointer to an input buffer
 * @param[in] pBufOut4        pointer to an output buffer
 * @param[in] lenInBytes4     message size in bytes
 * @param[in] pBufIn5         pointer to an input buffer
 * @param[in] pBufOut5        pointer to an output buffer
 * @param[in] lenInBytes5     message size in bytes
 * @param[in] pBufIn6         pointer to an input buffer
 * @param[in] pBufOut6        pointer to an output buffer
 * @param[in] lenInBytes6     message size in bytes
 * @param[in] pBufIn7         pointer to an input buffer
 * @param[in] pBufOut7        pointer to an output buffer
 * @param[in] lenInBytes7     message size in bytes
 * @param[in] pBufIn8         pointer to an input buffer
 * @param[in] pBufOut8        pointer to an output buffer
 * @param[in] lenInBytes8     message size in bytes
 */
void SNOW3G_F8_8_BUFFER(const snow3g_key_schedule_t *pHandle,
                        const void *pIV1,
                        const void *pIV2,
                        const void *pIV3,
                        const void *pIV4,
                        const void *pIV5,
                        const void *pIV6,
                        const void *pIV7,
                        const void *pIV8,
                        const void *pBufIn1,
                        void *pBufOut1,
                        const uint32_t lenInBytes1,
                        const void *pBufIn2,
                        void *pBufOut2,
                        const uint32_t lenInBytes2,
                        const void *pBufIn3,
                        void *pBufOut3,
                        const uint32_t lenInBytes3,
                        const void *pBufIn4,
                        void *pBufOut4,
                        const uint32_t lenInBytes4,
                        const void *pBufIn5,
                        void *pBufOut5,
                        const uint32_t lenInBytes5,
                        const void *pBufIn6,
                        void *pBufOut6,
                        const uint32_t lenInBytes6,
                        const void *pBufIn7,
                        void *pBufOut7,
                        const uint32_t lenInBytes7,
                        const void *pBufIn8,
                        void *pBufOut8,
                        const uint32_t lenInBytes8)
{
        uint32_t lengthInBytes[8];
        const uint8_t *pBufferIn[8];
        const void *pIV[8];
        uint8_t *pBufferOut[8];

        length_copy_8(lengthInBytes,
                      lenInBytes1, lenInBytes2, lenInBytes3, lenInBytes4,
                      lenInBytes5, lenInBytes6, lenInBytes7, lenInBytes8);

        cptr_copy_8((const void **)pBufferIn,
                    pBufIn1, pBufIn2, pBufIn3, pBufIn4,
                    pBufIn5, pBufIn6, pBufIn7, pBufIn8);

        cptr_copy_8(pIV, pIV1, pIV2, pIV3, pIV4, pIV5, pIV6, pIV7, pIV8);

        ptr_copy_8((void **)pBufferOut,
                   pBufOut1, pBufOut2, pBufOut3, pBufOut4,
                   pBufOut5, pBufOut6, pBufOut7, pBufOut8);

#ifdef SAFE_PARAM
        const size_t num_lanes = 8;

        /* reset error status */
        imb_set_errno(NULL, 0);

        if (pHandle == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (!cptr_check(pIV, num_lanes, IMB_ERR_NULL_IV))
                return;
        if (!cptr_check((const void * const *)pBufferIn,
                        num_lanes,
                        IMB_ERR_NULL_SRC))
                return;
        if (!ptr_check((void **)pBufferOut, num_lanes, IMB_ERR_NULL_DST))
                return;
        if (!length_check(lengthInBytes, num_lanes))
                return;
#endif

#ifdef AVX2

#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        snow3g_8_buffer_ks_32_8(pHandle, pIV, pBufferIn,
                                pBufferOut, lengthInBytes);

#ifdef SAFE_DATA
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif
#else  /* ~AVX2 */
        SNOW3G_F8_4_BUFFER(pHandle,
                           pIV[0], pIV[1], pIV[2], pIV[3],
                           pBufferIn[0], pBufferOut[0], lengthInBytes[0],
                           pBufferIn[1], pBufferOut[1], lengthInBytes[1],
                           pBufferIn[2], pBufferOut[2], lengthInBytes[2],
                           pBufferIn[3], pBufferOut[3], lengthInBytes[3]);

        SNOW3G_F8_4_BUFFER(pHandle,
                           pIV[4], pIV[5], pIV[6], pIV[7],
                           pBufferIn[4], pBufferOut[4], lengthInBytes[4],
                           pBufferIn[5], pBufferOut[5], lengthInBytes[5],
                           pBufferIn[6], pBufferOut[6], lengthInBytes[6],
                           pBufferIn[7], pBufferOut[7], lengthInBytes[7]);
#endif /* AVX */
}

/**
 * @brief Single-key N buffer F8 encrypt/decrypt
 *
 * Performs F8 enc/dec on N packets.
 * The input IV's are passed in Little Endian format.
 * The KeySchedule is in Little Endian format.
 *
 * @param[in] pCtx          pointer to a key schedule
 * @param[in] IV            pointer to an array of IV's
 * @param[in] pBufferIn     pointer to an array of input buffers
 * @param[out] pBufferOut   pointer to an array of output buffers
 * @param[in] bufLenInBytes pointer to an array of message lengths in bytes
 * @param[in] packetCount   number of packets to process (N)
 */
void SNOW3G_F8_N_BUFFER(const snow3g_key_schedule_t *pCtx,
                        const void * const IV[],
                        const void * const pBufferIn[],
                        void *pBufferOut[],
                        const uint32_t bufLenInBytes[],
                        const uint32_t packetCount)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);

        if (pCtx == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (!cptr_check(IV, packetCount, IMB_ERR_NULL_IV))
                return;
        if (!cptr_check((const void * const *)pBufferIn,
                        packetCount,
                        IMB_ERR_NULL_SRC))
                return;
        if (!ptr_check((void **)pBufferOut, packetCount, IMB_ERR_NULL_DST))
                return;
        if (!length_check(bufLenInBytes, packetCount))
                return;
#endif

#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        if (packetCount > NUM_PACKETS_16) {
                pBufferOut[0] = NULL;
                printf("packetCount too high (%d)\n", packetCount);
                return;
        }

        uint32_t packet_index, inner_index, pktCnt = packetCount;
        int sortNeeded = 0, tempLen = 0;
        uint8_t *srctempbuff;
        uint8_t *dsttempbuff;
        uint8_t *ivtempbuff;
        uint8_t *pSrcBuf[NUM_PACKETS_16] = {NULL};
        uint8_t *pDstBuf[NUM_PACKETS_16] = {NULL};
        uint8_t *pIV[NUM_PACKETS_16] = {NULL};
        uint32_t lensBuf[NUM_PACKETS_16] = {0};

        memcpy((void *)lensBuf, bufLenInBytes, packetCount * sizeof(uint32_t));
        memcpy((void *)pSrcBuf, pBufferIn, packetCount * sizeof(void *));
        memcpy((void *)pDstBuf, pBufferOut, packetCount * sizeof(void *));
        memcpy((void *)pIV, IV, packetCount * sizeof(void *));

        packet_index = packetCount;

        while (packet_index--) {

                /* check if all packets are sorted by decreasing length */
                if (packet_index > 0 && lensBuf[packet_index - 1] <
                    lensBuf[packet_index]) {
                        /* this packet array is not correctly sorted */
                        sortNeeded = 1;
                }
        }

        if (sortNeeded) {

                /* sort packets in decreasing buffer size from [0] to
                   [n]th packet, ** where buffer[0] will contain longest
                   buffer and buffer[n] will contain the shortest buffer.
                   4 arrays are swapped :
                   - pointers to input buffers
                   - pointers to output buffers
                   - pointers to input IV's
                   - input buffer lengths */
                packet_index = packetCount;
                while (packet_index--) {

                        inner_index = packet_index;
                        while (inner_index--) {

                                if (lensBuf[packet_index] >
                                    lensBuf[inner_index]) {

                                        /* swap buffers to arrange in
                                           descending order from [0]. */
                                        srctempbuff = pSrcBuf[packet_index];
                                        dsttempbuff = pDstBuf[packet_index];
                                        ivtempbuff = pIV[packet_index];
                                        tempLen = lensBuf[packet_index];

                                        pSrcBuf[packet_index] =
                                                pSrcBuf[inner_index];
                                        pDstBuf[packet_index] =
                                                pDstBuf[inner_index];
                                        pIV[packet_index] = pIV[inner_index];
                                        lensBuf[packet_index] =
                                                lensBuf[inner_index];

                                        pSrcBuf[inner_index] = srctempbuff;
                                        pDstBuf[inner_index] = dsttempbuff;
                                        pIV[inner_index] = ivtempbuff;
                                        lensBuf[inner_index] = tempLen;
                                }
                        } /* for inner packet index (inner bubble-sort) */
                }         /* for outer packet index (outer bubble-sort) */
        }                 /* if sortNeeded */

        packet_index = 0;
        /* process 8 buffers at-a-time */
#ifdef AVX2
        while (pktCnt >= 8) {
                pktCnt -= 8;
                SNOW3G_F8_8_BUFFER(pCtx, pIV[packet_index],
                                   pIV[packet_index + 1],
                                   pIV[packet_index + 2],
                                   pIV[packet_index + 3],
                                   pIV[packet_index + 4],
                                   pIV[packet_index + 5],
                                   pIV[packet_index + 6],
                                   pIV[packet_index + 7],
                                   pSrcBuf[packet_index],
                                   pDstBuf[packet_index],
                                   lensBuf[packet_index],
                                   pSrcBuf[packet_index + 1],
                                   pDstBuf[packet_index + 1],
                                   lensBuf[packet_index + 1],
                                   pSrcBuf[packet_index + 2],
                                   pDstBuf[packet_index + 2],
                                   lensBuf[packet_index + 2],
                                   pSrcBuf[packet_index + 3],
                                   pDstBuf[packet_index + 3],
                                   lensBuf[packet_index + 3],
                                   pSrcBuf[packet_index + 4],
                                   pDstBuf[packet_index + 4],
                                   lensBuf[packet_index + 4],
                                   pSrcBuf[packet_index + 5],
                                   pDstBuf[packet_index + 5],
                                   lensBuf[packet_index + 5],
                                   pSrcBuf[packet_index + 6],
                                   pDstBuf[packet_index + 6],
                                   lensBuf[packet_index + 6],
                                   pSrcBuf[packet_index + 7],
                                   pDstBuf[packet_index + 7],
                                   lensBuf[packet_index + 7]);
                packet_index += 8;
        }
#endif
        /* process 4 buffers at-a-time */
        while (pktCnt >= 4) {
                pktCnt -= 4;
                SNOW3G_F8_4_BUFFER(pCtx, pIV[packet_index + 0],
                                   pIV[packet_index + 1],
                                   pIV[packet_index + 2],
                                   pIV[packet_index + 3],
                                   pSrcBuf[packet_index + 0],
                                   pDstBuf[packet_index + 0],
                                   lensBuf[packet_index + 0],
                                   pSrcBuf[packet_index + 1],
                                   pDstBuf[packet_index + 1],
                                   lensBuf[packet_index + 1],
                                   pSrcBuf[packet_index + 2],
                                   pDstBuf[packet_index + 2],
                                   lensBuf[packet_index + 2],
                                   pSrcBuf[packet_index + 3],
                                   pDstBuf[packet_index + 3],
                                   lensBuf[packet_index + 3]);
                packet_index += 4;
        }

        /* process 2 packets at-a-time */
        while (pktCnt >= 2) {
                pktCnt -= 2;
                SNOW3G_F8_2_BUFFER(pCtx, pIV[packet_index + 0],
                                   pIV[packet_index + 1],
                                   pSrcBuf[packet_index + 0],
                                   pDstBuf[packet_index + 0],
                                   lensBuf[packet_index + 0],
                                   pSrcBuf[packet_index + 1],
                                   pDstBuf[packet_index + 1],
                                   lensBuf[packet_index + 1]);
                packet_index += 2;
        }

        /* remaining packets are processed 1 at a time */
        while (pktCnt--) {
                SNOW3G_F8_1_BUFFER(pCtx, pIV[packet_index + 0],
                                   pSrcBuf[packet_index + 0],
                                   pDstBuf[packet_index + 0],
                                   lensBuf[packet_index + 0]);
                packet_index++;
        }
}

/**
 * @brief Multi-key N buffer F8 encrypt/decrypt
 *
 * Performs F8 enc/dec on N packets.
 * The input IV's are passed in Little Endian format.
 * The KeySchedule is in Little Endian format.
 *
 * @param[in]  pCtx          pointer to an array of key schedules
 * @param[in]  IV            pointer to an array of IV's
 * @param[in]  pBufferIn     pointer to an array of input buffers
 * @param[out] pBufferOut    pointer to an array of output buffers
 * @param[in]  bufLenInBytes pointer to an array of message lengths in bytes
 * @param[in]  packetCount   number of packets to process (N)
 */
void SNOW3G_F8_N_BUFFER_MULTIKEY(const snow3g_key_schedule_t * const pCtx[],
                                 const void * const IV[],
                                 const void * const pBufferIn[],
                                 void *pBufferOut[],
                                 const uint32_t bufLenInBytes[],
                                 const uint32_t packetCount)
{
#ifdef SAFE_PARAM
        /* reset error status */
        imb_set_errno(NULL, 0);
        if (!cptr_check((const void * const *)pCtx, packetCount,
            IMB_ERR_NULL_EXP_KEY))
                return;
        if (!cptr_check(IV, packetCount, IMB_ERR_NULL_IV))
                return;
        if (!cptr_check((const void * const *)pBufferIn,
                        packetCount,
                        IMB_ERR_NULL_SRC))
                return;
        if (!ptr_check((void **)pBufferOut, packetCount, IMB_ERR_NULL_DST))
                return;
        if (!length_check(bufLenInBytes, packetCount))
                return;
#endif
#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        if (packetCount > NUM_PACKETS_16) {
                pBufferOut[0] = NULL;
                printf("packetCount too high (%d)\n", packetCount);
                return;
        }

        uint32_t packet_index, inner_index, pktCnt = packetCount;
        int sortNeeded = 0, tempLen = 0;
        uint8_t *srctempbuff;
        uint8_t *dsttempbuff;
        uint8_t *ivtempbuff;
        snow3g_key_schedule_t *pCtxBuf[NUM_PACKETS_16] = {NULL};
        uint8_t *pSrcBuf[NUM_PACKETS_16] = {NULL};
        uint8_t *pDstBuf[NUM_PACKETS_16] = {NULL};
        uint8_t *pIV[NUM_PACKETS_16] = {NULL};
        uint32_t lensBuf[NUM_PACKETS_16] = {0};
        snow3g_key_schedule_t *tempCtx;

        memcpy((void *)pCtxBuf, pCtx, packetCount * sizeof(void *));
        memcpy((void *)lensBuf, bufLenInBytes, packetCount * sizeof(uint32_t));
        memcpy((void *)pSrcBuf, pBufferIn, packetCount * sizeof(void *));
        memcpy((void *)pDstBuf, pBufferOut, packetCount * sizeof(void *));
        memcpy((void *)pIV, IV, packetCount * sizeof(void *));

        packet_index = packetCount;

        while (packet_index--) {

                /* check if all packets are sorted by decreasing length */
                if (packet_index > 0 && lensBuf[packet_index - 1] <
                    lensBuf[packet_index]) {
                        /* this packet array is not correctly sorted */
                        sortNeeded = 1;
                }
        }

        if (sortNeeded) {
                /* sort packets in decreasing buffer size from [0] to [n]th
                   packet, where buffer[0] will contain longest buffer and
                   buffer[n] will contain the shortest buffer.
                   4 arrays are swapped :
                   - pointers to input buffers
                   - pointers to output buffers
                   - pointers to input IV's
                   - input buffer lengths */
                packet_index = packetCount;
                while (packet_index--) {
                        inner_index = packet_index;
                        while (inner_index--) {
                                if (lensBuf[packet_index] >
                                    lensBuf[inner_index]) {
                                        /* swap buffers to arrange in
                                           descending order from [0]. */
                                        srctempbuff = pSrcBuf[packet_index];
                                        dsttempbuff = pDstBuf[packet_index];
                                        ivtempbuff = pIV[packet_index];
                                        tempLen = lensBuf[packet_index];
                                        tempCtx = pCtxBuf[packet_index];

                                        pSrcBuf[packet_index] =
                                                pSrcBuf[inner_index];
                                        pDstBuf[packet_index] =
                                                pDstBuf[inner_index];
                                        pIV[packet_index] = pIV[inner_index];
                                        lensBuf[packet_index] =
                                                lensBuf[inner_index];
                                        pCtxBuf[packet_index] =
                                                pCtxBuf[inner_index];

                                        pSrcBuf[inner_index] = srctempbuff;
                                        pDstBuf[inner_index] = dsttempbuff;
                                        pIV[inner_index] = ivtempbuff;
                                        lensBuf[inner_index] = tempLen;
                                        pCtxBuf[inner_index] = tempCtx;
                                }
                        } /* for inner packet index (inner bubble-sort) */
                }         /* for outer packet index (outer bubble-sort) */
        }                 /* if sortNeeded */

        packet_index = 0;
        /* process 8 buffers at-a-time */
#ifdef AVX2
        while (pktCnt >= 8) {
                pktCnt -= 8;
                SNOW3G_F8_8_BUFFER_MULTIKEY(
                        (const snow3g_key_schedule_t * const *)
                        &pCtxBuf[packet_index],
                        (const void * const *)&pIV[packet_index],
                        (const void * const *)&pSrcBuf[packet_index],
                        (void **)&pDstBuf[packet_index],
                        &lensBuf[packet_index]);
                packet_index += 8;
        }
#endif
        /* @todo process 4 buffers at-a-time */
        /* @todo process 2 packets at-a-time */
        /* remaining packets are processed 1 at a time */
        while (pktCnt--) {
                SNOW3G_F8_1_BUFFER(pCtxBuf[packet_index + 0],
                                   pIV[packet_index + 0],
                                   pSrcBuf[packet_index + 0],
                                   pDstBuf[packet_index + 0],
                                   lensBuf[packet_index + 0]);
                packet_index++;
        }
}

/**
 * @brief Single buffer bit-length F9 function
 *
 * Single buffer digest with IV and precomputed key schedule.
 *
 * @param[in] pHandle      pointer to precomputed key schedule
 * @param[in] pIV          pointer to IV
 * @param[in] pBufferIn    pointer to an input buffer
 * @param[in] lengthInBits message length in bits
 * @param[out] pDigest     pointer to store the F9 digest
 */
void SNOW3G_F9_1_BUFFER(const snow3g_key_schedule_t *pHandle,
                        const void *pIV,
                        const void *pBufferIn,
                        const uint64_t lengthInBits,
                        void *pDigest)
{
#ifdef SAFE_PARAM
        if (pHandle == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pIV == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }
        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pDigest == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }
        if ((lengthInBits == 0) || (lengthInBits > SNOW3G_MAX_BITLEN)) {
                imb_set_errno(NULL, IMB_ERR_AUTH_LEN);
                return;
        }
#endif
#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        snow3gKeyState1_t ctx;
        uint32_t z[5];
        const uint64_t *inputBuffer;

        inputBuffer = (const uint64_t *)pBufferIn;

        /* Initialize the SNOW3G key schedule */
        snow3gStateInitialize_1(&ctx, pHandle, pIV);

        /*Generate 5 key stream words*/
        snow3g_f9_keystream_words(&ctx, &z[0]);

        /* Final MAC */
        *(uint32_t *)pDigest =

#ifdef NO_AESNI
                snow3g_f9_1_buffer_internal_sse_no_aesni(&inputBuffer[0],
                                                         z, lengthInBits);
#elif defined(SSE)
                snow3g_f9_1_buffer_internal_sse(&inputBuffer[0],
                                                z, lengthInBits);
#else /* AVX / AVX2 / AVX512 */
                snow3g_f9_1_buffer_internal_avx(&inputBuffer[0],
                                                z, lengthInBits);
#endif /* NO_AESNI */
#ifdef SAFE_DATA
        CLEAR_MEM(&z, sizeof(z));
        CLEAR_MEM(&ctx, sizeof(ctx));
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */
}

#ifdef AVX512
/**
 * @brief Single buffer bit-length F9 function
 *
 * Single buffer digest with IV and precomputed key schedule.
 *
 * @param[in] pHandle      pointer to precomputed key schedule
 * @param[in] pIV          pointer to IV
 * @param[in] pBufferIn    pointer to an input buffer
 * @param[in] lengthInBits message length in bits
 * @param[out] pDigest     pointer to store the F9 digest
 */
void snow3g_f9_1_buffer_vaes_avx512(const snow3g_key_schedule_t *pHandle,
                                    const void *pIV,
                                    const void *pBufferIn,
                                    const uint64_t lengthInBits,
                                    void *pDigest)
{
#ifdef SAFE_PARAM
        if (pHandle == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return;
        }
        if (pIV == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_IV);
                return;
        }
        if (pBufferIn == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_SRC);
                return;
        }
        if (pDigest == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_AUTH);
                return;
        }
        if ((lengthInBits == 0) || (lengthInBits > SNOW3G_MAX_BITLEN)) {
                imb_set_errno(NULL, IMB_ERR_AUTH_LEN);
                return;
        }
#endif
#ifdef SAFE_DATA
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */

        snow3gKeyState1_t ctx;
        uint32_t z[5];

        /* Initialize the SNOW3G key schedule */
        snow3gStateInitialize_1(&ctx, pHandle, pIV);

        /*Generate 5 key stream words*/
        snow3g_f9_keystream_words(&ctx, z);

        /* Final MAC */
        *(uint32_t *)pDigest =
                snow3g_f9_1_buffer_internal_vaes_avx512((const uint64_t *)
                                                        pBufferIn, z,
                                                        lengthInBits);
#ifdef SAFE_DATA
        CLEAR_MEM(z, sizeof(z));
        CLEAR_MEM(&ctx, sizeof(ctx));
        CLEAR_SCRATCH_GPS();
        CLEAR_SCRATCH_SIMD_REGS();
#endif /* SAFE_DATA */
}
#endif /* AVX512 */

#endif /* SNOW3G_COMMON_H */
