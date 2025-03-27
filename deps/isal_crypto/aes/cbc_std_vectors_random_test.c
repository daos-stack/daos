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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <aes_cbc.h>
#include "types.h"
#include "ossl_helper.h"
#include "cbc_std_vectors.h"

//define CBC_VECTORS_VERBOSE
//define CBC_VECTORS_EXTRA_VERBOSE

#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif
#ifndef RANDOMS
# define RANDOMS  100
#endif
#ifndef TEST_LEN
# define TEST_LEN  (8*1024*1024)
#endif
#ifndef PAGE_LEN
# define PAGE_LEN  (4*1024)
#endif
#ifndef MAX_UNALINED
# define MAX_UNALINED  (16)
#endif

static cbc_key_size const Ksize[] = { CBC_128_BITS, CBC_192_BITS, CBC_256_BITS };

typedef void (*aes_cbc_generic)(uint8_t * in,
				uint8_t * IV,
				uint8_t * keys, uint8_t * out, uint64_t len_bytes);

int OpenSslEnc(uint8_t k_len,
	       uint8_t * key, uint8_t * in, uint8_t * iv, uint8_t * out, uint64_t len_bytes)
{
	if (CBC_128_BITS == k_len) {
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" OpenSSL128 ");
#endif
		openssl_aes_128_cbc_enc(key, (uint8_t *) iv, len_bytes, in, out);
	} else if (CBC_192_BITS == k_len) {
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" OpenSSL192 ");
#endif
		openssl_aes_192_cbc_enc(key, (uint8_t *) iv, len_bytes, in, out);
	} else if (CBC_256_BITS == k_len) {
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" OpenSSL256 ");
		fflush(0);
#endif
		openssl_aes_256_cbc_enc(key, (uint8_t *) iv, len_bytes, in, out);
	} else {
		fprintf(stderr, "Invalid key length: %d\n", k_len);
		return 1;
	}
	return 0;
}

int OpenSslDec(uint8_t k_len,
	       uint8_t * key, uint8_t * in, uint8_t * iv, uint8_t * out, uint64_t len_bytes)
{
	if (CBC_128_BITS == k_len) {
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" OpenSSL128 ");
#endif
		openssl_aes_128_cbc_dec(key, (uint8_t *) iv, len_bytes, in, out);
	} else if (CBC_192_BITS == k_len) {
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" OpenSSL192 ");
#endif
		openssl_aes_192_cbc_dec(key, (uint8_t *) iv, len_bytes, in, out);
	} else if (CBC_256_BITS == k_len) {
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" OpenSSL256 ");
#endif
		openssl_aes_256_cbc_dec(key, (uint8_t *) iv, len_bytes, in, out);
	} else {
		fprintf(stderr, "Invalid key length: %d\n", k_len);
		return 1;
	}
	return 0;
}

void mk_rand_data(uint8_t * data, uint32_t size)
{
	int i;
	for (i = 0; i < size; i++) {
		*data++ = rand();
	}
}

int check_data(uint8_t * test, uint8_t * expected, uint64_t len, char *data_name)
{
	int mismatch;
	int OK = 0;
	uint64_t a;

	mismatch = memcmp(test, expected, len);
	if (!mismatch) {
		return OK;

	} else {
		OK = 1;
		printf("  failed %s \t\t", data_name);
		for (a = 0; a < len; a++) {
			if (test[a] != expected[a]) {
				printf(" '%x' != '%x' at %lx of %lx\n",
				       test[a], expected[a], a, len);
				break;
			}
		}
	}
	return OK;
}

int check_vector(struct cbc_vector *vector)
{
	uint8_t *pt_test = NULL;
	uint8_t *o_ct_test = NULL;
	int OK = 0;
	aes_cbc_generic enc;
	aes_cbc_generic dec;

#ifdef CBC_VECTORS_VERBOSE
	printf(" Keylen:%d PLen:%d ", (int)vector->K_LEN, (int)vector->P_LEN);
#ifdef CBC_VECTORS_EXTRA_VERBOSE
	printf(" K:%p P:%p C:%p IV:%p expC:%p Keys:%p ", vector->K, vector->P, vector->C,
	       vector->IV, vector->EXP_C, vector->KEYS);
#endif
	fflush(0);
#else
	printf(".");
#endif

	if (CBC_128_BITS == vector->K_LEN) {
		enc = (aes_cbc_generic) & aes_cbc_enc_128;
		dec = (aes_cbc_generic) & aes_cbc_dec_128;
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" CBC128 ");
#endif
	} else if (CBC_192_BITS == vector->K_LEN) {
		enc = (aes_cbc_generic) & aes_cbc_enc_192;
		dec = (aes_cbc_generic) & aes_cbc_dec_192;
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" CBC192 ");
#endif
	} else if (CBC_256_BITS == vector->K_LEN) {
		enc = (aes_cbc_generic) & aes_cbc_enc_256;
		dec = (aes_cbc_generic) & aes_cbc_dec_256;
#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" CBC256 ");
#endif
	} else {
		printf("Invalid key length: %d\n", vector->K_LEN);
		return 1;
	}

	// Allocate space for the calculated ciphertext
	pt_test = malloc(vector->P_LEN);
	o_ct_test = malloc(vector->P_LEN);
	if ((pt_test == NULL) || (o_ct_test == NULL)) {
		fprintf(stderr, "Can't allocate ciphertext memory\n");
		return 1;
	}

	aes_cbc_precomp(vector->K, vector->K_LEN, vector->KEYS);

#ifdef CBC_VECTORS_VERBOSE
	fflush(0);
#endif
	////
	// ISA-l Encrypt
	////
	enc(vector->P, vector->IV, vector->KEYS->enc_keys, vector->C, vector->P_LEN);
	if (NULL != vector->EXP_C) {	//when the encrypted text is know verify correct
		OK |=
		    check_data(vector->EXP_C, vector->C, vector->P_LEN,
			       "ISA-L expected cypher text (C)");
	}
	OpenSslEnc(vector->K_LEN, vector->K, vector->P, vector->IV, o_ct_test, vector->P_LEN);
	OK |=
	    check_data(vector->C, o_ct_test, vector->P_LEN,
		       "OpenSSL vs ISA-L cypher text (C)");

	memcpy(pt_test, vector->P, vector->P_LEN);
	memset(vector->P, 0, vector->P_LEN);
#ifdef CBC_VECTORS_VERBOSE
	fflush(0);
#endif

	////
	// ISA-l Decrypt
	////
	dec(vector->C, vector->IV, vector->KEYS->dec_keys, vector->P, vector->P_LEN);
	OK |= check_data(vector->P, pt_test, vector->P_LEN, "ISA-L decrypted plain text (P)");
	memset(vector->P, 0, vector->P_LEN);
	dec(o_ct_test, vector->IV, vector->KEYS->dec_keys, vector->P, vector->P_LEN);
	OK |= check_data(vector->P, pt_test, vector->P_LEN, "ISA-L decrypted OpenSSL (P)");
	memset(vector->P, 0, vector->P_LEN);
	OpenSslDec(vector->K_LEN, vector->K, vector->C, vector->IV, vector->P, vector->P_LEN);
	OK |= check_data(vector->P, pt_test, vector->P_LEN, "OpenSSL decrypted ISA-L (P)");
#ifdef CBC_VECTORS_VERBOSE
	if (OK)
		printf("Failed");
	else
		printf("Passed");

	printf("\n");
#endif

	return OK;
}

int test_std_combinations(void)
{
	int const vectors_cnt = sizeof(cbc_vectors) / sizeof(cbc_vectors[0]);
	int i;
	uint8_t *iv = NULL;

	printf("AES CBC standard test vectors:");
#ifdef CBC_VECTORS_VERBOSE
	printf("\n");
#endif
	posix_memalign((void **)&iv, 16, (CBC_IV_DATA_LEN));
	if (NULL == iv)
		return 1;

	for (i = 0; (i < vectors_cnt); i++) {
		struct cbc_vector vect = cbc_vectors[i];

		posix_memalign((void **)&vect.KEYS, 16, (sizeof(*vect.KEYS)));
		if (NULL == vect.KEYS)
			return 1;
		// IV data must be aligned to 16 byte boundary so move data in aligned buffer and change out the pointer
		memcpy(iv, vect.IV, CBC_IV_DATA_LEN);
		vect.IV = iv;
		vect.C = NULL;
		vect.C = malloc(vect.P_LEN);
		if ((NULL == vect.C))
			return 1;
#ifdef CBC_VECTORS_VERBOSE
		printf("vector[%d of %d] ", i, vectors_cnt);
#endif
		if (0 == (i % 25))
			printf("\n");
		if (0 == (i % 10))
			fflush(0);

		if (0 != check_vector(&vect))
			return 1;

		aligned_free(vect.KEYS);
		free(vect.C);
	}

	aligned_free(iv);
	printf("\n");
	return 0;
}

int test_random_combinations(void)
{
	struct cbc_vector test;
	int t;

	printf("AES CBC random test vectors:");
#ifdef CBC_VECTORS_VERBOSE
	fflush(0);
#endif
	test.IV = NULL;
	posix_memalign((void **)&test.IV, 16, (CBC_IV_DATA_LEN));
	if (NULL == test.IV)
		return 1;
	test.KEYS = NULL;
	posix_memalign((void **)&test.KEYS, 16, (sizeof(*test.KEYS)));
	if (NULL == test.KEYS)
		return 1;

	for (t = 0; RANDOMS > t; t++) {
		int Plen = 16 + ((rand() % TEST_LEN) & ~0xf);	//must be a 16byte multiple
		int offset = (rand() % MAX_UNALINED);
		int Kindex = (rand() % (sizeof(Ksize) / sizeof(Ksize[0])));	// select one of the valid key sizes

		if (0 == (t % 25))
			printf("\n");
		if (0 == (t % 10))
			fflush(0);

		test.C = NULL;
		test.P = NULL;
		test.K = NULL;
		test.EXP_C = NULL;
		test.P_LEN = Plen;
		test.K_LEN = Ksize[Kindex];

		test.P = malloc(test.P_LEN + offset);
		test.C = malloc(test.P_LEN + offset);
		test.K = malloc(test.K_LEN + offset);
		if ((NULL == test.P) || (NULL == test.C) || (NULL == test.K)) {
			printf("malloc of testsize:0x%x failed\n", Plen);
			return -1;
		}
		test.P += offset;
		test.C += offset;
		test.K += offset;

		mk_rand_data(test.P, test.P_LEN);
		mk_rand_data(test.K, test.K_LEN);
		mk_rand_data(test.IV, CBC_IV_DATA_LEN);

#ifdef CBC_VECTORS_EXTRA_VERBOSE
		printf(" Offset:0x%x ", offset);
#endif
		if (0 != check_vector(&test))
			return 1;

		test.C -= offset;
		free(test.C);
		test.K -= offset;
		free(test.K);
		test.P -= offset;
		free(test.P);
	}

	aligned_free(test.IV);
	aligned_free(test.KEYS);
	printf("\n");
	return 0;
}

int test_efence_combinations(void)
{
	struct cbc_vector test;
	int offset = 0;
	int key_idx;
	uint8_t *P = NULL, *C = NULL, *K = NULL, *IV = NULL;
	uint8_t *key_data = NULL;

	P = malloc(PAGE_LEN);
	C = malloc(PAGE_LEN);
	K = malloc(PAGE_LEN);
	IV = malloc(PAGE_LEN);
	key_data = malloc(PAGE_LEN);

	if ((NULL == P) || (NULL == C) || (NULL == K) || (NULL == IV)
	    || (NULL == key_data)
	    ) {
		printf("malloc of testsize:0x%x failed\n", PAGE_LEN);
		return -1;
	}
	// place buffers to end at page boundary
	test.P_LEN = PAGE_LEN / 2;
	test.EXP_C = NULL;

	printf("AES CBC efence test vectors:");
	for (key_idx = 0; key_idx < (sizeof(Ksize) / sizeof(Ksize[0])); key_idx++) {
		test.K_LEN = Ksize[key_idx];

		for (offset = 0; MAX_UNALINED > offset; offset++) {
			if (0 == (offset % 80))
				printf("\n");
			// move the start and size of the data block towards the end of the page
			test.P_LEN = ((PAGE_LEN / (1 + (2 * offset))) & ~0xff);	// must be a multiple of 16
			if (16 > test.P_LEN)
				test.P_LEN = 16;
			//Place data at end of page
			test.P = P + PAGE_LEN - test.P_LEN - offset;
			test.C = C + PAGE_LEN - test.P_LEN - offset;
			test.K = K + PAGE_LEN - test.K_LEN - offset;
			test.IV = IV + PAGE_LEN - CBC_IV_DATA_LEN - offset;
			test.IV = test.IV - ((uint64_t) test.IV & 0xff);	// align to 16 byte boundary
			test.KEYS = (struct cbc_key_data *)
			    (key_data + PAGE_LEN - sizeof(*test.KEYS) - offset);
			test.KEYS = (struct cbc_key_data *)
			    ((uint8_t *) test.KEYS - ((uint64_t) test.KEYS & 0xff));	// align to 16 byte boundary

			mk_rand_data(test.P, test.P_LEN);
			mk_rand_data(test.K, test.K_LEN);
			mk_rand_data(test.IV, CBC_IV_DATA_LEN);
#ifdef CBC_VECTORS_EXTRA_VERBOSE
			printf(" Offset:0x%x ", offset);
#endif
			if (0 != check_vector(&test))
				return 1;
		}

	}

	free(P);
	free(C);
	free(K);
	free(IV);
	free(key_data);
	printf("\n");
	return 0;
}

int main(void)
{
	uint32_t OK = 0;

	srand(TEST_SEED);
	OK |= test_std_combinations();
	OK |= test_random_combinations();
	OK |= test_efence_combinations();
	if (0 == OK) {
		printf("...Pass\n");
	} else {
		printf("...Fail\n");
	}
	return OK;
}
