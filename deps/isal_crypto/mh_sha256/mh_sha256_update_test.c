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
#include "mh_sha256.h"

#define TEST_LEN   16*1024
#define TEST_SIZE   8*1024
#define TEST_MEM   TEST_LEN
#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif

#define str(s) #s
#define xstr(s) str(s)

#define _FUNC_TOKEN(func, type)		func##type
#define FUNC_TOKEN(func, type)		_FUNC_TOKEN(func, type)

#ifndef MH_SHA256_FUNC_TYPE
#define	MH_SHA256_FUNC_TYPE
#endif

#define TEST_UPDATE_FUNCTION		FUNC_TOKEN(mh_sha256_update, MH_SHA256_FUNC_TYPE)
#define TEST_FINAL_FUNCTION		FUNC_TOKEN(mh_sha256_finalize, MH_SHA256_FUNC_TYPE)

#define CHECK_RETURN(state)		do{ \
					  if((state) != MH_SHA256_CTX_ERROR_NONE){ \
					    printf("The mh_sha256 function is failed.\n"); \
					    return 1; \
					  } \
					}while(0)

extern void mh_sha256_ref(const void *buffer, uint32_t len, uint32_t * mh_sha256_digest);

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

int compare_digests(uint32_t hash_ref[SHA256_DIGEST_WORDS],
		    uint32_t hash_test[SHA256_DIGEST_WORDS])
{
	int i;
	int mh_sha256_fail = 0;

	for (i = 0; i < SHA256_DIGEST_WORDS; i++) {
		if (hash_test[i] != hash_ref[i])
			mh_sha256_fail++;
	}

	if (mh_sha256_fail) {
		printf("mh_sha256 fail test\n");
		printf("ref: ");
		dump((char *)hash_ref, 20);
		printf("test: ");
		dump((char *)hash_test, 20);
	}

	return mh_sha256_fail;
}

int main(int argc, char *argv[])
{
	int fail = 0, i;
	uint32_t hash_test[SHA256_DIGEST_WORDS], hash_ref[SHA256_DIGEST_WORDS];
	uint8_t *buff = NULL;
	int update_count;
	int size1, size2, offset, addr_offset;
	struct mh_sha256_ctx *update_ctx = NULL;
	uint8_t *mem_addr = NULL;

	printf(xstr(TEST_UPDATE_FUNCTION) "_test:");

	srand(TEST_SEED);

	buff = malloc(TEST_LEN);
	update_ctx = malloc(sizeof(*update_ctx));

	if (buff == NULL || update_ctx == NULL) {
		printf("malloc failed test aborted\n");
		return -1;
	}
	// Rand test1
	rand_buffer(buff, TEST_LEN);

	mh_sha256_ref(buff, TEST_LEN, hash_ref);

	CHECK_RETURN(mh_sha256_init(update_ctx));
	CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff, TEST_LEN));
	CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

	fail = compare_digests(hash_ref, hash_test);

	if (fail) {
		printf("fail rand1 test\n");
		return -1;
	} else
		putchar('.');

	// Test various size messages by update twice.
	printf("\n various size messages by update twice tests");
	for (size1 = TEST_LEN; size1 >= 0; size1--) {

		// Fill with rand data
		rand_buffer(buff, TEST_LEN);

		mh_sha256_ref(buff, TEST_LEN, hash_ref);

		// subsequent update
		size2 = TEST_LEN - size1;	// size2 is different with the former
		CHECK_RETURN(mh_sha256_init(update_ctx));
		CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff, size1));
		CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff + size1, size2));
		CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

		fail = compare_digests(hash_ref, hash_test);

		if (fail) {
			printf("Fail size1=%d\n", size1);
			return -1;
		}

		if ((size2 & 0xff) == 0) {
			putchar('.');
			fflush(0);
		}
	}

	// Test various update count
	printf("\n various update count tests");
	for (update_count = 1; update_count <= TEST_LEN; update_count++) {

		// Fill with rand data
		rand_buffer(buff, TEST_LEN);

		mh_sha256_ref(buff, TEST_LEN, hash_ref);

		// subsequent update
		size1 = TEST_LEN / update_count;
		size2 = TEST_LEN - size1 * (update_count - 1);	// size2 is different with the former

		CHECK_RETURN(mh_sha256_init(update_ctx));
		for (i = 1, offset = 0; i < update_count; i++) {
			CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff + offset, size1));
			offset += size1;
		}
		CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff + offset, size2));
		CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

		fail = compare_digests(hash_ref, hash_test);

		if (fail) {
			printf("Fail size1=%d\n", size1);
			return -1;
		}

		if ((size2 & 0xff) == 0) {
			putchar('.');
			fflush(0);
		}
	}

	// test various start address of ctx.
	printf("\n various start address of ctx test");
	free(update_ctx);
	mem_addr = (uint8_t *) malloc(sizeof(*update_ctx) + AVX512_ALIGNED * 10);
	for (addr_offset = AVX512_ALIGNED * 10; addr_offset >= 0; addr_offset--) {

		// Fill with rand data
		rand_buffer(buff, TEST_LEN);

		mh_sha256_ref(buff, TEST_LEN, hash_ref);

		// a unaligned offset
		update_ctx = (struct mh_sha256_ctx *)(mem_addr + addr_offset);
		CHECK_RETURN(mh_sha256_init(update_ctx));
		CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff, TEST_LEN));
		CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

		fail = compare_digests(hash_ref, hash_test);

		if (fail) {
			printf("Fail addr_offset=%d\n", addr_offset);
			return -1;
		}

		if ((addr_offset & 0xf) == 0) {
			putchar('.');
			fflush(0);
		}
	}

	printf("\n" xstr(TEST_UPDATE_FUNCTION) "_test: %s\n", fail == 0 ? "Pass" : "Fail");

	return fail;

}
