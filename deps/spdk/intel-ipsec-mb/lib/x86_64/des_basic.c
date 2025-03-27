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

/* basic DES implementation */

#include <stdint.h>
#include <string.h>

#include "intel-ipsec-mb.h"
#include "include/des.h"
#include "include/des_utils.h"
#include "include/clear_regs_mem.h"
#include "include/constant_lookup.h"
#include "include/memcpy.h"

__forceinline
void permute_operation(uint32_t *pa, uint32_t *pb,
                       const uint32_t n, const uint32_t m)
{
        register uint32_t t = (*pb ^ (*pa >> n)) & m;

        *pb ^= t;
        *pa ^= (t << n);
}

/* initial permutation */
__forceinline
void ip_z(uint32_t *pl, uint32_t *pr)
{
        permute_operation(pr, pl, 4, 0x0f0f0f0f);
        permute_operation(pl, pr, 16, 0x0000ffff);
        permute_operation(pr, pl, 2, 0x33333333);
        permute_operation(pl, pr, 8, 0x00ff00ff);
        permute_operation(pr, pl, 1, 0x55555555);
}

/* final permutation */
__forceinline
void fp_z(uint32_t *pl, uint32_t *pr)
{
        permute_operation(pl, pr, 1, 0x55555555);
        permute_operation(pr, pl, 8, 0x00ff00ff);
        permute_operation(pl, pr, 2, 0x33333333);
        permute_operation(pr, pl, 16, 0x0000ffff);
        permute_operation(pl, pr, 4, 0x0f0f0f0f);
}

/* 1st part of DES round
 * - permutes and exands R(32 bits) into 48 bits
 */
__forceinline
uint64_t e_phase(const uint64_t R)
{
        /* E phase as in FIPS46-3 and also 8x6 to 8x8 expansion.
         *
         * Bit selection table for this operation looks as follows:
         *         32, 1,  2,  3,  4,  5,  X, X,
         *         4,  5,  6,  7,  8,  9,  X, X,
         *         8,  9,  10, 11, 12, 13, X, X,
         *         12, 13, 14, 15, 16, 17, X, X,
         *         16, 17, 18, 19, 20, 21, X, X,
         *         20, 21, 22, 23, 24, 25, X, X,
         *         24, 25, 26, 27, 28, 29, X, X,
         *         28, 29, 30, 31, 32,  1, X, X
         * where 'X' is bit value 0.
         */
        return ((R << 1) & UINT64_C(0x3e)) | ((R >> 31) & UINT64_C(1)) |
                ((R << 5) & UINT64_C(0x3f00)) |
                ((R << 9) & UINT64_C(0x3f0000)) |
                ((R << 13) & UINT64_C(0x3f000000)) |
                ((R << 17) & UINT64_C(0x3f00000000)) |
                ((R << 21) & UINT64_C(0x3f0000000000)) |
                ((R << 25) & UINT64_C(0x3f000000000000)) |
                ((R << 29) & UINT64_C(0x1f00000000000000)) |
                ((R & UINT64_C(1)) << 61);
}

static const uint32_t sbox0p[64] = {
        UINT32_C(0x00410100), UINT32_C(0x00010000),
        UINT32_C(0x40400000), UINT32_C(0x40410100),
        UINT32_C(0x00400000), UINT32_C(0x40010100),
        UINT32_C(0x40010000), UINT32_C(0x40400000),
        UINT32_C(0x40010100), UINT32_C(0x00410100),
        UINT32_C(0x00410000), UINT32_C(0x40000100),
        UINT32_C(0x40400100), UINT32_C(0x00400000),
        UINT32_C(0x00000000), UINT32_C(0x40010000),
        UINT32_C(0x00010000), UINT32_C(0x40000000),
        UINT32_C(0x00400100), UINT32_C(0x00010100),
        UINT32_C(0x40410100), UINT32_C(0x00410000),
        UINT32_C(0x40000100), UINT32_C(0x00400100),
        UINT32_C(0x40000000), UINT32_C(0x00000100),
        UINT32_C(0x00010100), UINT32_C(0x40410000),
        UINT32_C(0x00000100), UINT32_C(0x40400100),
        UINT32_C(0x40410000), UINT32_C(0x00000000),
        UINT32_C(0x00000000), UINT32_C(0x40410100),
        UINT32_C(0x00400100), UINT32_C(0x40010000),
        UINT32_C(0x00410100), UINT32_C(0x00010000),
        UINT32_C(0x40000100), UINT32_C(0x00400100),
        UINT32_C(0x40410000), UINT32_C(0x00000100),
        UINT32_C(0x00010100), UINT32_C(0x40400000),
        UINT32_C(0x40010100), UINT32_C(0x40000000),
        UINT32_C(0x40400000), UINT32_C(0x00410000),
        UINT32_C(0x40410100), UINT32_C(0x00010100),
        UINT32_C(0x00410000), UINT32_C(0x40400100),
        UINT32_C(0x00400000), UINT32_C(0x40000100),
        UINT32_C(0x40010000), UINT32_C(0x00000000),
        UINT32_C(0x00010000), UINT32_C(0x00400000),
        UINT32_C(0x40400100), UINT32_C(0x00410100),
        UINT32_C(0x40000000), UINT32_C(0x40410000),
        UINT32_C(0x00000100), UINT32_C(0x40010100)
};

static const uint32_t sbox1p[64] = {
        UINT32_C(0x08021002), UINT32_C(0x00000000),
        UINT32_C(0x00021000), UINT32_C(0x08020000),
        UINT32_C(0x08000002), UINT32_C(0x00001002),
        UINT32_C(0x08001000), UINT32_C(0x00021000),
        UINT32_C(0x00001000), UINT32_C(0x08020002),
        UINT32_C(0x00000002), UINT32_C(0x08001000),
        UINT32_C(0x00020002), UINT32_C(0x08021000),
        UINT32_C(0x08020000), UINT32_C(0x00000002),
        UINT32_C(0x00020000), UINT32_C(0x08001002),
        UINT32_C(0x08020002), UINT32_C(0x00001000),
        UINT32_C(0x00021002), UINT32_C(0x08000000),
        UINT32_C(0x00000000), UINT32_C(0x00020002),
        UINT32_C(0x08001002), UINT32_C(0x00021002),
        UINT32_C(0x08021000), UINT32_C(0x08000002),
        UINT32_C(0x08000000), UINT32_C(0x00020000),
        UINT32_C(0x00001002), UINT32_C(0x08021002),
        UINT32_C(0x00020002), UINT32_C(0x08021000),
        UINT32_C(0x08001000), UINT32_C(0x00021002),
        UINT32_C(0x08021002), UINT32_C(0x00020002),
        UINT32_C(0x08000002), UINT32_C(0x00000000),
        UINT32_C(0x08000000), UINT32_C(0x00001002),
        UINT32_C(0x00020000), UINT32_C(0x08020002),
        UINT32_C(0x00001000), UINT32_C(0x08000000),
        UINT32_C(0x00021002), UINT32_C(0x08001002),
        UINT32_C(0x08021000), UINT32_C(0x00001000),
        UINT32_C(0x00000000), UINT32_C(0x08000002),
        UINT32_C(0x00000002), UINT32_C(0x08021002),
        UINT32_C(0x00021000), UINT32_C(0x08020000),
        UINT32_C(0x08020002), UINT32_C(0x00020000),
        UINT32_C(0x00001002), UINT32_C(0x08001000),
        UINT32_C(0x08001002), UINT32_C(0x00000002),
        UINT32_C(0x08020000), UINT32_C(0x00021000)
};

static const uint32_t sbox2p[64] = {
        UINT32_C(0x20800000), UINT32_C(0x00808020),
        UINT32_C(0x00000020), UINT32_C(0x20800020),
        UINT32_C(0x20008000), UINT32_C(0x00800000),
        UINT32_C(0x20800020), UINT32_C(0x00008020),
        UINT32_C(0x00800020), UINT32_C(0x00008000),
        UINT32_C(0x00808000), UINT32_C(0x20000000),
        UINT32_C(0x20808020), UINT32_C(0x20000020),
        UINT32_C(0x20000000), UINT32_C(0x20808000),
        UINT32_C(0x00000000), UINT32_C(0x20008000),
        UINT32_C(0x00808020), UINT32_C(0x00000020),
        UINT32_C(0x20000020), UINT32_C(0x20808020),
        UINT32_C(0x00008000), UINT32_C(0x20800000),
        UINT32_C(0x20808000), UINT32_C(0x00800020),
        UINT32_C(0x20008020), UINT32_C(0x00808000),
        UINT32_C(0x00008020), UINT32_C(0x00000000),
        UINT32_C(0x00800000), UINT32_C(0x20008020),
        UINT32_C(0x00808020), UINT32_C(0x00000020),
        UINT32_C(0x20000000), UINT32_C(0x00008000),
        UINT32_C(0x20000020), UINT32_C(0x20008000),
        UINT32_C(0x00808000), UINT32_C(0x20800020),
        UINT32_C(0x00000000), UINT32_C(0x00808020),
        UINT32_C(0x00008020), UINT32_C(0x20808000),
        UINT32_C(0x20008000), UINT32_C(0x00800000),
        UINT32_C(0x20808020), UINT32_C(0x20000000),
        UINT32_C(0x20008020), UINT32_C(0x20800000),
        UINT32_C(0x00800000), UINT32_C(0x20808020),
        UINT32_C(0x00008000), UINT32_C(0x00800020),
        UINT32_C(0x20800020), UINT32_C(0x00008020),
        UINT32_C(0x00800020), UINT32_C(0x00000000),
        UINT32_C(0x20808000), UINT32_C(0x20000020),
        UINT32_C(0x20800000), UINT32_C(0x20008020),
        UINT32_C(0x00000020), UINT32_C(0x00808000)
};

static const uint32_t sbox3p[64] = {
        UINT32_C(0x00080201), UINT32_C(0x02000200),
        UINT32_C(0x00000001), UINT32_C(0x02080201),
        UINT32_C(0x00000000), UINT32_C(0x02080000),
        UINT32_C(0x02000201), UINT32_C(0x00080001),
        UINT32_C(0x02080200), UINT32_C(0x02000001),
        UINT32_C(0x02000000), UINT32_C(0x00000201),
        UINT32_C(0x02000001), UINT32_C(0x00080201),
        UINT32_C(0x00080000), UINT32_C(0x02000000),
        UINT32_C(0x02080001), UINT32_C(0x00080200),
        UINT32_C(0x00000200), UINT32_C(0x00000001),
        UINT32_C(0x00080200), UINT32_C(0x02000201),
        UINT32_C(0x02080000), UINT32_C(0x00000200),
        UINT32_C(0x00000201), UINT32_C(0x00000000),
        UINT32_C(0x00080001), UINT32_C(0x02080200),
        UINT32_C(0x02000200), UINT32_C(0x02080001),
        UINT32_C(0x02080201), UINT32_C(0x00080000),
        UINT32_C(0x02080001), UINT32_C(0x00000201),
        UINT32_C(0x00080000), UINT32_C(0x02000001),
        UINT32_C(0x00080200), UINT32_C(0x02000200),
        UINT32_C(0x00000001), UINT32_C(0x02080000),
        UINT32_C(0x02000201), UINT32_C(0x00000000),
        UINT32_C(0x00000200), UINT32_C(0x00080001),
        UINT32_C(0x00000000), UINT32_C(0x02080001),
        UINT32_C(0x02080200), UINT32_C(0x00000200),
        UINT32_C(0x02000000), UINT32_C(0x02080201),
        UINT32_C(0x00080201), UINT32_C(0x00080000),
        UINT32_C(0x02080201), UINT32_C(0x00000001),
        UINT32_C(0x02000200), UINT32_C(0x00080201),
        UINT32_C(0x00080001), UINT32_C(0x00080200),
        UINT32_C(0x02080000), UINT32_C(0x02000201),
        UINT32_C(0x00000201), UINT32_C(0x02000000),
        UINT32_C(0x02000001), UINT32_C(0x02080200)
};

static const uint32_t sbox4p[64] = {
        UINT32_C(0x01000000), UINT32_C(0x00002000),
        UINT32_C(0x00000080), UINT32_C(0x01002084),
        UINT32_C(0x01002004), UINT32_C(0x01000080),
        UINT32_C(0x00002084), UINT32_C(0x01002000),
        UINT32_C(0x00002000), UINT32_C(0x00000004),
        UINT32_C(0x01000004), UINT32_C(0x00002080),
        UINT32_C(0x01000084), UINT32_C(0x01002004),
        UINT32_C(0x01002080), UINT32_C(0x00000000),
        UINT32_C(0x00002080), UINT32_C(0x01000000),
        UINT32_C(0x00002004), UINT32_C(0x00000084),
        UINT32_C(0x01000080), UINT32_C(0x00002084),
        UINT32_C(0x00000000), UINT32_C(0x01000004),
        UINT32_C(0x00000004), UINT32_C(0x01000084),
        UINT32_C(0x01002084), UINT32_C(0x00002004),
        UINT32_C(0x01002000), UINT32_C(0x00000080),
        UINT32_C(0x00000084), UINT32_C(0x01002080),
        UINT32_C(0x01002080), UINT32_C(0x01000084),
        UINT32_C(0x00002004), UINT32_C(0x01002000),
        UINT32_C(0x00002000), UINT32_C(0x00000004),
        UINT32_C(0x01000004), UINT32_C(0x01000080),
        UINT32_C(0x01000000), UINT32_C(0x00002080),
        UINT32_C(0x01002084), UINT32_C(0x00000000),
        UINT32_C(0x00002084), UINT32_C(0x01000000),
        UINT32_C(0x00000080), UINT32_C(0x00002004),
        UINT32_C(0x01000084), UINT32_C(0x00000080),
        UINT32_C(0x00000000), UINT32_C(0x01002084),
        UINT32_C(0x01002004), UINT32_C(0x01002080),
        UINT32_C(0x00000084), UINT32_C(0x00002000),
        UINT32_C(0x00002080), UINT32_C(0x01002004),
        UINT32_C(0x01000080), UINT32_C(0x00000084),
        UINT32_C(0x00000004), UINT32_C(0x00002084),
        UINT32_C(0x01002000), UINT32_C(0x01000004)
};

const uint32_t sbox5p[64] = {
        UINT32_C(0x10000008), UINT32_C(0x00040008),
        UINT32_C(0x00000000), UINT32_C(0x10040400),
        UINT32_C(0x00040008), UINT32_C(0x00000400),
        UINT32_C(0x10000408), UINT32_C(0x00040000),
        UINT32_C(0x00000408), UINT32_C(0x10040408),
        UINT32_C(0x00040400), UINT32_C(0x10000000),
        UINT32_C(0x10000400), UINT32_C(0x10000008),
        UINT32_C(0x10040000), UINT32_C(0x00040408),
        UINT32_C(0x00040000), UINT32_C(0x10000408),
        UINT32_C(0x10040008), UINT32_C(0x00000000),
        UINT32_C(0x00000400), UINT32_C(0x00000008),
        UINT32_C(0x10040400), UINT32_C(0x10040008),
        UINT32_C(0x10040408), UINT32_C(0x10040000),
        UINT32_C(0x10000000), UINT32_C(0x00000408),
        UINT32_C(0x00000008), UINT32_C(0x00040400),
        UINT32_C(0x00040408), UINT32_C(0x10000400),
        UINT32_C(0x00000408), UINT32_C(0x10000000),
        UINT32_C(0x10000400), UINT32_C(0x00040408),
        UINT32_C(0x10040400), UINT32_C(0x00040008),
        UINT32_C(0x00000000), UINT32_C(0x10000400),
        UINT32_C(0x10000000), UINT32_C(0x00000400),
        UINT32_C(0x10040008), UINT32_C(0x00040000),
        UINT32_C(0x00040008), UINT32_C(0x10040408),
        UINT32_C(0x00040400), UINT32_C(0x00000008),
        UINT32_C(0x10040408), UINT32_C(0x00040400),
        UINT32_C(0x00040000), UINT32_C(0x10000408),
        UINT32_C(0x10000008), UINT32_C(0x10040000),
        UINT32_C(0x00040408), UINT32_C(0x00000000),
        UINT32_C(0x00000400), UINT32_C(0x10000008),
        UINT32_C(0x10000408), UINT32_C(0x10040400),
        UINT32_C(0x10040000), UINT32_C(0x00000408),
        UINT32_C(0x00000008), UINT32_C(0x10040008)
};

static const uint32_t sbox6p[64] = {
        UINT32_C(0x00000800), UINT32_C(0x00000040),
        UINT32_C(0x00200040), UINT32_C(0x80200000),
        UINT32_C(0x80200840), UINT32_C(0x80000800),
        UINT32_C(0x00000840), UINT32_C(0x00000000),
        UINT32_C(0x00200000), UINT32_C(0x80200040),
        UINT32_C(0x80000040), UINT32_C(0x00200800),
        UINT32_C(0x80000000), UINT32_C(0x00200840),
        UINT32_C(0x00200800), UINT32_C(0x80000040),
        UINT32_C(0x80200040), UINT32_C(0x00000800),
        UINT32_C(0x80000800), UINT32_C(0x80200840),
        UINT32_C(0x00000000), UINT32_C(0x00200040),
        UINT32_C(0x80200000), UINT32_C(0x00000840),
        UINT32_C(0x80200800), UINT32_C(0x80000840),
        UINT32_C(0x00200840), UINT32_C(0x80000000),
        UINT32_C(0x80000840), UINT32_C(0x80200800),
        UINT32_C(0x00000040), UINT32_C(0x00200000),
        UINT32_C(0x80000840), UINT32_C(0x00200800),
        UINT32_C(0x80200800), UINT32_C(0x80000040),
        UINT32_C(0x00000800), UINT32_C(0x00000040),
        UINT32_C(0x00200000), UINT32_C(0x80200800),
        UINT32_C(0x80200040), UINT32_C(0x80000840),
        UINT32_C(0x00000840), UINT32_C(0x00000000),
        UINT32_C(0x00000040), UINT32_C(0x80200000),
        UINT32_C(0x80000000), UINT32_C(0x00200040),
        UINT32_C(0x00000000), UINT32_C(0x80200040),
        UINT32_C(0x00200040), UINT32_C(0x00000840),
        UINT32_C(0x80000040), UINT32_C(0x00000800),
        UINT32_C(0x80200840), UINT32_C(0x00200000),
        UINT32_C(0x00200840), UINT32_C(0x80000000),
        UINT32_C(0x80000800), UINT32_C(0x80200840),
        UINT32_C(0x80200000), UINT32_C(0x00200840),
        UINT32_C(0x00200800), UINT32_C(0x80000800)
};

static const uint32_t sbox7p[64] = {
        UINT32_C(0x04100010), UINT32_C(0x04104000),
        UINT32_C(0x00004010), UINT32_C(0x00000000),
        UINT32_C(0x04004000), UINT32_C(0x00100010),
        UINT32_C(0x04100000), UINT32_C(0x04104010),
        UINT32_C(0x00000010), UINT32_C(0x04000000),
        UINT32_C(0x00104000), UINT32_C(0x00004010),
        UINT32_C(0x00104010), UINT32_C(0x04004010),
        UINT32_C(0x04000010), UINT32_C(0x04100000),
        UINT32_C(0x00004000), UINT32_C(0x00104010),
        UINT32_C(0x00100010), UINT32_C(0x04004000),
        UINT32_C(0x04104010), UINT32_C(0x04000010),
        UINT32_C(0x00000000), UINT32_C(0x00104000),
        UINT32_C(0x04000000), UINT32_C(0x00100000),
        UINT32_C(0x04004010), UINT32_C(0x04100010),
        UINT32_C(0x00100000), UINT32_C(0x00004000),
        UINT32_C(0x04104000), UINT32_C(0x00000010),
        UINT32_C(0x00100000), UINT32_C(0x00004000),
        UINT32_C(0x04000010), UINT32_C(0x04104010),
        UINT32_C(0x00004010), UINT32_C(0x04000000),
        UINT32_C(0x00000000), UINT32_C(0x00104000),
        UINT32_C(0x04100010), UINT32_C(0x04004010),
        UINT32_C(0x04004000), UINT32_C(0x00100010),
        UINT32_C(0x04104000), UINT32_C(0x00000010),
        UINT32_C(0x00100010), UINT32_C(0x04004000),
        UINT32_C(0x04104010), UINT32_C(0x00100000),
        UINT32_C(0x04100000), UINT32_C(0x04000010),
        UINT32_C(0x00104000), UINT32_C(0x00004010),
        UINT32_C(0x04004010), UINT32_C(0x04100000),
        UINT32_C(0x00000010), UINT32_C(0x04104000),
        UINT32_C(0x00104010), UINT32_C(0x00000000),
        UINT32_C(0x04000000), UINT32_C(0x04100010),
        UINT32_C(0x00004000), UINT32_C(0x00104010)
};

__forceinline
uint32_t fRK(const uint32_t R, const uint64_t K)
{
        uint64_t x;

        /* Combined e-phase and 8x6bits to 8x8bits expansion.
         * 32 bits -> 48 bits permutation
         */
        x = e_phase((uint64_t) R) ^ K;

        /* Combined s-box and p-phase.
         *   s-box: 48 bits -> 32 bits
         *   p-phase: 32 bits -> 32 bites permutation
         */
        return ((LOOKUP32_SSE(sbox0p, ((x >> (8 * 0)) & 0x3f),
                              sizeof(sbox0p))) |
                (LOOKUP32_SSE(sbox1p, ((x >> (8 * 1)) & 0x3f),
                              sizeof(sbox1p))) |
                (LOOKUP32_SSE(sbox2p, ((x >> (8 * 2)) & 0x3f),
                              sizeof(sbox2p))) |
                (LOOKUP32_SSE(sbox3p, ((x >> (8 * 3)) & 0x3f),
                              sizeof(sbox3p))) |
                (LOOKUP32_SSE(sbox4p, ((x >> (8 * 4)) & 0x3f),
                              sizeof(sbox4p))) |
                (LOOKUP32_SSE(sbox5p, ((x >> (8 * 5)) & 0x3f),
                              sizeof(sbox5p))) |
                (LOOKUP32_SSE(sbox6p, ((x >> (8 * 6)) & 0x3f),
                              sizeof(sbox6p))) |
                (LOOKUP32_SSE(sbox7p, ((x >> (8 * 7)) & 0x3f),
                              sizeof(sbox7p))));
}

__forceinline
uint64_t enc_dec_1(const uint64_t data, const uint64_t *ks, const int enc)
{
        uint32_t l, r;
        uint64_t k[16];

        r = (uint32_t) (data);
        l = (uint32_t) (data >> 32);

        memcpy_fn_sse_128(k, ks);

        ip_z(&r, &l);

        if (enc) {
                l ^= fRK(r, k[0]);
                r ^= fRK(l, k[1]);
                l ^= fRK(r, k[2]);
                r ^= fRK(l, k[3]);
                l ^= fRK(r, k[4]);
                r ^= fRK(l, k[5]);
                l ^= fRK(r, k[6]);
                r ^= fRK(l, k[7]);
                l ^= fRK(r, k[8]);
                r ^= fRK(l, k[9]);
                l ^= fRK(r, k[10]);
                r ^= fRK(l, k[11]);
                l ^= fRK(r, k[12]);
                r ^= fRK(l, k[13]);
                l ^= fRK(r, k[14]);
                r ^= fRK(l, k[15]);
        } else {
                l ^= fRK(r, k[15]);     /* l: l0 -> r1/l2 */
                r ^= fRK(l, k[14]);     /* r: r0 -> r2 */
                l ^= fRK(r, k[13]);
                r ^= fRK(l, k[12]);
                l ^= fRK(r, k[11]);
                r ^= fRK(l, k[10]);
                l ^= fRK(r, k[9]);
                r ^= fRK(l, k[8]);
                l ^= fRK(r, k[7]);
                r ^= fRK(l, k[6]);
                l ^= fRK(r, k[5]);
                r ^= fRK(l, k[4]);
                l ^= fRK(r, k[3]);
                r ^= fRK(l, k[2]);
                l ^= fRK(r, k[1]);
                r ^= fRK(l, k[0]);
        }

        fp_z(&r, &l);

#ifdef SAFE_DATA
        clear_mem(k, sizeof(k));
#endif
        return ((uint64_t) l) | (((uint64_t) r) << 32);
}

IMB_DLL_LOCAL
void
des_enc_cbc_basic(const void *input, void *output, const int size,
                  const uint64_t *ks, const uint64_t *ivec)
{
#ifdef SAFE_PARAM
        if ((input == NULL) || (output == NULL) ||
            (ks == NULL) || (ivec == NULL) || (size < 0))
                return;
#endif
        const uint64_t *in = input;
        uint64_t *out = output;
        const int nblocks = size / 8;
        int n;
        uint64_t iv = *ivec;

        IMB_ASSERT(size >= 0);
        IMB_ASSERT(input != NULL);
        IMB_ASSERT(output != NULL);
        IMB_ASSERT(ks != NULL);
        IMB_ASSERT(ivec != NULL);

        for (n = 0; n < nblocks; n++)
                out[n] = iv = enc_dec_1(in[n] ^ iv, ks, 1 /* encrypt */);


#ifdef SAFE_DATA
        /* *ivec = iv; */
        clear_var(&iv, sizeof(iv));
#endif
}

IMB_DLL_LOCAL
void
des_dec_cbc_basic(const void *input, void *output, const int size,
                  const uint64_t *ks, const uint64_t *ivec)
{
#ifdef SAFE_PARAM
        if ((input == NULL) || (output == NULL) ||
            (ks == NULL) || (ivec == NULL) || (size < 0))
                return;
#endif
        const uint64_t *in = input;
        uint64_t *out = output;
        const int nblocks = size / 8;
        int n;
        uint64_t iv = *ivec;

        IMB_ASSERT(size >= 0);
        IMB_ASSERT(input != NULL);
        IMB_ASSERT(output != NULL);
        IMB_ASSERT(ks != NULL);
        IMB_ASSERT(ivec != NULL);

        for (n = 0; n < nblocks; n++) {
                uint64_t in_block = in[n];

                out[n] = enc_dec_1(in_block, ks, 0 /* decrypt */) ^ iv;
                iv = in_block;
        }

#ifdef SAFE_DATA
        /* *ivec = iv; */
        clear_var(&iv, sizeof(iv));
#endif
}

IMB_DLL_LOCAL
void
des3_enc_cbc_basic(const void *input, void *output, const int size,
                   const uint64_t *ks1, const uint64_t *ks2,
                   const uint64_t *ks3, const uint64_t *ivec)
{
#ifdef SAFE_PARAM
        if ((input == NULL) || (output == NULL) ||
            (ks1 == NULL) || (ks2 == NULL) || (ks3 == NULL) ||
            (ivec == NULL) || (size < 0))
                return;
#endif
        const uint64_t *in = input;
        uint64_t *out = output;
        const int nblocks = size / 8;
        int n;
        uint64_t iv = *ivec;

        IMB_ASSERT(size >= 0);
        IMB_ASSERT(input != NULL);
        IMB_ASSERT(output != NULL);
        IMB_ASSERT(ks1 != NULL);
        IMB_ASSERT(ks2 != NULL);
        IMB_ASSERT(ks3 != NULL);
        IMB_ASSERT(ivec != NULL);

        for (n = 0; n < nblocks; n++) {
                uint64_t t = in[n] ^ iv;

                t = enc_dec_1(t, ks1, 1 /* encrypt */);
                t = enc_dec_1(t, ks2, 0 /* decrypt */);
                t = enc_dec_1(t, ks3, 1 /* encrypt */);
                out[n] = iv = t;
        }

#ifdef SAFE_DATA
        /* *ivec = iv; */
        clear_var(&iv, sizeof(iv));
#endif
}

IMB_DLL_LOCAL
void
des3_dec_cbc_basic(const void *input, void *output, const int size,
                   const uint64_t *ks1, const uint64_t *ks2,
                   const uint64_t *ks3, const uint64_t *ivec)
{
#ifdef SAFE_PARAM
        if ((input == NULL) || (output == NULL) ||
            (ks1 == NULL) || (ks2 == NULL) || (ks3 == NULL) ||
            (ivec == NULL) || (size < 0))
                return;
#endif
        const uint64_t *in = input;
        uint64_t *out = output;
        const int nblocks = size / 8;
        int n;
        uint64_t iv = *ivec;

        IMB_ASSERT(size >= 0);
        IMB_ASSERT(input != NULL);
        IMB_ASSERT(output != NULL);
        IMB_ASSERT(ks1 != NULL);
        IMB_ASSERT(ks2 != NULL);
        IMB_ASSERT(ks3 != NULL);
        IMB_ASSERT(ivec != NULL);

        for (n = 0; n < nblocks; n++) {
                uint64_t t;
                const uint64_t next_iv = in[n];

                t = enc_dec_1(next_iv, ks3, 0 /* decrypt */);
                t = enc_dec_1(t, ks2, 1 /* encrypt */);
                t = enc_dec_1(t, ks1, 0 /* decrypt */);
                out[n] = t ^ iv;

                iv = next_iv;
        }

#ifdef SAFE_DATA
        /* *ivec = iv; */
        clear_var(&iv, sizeof(iv));
#endif
}

__forceinline
void
cfb_one_basic(const void *input, void *output, const int size,
              const uint64_t *ks, const uint64_t *ivec)
{
#ifdef SAFE_PARAM
        if ((input == NULL) || (output == NULL) ||
            (ks == NULL) || (ivec == NULL) || (size < 0))
                return;
#endif
        uint8_t *out = (uint8_t *) output;
        const uint8_t *in = (const uint8_t *) input;
        uint64_t t;

        IMB_ASSERT(size <= 8 && size >= 0);
        IMB_ASSERT(input != NULL);
        IMB_ASSERT(output != NULL);
        IMB_ASSERT(ks != NULL);
        IMB_ASSERT(ivec != NULL);

        t = enc_dec_1(*ivec, ks, 1 /* encrypt */);

        /* XOR and copy in one go */
        if (size & 1) {
                *out++ = *in++ ^ ((uint8_t) t);
                t >>= 8;
        }

        if (size & 2) {
                uint16_t *out2 = (uint16_t *) out;
                const uint16_t *in2 = (const uint16_t *) in;

                *out2 = *in2 ^ ((uint16_t) t);
                t >>= 16;
                out += 2;
                in += 2;
        }

        if (size & 4) {
                uint32_t *out4 = (uint32_t *) out;
                const uint32_t *in4 = (const uint32_t *) in;

                *out4 = *in4 ^ ((uint32_t) t);
        }

#ifdef SAFE_DATA
        clear_var(&t, sizeof(t));
#endif
}

IMB_DLL_LOCAL
void
docsis_des_enc_basic(const void *input, void *output, const int size,
                     const uint64_t *ks, const uint64_t *ivec)
{
#ifdef SAFE_PARAM
        if ((input == NULL) || (output == NULL) ||
            (ks == NULL) || (ivec == NULL) || (size < 0))
                return;
#endif
        const uint64_t *in = input;
        uint64_t *out = output;
        const int nblocks = size / IMB_DES_BLOCK_SIZE;
        const int partial = size & 7;
        int n;
        uint64_t iv = *ivec;

        IMB_ASSERT(size >= 0);
        IMB_ASSERT(input != NULL);
        IMB_ASSERT(output != NULL);
        IMB_ASSERT(ks != NULL);
        IMB_ASSERT(ivec != NULL);

        for (n = 0; n < nblocks; n++)
                out[n] = iv = enc_dec_1(in[n] ^ iv, ks, 1 /* encrypt */);

        if (partial) {
                if (nblocks)
                        cfb_one_basic(&in[nblocks], &out[nblocks], partial,
                                      ks, &out[nblocks - 1]);
                else
                        cfb_one_basic(input, output, partial, ks, ivec);
        }

#ifdef SAFE_DATA
        /* *ivec = iv; */
        clear_var(&iv, sizeof(iv));
#endif
}

IMB_DLL_LOCAL
void
docsis_des_dec_basic(const void *input, void *output, const int size,
                     const uint64_t *ks, const uint64_t *ivec)
{
#ifdef SAFE_PARAM
        if ((input == NULL) || (output == NULL) ||
            (ks == NULL) || (ivec == NULL) || (size < 0))
                return;
#endif
        const uint64_t *in = input;
        uint64_t *out = output;
        const int nblocks = size / IMB_DES_BLOCK_SIZE;
        const int partial = size & 7;
        int n;
        uint64_t iv = *ivec;

        IMB_ASSERT(size >= 0);
        IMB_ASSERT(input != NULL);
        IMB_ASSERT(output != NULL);
        IMB_ASSERT(ks != NULL);
        IMB_ASSERT(ivec != NULL);

        if (partial) {
                if (!nblocks) {
                        /* first block is the partial one */
                        cfb_one_basic(input, output, partial, ks, ivec);
                        iv = 0;
                        return;
                }
                /* last block is partial */
                cfb_one_basic(&in[nblocks], &out[nblocks], partial,
                              ks, &in[nblocks - 1]);
        }

        for (n = 0; n < nblocks; n++) {
                uint64_t in_block = in[n];

                out[n] = enc_dec_1(in_block, ks, 0 /* decrypt */) ^ iv;
                iv = in_block;
        }

#ifdef SAFE_DATA
        /* *ivec = iv; */
        clear_var(&iv, sizeof(iv));
#endif
}
