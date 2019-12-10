/* Copyright (C) 2018-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This test verifies proper destruction of contexts and of associated internal
 * lookup and uri caches when done in parallel.
 *
 * Test creates 8 contexts with 8 threads, sets self rank to 0, adds 99 ranks
 * each with the uri of ourself (need to add valid uri address), and issues
 * shutdown sequence on threads.
 *
 * Threads attempt to destroy their respective contexts, triggering internal
 * lookup cache/uri table destruction.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <gurt/common.h>
#include <cart/api.h>


#define NUM_CTX 8
#define NUM_RANKS 99

static int g_do_shutdown;

static void *
progress_function(void *data)
{
	crt_context_t *p_ctx = (crt_context_t *)data;

	while (g_do_shutdown == 0)
		crt_progress(*p_ctx, 1000, NULL, NULL);

	crt_context_destroy(*p_ctx, 1);

	return NULL;
}

int main(int argc, char **argv)
{
	crt_group_t	*grp;
	crt_context_t	crt_ctx[NUM_CTX];
	pthread_t	progress_thread[NUM_CTX];
	int		i;
	int		rc;
	char		*my_uri;

	rc = d_log_init();
	assert(rc == 0);

	rc = crt_init(0, CRT_FLAG_BIT_SERVER);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	grp = crt_group_lookup(NULL);
	if (!grp) {
		D_ERROR("Failed to lookup group\n");
		assert(0);
	}

	rc = crt_rank_self_set(0);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(0) failed; rc=%d\n", rc);
		assert(0);
	}


	for (i = 0; i < NUM_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("crt_context_create() failed; rc=%d\n", rc);
			assert(0);
		}

		rc = pthread_create(&progress_thread[i], 0,
				progress_function, &crt_ctx[i]);
		assert(rc == 0);
	}

	rc = crt_rank_uri_get(grp, 0, 0, &my_uri);
	if (rc != 0) {
		D_ERROR("crt_rank_uri_get() failed; rc=%d\n", rc);
		assert(0);
	}

	/* NOTE: We have to pass valid uri or else group_node_add fails */
	for (i = 1; i < (NUM_RANKS + 1); i++) {
		rc = crt_group_primary_rank_add(crt_ctx[0], grp, i, my_uri);
		if (rc != 0) {
			D_ERROR("crt_group_primary_rank_add() failed; rc=%d\n",
				rc);
			assert(0);
		}
	}

	D_FREE(my_uri);
	sleep(1);
	g_do_shutdown = 1;

	for (i = 0; i < NUM_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		assert(0);
	}

	d_log_fini();

	return 0;
}

