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

#include <string.h>
#include "mh_sha1_internal.h"

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
 //  Macros and sub-functions which already exist in source code file
 //  (sha1_for_mh_sha1.c) is part of ISA-L library as internal functions.
 //  The reason why writing them twice is the linking issue caused by
 //  mh_sha1_ref(). mh_sha1_ref() needs these macros and sub-functions
 //  without linking ISA-L library. So mh_sha1_ref() includes them in
 //  order to contain essential sub-functions in its own object file.
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

void sha1_single_for_mh_sha1_ref(const uint8_t * data, uint32_t digest[])
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

void sha1_for_mh_sha1_ref(const uint8_t * input_data, uint32_t * digest, const uint32_t len)
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
		sha1_single_for_mh_sha1_ref(input_data, digest);
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

	sha1_single_for_mh_sha1_ref(buf, digest);
	if (i == (2 * SHA1_BLOCK_SIZE))
		sha1_single_for_mh_sha1_ref(buf + SHA1_BLOCK_SIZE, digest);
}

/*
 * buffer to rearrange one segment data from one block.
 *
 * Layout of new_data:
 *  segment
 *  -------------------------
 *   w0  |  w1  | ... |  w15
 *
 */
static inline void transform_input_single(uint32_t * new_data, uint32_t * input,
					  uint32_t segment)
{
	new_data[16 * segment + 0] = input[16 * 0 + segment];
	new_data[16 * segment + 1] = input[16 * 1 + segment];
	new_data[16 * segment + 2] = input[16 * 2 + segment];
	new_data[16 * segment + 3] = input[16 * 3 + segment];
	new_data[16 * segment + 4] = input[16 * 4 + segment];
	new_data[16 * segment + 5] = input[16 * 5 + segment];
	new_data[16 * segment + 6] = input[16 * 6 + segment];
	new_data[16 * segment + 7] = input[16 * 7 + segment];
	new_data[16 * segment + 8] = input[16 * 8 + segment];
	new_data[16 * segment + 9] = input[16 * 9 + segment];
	new_data[16 * segment + 10] = input[16 * 10 + segment];
	new_data[16 * segment + 11] = input[16 * 11 + segment];
	new_data[16 * segment + 12] = input[16 * 12 + segment];
	new_data[16 * segment + 13] = input[16 * 13 + segment];
	new_data[16 * segment + 14] = input[16 * 14 + segment];
	new_data[16 * segment + 15] = input[16 * 15 + segment];
}

// Adapt parameters to sha1_single_for_mh_sha1_ref
#define sha1_update_one_seg(data, digest) \
	sha1_single_for_mh_sha1_ref((const uint8_t *)(data), (uint32_t *)(digest))

/*
 * buffer to Rearrange all segments data from one block.
 *
 * Layout of new_data:
 *  segment
 *  -------------------------
 *   seg0:   | w0  |  w1  | ... |  w15
 *   seg1:   | w0  |  w1  | ... |  w15
 *   seg2:   | w0  |  w1  | ... |  w15
 *   ....
 *   seg15: | w0  |  w1  | ... |  w15
 *
 */
static inline void transform_input(uint32_t * new_data, uint32_t * input, uint32_t block)
{
	uint32_t *current_input = input + block * MH_SHA1_BLOCK_SIZE / 4;

	transform_input_single(new_data, current_input, 0);
	transform_input_single(new_data, current_input, 1);
	transform_input_single(new_data, current_input, 2);
	transform_input_single(new_data, current_input, 3);
	transform_input_single(new_data, current_input, 4);
	transform_input_single(new_data, current_input, 5);
	transform_input_single(new_data, current_input, 6);
	transform_input_single(new_data, current_input, 7);
	transform_input_single(new_data, current_input, 8);
	transform_input_single(new_data, current_input, 9);
	transform_input_single(new_data, current_input, 10);
	transform_input_single(new_data, current_input, 11);
	transform_input_single(new_data, current_input, 12);
	transform_input_single(new_data, current_input, 13);
	transform_input_single(new_data, current_input, 14);
	transform_input_single(new_data, current_input, 15);

}

/*
 * buffer to Calculate all segments' digests from one block.
 *
 * Layout of seg_digest:
 *  segment
 *  -------------------------
 *   seg0:   | H0  |  H1  | ... |  H4
 *   seg1:   | H0  |  H1  | ... |  H4
 *   seg2:   | H0  |  H1  | ... |  H4
 *   ....
 *   seg15: | H0  |  H1  | ... |  H4
 *
 */
static inline void sha1_update_all_segs(uint32_t * new_data,
					uint32_t(*mh_sha1_seg_digests)[SHA1_DIGEST_WORDS])
{
	sha1_update_one_seg(&(new_data)[16 * 0], mh_sha1_seg_digests[0]);
	sha1_update_one_seg(&(new_data)[16 * 1], mh_sha1_seg_digests[1]);
	sha1_update_one_seg(&(new_data)[16 * 2], mh_sha1_seg_digests[2]);
	sha1_update_one_seg(&(new_data)[16 * 3], mh_sha1_seg_digests[3]);
	sha1_update_one_seg(&(new_data)[16 * 4], mh_sha1_seg_digests[4]);
	sha1_update_one_seg(&(new_data)[16 * 5], mh_sha1_seg_digests[5]);
	sha1_update_one_seg(&(new_data)[16 * 6], mh_sha1_seg_digests[6]);
	sha1_update_one_seg(&(new_data)[16 * 7], mh_sha1_seg_digests[7]);
	sha1_update_one_seg(&(new_data)[16 * 8], mh_sha1_seg_digests[8]);
	sha1_update_one_seg(&(new_data)[16 * 9], mh_sha1_seg_digests[9]);
	sha1_update_one_seg(&(new_data)[16 * 10], mh_sha1_seg_digests[10]);
	sha1_update_one_seg(&(new_data)[16 * 11], mh_sha1_seg_digests[11]);
	sha1_update_one_seg(&(new_data)[16 * 12], mh_sha1_seg_digests[12]);
	sha1_update_one_seg(&(new_data)[16 * 13], mh_sha1_seg_digests[13]);
	sha1_update_one_seg(&(new_data)[16 * 14], mh_sha1_seg_digests[14]);
	sha1_update_one_seg(&(new_data)[16 * 15], mh_sha1_seg_digests[15]);
}

void mh_sha1_block_ref(const uint8_t * input_data, uint32_t(*digests)[HASH_SEGS],
		       uint8_t frame_buffer[MH_SHA1_BLOCK_SIZE], uint32_t num_blocks)
{
	uint32_t i, j;
	uint32_t *temp_buffer = (uint32_t *) frame_buffer;
	uint32_t(*trans_digests)[SHA1_DIGEST_WORDS];

	trans_digests = (uint32_t(*)[SHA1_DIGEST_WORDS]) digests;

	// Re-structure seg_digests from 5*16 to 16*5
	for (j = 0; j < HASH_SEGS; j++) {
		for (i = 0; i < SHA1_DIGEST_WORDS; i++) {
			temp_buffer[j * SHA1_DIGEST_WORDS + i] = digests[i][j];
		}
	}
	memcpy(trans_digests, temp_buffer, 4 * SHA1_DIGEST_WORDS * HASH_SEGS);

	// Calculate digests for all segments, leveraging sha1 API
	for (i = 0; i < num_blocks; i++) {
		transform_input(temp_buffer, (uint32_t *) input_data, i);
		sha1_update_all_segs(temp_buffer, trans_digests);
	}

	// Re-structure seg_digests from 16*5 to 5*16
	for (j = 0; j < HASH_SEGS; j++) {
		for (i = 0; i < SHA1_DIGEST_WORDS; i++) {
			temp_buffer[i * HASH_SEGS + j] = trans_digests[j][i];
		}
	}
	memcpy(digests, temp_buffer, 4 * SHA1_DIGEST_WORDS * HASH_SEGS);

	return;
}

void mh_sha1_tail_ref(uint8_t * partial_buffer, uint32_t total_len,
		      uint32_t(*mh_sha1_segs_digests)[HASH_SEGS], uint8_t * frame_buffer,
		      uint32_t digests[SHA1_DIGEST_WORDS])
{
	uint64_t partial_buffer_len, len_in_bit;

	partial_buffer_len = total_len % MH_SHA1_BLOCK_SIZE;

	// Padding the first block
	partial_buffer[partial_buffer_len] = 0x80;
	partial_buffer_len++;
	memset(partial_buffer + partial_buffer_len, 0,
	       MH_SHA1_BLOCK_SIZE - partial_buffer_len);

	// Calculate the first block without total_length if padding needs 2 block
	if (partial_buffer_len > (MH_SHA1_BLOCK_SIZE - 8)) {
		mh_sha1_block_ref(partial_buffer, mh_sha1_segs_digests, frame_buffer, 1);
		//Padding the second block
		memset(partial_buffer, 0, MH_SHA1_BLOCK_SIZE);
	}
	//Padding the block
	len_in_bit = bswap64((uint64_t) total_len * 8);
	*(uint64_t *) (partial_buffer + MH_SHA1_BLOCK_SIZE - 8) = len_in_bit;
	mh_sha1_block_ref(partial_buffer, mh_sha1_segs_digests, frame_buffer, 1);

	//Calculate multi-hash SHA1 digests (segment digests as input message)
	sha1_for_mh_sha1_ref((uint8_t *) mh_sha1_segs_digests, digests,
			     4 * SHA1_DIGEST_WORDS * HASH_SEGS);

	return;
}

void mh_sha1_ref(const void *buffer, uint32_t len, uint32_t * mh_sha1_digest)
{
	uint64_t total_len;
	uint64_t num_blocks;
	uint32_t mh_sha1_segs_digests[SHA1_DIGEST_WORDS][HASH_SEGS];
	uint8_t frame_buffer[MH_SHA1_BLOCK_SIZE];
	uint8_t partial_block_buffer[MH_SHA1_BLOCK_SIZE * 2];
	uint32_t mh_sha1_hash_dword[SHA1_DIGEST_WORDS];
	uint32_t i;
	const uint8_t *input_data = (const uint8_t *)buffer;

	/* Initialize digests of all segments */
	for (i = 0; i < HASH_SEGS; i++) {
		mh_sha1_segs_digests[0][i] = MH_SHA1_H0;
		mh_sha1_segs_digests[1][i] = MH_SHA1_H1;
		mh_sha1_segs_digests[2][i] = MH_SHA1_H2;
		mh_sha1_segs_digests[3][i] = MH_SHA1_H3;
		mh_sha1_segs_digests[4][i] = MH_SHA1_H4;
	}

	total_len = len;

	// Calculate blocks
	num_blocks = len / MH_SHA1_BLOCK_SIZE;
	if (num_blocks > 0) {
		//do num_blocks process
		mh_sha1_block_ref(input_data, mh_sha1_segs_digests, frame_buffer, num_blocks);
		len -= num_blocks * MH_SHA1_BLOCK_SIZE;
		input_data += num_blocks * MH_SHA1_BLOCK_SIZE;
	}
	// Store the partial block
	if (len != 0) {
		memcpy(partial_block_buffer, input_data, len);
	}

	/* Finalize */
	mh_sha1_tail_ref(partial_block_buffer, total_len, mh_sha1_segs_digests,
			 frame_buffer, mh_sha1_hash_dword);

	// Output the digests of mh_sha1
	if (mh_sha1_digest != NULL) {
		mh_sha1_digest[0] = mh_sha1_hash_dword[0];
		mh_sha1_digest[1] = mh_sha1_hash_dword[1];
		mh_sha1_digest[2] = mh_sha1_hash_dword[2];
		mh_sha1_digest[3] = mh_sha1_hash_dword[3];
		mh_sha1_digest[4] = mh_sha1_hash_dword[4];
	}

	return;
}
