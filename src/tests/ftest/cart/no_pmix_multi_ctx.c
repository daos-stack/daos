/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <cart/api.h>
#include "crt_utils.h"

#define NUM_CTX 8
#define NUM_RANKS 99
#define NUM_CREATE_DESTROY 10

static pthread_barrier_t	barrier1;
static pthread_barrier_t	barrier2;
static crt_context_t		crt_ctx[NUM_CTX];

static void *
my_crtu_progress_fn(void *data)
{
	crt_context_t	*p_ctx = (crt_context_t *)data;
	void		*ret;
	int		rc;
	int		i;

	/* Create and destroy context multiple times to test DAOS-12012 */
	for (i = 0; i < NUM_CREATE_DESTROY; i++) {
		rc = crt_context_create(p_ctx);
		D_ASSERTF(rc == 0, "crt_context_create() failed; rc=%d\n", rc);

		rc = crt_context_destroy(*p_ctx, false);
		D_ASSERTF(rc == 0, "crt_context_destroy() failed; rc=%d\n", rc);
	}

	rc = crt_context_create(p_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed; rc=%d\n", rc);

	/* Wait for all threads to create their contexts */
	pthread_barrier_wait(&barrier1);

	/* Only the first thread will do the sanity check */
	if (p_ctx == &crt_ctx[0]) {
		bool		ctx_id_present[NUM_CTX];
		int		idx;
		char		*my_uri;
		crt_group_t	*grp;

		for (i = 0; i < NUM_CTX; i++)
			ctx_id_present[i] = false;

		for (i = 0; i < NUM_CTX; i++) {
			rc = crt_context_idx(crt_ctx[i], &idx);
			D_ASSERTF(rc == 0, "crt_context_idx() failed; rc=%d\n", rc);

			ctx_id_present[idx] = true;
		}

		for (i = 0; i < NUM_CTX; i++)
			D_ASSERTF(ctx_id_present[i] == true, "ctx id=%d not found\n", i);

		DBG_PRINT("Context creation sanity check passed\n");
		grp = crt_group_lookup(NULL);
		if (!grp) {
			D_ERROR("Failed to lookup group\n");
			assert(0);
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
	}

	/* Prevent other threads from executing progress/destroy during sanity check */
	pthread_barrier_wait(&barrier2);

	/* context is destroyed by crtu_progress_fn() at exit */
	ret = crtu_progress_fn(p_ctx);

	return ret;
}

int main(int argc, char **argv)
{
	pthread_t	progress_thread[NUM_CTX];
	int		i;
	int		rc;

	rc = d_log_init();
	assert(rc == 0);

	rc = crt_init(0, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = pthread_barrier_init(&barrier1, NULL, NUM_CTX);
	D_ASSERTF(rc == 0, "pthread_barrier_init() failed; rc=%d\n", rc);

	rc = pthread_barrier_init(&barrier2, NULL, NUM_CTX);
	D_ASSERTF(rc == 0, "pthread_barrier_init() failed; rc=%d\n", rc);

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 20, true, true);

	rc = crt_rank_self_set(0, 1 /* group_version_min */);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(0) failed; rc=%d\n", rc);
		assert(0);
	}

	for (i = 0; i < NUM_CTX; i++) {
		rc = pthread_create(&progress_thread[i], 0,
				    my_crtu_progress_fn, &crt_ctx[i]);
		assert(rc == 0);
	}

	crtu_set_shutdown_delay(0);
	crtu_progress_stop();

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

