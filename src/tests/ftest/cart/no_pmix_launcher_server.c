/*
 * (C) Copyright 2018-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Server utilizing crt_launch generated environment for NO-PMIX mode
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
#include "no_pmix_launcher_common.h"

int main(int argc, char **argv)
{
	crt_group_t         *grp;
	crt_context_t        crt_ctx[NUM_SERVER_CTX];
	pthread_t            progress_thread[NUM_SERVER_CTX];
	struct test_options *opts = crtu_get_opts();
	int                  i;
	char                *my_uri;
	char                *env;
	d_rank_t             my_rank;
	uint32_t             grp_size;
	int                  rc;

	rc = d_getenv_uint32_t(&my_rank, "CRT_L_RANK");
	if (rc != -DER_SUCCESS) {
		printf("CRT_L_RANK can not be retrieve: " DF_RC "\n", DP_RC(rc));
		return -1;
	}

	/* rank, num_attach_retries, is_server, assert_on_error */
	crtu_test_init(my_rank, 20, true, true);

	rc = d_log_init();
	assert(rc == 0);

	DBG_PRINT("Server starting up\n");
	rc = crt_init("server_grp", CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	if (rc != 0) {
		D_ERROR("crt_init() failed; rc=%d\n", rc);
		assert(0);
	}

	grp = crt_group_lookup(NULL);
	if (!grp) {
		D_ERROR("Failed to lookup group\n");
		assert(0);
	}

	rc = crt_rank_self_set(my_rank, 1 /* group_version_min */);
	if (rc != 0) {
		D_ERROR("crt_rank_self_set(%d) failed; rc=%d\n",
			my_rank, rc);
		assert(0);
	}

	rc = crt_context_create(&crt_ctx[0]);
	if (rc != 0) {
		D_ERROR("crt_context_create() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = pthread_create(&progress_thread[0], 0,
			    crtu_progress_fn, &crt_ctx[0]);
	if (rc != 0) {
		D_ERROR("pthread_create() failed; rc=%d\n", rc);
		assert(0);
	}

	if (opts->is_swim_enabled) {
		rc = crt_swim_init(0);
		if (rc != 0) {
			D_ERROR("crt_swim_init() failed; rc=%d\n", rc);
			assert(0);
		}
	}

	rc = d_agetenv_str(&env, "CRT_L_GRP_CFG");
	if (env == NULL) {
		D_ERROR("CRT_L_GRP_CFG can not be retrieve: " DF_RC "\n", DP_RC(rc));
		assert(0);
	}
	D_DEBUG(DB_TEST, "Group Config File: %s\n", env);

	rc = crt_rank_uri_get(grp, my_rank, 0, &my_uri);
	if (rc != 0) {
		D_ERROR("crt_rank_uri_get() failed; rc=%d\n", rc);
		assert(0);
	}

	/* load group info from a config file and delete file upon return */
	rc = crtu_load_group_from_file(env, crt_ctx[0], grp, my_rank, true);
	if (rc != 0) {
		D_ERROR("crtu_load_group_from_file() failed; rc=%d\n", rc);
		assert(0);
	}

	DBG_PRINT("self_rank=%d uri=%s grp_cfg_file=%s\n", my_rank, my_uri, env);
	D_FREE(env);
	D_FREE(my_uri);

	rc = crt_group_size(NULL, &grp_size);
	if (rc != 0) {
		D_ERROR("crt_group_size() failed; rc=%d\n", rc);
		assert(0);
	}

	rc = crt_proto_register(&my_proto_fmt);
	if (rc != 0) {
		D_ERROR("crt_proto_register() failed; rc=%d\n", rc);
		assert(0);
	}

	for (i = 1; i < NUM_SERVER_CTX; i++) {
		rc = crt_context_create(&crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("crt_context_create() failed; rc=%d\n", rc);
			assert(0);
		}
	}

	for (i = 1; i < NUM_SERVER_CTX; i++) {
		rc = pthread_create(&progress_thread[i], 0,
				    crtu_progress_fn, &crt_ctx[i]);
		if (rc != 0) {
			D_ERROR("pthread_create() failed; rc=%d\n", rc);
			assert(0);
		}
	}

	/* Wait until shutdown is issued and progress threads exit */
	for (i = 0; i < NUM_SERVER_CTX; i++)
		pthread_join(progress_thread[i], NULL);

	rc = crt_finalize();
	if (rc != 0) {
		D_ERROR("crt_finalize() failed with rc=%d\n", rc);
		assert(0);
	}

	d_log_fini();

	return 0;
}

