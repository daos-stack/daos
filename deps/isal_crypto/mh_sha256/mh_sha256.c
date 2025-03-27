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

int mh_sha256_init(struct mh_sha256_ctx *ctx)
{
	uint32_t(*mh_sha256_segs_digests)[HASH_SEGS];
	uint32_t i;

	if (ctx == NULL)
		return MH_SHA256_CTX_ERROR_NULL;

	memset(ctx, 0, sizeof(*ctx));

	mh_sha256_segs_digests = (uint32_t(*)[HASH_SEGS]) ctx->mh_sha256_interim_digests;
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

	return MH_SHA256_CTX_ERROR_NONE;
}

#if (!defined(NOARCH)) && (defined(__i386__) || defined(__x86_64__) \
	|| defined( _M_X64) || defined(_M_IX86))
/***************mh_sha256_update***********/
// mh_sha256_update_sse.c
#define MH_SHA256_UPDATE_FUNCTION	mh_sha256_update_sse
#define MH_SHA256_BLOCK_FUNCTION	mh_sha256_block_sse
#include "mh_sha256_update_base.c"
#undef MH_SHA256_UPDATE_FUNCTION
#undef MH_SHA256_BLOCK_FUNCTION

// mh_sha256_update_avx.c
#define MH_SHA256_UPDATE_FUNCTION	mh_sha256_update_avx
#define MH_SHA256_BLOCK_FUNCTION	mh_sha256_block_avx
#include "mh_sha256_update_base.c"
#undef MH_SHA256_UPDATE_FUNCTION
#undef MH_SHA256_BLOCK_FUNCTION

// mh_sha256_update_avx2.c
#define MH_SHA256_UPDATE_FUNCTION	mh_sha256_update_avx2
#define MH_SHA256_BLOCK_FUNCTION	mh_sha256_block_avx2
#include "mh_sha256_update_base.c"
#undef MH_SHA256_UPDATE_FUNCTION
#undef MH_SHA256_BLOCK_FUNCTION

/***************mh_sha256_finalize AND mh_sha256_tail***********/
// mh_sha256_tail is used to calculate the last incomplete src data block
// mh_sha256_finalize is a mh_sha256_ctx wrapper of mh_sha256_tail

// mh_sha256_finalize_sse.c and mh_sha256_tail_sse.c
#define MH_SHA256_FINALIZE_FUNCTION	mh_sha256_finalize_sse
#define MH_SHA256_TAIL_FUNCTION		mh_sha256_tail_sse
#define MH_SHA256_BLOCK_FUNCTION	mh_sha256_block_sse
#include "mh_sha256_finalize_base.c"
#undef MH_SHA256_FINALIZE_FUNCTION
#undef MH_SHA256_TAIL_FUNCTION
#undef MH_SHA256_BLOCK_FUNCTION

// mh_sha256_finalize_avx.c and mh_sha256_tail_avx.c
#define MH_SHA256_FINALIZE_FUNCTION	mh_sha256_finalize_avx
#define MH_SHA256_TAIL_FUNCTION		mh_sha256_tail_avx
#define MH_SHA256_BLOCK_FUNCTION	mh_sha256_block_avx
#include "mh_sha256_finalize_base.c"
#undef MH_SHA256_FINALIZE_FUNCTION
#undef MH_SHA256_TAIL_FUNCTION
#undef MH_SHA256_BLOCK_FUNCTION

// mh_sha256_finalize_avx2.c and mh_sha256_tail_avx2.c
#define MH_SHA256_FINALIZE_FUNCTION	mh_sha256_finalize_avx2
#define MH_SHA256_TAIL_FUNCTION		mh_sha256_tail_avx2
#define MH_SHA256_BLOCK_FUNCTION	mh_sha256_block_avx2
#include "mh_sha256_finalize_base.c"
#undef MH_SHA256_FINALIZE_FUNCTION
#undef MH_SHA256_TAIL_FUNCTION
#undef MH_SHA256_BLOCK_FUNCTION

/***************version info***********/

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};
// Version info
struct slver mh_sha256_init_slver_000002b1;
struct slver mh_sha256_init_slver = { 0x02b1, 0x00, 0x00 };

// mh_sha256_update version info
struct slver mh_sha256_update_sse_slver_000002b4;
struct slver mh_sha256_update_sse_slver = { 0x02b4, 0x00, 0x00 };

struct slver mh_sha256_update_avx_slver_020002b6;
struct slver mh_sha256_update_avx_slver = { 0x02b6, 0x00, 0x02 };

struct slver mh_sha256_update_avx2_slver_040002b8;
struct slver mh_sha256_update_avx2_slver = { 0x02b8, 0x00, 0x04 };

// mh_sha256_finalize version info
struct slver mh_sha256_finalize_sse_slver_000002b5;
struct slver mh_sha256_finalize_sse_slver = { 0x02b5, 0x00, 0x00 };

struct slver mh_sha256_finalize_avx_slver_020002b7;
struct slver mh_sha256_finalize_avx_slver = { 0x02b7, 0x00, 0x02 };

struct slver mh_sha256_finalize_avx2_slver_040002b9;
struct slver mh_sha256_finalize_avx2_slver = { 0x02b9, 0x00, 0x04 };
#endif
