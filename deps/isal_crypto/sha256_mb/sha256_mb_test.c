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
#include "sha256_mb.h"

typedef uint32_t DigestSHA256[SHA256_DIGEST_NWORDS];

#define MSGS 7
#define NUM_JOBS 1000

#define PSEUDO_RANDOM_NUM(seed) ((seed) * 5 + ((seed) * (seed)) / 64) % MSGS

static uint8_t msg1[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static uint8_t msg2[] = "0123456789:;<=>?@ABCDEFGHIJKLMNO";
static uint8_t msg3[] =
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
    "0123456789:;<";
static uint8_t msg4[] =
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQR";
static uint8_t msg5[] =
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?";
static uint8_t msg6[] =
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX" "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTU";
static uint8_t msg7[] = "";

static DigestSHA256 expResultDigest1 = { 0x248D6A61, 0xD20638B8, 0xE5C02693, 0x0C3E6039,
	0xA33CE459, 0x64FF2167, 0xF6ECEDD4, 0x19DB06C1
};

static DigestSHA256 expResultDigest2 = { 0xD9C2E699, 0x586B948F, 0x4022C799, 0x4FFE14C6,
	0x3A4E8E31, 0x2EE2AEE1, 0xEBE51BED, 0x85705CFD
};

static DigestSHA256 expResultDigest3 = { 0xE3057651, 0x81295681, 0x7ECF1791, 0xFF9A1619,
	0xB2BC5CAD, 0x2AC00018, 0x92AE489C, 0x48DD10B3
};

static DigestSHA256 expResultDigest4 = { 0x0307DAA3, 0x7130A140, 0x270790F9, 0x95B71407,
	0x8EC752A6, 0x084EC1F3, 0xBD873D79, 0x3FF78383
};

static DigestSHA256 expResultDigest5 = { 0x679312F7, 0x2E18D599, 0x5F51BDC6, 0x4ED56AFD,
	0x9B5704D3, 0x4387E11C, 0xC2331089, 0x2CD45DAA
};

static DigestSHA256 expResultDigest6 = { 0x8B1767E9, 0x7BA7BBE5, 0xF9A6E8D9, 0x9996904F,
	0x3AF6562E, 0xA58AF438, 0x5D8D584B, 0x81C808CE
};

static DigestSHA256 expResultDigest7 = { 0xE3B0C442, 0x98FC1C14, 0x9AFBF4C8, 0x996FB924,
	0x27AE41E4, 0x649B934C, 0xA495991B, 0x7852B855
};

static uint8_t *msgs[MSGS] = { msg1, msg2, msg3, msg4, msg5, msg6, msg7 };

static uint32_t *expResultDigest[MSGS] = {
	expResultDigest1, expResultDigest2, expResultDigest3,
	expResultDigest4, expResultDigest5, expResultDigest6,
	expResultDigest7
};

int main(void)
{
	SHA256_HASH_CTX_MGR *mgr = NULL;
	SHA256_HASH_CTX ctxpool[NUM_JOBS], *ctx = NULL;
	uint32_t i, j, k, t, checked = 0;
	uint32_t *good;

	posix_memalign((void *)&mgr, 16, sizeof(SHA256_HASH_CTX_MGR));
	sha256_ctx_mgr_init(mgr);

	// Init contexts before first use
	for (i = 0; i < MSGS; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}

	for (i = 0; i < MSGS; i++) {
		ctx = sha256_ctx_mgr_submit(mgr,
					    &ctxpool[i],
					    msgs[i], strlen((char *)msgs[i]), HASH_ENTIRE);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = expResultDigest[t];
			checked++;
			for (j = 0; j < SHA256_DIGEST_NWORDS; j++) {
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
		ctx = sha256_ctx_mgr_flush(mgr);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			good = expResultDigest[t];
			checked++;
			for (j = 0; j < SHA256_DIGEST_NWORDS; j++) {
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
		ctx = sha256_ctx_mgr_submit(mgr,
					    &ctxpool[i],
					    msgs[j], strlen((char *)msgs[j]), HASH_ENTIRE);
		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = expResultDigest[k];
			checked++;
			for (j = 0; j < SHA256_DIGEST_NWORDS; j++) {
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
		ctx = sha256_ctx_mgr_flush(mgr);

		if (ctx) {
			t = (unsigned long)(ctx->user_data);
			k = PSEUDO_RANDOM_NUM(t);
			good = expResultDigest[k];
			checked++;
			for (j = 0; j < SHA256_DIGEST_NWORDS; j++) {
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

	printf(" multibinary_sha256 test: Pass\n");

	return 0;
}
