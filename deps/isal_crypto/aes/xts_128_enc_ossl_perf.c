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
#include <stdlib.h>		// for rand
#include <string.h>		// for memcmp
#include "aes_xts.h"
#include "test.h"

#include <openssl/evp.h>

//#define CACHED_TEST
#ifdef CACHED_TEST
// Cached test, loop many times over small dataset
# define TEST_LEN     8*1024
# define TEST_LOOPS   400000
# define TEST_TYPE_STR "_warm"
#else
// Uncached test.  Pull from large mem base.
# define GT_L3_CACHE  32*1024*1024	/* some number > last level cache */
# define TEST_LEN     (2 * GT_L3_CACHE)
# define TEST_LOOPS   50
# define TEST_TYPE_STR "_cold"
#endif

#define TEST_MEM TEST_LEN

void xts128_mk_rand_data(unsigned char *k1, unsigned char *k2, unsigned char *k3,
			 unsigned char *p, int n)
{
	int i;
	for (i = 0; i < 16; i++) {
		*k1++ = rand();
		*k2++ = rand();
		*k3++ = rand();
	}
	for (i = 0; i < n; i++)
		*p++ = rand();

}

static inline
    int openssl_aes_128_xts_enc(EVP_CIPHER_CTX * ctx, unsigned char *key, unsigned char *iv,
				int len, unsigned char *pt, unsigned char *ct)
{
	int outlen, tmplen;
	if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_xts(), NULL, key, iv))
		printf("\n ERROR!! \n");
	if (!EVP_EncryptUpdate(ctx, ct, &outlen, (const unsigned char *)pt, len))
		printf("\n ERROR!! \n");
	if (!EVP_EncryptFinal_ex(ctx, ct + outlen, &tmplen))
		printf("\n ERROR!! \n");

	return 0;
}

int main(void)
{
	int i;

	unsigned char key1[16], key2[16], tinit[16];
	unsigned char *pt, *ct, *refct;
	struct perf start, stop;
	unsigned char keyssl[32];	/* SSL takes both keys together */

	/* Initialise our cipher context, which can use same input vectors */
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	printf("aes_xts_128_enc_perf:\n");

	pt = malloc(TEST_LEN);
	ct = malloc(TEST_LEN);
	refct = malloc(TEST_LEN);

	if (NULL == pt || NULL == ct || NULL == refct) {
		printf("malloc of testsize failed\n");
		return -1;
	}

	xts128_mk_rand_data(key1, key2, tinit, pt, TEST_LEN);

	/* Set up key for the SSL engine */
	for (i = 0; i < 16; i++) {
		keyssl[i] = key1[i];
		keyssl[i + 16] = key2[i];
	}

	/* Encrypt and compare output */
	XTS_AES_128_enc(key2, key1, tinit, TEST_LEN, pt, ct);
	openssl_aes_128_xts_enc(ctx, keyssl, tinit, TEST_LEN, pt, refct);
	if (memcmp(ct, refct, TEST_LEN)) {
		printf("ISA-L and OpenSSL results don't match\n");
		return -1;
	}

	/* Time ISA-L encryption */
	perf_start(&start);
	for (i = 0; i < TEST_LOOPS; i++)
		XTS_AES_128_enc(key2, key1, tinit, TEST_LEN, pt, ct);
	perf_stop(&stop);

	printf("aes_xts_128_enc" TEST_TYPE_STR ": ");
	perf_print(stop, start, (long long)TEST_LEN * i);

	/* Time OpenSSL encryption */
	perf_start(&start);
	for (i = 0; i < TEST_LOOPS; i++)
		openssl_aes_128_xts_enc(ctx, keyssl, tinit, TEST_LEN, pt, refct);
	perf_stop(&stop);

	printf("aes_xts_128_openssl_enc" TEST_TYPE_STR ": ");
	perf_print(stop, start, (long long)TEST_LEN * i);

	EVP_CIPHER_CTX_free(ctx);

	return 0;
}
