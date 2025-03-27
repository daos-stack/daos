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
#include "md5_mb.h"
#include "memcpy_inline.h"

#ifdef _MSC_VER
#include <intrin.h>
#define inline __inline
#endif

#define F1(b,c,d) (d ^ (b & (c ^ d)))
#define F2(b,c,d) (c ^ (d & (b ^ c)))
#define F3(b,c,d) (b ^ c ^ d)
#define F4(b,c,d) (c ^ (b | ~d))

#define rol32(x, r) (((x)<<(r)) ^ ((x)>>(32-(r))))

#define step(i,a,b,c,d,f,k,w,r) \
	if (i < 16) {f = F1(b,c,d); } else \
	if (i < 32) {f = F2(b,c,d); } else \
	if (i < 48) {f = F3(b,c,d); } else \
				{f = F4(b,c,d); } \
	f = a + f + k + w; \
	a = b + rol32(f, r);

static void md5_init(MD5_HASH_CTX * ctx, const void *buffer, uint32_t len);
static uint32_t md5_update(MD5_HASH_CTX * ctx, const void *buffer, uint32_t len);
static void md5_final(MD5_HASH_CTX * ctx, uint32_t remain_len);
static void md5_single(const void *data, uint32_t digest[4]);
static inline void hash_init_digest(MD5_WORD_T * digest);

void md5_ctx_mgr_init_base(MD5_HASH_CTX_MGR * mgr)
{
}

MD5_HASH_CTX *md5_ctx_mgr_submit_base(MD5_HASH_CTX_MGR * mgr, MD5_HASH_CTX * ctx,
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

		md5_init(ctx, buffer, len);
		md5_update(ctx, buffer, len);
	}

	if (flags == HASH_UPDATE) {
		md5_update(ctx, buffer, len);
	}

	if (flags == HASH_LAST) {
		remain_len = md5_update(ctx, buffer, len);
		md5_final(ctx, remain_len);
	}

	if (flags == HASH_ENTIRE) {
		md5_init(ctx, buffer, len);
		remain_len = md5_update(ctx, buffer, len);
		md5_final(ctx, remain_len);
	}

	return ctx;
}

MD5_HASH_CTX *md5_ctx_mgr_flush_base(MD5_HASH_CTX_MGR * mgr)
{
	return NULL;
}

static void md5_init(MD5_HASH_CTX * ctx, const void *buffer, uint32_t len)
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

static uint32_t md5_update(MD5_HASH_CTX * ctx, const void *buffer, uint32_t len)
{
	uint32_t remain_len = len;
	uint32_t *digest = ctx->job.result_digest;
	while (remain_len >= 64) {
		md5_single(buffer, digest);
		buffer = (void *)((uint8_t *) buffer + 64);
		remain_len -= 64;
		ctx->total_length += 64;
	}

	ctx->status = HASH_CTX_STS_IDLE;
	ctx->incoming_buffer = buffer;
	return remain_len;
}

static void md5_final(MD5_HASH_CTX * ctx, uint32_t remain_len)
{
	const void *buffer = ctx->incoming_buffer;
	uint32_t i = remain_len, j;
	uint8_t buf[128];
	uint32_t *digest = ctx->job.result_digest;

	ctx->total_length += i;
	union {
		uint64_t uint;
		uint8_t uchar[8];
	} convert;
	uint8_t *p;
	memcpy(buf, buffer, i);
	buf[i++] = 0x80;
	for (j = i; j < 120; j++)
		buf[j] = 0;

	if (i > 64 - 8)
		i = 128;
	else
		i = 64;

	convert.uint = 8 * ctx->total_length;
	p = buf + i - 8;
	p[7] = convert.uchar[7];
	p[6] = convert.uchar[6];
	p[5] = convert.uchar[5];
	p[4] = convert.uchar[4];
	p[3] = convert.uchar[3];
	p[2] = convert.uchar[2];
	p[1] = convert.uchar[1];
	p[0] = convert.uchar[0];

	md5_single(buf, digest);
	if (i == 128) {
		md5_single(buf + 64, digest);
	}

	ctx->status = HASH_CTX_STS_COMPLETE;
}

static void md5_single(const void *data, uint32_t digest[4])
{

	uint32_t a, b, c, d;
	uint32_t f;
	uint32_t *w = (uint32_t *) data;

	a = digest[0];
	b = digest[1];
	c = digest[2];
	d = digest[3];

	step(0, a, b, c, d, f, 0xd76aa478, w[0], 7);
	step(1, d, a, b, c, f, 0xe8c7b756, w[1], 12);
	step(2, c, d, a, b, f, 0x242070db, w[2], 17);
	step(3, b, c, d, a, f, 0xc1bdceee, w[3], 22);
	step(4, a, b, c, d, f, 0xf57c0faf, w[4], 7);
	step(5, d, a, b, c, f, 0x4787c62a, w[5], 12);
	step(6, c, d, a, b, f, 0xa8304613, w[6], 17);
	step(7, b, c, d, a, f, 0xfd469501, w[7], 22);
	step(8, a, b, c, d, f, 0x698098d8, w[8], 7);
	step(9, d, a, b, c, f, 0x8b44f7af, w[9], 12);
	step(10, c, d, a, b, f, 0xffff5bb1, w[10], 17);
	step(11, b, c, d, a, f, 0x895cd7be, w[11], 22);
	step(12, a, b, c, d, f, 0x6b901122, w[12], 7);
	step(13, d, a, b, c, f, 0xfd987193, w[13], 12);
	step(14, c, d, a, b, f, 0xa679438e, w[14], 17);
	step(15, b, c, d, a, f, 0x49b40821, w[15], 22);

	step(16, a, b, c, d, f, 0xf61e2562, w[1], 5);
	step(17, d, a, b, c, f, 0xc040b340, w[6], 9);
	step(18, c, d, a, b, f, 0x265e5a51, w[11], 14);
	step(19, b, c, d, a, f, 0xe9b6c7aa, w[0], 20);
	step(20, a, b, c, d, f, 0xd62f105d, w[5], 5);
	step(21, d, a, b, c, f, 0x02441453, w[10], 9);
	step(22, c, d, a, b, f, 0xd8a1e681, w[15], 14);
	step(23, b, c, d, a, f, 0xe7d3fbc8, w[4], 20);
	step(24, a, b, c, d, f, 0x21e1cde6, w[9], 5);
	step(25, d, a, b, c, f, 0xc33707d6, w[14], 9);
	step(26, c, d, a, b, f, 0xf4d50d87, w[3], 14);
	step(27, b, c, d, a, f, 0x455a14ed, w[8], 20);
	step(28, a, b, c, d, f, 0xa9e3e905, w[13], 5);
	step(29, d, a, b, c, f, 0xfcefa3f8, w[2], 9);
	step(30, c, d, a, b, f, 0x676f02d9, w[7], 14);
	step(31, b, c, d, a, f, 0x8d2a4c8a, w[12], 20);

	step(32, a, b, c, d, f, 0xfffa3942, w[5], 4);
	step(33, d, a, b, c, f, 0x8771f681, w[8], 11);
	step(34, c, d, a, b, f, 0x6d9d6122, w[11], 16);
	step(35, b, c, d, a, f, 0xfde5380c, w[14], 23);
	step(36, a, b, c, d, f, 0xa4beea44, w[1], 4);
	step(37, d, a, b, c, f, 0x4bdecfa9, w[4], 11);
	step(38, c, d, a, b, f, 0xf6bb4b60, w[7], 16);
	step(39, b, c, d, a, f, 0xbebfbc70, w[10], 23);
	step(40, a, b, c, d, f, 0x289b7ec6, w[13], 4);
	step(41, d, a, b, c, f, 0xeaa127fa, w[0], 11);
	step(42, c, d, a, b, f, 0xd4ef3085, w[3], 16);
	step(43, b, c, d, a, f, 0x04881d05, w[6], 23);
	step(44, a, b, c, d, f, 0xd9d4d039, w[9], 4);
	step(45, d, a, b, c, f, 0xe6db99e5, w[12], 11);
	step(46, c, d, a, b, f, 0x1fa27cf8, w[15], 16);
	step(47, b, c, d, a, f, 0xc4ac5665, w[2], 23);

	step(48, a, b, c, d, f, 0xf4292244, w[0], 6);
	step(49, d, a, b, c, f, 0x432aff97, w[7], 10);
	step(50, c, d, a, b, f, 0xab9423a7, w[14], 15);
	step(51, b, c, d, a, f, 0xfc93a039, w[5], 21);
	step(52, a, b, c, d, f, 0x655b59c3, w[12], 6);
	step(53, d, a, b, c, f, 0x8f0ccc92, w[3], 10);
	step(54, c, d, a, b, f, 0xffeff47d, w[10], 15);
	step(55, b, c, d, a, f, 0x85845dd1, w[1], 21);
	step(56, a, b, c, d, f, 0x6fa87e4f, w[8], 6);
	step(57, d, a, b, c, f, 0xfe2ce6e0, w[15], 10);
	step(58, c, d, a, b, f, 0xa3014314, w[6], 15);
	step(59, b, c, d, a, f, 0x4e0811a1, w[13], 21);
	step(60, a, b, c, d, f, 0xf7537e82, w[4], 6);
	step(61, d, a, b, c, f, 0xbd3af235, w[11], 10);
	step(62, c, d, a, b, f, 0x2ad7d2bb, w[2], 15);
	step(63, b, c, d, a, f, 0xeb86d391, w[9], 21);

	digest[0] += a;
	digest[1] += b;
	digest[2] += c;
	digest[3] += d;
}

static inline void hash_init_digest(MD5_WORD_T * digest)
{
	static const MD5_WORD_T hash_initial_digest[MD5_DIGEST_NWORDS] =
	    { MD5_INITIAL_DIGEST };
	memcpy_fixedlen(digest, hash_initial_digest, sizeof(hash_initial_digest));
}

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};
struct slver md5_ctx_mgr_init_base_slver_0000018f;
struct slver md5_ctx_mgr_init_base_slver = { 0x018f, 0x00, 0x00 };

struct slver md5_ctx_mgr_submit_base_slver_00000190;
struct slver md5_ctx_mgr_submit_base_slver = { 0x0190, 0x00, 0x00 };

struct slver md5_ctx_mgr_flush_base_slver_00000191;
struct slver md5_ctx_mgr_flush_base_slver = { 0x0191, 0x00, 0x00 };
