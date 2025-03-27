/**********************************************************************
  Copyright(c) 2011-2016 Intel Corporation All rights reserved.

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

#include "mh_sha1_internal.h"
#include <string.h>

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// Reference SHA1 Functions for mh_sha1
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

#define W(x) w[(x) & 15]

#define step00_19(i,a,b,c,d,e) \
	if (i>15) W(i) = rol32(W(i-3)^W(i-8)^W(i-14)^W(i-16), 1); \
	else W(i) = bswap(ww[i]); \
	e += rol32(a,5) + F1(b,c,d) + 0x5A827999 + W(i); \
	b = rol32(b,30)

#define step20_39(i,a,b,c,d,e) \
	W(i) = rol32(W(i-3)^W(i-8)^W(i-14)^W(i-16), 1); \
	e += rol32(a,5) + F2(b,c,d) + 0x6ED9EBA1 + W(i); \
	b = rol32(b,30)

#define step40_59(i,a,b,c,d,e) \
	W(i) = rol32(W(i-3)^W(i-8)^W(i-14)^W(i-16), 1); \
	e += rol32(a,5) + F3(b,c,d) + 0x8F1BBCDC + W(i); \
	b = rol32(b,30)

#define step60_79(i,a,b,c,d,e) \
	W(i) = rol32(W(i-3)^W(i-8)^W(i-14)^W(i-16), 1); \
	e += rol32(a,5) + F4(b,c,d) + 0xCA62C1D6 + W(i); \
	b = rol32(b,30)

void sha1_single_for_mh_sha1(const uint8_t * data, uint32_t digest[])
{
	uint32_t a, b, c, d, e;
	uint32_t w[16] = { 0 };
	uint32_t *ww = (uint32_t *) data;

	a = digest[0];
	b = digest[1];
	c = digest[2];
	d = digest[3];
	e = digest[4];

	step00_19(0, a, b, c, d, e);
	step00_19(1, e, a, b, c, d);
	step00_19(2, d, e, a, b, c);
	step00_19(3, c, d, e, a, b);
	step00_19(4, b, c, d, e, a);
	step00_19(5, a, b, c, d, e);
	step00_19(6, e, a, b, c, d);
	step00_19(7, d, e, a, b, c);
	step00_19(8, c, d, e, a, b);
	step00_19(9, b, c, d, e, a);
	step00_19(10, a, b, c, d, e);
	step00_19(11, e, a, b, c, d);
	step00_19(12, d, e, a, b, c);
	step00_19(13, c, d, e, a, b);
	step00_19(14, b, c, d, e, a);
	step00_19(15, a, b, c, d, e);
	step00_19(16, e, a, b, c, d);
	step00_19(17, d, e, a, b, c);
	step00_19(18, c, d, e, a, b);
	step00_19(19, b, c, d, e, a);

	step20_39(20, a, b, c, d, e);
	step20_39(21, e, a, b, c, d);
	step20_39(22, d, e, a, b, c);
	step20_39(23, c, d, e, a, b);
	step20_39(24, b, c, d, e, a);
	step20_39(25, a, b, c, d, e);
	step20_39(26, e, a, b, c, d);
	step20_39(27, d, e, a, b, c);
	step20_39(28, c, d, e, a, b);
	step20_39(29, b, c, d, e, a);
	step20_39(30, a, b, c, d, e);
	step20_39(31, e, a, b, c, d);
	step20_39(32, d, e, a, b, c);
	step20_39(33, c, d, e, a, b);
	step20_39(34, b, c, d, e, a);
	step20_39(35, a, b, c, d, e);
	step20_39(36, e, a, b, c, d);
	step20_39(37, d, e, a, b, c);
	step20_39(38, c, d, e, a, b);
	step20_39(39, b, c, d, e, a);

	step40_59(40, a, b, c, d, e);
	step40_59(41, e, a, b, c, d);
	step40_59(42, d, e, a, b, c);
	step40_59(43, c, d, e, a, b);
	step40_59(44, b, c, d, e, a);
	step40_59(45, a, b, c, d, e);
	step40_59(46, e, a, b, c, d);
	step40_59(47, d, e, a, b, c);
	step40_59(48, c, d, e, a, b);
	step40_59(49, b, c, d, e, a);
	step40_59(50, a, b, c, d, e);
	step40_59(51, e, a, b, c, d);
	step40_59(52, d, e, a, b, c);
	step40_59(53, c, d, e, a, b);
	step40_59(54, b, c, d, e, a);
	step40_59(55, a, b, c, d, e);
	step40_59(56, e, a, b, c, d);
	step40_59(57, d, e, a, b, c);
	step40_59(58, c, d, e, a, b);
	step40_59(59, b, c, d, e, a);

	step60_79(60, a, b, c, d, e);
	step60_79(61, e, a, b, c, d);
	step60_79(62, d, e, a, b, c);
	step60_79(63, c, d, e, a, b);
	step60_79(64, b, c, d, e, a);
	step60_79(65, a, b, c, d, e);
	step60_79(66, e, a, b, c, d);
	step60_79(67, d, e, a, b, c);
	step60_79(68, c, d, e, a, b);
	step60_79(69, b, c, d, e, a);
	step60_79(70, a, b, c, d, e);
	step60_79(71, e, a, b, c, d);
	step60_79(72, d, e, a, b, c);
	step60_79(73, c, d, e, a, b);
	step60_79(74, b, c, d, e, a);
	step60_79(75, a, b, c, d, e);
	step60_79(76, e, a, b, c, d);
	step60_79(77, d, e, a, b, c);
	step60_79(78, c, d, e, a, b);
	step60_79(79, b, c, d, e, a);

	digest[0] += a;
	digest[1] += b;
	digest[2] += c;
	digest[3] += d;
	digest[4] += e;
}

void sha1_for_mh_sha1(const uint8_t * input_data, uint32_t * digest, const uint32_t len)
{
	uint32_t i, j;
	uint8_t buf[2 * SHA1_BLOCK_SIZE];
	union {
		uint64_t uint;
		uint8_t uchar[8];
	} convert;
	uint8_t *p;

	digest[0] = MH_SHA1_H0;
	digest[1] = MH_SHA1_H1;
	digest[2] = MH_SHA1_H2;
	digest[3] = MH_SHA1_H3;
	digest[4] = MH_SHA1_H4;

	i = len;
	while (i >= SHA1_BLOCK_SIZE) {
		sha1_single_for_mh_sha1(input_data, digest);
		input_data += SHA1_BLOCK_SIZE;
		i -= SHA1_BLOCK_SIZE;
	}

	memcpy(buf, input_data, i);
	buf[i++] = 0x80;
	for (j = i; j < ((2 * SHA1_BLOCK_SIZE) - 8); j++)
		buf[j] = 0;

	if (i > SHA1_BLOCK_SIZE - 8)
		i = 2 * SHA1_BLOCK_SIZE;
	else
		i = SHA1_BLOCK_SIZE;

	convert.uint = 8 * len;
	p = buf + i - 8;
	p[0] = convert.uchar[7];
	p[1] = convert.uchar[6];
	p[2] = convert.uchar[5];
	p[3] = convert.uchar[4];
	p[4] = convert.uchar[3];
	p[5] = convert.uchar[2];
	p[6] = convert.uchar[1];
	p[7] = convert.uchar[0];

	sha1_single_for_mh_sha1(buf, digest);
	if (i == (2 * SHA1_BLOCK_SIZE))
		sha1_single_for_mh_sha1(buf + SHA1_BLOCK_SIZE, digest);
}
