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

#ifndef _KEYEXP_128_H
#define _KEYEXP_128_H

/**
 *  @file aes_keyexp.h
 *  @brief AES key expansion functions
 *
 * This defines the interface to key expansion functions.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief AES key expansion 128 bit
* @requires SSE4.1
*/
void aes_keyexp_128(
	const uint8_t *key,  		//!< input key for AES-128, 16 bytes
	uint8_t *exp_key_enc,	//!< expanded encryption keys, 16*11 bytes
	uint8_t *exp_key_dec	//!< expanded decryption keys, 16*11 bytes
	);

/** @brief AES key expansion 192 bit
* @requires SSE4.1
*/
void aes_keyexp_192(
	const uint8_t *key,	//!< input key for AES-192, 16*1.5 bytes
	uint8_t *exp_key_enc,	//!< expanded encryption keys, 16*13 bytes
	uint8_t *exp_key_dec	//!< expanded decryption keys, 16*13 bytes
	);

/** @brief AES key expansion 256 bit
* @requires SSE4.1
*/
void aes_keyexp_256(
	const uint8_t *key,	//!< input key for AES-256, 16*2 bytes
	uint8_t *exp_key_enc,	//!< expanded encryption keys, 16*15 bytes
	uint8_t *exp_key_dec	//!< expanded decryption keys, 16*15 bytes
	);

#ifdef __cplusplus
}
#endif //__cplusplus
#endif //ifndef _KEYEXP_128_H
