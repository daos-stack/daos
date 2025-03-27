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

#ifndef FINALIZE_FUNCTION
#include <stdlib.h>		// For NULL
#include "mh_sha1_murmur3_x64_128_internal.h"

#define FINALIZE_FUNCTION		mh_sha1_murmur3_x64_128_finalize_base
#define MH_SHA1_TAIL_FUNCTION		mh_sha1_tail_base
#define FINALIZE_FUNCTION_SLVER
#endif

#define MURMUR_BLOCK_FUNCTION		murmur3_x64_128_block
#define MURMUR_TAIL_FUNCTION		murmur3_x64_128_tail

int FINALIZE_FUNCTION(struct mh_sha1_murmur3_x64_128_ctx *ctx, void *mh_sha1_digest,
		      void *murmur3_x64_128_digest)
{
	uint8_t *partial_block_buffer, *murmur_tail_data;
	uint64_t partial_block_len, total_len;
	uint32_t(*mh_sha1_segs_digests)[HASH_SEGS];
	uint8_t *aligned_frame_buffer;

	if (ctx == NULL)
		return MH_SHA1_MURMUR3_CTX_ERROR_NULL;

	total_len = ctx->total_length;
	partial_block_len = total_len % MH_SHA1_BLOCK_SIZE;
	partial_block_buffer = ctx->partial_block_buffer;

	// Calculate murmur3 firstly
	// because mh_sha1 will change the partial_block_buffer
	// ( partial_block_buffer = n murmur3 blocks and 1 murmur3 tail)
	murmur_tail_data =
	    partial_block_buffer + partial_block_len - partial_block_len % MUR_BLOCK_SIZE;
	MURMUR_BLOCK_FUNCTION(partial_block_buffer, partial_block_len / MUR_BLOCK_SIZE,
			      ctx->murmur3_x64_128_digest);
	MURMUR_TAIL_FUNCTION(murmur_tail_data, total_len, ctx->murmur3_x64_128_digest);

	/* mh_sha1 final */
	aligned_frame_buffer = (uint8_t *) ALIGN_64(ctx->frame_buffer);
	mh_sha1_segs_digests = (uint32_t(*)[HASH_SEGS]) ctx->mh_sha1_interim_digests;

	MH_SHA1_TAIL_FUNCTION(partial_block_buffer, total_len, mh_sha1_segs_digests,
			      aligned_frame_buffer, ctx->mh_sha1_digest);

	/* Output  the digests of murmur3 and mh_sha1 */
	if (mh_sha1_digest != NULL) {
		((uint32_t *) mh_sha1_digest)[0] = ctx->mh_sha1_digest[0];
		((uint32_t *) mh_sha1_digest)[1] = ctx->mh_sha1_digest[1];
		((uint32_t *) mh_sha1_digest)[2] = ctx->mh_sha1_digest[2];
		((uint32_t *) mh_sha1_digest)[3] = ctx->mh_sha1_digest[3];
		((uint32_t *) mh_sha1_digest)[4] = ctx->mh_sha1_digest[4];
	}

	if (murmur3_x64_128_digest != NULL) {
		((uint32_t *) murmur3_x64_128_digest)[0] = ctx->murmur3_x64_128_digest[0];
		((uint32_t *) murmur3_x64_128_digest)[1] = ctx->murmur3_x64_128_digest[1];
		((uint32_t *) murmur3_x64_128_digest)[2] = ctx->murmur3_x64_128_digest[2];
		((uint32_t *) murmur3_x64_128_digest)[3] = ctx->murmur3_x64_128_digest[3];
	}

	return MH_SHA1_MURMUR3_CTX_ERROR_NONE;
}

#ifdef FINALIZE_FUNCTION_SLVER
struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};

 // Version info
struct slver mh_sha1_murmur3_x64_128_finalize_base_slver_0000025b;
struct slver mh_sha1_murmur3_x64_128_finalize_base_slver = { 0x025b, 0x00, 0x00 };
#endif
