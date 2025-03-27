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
#include <stdio.h>
#include <assert.h>
#include "rolling_hashx.h"
#include "sha256_mb.h"
#include "test.h"

#define MAX_BUFFER_SIZE 128*1024*1024
#define HASH_POOL_SIZE SHA256_MAX_LANES

#ifndef TEST_SEED
# define TEST_SEED 0x1234
#endif

#define FILTER_BITS 10
#define FILTER_SIZE (1 << FILTER_BITS)
#define FILTER_MASK (FILTER_SIZE - 1)

#define BITS_TO_INDEX_LONG 6
#define MASK_TO_INDEX_LONG ((1 << BITS_TO_INDEX_LONG) - 1)

// Globals
SHA256_HASH_CTX ctxpool[SHA256_MAX_LANES], *last_ctx;
SHA256_HASH_CTX_MGR mb_hash_mgr;
uint64_t filter_table[FILTER_SIZE];
unsigned long chunks_created = 0;
unsigned long filter_hits = 0;

// Example function to run on each chunk

void run_fragment(SHA256_HASH_CTX * ctx)
{
	uint64_t lookup, set_hash;
	unsigned int lookup_hash;
	uint32_t idx;

	chunks_created++;

	// Run a simple lookup filter on chunk using digest
	lookup_hash = ctx->job.result_digest[0] & FILTER_MASK;
	lookup = filter_table[lookup_hash];

	idx = ctx->job.result_digest[1];

	set_hash = 1 << (idx & MASK_TO_INDEX_LONG) |
	    1 << ((idx >> BITS_TO_INDEX_LONG) & MASK_TO_INDEX_LONG) |
	    1 << ((idx >> (2 * BITS_TO_INDEX_LONG)) & MASK_TO_INDEX_LONG);

	if ((lookup & set_hash) == set_hash)
		filter_hits++;
	else
		filter_table[lookup_hash] = lookup | set_hash;
}

void setup_chunk_processing(void)
{
	int i;

	sha256_ctx_mgr_init(&mb_hash_mgr);

	for (i = 0; i < HASH_POOL_SIZE; i++)
		hash_ctx_init(&ctxpool[i]);

	last_ctx = &ctxpool[0];
}

SHA256_HASH_CTX *get_next_job_ctx(void)
{
	int i;
	SHA256_HASH_CTX *ctx;

	if (last_ctx && hash_ctx_complete(last_ctx))
		return last_ctx;

	for (i = 0; i < HASH_POOL_SIZE; i++) {
		if (hash_ctx_complete(&ctxpool[i]))
			return &ctxpool[i];
	}
	ctx = sha256_ctx_mgr_flush(&mb_hash_mgr);
	assert(ctx != NULL);
	return ctx;
}

void put_next_job_ctx(SHA256_HASH_CTX * ctx)
{
	if (ctx && hash_ctx_complete(ctx))
		last_ctx = ctx;

	run_fragment(ctx);
}

void process_chunk(uint8_t * buff, int len)
{
	SHA256_HASH_CTX *ctx;

	ctx = get_next_job_ctx();
	ctx = sha256_ctx_mgr_submit(&mb_hash_mgr, ctx, buff, len, HASH_ENTIRE);

	if (ctx)
		put_next_job_ctx(ctx);
}

void finish_chunk_processing(void)
{
	SHA256_HASH_CTX *ctx;

	while ((ctx = sha256_ctx_mgr_flush(&mb_hash_mgr)) != NULL)
		run_fragment(ctx);
}

int main(void)
{
	int i, w;
	uint8_t *buffer, *p;
	uint32_t mask, trigger, offset = 0;
	uint32_t min_chunk, max_chunk, mean_chunk;
	long remain;
	struct rh_state2 state;
	struct perf start, stop;

	// Chunking parameters
	w = 32;
	min_chunk = 1024;
	mean_chunk = 4 * 1024;
	max_chunk = 32 * 1024;
	mask = rolling_hashx_mask_gen(mean_chunk, 0);
	trigger = rand() & mask;

	printf("chunk and hash test w=%d, min=%d, target_ave=%d, max=%d:\n", w, min_chunk,
	       mean_chunk, max_chunk);

	if (min_chunk < w || min_chunk > max_chunk) {
		printf(" Improper parameters selected\n");
		return -1;
	}

	if ((buffer = malloc(MAX_BUFFER_SIZE)) == NULL) {
		printf("cannot allocate mem\n");
		return -1;
	}
	// Initialize buffer with random data
	srand(TEST_SEED);
	for (i = 0; i < MAX_BUFFER_SIZE; i++)
		buffer[i] = rand();

	// Start chunking test with multi-buffer hashing of results
	perf_start(&start);

	rolling_hash2_init(&state, w);
	setup_chunk_processing();

	p = buffer;
	remain = MAX_BUFFER_SIZE;

	while (remain > max_chunk) {
		// Skip to min chunk
		rolling_hash2_reset(&state, p + min_chunk - w);
		rolling_hash2_run(&state, p + min_chunk, max_chunk - min_chunk,
				  mask, trigger, &offset);

		process_chunk(p, min_chunk + offset);

		p += offset + min_chunk;
		remain -= (offset + min_chunk);
	}

	while (remain > min_chunk) {
		rolling_hash2_reset(&state, p + min_chunk - w);
		rolling_hash2_run(&state, p + min_chunk, remain - min_chunk,
				  mask, trigger, &offset);

		process_chunk(p, min_chunk + offset);

		p += offset + min_chunk;
		remain -= (offset + min_chunk);
	}

	if (remain > 0)
		process_chunk(p, remain);

	finish_chunk_processing();
	perf_stop(&stop);

	printf("chunking_with_mb_hash: ");
	perf_print(stop, start, MAX_BUFFER_SIZE);

	printf(" found %ld chunks, ave_len=%ld, filter hits=%ld\n", chunks_created,
	       MAX_BUFFER_SIZE / chunks_created, filter_hits);

	return 0;
}
