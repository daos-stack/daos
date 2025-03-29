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

#include <stdlib.h>		// for NULL
#include "murmur3_x64_128_internal.c"
/*******************************************************************
 * Single API which can calculate murmur3
 ******************************************************************/
/**
 * @brief Get the digest of murmur3_x64_128 through a single API.
 *
 * Using murmur3_x64_128_block and murmur3_x64_128_tail.
 * Used to test the murmur3_x64_128 digest.
 *
 * @param  buffer Pointer to buffer to be processed
 * @param  len Length of buffer (in bytes) to be processed
 * @param  murmur_seed Seed as an initial digest of murmur3
 * @param  murmur3_x64_128_digest The digest of murmur3_x64_128
 * @returns none
 *
 */
void murmur3_x64_128(const void *buffer, uint32_t len, uint64_t murmur_seed,
		     uint32_t * murmur3_x64_128_digest)
{
	uint64_t *murmur3_x64_128_hash;
	uint32_t murmur3_x64_128_hash_dword[4];
	uint8_t *tail_buffer;
	const uint8_t *input_data = (const uint8_t *)buffer;

	// Initiate murmur3
	murmur3_x64_128_hash = (uint64_t *) murmur3_x64_128_hash_dword;
	murmur3_x64_128_hash[0] = murmur_seed;
	murmur3_x64_128_hash[1] = murmur_seed;

	// process bodies
	murmur3_x64_128_block((uint8_t *) input_data, len / MUR_BLOCK_SIZE,
			      murmur3_x64_128_hash_dword);

	// process finalize
	tail_buffer = (uint8_t *) input_data + len - len % MUR_BLOCK_SIZE;
	murmur3_x64_128_tail(tail_buffer, len, murmur3_x64_128_hash_dword);

	// output the digests
	if (murmur3_x64_128_digest != NULL) {
		murmur3_x64_128_digest[0] = murmur3_x64_128_hash_dword[0];
		murmur3_x64_128_digest[1] = murmur3_x64_128_hash_dword[1];
		murmur3_x64_128_digest[2] = murmur3_x64_128_hash_dword[2];
		murmur3_x64_128_digest[3] = murmur3_x64_128_hash_dword[3];
	}

	return;
}
