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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "aes_gcm.h"

#define TXT_SIZE  8
#define AAD_SIZE 32
#define TAG_SIZE 16		/* Valid values are 16, 12, or 8 */
#define KEY_SIZE GCM_256_KEY_LEN
#define IV_SIZE  GCM_IV_DATA_LEN

void mprint(const char *msg, uint8_t * buf, int len)
{
	int i;
	printf("%s", msg);
	for (i = 0; i < len;) {
		printf(" %2x", 0xff & buf[i++]);
		if (i % 32 == 0)
			printf("\n");
	}
	printf("\n");
}

int main(void)
{
	struct gcm_key_data gkey;
	struct gcm_context_data gctx;
	uint8_t ct[TXT_SIZE], pt[TXT_SIZE], pt2[TXT_SIZE];	// Cipher text and plain text
	uint8_t iv[IV_SIZE], aad[AAD_SIZE], key[KEY_SIZE];	// Key and authentication data
	uint8_t tag1[TAG_SIZE], tag2[TAG_SIZE];	// Authentication tags for encode and decode

	printf("gcm example:\n");
	memset(key, 0, KEY_SIZE);
	memset(pt, 0, TXT_SIZE);
	memset(iv, 0, IV_SIZE);
	memset(aad, 0, AAD_SIZE);

	aes_gcm_pre_256(key, &gkey);
	aes_gcm_enc_256(&gkey, &gctx, ct, pt, TXT_SIZE, iv, aad, AAD_SIZE, tag1, TAG_SIZE);
	aes_gcm_dec_256(&gkey, &gctx, pt2, ct, TXT_SIZE, iv, aad, AAD_SIZE, tag2, TAG_SIZE);

	mprint("  input text:     ", pt, TXT_SIZE);
	mprint("  cipher text:    ", ct, TXT_SIZE);
	mprint("  decode text:    ", pt2, TXT_SIZE);
	mprint("  ath tag1 (enc): ", tag1, TAG_SIZE);
	mprint("  ath tag2 (dec): ", tag2, TAG_SIZE);

	return memcmp(tag1, tag2, TAG_SIZE);
}
