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

#include <string.h>
#include "mh_sha256_internal.h"

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
 //  Macros and sub-functions which already exist in source code file
 //  (sha256_for_mh_sha256.c) is part of ISA-L library as internal functions.
 //  The reason why writing them twice is the linking issue caused by
 //  mh_sha256_ref(). mh_sha256_ref() needs these macros and sub-functions
 //  without linking ISA-L library. So mh_sha256_ref() includes them in
 //  order to contain essential sub-functions in its own object file.
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////

#define W(x) w[(x) & 15]

#define step(i,a,b,c,d,e,f,g,h,k) \
	if (i<16) W(i) = bswap(ww[i]); \
	else \
	W(i) = W(i-16) + S0(W(i-15)) + W(i-7) + S1(W(i-2)); \
	t2 = s0(a) + maj(a,b,c); \
	t1 = h + s1(e) + ch(e,f,g) + k + W(i); \
	d += t1; \
	h = t1 + t2;

void sha256_single_for_mh_sha256_ref(const uint8_t * data, uint32_t digest[])
{
	uint32_t a, b, c, d, e, f, g, h, t1, t2;
	uint32_t w[16];
	uint32_t *ww = (uint32_t *) data;

	a = digest[0];
	b = digest[1];
	c = digest[2];
	d = digest[3];
	e = digest[4];
	f = digest[5];
	g = digest[6];
	h = digest[7];

	step(0, a, b, c, d, e, f, g, h, 0x428a2f98);
	step(1, h, a, b, c, d, e, f, g, 0x71374491);
	step(2, g, h, a, b, c, d, e, f, 0xb5c0fbcf);
	step(3, f, g, h, a, b, c, d, e, 0xe9b5dba5);
	step(4, e, f, g, h, a, b, c, d, 0x3956c25b);
	step(5, d, e, f, g, h, a, b, c, 0x59f111f1);
	step(6, c, d, e, f, g, h, a, b, 0x923f82a4);
	step(7, b, c, d, e, f, g, h, a, 0xab1c5ed5);
	step(8, a, b, c, d, e, f, g, h, 0xd807aa98);
	step(9, h, a, b, c, d, e, f, g, 0x12835b01);
	step(10, g, h, a, b, c, d, e, f, 0x243185be);
	step(11, f, g, h, a, b, c, d, e, 0x550c7dc3);
	step(12, e, f, g, h, a, b, c, d, 0x72be5d74);
	step(13, d, e, f, g, h, a, b, c, 0x80deb1fe);
	step(14, c, d, e, f, g, h, a, b, 0x9bdc06a7);
	step(15, b, c, d, e, f, g, h, a, 0xc19bf174);
	step(16, a, b, c, d, e, f, g, h, 0xe49b69c1);
	step(17, h, a, b, c, d, e, f, g, 0xefbe4786);
	step(18, g, h, a, b, c, d, e, f, 0x0fc19dc6);
	step(19, f, g, h, a, b, c, d, e, 0x240ca1cc);
	step(20, e, f, g, h, a, b, c, d, 0x2de92c6f);
	step(21, d, e, f, g, h, a, b, c, 0x4a7484aa);
	step(22, c, d, e, f, g, h, a, b, 0x5cb0a9dc);
	step(23, b, c, d, e, f, g, h, a, 0x76f988da);
	step(24, a, b, c, d, e, f, g, h, 0x983e5152);
	step(25, h, a, b, c, d, e, f, g, 0xa831c66d);
	step(26, g, h, a, b, c, d, e, f, 0xb00327c8);
	step(27, f, g, h, a, b, c, d, e, 0xbf597fc7);
	step(28, e, f, g, h, a, b, c, d, 0xc6e00bf3);
	step(29, d, e, f, g, h, a, b, c, 0xd5a79147);
	step(30, c, d, e, f, g, h, a, b, 0x06ca6351);
	step(31, b, c, d, e, f, g, h, a, 0x14292967);
	step(32, a, b, c, d, e, f, g, h, 0x27b70a85);
	step(33, h, a, b, c, d, e, f, g, 0x2e1b2138);
	step(34, g, h, a, b, c, d, e, f, 0x4d2c6dfc);
	step(35, f, g, h, a, b, c, d, e, 0x53380d13);
	step(36, e, f, g, h, a, b, c, d, 0x650a7354);
	step(37, d, e, f, g, h, a, b, c, 0x766a0abb);
	step(38, c, d, e, f, g, h, a, b, 0x81c2c92e);
	step(39, b, c, d, e, f, g, h, a, 0x92722c85);
	step(40, a, b, c, d, e, f, g, h, 0xa2bfe8a1);
	step(41, h, a, b, c, d, e, f, g, 0xa81a664b);
	step(42, g, h, a, b, c, d, e, f, 0xc24b8b70);
	step(43, f, g, h, a, b, c, d, e, 0xc76c51a3);
	step(44, e, f, g, h, a, b, c, d, 0xd192e819);
	step(45, d, e, f, g, h, a, b, c, 0xd6990624);
	step(46, c, d, e, f, g, h, a, b, 0xf40e3585);
	step(47, b, c, d, e, f, g, h, a, 0x106aa070);
	step(48, a, b, c, d, e, f, g, h, 0x19a4c116);
	step(49, h, a, b, c, d, e, f, g, 0x1e376c08);
	step(50, g, h, a, b, c, d, e, f, 0x2748774c);
	step(51, f, g, h, a, b, c, d, e, 0x34b0bcb5);
	step(52, e, f, g, h, a, b, c, d, 0x391c0cb3);
	step(53, d, e, f, g, h, a, b, c, 0x4ed8aa4a);
	step(54, c, d, e, f, g, h, a, b, 0x5b9cca4f);
	step(55, b, c, d, e, f, g, h, a, 0x682e6ff3);
	step(56, a, b, c, d, e, f, g, h, 0x748f82ee);
	step(57, h, a, b, c, d, e, f, g, 0x78a5636f);
	step(58, g, h, a, b, c, d, e, f, 0x84c87814);
	step(59, f, g, h, a, b, c, d, e, 0x8cc70208);
	step(60, e, f, g, h, a, b, c, d, 0x90befffa);
	step(61, d, e, f, g, h, a, b, c, 0xa4506ceb);
	step(62, c, d, e, f, g, h, a, b, 0xbef9a3f7);
	step(63, b, c, d, e, f, g, h, a, 0xc67178f2);

	digest[0] += a;
	digest[1] += b;
	digest[2] += c;
	digest[3] += d;
	digest[4] += e;
	digest[5] += f;
	digest[6] += g;
	digest[7] += h;
}

void sha256_for_mh_sha256_ref(const uint8_t * input_data, uint32_t * digest,
			      const uint32_t len)
{
	uint32_t i, j;
	uint8_t buf[2 * SHA256_BLOCK_SIZE];
	union {
		uint64_t uint;
		uint8_t uchar[8];
	} convert;
	uint8_t *p;

	digest[0] = MH_SHA256_H0;
	digest[1] = MH_SHA256_H1;
	digest[2] = MH_SHA256_H2;
	digest[3] = MH_SHA256_H3;
	digest[4] = MH_SHA256_H4;
	digest[5] = MH_SHA256_H5;
	digest[6] = MH_SHA256_H6;
	digest[7] = MH_SHA256_H7;

	i = len;
	while (i >= SHA256_BLOCK_SIZE) {
		sha256_single_for_mh_sha256_ref(input_data, digest);
		input_data += SHA256_BLOCK_SIZE;
		i -= SHA256_BLOCK_SIZE;
	}

	memcpy(buf, input_data, i);
	buf[i++] = 0x80;
	for (j = i; j < ((2 * SHA256_BLOCK_SIZE) - 8); j++)
		buf[j] = 0;

	if (i > SHA256_BLOCK_SIZE - 8)
		i = 2 * SHA256_BLOCK_SIZE;
	else
		i = SHA256_BLOCK_SIZE;

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

	sha256_single_for_mh_sha256_ref(buf, digest);
	if (i == (2 * SHA256_BLOCK_SIZE))
		sha256_single_for_mh_sha256_ref(buf + SHA256_BLOCK_SIZE, digest);
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

// Adapt parameters to sha256_single_for_mh_sha256_ref
#define sha256_update_one_seg(data, digest) \
	sha256_single_for_mh_sha256_ref((const uint8_t *)(data), (uint32_t *)(digest))

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
	uint32_t *current_input = input + block * MH_SHA256_BLOCK_SIZE / 4;

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
 *   seg0:   | H0  |  H1  | ... |  H7
 *   seg1:   | H0  |  H1  | ... |  H7
 *   seg2:   | H0  |  H1  | ... |  H7
 *   ....
 *   seg15: | H0  |  H1  | ... |  H7
 *
 */
static inline void sha256_update_all_segs(uint32_t * new_data, uint32_t(*mh_sha256_seg_digests)
					  [SHA256_DIGEST_WORDS])
{
	sha256_update_one_seg(&(new_data)[16 * 0], mh_sha256_seg_digests[0]);
	sha256_update_one_seg(&(new_data)[16 * 1], mh_sha256_seg_digests[1]);
	sha256_update_one_seg(&(new_data)[16 * 2], mh_sha256_seg_digests[2]);
	sha256_update_one_seg(&(new_data)[16 * 3], mh_sha256_seg_digests[3]);
	sha256_update_one_seg(&(new_data)[16 * 4], mh_sha256_seg_digests[4]);
	sha256_update_one_seg(&(new_data)[16 * 5], mh_sha256_seg_digests[5]);
	sha256_update_one_seg(&(new_data)[16 * 6], mh_sha256_seg_digests[6]);
	sha256_update_one_seg(&(new_data)[16 * 7], mh_sha256_seg_digests[7]);
	sha256_update_one_seg(&(new_data)[16 * 8], mh_sha256_seg_digests[8]);
	sha256_update_one_seg(&(new_data)[16 * 9], mh_sha256_seg_digests[9]);
	sha256_update_one_seg(&(new_data)[16 * 10], mh_sha256_seg_digests[10]);
	sha256_update_one_seg(&(new_data)[16 * 11], mh_sha256_seg_digests[11]);
	sha256_update_one_seg(&(new_data)[16 * 12], mh_sha256_seg_digests[12]);
	sha256_update_one_seg(&(new_data)[16 * 13], mh_sha256_seg_digests[13]);
	sha256_update_one_seg(&(new_data)[16 * 14], mh_sha256_seg_digests[14]);
	sha256_update_one_seg(&(new_data)[16 * 15], mh_sha256_seg_digests[15]);
}

void mh_sha256_block_ref(const uint8_t * input_data, uint32_t(*digests)[HASH_SEGS],
			 uint8_t frame_buffer[MH_SHA256_BLOCK_SIZE], uint32_t num_blocks)
{
	uint32_t i, j;
	uint32_t *temp_buffer = (uint32_t *) frame_buffer;
	uint32_t(*trans_digests)[SHA256_DIGEST_WORDS];

	trans_digests = (uint32_t(*)[SHA256_DIGEST_WORDS]) digests;

	// Re-structure seg_digests from 5*16 to 16*5
	for (j = 0; j < HASH_SEGS; j++) {
		for (i = 0; i < SHA256_DIGEST_WORDS; i++) {
			temp_buffer[j * SHA256_DIGEST_WORDS + i] = digests[i][j];
		}
	}
	memcpy(trans_digests, temp_buffer, 4 * SHA256_DIGEST_WORDS * HASH_SEGS);

	// Calculate digests for all segments, leveraging sha256 API
	for (i = 0; i < num_blocks; i++) {
		transform_input(temp_buffer, (uint32_t *) input_data, i);
		sha256_update_all_segs(temp_buffer, trans_digests);
	}

	// Re-structure seg_digests from 16*5 to 5*16
	for (j = 0; j < HASH_SEGS; j++) {
		for (i = 0; i < SHA256_DIGEST_WORDS; i++) {
			temp_buffer[i * HASH_SEGS + j] = trans_digests[j][i];
		}
	}
	memcpy(digests, temp_buffer, 4 * SHA256_DIGEST_WORDS * HASH_SEGS);

	return;
}

void mh_sha256_tail_ref(uint8_t * partial_buffer, uint32_t total_len,
			uint32_t(*mh_sha256_segs_digests)[HASH_SEGS], uint8_t * frame_buffer,
			uint32_t digests[SHA256_DIGEST_WORDS])
{
	uint64_t partial_buffer_len, len_in_bit;

	partial_buffer_len = total_len % MH_SHA256_BLOCK_SIZE;

	// Padding the first block
	partial_buffer[partial_buffer_len] = 0x80;
	partial_buffer_len++;
	memset(partial_buffer + partial_buffer_len, 0,
	       MH_SHA256_BLOCK_SIZE - partial_buffer_len);

	// Calculate the first block without total_length if padding needs 2 block
	if (partial_buffer_len > (MH_SHA256_BLOCK_SIZE - 8)) {
		mh_sha256_block_ref(partial_buffer, mh_sha256_segs_digests, frame_buffer, 1);
		//Padding the second block
		memset(partial_buffer, 0, MH_SHA256_BLOCK_SIZE);
	}
	//Padding the block
	len_in_bit = bswap64((uint64_t) total_len * 8);
	*(uint64_t *) (partial_buffer + MH_SHA256_BLOCK_SIZE - 8) = len_in_bit;
	mh_sha256_block_ref(partial_buffer, mh_sha256_segs_digests, frame_buffer, 1);

	//Calculate multi-hash SHA256 digests (segment digests as input message)
	sha256_for_mh_sha256_ref((uint8_t *) mh_sha256_segs_digests, digests,
				 4 * SHA256_DIGEST_WORDS * HASH_SEGS);

	return;
}

void mh_sha256_ref(const void *buffer, uint32_t len, uint32_t * mh_sha256_digest)
{
	uint64_t total_len;
	uint64_t num_blocks;
	uint32_t mh_sha256_segs_digests[SHA256_DIGEST_WORDS][HASH_SEGS];
	uint8_t frame_buffer[MH_SHA256_BLOCK_SIZE];
	uint8_t partial_block_buffer[MH_SHA256_BLOCK_SIZE * 2];
	uint32_t mh_sha256_hash_dword[SHA256_DIGEST_WORDS];
	uint32_t i;
	const uint8_t *input_data = (const uint8_t *)buffer;

	/* Initialize digests of all segments */
	for (i = 0; i < HASH_SEGS; i++) {
		mh_sha256_segs_digests[0][i] = MH_SHA256_H0;
		mh_sha256_segs_digests[1][i] = MH_SHA256_H1;
		mh_sha256_segs_digests[2][i] = MH_SHA256_H2;
		mh_sha256_segs_digests[3][i] = MH_SHA256_H3;
		mh_sha256_segs_digests[4][i] = MH_SHA256_H4;
		mh_sha256_segs_digests[5][i] = MH_SHA256_H5;
		mh_sha256_segs_digests[6][i] = MH_SHA256_H6;
		mh_sha256_segs_digests[7][i] = MH_SHA256_H7;
	}

	total_len = len;

	// Calculate blocks
	num_blocks = len / MH_SHA256_BLOCK_SIZE;
	if (num_blocks > 0) {
		//do num_blocks process
		mh_sha256_block_ref(input_data, mh_sha256_segs_digests, frame_buffer,
				    num_blocks);
		len -= num_blocks * MH_SHA256_BLOCK_SIZE;
		input_data += num_blocks * MH_SHA256_BLOCK_SIZE;
	}
	// Store the partial block
	if (len != 0) {
		memcpy(partial_block_buffer, input_data, len);
	}

	/* Finalize */
	mh_sha256_tail_ref(partial_block_buffer, total_len, mh_sha256_segs_digests,
			   frame_buffer, mh_sha256_hash_dword);

	// Output the digests of mh_sha256
	if (mh_sha256_digest != NULL) {
		mh_sha256_digest[0] = mh_sha256_hash_dword[0];
		mh_sha256_digest[1] = mh_sha256_hash_dword[1];
		mh_sha256_digest[2] = mh_sha256_hash_dword[2];
		mh_sha256_digest[3] = mh_sha256_hash_dword[3];
		mh_sha256_digest[4] = mh_sha256_hash_dword[4];
		mh_sha256_digest[5] = mh_sha256_hash_dword[5];
		mh_sha256_digest[6] = mh_sha256_hash_dword[6];
		mh_sha256_digest[7] = mh_sha256_hash_dword[7];
	}

	return;
}
