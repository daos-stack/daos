/**********************************************************************
  Copyright(c) 2011-2017 Intel Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************/

#include "crc.h"
#include "crc64.h"
#include <stdint.h>

unsigned int crc32_iscsi(unsigned char *buffer, int len, unsigned int crc_init)
{
	return crc32_iscsi_base(buffer, len, crc_init);
}

uint16_t crc16_t10dif(uint16_t seed, const unsigned char *buf, uint64_t len)
{
	return crc16_t10dif_base(seed, (uint8_t *) buf, len);
}

uint16_t crc16_t10dif_copy(uint16_t seed, uint8_t * dst, uint8_t * src, uint64_t len)
{
	return crc16_t10dif_copy_base(seed, dst, src, len);
}

uint32_t crc32_ieee(uint32_t seed, const unsigned char *buf, uint64_t len)
{
	return crc32_ieee_base(seed, (uint8_t *) buf, len);
}

uint32_t crc32_gzip_refl(uint32_t seed, const unsigned char *buf, uint64_t len)
{
	return crc32_gzip_refl_base(seed, (uint8_t *) buf, len);
}

uint64_t crc64_ecma_refl(uint64_t seed, const uint8_t * buf, uint64_t len)
{
	return crc64_ecma_refl_base(seed, buf, len);
}

uint64_t crc64_ecma_norm(uint64_t seed, const uint8_t * buf, uint64_t len)
{
	return crc64_ecma_norm_base(seed, buf, len);
}

uint64_t crc64_iso_refl(uint64_t seed, const uint8_t * buf, uint64_t len)
{
	return crc64_iso_refl_base(seed, buf, len);
}

uint64_t crc64_iso_norm(uint64_t seed, const uint8_t * buf, uint64_t len)
{
	return crc64_iso_norm_base(seed, buf, len);
}

uint64_t crc64_jones_refl(uint64_t seed, const uint8_t * buf, uint64_t len)
{
	return crc64_jones_refl_base(seed, buf, len);
}

uint64_t crc64_jones_norm(uint64_t seed, const uint8_t * buf, uint64_t len)
{
	return crc64_jones_norm_base(seed, buf, len);
}
