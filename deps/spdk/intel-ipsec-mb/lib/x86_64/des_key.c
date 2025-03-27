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

#include <stdlib.h>
#include <stdint.h>

#include "intel-ipsec-mb.h"
#include "include/des.h"
#include "include/des_utils.h"
#include "include/clear_regs_mem.h"
#include "include/error.h"

/**
 * @brief Rotates 28-bit word
 *
 * Roll right of 28-bit word - used in 28-bit subkey operations
 *
 * @param val 28-bit word to be rotated
 * @param nshift number of bits to rotate by
 *
 * @return val rotated by nshift bits
 */
__forceinline
uint32_t rotate28(const uint32_t val, const unsigned nshift)
{
        const uint32_t mask = (UINT32_C(1) << 28) - UINT32_C(1);

        IMB_ASSERT(nshift <= 28);
        return ((val >> nshift) & mask) |
                ((val << (28 - nshift)) & mask);
}

/**
 * @brief Expands 8 groups of 6bits into 8 groups of 8bits
 *
 * @param in a 48-bit word including 8 groups of 6bits
 *
 * @return 64-bit word with 8 groups of 8bits
 */
__forceinline
uint64_t expand_8x6_to_8x8(const uint64_t in)
{
        return (((in >> (6 * 0)) & UINT64_C(63)) << (8 * 0)) |
                (((in >> (6 * 1)) & UINT64_C(63)) << (8 * 1)) |
                (((in >> (6 * 2)) & UINT64_C(63)) << (8 * 2)) |
                (((in >> (6 * 3)) & UINT64_C(63)) << (8 * 3)) |
                (((in >> (6 * 4)) & UINT64_C(63)) << (8 * 4)) |
                (((in >> (6 * 5)) & UINT64_C(63)) << (8 * 5)) |
                (((in >> (6 * 6)) & UINT64_C(63)) << (8 * 6)) |
                (((in >> (6 * 7)) & UINT64_C(63)) << (8 * 7));
}

static const uint8_t pc1c_table_fips46_3[28] = {
        57, 49, 41, 33, 25, 17,  9,
        1,  58, 50, 42, 34, 26, 18,
        10,  2, 59, 51, 43, 35, 27,
        19, 11,  3, 60, 52, 44, 36
};

static const uint8_t pc1d_table_fips46_3[28] = {
        63, 55, 47, 39, 31, 23, 15,
         7, 62, 54, 46, 38, 30, 22,
        14,  6, 61, 53, 45, 37, 29,
        21, 13,  5, 28, 20, 12, 4
};

static const uint8_t pc2_table_fips46_3[48] = {
        14, 17, 11, 24,  1,  5,
         3, 28, 15,  6, 21, 10,
        23, 19, 12,  4, 26,  8,
        16,  7, 27, 20, 13,  2,
        41, 52, 31, 37, 47, 55,
        30, 40, 51, 45, 33, 48,
        44, 49, 39, 56, 34, 53,
        46, 42, 50, 36, 29, 32
};

static const uint8_t shift_tab_fips46_3[16] = {
        1, 1, 2, 2, 2, 2, 2, 2,
        1, 2, 2, 2, 2, 2, 2, 1
};

int des_key_schedule(uint64_t *ks, const void *key)
{
#ifdef SAFE_PARAM
        imb_set_errno(NULL, 0);
        if (key == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_KEY);
                return -1;
        }
        if (ks == NULL) {
                imb_set_errno(NULL, IMB_ERR_NULL_EXP_KEY);
                return -1;
        }


#endif

        uint64_t c, d;
        uint64_t t = 0;
        int n;

        /* KEY: 56 bits but spread across 64 bits
         * - MSB per byte used for parity
         * - load_and_convert loads the key and swaps bits in bytes
         *   so that bit numbers are more suitable for LE machine and
         *   FIPS46-3 DES tables
         */
        t = load64_reflect(key);

        /* PC1
         * - built from the KEY, PC1 permute tables skip KEY parity bits
         * - c & d are both 28 bits
         */
        c = permute_64b(t, pc1c_table_fips46_3, IMB_DIM(pc1c_table_fips46_3));
        d = permute_64b(t, pc1d_table_fips46_3, IMB_DIM(pc1d_table_fips46_3));

        /* KS rounds */
        for (n = 0; n < 16; n++) {
                c = rotate28((uint32_t)c, (unsigned) shift_tab_fips46_3[n]);
                d = rotate28((uint32_t)d, (unsigned) shift_tab_fips46_3[n]);

                /* PC2 */
                t = permute_64b(c | (d << 28), pc2_table_fips46_3,
                                IMB_DIM(pc2_table_fips46_3));

                /* store KS as 6 bits per byte and keep LE */
                ks[n] = expand_8x6_to_8x8(t);
        }

#ifdef SAFE_DATA
        clear_var(&c, sizeof(c));
        clear_var(&d, sizeof(d));
        clear_var(&t, sizeof(t));
#endif
        return 0;
}
