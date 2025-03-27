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
#include "mh_sha1.h"

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

#ifndef MH_SHA1_FUNC_TYPE
#define	MH_SHA1_FUNC_TYPE
#endif

#define TEST_UPDATE_FUNCTION		FUNC_TOKEN(mh_sha1_update, MH_SHA1_FUNC_TYPE)
#define TEST_FINAL_FUNCTION		FUNC_TOKEN(mh_sha1_finalize, MH_SHA1_FUNC_TYPE)

#define CHECK_RETURN(state)		do{ \
					  if((state) != MH_SHA1_CTX_ERROR_NONE){ \
					    printf("The mh_sha1 function is failed.\n"); \
					    return 1; \
					  } \
					}while(0)

extern void mh_sha1_ref(const void *buffer, uint32_t len, uint32_t * mh_sha1_digest);
#define MH_SHA1_REF	mh_sha1_ref

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

int compare_digests(uint32_t hash_ref[SHA1_DIGEST_WORDS],
		    uint32_t hash_test[SHA1_DIGEST_WORDS])
{
	int i;
	int mh_sha1_fail = 0;

	for (i = 0; i < SHA1_DIGEST_WORDS; i++) {
		if (hash_test[i] != hash_ref[i])
			mh_sha1_fail++;
	}

	if (mh_sha1_fail) {
		printf("mh_sha1 fail test\n");
		printf("ref: ");
		dump((char *)hash_ref, 20);
		printf("test: ");
		dump((char *)hash_test, 20);
	}

	return mh_sha1_fail;
}

int main(int argc, char *argv[])
{
	int fail = 0;
	uint32_t hash_test[SHA1_DIGEST_WORDS], hash_ref[SHA1_DIGEST_WORDS];
	uint8_t *buff = NULL;
	int size, offset;
	struct mh_sha1_ctx *update_ctx = NULL;

	printf(xstr(TEST_UPDATE_FUNCTION) "_test:\n");

	srand(TEST_SEED);

	buff = malloc(TEST_LEN);
	update_ctx = malloc(sizeof(*update_ctx));

	if (buff == NULL || update_ctx == NULL) {
		printf("malloc failed test aborted\n");
		return -1;
	}
	// Rand test1
	rand_buffer(buff, TEST_LEN);

	MH_SHA1_REF(buff, TEST_LEN, hash_ref);
	CHECK_RETURN(mh_sha1_init(update_ctx));
	CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff, TEST_LEN));
	CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

	fail = compare_digests(hash_ref, hash_test);

	if (fail) {
		printf("fail rand1 test\n");
		return -1;
	} else
		putchar('.');

	// Test various size messages
	for (size = TEST_LEN; size >= 0; size--) {

		// Fill with rand data
		rand_buffer(buff, size);

		MH_SHA1_REF(buff, size, hash_ref);
		CHECK_RETURN(mh_sha1_init(update_ctx));
		CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff, size));
		CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

		fail = compare_digests(hash_ref, hash_test);

		if (fail) {
			printf("Fail size=%d\n", size);
			return -1;
		}

		if ((size & 0xff) == 0) {
			putchar('.');
			fflush(0);
		}
	}

	// Test various buffer offsets and sizes
	printf("offset tests");
	for (size = TEST_LEN - 256; size > 256; size -= 11) {
		for (offset = 0; offset < 256; offset++) {
			MH_SHA1_REF(buff + offset, size, hash_ref);

			CHECK_RETURN(mh_sha1_init(update_ctx));
			CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff + offset, size));
			CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

			fail = compare_digests(hash_ref, hash_test);

			if (fail) {
				printf("Fail size=%d\n", size);
				return -1;
			}

		}
		if ((size & 0xf) == 0) {
			putchar('.');
			fflush(0);
		}
	}

	// Run efence tests
	printf("efence tests");
	for (size = TEST_SIZE; size > 0; size--) {
		offset = TEST_LEN - size;

		MH_SHA1_REF(buff + offset, size, hash_ref);

		CHECK_RETURN(mh_sha1_init(update_ctx));
		CHECK_RETURN(TEST_UPDATE_FUNCTION(update_ctx, buff + offset, size));
		CHECK_RETURN(TEST_FINAL_FUNCTION(update_ctx, hash_test));

		fail = compare_digests(hash_ref, hash_test);

		if (fail) {
			printf("Fail size=%d\n", size);
			return -1;
		}

		if ((size & 0xf) == 0) {
			putchar('.');
			fflush(0);
		}
	}

	printf(xstr(TEST_UPDATE_FUNCTION) "_test:");
	printf(" %s\n", fail == 0 ? "Pass" : "Fail");

	return fail;
}
