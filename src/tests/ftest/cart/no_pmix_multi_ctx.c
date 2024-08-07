/*
 * (C) Copyright 2018-2024 Intel Corporation.
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
 *
 * Second part of the test checks out multi-interface specific context APIs.
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
		D_ASSERTF(grp != NULL, "Failed to lookup group\n");

		rc = crt_rank_uri_get(grp, 0, 0, &my_uri);
		D_ASSERTF(rc == 0, "crt_rank_uri_get() failed; rc=%d\n", rc);

		/* NOTE: We have to pass valid uri or else group_node_add fails */
		for (i = 1; i < (NUM_RANKS + 1); i++) {
			rc = crt_group_primary_rank_add(crt_ctx[0], grp, i, my_uri);
			D_ASSERTF(rc == 0, "crt_group_primary_rank_add() failed; rc=%d\n", rc);
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
	char		*env;
	char		*cur_iface_str;
	char		*new_iface_str;
	char            *cur_domain_str = NULL;
	char            *new_domain_str = NULL;
	int		iface_idx = -1;
	int		num_ifaces;
	crt_context_t	c1,c2;
	char		*uri1;
	char		*uri2;
	int		rc;

	/* Set these 2 if they are not set so that test still runs by default */
	setenv("D_PROVIDER", "ofi+tcp", 0);
	setenv("D_INTERFACE", "eth0", 0);

	rc = d_log_init();
	assert(rc == 0);

	rc = crt_init(0, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	D_ASSERTF(rc == 0, "crt_init() failed; rc=%d\n", rc);

	rc = pthread_barrier_init(&barrier1, NULL, NUM_CTX);
	D_ASSERTF(rc == 0, "pthread_barrier_init() failed; rc=%d\n", rc);

	rc = pthread_barrier_init(&barrier2, NULL, NUM_CTX);
	D_ASSERTF(rc == 0, "pthread_barrier_init() failed; rc=%d\n", rc);

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(0, 20, true, true);

	rc = crt_rank_self_set(0, 1 /* group_version_min */);
	D_ASSERTF(rc == 0, "crt_rank_self_set(0) failed; rc=%d\n", rc);

	for (i = 0; i < NUM_CTX; i++) {
		rc = pthread_create(&progress_thread[i], 0,
				    my_crtu_progress_fn, &crt_ctx[i]);
		D_ASSERTF(rc == 0, "pthread_create() failed; rc=%d\n", rc);
	}

	crtu_set_shutdown_delay(0);
	crtu_progress_stop();

	for (i = 0; i < NUM_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed with rc=%d\n", rc);

	/* Multi-interface tests */
	DBG_PRINT("Checking multi-interface setup\n");

	/* Append ',lo' to interface string as 'lo' should be available everywhere */
	env = getenv("D_INTERFACE");
	D_ASSERTF(env != NULL, "D_INTERFACE must be set");

	/* Save current interface value */
	D_ASPRINTF(cur_iface_str, "%s", env);
	D_ASSERTF(cur_iface_str != NULL, "Failed to allocate string");

	/* Append loopback to the current interface list */
	D_ASPRINTF(new_iface_str, "%s,lo", cur_iface_str);
	D_ASSERTF(new_iface_str != NULL, "Failed to allocate string");
	setenv("D_INTERFACE", new_iface_str, 1);

	/* Append ',lo' to domain string as 'lo' should be available everywhere */
	env = getenv("D_DOMAIN");

	/* Domain is optional, can be set for manual testing */
	if (env != NULL) {
		/* Save current domain value */
		D_ASPRINTF(cur_domain_str, "%s", env);
		D_ASSERTF(cur_domain_str != NULL, "Failed to allocate string");

		/* Append loopback to the current domain list */
		D_ASPRINTF(new_domain_str, "%s,lo", cur_domain_str);
		D_ASSERTF(new_domain_str != NULL, "Failed to allocate string");

		setenv("D_DOMAIN", new_domain_str, 1);
	}

	/* Reinitialize as a client to be able to use multi-interface APIs */
	rc = crt_init(0, 0);
	D_ASSERTF(rc == 0, "crt_init() failed; rc=%d\n", rc);

	/* Test multi-interface APIs */
	num_ifaces = crt_num_ifaces_get();
	D_ASSERTF(num_ifaces == 2, "crt_num_ifaces_get() returned %d, expected 2\n", num_ifaces);
	DBG_PRINT("crt_num_ifaces_get() PASSED\n");

	rc = crt_iface_name2idx(cur_iface_str, &iface_idx);
	D_ASSERTF(rc == 0, "crt_iface_name2idx() failed");
	D_ASSERTF(iface_idx == 0, "expected 0 got %d for %s", iface_idx, cur_iface_str);
	DBG_PRINT("crt_iface_name2idx(%s) PASSED\n", cur_iface_str);

	rc = crt_iface_name2idx("lo", &iface_idx);
	D_ASSERTF(rc == 0, "crt_iface_name2idx() failed");
	D_ASSERTF(iface_idx == 1, "expected 1 got %d for lo interface index", iface_idx);
	DBG_PRINT("crt_iface_name2idx(lo) PASSED\n");

	rc = crt_context_create_on_iface(cur_iface_str, &c1);
	D_ASSERTF(rc == 0, "crt_context_create_on_iface(%s) failed", cur_iface_str);
	DBG_PRINT("crt_context_create_on_iface(%s) PASSED\n", cur_iface_str);

	rc = crt_context_create_on_iface_idx(1, &c2);
	D_ASSERTF(rc == 0, "crt_context_create_on_iface_idx(1) failed");
	DBG_PRINT("crt_context_create_on_iface_idx(1) PASSED\n");

	rc = crt_context_uri_get(c1, &uri1);
	D_ASSERTF(rc == 0, "crt_context_uri_get(c1) failed");
	DBG_PRINT("c1(nic=%s) uri=%s\n", cur_iface_str, uri1);

	rc = crt_context_uri_get(c2, &uri2);
	D_ASSERTF(rc == 0, "crt_context_uri_get(c2) failed");
	DBG_PRINT("c2(nic=lo) uri=%s\n", uri2);

	D_FREE(cur_iface_str);
	D_FREE(new_iface_str);
	D_FREE(cur_domain_str);
	D_FREE(new_domain_str);
	D_FREE(uri1);
	D_FREE(uri2);

	rc = crt_context_destroy(c1, false);
	D_ASSERTF(rc == 0, "crt_context_destroy(c1) failed");

	rc = crt_context_destroy(c2, false);
	D_ASSERTF(rc == 0, "crt_context_destroy(c2) failed");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed");
	DBG_PRINT("Multi-interface context tests PASSED\n");

	/* Test CXI as CaRT treats interface differently for it from other providers */
	DBG_PRINT("Multi-interface tests, stage 2\n");
	setenv("D_PROVIDER", "ofi+cxi", 1);
	setenv("D_INTERFACE", "hsn0,hsn1,hsn2,hsn3,hsn4,hsn5,hsn6,hsn7", 1);
	setenv("D_DOMAIN", "cxi0,cxi1,cxi2,cxi3,cxi4,cxi5,cxi6,cxi7", 1);

	/* Reinitialize as a client to be able to use multi-interface APIs */
	rc = crt_init(0, 0);
	D_ASSERTF(rc == 0, "crt_init() failed; rc=%d\n", rc);

	/* Test multi-interface APIs */
	num_ifaces = crt_num_ifaces_get();
	D_ASSERTF(num_ifaces == 8, "expected 8, got %d interafces\n", num_ifaces);

	rc = crt_iface_name2idx("hsn4", &iface_idx);
	D_ASSERTF(rc == 0, "crt_iface_name2idx() failed; rc=%d\n", rc);
	D_ASSERTF(iface_idx == 4, "Expected index 4, got %d\n", iface_idx);

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed");

	DBG_PRINT("Multi-interface tests, stage 2 PASSED\n");
	d_log_fini();

	return 0;
}
