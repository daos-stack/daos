/*******************************************************************************
  Copyright (c) 2019-2021, Intel Corporation

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

#ifndef CONSTANT_LOOKUP_H
#define CONSTANT_LOOKUP_H

#include "intel-ipsec-mb.h"

/* intrinsic include is needed for data types used in prototypes */
#ifdef LINUX
#include <x86intrin.h>
#else
#include <intrin.h>
#endif

#ifdef SAFE_LOOKUP
#define LOOKUP8_SSE(_table, _idx, _size) \
        lookup_8bit_sse(_table, _idx, _size)
#define LOOKUP8_AVX(_table, _idx, _size) \
        lookup_8bit_avx(_table, _idx, _size)
#define LOOKUP16_SSE(_table, _idx, _size) \
        lookup_16bit_sse(_table, _idx, _size)
#define LOOKUP16_AVX(_table, _idx, _size) \
        lookup_16bit_avx(_table, _idx, _size)
#define LOOKUP32_SSE(_table, _idx, _size) \
        lookup_32bit_sse(_table, _idx, _size)
#define LOOKUP32_AVX(_table, _idx, _size) \
        lookup_32bit_avx(_table, _idx, _size)
#define LOOKUP64_SSE(_table, _idx, _size) \
        lookup_64bit_sse(_table, _idx, _size)
#define LOOKUP64_AVX(_table, _idx, _size) \
        lookup_64bit_avx(_table, _idx, _size)
#else
#define LOOKUP8_SSE(_table, _idx, _size) \
        _table[_idx]
#define LOOKUP8_AVX(_table, _idx, _size) \
        _table[_idx]
#define LOOKUP16_SSE(_table, _idx, _size) \
        _table[_idx]
#define LOOKUP16_AVX(_table, _idx, _size) \
        _table[_idx]
#define LOOKUP32_SSE(_table, _idx, _size) \
        _table[_idx]
#define LOOKUP32_AVX(_table, _idx, _size) \
        _table[_idx]
#define LOOKUP64_SSE(_table, _idx, _size) \
        _table[_idx]
#define LOOKUP64_AVX(_table, _idx, _size) \
        _table[_idx]
#endif

/**
 * @brief Constant time SSE lookup function on variable size table
 *        with 8-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 8 bit elements in the table (multiple of 16)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint8_t
lookup_8bit_sse(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time AVX lookup function on variable size table
 *        with 8-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 8 bit elements in the table (multiple of 16)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint8_t
lookup_8bit_avx(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time SSE lookup function on variable size table
 *        with 16-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 16 bit elements in the table (multiple of 8)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint16_t
lookup_16bit_sse(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time AVX lookup function on variable size table
 *        with 16-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 16 bit elements in the table (multiple of 8)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint16_t
lookup_16bit_avx(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time SSE lookup function on
 *        variable size table with 32-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 32 bit elements in the table (multiple of 4)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint32_t
lookup_32bit_sse(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time AVX lookup function on
 *        variable size table with 32-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 32 bit elements in the table (multiple of 4)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint32_t
lookup_32bit_avx(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time SSE lookup function on
 *        variable size table with 64-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 64 bit elements in the table (multiple of 2)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint64_t
lookup_64bit_sse(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time AVX lookup function on
 *        variable size table with 64-bit values
 *
 * @param[in] table     Pointer to the table to look up (16-byte aligned)
 * @param[in] idx       Index to look up
 * @param[in] size      Number of 64 bit elements in the table (multiple of 2)
 *
 * @return value to lookup
 */
IMB_DLL_LOCAL uint64_t
lookup_64bit_avx(const void *table, const uint32_t idx, const uint32_t size);

/**
 * @brief Constant time and parallel SSE lookup function on table of
 *        256 elements of 8-bit values.
 *
 * @param[in] indexes   vector with 16 8-bit indexes
 * @param[in] table     pointer to 256 element table
 *
 * @return Vector with 16 8-bit values corresponding to the indexes
 */
IMB_DLL_LOCAL __m128i
lookup_16x8bit_sse(const __m128i indexes, const void *table);

/**
 * @brief Constant time and parallel AVX lookup function on table of
 *        256 elements of 8-bit values.
 *
 * @param[in] indexes   vector with 16 8-bit indexes
 * @param[in] table     pointer to 256 element table
 *
 * @return Vector with 16 8-bit values corresponding to the indexes
 */
IMB_DLL_LOCAL __m128i
lookup_16x8bit_avx(const __m128i indexes, const void *table);

#ifdef AVX2
/**
 * @brief Constant time and parallel AVX2 lookup function on table of
 *        256 elements of 8-bit values.
 *
 * @param[in] indexes   vector with 32 8-bit indexes
 * @param[in] table     pointer to 256 element table
 *
 * @return Vector with 32 8-bit values corresponding to the indexes
 */
IMB_DLL_LOCAL __m256i
lookup_32x8bit_avx2(const __m256i indexes, const void *table);
#endif

#ifdef AVX512
/**
 * @brief Constant time and parallel AVX512 lookup function on table of
 *        256 elements of 8-bit values.
 *
 * @param[in] indexes   memory with 64 8-bit indexes
 * @param[out] ret      memory with 64 8-bit indexes
 * @param[in] table     pointer to 256 element table
 *
 */
IMB_DLL_LOCAL void
lookup_64x8bit_avx512(const void *indices, void *ret, const void *table);

/**
 * @brief Constant time and parallel AVX512/VBMI lookup function on table of
 *        256 elements of 8-bit values.
 *
 * @param[in] indexes   memory with 64 8-bit indexes
 * @param[out] ret      memory with 64 8-bit indexes
 * @param[in] table     pointer to 256 element table
 *
 */
IMB_DLL_LOCAL void
lookup_64x8bit_avx512_vbmi(const void *indices, void *ret, const void *table);
#endif

#endif /* CONSTANT_LOOKUP_H */
