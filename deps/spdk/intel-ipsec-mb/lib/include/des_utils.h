/*******************************************************************************
  Copyright (c) 2017-2021, Intel Corporation

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

/* DES utility functions and macros */

#ifndef DES_UTILS_H
#define DES_UTILS_H

#include <stdint.h>
#include "intel-ipsec-mb.h"

/**
 * @brief Gets selected bit value out of a 64-bit word
 *
 * @param val 64-bit word
 * @param n bit number (0 to 63) to get value of
 *
 * @return n-th bit value (0 or 1 value only)
 */
__forceinline
uint64_t bit_get64b(const uint64_t val, const unsigned n)
{
        IMB_ASSERT(n < 64);
        return (val >> n) & UINT64_C(1);
}

/**
 * @brief Sets selected bit in a 64-bit word
 *
 * @param val 64-bit word
 * @param n bit number (0 to 63) to get value of
 * @param b bit value (0 or 1)
 *
 * @return val with n-th bit set to value b
 */
__forceinline
uint64_t bit_set64b(const uint64_t val, const unsigned n, const uint64_t b)
{
        const uint64_t m = UINT64_C(1) << n;

        IMB_ASSERT(n < 64);
        return (val & (~m)) | (b << n);
}

/**
 * @brief Permutes bits in a 64-bit word as described by pattern
 *
 * The function goes through pattern array from index 0 to 'size' (max 63).
 * It sets output bit number 'index' to value of
 * bit number 'pattern[index] - 1' from 'in'.
 *
 * @param in 64-bit word to be permuted
 * @param pattern pointer to array defining the permutation
 * @param size is size of the permutation pattern
 *
 * @return permuted in word as described by the pattern
 */
__forceinline
uint64_t permute_64b(const uint64_t in, const uint8_t *pattern, const int size)
{
        uint64_t out = 0;
        int n = 0;

        IMB_ASSERT(size <= 64);

        for (n = 0; n < size; n++) {
                /* '-1' is required as bit numbers in FIPS start with 1 not 0 */
                const int m = ((int) pattern[n]) - 1;
                const uint64_t bit_val = bit_get64b(in, m);

                out = bit_set64b(out, n, bit_val);
        }

        return out;
}

static const uint8_t reflect_tab[16] = {
        /* [ 0] 0000 => 0000 */ 0, /* [ 1] 0001 => 1000 */ 8,
        /* [ 2] 0010 => 0100 */ 4, /* [ 3] 0011 => 1100 */ 12,
        /* [ 4] 0100 => 0010 */ 2, /* [ 5] 0101 => 1010 */ 10,
        /* [ 6] 0110 => 0110 */ 6, /* [ 7] 0111 => 1110 */ 14,
        /* [ 8] 1000 => 0001 */ 1, /* [ 9] 1001 => 1001 */ 9,
        /* [10] 1010 => 0101 */ 5, /* [11] 1011 => 1101 */ 13,
        /* [12] 1100 => 0011 */ 3, /* [13] 1101 => 1011 */ 11,
        /* [14] 1110 => 0111 */ 7, /* [15] 1111 => 1111 */ 15
};

__forceinline
uint8_t reflect_8b(const uint8_t pb)
{
        return reflect_tab[pb >> 4] | (reflect_tab[pb & 15] << 4);
}

__forceinline
uint64_t load64_reflect(const void *key)
{
        const uint8_t *kb = (const uint8_t *) key;

        return ((uint64_t) reflect_8b(kb[0])) |
                ((uint64_t) reflect_8b(kb[1])) << 8 |
                ((uint64_t) reflect_8b(kb[2])) << 16 |
                ((uint64_t) reflect_8b(kb[3])) << 24 |
                ((uint64_t) reflect_8b(kb[4])) << 32 |
                ((uint64_t) reflect_8b(kb[5])) << 40 |
                ((uint64_t) reflect_8b(kb[6])) << 48 |
                ((uint64_t) reflect_8b(kb[7])) << 56;
}


#endif /* DES_UTILS_H */
