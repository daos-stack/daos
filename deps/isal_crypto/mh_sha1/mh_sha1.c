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
#include "mh_sha1_internal.h"

int mh_sha1_init(struct mh_sha1_ctx *ctx)
{
	uint32_t(*mh_sha1_segs_digests)[HASH_SEGS];
	uint32_t i;

	if (ctx == NULL)
		return MH_SHA1_CTX_ERROR_NULL;

	memset(ctx, 0, sizeof(*ctx));

	mh_sha1_segs_digests = (uint32_t(*)[HASH_SEGS]) ctx->mh_sha1_interim_digests;
	for (i = 0; i < HASH_SEGS; i++) {
		mh_sha1_segs_digests[0][i] = MH_SHA1_H0;
		mh_sha1_segs_digests[1][i] = MH_SHA1_H1;
		mh_sha1_segs_digests[2][i] = MH_SHA1_H2;
		mh_sha1_segs_digests[3][i] = MH_SHA1_H3;
		mh_sha1_segs_digests[4][i] = MH_SHA1_H4;
	}

	return MH_SHA1_CTX_ERROR_NONE;
}

#if (!defined(NOARCH)) && (defined(__i386__) || defined(__x86_64__) \
	|| defined( _M_X64) || defined(_M_IX86))
/***************mh_sha1_update***********/
// mh_sha1_update_sse.c
#define MH_SHA1_UPDATE_FUNCTION mh_sha1_update_sse
#define MH_SHA1_BLOCK_FUNCTION	mh_sha1_block_sse
#include "mh_sha1_update_base.c"
#undef MH_SHA1_UPDATE_FUNCTION
#undef MH_SHA1_BLOCK_FUNCTION

// mh_sha1_update_avx.c
#define MH_SHA1_UPDATE_FUNCTION mh_sha1_update_avx
#define MH_SHA1_BLOCK_FUNCTION	mh_sha1_block_avx
#include "mh_sha1_update_base.c"
#undef MH_SHA1_UPDATE_FUNCTION
#undef MH_SHA1_BLOCK_FUNCTION

// mh_sha1_update_avx2.c
#define MH_SHA1_UPDATE_FUNCTION mh_sha1_update_avx2
#define MH_SHA1_BLOCK_FUNCTION	mh_sha1_block_avx2
#include "mh_sha1_update_base.c"
#undef MH_SHA1_UPDATE_FUNCTION
#undef MH_SHA1_BLOCK_FUNCTION

/***************mh_sha1_finalize AND mh_sha1_tail***********/
// mh_sha1_tail is used to calculate the last incomplete src data block
// mh_sha1_finalize is a mh_sha1_ctx wrapper of mh_sha1_tail

// mh_sha1_finalize_sse.c and mh_sha1_tail_sse.c
#define MH_SHA1_FINALIZE_FUNCTION	mh_sha1_finalize_sse
#define MH_SHA1_TAIL_FUNCTION		mh_sha1_tail_sse
#define MH_SHA1_BLOCK_FUNCTION		mh_sha1_block_sse
#include "mh_sha1_finalize_base.c"
#undef MH_SHA1_FINALIZE_FUNCTION
#undef MH_SHA1_TAIL_FUNCTION
#undef MH_SHA1_BLOCK_FUNCTION

// mh_sha1_finalize_avx.c and mh_sha1_tail_avx.c
#define MH_SHA1_FINALIZE_FUNCTION	mh_sha1_finalize_avx
#define MH_SHA1_TAIL_FUNCTION		mh_sha1_tail_avx
#define MH_SHA1_BLOCK_FUNCTION		mh_sha1_block_avx
#include "mh_sha1_finalize_base.c"
#undef MH_SHA1_FINALIZE_FUNCTION
#undef MH_SHA1_TAIL_FUNCTION
#undef MH_SHA1_BLOCK_FUNCTION

// mh_sha1_finalize_avx2.c and mh_sha1_tail_avx2.c
#define MH_SHA1_FINALIZE_FUNCTION	mh_sha1_finalize_avx2
#define MH_SHA1_TAIL_FUNCTION		mh_sha1_tail_avx2
#define MH_SHA1_BLOCK_FUNCTION		mh_sha1_block_avx2
#include "mh_sha1_finalize_base.c"
#undef MH_SHA1_FINALIZE_FUNCTION
#undef MH_SHA1_TAIL_FUNCTION
#undef MH_SHA1_BLOCK_FUNCTION

/***************version info***********/

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};
// Version info
struct slver mh_sha1_init_slver_00000271;
struct slver mh_sha1_init_slver = { 0x0271, 0x00, 0x00 };

// mh_sha1_update version info
struct slver mh_sha1_update_sse_slver_00000274;
struct slver mh_sha1_update_sse_slver = { 0x0274, 0x00, 0x00 };

struct slver mh_sha1_update_avx_slver_02000276;
struct slver mh_sha1_update_avx_slver = { 0x0276, 0x00, 0x02 };

struct slver mh_sha1_update_avx2_slver_04000278;
struct slver mh_sha1_update_avx2_slver = { 0x0278, 0x00, 0x04 };

// mh_sha1_finalize version info
struct slver mh_sha1_finalize_sse_slver_00000275;
struct slver mh_sha1_finalize_sse_slver = { 0x0275, 0x00, 0x00 };

struct slver mh_sha1_finalize_avx_slver_02000277;
struct slver mh_sha1_finalize_avx_slver = { 0x0277, 0x00, 0x02 };

struct slver mh_sha1_finalize_avx2_slver_04000279;
struct slver mh_sha1_finalize_avx2_slver = { 0x0279, 0x00, 0x04 };

#endif
