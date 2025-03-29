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
#define ISAL_UNIT_TEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sm3_mb.h"

typedef uint32_t digest_sm3[SM3_DIGEST_NWORDS];

#define MSGS 2
#define NUM_JOBS 1000

#define PSEUDO_RANDOM_NUM(seed) ((seed) * 5 + ((seed) * (seed)) / 64) % MSGS

static uint8_t msg1[] = "abc";
static uint8_t msg2[] = "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd";

/* small endian */
static digest_sm3 exp_result_digest1 = { 0x66c7f0f4, 0x62eeedd9, 0xd1f2d46b, 0xdc10e4e2,
	0x4167c487, 0x5cf2f7a2, 0x297da02b, 0x8f4ba8e0
};

/* small endian */
static digest_sm3 exp_result_digest2 = { 0xdebe9ff9, 0x2275b8a1, 0x38604889, 0xc18e5a4d,
	0x6fdb70e5, 0x387e5765, 0x293dcba3, 0x9c0c5732
};

static uint8_t *msgs[MSGS] = { msg1, msg2 };

static uint32_t *exp_result_digest[MSGS] = {
	exp_result_digest1, exp_result_digest2
};

static inline uint32_t byteswap32(uint32_t x)
{
	return (x >> 24) | (x >> 8 & 0xff00) | (x << 8 & 0xff0000) | (x << 24);
}

int main(void)
{
	SM3_HASH_CTX_MGR *mgr = NULL;
	SM3_HASH_CTX ctxpool[NUM_JOBS], *ctx = NULL;
	uint32_t i, j, k, t, checked = 0;
	uint32_t *good;

	posix_memalign((void *)&mgr, 16, sizeof(SM3_HASH_CTX_MGR));
	sm3_ctx_mgr_init(mgr);

	// Init contexts before first use
	for (i = 0; i < MSGS; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}

	for (i = 0; i < MSGS; i++) {
		ctx = sm3_ctx_mgr_submit(mgr,
					 &ctxpool[i],
					 msgs[i], strlen((char *)msgs[i]), HASH_ENTIRE);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = exp_result_digest[t];
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (byteswap32(good[j]) != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j],
					       byteswap32(good[j]));
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
			good = exp_result_digest[t];
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (byteswap32(good[j]) != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j],
					       byteswap32(good[j]));
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
					 msgs[j], strlen((char *)msgs[j]), HASH_ENTIRE);
		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = exp_result_digest[k];
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (byteswap32(good[j]) != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j],
					       byteswap32(good[j]));
					return -1;
				}
			}

			if (ctx->error) {
				printf("Something bad happened during the"
				       " submit. Error code: %d", ctx->error);
				return -1;
			}
		}
	}
	while (1) {
		ctx = sm3_ctx_mgr_flush(mgr);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = exp_result_digest[k];
			checked++;
			for (j = 0; j < SM3_DIGEST_NWORDS; j++) {
				if (byteswap32(good[j]) != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %08X, should be %08X\n",
					       t, j, ctxpool[t].job.result_digest[j],
					       byteswap32(good[j]));
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
