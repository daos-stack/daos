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
#include "sha512_mb.h"

typedef uint64_t DigestSHA512[SHA512_DIGEST_NWORDS];

#define MSGS 8
#define NUM_JOBS 1000

#define PSEUDO_RANDOM_NUM(seed) ((seed) * 5 + ((seed) * (seed)) / 64) % MSGS

static uint8_t msg1[] = "The quick brown fox jumps over the lazy dog";
static uint8_t msg2[] = "The quick brown fox jumps over the lazy dog.";
static uint8_t msg3[] = { 0x0a, 0x55, 0xdb, 0 };
static uint8_t msg4[] = { 0xba, 0xd7, 0xc6, 0x18, 0xf4, 0x5b, 0xe2, 0x07, 0x97, 0x5e, 0 };

static uint8_t msg5[] = {
	0xb1, 0x71, 0x5f, 0x78, 0x2f, 0xf0, 0x2c, 0x6b, 0x88, 0x93,
	0x7f, 0x05, 0x41, 0x16, 0
};

static uint8_t msg6[] = {
	0xc6, 0xa1, 0x70, 0x93, 0x65, 0x68, 0x65, 0x10, 0x20, 0xed,
	0xfe, 0x15, 0xdf, 0x80, 0x12, 0xac, 0xda, 0x8d, 0
};

static uint8_t msg7[] = {
	0xa8, 0xa3, 0x7d, 0xfc, 0x08, 0x3a, 0xd2, 0xf4, 0x7f, 0xff,
	0x46, 0x87, 0x38, 0xbf, 0x8b, 0x72, 0x8e, 0xb7, 0xf1, 0x90,
	0x7e, 0x42, 0x7f, 0xa1, 0x5c, 0xb4, 0x42, 0x4b, 0xc6, 0x85,
	0xe5, 0x5e, 0xd7, 0xb2, 0x82, 0x5c, 0x9c, 0x60, 0xb8, 0x39,
	0xcc, 0xc2, 0xfe, 0x5f, 0xb3, 0x3e, 0x36, 0xf5, 0x70, 0xcb,
	0x86, 0x61, 0x60, 0x9e, 0x63, 0x0b, 0xda, 0x05, 0xee, 0x64,
	0x1d, 0x93, 0x84, 0x28, 0x86, 0x7d, 0x90, 0xe0, 0x07, 0x44,
	0xa4, 0xaa, 0xd4, 0x94, 0xc9, 0x3c, 0x5f, 0x6d, 0x13, 0x27,
	0x87, 0x80, 0x78, 0x59, 0x0c, 0xdc, 0xe1, 0xe6, 0x47, 0xc9,
	0x82, 0x08, 0x18, 0xf4, 0x67, 0x64, 0x1f, 0xcd, 0x50, 0x8e,
	0x2f, 0x2e, 0xbf, 0xd0, 0xff, 0x3d, 0x4f, 0x27, 0x23, 0x93,
	0x47, 0x8f, 0x3b, 0x9e, 0x6f, 0x80, 0x6b, 0x43, 0
};

static uint8_t msg8[] = "";

static DigestSHA512 expResultDigest1 = {
	0x07e547d9586f6a73, 0xf73fbac0435ed769, 0x51218fb7d0c8d788, 0xa309d785436bbb64,
	0x2e93a252a954f239, 0x12547d1e8a3b5ed6, 0xe1bfd7097821233f, 0xa0538f3db854fee6
};

static DigestSHA512 expResultDigest2 = {
	0x91ea1245f20d46ae, 0x9a037a989f54f1f7, 0x90f0a47607eeb8a1, 0x4d12890cea77a1bb,
	0xc6c7ed9cf205e67b, 0x7f2b8fd4c7dfd3a7, 0xa8617e45f3c463d4, 0x81c7e586c39ac1ed
};

static DigestSHA512 expResultDigest3 = {
	0x7952585e5330cb24, 0x7d72bae696fc8a6b, 0x0f7d0804577e347d, 0x99bc1b11e52f3849,
	0x85a428449382306a, 0x89261ae143c2f3fb, 0x613804ab20b42dc0, 0x97e5bf4a96ef919b
};

static DigestSHA512 expResultDigest4 = {
	0x5886828959d1f822, 0x54068be0bd14b6a8, 0x8f59f534061fb203, 0x76a0541052dd3635,
	0xedf3c6f0ca3d0877, 0x5e13525df9333a21, 0x13c0b2af76515887, 0x529910b6c793c8a5
};

static DigestSHA512 expResultDigest5 = {
	0xee1a56ee78182ec4, 0x1d2c3ab33d4c4187, 0x1d437c5c1ca060ee, 0x9e219cb83689b4e5,
	0xa4174dfdab5d1d10, 0x96a31a7c8d3abda7, 0x5c1b5e6da97e1814, 0x901c505b0bc07f25
};

static DigestSHA512 expResultDigest6 = {
	0xc36c100cdb6c8c45, 0xb072f18256d63a66, 0xc9843acb4d07de62, 0xe0600711d4fbe64c,
	0x8cf314ec3457c903, 0x08147cb7ac7e4d07, 0x3ba10f0ced78ea72, 0x4a474b32dae71231
};

static DigestSHA512 expResultDigest7 = {
	0x8e1c91729be8eb40, 0x226f6c58a029380e, 0xf7edb9dc166a5c3c, 0xdbcefe90bd30d85c,
	0xb7c4b248e66abf0a, 0x3a4c842281299bef, 0x6db88858d9e5ab52, 0x44f70b7969e1c072
};

static DigestSHA512 expResultDigest8 = {
	0Xcf83e1357eefb8bd, 0Xf1542850d66d8007, 0Xd620e4050b5715dc, 0X83f4a921d36ce9ce,
	0X47d0d13c5d85f2b0, 0Xff8318d2877eec2f, 0X63b931bd47417a81, 0Xa538327af927da3e
};

static uint8_t *msgs[MSGS] = { msg1, msg2, msg3, msg4, msg5, msg6, msg7, msg8 };

static uint64_t *expResultDigest[MSGS] = { expResultDigest1, expResultDigest2,
	expResultDigest3, expResultDigest4, expResultDigest5, expResultDigest6,
	expResultDigest7, expResultDigest8
};

int main(void)
{
	SHA512_HASH_CTX_MGR *mgr = NULL;
	SHA512_HASH_CTX ctxpool[NUM_JOBS], *ctx = NULL;
	uint32_t i, j, k, t, checked = 0;
	uint64_t *good;

	posix_memalign((void *)&mgr, 16, sizeof(SHA512_HASH_CTX_MGR));
	sha512_ctx_mgr_init(mgr);

	// Init contexts before first use
	for (i = 0; i < MSGS; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}

	for (i = 0; i < MSGS; i++) {
		ctx = sha512_ctx_mgr_submit(mgr,
					    &ctxpool[i],
					    msgs[i], strlen((char *)msgs[i]), HASH_ENTIRE);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = expResultDigest[t];
			checked++;
			for (j = 0; j < SHA512_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %016lX, "
					       "should be %016lX\n", t, j,
					       ctxpool[t].job.result_digest[j], good[j]);
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
		ctx = sha512_ctx_mgr_flush(mgr);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = expResultDigest[t];
			checked++;
			for (j = 0; j < SHA512_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %016lX, "
					       "should be %016lX\n", t, j,
					       ctxpool[t].job.result_digest[j], good[j]);
					return -1;
				}
			}

			if (ctx->error) {
				printf("Something bad happened during the "
				       "submit. Error code: %d", ctx->error);
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

		ctx = sha512_ctx_mgr_submit(mgr,
					    &ctxpool[i],
					    msgs[j], strlen((char *)msgs[j]), HASH_ENTIRE);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = expResultDigest[k];
			checked++;
			for (j = 0; j < SHA512_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %016lX, "
					       "should be %016lX\n", t, j,
					       ctxpool[t].job.result_digest[j], good[j]);
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
		ctx = sha512_ctx_mgr_flush(mgr);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = expResultDigest[k];
			checked++;
			for (j = 0; j < SHA512_DIGEST_NWORDS; j++) {
				if (good[j] != ctxpool[t].job.result_digest[j]) {
					printf("Test %d, digest %d is %016lX, "
					       "should be %016lX\n", t, j,
					       ctxpool[t].job.result_digest[j], good[j]);
					return -1;
				}
			}

			if (ctx->error) {
				printf("Something bad happened during the"
				       " submit. Error code: %d", ctx->error);
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

	printf(" multibinary_sha512 test: Pass\n");

	return 0;
}
