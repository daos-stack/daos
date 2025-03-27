/**********************************************************************
  Copyright(c) 2020 Arm Corporation All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Arm Corporation nor the names of its
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sm3_mb.h"

typedef struct {
	const char *msg;
	uint32_t resultDigest[SM3_DIGEST_NWORDS];
} TestData;

static TestData test_data[] = {
	{
	 .msg = "abc",
	 .resultDigest = {0xf4f0c766, 0xd9edee62, 0x6bd4f2d1, 0xe2e410dc,
			  0x87c46741, 0xa2f7f25c, 0x2ba07d29, 0xe0a84b8f}
	 },
	{
	 .msg = "abcdabcdabcdabcdabcdabcdabcdabcd" "abcdabcdabcdabcdabcdabcdabcdabcd",
	 .resultDigest = {0xf99fbede, 0xa1b87522, 0x89486038, 0x4d5a8ec1,
			  0xe570db6f, 0x65577e38, 0xa3cb3d29, 0x32570c9c}

	 },
	{
	 .msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
	 .resultDigest = {0xc56c9b63, 0x379e4de6, 0x92b190a3, 0xeaa14fdf,
			  0x74ab2007, 0xb992f67f, 0x664e8cf3, 0x058c7bad}
	 },

	{.msg = "0123456789:;<=>?@ABCDEFGHIJKLMNO",
	 .resultDigest = {0x076833d0, 0xd089ec39, 0xad857685, 0x8089797a,
			  0x9df9e8fd, 0x4126eb9a, 0xf38c22e8, 0x054bb846}},
	{
	 .msg =
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<",
	 .resultDigest = {0x6cb9d38e, 0x846ac99e, 0x6d05634b, 0x3fe1bb26,
			  0x90368c4b, 0xee8c4299, 0x08c0e96a, 0x2233cdc7}
	 },
	{
	 .msg =
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQR",
	 .resultDigest = {0x83758189, 0x050f14d1, 0x91d8a730, 0x4a2825e4,
			  0x11723273, 0x2114ee3f, 0x18cac172, 0xa9c5b07a}
	 },
	{
	 .msg =
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?",
	 .resultDigest = {0xb80f8aba, 0x55e96119, 0x851ac77b, 0xae31b3a5,
			  0x1333e764, 0xc86ac40d, 0x34878db1, 0x7da873f6},
	 },
	{
	 .msg =
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
	 "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTU",
	 .resultDigest = {0xbd5736a7, 0x55977d13, 0xa950c78a, 0x71eeb7cb,
			  0xe9ef0ba5, 0x95a9302e, 0x155e5c33, 0xad96ce3c}
	 },
	{
	 .msg = "",
	 .resultDigest = {0x831db21a, 0x7fa1cf55, 0x4819618e, 0x8f1ae831,
			  0xc7c8be22, 0x74fbfe28, 0xeb35d07e, 0x2baa8250}

	 },

};

#define MSGS sizeof(test_data)/sizeof(TestData)
#define NUM_JOBS 1000

#define PSEUDO_RANDOM_NUM(seed) ((seed) * 5 + ((seed) * (seed)) / 64) % MSGS

int main(void)
{

	SM3_HASH_CTX_MGR *mgr = NULL;
	SM3_HASH_CTX ctxpool[NUM_JOBS], *ctx = NULL;
	uint32_t i, j, k, t, checked = 0;
	uint32_t *good;
	int ret;
	ret = posix_memalign((void *)&mgr, 16, sizeof(SM3_HASH_CTX_MGR));
	if (ret) {
		printf("alloc error: Fail");
		return -1;
	}
	sm3_ctx_mgr_init(mgr);
	// Init contexts before first use
	for (i = 0; i < MSGS; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}

	for (i = 0; i < MSGS; i++) {
		ctx = sm3_ctx_mgr_submit(mgr,
					 &ctxpool[i], test_data[i].msg,
					 strlen((char *)test_data[i].msg), HASH_ENTIRE);
		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = test_data[t].resultDigest;
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j], good[j]);
					return -1;
				}
			}

			if (ctx->error) {
				printf("Something bad happened during the submit."
				       " Error code: %d", ctx->error);
				return -1;
			}

		}
	}

	while (1) {
		ctx = sm3_ctx_mgr_flush(mgr);
		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = test_data[t].resultDigest;
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j], good[j]);
					return -1;
				}
			}

			if (ctx->error) {
				printf("Something bad happened during the submit."
				       " Error code: %d", ctx->error);
				return -1;
			}
		} else {
			break;
		}
	}

	// do larger test in pseudo-random order

	// Init contexts before first use
	for (i = 0; i < NUM_JOBS; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}

	checked = 0;
	for (i = 0; i < NUM_JOBS; i++) {
		j = PSEUDO_RANDOM_NUM(i);
		ctx = sm3_ctx_mgr_submit(mgr,
					 &ctxpool[i],
					 test_data[j].msg, strlen((char *)test_data[j].msg),
					 HASH_ENTIRE);
		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = test_data[k].resultDigest;
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j], good[j]);
					return -1;
				}
			}

			if (ctx->error) {
				printf("Something bad happened during the"
				       " submit. Error code: %d", ctx->error);
				return -1;
			}

			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
		}
	}
	while (1) {
		ctx = sm3_ctx_mgr_flush(mgr);
		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = test_data[k].resultDigest;
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j], good[j]);
					return -1;
				}
			}

			if (ctx->error) {
				printf("Something bad happened during the submit."
				       " Error code: %d", ctx->error);
				return -1;
			}
		} else {
			break;
		}
	}

	if (checked != NUM_JOBS) {
		printf("only tested %d rather than %d\n", checked, NUM_JOBS);
		return -1;
	}

	printf(" multibinary_sm3 test: Pass\n");
	return 0;
}
