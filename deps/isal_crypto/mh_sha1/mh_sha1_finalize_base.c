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

/*
 * mh_sha1_finalize_base.c contains the prototypes of mh_sha1_finalize_XXX
 * and mh_sha1_tail_XXX. Default definitions are base type which generates
 * mh_sha1_finalize_base and mh_sha1_tail_base. Other types are generated
 * through different predefined macros by mh_sha1.c.
 * mh_sha1_tail is used to calculate the last incomplete block of input
 * data. mh_sha1_finalize is the mh_sha1_ctx wrapper of mh_sha1_tail.
 */
#ifndef MH_SHA1_FINALIZE_FUNCTION
#include <string.h>
#include "mh_sha1_internal.h"

#define MH_SHA1_FINALIZE_FUNCTION	mh_sha1_finalize_base
#define MH_SHA1_TAIL_FUNCTION		mh_sha1_tail_base
#define MH_SHA1_BLOCK_FUNCTION		mh_sha1_block_base
#define MH_SHA1_FINALIZE_SLVER
#endif

void MH_SHA1_TAIL_FUNCTION(uint8_t * partial_buffer, uint32_t total_len,
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
		MH_SHA1_BLOCK_FUNCTION(partial_buffer, mh_sha1_segs_digests, frame_buffer, 1);
		//Padding the second block
		memset(partial_buffer, 0, MH_SHA1_BLOCK_SIZE);
	}
	//Padding the block
	len_in_bit = bswap64((uint64_t) total_len * 8);
	*(uint64_t *) (partial_buffer + MH_SHA1_BLOCK_SIZE - 8) = len_in_bit;
	MH_SHA1_BLOCK_FUNCTION(partial_buffer, mh_sha1_segs_digests, frame_buffer, 1);

	//Calculate multi-hash SHA1 digests (segment digests as input message)
	sha1_for_mh_sha1((uint8_t *) mh_sha1_segs_digests, digests,
			 4 * SHA1_DIGEST_WORDS * HASH_SEGS);

	return;
}

int MH_SHA1_FINALIZE_FUNCTION(struct mh_sha1_ctx *ctx, void *mh_sha1_digest)
{
	uint8_t *partial_block_buffer;
	uint64_t total_len;
	uint32_t(*mh_sha1_segs_digests)[HASH_SEGS];
	uint8_t *aligned_frame_buffer;

	if (ctx == NULL)
		return MH_SHA1_CTX_ERROR_NULL;

	total_len = ctx->total_length;
	partial_block_buffer = ctx->partial_block_buffer;

	/* mh_sha1 tail */
	aligned_frame_buffer = (uint8_t *) ALIGN_64(ctx->frame_buffer);
	mh_sha1_segs_digests = (uint32_t(*)[HASH_SEGS]) ctx->mh_sha1_interim_digests;

	MH_SHA1_TAIL_FUNCTION(partial_block_buffer, total_len, mh_sha1_segs_digests,
			      aligned_frame_buffer, ctx->mh_sha1_digest);

	/* Output the digests of mh_sha1 */
	if (mh_sha1_digest != NULL) {
		((uint32_t *) mh_sha1_digest)[0] = ctx->mh_sha1_digest[0];
		((uint32_t *) mh_sha1_digest)[1] = ctx->mh_sha1_digest[1];
		((uint32_t *) mh_sha1_digest)[2] = ctx->mh_sha1_digest[2];
		((uint32_t *) mh_sha1_digest)[3] = ctx->mh_sha1_digest[3];
		((uint32_t *) mh_sha1_digest)[4] = ctx->mh_sha1_digest[4];
	}

	return MH_SHA1_CTX_ERROR_NONE;
}

#ifdef MH_SHA1_FINALIZE_SLVER
struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};

// Version info
struct slver mh_sha1_finalize_base_slver_0000027b;
struct slver mh_sha1_finalize_base_slver = { 0x027b, 0x00, 0x00 };
#endif
