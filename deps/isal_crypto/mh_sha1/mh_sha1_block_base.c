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
// Base multi-hash SHA1 Functions
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
#define store_w(s, i, w, ww) (w[i][s] = bswap(ww[i*HASH_SEGS+s]))	// only used for step 0 ~ 15
#define update_w(s, i, w) (w[i&15][s] = rol32(w[(i-3)&15][s]^w[(i-8)&15][s]^w[(i-14)&15][s]^w[(i-16)&15][s], 1))	// used for step > 15
#define update_e_1(s, a, b, c, d, e, i, w)  (e[s] += rol32(a[s],5) + F1(b[s],c[s],d[s]) + K_00_19 + w[i&15][s])
#define update_e_2(s, a, b, c, d, e, i, w)  (e[s] += rol32(a[s],5) + F2(b[s],c[s],d[s]) + K_20_39 + w[i&15][s])
#define update_e_3(s, a, b, c, d, e, i, w)  (e[s] += rol32(a[s],5) + F3(b[s],c[s],d[s]) + K_40_59 + w[i&15][s])
#define update_e_4(s, a, b, c, d, e, i, w)  (e[s] += rol32(a[s],5) + F4(b[s],c[s],d[s]) + K_60_79 + w[i&15][s])
#define update_b(s, b)  (b[s] = rol32(b[s],30))

#define STORE_W(i, w, ww)			\
  store_w(0, i, w, ww);				\
  store_w(1, i, w, ww);				\
  store_w(2, i, w, ww);				\
  store_w(3, i, w, ww);				\
  store_w(4, i, w, ww);				\
  store_w(5, i, w, ww);				\
  store_w(6, i, w, ww);				\
  store_w(7, i, w, ww);				\
  store_w(8, i, w, ww);				\
  store_w(9, i, w, ww);				\
  store_w(10, i, w, ww);			\
  store_w(11, i, w, ww);			\
  store_w(12, i, w, ww);			\
  store_w(13, i, w, ww);			\
  store_w(14, i, w, ww);			\
  store_w(15, i, w, ww)

#define UPDATE_W(i, w)				\
  update_w(0, i, w);				\
  update_w(1, i, w);				\
  update_w(2, i, w);				\
  update_w(3, i, w);				\
  update_w(4, i, w);				\
  update_w(5, i, w);				\
  update_w(6, i, w);				\
  update_w(7, i, w);				\
  update_w(8, i, w);				\
  update_w(9, i, w);				\
  update_w(10, i, w);				\
  update_w(11, i, w);				\
  update_w(12, i, w);				\
  update_w(13, i, w);				\
  update_w(14, i, w);				\
  update_w(15, i, w)

#define UPDATE_E1(a, b, c, d, e, i, w)		\
  update_e_1(0, a, b, c, d, e, i, w);		\
  update_e_1(1, a, b, c, d, e, i, w);		\
  update_e_1(2, a, b, c, d, e, i, w);		\
  update_e_1(3, a, b, c, d, e, i, w);		\
  update_e_1(4, a, b, c, d, e, i, w);		\
  update_e_1(5, a, b, c, d, e, i, w);		\
  update_e_1(6, a, b, c, d, e, i, w);		\
  update_e_1(7, a, b, c, d, e, i, w);		\
  update_e_1(8, a, b, c, d, e, i, w);		\
  update_e_1(9, a, b, c, d, e, i, w);		\
  update_e_1(10, a, b, c, d, e, i, w);		\
  update_e_1(11, a, b, c, d, e, i, w);		\
  update_e_1(12, a, b, c, d, e, i, w);		\
  update_e_1(13, a, b, c, d, e, i, w);		\
  update_e_1(14, a, b, c, d, e, i, w);		\
  update_e_1(15, a, b, c, d, e, i, w)

#define UPDATE_E2(a, b, c, d, e, i, w)		\
  update_e_2(0, a, b, c, d, e, i, w);		\
  update_e_2(1, a, b, c, d, e, i, w);		\
  update_e_2(2, a, b, c, d, e, i, w);		\
  update_e_2(3, a, b, c, d, e, i, w);		\
  update_e_2(4, a, b, c, d, e, i, w);		\
  update_e_2(5, a, b, c, d, e, i, w);		\
  update_e_2(6, a, b, c, d, e, i, w);		\
  update_e_2(7, a, b, c, d, e, i, w);		\
  update_e_2(8, a, b, c, d, e, i, w);		\
  update_e_2(9, a, b, c, d, e, i, w);		\
  update_e_2(10, a, b, c, d, e, i, w);		\
  update_e_2(11, a, b, c, d, e, i, w);		\
  update_e_2(12, a, b, c, d, e, i, w);		\
  update_e_2(13, a, b, c, d, e, i, w);		\
  update_e_2(14, a, b, c, d, e, i, w);		\
  update_e_2(15, a, b, c, d, e, i, w)

#define UPDATE_E3(a, b, c, d, e, i, w)		\
  update_e_3(0, a, b, c, d, e, i, w);		\
  update_e_3(1, a, b, c, d, e, i, w);		\
  update_e_3(2, a, b, c, d, e, i, w);		\
  update_e_3(3, a, b, c, d, e, i, w);		\
  update_e_3(4, a, b, c, d, e, i, w);		\
  update_e_3(5, a, b, c, d, e, i, w);		\
  update_e_3(6, a, b, c, d, e, i, w);		\
  update_e_3(7, a, b, c, d, e, i, w);		\
  update_e_3(8, a, b, c, d, e, i, w);		\
  update_e_3(9, a, b, c, d, e, i, w);		\
  update_e_3(10, a, b, c, d, e, i, w);		\
  update_e_3(11, a, b, c, d, e, i, w);		\
  update_e_3(12, a, b, c, d, e, i, w);		\
  update_e_3(13, a, b, c, d, e, i, w);		\
  update_e_3(14, a, b, c, d, e, i, w);		\
  update_e_3(15, a, b, c, d, e, i, w)

#define UPDATE_E4(a, b, c, d, e, i, w)		\
  update_e_4(0, a, b, c, d, e, i, w);		\
  update_e_4(1, a, b, c, d, e, i, w);		\
  update_e_4(2, a, b, c, d, e, i, w);		\
  update_e_4(3, a, b, c, d, e, i, w);		\
  update_e_4(4, a, b, c, d, e, i, w);		\
  update_e_4(5, a, b, c, d, e, i, w);		\
  update_e_4(6, a, b, c, d, e, i, w);		\
  update_e_4(7, a, b, c, d, e, i, w);		\
  update_e_4(8, a, b, c, d, e, i, w);		\
  update_e_4(9, a, b, c, d, e, i, w);		\
  update_e_4(10, a, b, c, d, e, i, w);		\
  update_e_4(11, a, b, c, d, e, i, w);		\
  update_e_4(12, a, b, c, d, e, i, w);		\
  update_e_4(13, a, b, c, d, e, i, w);		\
  update_e_4(14, a, b, c, d, e, i, w);		\
  update_e_4(15, a, b, c, d, e, i, w)

#define UPDATE_B(b)				\
  update_b(0, b);				\
  update_b(1, b);				\
  update_b(2, b);				\
  update_b(3, b);				\
  update_b(4, b);				\
  update_b(5, b);				\
  update_b(6, b);				\
  update_b(7, b);				\
  update_b(8, b);				\
  update_b(9, b);				\
  update_b(10, b);				\
  update_b(11, b);				\
  update_b(12, b);				\
  update_b(13, b);				\
  update_b(14, b);				\
  update_b(15, b)

static inline void step00_15(int i, uint32_t * a, uint32_t * b, uint32_t * c,
			     uint32_t * d, uint32_t * e, uint32_t(*w)[HASH_SEGS],
			     uint32_t * ww)
{
	STORE_W(i, w, ww);
	UPDATE_E1(a, b, c, d, e, i, w);
	UPDATE_B(b);
}

static inline void step16_19(int i, uint32_t * a, uint32_t * b, uint32_t * c,
			     uint32_t * d, uint32_t * e, uint32_t(*w)[HASH_SEGS])
{
	UPDATE_W(i, w);
	UPDATE_E1(a, b, c, d, e, i, w);
	UPDATE_B(b);

}

static inline void step20_39(int i, uint32_t * a, uint32_t * b, uint32_t * c,
			     uint32_t * d, uint32_t * e, uint32_t(*w)[HASH_SEGS])
{
	UPDATE_W(i, w);
	UPDATE_E2(a, b, c, d, e, i, w);
	UPDATE_B(b);
}

static inline void step40_59(int i, uint32_t * a, uint32_t * b, uint32_t * c,
			     uint32_t * d, uint32_t * e, uint32_t(*w)[HASH_SEGS])
{
	UPDATE_W(i, w);
	UPDATE_E3(a, b, c, d, e, i, w);
	UPDATE_B(b);
}

static inline void step60_79(int i, uint32_t * a, uint32_t * b, uint32_t * c,
			     uint32_t * d, uint32_t * e, uint32_t(*w)[HASH_SEGS])
{
	UPDATE_W(i, w);
	UPDATE_E4(a, b, c, d, e, i, w);
	UPDATE_B(b);
}

static inline void init_abcde(uint32_t * xx, uint32_t n,
			      uint32_t digests[SHA1_DIGEST_WORDS][HASH_SEGS])
{
	xx[0] = digests[n][0];
	xx[1] = digests[n][1];
	xx[2] = digests[n][2];
	xx[3] = digests[n][3];
	xx[4] = digests[n][4];
	xx[5] = digests[n][5];
	xx[6] = digests[n][6];
	xx[7] = digests[n][7];
	xx[8] = digests[n][8];
	xx[9] = digests[n][9];
	xx[10] = digests[n][10];
	xx[11] = digests[n][11];
	xx[12] = digests[n][12];
	xx[13] = digests[n][13];
	xx[14] = digests[n][14];
	xx[15] = digests[n][15];
}

static inline void add_abcde(uint32_t * xx, uint32_t n,
			     uint32_t digests[SHA1_DIGEST_WORDS][HASH_SEGS])
{
	digests[n][0] += xx[0];
	digests[n][1] += xx[1];
	digests[n][2] += xx[2];
	digests[n][3] += xx[3];
	digests[n][4] += xx[4];
	digests[n][5] += xx[5];
	digests[n][6] += xx[6];
	digests[n][7] += xx[7];
	digests[n][8] += xx[8];
	digests[n][9] += xx[9];
	digests[n][10] += xx[10];
	digests[n][11] += xx[11];
	digests[n][12] += xx[12];
	digests[n][13] += xx[13];
	digests[n][14] += xx[14];
	digests[n][15] += xx[15];
}

/*
 * API to perform 0-79 steps of the multi-hash algorithm for
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
void mh_sha1_single(const uint8_t * input, uint32_t(*digests)[HASH_SEGS],
		    uint8_t * frame_buffer)
{
	uint32_t aa[HASH_SEGS], bb[HASH_SEGS], cc[HASH_SEGS], dd[HASH_SEGS], ee[HASH_SEGS];
	uint32_t *ww = (uint32_t *) input;
	uint32_t(*w)[HASH_SEGS];

	w = (uint32_t(*)[HASH_SEGS]) frame_buffer;

	init_abcde(aa, 0, digests);
	init_abcde(bb, 1, digests);
	init_abcde(cc, 2, digests);
	init_abcde(dd, 3, digests);
	init_abcde(ee, 4, digests);

	step00_15(0, aa, bb, cc, dd, ee, w, ww);
	step00_15(1, ee, aa, bb, cc, dd, w, ww);
	step00_15(2, dd, ee, aa, bb, cc, w, ww);
	step00_15(3, cc, dd, ee, aa, bb, w, ww);
	step00_15(4, bb, cc, dd, ee, aa, w, ww);
	step00_15(5, aa, bb, cc, dd, ee, w, ww);
	step00_15(6, ee, aa, bb, cc, dd, w, ww);
	step00_15(7, dd, ee, aa, bb, cc, w, ww);
	step00_15(8, cc, dd, ee, aa, bb, w, ww);
	step00_15(9, bb, cc, dd, ee, aa, w, ww);
	step00_15(10, aa, bb, cc, dd, ee, w, ww);
	step00_15(11, ee, aa, bb, cc, dd, w, ww);
	step00_15(12, dd, ee, aa, bb, cc, w, ww);
	step00_15(13, cc, dd, ee, aa, bb, w, ww);
	step00_15(14, bb, cc, dd, ee, aa, w, ww);
	step00_15(15, aa, bb, cc, dd, ee, w, ww);

	step16_19(16, ee, aa, bb, cc, dd, w);
	step16_19(17, dd, ee, aa, bb, cc, w);
	step16_19(18, cc, dd, ee, aa, bb, w);
	step16_19(19, bb, cc, dd, ee, aa, w);

	step20_39(20, aa, bb, cc, dd, ee, w);
	step20_39(21, ee, aa, bb, cc, dd, w);
	step20_39(22, dd, ee, aa, bb, cc, w);
	step20_39(23, cc, dd, ee, aa, bb, w);
	step20_39(24, bb, cc, dd, ee, aa, w);
	step20_39(25, aa, bb, cc, dd, ee, w);
	step20_39(26, ee, aa, bb, cc, dd, w);
	step20_39(27, dd, ee, aa, bb, cc, w);
	step20_39(28, cc, dd, ee, aa, bb, w);
	step20_39(29, bb, cc, dd, ee, aa, w);
	step20_39(30, aa, bb, cc, dd, ee, w);
	step20_39(31, ee, aa, bb, cc, dd, w);
	step20_39(32, dd, ee, aa, bb, cc, w);
	step20_39(33, cc, dd, ee, aa, bb, w);
	step20_39(34, bb, cc, dd, ee, aa, w);
	step20_39(35, aa, bb, cc, dd, ee, w);
	step20_39(36, ee, aa, bb, cc, dd, w);
	step20_39(37, dd, ee, aa, bb, cc, w);
	step20_39(38, cc, dd, ee, aa, bb, w);
	step20_39(39, bb, cc, dd, ee, aa, w);

	step40_59(40, aa, bb, cc, dd, ee, w);
	step40_59(41, ee, aa, bb, cc, dd, w);
	step40_59(42, dd, ee, aa, bb, cc, w);
	step40_59(43, cc, dd, ee, aa, bb, w);
	step40_59(44, bb, cc, dd, ee, aa, w);
	step40_59(45, aa, bb, cc, dd, ee, w);
	step40_59(46, ee, aa, bb, cc, dd, w);
	step40_59(47, dd, ee, aa, bb, cc, w);
	step40_59(48, cc, dd, ee, aa, bb, w);
	step40_59(49, bb, cc, dd, ee, aa, w);
	step40_59(50, aa, bb, cc, dd, ee, w);
	step40_59(51, ee, aa, bb, cc, dd, w);
	step40_59(52, dd, ee, aa, bb, cc, w);
	step40_59(53, cc, dd, ee, aa, bb, w);
	step40_59(54, bb, cc, dd, ee, aa, w);
	step40_59(55, aa, bb, cc, dd, ee, w);
	step40_59(56, ee, aa, bb, cc, dd, w);
	step40_59(57, dd, ee, aa, bb, cc, w);
	step40_59(58, cc, dd, ee, aa, bb, w);
	step40_59(59, bb, cc, dd, ee, aa, w);

	step60_79(60, aa, bb, cc, dd, ee, w);
	step60_79(61, ee, aa, bb, cc, dd, w);
	step60_79(62, dd, ee, aa, bb, cc, w);
	step60_79(63, cc, dd, ee, aa, bb, w);
	step60_79(64, bb, cc, dd, ee, aa, w);
	step60_79(65, aa, bb, cc, dd, ee, w);
	step60_79(66, ee, aa, bb, cc, dd, w);
	step60_79(67, dd, ee, aa, bb, cc, w);
	step60_79(68, cc, dd, ee, aa, bb, w);
	step60_79(69, bb, cc, dd, ee, aa, w);
	step60_79(70, aa, bb, cc, dd, ee, w);
	step60_79(71, ee, aa, bb, cc, dd, w);
	step60_79(72, dd, ee, aa, bb, cc, w);
	step60_79(73, cc, dd, ee, aa, bb, w);
	step60_79(74, bb, cc, dd, ee, aa, w);
	step60_79(75, aa, bb, cc, dd, ee, w);
	step60_79(76, ee, aa, bb, cc, dd, w);
	step60_79(77, dd, ee, aa, bb, cc, w);
	step60_79(78, cc, dd, ee, aa, bb, w);
	step60_79(79, bb, cc, dd, ee, aa, w);

	add_abcde(aa, 0, digests);
	add_abcde(bb, 1, digests);
	add_abcde(cc, 2, digests);
	add_abcde(dd, 3, digests);
	add_abcde(ee, 4, digests);
}

void mh_sha1_block_base(const uint8_t * input_data,
			uint32_t digests[SHA1_DIGEST_WORDS][HASH_SEGS],
			uint8_t frame_buffer[MH_SHA1_BLOCK_SIZE], uint32_t num_blocks)
{
	uint32_t i;

	for (i = 0; i < num_blocks; i++) {
		mh_sha1_single(input_data, digests, frame_buffer);
		input_data += MH_SHA1_BLOCK_SIZE;
	}

	return;
}
