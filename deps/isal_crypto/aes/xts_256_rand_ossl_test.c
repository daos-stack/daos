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

#include "aes_xts.h"
#include <stdlib.h>
#include <openssl/evp.h>

#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif
#ifndef RANDOMS
# define RANDOMS  128
#endif
#define TEST_LOOPS  128
#define TEST_LEN    (1024*1024)
#define LENGTH_SCAN (2*1024)

/* Generates random data for keys, tweak and plaintext */
void xts256_mk_rand_data(unsigned char *k1, unsigned char *k2, unsigned char *t,
			 unsigned char *p, int n)
{
	int i;
	for (i = 0; i < 32; i++) {
		*k1++ = rand();
		*k2++ = rand();
	}
	for (i = 0; i < 16; i++)
		*t++ = rand();

	for (i = 0; i < n; i++)
		*p++ = rand();

}

/* Wrapper for OpenSSL EVP AES-XTS 256 encryption */
static inline
    int openssl_aes_256_xts_enc(EVP_CIPHER_CTX * ctx, unsigned char *key, unsigned char *iv,
				int len, unsigned char *pt, unsigned char *ct)
{
	int outlen, tmplen;
	if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_xts(), NULL, key, iv)
	    || (!EVP_EncryptUpdate(ctx, ct, &outlen, (const unsigned char *)pt, len))
	    || (!EVP_EncryptFinal_ex(ctx, ct + outlen, &tmplen))) {
		printf("\n Error in openssl encoding of %d bytes\n", len);
		return 1;
	}
	return 0;
}

/* Wrapper for OpenSSL EVP AES-XTS 256 decryption */
static inline
    int openssl_aes_256_xts_dec(EVP_CIPHER_CTX * ctx, unsigned char *key, unsigned char *iv,
				int len, unsigned char *ct, unsigned char *dt)
{
	int outlen, tmplen;
	if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_xts(), NULL, key, iv)
	    || (!EVP_DecryptUpdate(ctx, dt, &outlen, (const unsigned char *)ct, len))
	    || (!EVP_DecryptFinal_ex(ctx, dt + outlen, &tmplen))) {
		printf("\n Error in openssl decoding of %d bytes\n", len);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{

	unsigned char key1[32], key2[32], tinit[16];
	unsigned char *pt, *ct, *dt, *refct, *refdt;
	unsigned char keyssl[64];	/* SSL takes both keys together */
	int i, j, k, ret;
	int seed;

	if (argc == 1)
		seed = TEST_SEED;
	else
		seed = atoi(argv[1]);

	srand(seed);
	printf("SEED: %d\n", seed);

	/* Initialise our cipher context, which can use same input vectors */
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();
	EVP_CIPHER_CTX_init(ctx);

	/* Allocate space for input and output buffers */
	pt = malloc(TEST_LEN);
	ct = malloc(TEST_LEN);
	dt = malloc(TEST_LEN);
	refct = malloc(TEST_LEN);
	refdt = malloc(TEST_LEN);

	if (NULL == pt || NULL == ct || NULL == dt || NULL == refct || NULL == refdt) {
		printf("malloc of testsize failed\n");
		return -1;
	}

	/**************************** LENGTH SCAN TEST *************************/
	printf("aes_xts_256_rand_ossl test, %d sets of various length: ", 2 * 1024);

	xts256_mk_rand_data(key1, key2, tinit, pt, TEST_LEN);

	/* Set up key for the SSL engine */
	for (k = 0; k < 32; k++) {
		keyssl[k] = key1[k];
		keyssl[k + 32] = key2[k];
	}

	for (ret = 0, i = 16; ret == 0 && i < LENGTH_SCAN; i++) {

		/* Encrypt using each method */
		XTS_AES_256_enc(key2, key1, tinit, i, pt, ct);
		ret |= openssl_aes_256_xts_enc(ctx, keyssl, tinit, i, pt, refct);

		// Compare
		for (ret = 0, j = 0; j < i && ret == 0; j++) {
			if (ct[j] != refct[j])
				ret = 1;
		}
		if (ret)
			printf(" XTS_AES_256_enc size=%d failed at byte %d!\n", i, j);

		/* Decrypt using each method */
		XTS_AES_256_dec(key2, key1, tinit, i, ct, dt);
		ret |= openssl_aes_256_xts_dec(ctx, keyssl, tinit, i, refct, refdt);

		for (k = 0, j = 0; j < TEST_LEN && ret == 0; j++) {
			if (dt[j] != refdt[j])
				ret = 1;
		}
		if (ret)
			printf(" XTS_AES_256_dec size=%d failed at byte %d!\n", i, j);
		if (0 == i % (LENGTH_SCAN / 16))
			printf(".");
		fflush(0);
	}
	if (ret)
		return -1;
	printf("Pass\n");

	/**************************** FIXED LENGTH TEST *************************/
	printf("aes_xts_256_rand_ossl test, %d sets of length %d: ", TEST_LOOPS, TEST_LEN);

	/* Loop over the vectors */
	for (i = 0; i < TEST_LOOPS; i++) {

		xts256_mk_rand_data(key1, key2, tinit, pt, TEST_LEN);

		/* Set up key for the SSL engine */
		for (k = 0; k < 32; k++) {
			keyssl[k] = key1[k];
			keyssl[k + 32] = key2[k];
		}

		/* Encrypt using each method */
		XTS_AES_256_enc(key2, key1, tinit, TEST_LEN, pt, ct);
		if (openssl_aes_256_xts_enc(ctx, keyssl, tinit, TEST_LEN, pt, refct))
			return -1;

		// Carry out comparison of the calculated ciphertext with
		// the reference
		for (j = 0; j < TEST_LEN; j++) {

			if (ct[j] != refct[j]) {
				printf("XTS_AES_256_enc failed at byte %d! \n", j);
				return -1;
			}
		}

		/* Decrypt using each method */
		XTS_AES_256_dec(key2, key1, tinit, TEST_LEN, ct, dt);
		if (openssl_aes_256_xts_dec(ctx, keyssl, tinit, TEST_LEN, refct, refdt))
			return -1;

		for (j = 0; j < TEST_LEN; j++) {

			if (dt[j] != refdt[j]) {
				printf("XTS_AES_256_dec failed at byte %d! \n", j);
				return -1;
			}
		}
		if (0 == i % (TEST_LOOPS / 16))
			printf(".");
		fflush(0);
	}
	printf("Pass\n");

	/**************************** RANDOM LENGTH TEST *************************/
	printf("aes_xts_256_rand_ossl test, %d sets of random lengths: ", RANDOMS);

	/* Run tests with random size */

	unsigned int rand_len, t;

	for (t = 0; t < RANDOMS; t++) {

		rand_len = rand() % (TEST_LEN);
		rand_len = rand_len < 16 ? 16 : rand_len;
		xts256_mk_rand_data(key1, key2, tinit, pt, rand_len);

		/* Set up key for the SSL engine */
		for (k = 0; k < 32; k++) {
			keyssl[k] = key1[k];
			keyssl[k + 32] = key2[k];
		}

		/* Encrypt using each method */
		XTS_AES_256_enc(key2, key1, tinit, rand_len, pt, ct);
		if (openssl_aes_256_xts_enc(ctx, keyssl, tinit, rand_len, pt, refct))
			return -1;

		/* Carry out comparison of the calculated ciphertext with
		 * the reference
		 */
		for (j = 0; j < rand_len; j++) {

			if (ct[j] != refct[j]) {
				printf("XTS_AES_256_enc failed at byte %d! \n", j);
				return -1;
			}
		}

		/* Decrypt using each method */
		XTS_AES_256_dec(key2, key1, tinit, rand_len, ct, dt);
		if (openssl_aes_256_xts_dec(ctx, keyssl, tinit, rand_len, refct, refdt))
			return -1;

		for (j = 0; j < rand_len; j++) {

			if (dt[j] != refdt[j]) {
				printf("XTS_AES_256_dec failed at byte %d! \n", j);
				return -1;
			}
		}
		if (0 == t % (RANDOMS / 16))
			printf(".");
		fflush(0);
	}

	EVP_CIPHER_CTX_free(ctx);

	printf("Pass\n");

	printf("aes_xts_256_rand_ossl: All tests passed\n");

	return 0;
}
