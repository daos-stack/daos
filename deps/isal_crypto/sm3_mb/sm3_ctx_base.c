/**********************************************************************
  Copyright(c) 2011-2019 Intel Corporation All rights reserved.

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
#include "sm3_mb.h"
#include "memcpy_inline.h"

#ifdef _MSC_VER
#include <intrin.h>
#define inline __inline
#endif

#define rol32(x, r) (((x)<<(r)) | ((x)>>(32-(r))))

static void sm3_init(SM3_HASH_CTX * ctx, const void *buffer, uint32_t len);
static uint32_t sm3_update(SM3_HASH_CTX * ctx, const void *buffer, uint32_t len);
static void sm3_final(SM3_HASH_CTX * ctx, uint32_t remain_len);
static void sm3_single(const volatile void *data, uint32_t digest[]);
static inline void hash_init_digest(SM3_WORD_T * digest);

static inline uint32_t byteswap32(uint32_t x)
{
	return (x >> 24) | (x >> 8 & 0xff00) | (x << 8 & 0xff0000) | (x << 24);
}

static inline uint32_t P0(uint32_t X)
{
	return (X ^ (rol32(X, 9)) ^ (rol32(X, 17)));
}

static inline uint32_t P1(uint32_t X)
{
	return (X ^ (rol32(X, 15)) ^ (rol32(X, 23)));
}

static inline uint32_t sm3_ff(int j, uint32_t x, uint32_t y, uint32_t z)
{
	return j < 16 ? (x ^ y ^ z) : ((x & y) | (x & z) | (y & z));
}

static inline uint32_t sm3_gg(int j, uint32_t x, uint32_t y, uint32_t z)
{
	return j < 16 ? (x ^ y ^ z) : ((x & y) | ((~x) & z));
}

static inline void sm3_message_schedule(uint32_t bi[], volatile uint32_t W[],
					volatile uint32_t W_B[])
{
	int j;
	volatile uint32_t tmp;

	for (j = 0; j <= 15; j++) {
		W[j] = byteswap32(bi[j]);
	}

	for (; j <= 67; j++) {
		tmp = W[j - 16] ^ W[j - 9] ^ rol32(W[j - 3], 15);
		W[j] = P1(tmp) ^ (rol32(W[j - 13], 7)) ^ W[j - 6];
	}

	for (j = 0; j < 64; j++) {
		W_B[j] = W[j] ^ W[j + 4];
	}

	tmp = 0;
}

static inline void sm3_compress_step_func(int j, volatile uint32_t * a_p,
					  volatile uint32_t * b_p, volatile uint32_t * c_p,
					  volatile uint32_t * d_p, volatile uint32_t * e_p,
					  volatile uint32_t * f_p, volatile uint32_t * g_p,
					  volatile uint32_t * h_p, volatile uint32_t W[],
					  volatile uint32_t W_B[])
{
	volatile uint32_t SS1, SS2, TT1, TT2;
	uint32_t T = j < 16 ? 0x79cc4519 : 0x7a879d8a;

	SS1 = rol32(rol32(*a_p, 12) + *e_p + rol32(T, (j % 32)), 7);
	SS2 = SS1 ^ rol32(*a_p, 12);
	TT1 = sm3_ff(j, *a_p, *b_p, *c_p) + *d_p + SS2 + W_B[j];
	TT2 = sm3_gg(j, *e_p, *f_p, *g_p) + *h_p + SS1 + W[j];
	*d_p = *c_p;
	*c_p = rol32(*b_p, 9);
	*b_p = *a_p;
	*a_p = TT1;
	*h_p = *g_p;
	*g_p = rol32(*f_p, 19);
	*f_p = *e_p;
	*e_p = P0(TT2);

	SS1 = 0;
	SS2 = 0;
	TT1 = 0;
	TT2 = 0;
}

void sm3_ctx_mgr_init_base(SM3_HASH_CTX_MGR * mgr)
{
}

SM3_HASH_CTX *sm3_ctx_mgr_submit_base(SM3_HASH_CTX_MGR * mgr, SM3_HASH_CTX * ctx,
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
		if (len % SM3_BLOCK_SIZE != 0) {
			ctx->error = HASH_CTX_ERROR_INVALID_FLAGS;
			return ctx;
		}
		sm3_init(ctx, buffer, len);
		sm3_update(ctx, buffer, len);
	}

	if (flags == HASH_UPDATE) {
		if (len % SM3_BLOCK_SIZE != 0) {
			ctx->error = HASH_CTX_ERROR_INVALID_FLAGS;
			return ctx;
		}
		sm3_update(ctx, buffer, len);
	}

	if (flags == HASH_LAST) {
		remain_len = sm3_update(ctx, buffer, len);
		sm3_final(ctx, remain_len);
	}

	if (flags == HASH_ENTIRE) {
		sm3_init(ctx, buffer, len);
		remain_len = sm3_update(ctx, buffer, len);
		sm3_final(ctx, remain_len);
	}

	return ctx;
}

SM3_HASH_CTX *sm3_ctx_mgr_flush_base(SM3_HASH_CTX_MGR * mgr)
{
	return NULL;
}

static void sm3_init(SM3_HASH_CTX * ctx, const void *buffer, uint32_t len)
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

static uint32_t sm3_update(SM3_HASH_CTX * ctx, const void *buffer, uint32_t len)
{
	uint32_t remain_len = len;
	uint32_t *digest = ctx->job.result_digest;

	while (remain_len >= SM3_BLOCK_SIZE) {
		sm3_single(buffer, digest);
		buffer = (void *)((uint8_t *) buffer + SM3_BLOCK_SIZE);
		remain_len -= SM3_BLOCK_SIZE;
		ctx->total_length += SM3_BLOCK_SIZE;
	}

	ctx->incoming_buffer = buffer;
	return remain_len;
}

static void sm3_final(SM3_HASH_CTX * ctx, uint32_t remain_len)
{
	const void *buffer = ctx->incoming_buffer;
	uint32_t i = remain_len;
	uint32_t j;
	volatile uint8_t buf[2 * SM3_BLOCK_SIZE] = { 0 };
	uint32_t *digest = ctx->job.result_digest;
	union {
		uint64_t uint;
		uint8_t uchar[8];
	} convert;
	volatile uint8_t *p;

	ctx->total_length += i;
	memcpy((void *)buf, buffer, i);
	buf[i++] = 0x80;

	i = (i > SM3_BLOCK_SIZE - SM3_PADLENGTHFIELD_SIZE ?
	     2 * SM3_BLOCK_SIZE : SM3_BLOCK_SIZE);

	convert.uint = 8 * ctx->total_length;
	p = buf + i - 8;
	for (j = 0; j < 8; j++) {
		p[j] = convert.uchar[7 - j];
	}

	sm3_single(buf, digest);
	if (i == 2 * SM3_BLOCK_SIZE) {
		sm3_single(buf + SM3_BLOCK_SIZE, digest);
	}

	/* convert to small-endian for words */
	for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
		digest[j] = byteswap32(digest[j]);
	}

	ctx->status = HASH_CTX_STS_COMPLETE;
	memset((void *)buf, 0, sizeof(buf));
	p = NULL;
}

static void sm3_single(const volatile void *data, uint32_t digest[])
{
	volatile uint32_t a, b, c, d, e, f, g, h;
	volatile uint32_t W[68], W_bar[64];
	int j;

	a = digest[0];
	b = digest[1];
	c = digest[2];
	d = digest[3];
	e = digest[4];
	f = digest[5];
	g = digest[6];
	h = digest[7];

	sm3_message_schedule((uint32_t *) data, W, W_bar);
	for (j = 0; j < 64; j++) {
		sm3_compress_step_func(j, &a, &b, &c, &d, &e, &f, &g, &h, W, W_bar);
	}

	digest[0] ^= a;
	digest[1] ^= b;
	digest[2] ^= c;
	digest[3] ^= d;
	digest[4] ^= e;
	digest[5] ^= f;
	digest[6] ^= g;
	digest[7] ^= h;

	memset((void *)W, 0, sizeof(W));
	memset((void *)W_bar, 0, sizeof(W_bar));

	a = 0;
	b = 0;
	c = 0;
	d = 0;
	e = 0;
	f = 0;
	g = 0;
	h = 0;
}

static inline void hash_init_digest(SM3_WORD_T * digest)
{
	static const SM3_WORD_T hash_initial_digest[SM3_DIGEST_NWORDS] =
	    { SM3_INITIAL_DIGEST };
	memcpy_fixedlen(digest, hash_initial_digest, sizeof(hash_initial_digest));
}

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};
struct slver sm3_ctx_mgr_init_base_slver_0000;
struct slver sm3_ctx_mgr_init_base_slver = { 0x2303, 0x00, 0x00 };

struct slver sm3_ctx_mgr_submit_base_slver_0000;
struct slver sm3_ctx_mgr_submit_base_slver = { 0x2304, 0x00, 0x00 };

struct slver sm3_ctx_mgr_flush_base_slver_0000;
struct slver sm3_ctx_mgr_flush_base_slver = { 0x2305, 0x00, 0x00 };
