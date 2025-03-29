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

/*
 * mh_sha256_update_base.c contains the prototype of mh_sha256_update_XXX.
 * Default definitions are base type which generates mh_sha256_update_base.
 * Other types are generated through different predefined macros by mh_sha256.c.
 */
#ifndef MH_SHA256_UPDATE_FUNCTION
#include "mh_sha256_internal.h"
#include <string.h>

#define MH_SHA256_UPDATE_FUNCTION	mh_sha256_update_base
#define MH_SHA256_BLOCK_FUNCTION	mh_sha256_block_base
#define MH_SHA256_UPDATE_SLVER
#endif

int MH_SHA256_UPDATE_FUNCTION(struct mh_sha256_ctx *ctx, const void *buffer, uint32_t len)
{

	uint8_t *partial_block_buffer;
	uint64_t partial_block_len;
	uint64_t num_blocks;
	uint32_t(*mh_sha256_segs_digests)[HASH_SEGS];
	uint8_t *aligned_frame_buffer;
	const uint8_t *input_data = (const uint8_t *)buffer;

	if (ctx == NULL)
		return MH_SHA256_CTX_ERROR_NULL;

	if (len == 0)
		return MH_SHA256_CTX_ERROR_NONE;

	partial_block_len = ctx->total_length % MH_SHA256_BLOCK_SIZE;
	partial_block_buffer = ctx->partial_block_buffer;
	aligned_frame_buffer = (uint8_t *) ALIGN_64(ctx->frame_buffer);
	mh_sha256_segs_digests = (uint32_t(*)[HASH_SEGS]) ctx->mh_sha256_interim_digests;

	ctx->total_length += len;
	// No enough input data for mh_sha256 calculation
	if (len + partial_block_len < MH_SHA256_BLOCK_SIZE) {
		memcpy(partial_block_buffer + partial_block_len, input_data, len);
		return MH_SHA256_CTX_ERROR_NONE;
	}
	// mh_sha256 calculation for the previous partial block
	if (partial_block_len != 0) {
		memcpy(partial_block_buffer + partial_block_len, input_data,
		       MH_SHA256_BLOCK_SIZE - partial_block_len);
		//do one_block process
		MH_SHA256_BLOCK_FUNCTION(partial_block_buffer, mh_sha256_segs_digests,
					 aligned_frame_buffer, 1);
		input_data += MH_SHA256_BLOCK_SIZE - partial_block_len;
		len -= MH_SHA256_BLOCK_SIZE - partial_block_len;
		memset(partial_block_buffer, 0, MH_SHA256_BLOCK_SIZE);
	}
	// Calculate mh_sha256 for the current blocks
	num_blocks = len / MH_SHA256_BLOCK_SIZE;
	if (num_blocks > 0) {
		//do num_blocks process
		MH_SHA256_BLOCK_FUNCTION(input_data, mh_sha256_segs_digests,
					 aligned_frame_buffer, num_blocks);
		len -= num_blocks * MH_SHA256_BLOCK_SIZE;
		input_data += num_blocks * MH_SHA256_BLOCK_SIZE;
	}
	// Store the partial block
	if (len != 0) {
		memcpy(partial_block_buffer, input_data, len);
	}

	return MH_SHA256_CTX_ERROR_NONE;

}

#ifdef MH_SHA256_UPDATE_SLVER
struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};

// Version info
struct slver mh_sha256_update_base_slver_000002ba;
struct slver mh_sha256_update_base_slver = { 0x02ba, 0x00, 0x00 };
#endif
