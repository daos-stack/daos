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
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "rolling_hashx.h"

#ifndef FUT_run
# define FUT_run rolling_hash2_run
#endif
#ifndef FUT_init
# define FUT_init rolling_hash2_init
#endif
#ifndef FUT_reset
# define FUT_reset rolling_hash2_reset
#endif
#ifndef FUT_ref
# define FUT_ref rolling_hash2_ref
#endif

#define str(s) #s
#define xstr(s) str(s)

#define MAX_BUFFER_SIZE 128*1024*1024
#define MAX_ROLLING_HASH_WIDTH 32

#ifndef RANDOMS
# define RANDOMS 200
#endif
#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif

static
uint64_t rolling_hash2_ref(struct rh_state2 *state, unsigned char *p, int len,
			   uint64_t hash_init)
{
	int i;
	uint64_t h = hash_init;

	for (i = 0; i < len; i++) {
		h = (h << 1) | (h >> (64 - 1));
		h ^= state->table1[*p++];
	}
	return h;
}

int ones_in_mask(uint32_t in)
{
	int count;

	for (count = 0; in != 0; in &= (in - 1))
		count++;

	return count;
}

/*
 * Utility function to pick a random mask.  Not uniform in number of bits.
 */
uint32_t pick_rand_mask_in_range(int min_bits, int max_bits)
{
	uint32_t mask = 0;
	int ones;

	do {
		mask = rand();
#ifdef _MSC_VER
		mask = (mask << 16) ^ rand();
#endif
		ones = ones_in_mask(mask);
	} while (ones < min_bits || ones > max_bits);

	return mask;
}

int main(void)
{
	uint8_t *buffer;
	uint64_t hash;
	uint32_t mask, trigger, offset = 0;
	int i, w, r, ret, max, errors = 0;
	uint32_t offset_fut;
	struct rh_state2 state;

	printf(xstr(FUT_run) ": " xstr(MAX_BUFFER_SIZE));

	buffer = malloc(MAX_BUFFER_SIZE);
	if (buffer == NULL) {
		printf("cannot allocate mem\n");
		return -1;
	}
	srand(TEST_SEED);

	// Test case 1, compare trigger case at boundary with reference hash
	w = 32;
	mask = 0xffff0;
	trigger = 0x3df0;
	trigger &= mask;

	for (i = 0; i < MAX_BUFFER_SIZE; i++)
		buffer[i] = rand();

	FUT_init(&state, w);
	FUT_reset(&state, buffer);

	uint8_t *p = buffer;
	int remain = MAX_BUFFER_SIZE;
	ret = FINGERPRINT_RET_HIT;

	while ((ret == FINGERPRINT_RET_HIT) && (remain > 0)) {
		ret = FUT_run(&state, p, remain, mask, trigger, &offset);

		if (offset > remain) {
			printf(" error offset past remaining limit\n");
			errors++;
		}

		if ((ret == FINGERPRINT_RET_HIT) && (&p[offset] > &buffer[w])) {
			hash = FUT_ref(&state, &p[offset] - w, w, 0);
			if ((hash & mask) != trigger) {
				printf("   mismatch chunk from ref");
				printf(" hit: offset=%d %lx %lx\n", offset, state.hash, hash);
				errors++;
			}
		}
		p += offset;
		remain -= offset;
		putchar('.');
	}

	putchar('.');		// Finished test 1

	// Test case 2, check if reference function hits same chunk boundary as test

	w = 32;
	mask = 0xffff;
	trigger = rand();
	trigger &= mask;
	p = buffer;

	// Function under test
	FUT_init(&state, w);
	FUT_reset(&state, p);
	ret = FUT_run(&state, p + w, MAX_BUFFER_SIZE - w, mask, trigger, &offset_fut);
	offset_fut += w;

	// Reference
	for (p++, offset = w + 1; offset < MAX_BUFFER_SIZE; offset++) {
		hash = FUT_ref(&state, p++, w, 0);
		if ((hash & mask) == trigger)
			break;
	}

	if (offset != offset_fut) {
		printf("\ncase 2, offset of chunk different from ref\n");
		printf("  case 2: stop fut at offset=%d\n", offset_fut);
		printf("  case 2: stop ref at offset=%d\n", offset);
		errors++;
		return errors;
	}
	putchar('.');		// Finished test 2

	// Do case 2 above with random args

	for (r = 0; r < RANDOMS; r++) {
		w = rand() % MAX_ROLLING_HASH_WIDTH;
		if (w < 3)
			continue;

		mask = pick_rand_mask_in_range(4, 20);
		trigger = rand() & mask;
		p = buffer;

		// Function under test
		FUT_init(&state, w);
		FUT_reset(&state, p);
		ret = FUT_run(&state, p + w, MAX_BUFFER_SIZE - w, mask, trigger, &offset_fut);
		offset_fut += w;

		// Reference
		for (p++, offset = w + 1; offset < MAX_BUFFER_SIZE; offset++) {
			hash = FUT_ref(&state, p++, w, 0);
			if ((hash & mask) == trigger)
				break;
		}

		if (offset != offset_fut) {
			printf("\nrand case 2 #%d: w=%d, mask=0x%x, trigger=0x%x\n", r, w,
			       mask, trigger);
			printf("  offset of chunk different from ref\n");
			printf("  case 2r: stop fut at offset=%d\n", offset_fut);
			printf("  case 2r: stop ref at offset=%d\n", offset);
			errors++;
			return errors;
		}
		putchar('.');
	}

	// Test case 3, check if max bound is same

	w = 32;
	mask = 0xfffff;
	trigger = rand();
	trigger &= mask;
	putchar('|');

	for (max = w + 1; max < 500; max++) {
		p = buffer;
		FUT_init(&state, w);
		FUT_reset(&state, p);

		ret = FUT_run(&state, p + w, max - w, mask, trigger, &offset_fut);
		offset_fut += w;

		int ret_ref = FINGERPRINT_RET_MAX;
		for (p++, offset = w + 1; offset < max; offset++) {
			hash = FUT_ref(&state, p++, w, 0);
			if ((hash & mask) == trigger) {
				ret_ref = FINGERPRINT_RET_HIT;
				break;
			}
		}

		if (offset != offset_fut || ret != ret_ref) {
			printf("\ncase 3 max=%d, offset of chunk different from ref\n", max);
			printf("  case 3: stop fut at offset=%d\n", offset_fut);
			printf("  case 3: stop ref at offset=%d\n", offset);
			printf("  case 3: ret_fut=%d ret_ref=%d\n", ret, ret_ref);
			errors++;
			return errors;
		}
		putchar('.');	// Finished test 3
	}

	// Test case 4, check if max bound is same under random params

	for (r = 0; r < RANDOMS; r++) {
		p = buffer;
		mask = pick_rand_mask_in_range(24, 30);	// Pick an unlikely mask
		trigger = rand() & mask;
		w = rand() % MAX_ROLLING_HASH_WIDTH;
		max = rand() % 1024;

		if (w < 3 || max < 2 * MAX_ROLLING_HASH_WIDTH)
			continue;

		FUT_init(&state, w);
		FUT_reset(&state, p);

		ret = FUT_run(&state, p, max, mask, trigger, &offset_fut);

		if (offset_fut <= w)
			continue;

		int ret_ref = FINGERPRINT_RET_MAX;
		for (p++, offset = w + 1; offset < max; offset++) {
			hash = FUT_ref(&state, p++, w, 0);
			if ((hash & mask) == trigger) {
				ret_ref = FINGERPRINT_RET_HIT;
				break;
			}
		}

		if (offset != offset_fut || ret != ret_ref) {
			printf("\ncase 4 rand case different from ref, max=%d w=%d\n", max, w);
			printf("  case 4: stop fut at offset=%d\n", offset_fut);
			printf("  case 4: stop ref at offset=%d\n", offset);
			printf("  case 4: ret_fut=%d ret_ref=%d\n", ret, ret_ref);
			errors++;
			return errors;
		}
		putchar('.');	// Finished test 4

		if (ret == FINGERPRINT_RET_HIT) {
			p[-1] = rand();	// Keep hits from repeating
		}
	}

	if (errors > 0)
		printf(" Fail: %d\n", errors);
	else
		printf(" Pass\n");
	return errors;
}
