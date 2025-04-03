/**********************************************************************
  Copyright(c) 2011-2017 Intel Corporation All rights reserved.

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
#include <string.h>		// for memcmp
#include <aes_gcm.h>
#include "gcm_vectors.h"
#include "types.h"

#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif

int check_data(uint8_t * test, uint8_t * expected, uint64_t len, char *data_name)
{
	int mismatch;
	int OK = 0;

	mismatch = memcmp(test, expected, len);
	if (mismatch) {
		OK = 1;
		printf("  expected results don't match %s \t\t", data_name);
		{
			uint64_t a;
			for (a = 0; a < len; a++) {
				if (test[a] != expected[a]) {
					printf(" '%x' != '%x' at %lx of %lx\n",
					       test[a], expected[a], a, len);
					break;
				}
			}
		}
	}
	return OK;
}

int test_gcm128_std_vectors_nt(gcm_vector const *vector)
{
	struct gcm_key_data gkey;
	struct gcm_context_data gctx;
	int OK = 0;
	// Temporary array for the calculated vectors
	uint8_t *ct_test = NULL;
	uint8_t *pt_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *T2_test = NULL;
	uint64_t IV_alloc_len = 0;

	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&ct_test, 32, vector->Plen);
	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&pt_test, 32, vector->Plen);
	if ((ct_test == NULL) || (pt_test == NULL)) {
		fprintf(stderr, "Can't allocate ciphertext or plaintext memory\n");
		return 1;
	}
	IV_alloc_len = vector->IVlen;
	// Allocate space for the calculated ciphertext
	IV_c = malloc(IV_alloc_len);
	if (IV_c == NULL) {
		fprintf(stderr, "Can't allocate ciphertext memory\n");
		return 1;
	}
	memcpy(IV_c, vector->IV, vector->IVlen);

	T_test = malloc(vector->Tlen);
	T2_test = malloc(vector->Tlen);
	if ((T_test == NULL) || (T2_test == NULL)) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_128(vector->K, &gkey);

	////
	// ISA-l Encrypt
	////
	memset(ct_test, 0, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_128_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(ct_test, vector->C, vector->Plen, "ISA-L encrypted cypher text (C)");
	OK |= check_data(T_test, vector->T, vector->Tlen, "ISA-L tag (T)");

	// test of in-place encrypt
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_128_nt(&gkey, &gctx, pt_test, pt_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(pt_test, vector->C, vector->Plen,
			 "ISA-L encrypted cypher text(in-place)");
	memset(ct_test, 0, vector->Plen);
	memset(T_test, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////
	memcpy(ct_test, vector->C, vector->Plen);
	aes_gcm_dec_128_nt(&gkey, &gctx, pt_test, ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	// GCM decryption outputs a 16 byte tag value that must be verified against the expected tag value
	OK |= check_data(T_test, vector->T, vector->Tlen, "ISA-L decrypted tag (T)");

	// test in in-place decrypt
	memcpy(ct_test, vector->C, vector->Plen);
	aes_gcm_dec_128_nt(&gkey, &gctx, ct_test, ct_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(ct_test, vector->P, vector->Plen, "ISA-L plain text (P) - in-place");
	OK |=
	    check_data(T_test, vector->T, vector->Tlen, "ISA-L decrypted tag (T) - in-place");
	// ISA-L enc -> ISA-L dec
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_128_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	memset(pt_test, 0, vector->Plen);
	aes_gcm_dec_128_nt(&gkey, &gctx, pt_test, ct_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T2_test, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "ISA-L self decrypted plain text (P)");
	OK |= check_data(T_test, T2_test, vector->Tlen, "ISA-L self decrypted tag (T)");

	memset(pt_test, 0, vector->Plen);

	if (NULL != ct_test)
		free(ct_test);
	if (NULL != pt_test)
		free(pt_test);
	if (NULL != IV_c)
		free(IV_c);
	if (NULL != T_test)
		free(T_test);
	if (NULL != T2_test)
		free(T2_test);

	return OK;
}

int test_gcm256_std_vectors_nt(gcm_vector const *vector)
{
	struct gcm_key_data gkey;
	struct gcm_context_data gctx;
	int OK = 0;
	// Temporary array for the calculated vectors
	uint8_t *ct_test = NULL;
	uint8_t *pt_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *T2_test = NULL;
	uint64_t IV_alloc_len = 0;

	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&ct_test, 32, vector->Plen);
	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&pt_test, 32, vector->Plen);
	if ((ct_test == NULL) || (pt_test == NULL)) {
		fprintf(stderr, "Can't allocate ciphertext or plaintext memory\n");
		return 1;
	}
	IV_alloc_len = vector->IVlen;
	// Allocate space for the calculated ciphertext
	IV_c = malloc(IV_alloc_len);
	if (IV_c == NULL) {
		fprintf(stderr, "Can't allocate ciphertext memory\n");
		return 1;
	}
	memcpy(IV_c, vector->IV, vector->IVlen);

	T_test = malloc(vector->Tlen);
	T2_test = malloc(vector->Tlen);
	if (T_test == NULL) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_256(vector->K, &gkey);

	////
	// ISA-l Encrypt
	////
	memset(ct_test, 0, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_256_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(ct_test, vector->C, vector->Plen, "ISA-L encrypted cypher text (C)");
	OK |= check_data(T_test, vector->T, vector->Tlen, "ISA-L tag (T)");

	// test of in-place encrypt
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_256_nt(&gkey, &gctx, pt_test, pt_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T_test, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->C, vector->Plen,
		       "ISA-L encrypted cypher text(in-place)");
	memset(ct_test, 0, vector->Plen);
	memset(T_test, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////
	memset(pt_test, 0, vector->Plen);
	memcpy(ct_test, vector->C, vector->Plen);
	aes_gcm_dec_256_nt(&gkey, &gctx, pt_test, ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	// GCM decryption outputs a 16 byte tag value that must be verified against the expected tag value
	OK |= check_data(T_test, vector->T, vector->Tlen, "ISA-L decrypted tag (T)");

	// test in in-place decrypt
	memcpy(ct_test, vector->C, vector->Plen);
	aes_gcm_dec_256_nt(&gkey, &gctx, ct_test, ct_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(ct_test, vector->P, vector->Plen, "ISA-L plain text (P) - in-place");
	OK |=
	    check_data(T_test, vector->T, vector->Tlen, "ISA-L decrypted tag (T) - in-place");
	// ISA-L enc -> ISA-L dec
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_256_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	memset(pt_test, 0, vector->Plen);
	aes_gcm_dec_256_nt(&gkey, &gctx, pt_test, ct_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T2_test, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "ISA-L self decrypted plain text (P)");
	OK |= check_data(T_test, T2_test, vector->Tlen, "ISA-L self decrypted tag (T)");

	if (NULL != ct_test)
		free(ct_test);
	if (NULL != pt_test)
		free(pt_test);
	if (NULL != IV_c)
		free(IV_c);
	if (NULL != T_test)
		free(T_test);
	if (NULL != T2_test)
		free(T2_test);

	return OK;
}

int test_gcm_std_vectors_nt(void)
{
	int const vectors_cnt = sizeof(gcm_vectors) / sizeof(gcm_vectors[0]);
	int vect;
	int OK = 0;

	printf("AES-GCM standard test vectors NT:\n");
	for (vect = 0; (vect < vectors_cnt); vect++) {
#ifdef DEBUG
		printf("Standard vector NT %d/%d"
		       "  Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
		       vect, vectors_cnt - 1, (int)gcm_vectors[vect].Klen,
		       (int)gcm_vectors[vect].IVlen, (int)gcm_vectors[vect].Plen,
		       (int)gcm_vectors[vect].Alen, (int)gcm_vectors[vect].Tlen);
#else
		printf(".");
#endif
		if (BITS_128 == gcm_vectors[vect].Klen)
			OK |= test_gcm128_std_vectors_nt(&gcm_vectors[vect]);
		else
			OK |= test_gcm256_std_vectors_nt(&gcm_vectors[vect]);
		if (0 != OK)
			return OK;
	}
	printf("\n");
	return OK;
}

int main(int argc, char **argv)
{
	int errors = 0;
	int seed;

	if (argc == 1)
		seed = TEST_SEED;
	else
		seed = atoi(argv[1]);

	srand(seed);
	printf("SEED: %d\n", seed);

	errors += test_gcm_std_vectors_nt();

	if (0 == errors)
		printf("...Pass\n");
	else
		printf("...Fail\n");

	return errors;
}
