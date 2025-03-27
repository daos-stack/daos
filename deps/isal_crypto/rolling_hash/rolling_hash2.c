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
#include <string.h>
#include "rolling_hashx.h"
#include "rolling_hash2_table.h"

extern
uint64_t rolling_hash2_run_until(uint32_t * idx, int max_idx, uint64_t * t1,
				 uint64_t * t2, uint8_t * b1, uint8_t * b2, uint64_t h,
				 uint64_t mask, uint64_t trigger);

int rolling_hash2_init(struct rh_state2 *state, uint32_t w)
{
	uint32_t i;
	uint64_t v;

	if (w > FINGERPRINT_MAX_WINDOW)
		return -1;

	for (i = 0; i < 256; i++) {
		v = rolling_hash2_table1[i];
		state->table1[i] = v;
		state->table2[i] = (v << w) | (v >> (64 - w));
	}
	state->w = w;
	return 0;
}

void rolling_hash2_reset(struct rh_state2 *state, uint8_t * init_bytes)
{
	uint64_t hash;
	uint32_t i, w;

	hash = 0;
	w = state->w;
	for (i = 0; i < w; i++) {
		hash = (hash << 1) | (hash >> (64 - 1));
		hash ^= state->table1[init_bytes[i]];
	}
	state->hash = hash;
	memcpy(state->history, init_bytes, w);
}

static
uint64_t hash_fn(struct rh_state2 *state, uint64_t h, uint8_t new_char, uint8_t old_char)
{
	h = (h << 1) | (h >> (64 - 1));
	h ^= state->table1[new_char] ^ state->table2[old_char];
	return h;
}

uint64_t rolling_hash2_run_until_base(uint32_t * idx, int max_idx, uint64_t * t1,
				      uint64_t * t2, uint8_t * b1, uint8_t * b2, uint64_t h,
				      uint64_t mask, uint64_t trigger)
{
	int i = *idx;

	if (trigger == 0) {
		for (; i < max_idx; i++) {
			h = (h << 1) | (h >> (64 - 1));
			h ^= t1[b1[i]] ^ t2[b2[i]];
			if ((h & mask) == 0) {
				*idx = i;
				return h;
			}
		}
	} else {
		for (; i < max_idx; i++) {
			h = (h << 1) | (h >> (64 - 1));
			h ^= t1[b1[i]] ^ t2[b2[i]];
			if ((h & mask) == trigger) {
				*idx = i;
				return h;
			}
		}
	}
	*idx = i;
	return h;
}

int
rolling_hash2_run(struct rh_state2 *state, uint8_t * buffer, uint32_t buffer_length,
		  uint32_t mask, uint32_t trigger, uint32_t * offset)
{

	uint32_t i;
	uint32_t w = state->w;
	uint64_t hash = state->hash;

	for (i = 0; i < w; i++) {
		if (i == buffer_length) {
			*offset = i;
			// update history
			memmove(state->history, state->history + i, w - i);
			memcpy(state->history + w - i, buffer, i);
			state->hash = hash;
			return FINGERPRINT_RET_MAX;
		}
		hash = hash_fn(state, hash, buffer[i], state->history[i]);

		if ((hash & mask) == trigger) {
			// found hit
			i++;
			*offset = i;
			memmove(state->history, state->history + i, w - i);
			memcpy(state->history + w - i, buffer, i);
			state->hash = hash;
			return FINGERPRINT_RET_HIT;
		}
	}

	hash = rolling_hash2_run_until(&i, buffer_length, state->table1, state->table2,
				       buffer, buffer - w, hash, mask, trigger);
	if ((hash & mask) == trigger) {
		// found hit
		i++;
		*offset = i;
		memcpy(state->history, buffer + i - w, w);
		state->hash = hash;
		return FINGERPRINT_RET_HIT;
	}
	// no hit
	*offset = i;
	memcpy(state->history, buffer + i - w, w);
	state->hash = hash;
	return FINGERPRINT_RET_MAX;
}

struct slver {
	uint16_t snum;
	uint8_t ver;
	uint8_t core;
};
struct slver rolling_hash2_init_slver_00000264;
struct slver rolling_hash2_init_slver = { 0x0264, 0x00, 0x00 };

struct slver rolling_hash2_reset_slver_00000265;
struct slver rolling_hash2_reset_slver = { 0x0265, 0x00, 0x00 };

struct slver rolling_hash2_run_slver_00000266;
struct slver rolling_hash2_run_slver = { 0x0266, 0x00, 0x00 };
