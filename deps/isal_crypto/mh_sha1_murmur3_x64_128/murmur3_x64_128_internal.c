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

#include "mh_sha1_murmur3_x64_128_internal.h"
#include <stdlib.h>		// for NULL

/* murmur3_x64_128 constants */
// Shift bits of circle rotate
#define MUR_SH1		31
#define MUR_SH2		33
#define MUR_SH3		27
#define MUR_SH4		31
#define MUR_SH5		33

#define MUR_MUL		5
#define MUR_ADD1	0x52dce729
#define MUR_ADD2	0x38495ab5

#define MUR_CON1	0x87c37b91114253d5LLU
#define MUR_CON2	0x4cf5ad432745937fLLU

#define MUR_FMUL1	0xff51afd7ed558ccdLLU
#define MUR_FMUL2	0xc4ceb9fe1a85ec53LLU

/* murmur3_x64_128 inline functions */
static inline uint64_t blockmix64(uint64_t data, uint64_t conA, uint64_t conB, uint64_t shift)
{
	data *= conA;
	data = (data << shift) | (data >> (64 - shift));
	data *= conB;
	return data;
}

static inline uint64_t hashmix64(uint64_t hashA, uint64_t hashB, uint64_t data, uint64_t add,
				 uint64_t shift)
{
	hashA ^= data;
	hashA = (hashA << shift) | (hashA >> (64 - shift));
	hashA += hashB;
	hashA = hashA * MUR_MUL + add;
	return hashA;
}

void murmur3_x64_128_block(const uint8_t * input_data, uint32_t num_blocks,
			   uint32_t digests[MURMUR3_x64_128_DIGEST_WORDS])
{
	uint64_t data1, data2;
	uint64_t *input_qword = (uint64_t *) input_data;
	uint64_t *hash = (uint64_t *) digests;
	uint32_t i = 0;

	while (i < num_blocks) {
		data1 = input_qword[i * 2];
		data2 = input_qword[i * 2 + 1];
		data1 = blockmix64(data1, MUR_CON1, MUR_CON2, MUR_SH1);
		data2 = blockmix64(data2, MUR_CON2, MUR_CON1, MUR_SH2);
		hash[0] = hashmix64(hash[0], hash[1], data1, MUR_ADD1, MUR_SH3);
		hash[1] = hashmix64(hash[1], hash[0], data2, MUR_ADD2, MUR_SH4);
		i++;
	}

	return;
}

void murmur3_x64_128_tail(const uint8_t * tail_buffer, uint32_t total_len,
			  uint32_t digests[MURMUR3_x64_128_DIGEST_WORDS])
{
	uint64_t data1, data2;
	uint64_t *hash = (uint64_t *) digests;
	uint64_t tail_len = total_len % 16;
	uint8_t *tail = (uint8_t *) tail_buffer;

	union {
		uint64_t hash[2];
		uint8_t hashB[16];
	} hashU;

	// tail
	hashU.hash[0] = hashU.hash[1] = 0;

	while (tail_len-- > 0)
		hashU.hashB[tail_len] = tail[tail_len];

	data1 = hashU.hash[0];
	data2 = hashU.hash[1];

	data1 = blockmix64(data1, MUR_CON1, MUR_CON2, MUR_SH1);
	data2 = blockmix64(data2, MUR_CON2, MUR_CON1, MUR_SH2);

	hash[0] ^= total_len ^ data1;
	hash[1] ^= total_len ^ data2;

	hash[0] += hash[1];
	hash[1] += hash[0];

	hash[0] ^= hash[0] >> MUR_SH5;
	hash[0] *= MUR_FMUL1;
	hash[0] ^= hash[0] >> MUR_SH5;
	hash[0] *= MUR_FMUL2;
	hash[0] ^= hash[0] >> MUR_SH5;

	hash[1] ^= hash[1] >> MUR_SH5;
	hash[1] *= MUR_FMUL1;
	hash[1] ^= hash[1] >> MUR_SH5;
	hash[1] *= MUR_FMUL2;
	hash[1] ^= hash[1] >> MUR_SH5;

	hash[0] += hash[1];
	hash[1] += hash[0];

	return;
}
