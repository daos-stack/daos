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

/*
 * Run list of standard CBC test vectors through encode and decode checks.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <aes_cbc.h>
#include "types.h"
#include "cbc_std_vectors.h"

typedef void (*aes_cbc_generic)(uint8_t * in, uint8_t * IV, uint8_t * keys, uint8_t * out,
				uint64_t len_bytes);

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
	int OK = 0;
	aes_cbc_generic enc;
	aes_cbc_generic dec;

	DEBUG_PRINT((" Keylen:%d PLen:%d ", (int)vector->K_LEN, (int)vector->P_LEN));
	DEBUG_PRINT((" K:%p P:%p C:%p IV:%p expC:%p Keys:%p ", vector->K, vector->P, vector->C,
		     vector->IV, vector->EXP_C, vector->KEYS));
	printf(".");

	switch (vector->K_LEN) {
	case CBC_128_BITS:
		enc = (aes_cbc_generic) & aes_cbc_enc_128;
		dec = (aes_cbc_generic) & aes_cbc_dec_128;
		DEBUG_PRINT((" CBC128 "));
		break;
	case CBC_192_BITS:
		enc = (aes_cbc_generic) & aes_cbc_enc_192;
		dec = (aes_cbc_generic) & aes_cbc_dec_192;
		DEBUG_PRINT((" CBC192 "));
		break;
	case CBC_256_BITS:
		enc = (aes_cbc_generic) & aes_cbc_enc_256;
		dec = (aes_cbc_generic) & aes_cbc_dec_256;
		DEBUG_PRINT((" CBC256 "));
		break;
	default:
		printf("Invalid key length: %d\n", vector->K_LEN);
		return 1;
	}

	// Allocate space for the calculated ciphertext
	pt_test = malloc(vector->P_LEN);

	if (pt_test == NULL) {
		fprintf(stderr, "Can't allocate ciphertext memory\n");
		return 1;
	}

	aes_cbc_precomp(vector->K, vector->K_LEN, vector->KEYS);

	////
	// ISA-l Encrypt
	////
	enc(vector->P, vector->IV, vector->KEYS->enc_keys, vector->C, vector->P_LEN);

	if (NULL != vector->EXP_C) {	//when the encrypted text is known verify correct
		OK |= check_data(vector->EXP_C, vector->C, vector->P_LEN,
				 "ISA-L expected cypher text (C)");
	}
	memcpy(pt_test, vector->P, vector->P_LEN);
	memset(vector->P, 0, vector->P_LEN);

	////
	// ISA-l Decrypt
	////
	dec(vector->C, vector->IV, vector->KEYS->dec_keys, vector->P, vector->P_LEN);
	OK |= check_data(vector->P, pt_test, vector->P_LEN, "ISA-L decrypted plain text (P)");
	DEBUG_PRINT((OK ? "Failed\n" : "Passed\n"));

	free(pt_test);
	return OK;
}

int test_std_combinations(void)
{
	int const vectors_cnt = sizeof(cbc_vectors) / sizeof(cbc_vectors[0]);
	int i;
	uint8_t *iv = NULL;

	printf("AES CBC standard test vectors: ");

	posix_memalign((void **)&iv, 16, (CBC_IV_DATA_LEN));
	if (NULL == iv)
		return 1;

	for (i = 0; (i < vectors_cnt); i++) {
		struct cbc_vector vect = cbc_vectors[i];

		posix_memalign((void **)&(vect.KEYS), 16, sizeof(*vect.KEYS));
		if (NULL == vect.KEYS)
			return 1;

		// IV data must be aligned to 16 byte boundary so move data in
		// aligned buffer and change out the pointer
		memcpy(iv, vect.IV, CBC_IV_DATA_LEN);
		vect.IV = iv;
		vect.C = malloc(vect.P_LEN);
		if (NULL == vect.C)
			return 1;

		DEBUG_PRINT(("vector[%d of %d] ", i, vectors_cnt));

		if (0 != check_vector(&vect))
			return 1;

		aligned_free(vect.KEYS);
		free(vect.C);
	}

	aligned_free(iv);
	return 0;
}

int main(void)
{
	uint32_t OK = 0;

	OK = test_std_combinations();

	printf(0 == OK ? "Pass\n" : "Fail\n");
	return OK;
}
