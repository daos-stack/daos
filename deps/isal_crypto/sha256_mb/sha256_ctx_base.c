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
#include "sha256_mb.h"
#include "memcpy_inline.h"

#ifdef _MSC_VER
#include <intrin.h>
#define inline __inline
#endif

#define ror32(x, r) (((x)>>(r)) ^ ((x)<<(32-(r))))
#define bswap(x) (((x)<<24) | (((x)&0xff00)<<8) | (((x)&0xff0000)>>8) | ((x)>>24))

#define W(x) w[(x) & 15]

#define S0(w) (ror32(w,7) ^ ror32(w,18) ^ (w >> 3))
#define S1(w) (ror32(w,17) ^ ror32(w,19) ^ (w >> 10))

#define s0(a) (ror32(a,2) ^ ror32(a,13) ^ ror32(a,22))
#define s1(e) (ror32(e,6) ^ ror32(e,11) ^ ror32(e,25))
#define maj(a,b,c) ((a & b) ^ (a & c) ^ (b & c))
#define ch(e,f,g) ((e & f) ^ (g & ~e))

#define step(i,a,b,c,d,e,f,g,h,k) \
	if (i<16) W(i) = bswap(ww[i]); \
	else \
	W(i) = W(i-16) + S0(W(i-15)) + W(i-7) + S1(W(i-2)); \
	t2 = s0(a) + maj(a,b,c); \
	t1 = h + s1(e) + ch(e,f,g) + k + W(i); \
	d += t1; \
	h = t1 + t2;

static void sha256_init(SHA256_HASH_CTX * ctx, const void *buffer, uint32_t len);
static uint32_t sha256_update(SHA256_HASH_CTX * ctx, const void *buffer, uint32_t len);
static void sha256_final(SHA256_HASH_CTX * ctx, uint32_t remain_len);
static void sha256_single(const void *data, uint32_t digest[]);
static inline void hash_init_digest(SHA256_WORD_T * digest);

void sha256_ctx_mgr_init_base(SHA256_HASH_CTX_MGR * mgr)
{
}

SHA256_HASH_CTX *sha256_ctx_mgr_submit_base(SHA256_HASH_CTX_MGR * mgr, SHA256_HASH_CTX * ctx,
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

		sha256_init(ctx, buffer, len);
		sha256_update(ctx, buffer, len);
	}

	if (flags == HASH_UPDATE) {
		sha256_update(ctx, buffer, len);
	}

	if (flags == HASH_LAST) {
		remain_len = sha256_update(ctx, buffer, len);
		sha256_final(ctx, remain_len);
	}

	if (flags == HASH_ENTIRE) {
		sha256_init(ctx, buffer, len);
		remain_len = sha256_update(ctx, buffer, len);
		sha256_final(ctx, remain_len);
	}

	return ctx;
}

SHA256_HASH_CTX *sha256_ctx_mgr_flush_base(SHA256_HASH_CTX_MGR * mgr)
{
	return NULL;
}

static void sha256_init(SHA256_HASH_CTX * ctx, const void *buffer, uint32_t len)
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

static uint32_t sha256_update(SHA256_HASH_CTX * ctx, const void *buffer, uint32_t len)
{
	uint32_t remain_len = len;
	uint32_t *digest = ctx->job.result_digest;

	while (remain_len >= SHA256_BLOCK_SIZE) {
		sha256_single(buffer, digest);
		buffer = (void *)((uint8_t *) buffer + SHA256_BLOCK_SIZE);
		remain_len -= SHA256_BLOCK_SIZE;
		ctx->total_length += SHA256_BLOCK_SIZE;
	}
	ctx->status = HASH_CTX_STS_IDLE;
	ctx->incoming_buffer = buffer;
	return remain_len;
}

static void sha256_final(SHA256_HASH_CTX * ctx, uint32_t remain_len)
{
	const void *buffer = ctx->incoming_buffer;
	uint32_t i = remain_len, j;
	uint8_t buf[2 * SHA256_BLOCK_SIZE];
	uint32_t *digest = ctx->job.result_digest;
	union {
		uint64_t uint;
		uint8_t uchar[8];
	} convert;
	uint8_t *p;

	ctx->total_length += i;
	memcpy(buf, buffer, i);
	buf[i++] = 0x80;
	for (j = i; j < ((2 * SHA256_BLOCK_SIZE) - SHA256_PADLENGTHFIELD_SIZE); j++)
		buf[j] = 0;

	if (i > SHA256_BLOCK_SIZE - SHA256_PADLENGTHFIELD_SIZE)
		i = 2 * SHA256_BLOCK_SIZE;
	else
		i = SHA256_BLOCK_SIZE;

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

	sha256_single(buf, digest);
	if (i == 2 * SHA256_BLOCK_SIZE) {
		sha256_single(buf + SHA256_BLOCK_SIZE, digest);
	}

	ctx->status = HASH_CTX_STS_COMPLETE;
}

void sha256_single(const void *data, uint32_t digest[])
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

static inline void hash_init_digest(SHA256_WORD_T * digest)
{
	static const SHA256_WORD_T hash_initial_digest[SHA256_DIGEST_NWORDS] =
	    { SHA256_INITIAL_DIGEST };
	memcpy_fixedlen(digest, hash_initial_digest, sizeof(hash_initial_digest));
}

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};
struct slver sha256_ctx_mgr_init_base_slver_000002f0;
struct slver sha256_ctx_mgr_init_base_slver = { 0x02f0, 0x00, 0x00 };

struct slver sha256_ctx_mgr_submit_base_slver_000002f1;
struct slver sha256_ctx_mgr_submit_base_slver = { 0x02f1, 0x00, 0x00 };

struct slver sha256_ctx_mgr_flush_base_slver_000002f2;
struct slver sha256_ctx_mgr_flush_base_slver = { 0x02f2, 0x00, 0x00 };
