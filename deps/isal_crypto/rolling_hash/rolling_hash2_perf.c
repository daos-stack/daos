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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "rolling_hashx.h"
#include "test.h"

//#define CACHED_TEST
#ifdef CACHED_TEST
// Cached test, loop many times over small dataset
# define TEST_LEN     8*1024
# define TEST_LOOPS   100000
# define TEST_TYPE_STR "_warm"
#else
// Uncached test.  Pull from large mem base.
#  define GT_L3_CACHE  32*1024*1024	/* some number > last level cache */
#  define TEST_LEN     (2 * GT_L3_CACHE)
#  define TEST_LOOPS   50
#  define TEST_TYPE_STR "_cold"
#endif

#ifndef FUT_run
# define FUT_run rolling_hash2_run
#endif
#ifndef FUT_init
# define FUT_init rolling_hash2_init
#endif
#ifndef FUT_reset
# define FUT_reset rolling_hash2_reset
#endif

#define str(s) #s
#define xstr(s) str(s)

#ifndef TEST_SEED
# define TEST_SEED 0x123f
#endif

#define TEST_MEM TEST_LEN

int main(int argc, char *argv[])
{
	uint8_t *buf;
	uint32_t mask, trigger, offset = 0;
	int i, w, ret;
	long long run_length;
	struct rh_state2 *state;
	struct perf start, stop;

	// Case
	w = 32;
	mask = 0xffffffff;
	trigger = 0x123;

	printf(xstr(FUT_run) "_perf:\n");

	buf = malloc(TEST_LEN);
	if (buf == NULL) {
		printf("alloc error: Fail\n");
		return -1;
	}
	if (posix_memalign((void **)&state, 64, sizeof(struct rh_state2))) {
		printf("alloc error rh_state: Fail\n");;
		return -1;
	}

	srand(TEST_SEED);

	for (i = 0; i < TEST_LEN; i++)
		buf[i] = rand();

	printf("Start timed tests\n");
	fflush(0);

	FUT_init(state, w);
	FUT_reset(state, buf);
	ret = FUT_run(state, buf, TEST_LEN, mask, trigger, &offset);

	perf_start(&start);
	for (i = 0; i < TEST_LOOPS; i++) {
		ret = FUT_run(state, buf, TEST_LEN, mask, trigger, &offset);
	}
	perf_stop(&stop);

	run_length = (ret == FINGERPRINT_RET_HIT) ? offset : TEST_LEN;
	printf("  returned %d after %lld B\n", ret, run_length);
	printf(xstr(FUT_run) TEST_TYPE_STR ": ");
	perf_print(stop, start, run_length * i);

	return 0;
}
