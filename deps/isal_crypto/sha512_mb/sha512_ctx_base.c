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
#include "sha512_mb.h"
#include "memcpy_inline.h"

#ifdef _MSC_VER
#include <intrin.h>
#define inline __inline
#endif

/* From the FIPS, these are the same as for SHA256, but operating on 64 bit words
 * instead of 32 bit.
 */
#define ch(e,f,g) ((e & f) ^ (g & ~e))
#define maj(a,b,c) ((a & b) ^ (a & c) ^ (b & c))

/* Sigma functions have same form as SHA256 but
 * 	- change the word size to 64bit
 * 	- change the amount to rotate
 */
#define ror64(x, r) (((x)>>(r)) ^ ((x)<<(64-(r))))

/* Technically, s0 should be S0 as these are "capital sigma" functions, and likewise the case
 * of the  S0 should be s0, but keep as-is to avoid confusion with the other reference functions.
 */
#define s0(a) (ror64(a,28) ^ ror64(a,34) ^ ror64(a,39))
#define s1(e) (ror64(e,14) ^ ror64(e,18) ^ ror64(e,41))

#define S0(w) (ror64(w,1) ^ ror64(w,8) ^ (w >> 7))
#define S1(w) (ror64(w,19) ^ ror64(w,61) ^ (w >> 6))

#define bswap(x)  (((x) & (0xffull << 0)) << 56) \
		| (((x) & (0xffull << 8)) << 40) \
		| (((x) & (0xffull <<16)) << 24) \
		| (((x) & (0xffull <<24)) << 8)  \
		| (((x) & (0xffull <<32)) >> 8)  \
		| (((x) & (0xffull <<40)) >> 24) \
		| (((x) & (0xffull <<48)) >> 40) \
		| (((x) & (0xffull <<56)) >> 56)

#define W(x) w[(x) & 15]

#define step(i,a,b,c,d,e,f,g,h,k) \
	if (i<16) W(i) = bswap(ww[i]); \
	else \
	W(i) = W(i-16) + S0(W(i-15)) + W(i-7) + S1(W(i-2)); \
	t2 = s0(a) + maj(a,b,c); \
	t1 = h + s1(e) + ch(e,f,g) + k + W(i); \
	d += t1; \
	h = t1 + t2;

static void sha512_init(SHA512_HASH_CTX * ctx, const void *buffer, uint32_t len);
static uint32_t sha512_update(SHA512_HASH_CTX * ctx, const void *buffer, uint32_t len);
static void sha512_final(SHA512_HASH_CTX * ctx, uint32_t remain_len);
static void sha512_single(const void *data, uint64_t digest[]);
static inline void hash_init_digest(SHA512_WORD_T * digest);

void sha512_ctx_mgr_init_base(SHA512_HASH_CTX_MGR * mgr)
{
}

SHA512_HASH_CTX *sha512_ctx_mgr_submit_base(SHA512_HASH_CTX_MGR * mgr, SHA512_HASH_CTX * ctx,
					    const void *buffer, uint32_t len,
					    HASH_CTX_FLAG flags)
{
	uint32_t remain_len;

	if (flags & (~HASH_ENTIRE)) {
		// User should not pass anything other than FIRST, UPDATE, or LAST
		ctx->error = HASH_CTX_ERROR_INVALID_FLAGS;
		return ctx;
	}

	if ((ctx->status & HASH_CTX_STS_PROCESSING) && (flags == HASH_ENTIRE)) {
		// Cannot submit a new entire job to a currently processing job.
		ctx->error = HASH_CTX_ERROR_ALREADY_PROCESSING;
		return ctx;
	}

	if ((ctx->status & HASH_CTX_STS_COMPLETE) && !(flags & HASH_FIRST)) {
		// Cannot update a finished job.
		ctx->error = HASH_CTX_ERROR_ALREADY_COMPLETED;
		return ctx;
	}

	if (flags == HASH_FIRST) {

		sha512_init(ctx, buffer, len);
		sha512_update(ctx, buffer, len);
	}

	if (flags == HASH_UPDATE) {
		sha512_update(ctx, buffer, len);
	}

	if (flags == HASH_LAST) {
		remain_len = sha512_update(ctx, buffer, len);
		sha512_final(ctx, remain_len);
	}

	if (flags == HASH_ENTIRE) {
		sha512_init(ctx, buffer, len);
		remain_len = sha512_update(ctx, buffer, len);
		sha512_final(ctx, remain_len);
	}

	return ctx;
}

SHA512_HASH_CTX *sha512_ctx_mgr_flush_base(SHA512_HASH_CTX_MGR * mgr)
{
	return NULL;
}

static void sha512_init(SHA512_HASH_CTX * ctx, const void *buffer, uint32_t len)
{
	// Init digest
	hash_init_digest(ctx->job.result_digest);

	// Reset byte counter
	ctx->total_length = 0;

	// Clear extra blocks
	ctx->partial_block_buffer_length = 0;

	// If we made it here, there were no errors during this call to submit
	ctx->error = HASH_CTX_ERROR_NONE;

	// Mark it as processing
	ctx->status = HASH_CTX_STS_PROCESSING;
}

static uint32_t sha512_update(SHA512_HASH_CTX * ctx, const void *buffer, uint32_t len)
{
	uint32_t remain_len = len;
	uint64_t *digest = ctx->job.result_digest;

	while (remain_len >= SHA512_BLOCK_SIZE) {
		sha512_single(buffer, digest);
		buffer = (void *)((uint8_t *) buffer + SHA512_BLOCK_SIZE);
		remain_len -= SHA512_BLOCK_SIZE;
		ctx->total_length += SHA512_BLOCK_SIZE;
	}
	ctx->status = HASH_CTX_STS_IDLE;
	ctx->incoming_buffer = buffer;
	return remain_len;
}

static void sha512_final(SHA512_HASH_CTX * ctx, uint32_t remain_len)
{
	const void *buffer = ctx->incoming_buffer;
	uint32_t i = remain_len, j;
	uint8_t buf[2 * SHA512_BLOCK_SIZE];
	uint64_t *digest = ctx->job.result_digest;
	union {
		uint64_t uint;
		uint8_t uchar[8];
	} convert;
	uint8_t *p;

	ctx->total_length += i;
	memcpy(buf, buffer, i);
	buf[i++] = 0x80;
	for (j = i; j < (2 * SHA512_BLOCK_SIZE); j++)
		buf[j] = 0;

	if (i > SHA512_BLOCK_SIZE - SHA512_PADLENGTHFIELD_SIZE)
		i = 2 * SHA512_BLOCK_SIZE;
	else
		i = SHA512_BLOCK_SIZE;

	convert.uint = 8 * ctx->total_length;
	p = buf + i - 8;
	p[0] = convert.uchar[7];
	p[1] = convert.uchar[6];
	p[2] = convert.uchar[5];
	p[3] = convert.uchar[4];
	p[4] = convert.uchar[3];
	p[5] = convert.uchar[2];
	p[6] = convert.uchar[1];
	p[7] = convert.uchar[0];

	sha512_single(buf, digest);
	if (i == 2 * SHA512_BLOCK_SIZE) {
		sha512_single(buf + SHA512_BLOCK_SIZE, digest);
	}

	ctx->status = HASH_CTX_STS_COMPLETE;
}

void sha512_single(const void *data, uint64_t digest[])
{
	/* Check these are all uint64_t */
	uint64_t a, b, c, d, e, f, g, h, t1, t2;
	uint64_t w[16];
	uint64_t *ww = (uint64_t *) data;

	a = digest[0];
	b = digest[1];
	c = digest[2];
	d = digest[3];
	e = digest[4];
	f = digest[5];
	g = digest[6];
	h = digest[7];

	step(0, a, b, c, d, e, f, g, h, 0x428a2f98d728ae22);
	step(1, h, a, b, c, d, e, f, g, 0x7137449123ef65cd);
	step(2, g, h, a, b, c, d, e, f, 0xb5c0fbcfec4d3b2f);
	step(3, f, g, h, a, b, c, d, e, 0xe9b5dba58189dbbc);
	step(4, e, f, g, h, a, b, c, d, 0x3956c25bf348b538);
	step(5, d, e, f, g, h, a, b, c, 0x59f111f1b605d019);
	step(6, c, d, e, f, g, h, a, b, 0x923f82a4af194f9b);
	step(7, b, c, d, e, f, g, h, a, 0xab1c5ed5da6d8118);
	step(8, a, b, c, d, e, f, g, h, 0xd807aa98a3030242);
	step(9, h, a, b, c, d, e, f, g, 0x12835b0145706fbe);
	step(10, g, h, a, b, c, d, e, f, 0x243185be4ee4b28c);
	step(11, f, g, h, a, b, c, d, e, 0x550c7dc3d5ffb4e2);
	step(12, e, f, g, h, a, b, c, d, 0x72be5d74f27b896f);
	step(13, d, e, f, g, h, a, b, c, 0x80deb1fe3b1696b1);
	step(14, c, d, e, f, g, h, a, b, 0x9bdc06a725c71235);
	step(15, b, c, d, e, f, g, h, a, 0xc19bf174cf692694);
	step(16, a, b, c, d, e, f, g, h, 0xe49b69c19ef14ad2);
	step(17, h, a, b, c, d, e, f, g, 0xefbe4786384f25e3);
	step(18, g, h, a, b, c, d, e, f, 0x0fc19dc68b8cd5b5);
	step(19, f, g, h, a, b, c, d, e, 0x240ca1cc77ac9c65);
	step(20, e, f, g, h, a, b, c, d, 0x2de92c6f592b0275);
	step(21, d, e, f, g, h, a, b, c, 0x4a7484aa6ea6e483);
	step(22, c, d, e, f, g, h, a, b, 0x5cb0a9dcbd41fbd4);
	step(23, b, c, d, e, f, g, h, a, 0x76f988da831153b5);
	step(24, a, b, c, d, e, f, g, h, 0x983e5152ee66dfab);
	step(25, h, a, b, c, d, e, f, g, 0xa831c66d2db43210);
	step(26, g, h, a, b, c, d, e, f, 0xb00327c898fb213f);
	step(27, f, g, h, a, b, c, d, e, 0xbf597fc7beef0ee4);
	step(28, e, f, g, h, a, b, c, d, 0xc6e00bf33da88fc2);
	step(29, d, e, f, g, h, a, b, c, 0xd5a79147930aa725);
	step(30, c, d, e, f, g, h, a, b, 0x06ca6351e003826f);
	step(31, b, c, d, e, f, g, h, a, 0x142929670a0e6e70);
	step(32, a, b, c, d, e, f, g, h, 0x27b70a8546d22ffc);
	step(33, h, a, b, c, d, e, f, g, 0x2e1b21385c26c926);
	step(34, g, h, a, b, c, d, e, f, 0x4d2c6dfc5ac42aed);
	step(35, f, g, h, a, b, c, d, e, 0x53380d139d95b3df);
	step(36, e, f, g, h, a, b, c, d, 0x650a73548baf63de);
	step(37, d, e, f, g, h, a, b, c, 0x766a0abb3c77b2a8);
	step(38, c, d, e, f, g, h, a, b, 0x81c2c92e47edaee6);
	step(39, b, c, d, e, f, g, h, a, 0x92722c851482353b);
	step(40, a, b, c, d, e, f, g, h, 0xa2bfe8a14cf10364);
	step(41, h, a, b, c, d, e, f, g, 0xa81a664bbc423001);
	step(42, g, h, a, b, c, d, e, f, 0xc24b8b70d0f89791);
	step(43, f, g, h, a, b, c, d, e, 0xc76c51a30654be30);
	step(44, e, f, g, h, a, b, c, d, 0xd192e819d6ef5218);
	step(45, d, e, f, g, h, a, b, c, 0xd69906245565a910);
	step(46, c, d, e, f, g, h, a, b, 0xf40e35855771202a);
	step(47, b, c, d, e, f, g, h, a, 0x106aa07032bbd1b8);
	step(48, a, b, c, d, e, f, g, h, 0x19a4c116b8d2d0c8);
	step(49, h, a, b, c, d, e, f, g, 0x1e376c085141ab53);
	step(50, g, h, a, b, c, d, e, f, 0x2748774cdf8eeb99);
	step(51, f, g, h, a, b, c, d, e, 0x34b0bcb5e19b48a8);
	step(52, e, f, g, h, a, b, c, d, 0x391c0cb3c5c95a63);
	step(53, d, e, f, g, h, a, b, c, 0x4ed8aa4ae3418acb);
	step(54, c, d, e, f, g, h, a, b, 0x5b9cca4f7763e373);
	step(55, b, c, d, e, f, g, h, a, 0x682e6ff3d6b2b8a3);
	step(56, a, b, c, d, e, f, g, h, 0x748f82ee5defb2fc);
	step(57, h, a, b, c, d, e, f, g, 0x78a5636f43172f60);
	step(58, g, h, a, b, c, d, e, f, 0x84c87814a1f0ab72);
	step(59, f, g, h, a, b, c, d, e, 0x8cc702081a6439ec);
	step(60, e, f, g, h, a, b, c, d, 0x90befffa23631e28);
	step(61, d, e, f, g, h, a, b, c, 0xa4506cebde82bde9);
	step(62, c, d, e, f, g, h, a, b, 0xbef9a3f7b2c67915);
	step(63, b, c, d, e, f, g, h, a, 0xc67178f2e372532b);	// step 63
	step(64, a, b, c, d, e, f, g, h, 0xca273eceea26619c);
	step(65, h, a, b, c, d, e, f, g, 0xd186b8c721c0c207);
	step(66, g, h, a, b, c, d, e, f, 0xeada7dd6cde0eb1e);
	step(67, f, g, h, a, b, c, d, e, 0xf57d4f7fee6ed178);
	step(68, e, f, g, h, a, b, c, d, 0x06f067aa72176fba);
	step(69, d, e, f, g, h, a, b, c, 0x0a637dc5a2c898a6);
	step(70, c, d, e, f, g, h, a, b, 0x113f9804bef90dae);
	step(71, b, c, d, e, f, g, h, a, 0x1b710b35131c471b);
	step(72, a, b, c, d, e, f, g, h, 0x28db77f523047d84);
	step(73, h, a, b, c, d, e, f, g, 0x32caab7b40c72493);
	step(74, g, h, a, b, c, d, e, f, 0x3c9ebe0a15c9bebc);
	step(75, f, g, h, a, b, c, d, e, 0x431d67c49c100d4c);
	step(76, e, f, g, h, a, b, c, d, 0x4cc5d4becb3e42b6);
	step(77, d, e, f, g, h, a, b, c, 0x597f299cfc657e2a);
	step(78, c, d, e, f, g, h, a, b, 0x5fcb6fab3ad6faec);
	step(79, b, c, d, e, f, g, h, a, 0x6c44198c4a475817);	// step 79

	digest[0] += a;
	digest[1] += b;
	digest[2] += c;
	digest[3] += d;
	digest[4] += e;
	digest[5] += f;
	digest[6] += g;
	digest[7] += h;
}

static inline void hash_init_digest(SHA512_WORD_T * digest)
{
	static const SHA512_WORD_T hash_initial_digest[SHA512_DIGEST_NWORDS] =
	    { SHA512_INITIAL_DIGEST };
	memcpy_fixedlen(digest, hash_initial_digest, sizeof(hash_initial_digest));
}

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};
struct slver sha512_ctx_mgr_init_base_slver_000002f3;
struct slver sha512_ctx_mgr_init_base_slver = { 0x02f3, 0x00, 0x00 };

struct slver sha512_ctx_mgr_submit_base_slver_000002f4;
struct slver sha512_ctx_mgr_submit_base_slver = { 0x02f4, 0x00, 0x00 };

struct slver sha512_ctx_mgr_flush_base_slver_000002f5;
struct slver sha512_ctx_mgr_flush_base_slver = { 0x02f5, 0x00, 0x00 };
