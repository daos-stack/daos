/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
#include "tests_common.h"

#define NUM_CTX 8
#define NUM_RANKS 99

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

	rc = crt_init(0, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	tc_test_init(0, 20, true, true);

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
				tc_progress_fn, &crt_ctx[i]);
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
	tc_progress_stop();

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

