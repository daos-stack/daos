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
snow3g_f8_iv_gen(const uint32_t count, const uint8_t bearer,
                 const uint8_t dir, void *iv_ptr)
{
        uint32_t *iv32 = (uint32_t *) iv_ptr;

        if (iv_ptr == NULL)
                return -1;

        /* Bearer must contain 5 bits only */
        if (bearer >= (1<<5))
                return -1;

        /* Direction must contain 1 bit only */
        if (dir > 1)
                return -1;
        /**
         * Parameters are passed in Little Endian format
         * and reversed to generate the IV in Big Endian format
         */
        /* IV[3] = BEARER || DIRECTION || 0s */
        iv32[3] = bswap4((bearer << 27) | (dir << 26));

        /* IV[2] = COUNT */
        iv32[2] = bswap4(count);

        /* IV[1] = BEARER || DIRECTION || 0s */
        iv32[1] = iv32[3];

        /* IV[0] = COUNT */
        iv32[0] = iv32[2];

        return 0;
}

int
snow3g_f9_iv_gen(const uint32_t count, const uint32_t fresh,
                 const uint8_t dir, void *iv_ptr)
{
        uint32_t *iv32 = (uint32_t *) iv_ptr;

        if (iv_ptr == NULL)
                return -1;

        /* Direction must contain 1 bit only */
        if (dir > 1)
                return -1;
        /**
         * Parameters are passed in Little Endian format
         * and reversed to generate the IV in Big Endian format
         */
        /* IV[3] = FRESH ^ (DIRECTION[0] << 17) */
        iv32[3] = bswap4(fresh ^ (dir << 15));

        /* IV[2] = DIRECTION[0] ^ COUNT[0-31] */
        iv32[2] = bswap4(count ^ (dir << 31));

        /* IV[1] = FRESH */
        iv32[1] = bswap4(fresh);

        /* IV[0] = COUNT */
        iv32[0] = bswap4(count);

        return 0;
}
