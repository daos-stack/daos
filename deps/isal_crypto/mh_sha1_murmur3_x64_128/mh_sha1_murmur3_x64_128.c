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
#include "mh_sha1_murmur3_x64_128_internal.h"

int mh_sha1_murmur3_x64_128_init(struct mh_sha1_murmur3_x64_128_ctx *ctx, uint64_t murmur_seed)
{
	uint64_t *murmur3_x64_128_hash;
	uint32_t(*mh_sha1_segs_digests)[HASH_SEGS];
	uint32_t i;

	if (ctx == NULL)
		return MH_SHA1_MURMUR3_CTX_ERROR_NULL;

	memset(ctx, 0, sizeof(*ctx));

	mh_sha1_segs_digests = (uint32_t(*)[HASH_SEGS]) ctx->mh_sha1_interim_digests;
	for (i = 0; i < HASH_SEGS; i++) {
		mh_sha1_segs_digests[0][i] = MH_SHA1_H0;
		mh_sha1_segs_digests[1][i] = MH_SHA1_H1;
		mh_sha1_segs_digests[2][i] = MH_SHA1_H2;
		mh_sha1_segs_digests[3][i] = MH_SHA1_H3;
		mh_sha1_segs_digests[4][i] = MH_SHA1_H4;
	}

	murmur3_x64_128_hash = (uint64_t *) ctx->murmur3_x64_128_digest;
	murmur3_x64_128_hash[0] = murmur_seed;
	murmur3_x64_128_hash[1] = murmur_seed;

	return MH_SHA1_MURMUR3_CTX_ERROR_NONE;
}

void mh_sha1_murmur3_x64_128_block_base(const uint8_t * input_data,
					uint32_t mh_sha1_digests[SHA1_DIGEST_WORDS][HASH_SEGS],
					uint8_t frame_buffer[MH_SHA1_BLOCK_SIZE],
					uint32_t
					murmur3_x64_128_digests[MURMUR3_x64_128_DIGEST_WORDS],
					uint32_t num_blocks)
{

	mh_sha1_block_base(input_data, mh_sha1_digests, frame_buffer, num_blocks);

	murmur3_x64_128_block(input_data,
			      num_blocks * MH_SHA1_BLOCK_SIZE / MUR_BLOCK_SIZE,
			      murmur3_x64_128_digests);

	return;
}

#if (!defined(NOARCH)) && (defined(__i386__) || defined(__x86_64__) \
	|| defined( _M_X64) || defined(_M_IX86))
/***************mh_sha1_murmur3_x64_128_update***********/
// mh_sha1_murmur3_x64_128_update_sse.c
#define UPDATE_FUNCTION mh_sha1_murmur3_x64_128_update_sse
#define BLOCK_FUNCTION	mh_sha1_murmur3_x64_128_block_sse
#include "mh_sha1_murmur3_x64_128_update_base.c"
#undef UPDATE_FUNCTION
#undef BLOCK_FUNCTION

// mh_sha1_murmur3_x64_128_update_avx.c
#define UPDATE_FUNCTION mh_sha1_murmur3_x64_128_update_avx
#define BLOCK_FUNCTION	mh_sha1_murmur3_x64_128_block_avx
#include "mh_sha1_murmur3_x64_128_update_base.c"
#undef UPDATE_FUNCTION
#undef BLOCK_FUNCTION

// mh_sha1_murmur3_x64_128_update_avx2.c
#define UPDATE_FUNCTION mh_sha1_murmur3_x64_128_update_avx2
#define BLOCK_FUNCTION	mh_sha1_murmur3_x64_128_block_avx2
#include "mh_sha1_murmur3_x64_128_update_base.c"
#undef UPDATE_FUNCTION
#undef BLOCK_FUNCTION

/***************mh_sha1_murmur3_x64_128_finalize***********/
// mh_sha1_murmur3_x64_128_finalize_sse.c
#define FINALIZE_FUNCTION mh_sha1_murmur3_x64_128_finalize_sse
#define MH_SHA1_TAIL_FUNCTION mh_sha1_tail_sse
#include "mh_sha1_murmur3_x64_128_finalize_base.c"
#undef FINALIZE_FUNCTION
#undef MH_SHA1_TAIL_FUNCTION

// mh_sha1_murmur3_x64_128_finalize_avx.c
#define FINALIZE_FUNCTION mh_sha1_murmur3_x64_128_finalize_avx
#define MH_SHA1_TAIL_FUNCTION mh_sha1_tail_avx
#include "mh_sha1_murmur3_x64_128_finalize_base.c"
#undef FINALIZE_FUNCTION
#undef MH_SHA1_TAIL_FUNCTION

// mh_sha1_murmur3_x64_128_finalize_avx2.c
#define FINALIZE_FUNCTION mh_sha1_murmur3_x64_128_finalize_avx2
#define MH_SHA1_TAIL_FUNCTION mh_sha1_tail_avx2
#include "mh_sha1_murmur3_x64_128_finalize_base.c"
#undef FINALIZE_FUNCTION
#undef MH_SHA1_TAIL_FUNCTION

/***************version info***********/

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};

// Version info
struct slver mh_sha1_murmur3_x64_128_init_slver_00000251;
struct slver mh_sha1_murmur3_x64_128_init_slver = { 0x0251, 0x00, 0x00 };

// mh_sha1_murmur3_x64_128_update version info
struct slver mh_sha1_murmur3_x64_128_update_sse_slver_00000254;
struct slver mh_sha1_murmur3_x64_128_update_sse_slver = { 0x0254, 0x00, 0x00 };

struct slver mh_sha1_murmur3_x64_128_update_avx_slver_02000256;
struct slver mh_sha1_murmur3_x64_128_update_avx_slver = { 0x0256, 0x00, 0x02 };

struct slver mh_sha1_murmur3_x64_128_update_avx2_slver_04000258;
struct slver mh_sha1_murmur3_x64_128_update_avx2_slver = { 0x0258, 0x00, 0x04 };

// mh_sha1_murmur3_x64_128_finalize version info
struct slver mh_sha1_murmur3_x64_128_finalize_sse_slver_00000255;
struct slver mh_sha1_murmur3_x64_128_finalize_sse_slver = { 0x0255, 0x00, 0x00 };

struct slver mh_sha1_murmur3_x64_128_finalize_avx_slver_02000257;
struct slver mh_sha1_murmur3_x64_128_finalize_avx_slver = { 0x0257, 0x00, 0x02 };

struct slver mh_sha1_murmur3_x64_128_finalize_avx2_slver_04000259;
struct slver mh_sha1_murmur3_x64_128_finalize_avx2_slver = { 0x0259, 0x00, 0x04 };
#endif
