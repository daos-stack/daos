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

#include <string.h>

#include "intel-ipsec-mb.h"
#include "include/wireless_common.h"

int
zuc_eea3_iv_gen(const uint32_t count, const uint8_t bearer,
                const uint8_t dir, void *iv_ptr)
{
        uint8_t *iv = (uint8_t *) iv_ptr;
        uint32_t *iv32 = (uint32_t *) iv_ptr;

        if (iv == NULL)
                return -1;

        /* Bearer must contain 5 bits only */
        if (bearer >= (1<<5))
                return -1;

        /* Direction must contain 1 bit only */
        if (dir > 1)
                return -1;

        /* IV[0-3] = COUNT */
        iv32[0] = bswap4(count);

        /* IV[4] = BEARER || DIRECTION || 0s */
        iv[4] = (bearer << 3) + (dir << 2);

        /* IV[5-7] = Os */
        memset(&iv[5], 0, 3);

        /* IV[8-15] = IV[0-7] */
        memcpy(&iv[8], &iv[0], 8);

        return 0;
}

int
zuc_eia3_iv_gen(const uint32_t count, const uint8_t bearer,
                const uint8_t dir, void *iv_ptr)
{
        uint8_t *iv = (uint8_t *) iv_ptr;
        uint32_t *iv32 = (uint32_t *) iv_ptr;

        if (iv == NULL)
                return -1;

        /* Bearer must contain 5 bits only */
        if (bearer >= (1<<5))
                return -1;

        /* Direction must contain 1 bit only */
        if (dir > 1)
                return -1;

        /* IV[0-3] = COUNT */
        iv32[0] = bswap4(count);

        /* IV[4] = BEARER || 0s */
        iv[4] = bearer << 3;

        /* IV[5-7] = Os */
        memset(&iv[5], 0, 3);

        /* IV[8-15] = IV[0-7] */
        memcpy(&iv[8], &iv[0], 8);

        /* IV[8] = IV[0] XOR (DIR << 7) */
        iv[8] ^= (dir << 7);

        /* IV[14] = IV[6] XOR (DIR << 7) */
        iv[14] ^= (dir << 7);

        return 0;
}
