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

#include <stdio.h>
#include <stdlib.h>
#include "mh_sha1_murmur3_x64_128.h"
#include "test.h"

//#define CACHED_TEST
#ifdef CACHED_TEST
// Loop many times over same
# define TEST_LEN     16*1024
# define TEST_LOOPS   20000
# define TEST_TYPE_STR "_warm"
#else
// Uncached test.  Pull from large mem base.
# define TEST_LEN     32*1024*1024
# define TEST_LOOPS   100
# define TEST_TYPE_STR "_cold"
#endif

#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif
#define TEST_MEM   TEST_LEN

#define str(s) #s
#define xstr(s) str(s)

#define _FUNC_TOKEN(func, type)		func##type
#define FUNC_TOKEN(func, type)		_FUNC_TOKEN(func, type)

#ifndef MH_SHA1_FUNC_TYPE
#define	MH_SHA1_FUNC_TYPE
#endif

#define TEST_UPDATE_FUNCTION		FUNC_TOKEN(mh_sha1_murmur3_x64_128_update, MH_SHA1_FUNC_TYPE)
#define TEST_FINAL_FUNCTION		FUNC_TOKEN(mh_sha1_murmur3_x64_128_finalize, MH_SHA1_FUNC_TYPE)

#define CHECK_RETURN(state)		do{ \
					  if((state) != MH_SHA1_MURMUR3_CTX_ERROR_NONE){ \
					    printf("The stitch function is failed.\n"); \
					    return 1; \
					  } \
					}while(0)

extern void mh_sha1_ref(const void *buffer, uint32_t len, uint32_t * mh_sha1_digest);

extern void murmur3_x64_128(const void *buffer, uint32_t len, uint64_t murmur_seed,
			    uint32_t * murmur3_x64_128_digest);

void mh_sha1_murmur3_x64_128_base(const void *buffer, uint32_t len, uint64_t murmur_seed,
				  uint32_t * mh_sha1_digest, uint32_t * murmur3_x64_128_digest)
{
	mh_sha1_ref(buffer, len, mh_sha1_digest);
	murmur3_x64_128(buffer, len, murmur_seed, murmur3_x64_128_digest);

	return;
}

// Generates pseudo-random data
void rand_buffer(uint8_t * buf, long buffer_size)
{
	long i;
	for (i = 0; i < buffer_size; i++)
		buf[i] = rand();
}

void dump(char *buf, int len)
{
	int i;
	for (i = 0; i < len;) {
		printf(" %2x", 0xff & buf[i++]);
		if (i % 20 == 0)
			printf("\n");
	}
	if (i % 20 != 0)
		printf("\n");
}

int compare_digests(uint32_t hash_base[SHA1_DIGEST_WORDS],
		    uint32_t hash_test[SHA1_DIGEST_WORDS],
		    uint32_t murmur3_base[MURMUR3_x64_128_DIGEST_WORDS],
		    uint32_t murmur3_test[MURMUR3_x64_128_DIGEST_WORDS])
{
	int i;
	int mh_sha1_fail = 0;
	int murmur3_fail = 0;

	for (i = 0; i < SHA1_DIGEST_WORDS; i++) {
		if (hash_test[i] != hash_base[i])
			mh_sha1_fail++;
	}

	for (i = 0; i < MURMUR3_x64_128_DIGEST_WORDS; i++) {
		if (murmur3_test[i] != murmur3_base[i])
			murmur3_fail++;
	}

	if (mh_sha1_fail) {
		printf("mh_sha1 fail test\n");
		printf("base: ");
		dump((char *)hash_base, 20);
		printf("ref: ");
		dump((char *)hash_test, 20);
	}
	if (murmur3_fail) {
		printf("murmur3 fail test\n");
		printf("base: ");
		dump((char *)murmur3_base, 16);
		printf("ref: ");
		dump((char *)murmur3_test, 16);
	}

	return mh_sha1_fail + murmur3_fail;
}

int main(int argc, char *argv[])
{
	int i, fail = 0;
	uint32_t hash_test[SHA1_DIGEST_WORDS], hash_base[SHA1_DIGEST_WORDS];
	uint32_t murmur3_test[MURMUR3_x64_128_DIGEST_WORDS],
	    murmur3_base[MURMUR3_x64_128_DIGEST_WORDS];
	uint8_t *buff = NULL;
	struct mh_sha1_murmur3_x64_128_ctx *update_ctx = NULL;
	struct perf start, stop;

	printf(xstr(TEST_UPDATE_FUNCTION) "_perf:\n");

	buff = malloc(TEST_LEN);
	update_ctx = malloc(sizeof(*update_ctx));

	if (buff == NULL || update_ctx == NULL) {
		printf("malloc failed test aborted\n");
		return -1;
	}
	// Rand test1
	rand_buffer(buff, TEST_LEN);

	// mh_sha1_murmur3 base version
	mh_sha1_murmur3_x64_128_base(buff, TEST_LEN, TEST_SEED, hash_base, murmur3_base);
	perf_start(&start);
	for (i = 0; i < TEST_LOOPS / 10; i++) {
		mh_sha1_murmur3_x64_128_base(buff, TEST_LEN, TEST_SEED, hash_base,
					     murmur3_base);
	}
	perf_stop(&stop);
	printf("mh_sha1_murmur3_x64_128_base" TEST_TYPE_STR ": ");
	perf_print(stop, start, (long long)TEST_MEM * i);

	//Update feature test
	CHECK_RETURN(mh_sha1_murmur3_x64_128_init(update_ctx, TEST_SEED));
	CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff, TEST_LEN));
	CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test, murmur3_test));

	perf_start(&start);
	for (i = 0; i < TEST_LOOPS; i++) {
		CHECK_RETURN(mh_sha1_murmur3_x64_128_init(update_ctx, TEST_SEED));
		CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff, TEST_LEN));
		CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test, murmur3_test));
	}
	perf_stop(&stop);
	printf(xstr(TEST_UPDATE_FUNCTION) TEST_TYPE_STR ": ");
	perf_print(stop, start, (long long)TEST_MEM * i);

	// Check results
	fail = compare_digests(hash_base, hash_test, murmur3_base, murmur3_test);

	if (fail) {
		printf("Fail size=%d\n", TEST_LEN);
		return -1;
	}

	if (fail)
		printf("Test failed function test%d\n", fail);
	else
		printf("Pass func check\n");

	return fail;
}
