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
#include "sha1_mb.h"

#define TEST_LEN  (1024*1024)
#define TEST_BUFS 100
#ifndef RANDOMS
# define RANDOMS  10
#endif
#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif

#define UPDATE_SIZE		13*SHA1_BLOCK_SIZE
#define MAX_RAND_UPDATE_BLOCKS 	(TEST_LEN/(16*SHA1_BLOCK_SIZE))

#ifdef DEBUG
# define debug_char(x) putchar(x)
#else
# define debug_char(x) do {} while (0)
#endif

/* Reference digest global to reduce stack usage */
static uint32_t digest_ref[TEST_BUFS][SHA1_DIGEST_NWORDS];

extern void sha1_ref(uint8_t * input_data, uint32_t * digest, uint32_t len);

// Generates pseudo-random data

void rand_buffer(unsigned char *buf, const long buffer_size)
{
	long i;
	for (i = 0; i < buffer_size; i++)
		buf[i] = rand();
}

int main(void)
{
	SHA1_HASH_CTX_MGR *mgr = NULL;
	SHA1_HASH_CTX ctxpool[TEST_BUFS], *ctx = NULL;
	uint32_t i, j, fail = 0;
	int len_done, len_rem, len_rand;
	unsigned char *bufs[TEST_BUFS];
	unsigned char *buf_ptr[TEST_BUFS];
	uint32_t lens[TEST_BUFS];
	unsigned int joblen, jobs, t;

	printf("multibinary_sha1_update test, %d sets of %dx%d max: ", RANDOMS, TEST_BUFS,
	       TEST_LEN);

	srand(TEST_SEED);

	posix_memalign((void *)&mgr, 16, sizeof(SHA1_HASH_CTX_MGR));
	sha1_ctx_mgr_init(mgr);

	for (i = 0; i < TEST_BUFS; i++) {
		// Allocte and fill buffer
		bufs[i] = (unsigned char *)malloc(TEST_LEN);
		buf_ptr[i] = bufs[i];
		if (bufs[i] == NULL) {
			printf("malloc failed test aborted\n");
			return 1;
		}
		rand_buffer(bufs[i], TEST_LEN);

		// Init ctx contents
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);

		// Run reference test
		sha1_ref(bufs[i], digest_ref[i], TEST_LEN);
	}

	// Run sb_sha1 tests
	for (i = 0; i < TEST_BUFS;) {
		len_done = (int)((unsigned long)buf_ptr[i] - (unsigned long)bufs[i]);
		len_rem = TEST_LEN - len_done;

		if (len_done == 0)
			ctx = sha1_ctx_mgr_submit(mgr,
						  &ctxpool[i],
						  buf_ptr[i], UPDATE_SIZE, HASH_FIRST);
		else if (len_rem <= UPDATE_SIZE)
			ctx = sha1_ctx_mgr_submit(mgr,
						  &ctxpool[i], buf_ptr[i], len_rem, HASH_LAST);
		else
			ctx = sha1_ctx_mgr_submit(mgr,
						  &ctxpool[i],
						  buf_ptr[i], UPDATE_SIZE, HASH_UPDATE);

		// Add jobs while available or finished
		if ((ctx == NULL) || hash_ctx_complete(ctx)) {
			i++;
			continue;
		}
		// Resubmit unfinished job
		i = (unsigned long)(ctx->user_data);
		buf_ptr[i] += UPDATE_SIZE;
	}

	// Start flushing finished jobs, end on last flushed
	ctx = sha1_ctx_mgr_flush(mgr);
	while (ctx) {
		if (hash_ctx_complete(ctx)) {
			debug_char('-');
			ctx = sha1_ctx_mgr_flush(mgr);
			continue;
		}
		// Resubmit unfinished job
		i = (unsigned long)(ctx->user_data);
		buf_ptr[i] += UPDATE_SIZE;

		len_done = (int)((unsigned long)buf_ptr[i]
				 - (unsigned long)bufs[i]);
		len_rem = TEST_LEN - len_done;

		if (len_rem <= UPDATE_SIZE)
			ctx = sha1_ctx_mgr_submit(mgr,
						  &ctxpool[i], buf_ptr[i], len_rem, HASH_LAST);
		else
			ctx = sha1_ctx_mgr_submit(mgr,
						  &ctxpool[i],
						  buf_ptr[i], UPDATE_SIZE, HASH_UPDATE);

		if (ctx == NULL)
			ctx = sha1_ctx_mgr_flush(mgr);
	}

	// Check digests
	for (i = 0; i < TEST_BUFS; i++) {
		for (j = 0; j < SHA1_DIGEST_NWORDS; j++) {
			if (ctxpool[i].job.result_digest[j] != digest_ref[i][j]) {
				fail++;
				printf("Test%d fixed size, digest%d fail %8X <=> %8X",
				       i, j, ctxpool[i].job.result_digest[j],
				       digest_ref[i][j]);
			}
		}
	}
	putchar('.');

	// Run tests with random size and number of jobs
	for (t = 0; t < RANDOMS; t++) {
		jobs = rand() % (TEST_BUFS);

		for (i = 0; i < jobs; i++) {
			joblen = rand() % (TEST_LEN);
			rand_buffer(bufs[i], joblen);
			lens[i] = joblen;
			buf_ptr[i] = bufs[i];
			sha1_ref(bufs[i], digest_ref[i], lens[i]);
		}

		sha1_ctx_mgr_init(mgr);

		// Run sha1_sb jobs
		i = 0;
		while (i < jobs) {
			// Submit a new job
			len_rand = SHA1_BLOCK_SIZE +
			    SHA1_BLOCK_SIZE * (rand() % MAX_RAND_UPDATE_BLOCKS);

			if (lens[i] > len_rand)
				ctx = sha1_ctx_mgr_submit(mgr,
							  &ctxpool[i],
							  buf_ptr[i], len_rand, HASH_FIRST);
			else
				ctx = sha1_ctx_mgr_submit(mgr,
							  &ctxpool[i],
							  buf_ptr[i], lens[i], HASH_ENTIRE);

			// Returned ctx could be:
			//  - null context (we are just getting started and lanes aren't full yet), or
			//  - finished already (an ENTIRE we submitted or a previous LAST is returned), or
			//  - an unfinished ctx, we will resubmit

			if ((ctx == NULL) || hash_ctx_complete(ctx)) {
				i++;
				continue;
			} else {
				// unfinished ctx returned, choose another random update length and submit either
				// UPDATE or LAST depending on the amount of buffer remaining
				while ((ctx != NULL) && !(hash_ctx_complete(ctx))) {
					j = (unsigned long)(ctx->user_data);	// Get index of the returned ctx
					buf_ptr[j] = bufs[j] + ctx->total_length;
					len_rand = (rand() % SHA1_BLOCK_SIZE)
					    * (rand() % MAX_RAND_UPDATE_BLOCKS);
					len_rem = lens[j] - ctx->total_length;

					if (len_rem <= len_rand)	// submit the rest of the job as LAST
						ctx = sha1_ctx_mgr_submit(mgr,
									  &ctxpool[j],
									  buf_ptr[j],
									  len_rem, HASH_LAST);
					else	// submit the random update length as UPDATE
						ctx = sha1_ctx_mgr_submit(mgr,
									  &ctxpool[j],
									  buf_ptr[j],
									  len_rand,
									  HASH_UPDATE);
				}	// Either continue submitting any contexts returned here as UPDATE/LAST, or
				// go back to submitting new jobs using the index i.

				i++;
			}
		}

		// Start flushing finished jobs, end on last flushed
		ctx = sha1_ctx_mgr_flush(mgr);
		while (ctx) {
			if (hash_ctx_complete(ctx)) {
				debug_char('-');
				ctx = sha1_ctx_mgr_flush(mgr);
				continue;
			}
			// Resubmit unfinished job
			i = (unsigned long)(ctx->user_data);
			buf_ptr[i] = bufs[i] + ctx->total_length;	// update buffer pointer
			len_rem = lens[i] - ctx->total_length;
			len_rand = (rand() % SHA1_BLOCK_SIZE)
			    * (rand() % MAX_RAND_UPDATE_BLOCKS);
			debug_char('+');
			if (len_rem <= len_rand)
				ctx = sha1_ctx_mgr_submit(mgr,
							  &ctxpool[i],
							  buf_ptr[i], len_rem, HASH_LAST);
			else
				ctx = sha1_ctx_mgr_submit(mgr,
							  &ctxpool[i],
							  buf_ptr[i], len_rand, HASH_UPDATE);

			if (ctx == NULL)
				ctx = sha1_ctx_mgr_flush(mgr);
		}

		// Check result digest
		for (i = 0; i < jobs; i++) {
			for (j = 0; j < SHA1_DIGEST_NWORDS; j++) {
				if (ctxpool[i].job.result_digest[j] != digest_ref[i][j]) {
					fail++;
					printf("Test%d, digest%d fail %8X <=> %8X\n",
					       i, j, ctxpool[i].job.result_digest[j],
					       digest_ref[i][j]);
				}
			}
		}
		if (fail) {
			printf("Test failed function check %d\n", fail);
			return fail;
		}

		putchar('.');
		fflush(0);
	}			// random test t

	if (fail)
		printf("Test failed function check %d\n", fail);
	else
		printf(" multibinary_sha1_update rand: Pass\n");

	return fail;
}
