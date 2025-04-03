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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sha1_mb.h"
#include "test.h"

// Test messages
#define TST_STR "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWX"
uint8_t msg1[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
uint8_t msg2[] = "0123456789:;<=>?@ABCDEFGHIJKLMNO";
uint8_t msg3[] = TST_STR TST_STR "0123456789:;<";
uint8_t msg4[] = TST_STR TST_STR TST_STR "0123456789:;<=>?@ABCDEFGHIJKLMNOPQR";
uint8_t msg5[] = TST_STR TST_STR TST_STR TST_STR TST_STR "0123456789:;<=>?";
uint8_t msg6[] =
    TST_STR TST_STR TST_STR TST_STR TST_STR TST_STR "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTU";
uint8_t msg7[] = "";

// Expected digests
uint32_t dgst1[] = { 0x84983E44, 0x1C3BD26E, 0xBAAE4AA1, 0xF95129E5, 0xE54670F1 };
uint32_t dgst2[] = { 0xB7C66452, 0x0FD122B3, 0x55D539F2, 0xA35E6FAA, 0xC2A5A11D };
uint32_t dgst3[] = { 0x127729B6, 0xA8B2F8A0, 0xA4DDC819, 0x08E1D8B3, 0x67CEEA55 };
uint32_t dgst4[] = { 0xFDDE2D00, 0xABD5B7A3, 0x699DE6F2, 0x3FF1D1AC, 0x3B872AC2 };
uint32_t dgst5[] = { 0xE7FCA85C, 0xA4AB3740, 0x6A180B32, 0x0B8D362C, 0x622A96E6 };
uint32_t dgst6[] = { 0x505B0686, 0xE1ACDF42, 0xB3588B5A, 0xB043D52C, 0x6D8C7444 };
uint32_t dgst7[] = { 0xDA39A3EE, 0x5E6B4B0D, 0x3255BFEF, 0x95601890, 0xAFD80709 };

uint8_t *msgs[] = { msg1, msg2, msg3, msg4, msg5, msg6, msg7 };
uint32_t *expected_digest[] = { dgst1, dgst2, dgst3, dgst4, dgst5, dgst6, dgst7 };

int check_job(uint32_t * ref, uint32_t * good, int words)
{
	int i;
	for (i = 0; i < words; i++)
		if (good[i] != ref[i])
			return 1;

	return 0;
}

#define MAX_MSGS 7

int main(void)
{
	SHA1_HASH_CTX_MGR *mgr = NULL;
	SHA1_HASH_CTX ctxpool[MAX_MSGS];
	SHA1_HASH_CTX *p_job;
	int i, checked = 0, failed = 0;
	int n = sizeof(msgs) / sizeof(msgs[0]);

	posix_memalign((void *)&mgr, 16, sizeof(SHA1_HASH_CTX_MGR));
	// Initialize multi-buffer manager
	sha1_ctx_mgr_init(mgr);

	for (i = 0; i < n; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)expected_digest[i];

		p_job = sha1_ctx_mgr_submit(mgr, &ctxpool[i], msgs[i],
					    strlen((char *)msgs[i]), HASH_ENTIRE);

		if (p_job) {	// If we have finished a job, process it
			checked++;
			failed +=
			    check_job(p_job->job.result_digest, p_job->user_data,
				      SHA1_DIGEST_NWORDS);
		}
	}

	// Finish remaining jobs
	while (NULL != (p_job = sha1_ctx_mgr_flush(mgr))) {
		checked++;
		failed +=
		    check_job(p_job->job.result_digest, p_job->user_data, SHA1_DIGEST_NWORDS);
	}

	printf("Example multi-buffer sha1 completed=%d, failed=%d\n", checked, failed);
	return failed;
}
