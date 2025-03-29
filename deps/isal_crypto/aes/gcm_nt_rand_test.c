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
#include <openssl/sha.h>
#include "gcm_vectors.h"
#include "ossl_helper.h"
#include "types.h"

//#define GCM_VECTORS_VERBOSE
//#define GCM_VECTORS_EXTRA_VERBOSE
#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif
#ifndef RANDOMS
# define RANDOMS  200
#endif
#ifndef TEST_LEN
# define TEST_LEN  32*1024
#endif
#ifndef PAGE_LEN
# define PAGE_LEN  (4*1024)
#endif

// NT versions require 64B alignment
# define NT_ALIGNMENT (64)
# define ALIGNMENT_MASK (~(NT_ALIGNMENT - 1))
# define OFFSET_BASE_VALUE (NT_ALIGNMENT)
#ifndef MAX_UNALIGNED
# define MAX_UNALIGNED  (1)
#endif

void dump_table(char *title, uint8_t * table, uint8_t count)
{
	int i;
	char const *space = "   ";

	printf("%s%s => {\n", space, title);
	for (i = 0; i < count; i++) {
		if (0 == (i & 15))
			printf("%s%s", space, space);
		printf("%2x, ", table[i]);
		if (15 == (i & 15))
			printf("\n");

	}
	printf("%s}\n", space);
}

void dump_gcm_data(struct gcm_key_data *gkey)
{
#ifdef GCM_VECTORS_EXTRA_VERBOSE
	printf("gcm_data {\n");
	dump_table("expanded_keys", gkey->expanded_keys, (16 * 11));
	dump_table("shifted_hkey_1", gkey->shifted_hkey_1, 16);
	dump_table("shifted_hkey_2", gkey->shifted_hkey_2, 16);
	dump_table("shifted_hkey_3", gkey->shifted_hkey_3, 16);
	dump_table("shifted_hkey_4", gkey->shifted_hkey_4, 16);
	dump_table("shifted_hkey_5", gkey->shifted_hkey_5, 16);
	dump_table("shifted_hkey_6", gkey->shifted_hkey_6, 16);
	dump_table("shifted_hkey_7", gkey->shifted_hkey_7, 16);
	dump_table("shifted_hkey_8", gkey->shifted_hkey_8, 16);
	dump_table("shifted_hkey_1_k", gkey->shifted_hkey_1_k, 16);
	dump_table("shifted_hkey_2_k", gkey->shifted_hkey_2_k, 16);
	dump_table("shifted_hkey_3_k", gkey->shifted_hkey_3_k, 16);
	dump_table("shifted_hkey_4_k", gkey->shifted_hkey_4_k, 16);
	dump_table("shifted_hkey_5_k", gkey->shifted_hkey_5_k, 16);
	dump_table("shifted_hkey_6_k", gkey->shifted_hkey_6_k, 16);
	dump_table("shifted_hkey_7_k", gkey->shifted_hkey_7_k, 16);
	dump_table("shifted_hkey_8_k", gkey->shifted_hkey_8_k, 16);
	printf("}\n");
#endif //GCM_VECTORS_VERBOSE
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

int check_vector(struct gcm_key_data *gkey, struct gcm_context_data *gctx, gcm_vector * vector)
{
	uint8_t *pt_test = NULL;
	uint8_t *ct_test = NULL;
	uint8_t *o_ct_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *o_T_test = NULL;
	uint64_t IV_alloc_len = 0;
	int result;
	int OK = 0;

#ifdef GCM_VECTORS_VERBOSE
	printf("combination vector Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
	       (int)vector->Klen,
	       (int)vector->IVlen, (int)vector->Plen, (int)vector->Alen, (int)vector->Tlen);
#else
	printf(".");
#endif
	// Allocate space for the calculated ciphertext
	if (vector->Plen != 0) {
		posix_memalign((void **)&pt_test, 64, vector->Plen);
		posix_memalign((void **)&ct_test, 64, vector->Plen);
		posix_memalign((void **)&o_ct_test, 64, vector->Plen);
		if ((pt_test == NULL) || (ct_test == NULL) || (o_ct_test == NULL)) {
			fprintf(stderr, "Can't allocate ciphertext memory\n");
			return 1;
		}
	}
	IV_alloc_len = vector->IVlen;

	IV_c = malloc(IV_alloc_len);
	if (IV_c == NULL) {
		fprintf(stderr, "Can't allocate ciphertext memory\n");
		return 1;
	}
	memcpy(IV_c, vector->IV, vector->IVlen);

	T_test = malloc(vector->Tlen);
	o_T_test = malloc(vector->Tlen);
	if ((T_test == NULL) || (o_T_test == NULL)) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_128(vector->K, gkey);

	////
	// ISA-l Encrypt
	////
	aes_gcm_enc_128_nt(gkey, gctx, vector->C, vector->P, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	openssl_aes_gcm_enc(vector->K, vector->IV,
			    vector->IVlen, vector->A, vector->Alen, o_T_test,
			    vector->Tlen, vector->P, vector->Plen, o_ct_test);
	OK |=
	    check_data(vector->C, o_ct_test, vector->Plen, "OpenSSL vs ISA-L cypher text (C)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L encrypt tag (T)");

	memcpy(ct_test, vector->C, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	memset(vector->P, 0, vector->Plen);
	memcpy(T_test, vector->T, vector->Tlen);
	memset(vector->T, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////
	aes_gcm_dec_128_nt(gkey, gctx, vector->P, vector->C, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L decrypt tag (T)");
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	memset(vector->P, 0, vector->Plen);
	aes_gcm_dec_128_nt(gkey, gctx, vector->P, o_ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	result =
	    openssl_aes_gcm_dec(vector->K, vector->IV,
				vector->IVlen, vector->A, vector->Alen,
				vector->T, vector->Tlen, vector->C, vector->Plen, pt_test);
	if (-1 == result)
		printf(" ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	free(T_test);
	free(o_T_test);
	free(IV_c);
	free(pt_test);
	free(ct_test);
	free(o_ct_test);

	return OK;
}

int check_strm_vector(struct gcm_key_data *gkey, struct gcm_context_data *gctx,
		      gcm_vector * vector, int test_len)
{
	uint8_t *pt_test = NULL;
	uint8_t *ct_test = NULL;
	uint8_t *o_ct_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *o_T_test = NULL;
	uint8_t *stream = NULL;
	uint64_t IV_alloc_len = 0;
	int result;
	int OK = 0;
	uint32_t last_break;
	int i;
	uint8_t *rand_data = NULL;
	uint64_t length;

	rand_data = malloc(100);

#ifdef GCM_VECTORS_VERBOSE
	printf("combination vector Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
	       (int)vector->Klen,
	       (int)vector->IVlen, (int)vector->Plen, (int)vector->Alen, (int)vector->Tlen);
#else
	printf(".");
#endif
	// Allocate space for the calculated ciphertext
	if (vector->Plen != 0) {
		posix_memalign((void **)&pt_test, 64, vector->Plen);
		posix_memalign((void **)&ct_test, 64, vector->Plen);
		posix_memalign((void **)&o_ct_test, 64, vector->Plen);
		if ((pt_test == NULL) || (ct_test == NULL) || (o_ct_test == NULL)) {
			fprintf(stderr, "Can't allocate ciphertext memory\n");
			return 1;
		}
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
	o_T_test = malloc(vector->Tlen);
	if ((T_test == NULL) || (o_T_test == NULL)) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_128(vector->K, gkey);

	////
	// ISA-l Encrypt
	////
	aes_gcm_init_128(gkey, gctx, IV_c, vector->A, vector->Alen);

	last_break = 0;
	i = (rand() % test_len / 8) & ALIGNMENT_MASK;
	while (i < (vector->Plen)) {
		if (i - last_break != 0) {
			posix_memalign((void **)&stream, 64, (i - last_break));
			memcpy(stream, vector->P + last_break, i - last_break);
		}
		aes_gcm_enc_128_update_nt(gkey, gctx, vector->C + last_break, stream,
					  i - last_break);
		if (i - last_break != 0)
			free(stream);

		if (rand() % 1024 == 0) {
			length = rand() % 100;
			mk_rand_data(rand_data, length);
			SHA1(rand_data, length, rand_data);
		}
		last_break = i;
		i = (rand() % test_len / 8) & ALIGNMENT_MASK;

	}
	aes_gcm_enc_128_update_nt(gkey, gctx, vector->C + last_break, vector->P + last_break,
				  vector->Plen - last_break);
	if (gctx->in_length != vector->Plen)
		printf("%lu, %lu\n", gctx->in_length, vector->Plen);
	aes_gcm_enc_128_finalize(gkey, gctx, vector->T, vector->Tlen);
	openssl_aes_gcm_enc(vector->K, vector->IV,
			    vector->IVlen, vector->A, vector->Alen, o_T_test,
			    vector->Tlen, vector->P, vector->Plen, o_ct_test);
	OK |=
	    check_data(vector->C, o_ct_test, vector->Plen, "OpenSSL vs ISA-L cypher text (C)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L encrypt tag (T)");

	memcpy(ct_test, vector->C, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	memset(vector->P, 0, vector->Plen);
	memcpy(T_test, vector->T, vector->Tlen);
	memset(vector->T, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////

	last_break = 0;
	i = 0;
	aes_gcm_init_128(gkey, gctx, IV_c, vector->A, vector->Alen);
	while (i < (vector->Plen)) {
		if (rand() % (test_len / 64) == 0) {
			if (i - last_break != 0) {
				posix_memalign((void **)&stream, 64, i - last_break);
				memcpy(stream, vector->C + last_break, i - last_break);
			}
			aes_gcm_dec_128_update_nt(gkey, gctx, vector->P + last_break, stream,
						  i - last_break);
			if (i - last_break != 0)
				free(stream);

			if (rand() % 1024 == 0) {
				length = rand() % 100;

				mk_rand_data(rand_data, length);
				SHA1(rand_data, length, rand_data);
			}

			last_break = i;

		}
		if (rand() % 1024 != 0)
			i++;

	}
	aes_gcm_dec_128_update_nt(gkey, gctx, vector->P + last_break, vector->C + last_break,
				  vector->Plen - last_break);
	aes_gcm_dec_128_finalize(gkey, gctx, vector->T, vector->Tlen);

	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L decrypt tag (T)");
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	memset(vector->P, 0, vector->Plen);
	aes_gcm_dec_128_nt(gkey, gctx, vector->P, o_ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	result =
	    openssl_aes_gcm_dec(vector->K, vector->IV,
				vector->IVlen, vector->A, vector->Alen,
				vector->T, vector->Tlen, vector->C, vector->Plen, pt_test);
	if (-1 == result)
		printf(" ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	free(T_test);
	free(o_T_test);
	free(IV_c);
	free(pt_test);
	free(ct_test);
	free(o_ct_test);
	free(rand_data);

	return OK;
}

int check_strm_vector2(struct gcm_key_data *gkey, struct gcm_context_data *gctx,
		       gcm_vector * vector, int length, int start, int breaks)
{
	uint8_t *pt_test = NULL;
	uint8_t *ct_test = NULL;
	uint8_t *o_ct_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *o_T_test = NULL;
	uint8_t *stream = NULL;
	uint64_t IV_alloc_len = 0;
	int result;
	int OK = 0;
	uint32_t last_break = 0;
	int i = length;
	uint8_t *rand_data = NULL;

	rand_data = malloc(100);

#ifdef GCM_VECTORS_VERBOSE
	printf("combination vector Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
	       (int)vector->Klen,
	       (int)vector->IVlen, (int)vector->Plen, (int)vector->Alen, (int)vector->Tlen);
#else
	printf(".");
#endif
	// Allocate space for the calculated ciphertext
	if (vector->Plen != 0) {
		pt_test = malloc(vector->Plen);
		ct_test = malloc(vector->Plen);
		posix_memalign((void **)&o_ct_test, 64, vector->Plen);
		if ((pt_test == NULL) || (ct_test == NULL) || (o_ct_test == NULL)) {
			fprintf(stderr, "Can't allocate ciphertext memory\n");
			return 1;
		}
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
	o_T_test = malloc(vector->Tlen);
	if ((T_test == NULL) || (o_T_test == NULL)) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_128(vector->K, gkey);

	////
	// ISA-l Encrypt
	////
	aes_gcm_enc_128_nt(gkey, gctx, vector->C, vector->P, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	aes_gcm_init_128(gkey, gctx, IV_c, vector->A, vector->Alen);
	while (i < (vector->Plen)) {
		if (i - last_break != 0) {
			posix_memalign((void **)&stream, 64, i - last_break);
			memcpy(stream, vector->P + last_break, i - last_break);
		}
		aes_gcm_enc_128_update_nt(gkey, gctx, vector->C + last_break, stream,
					  i - last_break);
		if (i - last_break != 0)
			free(stream);
		last_break = i;
		i = i + (length - start) / breaks;

	}
	aes_gcm_enc_128_update_nt(gkey, gctx, vector->C + last_break, vector->P + last_break,
				  vector->Plen - last_break);
	aes_gcm_enc_128_finalize(gkey, gctx, vector->T, vector->Tlen);
	openssl_aes_gcm_enc(vector->K, vector->IV,
			    vector->IVlen, vector->A, vector->Alen, o_T_test,
			    vector->Tlen, vector->P, vector->Plen, o_ct_test);

	OK |=
	    check_data(vector->C, o_ct_test, vector->Plen, "OpenSSL vs ISA-L cypher text (C)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L encrypt tag (T)");

	memcpy(ct_test, vector->C, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	memset(vector->P, 0, vector->Plen);
	memcpy(T_test, vector->T, vector->Tlen);
	memset(vector->T, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////

	last_break = 0;
	i = length;
	aes_gcm_init_128(gkey, gctx, IV_c, vector->A, vector->Alen);
	while (i < (vector->Plen)) {
		if (i - last_break != 0) {
			posix_memalign((void **)&stream, 64, i - last_break);
			memcpy(stream, vector->C + last_break, i - last_break);
		}
		aes_gcm_dec_128_update_nt(gkey, gctx, vector->P + last_break, stream,
					  i - last_break);
		if (i - last_break != 0)
			free(stream);
		last_break = i;
		i = i + (length - start) / breaks;

	}

	aes_gcm_dec_128_update_nt(gkey, gctx, vector->P + last_break, vector->C + last_break,
				  vector->Plen - last_break);
	aes_gcm_dec_128_finalize(gkey, gctx, vector->T, vector->Tlen);
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L decrypt tag (T)");
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	memset(vector->P, 0, vector->Plen);
	aes_gcm_dec_128_nt(gkey, gctx, vector->P, o_ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	result =
	    openssl_aes_gcm_dec(vector->K, vector->IV,
				vector->IVlen, vector->A, vector->Alen,
				vector->T, vector->Tlen, vector->C, vector->Plen, pt_test);
	if (-1 == result)
		printf(" ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	free(rand_data);

	return OK;
}

int check_strm_vector_efence(struct gcm_key_data *gkey, struct gcm_context_data *gctx,
			     gcm_vector * vector)
{
	uint8_t *pt_test = NULL;
	uint8_t *ct_test = NULL;
	uint8_t *o_ct_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *o_T_test = NULL;
	uint8_t *stream = NULL;
	uint64_t IV_alloc_len = 0;
	int result;
	int OK = 0;
	uint32_t last_break = 0;
	int i = 1;
	uint8_t *rand_data = NULL;
	uint64_t length;

	rand_data = malloc(100);

#ifdef GCM_VECTORS_VERBOSE
	printf("combination vector Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
	       (int)vector->Klen,
	       (int)vector->IVlen, (int)vector->Plen, (int)vector->Alen, (int)vector->Tlen);
#else
	printf(".");
#endif
	// Allocate space for the calculated ciphertext
	if (vector->Plen != 0) {
		posix_memalign((void **)&pt_test, 64, vector->Plen);
		posix_memalign((void **)&ct_test, 64, vector->Plen);
		posix_memalign((void **)&o_ct_test, 64, vector->Plen);
		if ((pt_test == NULL) || (ct_test == NULL) || (o_ct_test == NULL)) {
			fprintf(stderr, "Can't allocate ciphertext memory\n");
			return 1;
		}
	}
	IV_alloc_len = vector->IVlen;

	IV_c = malloc(IV_alloc_len);
	if (IV_c == NULL) {
		fprintf(stderr, "Can't allocate ciphertext memory\n");
		return 1;
	}
	memcpy(IV_c, vector->IV, vector->IVlen);

	T_test = malloc(vector->Tlen);
	o_T_test = malloc(vector->Tlen);
	if ((T_test == NULL) || (o_T_test == NULL)) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_128(vector->K, gkey);

	////
	// ISA-l Encrypt
	////
	aes_gcm_init_128(gkey, gctx, IV_c, vector->A, vector->Alen);
	while (i < vector->Plen) {
		if (rand() % 2000 == 0 || i - last_break > PAGE_LEN / 2) {
			posix_memalign((void **)&stream, 64, PAGE_LEN);
			i = i & ALIGNMENT_MASK;
			memcpy(stream + PAGE_LEN - (i - last_break), vector->P + last_break,
			       i - last_break);
			aes_gcm_enc_128_update_nt(gkey, gctx, vector->C + last_break,
						  stream + PAGE_LEN - (i - last_break),
						  i - last_break);
			free(stream);

			if (rand() % 1024 == 0) {
				length = rand() % 100;
				mk_rand_data(rand_data, length);
				SHA1(rand_data, length, rand_data);
			}
			last_break = i;
		}
		if (rand() % 1024 != 0)
			i++;

	}
	aes_gcm_enc_128_update_nt(gkey, gctx, vector->C + last_break, vector->P + last_break,
				  vector->Plen - last_break);
	aes_gcm_enc_128_finalize(gkey, gctx, vector->T, vector->Tlen);
	openssl_aes_gcm_enc(vector->K, vector->IV,
			    vector->IVlen, vector->A, vector->Alen, o_T_test,
			    vector->Tlen, vector->P, vector->Plen, o_ct_test);
	OK |=
	    check_data(vector->C, o_ct_test, vector->Plen, "OpenSSL vs ISA-L cypher text (C)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L encrypt tag (T)");

	memcpy(ct_test, vector->C, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	memset(vector->P, 0, vector->Plen);
	memcpy(T_test, vector->T, vector->Tlen);
	memset(vector->T, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////

	last_break = 0;
	i = 0;
	aes_gcm_init_128(gkey, gctx, IV_c, vector->A, vector->Alen);
	while (i < vector->Plen) {
		if (rand() % 2000 == 0 || i - last_break > PAGE_LEN / 2) {
			posix_memalign((void **)&stream, 64, PAGE_LEN);
			i = i & ALIGNMENT_MASK;
			memcpy(stream + PAGE_LEN - (i - last_break), vector->C + last_break,
			       i - last_break);
			aes_gcm_dec_128_update_nt(gkey, gctx, vector->P + last_break,
						  stream + PAGE_LEN - (i - last_break),
						  i - last_break);
			free(stream);

			if (rand() % 1024 == 0) {
				length = rand() % 100;

				mk_rand_data(rand_data, length);
				SHA1(rand_data, length, rand_data);
			}

			last_break = i;

		}
		if (rand() % 1024 != 0)
			i++;

	}
	aes_gcm_dec_128_update_nt(gkey, gctx, vector->P + last_break, vector->C + last_break,
				  vector->Plen - last_break);
	aes_gcm_dec_128_finalize(gkey, gctx, vector->T, vector->Tlen);

	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L decrypt tag (T)");
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	memset(vector->P, 0, vector->Plen);
	aes_gcm_dec_128_nt(gkey, gctx, vector->P, o_ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |= check_data(pt_test, vector->P, vector->Plen, "ISA-L decrypted plain text (P)");
	result =
	    openssl_aes_gcm_dec(vector->K, vector->IV,
				vector->IVlen, vector->A, vector->Alen,
				vector->T, vector->Tlen, vector->C, vector->Plen, pt_test);
	if (-1 == result)
		printf(" ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	free(T_test);
	free(o_T_test);
	free(IV_c);
	free(pt_test);
	free(ct_test);
	free(o_ct_test);
	free(rand_data);

	return OK;
}

int check_256_vector(struct gcm_key_data *gkey, struct gcm_context_data *gctx,
		     gcm_vector * vector)
{
	uint8_t *pt_test = NULL;
	uint8_t *ct_test = NULL;
	uint8_t *o_ct_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *o_T_test = NULL;
	uint64_t IV_alloc_len = 0;
	int result;
	int OK = 0;

#ifdef GCM_VECTORS_VERBOSE
	printf("combination vector Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
	       (int)vector->Klen,
	       (int)vector->IVlen, (int)vector->Plen, (int)vector->Alen, (int)vector->Tlen);
#else
	printf(".");
#endif
	// Allocate space for the calculated ciphertext
	if (vector->Plen != 0) {
		posix_memalign((void **)&pt_test, 64, vector->Plen);
		posix_memalign((void **)&ct_test, 64, vector->Plen);
		posix_memalign((void **)&o_ct_test, 64, vector->Plen);
		if ((pt_test == NULL) || (ct_test == NULL) || (o_ct_test == NULL)) {
			fprintf(stderr, "Can't allocate ciphertext memory\n");
			return 1;
		}
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
	o_T_test = malloc(vector->Tlen);
	if ((T_test == NULL) || (o_T_test == NULL)) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_256(vector->K, gkey);

	////
	// ISA-l Encrypt
	////
	aes_gcm_enc_256_nt(gkey, gctx, vector->C, vector->P, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	openssl_aes_256_gcm_enc(vector->K, vector->IV,
				vector->IVlen, vector->A, vector->Alen, o_T_test,
				vector->Tlen, vector->P, vector->Plen, o_ct_test);
	OK |=
	    check_data(vector->C, o_ct_test, vector->Plen, "OpenSSL vs ISA-L cypher text (C)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L encrypt tag (T)");

	memcpy(ct_test, vector->C, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	memset(vector->P, 0, vector->Plen);
	memcpy(T_test, vector->T, vector->Tlen);
	memset(vector->T, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////
	aes_gcm_dec_256_nt(gkey, gctx, vector->P, vector->C, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |= check_data(vector->T, T_test, vector->Tlen, "ISA-L decrypt vs encrypt tag (T)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L decrypt tag (T)");
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "ISA-L decrypted ISA-L plain text (P)");
	memset(vector->P, 0, vector->Plen);
	aes_gcm_dec_256_nt(gkey, gctx, vector->P, o_ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "ISA-L decrypted OpenSSL plain text (P)");
	result =
	    openssl_aes_256_gcm_dec(vector->K, vector->IV,
				    vector->IVlen, vector->A, vector->Alen,
				    vector->T, vector->Tlen, vector->C, vector->Plen, pt_test);
	if (-1 == result)
		printf(" ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	free(T_test);
	free(o_T_test);
	free(IV_c);
	free(pt_test);
	free(ct_test);
	free(o_ct_test);

	return OK;
}

int check_256_strm_vector(struct gcm_key_data *gkey, struct gcm_context_data *gctx,
			  gcm_vector * vector, int test_len)
{
	uint8_t *pt_test = NULL;
	uint8_t *ct_test = NULL;
	uint8_t *o_ct_test = NULL;
	uint8_t *IV_c = NULL;
	uint8_t *T_test = NULL;
	uint8_t *o_T_test = NULL;
	uint8_t *stream = NULL;
	uint64_t IV_alloc_len = 0;
	int result;
	int OK = 0;
	uint32_t last_break;
	int i;
	uint8_t *rand_data = NULL;
	uint64_t length;

	rand_data = malloc(100);

#ifdef GCM_VECTORS_VERBOSE
	printf("combination vector Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
	       (int)vector->Klen,
	       (int)vector->IVlen, (int)vector->Plen, (int)vector->Alen, (int)vector->Tlen);
#else
	printf(".");
#endif
	// Allocate space for the calculated ciphertext
	if (vector->Plen != 0) {
		posix_memalign((void **)&pt_test, 64, vector->Plen);
		posix_memalign((void **)&ct_test, 64, vector->Plen);
		posix_memalign((void **)&o_ct_test, 64, vector->Plen);
		if ((pt_test == NULL) || (ct_test == NULL) || (o_ct_test == NULL)) {
			fprintf(stderr, "Can't allocate ciphertext memory\n");
			return 1;
		}
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
	o_T_test = malloc(vector->Tlen);
	if ((T_test == NULL) || (o_T_test == NULL)) {
		fprintf(stderr, "Can't allocate tag memory\n");
		return 1;
	}
	// This is only required once for a given key
	aes_gcm_pre_256(vector->K, gkey);

	////
	// ISA-l Encrypt
	////
	aes_gcm_init_256(gkey, gctx, IV_c, vector->A, vector->Alen);

	last_break = 0;
	i = (rand() % test_len / 8) & ALIGNMENT_MASK;
	while (i < (vector->Plen)) {
		if (i - last_break != 0) {
			posix_memalign((void **)&stream, 64, i - last_break);
			memcpy(stream, vector->P + last_break, i - last_break);
		}

		aes_gcm_enc_256_update_nt(gkey, gctx, vector->C + last_break, stream,
					  i - last_break);
		if (i - last_break != 0)
			free(stream);

		if (rand() % 1024 == 0) {
			length = rand() % 100;
			mk_rand_data(rand_data, length);
			SHA1(rand_data, length, rand_data);
		}
		last_break = i;
		i += (rand() % test_len / 8) & ALIGNMENT_MASK;

	}
	aes_gcm_enc_256_update_nt(gkey, gctx, vector->C + last_break, vector->P + last_break,
				  vector->Plen - last_break);
	if (gctx->in_length != vector->Plen)
		printf("%lu, %lu\n", gctx->in_length, vector->Plen);
	aes_gcm_enc_256_finalize(gkey, gctx, vector->T, vector->Tlen);

	openssl_aes_256_gcm_enc(vector->K, vector->IV,
				vector->IVlen, vector->A, vector->Alen, o_T_test,
				vector->Tlen, vector->P, vector->Plen, o_ct_test);
	OK |=
	    check_data(vector->C, o_ct_test, vector->Plen, "OpenSSL vs ISA-L cypher text (C)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L encrypt tag (T)");

	memcpy(ct_test, vector->C, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	memset(vector->P, 0, vector->Plen);
	memcpy(T_test, vector->T, vector->Tlen);
	memset(vector->T, 0, vector->Tlen);

	////
	// ISA-l Decrypt
	////

	last_break = 0;
	i += (rand() % test_len / 8) & ALIGNMENT_MASK;
	aes_gcm_init_256(gkey, gctx, IV_c, vector->A, vector->Alen);
	while (i < (vector->Plen)) {
		if (i - last_break != 0) {
			posix_memalign((void **)&stream, 64, i - last_break);
			memcpy(stream, vector->C + last_break, i - last_break);
		}

		aes_gcm_dec_256_update_nt(gkey, gctx, vector->P + last_break, stream,
					  i - last_break);
		if (i - last_break != 0)
			free(stream);

		if (rand() % 1024 == 0) {
			length = rand() % 100;

			mk_rand_data(rand_data, length);
			SHA1(rand_data, length, rand_data);
		}

		last_break = i;
		i += (rand() % test_len / 8) & ALIGNMENT_MASK;

	}
	aes_gcm_dec_256_update_nt(gkey, gctx, vector->P + last_break, vector->C + last_break,
				  vector->Plen - last_break);
	aes_gcm_dec_256_finalize(gkey, gctx, vector->T, vector->Tlen);

	OK |= check_data(vector->T, T_test, vector->Tlen, "ISA-L decrypt vs encrypt tag (T)");
	OK |=
	    check_data(vector->T, o_T_test, vector->Tlen, "OpenSSL vs ISA-L decrypt tag (T)");
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "ISA-L decrypted ISA-L plain text (P)");
	memset(vector->P, 0, vector->Plen);
	aes_gcm_dec_256_nt(gkey, gctx, vector->P, o_ct_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, vector->T, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "ISA-L decrypted OpenSSL plain text (P)");
	result =
	    openssl_aes_256_gcm_dec(vector->K, vector->IV,
				    vector->IVlen, vector->A, vector->Alen,
				    vector->T, vector->Tlen, vector->C, vector->Plen, pt_test);
	if (-1 == result)
		printf(" ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	free(T_test);
	free(o_T_test);
	free(IV_c);
	free(pt_test);
	free(ct_test);
	free(o_ct_test);

	return OK;
}

int test_gcm_strm_efence(void)
{
	gcm_vector test;
	int tag_len = 8;
	int t = 0;
	struct gcm_key_data *gkey = NULL;
	struct gcm_context_data *gctx = NULL;

	gkey = malloc(sizeof(struct gcm_key_data));
	gctx = malloc(sizeof(struct gcm_context_data));
	if (NULL == gkey || NULL == gctx)
		return 1;

	printf("AES GCM random efence test vectors with random stream:");
	for (t = 0; RANDOMS > t; t++) {
		int Plen = (rand() % TEST_LEN);
		//lengths must be a multiple of 4 bytes
		int aad_len = (rand() % TEST_LEN);
		int offset = (rand() % MAX_UNALIGNED);
		if (offset == 0 && aad_len == 0)
			offset = OFFSET_BASE_VALUE;

		if (0 == (t % 25))
			printf("\n");
		if (0 == (t % 10))
			fflush(0);
		test.P = NULL;
		test.C = NULL;
		test.A = NULL;
		test.T = NULL;
		test.Plen = Plen;
		if (test.Plen + offset != 0) {
			posix_memalign((void **)&test.P, 64, test.Plen + offset);
			posix_memalign((void **)&test.C, 64, test.Plen + offset);
		} else {	//This else clause is here becuase openssl 1.0.1k does not handle NULL pointers
			posix_memalign((void **)&test.P, 64, 16);
			posix_memalign((void **)&test.C, 64, 16);
		}
		test.K = malloc(GCM_128_KEY_LEN + offset);
		test.Klen = GCM_128_KEY_LEN;
		test.IV = malloc(GCM_IV_DATA_LEN + offset);
		test.IVlen = GCM_IV_DATA_LEN;
		test.A = malloc(aad_len + offset);
		test.Alen = aad_len;
		test.T = malloc(MAX_TAG_LEN + offset);

		if ((NULL == test.P && test.Plen != 0) || (NULL == test.K)
		    || (NULL == test.IV)) {
			printf("malloc of testsize:0x%x failed\n", Plen);
			return 1;
		}

		test.P += offset;
		test.C += offset;
		test.K += offset;
		test.IV += offset;
		test.A += offset;
		test.T += offset;

		mk_rand_data(test.P, test.Plen);
		mk_rand_data(test.K, test.Klen);
		mk_rand_data(test.IV, test.IVlen);
		mk_rand_data(test.A, test.Alen);

		// single Key length of 128bits/16bytes supported
		// single IV length of 96bits/12bytes supported
		// Tag lengths of 8, 12 or 16
		for (tag_len = 8; tag_len <= MAX_TAG_LEN;) {
			test.Tlen = tag_len;
			if (0 != check_strm_vector_efence(gkey, gctx, &test))
				return 1;
			tag_len += 4;	//supported lengths are 8, 12 or 16
		}
		test.A -= offset;
		free(test.A);
		test.C -= offset;
		free(test.C);
		test.IV -= offset;
		free(test.IV);
		test.K -= offset;
		free(test.K);
		test.P -= offset;
		free(test.P);
		test.T -= offset;
		free(test.T);
	}
	printf("\n");
	free(gkey);
	free(gctx);
	return 0;
}

int test_gcm_strm_combinations(int test_len)
{
	gcm_vector test;
	int tag_len = 8;
	int t = 0;
	uint8_t *gkeytemp = NULL;
	struct gcm_key_data *gkey = NULL;
	struct gcm_context_data *gctx = NULL;

	gkeytemp = malloc(sizeof(struct gcm_key_data) + 64);
	gctx = malloc(sizeof(struct gcm_context_data));
	gkey = (struct gcm_key_data *)(gkeytemp + rand() % 64);
	if (NULL == gkey || NULL == gctx)
		return 1;

	printf("AES GCM random test vectors with random stream of average size %d:",
	       test_len / 64);
	for (t = 0; RANDOMS > t; t++) {
		int Plen = 0;	// (rand() % test_len);
		//lengths must be a multiple of 4 bytes
		int aad_len = (rand() % test_len);
		int offset = (rand() % MAX_UNALIGNED);
		if (offset == 0 && aad_len == 0)
			offset = OFFSET_BASE_VALUE;

		if (0 == (t % 25))
			printf("\n");
		if (0 == (t % 10))
			fflush(0);
		test.P = NULL;
		test.C = NULL;
		test.A = NULL;
		test.T = NULL;
		test.Plen = Plen;
		if (test.Plen + offset != 0) {
			posix_memalign((void **)&test.P, 64, test.Plen + offset);
			posix_memalign((void **)&test.C, 64, test.Plen + offset);
		} else {	//This else clause is here becuase openssl 1.0.1k does not handle NULL pointers
			posix_memalign((void **)&test.P, 64, 16);
			posix_memalign((void **)&test.C, 64, 16);
		}
		test.K = malloc(GCM_128_KEY_LEN + offset);
		test.Klen = GCM_128_KEY_LEN;
		test.IV = malloc(GCM_IV_DATA_LEN + offset);
		test.IVlen = GCM_IV_DATA_LEN;
		test.A = malloc(aad_len + offset);

		test.Alen = aad_len;
		test.T = malloc(MAX_TAG_LEN + offset);

		if ((NULL == test.P && test.Plen != 0) || (NULL == test.K)
		    || (NULL == test.IV)) {
			printf("malloc of testsize:0x%x failed\n", Plen);
			return 1;
		}

		test.P += offset;
		test.C += offset;
		test.K += offset;
		test.IV += offset;
		test.A += offset;
		test.T += offset;

		mk_rand_data(test.P, test.Plen);
		mk_rand_data(test.K, test.Klen);
		mk_rand_data(test.IV, test.IVlen);
		mk_rand_data(test.A, test.Alen);

		// single Key length of 128bits/16bytes supported
		// single IV length of 96bits/12bytes supported
		// Tag lengths of 8, 12 or 16
		for (tag_len = 8; tag_len <= MAX_TAG_LEN;) {
			test.Tlen = tag_len;
			if (0 != check_strm_vector(gkey, gctx, &test, test_len))
				return 1;
			tag_len += 4;	//supported lengths are 8, 12 or 16
		}
		test.A -= offset;
		free(test.A);
		test.C -= offset;
		free(test.C);
		test.IV -= offset;
		free(test.IV);
		test.K -= offset;
		free(test.K);
		test.P -= offset;
		free(test.P);
		test.T -= offset;
		free(test.T);
	}
	printf("\n");
	free(gkeytemp);
	free(gctx);
	return 0;
}

int test_gcm_combinations(void)
{
	gcm_vector test;
	int tag_len = 8;
	int t = 0;
	struct gcm_key_data *gkey = NULL;
	struct gcm_context_data *gctx = NULL;

	gkey = malloc(sizeof(struct gcm_key_data));
	gctx = malloc(sizeof(struct gcm_context_data));
	if (NULL == gkey || NULL == gctx)
		return 1;

	printf("AES GCM random test vectors:");
	for (t = 0; RANDOMS > t; t++) {
		int Plen = (rand() % TEST_LEN);
		//lengths must be a multiple of 4 bytes
		int aad_len = (rand() % TEST_LEN);
		int offset = (rand() % MAX_UNALIGNED);
		if (offset == 0 && aad_len == 0)
			offset = OFFSET_BASE_VALUE;

		if (0 == (t % 25))
			printf("\n");
		if (0 == (t % 10))
			fflush(0);
		test.P = NULL;
		test.C = NULL;
		test.A = NULL;
		test.T = NULL;
		test.Plen = Plen;
		if (test.Plen + offset != 0) {
			posix_memalign((void **)&test.P, 64, test.Plen + offset);
			posix_memalign((void **)&test.C, 64, test.Plen + offset);
		} else {	//This else clause is here becuase openssl 1.0.1k does not handle NULL pointers
			posix_memalign((void **)&test.P, 64, 16);
			posix_memalign((void **)&test.C, 64, 16);
		}
		test.K = malloc(GCM_128_KEY_LEN + offset);
		test.Klen = GCM_128_KEY_LEN;
		test.IV = malloc(GCM_IV_DATA_LEN + offset);
		test.IVlen = GCM_IV_DATA_LEN;
		test.A = malloc(aad_len + offset);

		test.Alen = aad_len;
		test.T = malloc(MAX_TAG_LEN + offset);

		if ((NULL == test.P && test.Plen != 0) || (NULL == test.K)
		    || (NULL == test.IV)) {
			printf("malloc of testsize:0x%x failed\n", Plen);
			return 1;
		}

		test.P += offset;
		test.C += offset;
		test.K += offset;
		test.IV += offset;
		test.A += offset;
		test.T += offset;

		mk_rand_data(test.P, test.Plen);
		mk_rand_data(test.K, test.Klen);
		mk_rand_data(test.IV, test.IVlen);
		mk_rand_data(test.A, test.Alen);

		// single Key length of 128bits/16bytes supported
		// single IV length of 96bits/12bytes supported
		// Tag lengths of 8, 12 or 16
		for (tag_len = 8; tag_len <= MAX_TAG_LEN;) {
			test.Tlen = tag_len;
			if (0 != check_vector(gkey, gctx, &test))
				return 1;
			tag_len += 4;	//supported lengths are 8, 12 or 16
		}
		test.A -= offset;
		free(test.A);
		test.C -= offset;
		free(test.C);
		test.IV -= offset;
		free(test.IV);
		test.K -= offset;
		free(test.K);
		test.P -= offset;
		free(test.P);
		test.T -= offset;
		free(test.T);
	}
	printf("\n");
	free(gkey);
	free(gctx);
	return 0;
}

int test_gcm256_combinations(void)
{
	gcm_vector test;
	int tag_len = 8;
	int t = 0;
	struct gcm_key_data *gkey = NULL;
	struct gcm_context_data *gctx = NULL;

	gkey = malloc(sizeof(struct gcm_key_data));
	gctx = malloc(sizeof(struct gcm_context_data));
	if (NULL == gkey || NULL == gctx)
		return 1;

	printf("AES-GCM-256 random test vectors:");
	for (t = 0; RANDOMS > t; t++) {
		int Plen = (rand() % TEST_LEN);
		//lengths must be a multiple of 4 bytes
		int aad_len = (rand() % TEST_LEN);
		int offset = (rand() % MAX_UNALIGNED);
		if (offset == 0 && aad_len == 0)
			offset = OFFSET_BASE_VALUE;

		if (0 == (t % 25))
			printf("\n");
		if (0 == (t % 10))
			fflush(0);
		test.P = NULL;
		test.C = NULL;
		test.A = NULL;
		test.T = NULL;
		test.Plen = Plen;
		if (test.Plen + offset != 0) {
			posix_memalign((void **)&test.P, 64, test.Plen + offset);
			posix_memalign((void **)&test.C, 64, test.Plen + offset);
		} else {	//This else clause is here becuase openssl 1.0.1k does not handle NULL pointers
			posix_memalign((void **)&test.P, 64, 16);
			posix_memalign((void **)&test.C, 64, 16);
		}
		test.K = malloc(GCM_256_KEY_LEN + offset);
		test.Klen = GCM_256_KEY_LEN;
		test.IV = malloc(GCM_IV_DATA_LEN + offset);
		test.IVlen = GCM_IV_DATA_LEN;
		test.A = malloc(aad_len + offset);

		test.Alen = aad_len;
		test.T = malloc(MAX_TAG_LEN + offset);

		if ((NULL == test.P && test.Plen != 0) || (NULL == test.K)
		    || (NULL == test.IV)) {
			printf("malloc of testsize:0x%x failed\n", Plen);
			return 1;
		}

		test.P += offset;
		test.C += offset;
		test.K += offset;
		test.IV += offset;
		test.A += offset;
		test.T += offset;

		mk_rand_data(test.P, test.Plen);
		mk_rand_data(test.K, test.Klen);
		mk_rand_data(test.IV, test.IVlen);
		mk_rand_data(test.A, test.Alen);

		// single Key length of 128bits/16bytes supported
		// single IV length of 96bits/12bytes supported
		// Tag lengths of 8, 12 or 16
		for (tag_len = 8; tag_len <= MAX_TAG_LEN;) {
			test.Tlen = tag_len;
			if (0 != check_256_vector(gkey, gctx, &test))
				return 1;
			tag_len += 4;	//supported lengths are 8, 12 or 16
		}
		test.A -= offset;
		free(test.A);
		test.C -= offset;
		free(test.C);
		test.IV -= offset;
		free(test.IV);
		test.K -= offset;
		free(test.K);
		test.P -= offset;
		free(test.P);
		test.T -= offset;
		free(test.T);
	}
	printf("\n");
	free(gkey);
	free(gctx);
	return 0;
}

int test_gcm256_strm_combinations(int test_len)
{
	gcm_vector test;
	int tag_len = 8;
	int t = 0;
	uint8_t *gkeytemp = NULL;
	struct gcm_key_data *gkey = NULL;
	struct gcm_context_data *gctx = NULL;

	gkeytemp = malloc(sizeof(struct gcm_key_data) + 64);
	gctx = malloc(sizeof(struct gcm_context_data));
	gkey = (struct gcm_key_data *)(gkeytemp + rand() % 64);
	if (NULL == gkey || NULL == gctx)
		return 1;

	printf("AES-GCM-256 random test vectors with random stream of average size %d:",
	       test_len / 64);
	for (t = 0; RANDOMS > t; t++) {
		int Plen = (rand() % test_len);
		//lengths must be a multiple of 4 bytes
		int aad_len = (rand() % test_len);
		int offset = (rand() % MAX_UNALIGNED);
		if (offset == 0 && aad_len == 0)
			offset = OFFSET_BASE_VALUE;

		if (0 == (t % 25))
			printf("\n");
		if (0 == (t % 10))
			fflush(0);
		test.P = NULL;
		test.C = NULL;
		test.A = NULL;
		test.T = NULL;
		test.Plen = Plen;
		if (test.Plen + offset != 0) {
			posix_memalign((void **)&test.P, 64, test.Plen + offset);
			posix_memalign((void **)&test.C, 64, test.Plen + offset);
		} else {	//This else clause is here becuase openssl 1.0.1k does not handle NULL pointers
			posix_memalign((void **)&test.P, 64, 16);
			posix_memalign((void **)&test.C, 64, 16);
		}
		test.K = malloc(GCM_256_KEY_LEN + offset);
		test.Klen = GCM_256_KEY_LEN;
		test.IV = malloc(GCM_IV_DATA_LEN + offset);
		test.IVlen = GCM_IV_DATA_LEN;
		test.A = malloc(aad_len + offset);

		test.Alen = aad_len;
		test.T = malloc(MAX_TAG_LEN + offset);

		if ((NULL == test.P && test.Plen != 0) || (NULL == test.K)
		    || (NULL == test.IV)) {
			printf("malloc of testsize:0x%x failed\n", Plen);
			return 1;
		}

		test.P += offset;
		test.C += offset;
		test.K += offset;
		test.IV += offset;
		test.A += offset;
		test.T += offset;

		mk_rand_data(test.P, test.Plen);
		mk_rand_data(test.K, test.Klen);
		mk_rand_data(test.IV, test.IVlen);
		mk_rand_data(test.A, test.Alen);

		// single Key length of 128bits/16bytes supported
		// single IV length of 96bits/12bytes supported
		// Tag lengths of 8, 12 or 16
		for (tag_len = 8; tag_len <= MAX_TAG_LEN;) {
			test.Tlen = tag_len;
			if (0 != check_256_strm_vector(gkey, gctx, &test, test_len))
				return 1;
			tag_len += 4;	//supported lengths are 8, 12 or 16
		}
		test.A -= offset;
		free(test.A);
		test.C -= offset;
		free(test.C);
		test.IV -= offset;
		free(test.IV);
		test.K -= offset;
		free(test.K);
		test.P -= offset;
		free(test.P);
		test.T -= offset;
		free(test.T);
	}
	printf("\n");
	free(gkeytemp);
	free(gctx);
	return 0;
}

//
// place all data to end at a page boundary to check for read past the end
//
int test_gcm_efence(void)
{
	gcm_vector test;
	int offset = 0;
	gcm_key_size key_len;
	struct gcm_key_data *gkey = NULL;
	struct gcm_context_data *gctx = NULL;
	uint8_t *P = NULL, *C = NULL, *K, *IV, *A, *T;

	gkey = malloc(sizeof(struct gcm_key_data));
	gctx = malloc(sizeof(struct gcm_context_data));
	posix_memalign((void **)&P, 64, PAGE_LEN);
	posix_memalign((void **)&C, 64, PAGE_LEN);
	K = malloc(PAGE_LEN);
	IV = malloc(PAGE_LEN);
	A = malloc(PAGE_LEN);
	T = malloc(PAGE_LEN);
	if ((NULL == P) || (NULL == C) || (NULL == K) || (NULL == IV) || (NULL == A)
	    || (NULL == T) || (NULL == gkey) || (NULL == gctx)) {
		printf("malloc of testsize:0x%x failed\n", PAGE_LEN);
		return -1;
	}

	test.Plen = PAGE_LEN / 2;
	// place buffers to end at page boundary
	test.IVlen = GCM_IV_DATA_LEN;
	test.Alen = test.Plen;
	test.Tlen = MAX_TAG_LEN;

	printf("AES GCM efence test vectors:");
	for (key_len = GCM_128_KEY_LEN; GCM_256_KEY_LEN >= key_len;
	     key_len += (GCM_256_KEY_LEN - GCM_128_KEY_LEN)) {
		test.Klen = key_len;
		for (offset = 0; MAX_UNALIGNED > offset; offset++) {
			if (0 == (offset % 80))
				printf("\n");
			// move the start and size of the data block towards the end of the page
			test.Plen = (PAGE_LEN / 2) - offset;
			test.Alen = (PAGE_LEN / 4) - (offset * 4);	//lengths must be a multiple of 4 bytes
			//Place data at end of page
			test.P = P + PAGE_LEN - test.Plen;
			test.C = C + PAGE_LEN - test.Plen;
			test.K = K + PAGE_LEN - test.Klen;
			test.IV = IV + PAGE_LEN - test.IVlen;
			test.A = A + PAGE_LEN - test.Alen;
			test.T = T + PAGE_LEN - test.Tlen;

			mk_rand_data(test.P, test.Plen);
			mk_rand_data(test.K, test.Klen);
			mk_rand_data(test.IV, test.IVlen);
			mk_rand_data(test.A, test.Alen);
			if (GCM_128_KEY_LEN == key_len) {
				if (0 != check_vector(gkey, gctx, &test))
					return 1;
			} else {
				if (0 != check_256_vector(gkey, gctx, &test))
					return 1;
			}
		}
	}
	free(gkey);
	free(gctx);
	free(P);
	free(C);
	free(K);
	free(IV);
	free(A);
	free(T);

	printf("\n");
	return 0;
}

int test_gcm128_std_vectors(gcm_vector const *vector)
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
	int result;

#ifdef GCM_VECTORS_VERBOSE
	printf("AES-GCM-128:\n");
#endif

	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&ct_test, 64, vector->Plen);
	if (ct_test == NULL) {
		fprintf(stderr, "Can't allocate ciphertext memory\n");
		return 1;
	}
	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&pt_test, 64, vector->Plen);
	if (pt_test == NULL) {
		fprintf(stderr, "Can't allocate plaintext memory\n");
		return 1;
	}
	IV_alloc_len = vector->IVlen;

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
#ifdef GCM_VECTORS_VERBOSE
	dump_gcm_data(&gkey);
#endif

	////
	// ISA-l Encrypt
	////
	memset(ct_test, 0, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_128_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(ct_test, vector->C, vector->Plen, "ISA-L encrypted cypher text (C)");
	OK |= check_data(T_test, vector->T, vector->Tlen, "ISA-L tag (T)");

	openssl_aes_gcm_enc(vector->K, vector->IV,
			    vector->IVlen, vector->A,
			    vector->Alen, pt_test, vector->Tlen,
			    vector->P, vector->Plen, ct_test);
	OK |= check_data(pt_test, T_test, vector->Tlen, "OpenSSL vs ISA-L tag (T)");
	// test of in-place encrypt
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_128_nt(&gkey, &gctx, pt_test, pt_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T_test, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->C, vector->Plen,
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
	// OpenSSl enc -> ISA-L dec
	openssl_aes_gcm_enc(vector->K, vector->IV,
			    vector->IVlen, vector->A,
			    vector->Alen, T_test, vector->Tlen,
			    vector->P, vector->Plen, ct_test);
	OK |=
	    check_data(ct_test, vector->C, vector->Plen, "OpenSSL encrypted cypher text (C)");

	memset(pt_test, 0, vector->Plen);
	aes_gcm_dec_128_nt(&gkey, &gctx, pt_test, ct_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T2_test, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "OpenSSL->ISA-L decrypted plain text (P)");
	OK |= check_data(T_test, T2_test, vector->Tlen, "OpenSSL->ISA-L decrypted tag (T)");
	// ISA-L enc -> OpenSSl dec
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_128_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	memset(pt_test, 0, vector->Plen);
	result =
	    openssl_aes_gcm_dec(vector->K, vector->IV,
				vector->IVlen, vector->A,
				vector->Alen, T_test, vector->Tlen,
				ct_test, vector->Plen, pt_test);
	if (-1 == result)
		printf("  ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	OK |= check_data(pt_test, vector->P, vector->Plen, "OSSL decrypted plain text (C)");
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

int test_gcm256_std_vectors(gcm_vector const *vector)
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
	int result;

#ifdef GCM_VECTORS_VERBOSE
	printf("AES-GCM-256:\n");
#endif

	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&ct_test, 64, vector->Plen);
	// Allocate space for the calculated ciphertext
	posix_memalign((void **)&pt_test, 64, vector->Plen);
	if ((ct_test == NULL) || (pt_test == NULL)) {
		fprintf(stderr, "Can't allocate ciphertext or plaintext memory\n");
		return 1;
	}
	IV_alloc_len = vector->IVlen;

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
#ifdef GCM_VECTORS_VERBOSE
	dump_gcm_data(&gkey);
#endif

	////
	// ISA-l Encrypt
	////
	memset(ct_test, 0, vector->Plen);
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_256_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	OK |= check_data(ct_test, vector->C, vector->Plen, "ISA-L encrypted cypher text (C)");
	OK |= check_data(T_test, vector->T, vector->Tlen, "ISA-L tag (T)");

	openssl_aes_256_gcm_enc(vector->K, vector->IV,
				vector->IVlen, vector->A,
				vector->Alen, pt_test, vector->Tlen,
				vector->P, vector->Plen, ct_test);
	OK |= check_data(ct_test, vector->C, vector->Tlen, "OpenSSL vs KA - cypher text (C)");
	OK |= check_data(pt_test, vector->T, vector->Tlen, "OpenSSL vs KA - tag (T)");
	OK |= check_data(pt_test, T_test, vector->Tlen, "OpenSSL vs ISA-L - tag (T)");
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
	// OpenSSl enc -> ISA-L dec
	openssl_aes_256_gcm_enc(vector->K, vector->IV,
				vector->IVlen, vector->A,
				vector->Alen, T_test, vector->Tlen,
				vector->P, vector->Plen, ct_test);
	OK |=
	    check_data(ct_test, vector->C, vector->Plen, "OpenSSL encrypted cypher text (C)");
	memset(pt_test, 0, vector->Plen);
	aes_gcm_dec_256_nt(&gkey, &gctx, pt_test, ct_test, vector->Plen, IV_c,
			   vector->A, vector->Alen, T2_test, vector->Tlen);
	OK |=
	    check_data(pt_test, vector->P, vector->Plen,
		       "OpenSSL->ISA-L decrypted plain text (P)");
	OK |= check_data(T_test, T2_test, vector->Tlen, "OpenSSL->ISA-L decrypted tag (T)");
	// ISA-L enc -> OpenSSl dec
	memcpy(pt_test, vector->P, vector->Plen);
	aes_gcm_enc_256_nt(&gkey, &gctx, ct_test, pt_test, vector->Plen,
			   IV_c, vector->A, vector->Alen, T_test, vector->Tlen);
	memset(pt_test, 0, vector->Plen);
	result =
	    openssl_aes_256_gcm_dec(vector->K, vector->IV,
				    vector->IVlen, vector->A,
				    vector->Alen, T_test, vector->Tlen,
				    ct_test, vector->Plen, pt_test);
	if (-1 == result)
		printf("  ISA-L->OpenSSL decryption failed Authentication\n");
	OK |= (-1 == result);
	OK |= check_data(pt_test, vector->P, vector->Plen, "OSSL decrypted plain text (C)");
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

int test_gcm_std_vectors(void)
{
	int const vectors_cnt = sizeof(gcm_vectors) / sizeof(gcm_vectors[0]);
	int vect;
	int OK = 0;

	printf("AES-GCM standard test vectors:\n");
	for (vect = 0; vect < vectors_cnt; vect++) {
#ifdef GCM_VECTORS_VERBOSE
		printf
		    ("Standard vector %d/%d  Keylen:%d IVlen:%d PTLen:%d AADlen:%d Tlen:%d\n",
		     vect, vectors_cnt - 1, (int)gcm_vectors[vect].Klen,
		     (int)gcm_vectors[vect].IVlen, (int)gcm_vectors[vect].Plen,
		     (int)gcm_vectors[vect].Alen, (int)gcm_vectors[vect].Tlen);
#else
		printf(".");
#endif

		if (BITS_128 == gcm_vectors[vect].Klen) {
			OK |= test_gcm128_std_vectors(&gcm_vectors[vect]);
		} else {
			OK |= test_gcm256_std_vectors(&gcm_vectors[vect]);
		}
		if (0 != OK)
			return OK;
	}
	printf("\n");
	return OK;
}

// The length of the data is set to length. The first stream is from 0 to start. After
// that the data is broken into breaks chunks of equal size (except possibly the last
// one due to divisibility).
int test_gcm_strm_combinations2(int length, int start, int breaks)
{
	gcm_vector test;
	int tag_len = 8;
	int t = 0;
	struct gcm_key_data *gkey = NULL;
	struct gcm_context_data *gctx = NULL;

	gkey = malloc(sizeof(struct gcm_key_data));
	gctx = malloc(sizeof(struct gcm_context_data));
	if (NULL == gkey || NULL == gctx)
		return 1;

	printf("AES GCM random test vectors of length %d and stream with %d breaks:", length,
	       breaks + 1);
	for (t = 0; RANDOMS > t; t++) {
		int Plen = length;
		//lengths must be a multiple of 4 bytes
		int aad_len = (rand() % TEST_LEN);
		int offset = (rand() % MAX_UNALIGNED);
		if (offset == 0 && aad_len == 0)
			offset = OFFSET_BASE_VALUE;

		if (0 == (t % 25))
			printf("\n");
		if (0 == (t % 10))
			fflush(0);
		test.P = NULL;
		test.C = NULL;
		test.A = NULL;
		test.T = NULL;
		test.Plen = Plen;
		if (test.Plen + offset != 0) {
			posix_memalign((void **)&test.P, 64, test.Plen + offset);
			posix_memalign((void **)&test.C, 64, test.Plen + offset);
		} else {	//This else clause is here becuase openssl 1.0.1k does not handle NULL pointers
			posix_memalign((void **)&test.P, 64, 16);
			posix_memalign((void **)&test.C, 64, 16);
		}
		test.K = malloc(GCM_128_KEY_LEN + offset);
		test.Klen = GCM_128_KEY_LEN;
		test.IV = malloc(GCM_IV_DATA_LEN + offset);
		test.IVlen = GCM_IV_DATA_LEN;
		test.A = malloc(aad_len + offset);

		test.Alen = aad_len;
		test.T = malloc(MAX_TAG_LEN + offset);

		if ((NULL == test.P && test.Plen != 0) || (NULL == test.K)
		    || (NULL == test.IV)) {
			printf("malloc of testsize:0x%x failed\n", Plen);
			return 1;
		}

		test.P += offset;
		test.C += offset;
		test.K += offset;
		test.IV += offset;
		test.A += offset;
		test.T += offset;

		mk_rand_data(test.P, test.Plen);
		mk_rand_data(test.K, test.Klen);
		mk_rand_data(test.IV, test.IVlen);
		mk_rand_data(test.A, test.Alen);

		// single Key length of 128bits/16bytes supported
		// single IV length of 96bits/12bytes supported
		// Tag lengths of 8, 12 or 16
		for (tag_len = 8; tag_len <= MAX_TAG_LEN;) {
			test.Tlen = tag_len;
			if (0 != check_strm_vector2(gkey, gctx, &test, length, start, breaks))
				return 1;
			tag_len += 4;	//supported lengths are 8, 12 or 16
		}
		test.A -= offset;
		free(test.A);
		test.C -= offset;
		free(test.C);
		test.IV -= offset;
		free(test.IV);
		test.K -= offset;
		free(test.K);
		test.P -= offset;
		free(test.P);
		test.T -= offset;
		free(test.T);
	}
	printf("\n");
	free(gkey);
	free(gctx);
	return 0;
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

	errors += test_gcm_std_vectors();
	errors += test_gcm256_combinations();
	errors += test_gcm_combinations();
	errors += test_gcm_efence();
	errors += test_gcm256_strm_combinations(TEST_LEN);
	errors += test_gcm_strm_combinations(TEST_LEN);
	errors += test_gcm256_strm_combinations(1024);
	errors += test_gcm_strm_combinations(1024);
	errors += test_gcm_strm_efence();
	errors += test_gcm_strm_combinations2(1024, 0, 1024);

	if (0 == errors)
		printf("...Pass\n");
	else
		printf("...Fail\n");

	return errors;
}
