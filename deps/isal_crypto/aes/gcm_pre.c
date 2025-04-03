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

#include <aes_gcm.h>
#include <aes_keyexp.h>

void aes_keyexp_128_enc(const void *, uint8_t *);
void aes_gcm_precomp_128(struct gcm_key_data *key_data);
void aes_gcm_precomp_256(struct gcm_key_data *key_data);

void aes_gcm_pre_128(const void *key, struct gcm_key_data *key_data)
{
	aes_keyexp_128_enc(key, key_data->expanded_keys);
	aes_gcm_precomp_128(key_data);
}

void aes_gcm_pre_256(const void *key, struct gcm_key_data *key_data)
{
	uint8_t tmp_exp_key[GCM_ENC_KEY_LEN * GCM_KEY_SETS];
	aes_keyexp_256((const uint8_t *)key, (uint8_t *) key_data->expanded_keys, tmp_exp_key);
	aes_gcm_precomp_256(key_data);
}

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};

// Version info
struct slver aes_gcm_pre_128_slver_000002c7;
struct slver aes_gcm_pre_128_slver = { 0x02c7, 0x00, 0x00 };

struct slver aes_gcm_pre_256_slver_000002d7;
struct slver aes_gcm_pre_256_slver = { 0x02d7, 0x00, 0x00 };
