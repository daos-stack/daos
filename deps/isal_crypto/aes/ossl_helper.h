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

#ifndef AES_OSSL_HELPER_H_
#define AES_OSSL_HELPER_H_

#ifdef _MSC_VER
# define inline __inline
#endif

#include <openssl/evp.h>

static inline
    int openssl_aes_128_cbc_dec(uint8_t * key, uint8_t * iv,
				int len, uint8_t * cyphertext, uint8_t * plaintext)
{
	int outlen = 0, tmplen = 0;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv))
		printf("\n ERROR!! EVP_DecryptInit_ex - EVP_aes_128_cbc\n");
	if (!EVP_CIPHER_CTX_set_padding(ctx, 0))
		printf("\n ERROR!! EVP_CIPHER_CTX_set_padding - no padding\n");
	if (!EVP_DecryptUpdate(ctx, plaintext, &outlen, (uint8_t const *)cyphertext, len))
		printf("\n ERROR!! EVP_DecryptUpdate - EVP_aes_128_cbc\n");
	if (!EVP_DecryptFinal_ex(ctx, &plaintext[outlen], &tmplen))
		printf("\n ERROR!! EVP_DecryptFinal_ex - EVP_aes_128_cbc %x, %x, %x\n", len,
		       outlen, tmplen);

	EVP_CIPHER_CTX_free(ctx);
	return tmplen;
}

static inline
    int openssl_aes_128_cbc_enc(uint8_t * key, uint8_t * iv,
				int len, uint8_t * plaintext, uint8_t * cyphertext)
{
	int outlen, tmplen;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv))
		printf("\n ERROR!! EVP_EncryptInit_ex - EVP_aes_128_cbc\n");
	if (!EVP_CIPHER_CTX_set_padding(ctx, 0))
		printf("\n ERROR!! EVP_CIPHER_CTX_set_padding - no padding\n");
	if (!EVP_EncryptUpdate
	    (ctx, cyphertext, &outlen, (const unsigned char *)plaintext, len))
		printf("\n ERROR!! EVP_EncryptUpdate - EVP_aes_128_cbc\n");
	if (!EVP_EncryptFinal_ex(ctx, cyphertext + outlen, &tmplen))
		printf("\n ERROR!! EVP_EncryptFinal_ex - EVP_aes_128_cbc\n");

	EVP_CIPHER_CTX_free(ctx);
	return tmplen;
}

static inline
    int openssl_aes_192_cbc_dec(uint8_t * key, uint8_t * iv,
				int len, uint8_t * cyphertext, uint8_t * plaintext)
{
	int outlen = 0, tmplen = 0;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_DecryptInit_ex(ctx, EVP_aes_192_cbc(), NULL, key, iv))
		printf("\n ERROR!! EVP_DecryptInit_ex - EVP_aes_192_cbc\n");
	if (!EVP_CIPHER_CTX_set_padding(ctx, 0))
		printf("\n ERROR!! EVP_CIPHER_CTX_set_padding - no padding\n");
	if (!EVP_DecryptUpdate
	    (ctx, plaintext, &outlen, (const unsigned char *)cyphertext, len))
		printf("\n ERROR!! EVP_DecryptUpdate - EVP_aes_192_cbc\n");
	if (!EVP_DecryptFinal_ex(ctx, plaintext + outlen, &tmplen))
		printf("\n ERROR!! EVP_DecryptFinal_ex - EVP_aes_192_cbc \n");

	EVP_CIPHER_CTX_free(ctx);
	return 0;
}

static inline
    int openssl_aes_192_cbc_enc(uint8_t * key, uint8_t * iv,
				int len, uint8_t * plaintext, uint8_t * cyphertext)
{
	int outlen, tmplen;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_EncryptInit_ex(ctx, EVP_aes_192_cbc(), NULL, key, iv))
		printf("\n ERROR!! EVP_EncryptInit_ex - EVP_aes_192_cbc\n");
	if (!EVP_CIPHER_CTX_set_padding(ctx, 0))
		printf("\n ERROR!! EVP_CIPHER_CTX_set_padding - no padding\n");
	if (!EVP_EncryptUpdate
	    (ctx, cyphertext, &outlen, (const unsigned char *)plaintext, len))
		printf("\n ERROR!! EVP_EncryptUpdate - EVP_aes_192_cbc\n");
	if (!EVP_EncryptFinal_ex(ctx, cyphertext + outlen, &tmplen))
		printf("\n ERROR!! EVP_EncryptFinal_ex - EVP_aes_192_cbc\n");

	EVP_CIPHER_CTX_free(ctx);
	return 0;
}

static inline
    int openssl_aes_256_cbc_dec(uint8_t * key, uint8_t * iv,
				int len, uint8_t * cyphertext, uint8_t * plaintext)
{
	int outlen = 0, tmplen = 0;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv))
		printf("\n ERROR!! EVP_DecryptInit_ex - EVP_aes_256_cbc\n");
	if (!EVP_CIPHER_CTX_set_padding(ctx, 0))
		printf("\n ERROR!! EVP_CIPHER_CTX_set_padding - no padding\n");
	if (!EVP_DecryptUpdate
	    (ctx, plaintext, &outlen, (const unsigned char *)cyphertext, len))
		printf("\n ERROR!! EVP_DecryptUpdate - EVP_aes_256_cbc\n");
	if (!EVP_DecryptFinal_ex(ctx, plaintext + outlen, &tmplen))
		printf("\n ERROR!! EVP_DecryptFinal_ex - EVP_aes_256_cbc %x,%x\n", outlen,
		       tmplen);

	EVP_CIPHER_CTX_free(ctx);
	return 0;
}

static inline
    int openssl_aes_256_cbc_enc(uint8_t * key, uint8_t * iv,
				int len, uint8_t * plaintext, uint8_t * cyphertext)
{
	int outlen, tmplen;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv))
		printf("\n ERROR!! EVP_EncryptInit_ex - EVP_aes_256_cbc\n");
	if (!EVP_CIPHER_CTX_set_padding(ctx, 0))
		printf("\n ERROR!! EVP_CIPHER_CTX_set_padding - no padding\n");
	if (!EVP_EncryptUpdate
	    (ctx, cyphertext, &outlen, (const unsigned char *)plaintext, len))
		printf("\n ERROR!! EVP_EncryptUpdate - EVP_aes_256_cbc\n");
	if (!EVP_EncryptFinal_ex(ctx, cyphertext + outlen, &tmplen))
		printf("\n ERROR!! EVP_EncryptFinal_ex - EVP_aes_256_cbc\n");

	EVP_CIPHER_CTX_free(ctx);
	return 0;
}

static inline
    int openssl_aes_gcm_dec(uint8_t * key, uint8_t * iv, int iv_len, uint8_t * aad,
			    int aad_len, uint8_t * tag, int tag_len, uint8_t * cyphertext,
			    int len, uint8_t * plaintext)
{
	int outlen = 0, tmplen = len, ret;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL))
		printf("\n ERROR!! EVP_DecryptInit_ex - EVP_aes_128_gcm\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, tag))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - set tag\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - IV length init\n");
	if (!EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv))
		printf("\n ERROR!! EVP_DecryptInit_ex - key init\n");
	if (!EVP_DecryptUpdate(ctx, NULL, &outlen, aad, aad_len))
		printf("\n ERROR!! EVP_DecryptUpdate - aad data setup\n");
	if (!EVP_DecryptUpdate
	    (ctx, plaintext, &outlen, (const unsigned char *)cyphertext, len))
		printf("\n ERROR!! EVP_DecryptUpdate - PT->CT\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, tag))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - set tag\n");

	ret = EVP_DecryptFinal_ex(ctx, plaintext + outlen, &tmplen);
	if (0 < ret) {
		tmplen += outlen;
	} else {
		//Authentication failed mismatched key, ADD or tag
		tmplen = -1;
	}

	EVP_CIPHER_CTX_free(ctx);
	return tmplen;
}

static inline
    int openssl_aes_gcm_enc(uint8_t * key, uint8_t * iv, int iv_len, uint8_t * aad,
			    int aad_len, uint8_t * tag, int tag_len, uint8_t * plaintext,
			    int len, uint8_t * cyphertext)
{
	int outlen, tmplen;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	//printf("ivl:%x addl:%x tagl:%x ptl:%x\n", iv_len, aad_len, tag_len, len);
	if (!EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL))
		printf("\n ERROR!! EVP_EncryptInit_ex - EVP_aes_128_cbc\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - IV length init\n");
	if (!EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv))
		printf("\n ERROR!! EVP_EncryptInit_ex - init\n");
	if (!EVP_EncryptUpdate(ctx, NULL, &outlen, aad, aad_len))
		printf("\n ERROR!! EVP_EncryptUpdate - aad insert\n");
	if (!EVP_EncryptUpdate(ctx, cyphertext, &outlen, (const uint8_t *)plaintext, len))
		printf("\n ERROR!! EVP_EncryptUpdate - EVP_aes_128_cbc\n");
	if (!EVP_EncryptFinal_ex(ctx, cyphertext + outlen, &tmplen))
		printf("\n ERROR!! EVP_EncryptFinal_ex - EVP_aes_128_cbc\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag_len, tag))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - tag \n");

	EVP_CIPHER_CTX_free(ctx);
	return tmplen;
}

static inline
    int openssl_aes_256_gcm_dec(uint8_t * key, uint8_t * iv, int iv_len, uint8_t * aad,
				int aad_len, uint8_t * tag, int tag_len, uint8_t * cyphertext,
				int len, uint8_t * plaintext)
{
	int outlen = 0, tmplen = len, ret;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL))
		printf("\n ERROR!! EVP_DecryptInit_ex - EVP_aes_128_gcm\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, tag))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - set tag\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - IV length init\n");
	if (!EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv))
		printf("\n ERROR!! EVP_DecryptInit_ex - key init\n");
	if (!EVP_DecryptUpdate(ctx, NULL, &outlen, aad, aad_len))
		printf("\n ERROR!! EVP_DecryptUpdate - aad data setup\n");
	if (!EVP_DecryptUpdate
	    (ctx, plaintext, &outlen, (const unsigned char *)cyphertext, len))
		printf("\n ERROR!! EVP_DecryptUpdate - PT->CT\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag_len, tag))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - set tag\n");
	ret = EVP_DecryptFinal_ex(ctx, plaintext + outlen, &tmplen);
	if (0 < ret) {
		tmplen += outlen;
	} else {
		//Authentication failed mismatched key, ADD or tag
		tmplen = -1;
	}

	EVP_CIPHER_CTX_free(ctx);
	return tmplen;
}

static inline
    int openssl_aes_256_gcm_enc(uint8_t * key, uint8_t * iv, int iv_len, uint8_t * aad,
				int aad_len, uint8_t * tag, int tag_len, uint8_t * plaintext,
				int len, uint8_t * cyphertext)
{
	int outlen, tmplen;
	EVP_CIPHER_CTX *ctx;
	ctx = EVP_CIPHER_CTX_new();

	if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL))
		printf("\n ERROR!! EVP_EncryptInit_ex - EVP_aes_128_cbc\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - IV length init\n");
	if (!EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv))
		printf("\n ERROR!! EVP_EncryptInit_ex - init\n");
	if (!EVP_EncryptUpdate(ctx, NULL, &outlen, aad, aad_len))
		printf("\n ERROR!! EVP_EncryptUpdate - aad insert\n");
	if (!EVP_EncryptUpdate(ctx, cyphertext, &outlen, (const uint8_t *)plaintext, len))
		printf("\n ERROR!! EVP_EncryptUpdate - EVP_aes_128_cbc\n");
	if (!EVP_EncryptFinal_ex(ctx, cyphertext + outlen, &tmplen))
		printf("\n ERROR!! EVP_EncryptFinal_ex - EVP_aes_128_cbc\n");
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag_len, tag))
		printf("\n ERROR!! EVP_CIPHER_CTX_ctrl - tag \n");

	EVP_CIPHER_CTX_free(ctx);
	return tmplen;
}

#endif /* AES_OSSL_HELPER_H_ */
