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

#include <stdint.h>
#include <string.h>
#include "sha1_mb.h"
#include "memcpy_inline.h"

#ifdef _MSC_VER
#include <intrin.h>
#define inline __inline
#endif

#define F1(b,c,d) (d ^ (b & (c ^ d)))
#define F2(b,c,d) (b ^ c ^ d)
#define F3(b,c,d) ((b & c) | (d & (b | c)))
#define F4(b,c,d) (b ^ c ^ d)

#define rol32(x, r) (((x)<<(r)) ^ ((x)>>(32-(r))))
#define bswap(x) (((x)<<24) | (((x)&0xff00)<<8) | (((x)&0xff0000)>>8) | ((x)>>24))

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

static void sha1_init(SHA1_HASH_CTX * ctx, const void *buffer, uint32_t len);
static uint32_t sha1_update(SHA1_HASH_CTX * ctx, const void *buffer, uint32_t len);
static void sha1_final(SHA1_HASH_CTX * ctx, uint32_t remain_len);
static void sha1_single(const void *data, uint32_t digest[]);
static inline void hash_init_digest(SHA1_WORD_T * digest);

void sha1_ctx_mgr_init_base(SHA1_HASH_CTX_MGR * mgr)
{
}

SHA1_HASH_CTX *sha1_ctx_mgr_submit_base(SHA1_HASH_CTX_MGR * mgr, SHA1_HASH_CTX * ctx,
					const void *buffer, uint32_t len, HASH_CTX_FLAG flags)
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

		sha1_init(ctx, buffer, len);
		sha1_update(ctx, buffer, len);
	}

	if (flags == HASH_UPDATE) {
		sha1_update(ctx, buffer, len);
	}

	if (flags == HASH_LAST) {
		remain_len = sha1_update(ctx, buffer, len);
		sha1_final(ctx, remain_len);
	}

	if (flags == HASH_ENTIRE) {
		sha1_init(ctx, buffer, len);
		remain_len = sha1_update(ctx, buffer, len);
		sha1_final(ctx, remain_len);
	}

	return ctx;
}

SHA1_HASH_CTX *sha1_ctx_mgr_flush_base(SHA1_HASH_CTX_MGR * mgr)
{
	return NULL;
}

static void sha1_init(SHA1_HASH_CTX * ctx, const void *buffer, uint32_t len)
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

static uint32_t sha1_update(SHA1_HASH_CTX * ctx, const void *buffer, uint32_t len)
{
	uint32_t remain_len = len;
	uint32_t *digest = ctx->job.result_digest;

	while (remain_len >= SHA1_BLOCK_SIZE) {
		sha1_single(buffer, digest);
		buffer = (void *)((uint8_t *) buffer + SHA1_BLOCK_SIZE);
		remain_len -= SHA1_BLOCK_SIZE;
		ctx->total_length += SHA1_BLOCK_SIZE;
	}

	ctx->status = HASH_CTX_STS_IDLE;
	ctx->incoming_buffer = buffer;
	return remain_len;
}

static void sha1_final(SHA1_HASH_CTX * ctx, uint32_t remain_len)
{
	const void *buffer = ctx->incoming_buffer;
	uint32_t i = remain_len, j;
	uint8_t buf[2 * SHA1_BLOCK_SIZE];
	uint32_t *digest = ctx->job.result_digest;
	union {
		uint64_t uint;
		uint8_t uchar[8];
	} convert;
	uint8_t *p;

	ctx->total_length += i;
	memcpy(buf, buffer, i);
	buf[i++] = 0x80;
	for (j = i; j < ((2 * SHA1_BLOCK_SIZE) - SHA1_PADLENGTHFIELD_SIZE); j++)
		buf[j] = 0;

	if (i > SHA1_BLOCK_SIZE - SHA1_PADLENGTHFIELD_SIZE)
		i = 2 * SHA1_BLOCK_SIZE;
	else
		i = SHA1_BLOCK_SIZE;

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

	sha1_single(buf, digest);
	if (i == 2 * SHA1_BLOCK_SIZE) {
		sha1_single(buf + SHA1_BLOCK_SIZE, digest);
	}

	ctx->status = HASH_CTX_STS_COMPLETE;
}

void sha1_single(const void *data, uint32_t digest[])
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

static inline void hash_init_digest(SHA1_WORD_T * digest)
{
	static const SHA1_WORD_T hash_initial_digest[SHA1_DIGEST_NWORDS] =
	    { SHA1_INITIAL_DIGEST };
	memcpy_fixedlen(digest, hash_initial_digest, sizeof(hash_initial_digest));
}

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};

struct slver sha1_ctx_mgr_init_base_slver_00000192;
struct slver sha1_ctx_mgr_init_base_slver = { 0x0192, 0x00, 0x00 };

struct slver sha1_ctx_mgr_submit_base_slver_00000193;
struct slver sha1_ctx_mgr_submit_base_slver = { 0x0193, 0x00, 0x00 };

struct slver sha1_ctx_mgr_flush_base_slver_00000194;
struct slver sha1_ctx_mgr_flush_base_slver = { 0x0194, 0x00, 0x00 };
