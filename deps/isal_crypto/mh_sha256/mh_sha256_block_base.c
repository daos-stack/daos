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

#include "mh_sha256_internal.h"
#include <string.h>

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// Base multi-hash SHA256 Functions
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// store_w is only used for step 0 ~ 15
#define store_w(s, i, w, ww) (w[i][s] = bswap(ww[i*HASH_SEGS+s]))
#define Ws(x, s) w[(x) & 15][s]
// update_w is used for step > 15
#define update_w(s, i, w) \
	Ws(i, s) = Ws(i-16, s) + S0(Ws(i-15, s)) + Ws(i-7, s) + S1(Ws(i-2, s))
#define update_t2(s, a, b, c) t2[s] = s0(a[s]) + maj(a[s],b[s],c[s])
#define update_t1(s, h, e, f, g, i, k) \
	t1[s] = h[s] + s1(e[s]) + ch(e[s],f[s],g[s]) + k + Ws(i, s);
#define update_d(s) d[s] += t1[s]
#define update_h(s) h[s] = t1[s] + t2[s]

// s is a iterator
#define STORE_W(s, i, w, ww) \
	for(s = 0; s < HASH_SEGS; s++) \
		store_w(s, i, w, ww);
#define UPDATE_W(s, i, w) \
	for(s = 0; s < HASH_SEGS; s++) \
		update_w(s, i, w);
#define UPDATE_T2(s, a, b, c) \
	for(s = 0; s < HASH_SEGS; s++) \
		update_t2(s, a, b, c);
#define UPDATE_T1(s, h, e, f, g, i, k) \
	for(s = 0; s < HASH_SEGS; s++) \
		update_t1(s, h, e, f, g, i, k);
#define UPDATE_D(s) \
	for(s = 0; s < HASH_SEGS; s++) \
		update_d(s);
#define UPDATE_H(s) \
	for(s = 0; s < HASH_SEGS; s++) \
		update_h(s);

static inline void step(int i, uint32_t * a, uint32_t * b, uint32_t * c,
			uint32_t * d, uint32_t * e, uint32_t * f,
			uint32_t * g, uint32_t * h, uint32_t k,
			uint32_t * t1, uint32_t * t2, uint32_t(*w)[HASH_SEGS], uint32_t * ww)
{
	uint8_t s;
	if (i < 16) {
		STORE_W(s, i, w, ww);
	} else {
		UPDATE_W(s, i, w);
	}
	UPDATE_T2(s, a, b, c);
	UPDATE_T1(s, h, e, f, g, i, k);
	UPDATE_D(s);
	UPDATE_H(s);
}

static inline void init_abcdefgh(uint32_t * xx, uint32_t n,
				 uint32_t digests[SHA256_DIGEST_WORDS][HASH_SEGS])
{
	uint8_t s;
	for (s = 0; s < HASH_SEGS; s++)
		xx[s] = digests[n][s];
}

static inline void add_abcdefgh(uint32_t * xx, uint32_t n,
				uint32_t digests[SHA256_DIGEST_WORDS][HASH_SEGS])
{
	uint8_t s;
	for (s = 0; s < HASH_SEGS; s++)
		digests[n][s] += xx[s];
}

/*
 * API to perform 0-64 steps of the multi-hash algorithm for
 * a single block of data. The caller is responsible for ensuring
 * a full block of data input.
 *
 * Argument:
 *   input  - the pointer to the data
 *   digest - the space to hold the digests for all segments.
 *
 * Return:
 *   N/A
 */
void mh_sha256_single(const uint8_t * input, uint32_t(*digests)[HASH_SEGS],
		      uint8_t * frame_buffer)
{
	uint8_t i;
	uint32_t aa[HASH_SEGS], bb[HASH_SEGS], cc[HASH_SEGS], dd[HASH_SEGS];
	uint32_t ee[HASH_SEGS], ff[HASH_SEGS], gg[HASH_SEGS], hh[HASH_SEGS];
	uint32_t t1[HASH_SEGS], t2[HASH_SEGS];
	uint32_t *ww = (uint32_t *) input;
	uint32_t(*w)[HASH_SEGS];

	const static uint32_t k[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
		0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
		0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};

	w = (uint32_t(*)[HASH_SEGS]) frame_buffer;

	init_abcdefgh(aa, 0, digests);
	init_abcdefgh(bb, 1, digests);
	init_abcdefgh(cc, 2, digests);
	init_abcdefgh(dd, 3, digests);
	init_abcdefgh(ee, 4, digests);
	init_abcdefgh(ff, 5, digests);
	init_abcdefgh(gg, 6, digests);
	init_abcdefgh(hh, 7, digests);

	for (i = 0; i < 64; i += 8) {
		step(i, aa, bb, cc, dd, ee, ff, gg, hh, k[i], t1, t2, w, ww);
		step(i + 1, hh, aa, bb, cc, dd, ee, ff, gg, k[i + 1], t1, t2, w, ww);
		step(i + 2, gg, hh, aa, bb, cc, dd, ee, ff, k[i + 2], t1, t2, w, ww);
		step(i + 3, ff, gg, hh, aa, bb, cc, dd, ee, k[i + 3], t1, t2, w, ww);
		step(i + 4, ee, ff, gg, hh, aa, bb, cc, dd, k[i + 4], t1, t2, w, ww);
		step(i + 5, dd, ee, ff, gg, hh, aa, bb, cc, k[i + 5], t1, t2, w, ww);
		step(i + 6, cc, dd, ee, ff, gg, hh, aa, bb, k[i + 6], t1, t2, w, ww);
		step(i + 7, bb, cc, dd, ee, ff, gg, hh, aa, k[i + 7], t1, t2, w, ww);
	}

	add_abcdefgh(aa, 0, digests);
	add_abcdefgh(bb, 1, digests);
	add_abcdefgh(cc, 2, digests);
	add_abcdefgh(dd, 3, digests);
	add_abcdefgh(ee, 4, digests);
	add_abcdefgh(ff, 5, digests);
	add_abcdefgh(gg, 6, digests);
	add_abcdefgh(hh, 7, digests);
}

void mh_sha256_block_base(const uint8_t * input_data,
			  uint32_t digests[SHA256_DIGEST_WORDS][HASH_SEGS],
			  uint8_t frame_buffer[MH_SHA256_BLOCK_SIZE], uint32_t num_blocks)
{
	uint32_t i;

	for (i = 0; i < num_blocks; i++) {
		mh_sha256_single(input_data, digests, frame_buffer);
		input_data += MH_SHA256_BLOCK_SIZE;
	}

	return;
}
