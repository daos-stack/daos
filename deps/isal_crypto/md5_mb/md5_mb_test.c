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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md5_mb.h"

typedef uint32_t DigestMD5[MD5_DIGEST_NWORDS];

#define MSGS 13
#define NUM_JOBS 1000

#define PSEUDO_RANDOM_NUM(seed) ((seed) * 5 + ((seed) * (seed)) / 64) % MSGS

static uint8_t msg1[] = "Test vector from febooti.com";
static uint8_t msg2[] = "12345678901234567890" "12345678901234567890"
    "12345678901234567890" "12345678901234567890";
static uint8_t msg3[] = "";
static uint8_t msg4[] = "abcdefghijklmnopqrstuvwxyz";
static uint8_t msg5[] = "message digest";
static uint8_t msg6[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz0123456789";
static uint8_t msg7[] = "abc";
static uint8_t msg8[] = "a";

static uint8_t msg9[] = "";
static uint8_t msgA[] = "abcdefghijklmnopqrstuvwxyz";
static uint8_t msgB[] = "message digest";
static uint8_t msgC[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz0123456789";
static uint8_t msgD[] = "abc";

static DigestMD5 expResultDigest1 = { 0x61b60a50, 0xfbb76d3c, 0xf5620cd3, 0x0f3d57ff };
static DigestMD5 expResultDigest2 = { 0xa2f4ed57, 0x55c9e32b, 0x2eda49ac, 0x7ab60721 };
static DigestMD5 expResultDigest3 = { 0xd98c1dd4, 0x04b2008f, 0x980980e9, 0x7e42f8ec };
static DigestMD5 expResultDigest4 = { 0xd7d3fcc3, 0x00e49261, 0x6c49fb7d, 0x3be167ca };
static DigestMD5 expResultDigest5 = { 0x7d696bf9, 0x8d93b77c, 0x312f5a52, 0xd061f1aa };
static DigestMD5 expResultDigest6 = { 0x98ab74d1, 0xf5d977d2, 0x2c1c61a5, 0x9f9d419f };
static DigestMD5 expResultDigest7 = { 0x98500190, 0xb04fd23c, 0x7d3f96d6, 0x727fe128 };
static DigestMD5 expResultDigest8 = { 0xb975c10c, 0xa8b6f1c0, 0xe299c331, 0x61267769 };

static DigestMD5 expResultDigest9 = { 0xd98c1dd4, 0x04b2008f, 0x980980e9, 0x7e42f8ec };
static DigestMD5 expResultDigestA = { 0xd7d3fcc3, 0x00e49261, 0x6c49fb7d, 0x3be167ca };
static DigestMD5 expResultDigestB = { 0x7d696bf9, 0x8d93b77c, 0x312f5a52, 0xd061f1aa };
static DigestMD5 expResultDigestC = { 0x98ab74d1, 0xf5d977d2, 0x2c1c61a5, 0x9f9d419f };
static DigestMD5 expResultDigestD = { 0x98500190, 0xb04fd23c, 0x7d3f96d6, 0x727fe128 };

static uint8_t *msgs[MSGS] = { msg1, msg2, msg3, msg4, msg5, msg6, msg7, msg8, msg9,
	msgA, msgB, msgC, msgD
};

static uint32_t *expResultDigest[MSGS] = {
	expResultDigest1, expResultDigest2, expResultDigest3,
	expResultDigest4, expResultDigest5, expResultDigest6,
	expResultDigest7, expResultDigest8, expResultDigest9,
	expResultDigestA, expResultDigestB, expResultDigestC,
	expResultDigestD
};

int main(void)
{
	MD5_HASH_CTX_MGR *mgr = NULL;
	MD5_HASH_CTX ctxpool[NUM_JOBS], *ctx = NULL;
	uint32_t i, j, k, t, checked = 0;
	uint32_t *good;

	posix_memalign((void *)&mgr, 16, sizeof(MD5_HASH_CTX_MGR));
	md5_ctx_mgr_init(mgr);

	// Init contexts before first use
	for (i = 0; i < MSGS; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}

	for (i = 0; i < MSGS; i++) {
		ctx = md5_ctx_mgr_submit(mgr,
					 &ctxpool[i], msgs[i],
					 strlen((char *)msgs[i]), HASH_ENTIRE);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = expResultDigest[t];
			checked++;
			for (j = 0; j < MD5_DIGEST_NWORDS; j++) {
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
		ctx = md5_ctx_mgr_flush(mgr);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = expResultDigest[t];
			checked++;
			for (j = 0; j < MD5_DIGEST_NWORDS; j++) {
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
		ctx = md5_ctx_mgr_submit(mgr,
					 &ctxpool[i],
					 msgs[j], strlen((char *)msgs[j]), HASH_ENTIRE);
		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = expResultDigest[k];
			checked++;
			for (j = 0; j < MD5_DIGEST_NWORDS; j++) {
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
		ctx = md5_ctx_mgr_flush(mgr);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = expResultDigest[k];
			checked++;
			for (j = 0; j < MD5_DIGEST_NWORDS; j++) {
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

	printf(" multibinary_md5 test: Pass\n");

	return 0;
}
